// Netplay's session: two to four players in lockstep over UDP, through the
// host (session.h). Ported from the Rage Wars port (wootbeer/trwrecomp,
// src/main/netplay.cpp) without its Controller Pak sharing: online nobody has
// a pak, so nothing is read from or written to anyone's saves.
//
// Each PC runs the whole game in the runtime's deterministic mode; at each
// sweep of the game's controller reads, each sends its own player's read
// (buttons and stick, after its own controls and mouse) for the read kDelay
// sweeps on, and waits for the others' for this one, so every game gets the
// same input at the same point and does the same thing.
//
// Started from the launcher's Netplay (later): the host waits on a UDP port;
// the joiner says Hello, gets the host's settings back (Welcome), and once
// they've read them and accepted says Ready, and the games start. Or, for
// testing, BTGA_NETPLAY=host:<udp port> (player 1) or
// join:<address>:<udp port>, at launch (BTGA_NETPLAYERS: how many the host
// waits for, 2 by default). BTGA_NETDELAY=<sweeps> sets the input delay.
//
// The input delay: the host picks a number of sweeps, or Auto. With Auto the
// joiner times Ping and Pong packets while it reads the host's settings and
// picks the smallest delay that covers half the round trip and its jitter.
//
// Join codes (tools/matchmaker): the host asks a STUN server what its UDP
// socket looks like from the internet, and gives that and its address on its
// own network to the join code service for a code. The joiner, with the
// code, gives its own and gets the host's; the host learns the joiner's by
// asking every so often. Then both send to all of the other's addresses
// (Punch from the host, Hello from the joiner) until one gets through. No
// game traffic goes through the service.
//
// Every kHashEvery sweeps both send a hash of the game's memory
// (sync_test.cpp) and a difference is reported once (a desync).
//
// Chat: a joiner's message goes to the host, which shows it and passes it on
// to the other joiners; the host's goes to each joiner. Each is sent again
// every kChatResend until its ChatAck comes, and known by its sender's port
// and number so a second copy isn't shown.
//
// What a peer can send is checked before use: a joiner is known by an id
// only it and the host have (never passed on); a port's reads only from its
// own player; reads only for sweeps not yet played and not far ahead; every
// value made sane (the stick finite and in range, the host's settings ones
// the menus offer); names, version labels and chat text printable ASCII and
// short; chat rate-limited per player on every PC.
#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <unordered_map>
#include <mutex>
#include <random>
#include <set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kNoSocket = INVALID_SOCKET;
static constexpr DWORD kUdpConnReset = _WSAIOW(IOC_VENDOR, 12); // SIO_UDP_CONNRESET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kNoSocket = -1;
#endif

#include "http.h"
#include "messages.h"
#include "session.h"
#include "settings.h"
#include "sync_test.h"
#include "upnp.h"
#include "ultramodern/ultramodern.hpp"

namespace {
    using btga::netplay::PortRead;
    using btga::netplay::SharedSettings;

    constexpr uint32_t kMagic = 0x41475442; // "BTGA"
    constexpr int kResend = 16;              // sweeps repeated in each packet
    constexpr uint32_t kHashEvery = 120;
    constexpr auto kTimeout = std::chrono::seconds(20);
    constexpr double kSweepMs = 1000.0 / 30.0; // the game reads the controllers at about 30 a second
    constexpr double kMarginMs = 8.0;
    constexpr uint32_t kMaxDelay = 8;
    constexpr int kMaxPlayers = 4;

    enum PacketType : uint32_t {
        Input = 0,   // reads and hashes (the host's own, or another player's passed on)
        Hello = 1,   // a joiner, until the host answers
        Welcome = 2, // the host's answer: its settings, and the joiner's port
        Ready = 3,   // a joiner has accepted the settings and waits for Start
        Leave = 4,   // a joiner has turned them down, or a player quit the game
        Ping = 5,    // a time (hash), sent back as it is
        Pong = 6,
        Punch = 7,   // the host, to a joiner's addresses: opens its router's way in
        Start = 8,   // the host has started: the joiner's port, the players, the delay
        Chat = 9,    // a message (in reads): port its player's, first its number, count its length
        ChatAck = 10, // a Chat came: its port and first
    };

    struct Packet {
        uint32_t magic;
        uint32_t type;       // PacketType
        uint32_t version;    // the game's build (all must be the same)
        uint32_t port;       // whose reads (Input), else the sender's port
        SharedSettings settings; // Welcome, Start: the host's
        uint32_t first;      // the sweep of reads[0]; Welcome: the host's input delay (0 Auto); Start: the session's
        uint32_t count;
        PortRead reads[kResend];
        uint32_t hash_sweep; // UINT32_MAX: none
        uint32_t place;      // with a hash: where the game is (0 for now)
        uint64_t hash;
        uint32_t assigned;   // Welcome, Start: the joiner's port (0: the game is full or has started)
        uint32_t players;    // Start: how many play
        uint32_t dropped[kMaxPlayers]; // the host's: from which sweep a port gets no input (UINT32_MAX: never)
        uint64_t sender;     // a joiner's id (0 from the host), as it may come from more than one address
    };

    // A player connected to this PC: the host's joiners (by port), or the
    // joiner's host (port 0).
    struct Peer {
        bool used = false;
        uint64_t id = 0;      // the joiner's, in its Hello (one joiner may come from several addresses)
        sockaddr_storage addr{};
        socklen_t len = 0;
        bool ready = false;   // the host: this joiner has accepted the settings
        std::string nickname; // the host: its Hello's
        bool started = false; // the host: this joiner's game has started (its Input came)
        bool lost = false;    // nothing from them for kTimeout, or they quit
        bool left = false;    // they quit
        std::vector<int64_t> trips; // round trips, microseconds (under round_trip_mutex)
        std::chrono::steady_clock::time_point last_heard, last_ping;
        bool has_input = false;
        Packet last_input{};  // the host: their latest Input, passed on again with each send
    };

    bool checked = false;
    bool enabled = false;   // the session has started (the game plays online)
    btga::netplay::Lobby lobby = btga::netplay::Lobby::Idle;
    std::string lobby_error;
    SharedSettings welcome_settings{}; // the joiner: the host's
    std::chrono::steady_clock::time_point lobby_started, last_hello, last_ready;
    bool welcomed = false;     // the joiner has the host's settings
    bool accepted = false;     // the joiner has accepted them (Ready sent)
    bool typed_address = false; // the joiner typed the host's address (so it may be shown)
    uint64_t joiner_id = 0;     // the joiner's, made up when it joins
    bool alone = false;        // the joiner: the host is gone, so everyone is (playing on alone)
    std::atomic<bool> holding_screen{ false }; // waiting for another player's input
    uint32_t game_version = 0; // a hash of the version string
    std::string version_label;  // "0.1.7 (1a2b3c4)", told to a player on another one
    std::string version_note;   // the host: someone on another version tried to join
    bool reported_version = false;
    int local = 0;
    int player_count = 2;           // how many play (ports 0 to players - 1)
    int env_players = 0;       // BTGA_NETPLAYERS's host: start once this many are in
    uint32_t delay = 3;
    uint32_t delay_choice = 0;  // the host's: sweeps, 0 Auto
    uint32_t welcome_delay = 0; // the joiner: the host's choice
    Peer peers[kMaxPlayers];
    uint32_t dropped[kMaxPlayers] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    uint32_t newest_read[kMaxPlayers] = {}; // the host: the latest sweep each port's reads came for, + 1

    std::mutex round_trip_mutex;
    constexpr size_t kRoundTrips = 24;

    // Chat messages on their way (under chat_mutex), each to one PC.
    constexpr auto kChatResend = std::chrono::milliseconds(250);
    constexpr int kChatTries = 40;
    constexpr size_t kChatRate = 12; // the host passes on at most this many of a player's in kChatWindow
    constexpr auto kChatWindow = std::chrono::seconds(30);
    struct ChatOut {
        int to;        // port
        int from;      // whose message
        uint32_t id;
        std::string text;
        int tries = 0;
        std::chrono::steady_clock::time_point last{};
    };
    std::mutex chat_mutex;
    std::vector<ChatOut> chat_out;
    uint32_t next_chat_id = 1;
    std::set<std::pair<int, uint32_t>> chat_seen; // (port, id) shown already
    std::vector<std::chrono::steady_clock::time_point> chat_times[kMaxPlayers]; // taken, by whose
    struct ChatPending {
        uint32_t id;
        std::string text;
    };
    std::vector<ChatPending> chat_pending; // this PC's, sent from the game's thread (send_chats)

    // Join codes. The service's address (tools/matchmaker, on the owner's
    // Cloudflare account): BTGA_MATCHMAKER, or this. Empty: none, so players
    // join by address.
    constexpr const char* kMatchmaker = "https://btga-match.wootbeer.workers.dev";
    constexpr uint32_t kStunCookie = 0x2112A442;
    struct Getting; // a join code being got, off the UI's thread
    struct Asking;
    std::shared_ptr<Getting> getting;
    bool host_public = false;      // the host: listed for anyone (the service's /list)
    std::string host_name;         // its lobby's name there
    std::shared_ptr<Asking> listing; // the public games being asked for
    std::vector<btga::netplay::PublicGame> public_list;
    btga::netplay::ListState list_state = btga::netplay::ListState::Idle;
    std::string code;              // the host's, once got
    std::string code_token;        // the host's, to ask who joined and to close it
    std::string code_note;         // the host: how getting a code went
    std::vector<sockaddr_in> candidates; // addresses to try: the host's (joiner), the joiners' (host)
    std::chrono::steady_clock::time_point last_punch, last_code_poll;
    std::shared_ptr<Asking> asking; // the host's "has anyone joined?"
    socket_t sock = kNoSocket;

