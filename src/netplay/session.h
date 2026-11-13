#ifndef __BTGA_NETPLAY_SESSION_H__
#define __BTGA_NETPLAY_SESSION_H__

#include <cstdint>
#include <string>
#include <vector>

// Netplay's session (session.cpp), inside the netplay folder: two to four
// players in lockstep over UDP, through the host. Ported from the Rage Wars
// port (wootbeer/trwrecomp's src/main/netplay.cpp), without its Controller
// Pak sharing: online, no player has a pak, so nothing is read from or
// written to anyone's saves.
namespace btga::netplay {
    // One controller port's read, as it goes to the game and over the network.
    struct PortRead {
        int32_t response = 0; // the controller answered
        uint32_t buttons = 0;
        float x = 0.0f, y = 0.0f;
    };

    // Before the game starts (startup): the build (everyone needs the same),
    // and BTGA_NETPLAY if it's set (testing: host:<udp port> or
    // join:<address>:<udp port>; the joiner waits for the host's Start).
    void start(const char* version);
    // The session has started: the game plays online.
    bool active();
    // This PC's player's controller port (0 hosting).
    int local_port();
    // A joiner whose host is gone (left or lost): it plays on alone.
    bool host_gone();
    // One sweep of the game's controller reads: hands over this player's read
    // and fills `out` with every port's for this sweep, waiting for the
    // others' if they haven't come.
    void exchange(const PortRead& local_read, PortRead out[4]);

    // The launcher's Netplay screen (later): the host waits (lobby_players)
    // until host_start, then begin and start the game. The joiner waits for
    // the host's answer, Found with its settings (show them, then accept or
    // cancel), waits again for the host's Start, Go once it has (begin, then
    // start the game). Failed: lobby_error_text says why.
    enum class Lobby { Idle, Waiting, Found, Failed, Go };
    bool host(uint16_t port, std::string& error);
    bool join(const std::string& address, uint16_t port, std::string& error);
    // Join codes (tools/matchmaker): the host gets one when it hosts, if the
    // build has the service's address; the joiner can type it instead.
    bool looks_like_code(const std::string& text);
    bool join_by_code(const std::string& code, std::string& error);
    std::string join_code();
    void set_public(bool listed, const std::string& name);
    struct PublicGame {
        std::string code;
        int players;
        std::string name;
    };
    enum class ListState { Idle, Loading, Done, Failed };
    void refresh_public_games();
    ListState public_games(std::vector<PublicGame>& games);
    std::string join_code_note();
    Lobby poll_lobby();
    struct LobbyPlayer {
        int port;
        bool ready;
        std::string name;
    };
    std::vector<LobbyPlayer> lobby_players();
    bool host_start();
    std::string lobby_error_text();
    std::string version_note_text();
    std::string host_settings_text();
    void accept();
    void cancel();
    void begin();
    // The game is waiting for another player's input: the screen is shown
    // again as it was, with the message over it.
    bool screen_held();
    // Quitting: tells the others, who play on without this player.
    void shutdown();
    struct Player {
        int port;
        std::string name;
        bool local;
        bool host;
        std::string where;  // the host's address (to a joiner who typed it)
        std::string status;
        int ping_ms;
    };
    // This PC's player's nickname, before host() or join(): letters, digits
    // and a little punctuation, no spaces, 12 characters at most.
    void set_nickname(const std::string& name);
    std::string player_name(int port);
    std::vector<Player> players();
    // The session's players (ports 0 to this - 1 have a controller).
    int players_playing();
    uint32_t input_delay();
    // The host's input delay, in controller reads; 0 Auto. Before hosting.
    void set_delay_choice(uint32_t sweeps);
    uint32_t host_delay_choice();
    int ping_ms();
    float sweep_ms();
    // Chat: a message to every player, through the host; false if there's
    // nobody to send it to.
    bool send_chat(const std::string& text);
    std::string lan_address();
}

#endif