    uint32_t sweep = 0; // controller read sweeps so far
    std::unordered_map<uint32_t, PortRead> local_reads, remote_reads[kMaxPlayers];
    std::unordered_map<uint32_t, uint64_t> local_hashes, remote_hashes[kMaxPlayers];
    std::unordered_map<uint32_t, uint32_t> local_places, remote_places[kMaxPlayers];
    // After a desync: checks in a row where a player's game was somewhere
    // else entirely (another mode or arena); kApartChecks of them (about
    // 12 s) are reported.
    constexpr int kApartChecks = 3;
    int apart_checks[kMaxPlayers] = {};
    bool reported_apart = false;
    uint32_t last_local_hash = UINT32_MAX;
    bool reported_desync = false;

    // Nicknames: this PC's player's, and the session's by port (the host's
    // Start). Up to kNameLength printable ASCII characters.
    constexpr size_t kNameLength = 12;
    std::string own_nickname;
    std::string port_names[4];

    template <typename... Args>
    void net_log(const char* fmt, Args... args) {
        std::fprintf(stderr, "[NET] ");
        std::fprintf(stderr, fmt, args...);
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
    }

    uint64_t new_joiner_id() {
        std::random_device random;
        uint64_t id = ((uint64_t)random() << 32) ^ random() ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
        return id != 0 ? id : 1; // 0 is the host's
    }

    bool net_init() {
#ifdef _WIN32
        static bool started = false;
        if (!started) {
            WSADATA wsa;
            started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
        }
        return started;
#else
        return true;
#endif
    }

    void close_socket() {
        if (sock != kNoSocket) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            sock = kNoSocket;
        }
    }

    void set_nonblocking(socket_t s) {
#ifdef _WIN32
        u_long on = 1;
        ioctlsocket(s, FIONBIO, &on);
        // Windows turns an ICMP "port unreachable" (an address of the other
        // player's that doesn't answer) into an error on the next receive.
        BOOL report = FALSE;
        DWORD bytes = 0;
        WSAIoctl(s, kUdpConnReset, &report, sizeof(report), nullptr, 0, &bytes, nullptr, nullptr);
#else
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    }

    bool same_address(const sockaddr_storage& a, const sockaddr_storage& b) {
        if (a.ss_family != b.ss_family) {
            return false;
        }
        if (a.ss_family == AF_INET) {
            const auto& x = reinterpret_cast<const sockaddr_in&>(a);
            const auto& y = reinterpret_cast<const sockaddr_in&>(b);
            return x.sin_port == y.sin_port && std::memcmp(&x.sin_addr, &y.sin_addr, sizeof(x.sin_addr)) == 0;
        }
        if (a.ss_family == AF_INET6) {
            const auto& x = reinterpret_cast<const sockaddr_in6&>(a);
            const auto& y = reinterpret_cast<const sockaddr_in6&>(b);
            return x.sin6_port == y.sin6_port && std::memcmp(&x.sin6_addr, &y.sin6_addr, sizeof(x.sin6_addr)) == 0;
        }
        return false;
    }

    // The peer a packet came from (-1: none).
    int peer_from(const sockaddr_storage& from) {
        for (int port = 0; port < kMaxPlayers; port++) {
            if (peers[port].used && same_address(peers[port].addr, from)) {
                return port;
            }
        }
        return -1;
    }

    void send_to(const Packet& packet, const Peer& to) {
        sendto(sock, reinterpret_cast<const char*>(&packet), sizeof(packet), 0, reinterpret_cast<const sockaddr*>(&to.addr), to.len);
    }

    Packet make_packet(PacketType type) {
        Packet packet{};
        packet.magic = kMagic;
        packet.type = type;
        packet.version = game_version;
        packet.port = (uint32_t)local;
        packet.hash_sweep = UINT32_MAX;
        for (int port = 0; port < kMaxPlayers; port++) {
            packet.dropped[port] = dropped[port];
        }
        packet.sender = local == 0 ? 0 : joiner_id;
        return packet;
    }

    // The host: a joiner counts as ready once it has accepted the settings.
    bool peer_ready(const Peer& peer) {
        return peer.ready;
    }

    // The host, at the start: each port's nickname, for the Start.
    void set_port_names() {
        port_names[0] = own_nickname;
        for (int port = 1; port < kMaxPlayers; port++) {
            port_names[port] = port < player_count ? peers[port].nickname : "";
        }
    }

    // In a Start (in reads): each port's nickname.
    struct StartNames {
        char names[4][16];
    };

    // A character a name may have: letters, digits, spaces and a little
    // punctuation (no markup, line breaks or other scripts).
    bool name_char(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            (c != '\0' && std::strchr(" '!?.,:()_-", c) != nullptr);
    }

    // A nickname as it may be shown: name_char's but no spaces, kNameLength
    // at most.
    std::string clean_nickname(const char* text, size_t length) {
        std::string name;
        for (size_t i = 0; i < length && text[i] != '\0' && name.size() < kNameLength; i++) {
            char c = text[i];
            if (name_char(c) && c != ' ') {
                name.push_back(c);
            }
        }
        return name;
    }

    void put_name(char* to, const std::string& name) {
        std::memset(to, 0, 16);
        std::memcpy(to, name.data(), std::min(name.size(), kNameLength));
    }

    // A version label from another game (in a Hello or Welcome's reads):
    // letters, digits, spaces and .()- only, 31 characters at most.
    constexpr size_t kLabelAt = 16, kLabelSize = 32; // after the nickname
    std::string clean_label(const char* text, size_t length) {
        std::string label;
        for (size_t i = 0; i < length && text[i] != '\0' && label.size() < kLabelSize - 1; i++) {
            char c = text[i];
            if (std::isalnum((unsigned char)c) || std::strchr(" .()-", c) != nullptr) {
                label.push_back(c);
            }
        }
        return label.empty() ? "an unknown version" : label;
    }

    void put_label(Packet& packet) {
        char* to = reinterpret_cast<char*>(packet.reads) + kLabelAt;
        std::memset(to, 0, kLabelSize);
        std::memcpy(to, version_label.data(), std::min(version_label.size(), kLabelSize - 1));
    }

    std::string label_in(const Packet& packet) {
        return clean_label(reinterpret_cast<const char*>(packet.reads) + kLabelAt, kLabelSize);
    }

    // What a joiner is told when the host is on another version.
    std::string version_clash_text(const std::string& host_label) {
        return "The host has version " + host_label + "; you have " + version_label + ". Everyone needs the same one: "
            "whoever has the older one can get the latest at github.com/wootbeer/btgarecomp/releases.";
    }

    // A Chat's text goes where an Input's reads do.
    static_assert(sizeof(Packet::reads) > btga::netplay::kChatMaxLength);
    char* chat_text(Packet& packet) {
        return reinterpret_cast<char*>(packet.reads);
    }

    // The messages on their way, sent again until they're answered.
    void send_chats() {
        std::lock_guard lock{ chat_mutex };
        for (const ChatPending& pending : chat_pending) {
            chat_seen.insert({ local, pending.id });
            for (int to = 0; to < player_count; to++) {
                if (to != local && (local == 0 || to == 0) && peers[to].used && !peers[to].lost) {
                    chat_out.push_back(ChatOut{ to, local, pending.id, pending.text });
                }
            }
        }
        chat_pending.clear();
        auto now = std::chrono::steady_clock::now();
        for (auto it = chat_out.begin(); it != chat_out.end();) {
            const Peer& peer = peers[it->to];
            if (!peer.used || peer.lost || it->tries >= kChatTries) {
                it = chat_out.erase(it);
                continue;
            }
            if (now - it->last >= kChatResend) {
                Packet packet = make_packet(Chat);
                packet.port = (uint32_t)it->from;
                packet.first = it->id;
                packet.count = (uint32_t)it->text.size();
                std::memcpy(chat_text(packet), it->text.data(), it->text.size());
                send_to(packet, peer);
                it->tries++;
                it->last = now;
            }
            ++it;
        }
    }

    // A Chat from `peer` (on `peer_port`): answered, shown if it's new, and
    // passed on by the host.
    void receive_chat(Packet& packet, const Peer& peer, int peer_port) {
        int from = (int)packet.port;
        if (from < 0 || from >= kMaxPlayers || packet.count > btga::netplay::kChatMaxLength || (local == 0 && from != peer_port)) {
            return; // the host takes a joiner's own only
        }
        Packet ack = make_packet(ChatAck);
        ack.port = (uint32_t)from;
        ack.first = packet.first;
        send_to(ack, peer);
        std::string text(chat_text(packet), packet.count);
        {
            std::lock_guard lock{ chat_mutex };
            if (chat_seen.count({ from, packet.first }) != 0) {
                return;
            }
            // More than the game sends is dropped, on every PC, before it's
            // remembered (so the list stays small).
            auto now = std::chrono::steady_clock::now();
            auto& times = chat_times[from];
            std::erase_if(times, [&](auto t) { return now - t >= kChatWindow; });
            if (times.size() >= kChatRate) {
                return;
            }
            times.push_back(now);
            if (chat_seen.size() >= 4096) {
                chat_seen.clear();
            }
            chat_seen.insert({ from, packet.first });
            if (local == 0) {
                for (int to = 1; to < player_count; to++) {
                    if (to != from && peers[to].used && !peers[to].lost) {
                        chat_out.push_back(ChatOut{ to, from, packet.first, text });
                    }
                }
            }
        }
        net_log("chat: a message from player %d", from + 1);
        btga::netplay::chat_add_line(from, text);
    }

    void receive_chat_ack(const Packet& packet, int peer_port) {
        std::lock_guard lock{ chat_mutex };
        std::erase_if(chat_out, [&](const ChatOut& out) {
            return out.to == peer_port && out.from == (int)packet.port && out.id == packet.first;
        });
    }

    bool open_socket(const std::string& mode, const std::string& address, const std::string& port) {
        if (!net_init()) {
            return false;
        }
        addrinfo hints{};
        hints.ai_family = AF_INET; // the host listens on IPv4
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* found = nullptr;
        if (mode == "host") {
            hints.ai_flags = AI_PASSIVE;
            if (getaddrinfo(nullptr, port.c_str(), &hints, &found) != 0) {
                return false;
            }
            sock = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
            if (sock == kNoSocket || bind(sock, found->ai_addr, (int)found->ai_addrlen) != 0) {
                freeaddrinfo(found);
                close_socket();
                return false;
            }
        }
        else {
            if (getaddrinfo(address.c_str(), port.c_str(), &hints, &found) != 0) {
                return false;
            }
            sock = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
            if (sock == kNoSocket) {
                freeaddrinfo(found);
                return false;
            }
            Peer& host = peers[0];
            host = Peer{};
            host.used = true;
            std::memcpy(&host.addr, found->ai_addr, found->ai_addrlen);
            host.len = (socklen_t)found->ai_addrlen;
            host.last_heard = std::chrono::steady_clock::now();
        }
        freeaddrinfo(found);
        set_nonblocking(sock);
        return true;
    }

    void check() {
        if (checked) {
            return;
        }
        checked = true;
        const char* spec = std::getenv("BTGA_NETPLAY");
        if (spec == nullptr || spec[0] == '\0') {
            return;
        }
        std::string s = spec;
        std::string mode = s.substr(0, s.find(':'));
        std::string rest = s.find(':') == std::string::npos ? "" : s.substr(s.find(':') + 1);
        std::string address, port;
        if (mode == "host") {
            port = rest;
            local = 0;
        }
        else if (mode == "join") {
            address = rest.substr(0, rest.rfind(':'));
            port = rest.substr(rest.rfind(':') + 1);
            local = 1;
        }
        else {
            net_log("BTGA_NETPLAY: host:<port> or join:<address>:<port>");
            return;
        }
        if (const char* n = std::getenv("BTGA_NICKNAME"); n != nullptr) {
            own_nickname = clean_nickname(n, std::strlen(n));
        }
        if (const char* d = std::getenv("BTGA_NETDELAY"); d != nullptr && d[0] != '\0') {
            delay = (uint32_t)std::max(1, std::atoi(d));
        }
        delay_choice = delay; // the host's, fixed
        if (!open_socket(mode, address, port)) {
            net_log("can't open the UDP socket (%s)", spec);
            return;
        }
        if (mode == "host") {
            // The joiners come in once the game runs (it waits for them), and
            // it starts once all have; the settings are taken then (the
            // config isn't loaded yet).
            const char* n = std::getenv("BTGA_NETPLAYERS");
            env_players = std::clamp(n != nullptr ? std::atoi(n) : 2, 2, kMaxPlayers);
            player_count = env_players;
            net_log("hosting for %d players, input delay %u", player_count, delay);
            ultramodern::set_deterministic(true);
            enabled = true;
            lobby = btga::netplay::Lobby::Waiting;
        }
        else {
            net_log("joining, input delay set by the host");
            joiner_id = new_joiner_id();
            lobby = btga::netplay::Lobby::Waiting; // start() waits for the Start
        }
    }

    // This player's reads, for the sweeps up to `delay` on.
    Packet input_packet() {
        Packet packet = make_packet(Input);
        uint32_t newest = sweep + delay; // the latest read made
        packet.first = newest + 1 >= kResend ? newest + 1 - kResend : 0;
        packet.count = newest + 1 - packet.first;
        for (uint32_t i = 0; i < packet.count; i++) {
            auto found = local_reads.find(packet.first + i);
            packet.reads[i] = found != local_reads.end() ? found->second : PortRead{};
        }
        if (last_local_hash != UINT32_MAX) {
            packet.hash_sweep = last_local_hash;
            packet.hash = local_hashes[last_local_hash];
            packet.place = local_places[last_local_hash];
        }
        return packet;
    }

    // Each sweep (and while waiting): the joiner's reads to the host; the
    // host's to each joiner, with every other joiner's latest passed on.
    void send_inputs() {
        Packet mine = input_packet();
        if (local != 0) {
            if (peers[0].used && !alone) {
                send_to(mine, peers[0]);
            }
            return;
        }
        for (int to = 1; to < player_count; to++) {
            Peer& peer = peers[to];
            if (!peer.used || peer.lost) {
                continue;
            }
            send_to(mine, peer);
            for (int from = 1; from < player_count; from++) {
                if (from != to && peers[from].has_input) {
                    Packet passed = peers[from].last_input;
                    for (int port = 0; port < kMaxPlayers; port++) {
                        passed.dropped[port] = dropped[port];
                    }
                    send_to(passed, peer);
                }
            }
        }
    }

    // The host's settings for the session (settings.h).
    SharedSettings session_settings() {
        return btga::netplay::own_settings();
    }

    void send_control(PacketType type, const sockaddr_storage* to, socklen_t to_len, uint32_t assigned = 0) {
        Packet packet = make_packet(type);
        packet.assigned = assigned;
        if (type == Welcome) {
            packet.settings = session_settings();
            packet.first = delay_choice;
            put_label(packet);
        }
        else if (type == Start) {
            // The settings again (the same), and each port's nickname.
            packet.settings = session_settings();
            packet.first = delay;
            packet.players = (uint32_t)player_count;
            static_assert(sizeof(Packet::reads) >= sizeof(StartNames));
            StartNames names{};
            for (int port = 0; port < kMaxPlayers; port++) {
                put_name(names.names[port], port_names[port]);
            }
            std::memcpy(packet.reads, &names, sizeof(names));
        }
        else if (type == Hello) {
            put_name(reinterpret_cast<char*>(packet.reads), own_nickname);
            put_label(packet);
        }
        sendto(sock, reinterpret_cast<const char*>(&packet), sizeof(packet), 0, reinterpret_cast<const sockaddr*>(to), to_len);
    }

    void send_control(PacketType type, const Peer& to, uint32_t assigned = 0) {
        send_control(type, &to.addr, to.len, assigned);
    }

    int64_t now_us() {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void send_ping(Peer& to) {
        Packet packet = make_packet(Ping);
        packet.hash = (uint64_t)now_us();
        send_to(packet, to);
        to.last_ping = std::chrono::steady_clock::now();
    }

    // A peer's round trips so far, sorted.
    std::vector<int64_t> sorted_round_trips(const Peer& peer) {
        std::lock_guard lock{ round_trip_mutex };
        std::vector<int64_t> sorted = peer.trips;
        std::sort(sorted.begin(), sorted.end());
        return sorted;
    }

    // Auto (the host, at Start): the sweeps a read needs to reach every
    // other player in time, at the game's pace (a sweep each kSweepMs). A
    // joiner's reads reach another joiner through the host: half of each
    // one's usual round trip, plus how much the round trips vary, plus a
    // margin (and half a sweep for the host passing them on). 3 if no Pong
    // came back.
    uint32_t auto_delay() {
        std::vector<double> halves;
        double jitter = 0.0;
        for (int port = 1; port < player_count; port++) {
            std::vector<int64_t> trips = sorted_round_trips(peers[port]);
            if (trips.empty()) {
                return 3;
            }
            double median = (double)trips[trips.size() / 2] / 1000.0;
            double high = (double)trips[std::min(trips.size() - 1, trips.size() * 9 / 10)] / 1000.0;
            halves.push_back(median / 2.0);
            jitter = std::max(jitter, high - median);
        }
        if (halves.empty()) {
            return 3;
        }
        std::sort(halves.rbegin(), halves.rend());
        double needed = halves[0] + jitter + kMarginMs;
        if (halves.size() >= 2) {
            needed += halves[1] + kSweepMs / 2.0;
        }
        return (uint32_t)std::clamp((int)std::ceil(needed / kSweepMs), 1, (int)kMaxDelay);
    }

    std::string matchmaker() {
        const char* env = std::getenv("BTGA_MATCHMAKER");
        std::string url = env != nullptr && env[0] != '\0' ? env : kMatchmaker;
        while (!url.empty() && url.back() == '/') {
            url.pop_back();
        }
        return url;
    }

    std::string address_text(const sockaddr_in& a) {
        char buf[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf)) == nullptr) {
            return "-";
        }
        return std::string(buf) + ":" + std::to_string(ntohs(a.sin_port));
    }

    bool parse_address(const std::string& text, sockaddr_in& a) {
        size_t colon = text.rfind(':');
        if (colon == std::string::npos) {
            return false;
        }
        a = {};
        a.sin_family = AF_INET;
        int port = std::atoi(text.c_str() + colon + 1);
        if (port < 1 || port > 65535 || inet_pton(AF_INET, text.substr(0, colon).c_str(), &a.sin_addr) != 1) {
            return false;
        }
        a.sin_port = htons((uint16_t)port);
        return true;
    }

    // The other player's addresses from the service's "ADDR LOCAL SEEN",
    // each once.
    std::vector<sockaddr_in> parse_candidates(const std::string& body) {
        std::vector<sockaddr_in> found;
        size_t start = 0;
        while (start < body.size()) {
            size_t end = body.find_first_of(" \r\n", start);
            std::string part = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
            sockaddr_in a;
            if (parse_address(part, a)) {
                bool again = std::any_of(found.begin(), found.end(), [&](const sockaddr_in& b) {
                    return b.sin_port == a.sin_port && b.sin_addr.s_addr == a.sin_addr.s_addr;
                });
                if (!again) {
                    found.push_back(a);
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return found;
    }

    bool is_candidate(const sockaddr_storage& from) {
        if (from.ss_family != AF_INET) {
            return false;
        }
        const auto& a = reinterpret_cast<const sockaddr_in&>(from);
        return std::any_of(candidates.begin(), candidates.end(), [&](const sockaddr_in& b) {
            return b.sin_port == a.sin_port && b.sin_addr.s_addr == a.sin_addr.s_addr;
        });
    }

    // What `s` looks like from the internet, from a STUN server ("-" if no
    // answer in about a second and a half). Other packets that come in
    // meanwhile are dropped (Hellos are sent again).
    std::string stun_address(socket_t s) {
        std::vector<std::pair<std::string, std::string>> servers = { { "stun.l.google.com", "19302" }, { "stun.cloudflare.com", "3478" } };
        // BTGA_STUN=<address>:<port>, for testing, instead.
        if (const char* test = std::getenv("BTGA_STUN"); test != nullptr && std::strchr(test, ':') != nullptr) {
            std::string t = test;
            servers = { { t.substr(0, t.rfind(':')), t.substr(t.rfind(':') + 1) } };
        }
        uint8_t request[20] = {};
        request[1] = 0x01; // Binding Request, no attributes
        uint32_t cookie = htonl(kStunCookie);
        std::memcpy(request + 4, &cookie, 4);
        std::random_device random;
        for (int i = 8; i < 20; i++) {
            request[i] = (uint8_t)(random() & 0xFF);
        }
        std::vector<sockaddr_storage> asked; // an answer counts only from these
        for (auto& server : servers) {
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            addrinfo* found = nullptr;
            if (getaddrinfo(server.first.c_str(), server.second.c_str(), &hints, &found) == 0 && found != nullptr) {
                sendto(s, reinterpret_cast<const char*>(request), sizeof(request), 0, found->ai_addr, (int)found->ai_addrlen);
                sockaddr_storage a{};
                std::memcpy(&a, found->ai_addr, std::min(sizeof(a), (size_t)found->ai_addrlen));
                asked.push_back(a);
                freeaddrinfo(found);
            }
        }
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        while (std::chrono::steady_clock::now() < until) {
            uint8_t reply[600];
            sockaddr_storage from{};
            socklen_t from_len = sizeof(from);
            int got = recvfrom(s, reinterpret_cast<char*>(reply), sizeof(reply), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
            if (got < 20) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            // A Binding Success Response to ours, from a server asked.
            bool from_asked = std::any_of(asked.begin(), asked.end(), [&](const sockaddr_storage& a) { return same_address(a, from); });
            if (!from_asked || reply[0] != 0x01 || reply[1] != 0x01 || std::memcmp(reply + 4, request + 4, 16) != 0) {
                continue;
            }
            int length = (reply[2] << 8) | reply[3];
            for (int at = 20; at + 4 <= 20 + length && at + 4 <= got;) {
                int type = (reply[at] << 8) | reply[at + 1];
                int size = (reply[at + 2] << 8) | reply[at + 3];
                const uint8_t* value = reply + at + 4;
                if (at + 4 + size > got) {
                    break;
                }
                // XOR-MAPPED-ADDRESS (or the older MAPPED-ADDRESS), IPv4.
                if ((type == 0x0020 || type == 0x0001) && size >= 8 && value[1] == 0x01) {
                    uint16_t port = (uint16_t)((value[2] << 8) | value[3]);
                    uint32_t ip = ((uint32_t)value[4] << 24) | ((uint32_t)value[5] << 16) | ((uint32_t)value[6] << 8) | value[7];
                    if (type == 0x0020) {
                        port ^= (uint16_t)(kStunCookie >> 16);
                        ip ^= kStunCookie;
                    }
                    sockaddr_in a{};
                    a.sin_family = AF_INET;
                    a.sin_port = htons(port);
                    a.sin_addr.s_addr = htonl(ip);
                    return address_text(a);
                }
                at += 4 + ((size + 3) & ~3);
            }
        }
        return "-";
    }

    // This PC's address on its own network, with the socket's port.
    std::string local_address(socket_t s) {
        sockaddr_in me{};
        socklen_t len = sizeof(me);
        std::string lan = btga::netplay::lan_address();
        if (lan.empty() || getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) != 0) {
            return "-";
        }
        return lan + ":" + std::to_string(ntohs(me.sin_port));
    }

    std::string version_text() {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08x", game_version);
        return buf;
    }

    // Getting a join code (the host) or the host's addresses (the joiner),
    // on a thread of its own: the STUN request (the socket is this thread's
    // until `stunned`), then the service. The UI's thread takes the result.
    struct Getting {
        std::mutex mutex;
        bool stunned = false;
        bool done = false;
        bool ok = false;
        std::string body;  // the service's answer
        std::string error; // why not, for the player
    };

    void start_getting(bool hosting, const std::string& joining_code) {
        auto job = std::make_shared<Getting>();
        getting = job;
        socket_t s = sock;
        std::string base = matchmaker();
        std::string version = version_text();
        std::string listed = hosting && host_public ? "&public=1&name=" + btga::netplay::http::escape(host_name) : "";
        std::string label = version_label;
        std::thread([job, s, base, version, hosting, joining_code, listed, label] {
            std::string addr = stun_address(s);
            std::string local = local_address(s);
            {
                std::lock_guard lock{ job->mutex };
                job->stunned = true;
            }
            std::string url = base + (hosting ? "/host?" : "/join?code=" + btga::netplay::http::escape(joining_code) + "&") +
                "addr=" + btga::netplay::http::escape(addr) + "&local=" + btga::netplay::http::escape(local) + "&v=" + version + "&vn=" +
                btga::netplay::http::escape(label) + listed;
            std::string body;
            bool ok = btga::netplay::http::get(url, body);
            std::lock_guard lock{ job->mutex };
            job->done = true;
            job->ok = ok;
            job->body = body;
            if (!ok) {
                if (body.rfind("version", 0) == 0) {
                    job->error = body.size() > 8 ? version_clash_text(clean_label(body.c_str() + 8, body.size() - 8))
                                                 : "The host has a different version of the game. Everyone needs the same one.";
                }
                else if (body.rfind("no game", 0) == 0) {
                    job->error = "No game with that join code. Check it with the host (codes last 15 minutes).";
                }
                else if (body.rfind("full", 0) == 0) {
                    job->error = "That game is full.";
                }
                else {
                    job->error = "Couldn't reach the join code service. Check your internet connection, or join by address.";
                }
            }
            net_log("join code service: %s (%s, us %s / %s)", ok ? "answered" : "no", hosting ? "hosting" : "joining", addr.c_str(), local.c_str());
        }).detach();
    }

    // The thread is still using the socket.
    bool stunning() {
        if (getting == nullptr) {
            return false;
        }
        std::lock_guard lock{ getting->mutex };
        return !getting->stunned;
    }

    // A request whose answer doesn't matter much (has anyone joined; close
    // the code), off the UI's thread.
    struct Asking {
        std::mutex mutex;
        bool done = false;
        bool ok = false;
        std::string body;
    };

    std::shared_ptr<Asking> ask(const std::string& url) {
        auto job = std::make_shared<Asking>();
        std::thread([job, url] {
            std::string body;
            bool ok = btga::netplay::http::get(url, body);
            std::lock_guard lock{ job->mutex };
            job->done = true;
            job->ok = ok;
            job->body = body;
        }).detach();
        return job;
    }

    // The host's code is no longer needed (someone joined, or it stopped).
    void close_code() {
        if (!code.empty()) {
            ask(matchmaker() + "/close?code=" + code + "&token=" + btga::netplay::http::escape(code_token));
        }
        code.clear();
        code_token.clear();
        asking = nullptr;
    }

    // The host: a joiner's port, for one who hasn't got one (the lowest free;
    // 0 if the game is full or has started).
    int free_port() {
        if (lobby != btga::netplay::Lobby::Waiting) {
            return 0;
        }
        int limit = env_players != 0 ? env_players : kMaxPlayers;
        for (int port = 1; port < limit; port++) {
            if (!peers[port].used) {
                return port;
            }
        }
        return 0;
    }

    // A player is gone (quit, or nothing came for kTimeout): from the sweep
    // after the newest of their reads the host has (none of theirs can have
    // reached anyone after that, as only the host passes them on), every game
    // gives their port no input. The host says so in each packet.
    void drop_port(int port, bool quit) {
        if (dropped[port] != UINT32_MAX) {
            return;
        }
        Peer& peer = peers[port];
        peer.lost = true;
        peer.left = quit;
        dropped[port] = std::max(newest_read[port], sweep);
        net_log("player %d %s: no input for them from read sweep %u", port + 1, quit ? "left the game" : "was lost", dropped[port]);
        btga::netplay::show_message("Player " + std::to_string(port + 1) + (quit ? " left the game." : " lost the connection.") +
            "\nThe others play on.", 10);
    }

    // The joiner: the host is gone, so this game goes on alone.
    void lose_host(bool quit) {
        if (alone) {
            return;
        }
        alone = true;
        peers[0].lost = true;
        peers[0].left = quit;
        net_log("the host %s: playing on alone", quit ? "left the game" : "was lost");
        btga::netplay::show_message(std::string(quit ? "The host left the game." : "Lost the connection to the host.") +
            "\nYou're playing on without them.", 10);
    }

    void note_round_trip(Peer& peer, const Packet& packet) {
        int64_t trip = now_us() - (int64_t)packet.hash;
        if (trip >= 0 && trip < 5000000) {
            std::lock_guard lock{ round_trip_mutex };
            peer.trips.push_back(trip);
            if (peer.trips.size() > kRoundTrips) {
                peer.trips.erase(peer.trips.begin());
            }
        }
    }

    // Reads only for sweeps this game hasn't played yet and not far ahead of
    // it (no game gets more than the input delay ahead), so nothing a peer
    // sends can grow the maps or wrap the numbers. Each read as the game may
    // take it: the stick and the mouse's turn finite and in range (every PC
    // takes a peer's reads through here, so all get the same).
    constexpr uint32_t kReadsAhead = 256;

    PortRead sane_read(PortRead read) {
        auto clamp = [](float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : 0.0f; };
        read.response = read.response != 0 ? 1 : 0;
        read.buttons &= 0xFFFF;
        read.x = clamp(read.x, -1.0f, 1.0f);
        read.y = clamp(read.y, -1.0f, 1.0f);
        return read;
    }

    void take_reads(int port, const Packet& packet) {
        if (port < 0 || port >= kMaxPlayers || port == local) {
            return;
        }
        for (uint32_t i = 0; i < packet.count && i < (uint32_t)kResend; i++) {
            uint64_t at = (uint64_t)packet.first + i;
            if (at >= sweep && at < (uint64_t)sweep + kReadsAhead) {
                remote_reads[port].emplace((uint32_t)at, sane_read(packet.reads[i]));
                newest_read[port] = std::max(newest_read[port], (uint32_t)at + 1);
            }
        }
        if (packet.hash_sweep != UINT32_MAX && packet.hash_sweep % kHashEvery == 0 &&
            (uint64_t)packet.hash_sweep + 16 * kHashEvery >= sweep && packet.hash_sweep < (uint64_t)sweep + kReadsAhead) {
            remote_hashes[port][packet.hash_sweep] = packet.hash;
            remote_places[port][packet.hash_sweep] = packet.place;
        }
    }

    void receive_as_host(Packet& packet, const sockaddr_storage& from, socklen_t from_len) {
        // A joiner by its id, from whichever of its addresses: the latest is
        // where it's answered. The id is the joiner's own, never sent to the
        // others (passed-on packets carry none), so nobody else can take its
        // place.
        if (packet.sender == 0) {
            return;
        }
        int port = -1;
        for (int p = 1; p < kMaxPlayers && port < 0; p++) {
            if (peers[p].used && packet.sender != 0 && peers[p].id == packet.sender) {
                port = p;
                if (!same_address(peers[p].addr, from)) {
                    std::memcpy(&peers[p].addr, &from, from_len);
                    peers[p].len = from_len;
                }
            }
        }
        if (packet.type == Hello) {
            if (port < 0) {
                port = free_port();
                if (port == 0) {
                    send_control(Welcome, &from, from_len, 0); // full, or started
                    return;
                }
                Peer& peer = peers[port];
                peer = Peer{};
                peer.used = true;
                peer.id = packet.sender;
                std::memcpy(&peer.addr, &from, from_len);
                peer.len = from_len;
                peer.nickname = clean_nickname(reinterpret_cast<const char*>(packet.reads), 16);
                net_log("player %d connected", port + 1);
            }
            peers[port].last_heard = std::chrono::steady_clock::now();
            send_control(Welcome, peers[port], (uint32_t)port);
            return;
        }
        if (port < 0) {
            return; // only players who have said Hello
        }
        Peer& peer = peers[port];
        peer.last_heard = std::chrono::steady_clock::now();
        switch (packet.type) {
        case Ping:
            packet.type = Pong;
            packet.port = (uint32_t)local;
            send_to(packet, peer);
            return;
        case Pong:
            note_round_trip(peer, packet);
            return;
        case Chat:
            receive_chat(packet, peer, port);
            return;
        case ChatAck:
            receive_chat_ack(packet, port);
            return;
        case Ready:
            if (lobby == btga::netplay::Lobby::Waiting) {
                peer.ready = true;
            }
            else if (enabled && port < player_count) {
                send_control(Start, peer, (uint32_t)port); // they missed it
            }
            return;
        case Leave:
            if (lobby == btga::netplay::Lobby::Waiting && !enabled) {
                net_log("player %d left", port + 1);
                peer = Peer{};
            }
            else if (port < player_count) {
                drop_port(port, true);
            }
            else {
                peer = Peer{};
            }
            return;
        case Input:
            if (port >= player_count || packet.port != (uint32_t)port) {
                return; // a joiner's own reads only
            }
            peer.started = true;
            take_reads(port, packet);
            peer.last_input = packet;
            peer.has_input = true;
            // Straight on to the others.
            for (int to = 1; to < player_count; to++) {
                if (to != port && peers[to].used && !peers[to].lost) {
                    Packet passed = packet;
                    passed.sender = 0;
                    for (int p = 0; p < kMaxPlayers; p++) {
                        passed.dropped[p] = dropped[p];
                    }
                    send_to(passed, peers[to]);
                }
            }
            return;
        default:
            return;
        }
    }

    void receive_as_joiner(Packet& packet, const sockaddr_storage& from, socklen_t from_len) {
        Peer& host = peers[0];
        bool from_host = host.used && (same_address(host.addr, from) || (welcomed && is_candidate(from)));
        // By join code: the first of the host's addresses to answer.
        if (!from_host && packet.type == Welcome && !welcomed && is_candidate(from)) {
            host = Peer{};
            host.used = true;
            std::memcpy(&host.addr, &from, from_len);
            host.len = from_len;
            from_host = true;
            net_log("reached the host at %s", address_text(reinterpret_cast<const sockaddr_in&>(from)).c_str());
        }
        if (!from_host || (packet.port == (uint32_t)local && packet.type == Input)) {
            return;
        }
        host.last_heard = std::chrono::steady_clock::now();
        switch (packet.type) {
        case Welcome:
            if (packet.assigned == 0) {
                if (lobby == btga::netplay::Lobby::Waiting && !welcomed) {
                    lobby = btga::netplay::Lobby::Failed;
                    lobby_error = "The host's game is full, or has already started.";
                }
                return;
            }
            if (packet.assigned >= (uint32_t)kMaxPlayers) {
                return;
            }
            if (!welcomed) {
                local = (int)packet.assigned;
                welcome_settings = btga::netplay::sane_settings(packet.settings);
                welcome_delay = packet.first;
                welcomed = true;
                if (lobby == btga::netplay::Lobby::Waiting) {
                    lobby = btga::netplay::Lobby::Found;
                }
            }
            return;
        case Start:
            if (accepted && !enabled && lobby == btga::netplay::Lobby::Waiting && packet.assigned != 0 && packet.players >= 2 &&
                packet.players <= (uint32_t)kMaxPlayers && packet.assigned < packet.players) {
                // The settings must be the ones this player was shown and
                // accepted (the Welcome's): a host can't start with others.
                if (!btga::netplay::same_settings(btga::netplay::sane_settings(packet.settings), welcome_settings)) {
                    lobby = btga::netplay::Lobby::Failed;
                    lobby_error = "The host's settings changed after you joined. Join again to see the new ones.";
                    return;
                }
                local = (int)packet.assigned;
                player_count = (int)packet.players;
                delay = std::clamp(packet.first, 1u, kMaxDelay);
                StartNames names;
                std::memcpy(&names, packet.reads, sizeof(names));
                for (int port = 0; port < kMaxPlayers; port++) {
                    port_names[port] = port < player_count ? clean_nickname(names.names[port], 16) : "";
                }
                lobby = btga::netplay::Lobby::Go;
            }
            return;
        case Leave:
            if (enabled) {
                lose_host(true);
            }
            else {
                lobby = btga::netplay::Lobby::Failed;
                lobby_error = accepted ? "The host started without you, or stopped hosting." : "The host stopped hosting.";
            }
            return;
        case Ping:
            packet.type = Pong;
            packet.port = (uint32_t)local;
            packet.sender = joiner_id;
            send_to(packet, host);
            return;
        case Pong:
            note_round_trip(host, packet);
            return;
        case Chat:
            receive_chat(packet, host, 0);
            return;
        case ChatAck:
            receive_chat_ack(packet, 0);
            return;
        case Input:
            take_reads((int)packet.port, packet);
            // The host gave up on this player: they're on their own now.
            if (packet.dropped[local] != UINT32_MAX && enabled) {
                lose_host(false);
                return;
            }
            for (int port = 0; port < kMaxPlayers; port++) {
                if (packet.dropped[port] != UINT32_MAX && dropped[port] == UINT32_MAX && port != local) {
                    dropped[port] = packet.dropped[port];
                    peers[port].lost = true;
                    net_log("the host: player %d is gone, no input for them from read sweep %u", port + 1, dropped[port]);
                    btga::netplay::show_message("Player " + std::to_string(port + 1) + " is gone.\nThe others play on.", 10);
                }
            }
            return;
        default:
            return;
        }
    }

    void receive_packets() {
        Packet packet;
        sockaddr_storage from{};
        while (true) {
            socklen_t from_len = sizeof(from);
            int got = recvfrom(sock, reinterpret_cast<char*>(&packet), sizeof(packet), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
            if (got < 0) {
                return;
            }
            if (got != (int)sizeof(packet) || packet.magic != kMagic || packet.type == Punch) {
                continue;
            }
            if (packet.version != game_version) {
                if (!reported_version) {
                    net_log("a player has a different build of the game: not playing with them");
                    reported_version = true;
                }
                if (local == 0 && packet.type == Hello) {
                    send_control(Welcome, &from, from_len, 0); // so the joiner knows (with this version's label)
                    version_note = "A player with version " + label_in(packet) + " tried to join; you have " + version_label +
                        ". Everyone needs the same version.";
                }
                else if (local != 0 && packet.type == Welcome && lobby == btga::netplay::Lobby::Waiting) {
                    lobby = btga::netplay::Lobby::Failed;
                    lobby_error = version_clash_text(label_in(packet));
                }
                continue;
            }
            if (local == 0) {
                receive_as_host(packet, from, from_len);
            }
            else {
                receive_as_joiner(packet, from, from_len);
            }
        }
    }

    // A player's game is somewhere else entirely: said once, more plainly
    // than the desync. The session goes on; the players choose.
    void report_apart(int port, uint32_t here, uint32_t there) {
        reported_apart = true;
        net_log("player %d's game is somewhere else entirely (mode %u arena %u there, %u %u here)", port + 1,
            there & 0xFF, (there >> 8) & 0xFF, here & 0xFF, (here >> 8) & 0xFF);
        btga::netplay::show_message("The games are far out of step: another player's game is somewhere else\n"
            "entirely. Quit (F1, then Quit) and start a new online session.", 30);
    }

    // Every so often each game's hash and place, compared with the others'.
    // A difference is reported once (a desync: what players see can differ
    // from then on, and they choose whether to go on). After one, a player
    // whose game is somewhere else entirely (another mode or arena) for
    // kApartChecks checks in a row is reported again, more plainly; nothing
    // is ended either way.
    void check_hashes() {
        for (int port = 0; port < kMaxPlayers; port++) {
            for (auto it = remote_hashes[port].begin(); it != remote_hashes[port].end();) {
                auto mine = local_hashes.find(it->first);
                if (mine == local_hashes.end()) {
                    ++it;
                    continue;
                }
                if (mine->second != it->second && !reported_desync) {
                    net_log("the games differ at controller read sweep %u, here and player %d's (hash %016llx here, %016llx there)", it->first,
                        port + 1, (unsigned long long)mine->second, (unsigned long long)it->second);
                    reported_desync = true;
                    btga::netplay::show_message("The games are out of step (desync): from here on, what each of you\n"
                        "sees can differ. Quit and start a new online session to play on together.", 20);
                }
                auto here = local_places.find(it->first);
                auto there = remote_places[port].find(it->first);
                if (reported_desync && !reported_apart && here != local_places.end() && there != remote_places[port].end() &&
                    dropped[port] == UINT32_MAX) {
                    apart_checks[port] = here->second != there->second ? apart_checks[port] + 1 : 0;
                    if (apart_checks[port] >= kApartChecks) {
                        report_apart(port, here->second, there->second);
                    }
                }
                if (there != remote_places[port].end()) {
                    remote_places[port].erase(there);
                }
                it = remote_hashes[port].erase(it);
            }
        }
    }

    void start_joiners();

    // BTGA_NETPLAY's host: true once everyone is in and it has started.
    bool env_start_if_ready() {
        if (local != 0 || env_players == 0 || lobby != btga::netplay::Lobby::Waiting) {
            return false;
        }
        int ready = 0;
        for (int port = 1; port < kMaxPlayers; port++) {
            ready += peers[port].used && peer_ready(peers[port]) ? 1 : 0;
        }
        if (ready != env_players - 1) {
            return false;
        }
        lobby = btga::netplay::Lobby::Idle;
        start_joiners();
        for (int port = 1; port < player_count; port++) {
            send_control(Start, peers[port], (uint32_t)port);
        }
        btga::netplay::use_settings(session_settings());
        net_log("all %d players in: started", player_count);
        return true;
    }

    void start_joiners() {
        Peer kept[kMaxPlayers];
        kept[0] = peers[0];
        int next = 1;
        for (int port = 1; port < kMaxPlayers; port++) {
            Peer& peer = peers[port];
            if (!peer.used) {
                continue;
            }
            if (peer_ready(peer)) {
                kept[next++] = peer;
            }
            else {
                send_control(Leave, peer);
            }
        }
        for (int port = 0; port < kMaxPlayers; port++) {
            peers[port] = kept[port];
        }
        player_count = next;
        set_port_names();
    }
}

void btga::netplay::start(const char* version) {
    for (const char* c = version; *c != '\0'; c++) {
        game_version = (game_version ^ (uint8_t)*c) * 16777619u;
    }
    // "0.1.7 1a2b3c4d5e6f" -> "0.1.7 (1a2b3c4)"; just "0.1.7" with no commit.
    std::string v = version;
    size_t space = v.find(' ');
    std::string build = space == std::string::npos ? "" : v.substr(space + 1, 7);
    std::string label = space == std::string::npos ? v : v.substr(0, space);
    if (!build.empty() && build != "unknown") {
        label += " (" + build + ")";
    }
    version_label = clean_label(label.c_str(), label.size());
    check();
    if (lobby == Lobby::Waiting && local == 0 && env_players != 0) {
        // BTGA_NETPLAY's host: its game starts once everyone is in.
        while (!env_start_if_ready()) {
            poll_lobby();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return;
    }
    if (lobby != Lobby::Waiting || local == 0) {
        return;
    }
    // BTGA_NETPLAY's joiner: the host's settings, then its Start, before
    // the game starts.
    lobby_started = std::chrono::steady_clock::now();
    while (poll_lobby() == Lobby::Waiting) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (lobby == Lobby::Found) {
        accept();
        while (poll_lobby() == Lobby::Waiting) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    if (lobby == Lobby::Go) {
        begin();
    }
    else {
        net_log("%s", lobby_error.c_str());
        cancel();
    }
}

bool btga::netplay::host(uint16_t port, std::string& error) {
    cancel();
    checked = true; // BTGA_NETPLAY no longer counts
    local = 0;
    if (!open_socket("host", "", std::to_string(port))) {
        error = "Can't use UDP port " + std::to_string(port) + " (is another program on it?).";
        return false;
    }
    lobby = Lobby::Waiting;
    lobby_started = std::chrono::steady_clock::now();
    net_log("hosting on UDP port %u", (unsigned)port);
    btga::netplay::upnp::open(port, lan_address());
    if (!matchmaker().empty()) {
        code_note = "Getting a join code...";
        start_getting(true, "");
    }
    return true;
}

bool btga::netplay::join(const std::string& address, uint16_t port, std::string& error) {
    cancel();
    checked = true;
    local = 1;
    if (!open_socket("join", address, std::to_string(port))) {
        error = "Can't find " + address + ".";
        return false;
    }
    lobby = Lobby::Waiting;
    lobby_started = std::chrono::steady_clock::now();
    last_hello = lobby_started - std::chrono::seconds(1);
    typed_address = true;
    joiner_id = new_joiner_id();
    net_log("joining %s, UDP port %u", address.c_str(), (unsigned)port);
    return true;
}

bool btga::netplay::looks_like_code(const std::string& text) {
    return text.size() == 6 && std::all_of(text.begin(), text.end(), [](char c) { return std::isalnum((unsigned char)c) != 0; }) &&
        std::any_of(text.begin(), text.end(), [](char c) { return std::isalpha((unsigned char)c) != 0; });
}

bool btga::netplay::join_by_code(const std::string& text, std::string& error) {
    cancel();
    checked = true;
    local = 1;
    if (matchmaker().empty()) {
        error = "Join codes aren't set up in this build: join by the host's address.";
        return false;
    }
    if (!net_init()) {
        error = "Can't use the network.";
        return false;
    }
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in any{};
    any.sin_family = AF_INET;
    if (sock == kNoSocket || bind(sock, reinterpret_cast<const sockaddr*>(&any), sizeof(any)) != 0) {
        close_socket();
        error = "Can't open a UDP socket.";
        return false;
    }
    set_nonblocking(sock);
    std::string upper = text;
    for (char& c : upper) {
        c = (char)std::toupper((unsigned char)c);
    }
    lobby = Lobby::Waiting;
    lobby_started = std::chrono::steady_clock::now();
    last_hello = lobby_started - std::chrono::seconds(1);
    joiner_id = new_joiner_id();
    start_getting(false, upper);
    net_log("joining by code %s", upper.c_str());
    return true;
}

void btga::netplay::set_public(bool listed, const std::string& name) {
    host_public = listed;
    host_name = name;
}

void btga::netplay::refresh_public_games() {
    if (matchmaker().empty() || listing != nullptr) {
        return;
    }
    listing = ask(matchmaker() + "/list?v=" + version_text());
    list_state = ListState::Loading;
}

// The service's list: "CODE<tab>PLAYERS<tab>NAME" a line each. Taken as text
// to show and a code to join, nothing more: names to name_char's.
btga::netplay::ListState btga::netplay::public_games(std::vector<PublicGame>& games) {
    if (listing != nullptr) {
        std::unique_lock lock{ listing->mutex };
        if (listing->done) {
            bool ok = listing->ok;
            std::string body = listing->body;
            lock.unlock();
            listing = nullptr;
            public_list.clear();
            list_state = ok ? ListState::Done : ListState::Failed;
            size_t start = 0;
            while (ok && start < body.size() && public_list.size() < 20) {
                size_t end = body.find('\n', start);
                std::string line = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
                start = end == std::string::npos ? body.size() : end + 1;
                size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
                if (b == std::string::npos) {
                    continue;
                }
                PublicGame game;
                game.code = line.substr(0, a);
                game.players = std::atoi(line.substr(a + 1, b - a - 1).c_str());
                for (char c : line.substr(b + 1)) {
                    if (c != '\0' && name_char(c) && game.name.size() < 24) {
                        game.name.push_back(c);
                    }
                }
                if (looks_like_code(game.code) && game.players >= 1 && game.players < kMaxPlayers) {
                    public_list.push_back(game);
                }
            }
        }
    }
    games = public_list;
    return list_state;
}

std::string btga::netplay::join_code() {
    return code;
}

std::string btga::netplay::join_code_note() {
    return code_note;
}

namespace {
    // poll_lobby's parts. The join code service's answer, once it has come:
    // the host's code, or the joiner's list of the host's addresses. False
    // if there's nothing more to do this time (failed, or nothing to send to
    // yet).
    bool take_matchmaker_answer() {
        if (getting != nullptr) {
            std::unique_lock lock{ getting->mutex };
            if (getting->done) {
                bool ok = getting->ok;
                std::string body = getting->body, error = getting->error;
                lock.unlock();
                getting = nullptr;
                if (local == 0) {
                    // "CODE TOKEN"
                    size_t space = body.find(' ');
                    std::string got_code = ok && space != std::string::npos ? body.substr(0, space) : "";
                    std::string token = ok && space != std::string::npos ? body.substr(space + 1) : "";
                    while (!token.empty() && (token.back() == '\n' || token.back() == '\r')) {
                        token.pop_back();
                    }
                    bool token_ok = !token.empty() && token.size() <= 32 &&
                        std::all_of(token.begin(), token.end(), [](char c) { return std::isalnum((unsigned char)c) != 0; });
                    if (btga::netplay::looks_like_code(got_code) && token_ok) {
                        code = got_code;
                        code_token = token;
                        code_note.clear();
                        net_log("join code %s", code.c_str());
                    }
                    else {
                        code_note = (error.empty() ? std::string("The join code service gave an answer that isn't a code.") : error) +
                            " Players can still join by address.";
                    }
                }
                else if (!ok) {
                    lobby = btga::netplay::Lobby::Failed;
                    lobby_error = error;
                    return false;
                }
                else {
                    candidates = parse_candidates(body);
                    lobby_started = std::chrono::steady_clock::now(); // the 30 s from here
                    std::string list;
                    for (const sockaddr_in& a : candidates) {
                        list += " " + address_text(a);
                    }
                    net_log("the host's addresses:%s", list.c_str());
                    if (candidates.empty()) {
                        lobby = btga::netplay::Lobby::Failed;
                        lobby_error = "The host's game has no address to reach it by.";
                        return false;
                    }
                }
            }
            else if (local != 0) {
                return false; // nothing to send to yet
            }
        }
        return true;
    }

    // The host: the join code's polls and punches, pings.
    void poll_host_lobby(std::chrono::steady_clock::time_point now) {
        // By join code: who has joined (a line each), and a way in for them;
        // timing each joiner.
        if (!code.empty()) {
            if (asking != nullptr) {
                std::unique_lock lock{ asking->mutex };
                if (asking->done) {
                    if (asking->ok && asking->body.rfind("waiting", 0) != 0) {
                        candidates = parse_candidates(asking->body);
                    }
                    lock.unlock();
                    asking = nullptr;
                }
            }
            if (asking == nullptr && now - last_code_poll >= std::chrono::milliseconds(1000)) {
                int players = 1;
                for (int port = 1; port < kMaxPlayers; port++) {
                    players += peers[port].used ? 1 : 0;
                }
                asking = ask(matchmaker() + "/poll?code=" + code + "&token=" + btga::netplay::http::escape(code_token) +
                    "&players=" + std::to_string(players));
                last_code_poll = now;
            }
            if (!candidates.empty() && now - last_punch >= std::chrono::milliseconds(100)) {
                for (const sockaddr_in& a : candidates) {
                    send_control(Punch, reinterpret_cast<const sockaddr_storage*>(&a), sizeof(a));
                }
                last_punch = now;
            }
        }
        for (int port = 1; port < kMaxPlayers; port++) {
            Peer& peer = peers[port];
            if (!peer.used) {
                continue;
            }
            if (now - peer.last_heard > std::chrono::seconds(10)) {
                net_log("player %d stopped answering", port + 1);
                peer = Peer{};
                continue;
            }
            if (now - peer.last_ping >= std::chrono::milliseconds(100)) {
                send_ping(peer);
            }
        }
    }

    // The joiner: Hello until welcomed, pings, Ready until Start.
    void poll_joiner_lobby(std::chrono::steady_clock::time_point now) {
        if (welcomed && now - peers[0].last_ping >= std::chrono::milliseconds(100)) {
            send_ping(peers[0]); // timing the connection while the settings are read
        }
        if (lobby == btga::netplay::Lobby::Waiting && !accepted) {
            if (now - last_hello >= std::chrono::milliseconds(100)) {
                if (peers[0].used) {
                    send_control(Hello, peers[0]);
                }
                else {
                    for (const sockaddr_in& a : candidates) {
                        send_control(Hello, reinterpret_cast<const sockaddr_storage*>(&a), sizeof(a));
                    }
                }
                last_hello = now;
            }
            if (now - lobby_started > std::chrono::seconds(30)) {
                lobby = btga::netplay::Lobby::Failed;
                lobby_error = "No answer from the host. Check the address and port, that the host is waiting, "
                    "and that the port is let through their firewall (and forwarded on their router, over the internet).";
            }
        }
        else if (lobby == btga::netplay::Lobby::Waiting && accepted) {
            // Waiting for the host to start: still here.
            if (now - last_ready >= std::chrono::milliseconds(250)) {
                send_control(Ready, peers[0]);
                last_ready = now;
            }
            if (now - peers[0].last_heard > std::chrono::seconds(20)) {
                lobby = btga::netplay::Lobby::Failed;
                lobby_error = "The host stopped answering.";
            }
        }
    }
}

btga::netplay::Lobby btga::netplay::poll_lobby() {
    if (lobby != Lobby::Waiting && lobby != Lobby::Found) {
        return lobby;
    }
    // The socket is the join code thread's while it asks the STUN server.
    if (stunning()) {
        return lobby;
    }
    if (!take_matchmaker_answer()) {
        return lobby;
    }
    receive_packets();
    auto now = std::chrono::steady_clock::now();
    if (local == 0) {
        poll_host_lobby(now);
    }
    else {
        poll_joiner_lobby(now);
    }
    return lobby;
}

std::vector<btga::netplay::LobbyPlayer> btga::netplay::lobby_players() {
    std::vector<LobbyPlayer> list;
    if (local != 0) {
        return list;
    }
    for (int port = 1; port < kMaxPlayers; port++) {
        if (peers[port].used) {
            list.push_back({ port, peer_ready(peers[port]), peers[port].nickname });
        }
    }
    return list;
}

bool btga::netplay::host_start() {
    if (local != 0 || lobby != Lobby::Waiting) {
        return false;
    }
    bool any = false;
    for (int port = 1; port < kMaxPlayers; port++) {
        any |= peers[port].used && peer_ready(peers[port]);
    }
    if (!any) {
        return false;
    }
    start_joiners();
    delay = delay_choice != 0 ? delay_choice : auto_delay();
    for (int port = 1; port < player_count; port++) {
        std::vector<int64_t> trips = sorted_round_trips(peers[port]);
        net_log("player %d: ping %d ms over %d", port + 1, trips.empty() ? -1 : (int)(trips[trips.size() / 2] / 1000), (int)trips.size());
        for (int i = 0; i < 3; i++) {
            send_control(Start, peers[port], (uint32_t)port);
        }
    }
    net_log("starting with %d players, input delay %u (%s)", player_count, delay, delay_choice != 0 ? "the host's" : "Auto");
    return true;
}

std::string btga::netplay::lobby_error_text() {
    return lobby_error;
}

std::string btga::netplay::version_note_text() {
    return version_note;
}

std::string btga::netplay::host_settings_text() {
    return btga::netplay::describe_settings(welcome_settings);
}

void btga::netplay::accept() {
    accepted = true;
    last_ready = std::chrono::steady_clock::now();
    lobby = Lobby::Waiting; // for the host's Start
    for (int i = 0; i < 3; i++) {
        send_control(Ready, peers[0]);
    }
}

void btga::netplay::cancel() {
    if (enabled) {
        return; // the game is playing online
    }
    // Not while the join code thread is still using the socket (about a
    // second and a half at most).
    while (stunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    getting = nullptr;
    if (sock != kNoSocket && lobby != Lobby::Idle) {
        for (int port = 0; port < kMaxPlayers; port++) {
            if (peers[port].used && port != local) {
                send_control(Leave, peers[port]);
            }
        }
    }
    close_code();
    code_note.clear();
    candidates.clear();
    if (local == 0) {
        btga::netplay::upnp::close();
    }
    close_socket();
    lobby = Lobby::Idle;
    lobby_error.clear();
    welcomed = accepted = reported_version = typed_address = false;
    welcome_delay = 0;
    version_note.clear();
    {
        std::lock_guard lock{ round_trip_mutex };
        for (Peer& peer : peers) {
            peer = Peer{};
        }
    }
    for (int port = 0; port < kMaxPlayers; port++) {
        remote_reads[port].clear();
        remote_hashes[port].clear();
        remote_places[port].clear();
    }
}

void btga::netplay::begin() {
    // The game runs in the runtime's deterministic mode
    // (lib-patches/N64ModernRuntime/0004), by the host's settings.
    ultramodern::set_deterministic(true);
    btga::netplay::use_settings(local == 0 ? session_settings() : welcome_settings);
    lobby = Lobby::Idle;
    enabled = true;
    close_code();
    net_log("player %d of %d, input delay %u", local + 1, player_count, delay);
    if (local != 0) {
        net_log("joined; the host's settings for this session:\n%s", btga::netplay::describe_settings(welcome_settings).c_str());
    }
}

std::string btga::netplay::lan_address() {
    // The address this PC would reach the internet from (connecting a UDP
    // socket sends nothing).
    if (!net_init()) {
        return {};
    }
    socket_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kNoSocket) {
        return {};
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
    std::string text;
    sockaddr_in me{};
    socklen_t len = sizeof(me);
    if (connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == 0 &&
        getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0) {
        char buf[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &me.sin_addr, buf, sizeof(buf)) != nullptr) {
            text = buf;
        }
    }
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return text;
}

void btga::netplay::set_nickname(const std::string& name) {
    own_nickname = clean_nickname(name.c_str(), name.size());
}

std::string btga::netplay::player_name(int port) {
    if (port >= 0 && port < kMaxPlayers && !port_names[port].empty()) {
        return port_names[port];
    }
    return "Player " + std::to_string(port + 1);
}

std::vector<btga::netplay::Player> btga::netplay::players() {
    std::vector<Player> list;
    if (!enabled) {
        return list;
    }
    for (int port = 0; port < player_count; port++) {
        Player player{};
        player.port = port;
        player.name = btga::netplay::player_name(port);
        player.local = port == local;
        player.host = port == 0;
        player.ping_ms = -1;
        // Only the host's address, and only if this player typed it.
        const Peer* direct = local == 0 ? &peers[port] : port == 0 ? &peers[0] : nullptr;
        if (player.local) {
            player.status = "This computer";
        }
        else {
            bool gone = dropped[port] != UINT32_MAX || (local != 0 && alone);
            bool quit = local == 0 ? peers[port].left : port == 0 ? peers[0].left : false;
            player.status = gone ? (quit ? "Left the game" : "Gone") : "Connected";
            if (direct != nullptr && direct->used && !gone) {
                std::vector<int64_t> trips = sorted_round_trips(*direct);
                player.ping_ms = trips.empty() ? -1 : (int)((trips[trips.size() / 2] + 500) / 1000);
                if (port == 0 && typed_address && direct->addr.ss_family == AF_INET) {
                    player.where = address_text(reinterpret_cast<const sockaddr_in&>(direct->addr));
                }
            }
        }
        list.push_back(player);
    }
    return list;
}

int btga::netplay::players_playing() {
    return player_count; // the session's ports (they all answer, even once a player is gone)
}

uint32_t btga::netplay::input_delay() {
    return delay;
}

uint32_t btga::netplay::host_delay_choice() {
    return welcome_delay;
}

void btga::netplay::set_delay_choice(uint32_t sweeps) {
    delay_choice = std::min(sweeps, kMaxDelay);
    if (delay_choice != 0) {
        delay = delay_choice;
    }
}

int btga::netplay::ping_ms() {
    // The joiner's to the host; the host's slowest joiner.
    int worst = -1;
    for (int port = 0; port < kMaxPlayers; port++) {
        if (port == local || !peers[port].used) {
            continue;
        }
        std::vector<int64_t> trips = sorted_round_trips(peers[port]);
        if (!trips.empty()) {
            worst = std::max(worst, (int)((trips[trips.size() / 2] + 500) / 1000));
        }
    }
    return worst;
}

float btga::netplay::sweep_ms() {
    return (float)kSweepMs;
}

bool btga::netplay::screen_held() {
    return holding_screen.load();
}

void btga::netplay::shutdown() {
    if (!enabled || sock == kNoSocket) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        for (int port = 0; port < kMaxPlayers; port++) {
            if (peers[port].used && port != local && !peers[port].lost) {
                send_control(Leave, peers[port]);
            }
        }
    }
}

bool btga::netplay::active() {
    check();
    return enabled;
}

// From the UI's thread: queued, and sent from the game's (send_chats), which
// owns the players' state.
bool btga::netplay::send_chat(const std::string& text) {
    if (!enabled || (local != 0 && alone)) {
        return false;
    }
    std::lock_guard lock{ chat_mutex };
    if (chat_pending.size() < 16) {
        chat_pending.push_back(ChatPending{ next_chat_id++, text });
    }
    return true;
}

bool btga::netplay::host_gone() {
    return enabled && local != 0 && alone;
}

int btga::netplay::local_port() {
    check();
    return local;
}

void btga::netplay::exchange(const PortRead& local_read, PortRead out[4]) {
    // Our read now goes with the sweep `delay` on; the first sweeps take none.
    local_reads[sweep + delay] = local_read;

    // A hash of the game every so often, taken here (the same point in all).
    if (sweep % kHashEvery == 0) {
        local_hashes.erase(sweep - 16 * kHashEvery); // as far back as take_reads keeps theirs
        local_places.erase(sweep - 16 * kHashEvery);
        local_hashes[sweep] = btga::netplay::memory_hash();
        local_places[sweep] = 0;
        // BTGA_FAKE_DESYNC=<sweep> (tests): this PC's hash and place read
        // as different from that sweep on, to see the messages.
        static const char* fake = std::getenv("BTGA_FAKE_DESYNC");
        if (fake != nullptr && sweep >= (uint32_t)std::atoi(fake)) {
            local_hashes[sweep] ^= 1;
            local_places[sweep] ^= 0xFF;
        }
        last_local_hash = sweep;
    }

    // The ports this sweep waits for: everyone else's not yet gone.
    auto waits_for = [&](int port) {
        return port != local && !(local != 0 && alone) && dropped[port] > sweep;
    };
    auto have_all = [&]() {
        if (sweep < delay) {
            return true;
        }
        for (int port = 0; port < player_count; port++) {
            if (waits_for(port) && remote_reads[port].count(sweep) == 0) {
                return false;
            }
        }
        return true;
    };

    auto started = std::chrono::steady_clock::now();
    auto last_send = started - std::chrono::seconds(1);
    bool waited = false;
    auto last_present = started;
    static bool waiting_shown = false; // "Waiting for player N" on screen
    static auto last_start_sent = started;
    while (true) {
        receive_packets();
        send_chats();
        auto now = std::chrono::steady_clock::now();
        // BTGA_NETPLAY's host: started once everyone is in (start()
        // waits for it, so the settings are in place before the game boots).
        if (env_start_if_ready()) {
            started = now; // the wait for them counts from here
        }
        // The host: Start again to those whose game hasn't been heard from.
        if (local == 0 && lobby != Lobby::Waiting && now - last_start_sent >= std::chrono::milliseconds(250)) {
            for (int port = 1; port < player_count; port++) {
                if (peers[port].used && !peers[port].started && !peers[port].lost) {
                    send_control(Start, peers[port], (uint32_t)port);
                }
            }
            last_start_sent = now;
        }
        if (now - last_send >= std::chrono::milliseconds(8)) {
            send_inputs();
            last_send = now;
        }
        for (int port = 0; port < kMaxPlayers; port++) {
            if (port != local && peers[port].used && !peers[port].lost && now - peers[port].last_ping >= std::chrono::seconds(1)) {
                send_ping(peers[port]);
            }
        }
        if (have_all()) {
            break;
        }
        // Who it's waiting for.
        int missing = -1;
        for (int port = 0; port < player_count && missing < 0; port++) {
            if (waits_for(port) && remote_reads[port].count(sweep) == 0) {
                missing = port;
            }
        }
        // (A joiner's game that hasn't been heard from yet may still be
        // starting up: three times as long for them.)
        auto limit = local == 0 && missing > 0 && !peers[missing].started ? 3 * kTimeout : kTimeout;
        if (now - started > limit && !(local == 0 && lobby == Lobby::Waiting)) {
            if (local == 0) {
                // The host gives up on them: everyone plays on without.
                drop_port(missing, false);
                started = now;
                continue;
            }
            if (missing == 0 || now - peers[0].last_heard > kTimeout || now - started > 2 * kTimeout) {
                lose_host(false);
                waiting_shown = false;
                holding_screen = false;
                break;
            }
        }
        if (!waited && now - started > std::chrono::milliseconds(500)) {
            net_log("waiting for player %d (read sweep %u)", missing + 1, sweep);
            waited = true;
        }
        // Said on screen after a second (a game starting up, or a slow
        // connection).
        if (!waiting_shown && now - started > std::chrono::seconds(1)) {
            btga::netplay::show_message("Waiting for player " + std::to_string(missing + 1) + "...", 0);
            waiting_shown = true;
            holding_screen = true;
        }
        // The game's frames stop while it waits; the screen is shown again
        // so the message is seen.
        if (waiting_shown && now - last_present >= std::chrono::milliseconds(100)) {
            ultramodern::deterministic_present();
            last_present = now;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    check_hashes();
    if (waiting_shown) {
        btga::netplay::show_message("", 0);
        waiting_shown = false;
        holding_screen = false;
    }
    btga::netplay::message_tick();
    btga::netplay::chat_tick();

    // Ports with nobody on them don't answer; a player who's gone answers
    // with nothing pressed.
    PortRead neutral{};
    neutral.response = 1;
    for (int port = 0; port < 4; port++) {
        out[port] = PortRead{};
        if (port >= player_count) {
            continue;
        }
        if (port == local) {
            out[port] = sweep < delay ? neutral : local_reads[sweep];
        }
        else if (sweep < delay || !waits_for(port)) {
            out[port] = neutral;
        }
        else {
            auto found = remote_reads[port].find(sweep);
            out[port] = found != remote_reads[port].end() ? found->second : neutral;
        }
    }
    // Now and then, the game's pace and the connection, for the log.
    static auto pace_from = std::chrono::steady_clock::now();
    if (sweep % 1800 == 0) {
        auto now = std::chrono::steady_clock::now();
        if (sweep != 0) {
            double ms = std::chrono::duration<double, std::milli>(now - pace_from).count() / 1800.0;
            net_log("read sweep %u: %.1f ms a sweep, ping %d ms, input delay %u", sweep, ms, btga::netplay::ping_ms(), delay);
        }
        pace_from = now;
    }

    // Old sweeps can go.
    if (sweep >= 2 * kResend) {
        local_reads.erase(sweep - 2 * kResend);
        for (int port = 0; port < kMaxPlayers; port++) {
            remote_reads[port].erase(sweep - 2 * kResend);
        }
    }
    sweep++;
}
