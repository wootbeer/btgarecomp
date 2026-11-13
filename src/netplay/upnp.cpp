// UPnP port mapping for the netplay host (upnp.h): find the
// router (an SSDP search on the local network), read its description for
// its WANIPConnection (or WANPPPConnection) service, and ask it with SOAP to
// forward the UDP port to this PC. BTGA_UPNP=<description URL> skips the
// search, for testing.
#include "upnp.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kNoSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kNoSocket = -1;
#endif

namespace {
    std::mutex mutex;
    std::string status_text;
    // The mapping made (to take away again).
    bool mapped = false;
    uint16_t mapped_port = 0;
    std::string control_url, service_type;
    int generation = 0; // a newer open() or close() makes an older answer moot

    void close_socket(socket_t s) {
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
    }

    void set_timeout(socket_t s, int ms) {
#ifdef _WIN32
        DWORD t = (DWORD)ms;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
#else
        timeval t{ ms / 1000, (ms % 1000) * 1000 };
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t));
#endif
    }

    std::string lower(std::string text) {
        for (char& c : text) {
            c = (char)std::tolower((unsigned char)c);
        }
        return text;
    }

    bool private_address(const std::string& ip) {
        in_addr a{};
        if (inet_pton(AF_INET, ip.c_str(), &a) != 1) {
            return false;
        }
        uint32_t v = ntohl(a.s_addr);
        return (v >> 24) == 10 || (v >> 20) == (172u << 4 | 1) || (v >> 16) == (192u << 8 | 168) || (v >> 22) == (100u << 2 | 1) ||
            v == 0;
    }

    // Text from a router that goes into a request line or a header: no
    // spaces, line breaks or other control characters.
    bool clean_header(const std::string& text) {
        return !text.empty() && text.size() < 1024 &&
            std::none_of(text.begin(), text.end(), [](char c) { return (unsigned char)c <= 0x20 || (unsigned char)c >= 0x7F; });
    }

    struct Url {
        std::string host, port = "80", path = "/";
    };

    bool parse_url(const std::string& url, Url& out) {
        if (lower(url.substr(0, 7)) != "http://") {
            return false;
        }
        std::string rest = url.substr(7);
        size_t slash = rest.find('/');
        std::string where = rest.substr(0, slash);
        out.path = slash == std::string::npos ? "/" : rest.substr(slash);
        size_t colon = where.rfind(':');
        out.host = where.substr(0, colon);
        if (colon != std::string::npos) {
            out.port = where.substr(colon + 1);
        }
        return !out.host.empty();
    }

    // A plain HTTP/1.0 request on the local network; the answer's body, and
    // its status.
    bool http(const std::string& url, const std::string& method, const std::string& extra_headers,
        const std::string& body, std::string& answer, int& status) {
        Url u;
        if (!parse_url(url, u)) {
            return false;
        }
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* found = nullptr;
        if (getaddrinfo(u.host.c_str(), u.port.c_str(), &hints, &found) != 0 || found == nullptr) {
            return false;
        }
        socket_t s = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
        if (s == kNoSocket) {
            freeaddrinfo(found);
            return false;
        }
        set_timeout(s, 3000);
        bool connected = connect(s, found->ai_addr, (int)found->ai_addrlen) == 0;
        freeaddrinfo(found);
        if (!connected) {
            close_socket(s);
            return false;
        }
        std::string request = method + " " + u.path + " HTTP/1.0\r\nHost: " + u.host + ":" + u.port + "\r\n" + extra_headers +
            "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        if (send(s, request.data(), (int)request.size(), 0) != (int)request.size()) {
            close_socket(s);
            return false;
        }
        // Read until the router closes, 256 KB, or 10 s in all (a reply
        // dribbled a byte at a time can't hold the thread).
        std::string reply;
        char buffer[2048];
        int got;
        auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < give_up && (got = recv(s, buffer, sizeof(buffer), 0)) > 0 && reply.size() < 262144) {
            reply.append(buffer, got);
        }
        close_socket(s);
        size_t space = reply.find(' ');
        size_t end = reply.find("\r\n\r\n");
        if (space == std::string::npos || end == std::string::npos) {
            return false;
        }
        status = std::atoi(reply.c_str() + space + 1);
        answer = reply.substr(end + 4);
        return true;
    }

    // The text of the first <tag> in `xml` from `from` on ("" if none).
    std::string element(const std::string& xml, const std::string& tag, size_t from = 0, size_t* at = nullptr) {
        std::string open = "<" + tag + ">", close = "</" + tag + ">";
        size_t start = xml.find(open, from);
        if (start == std::string::npos) {
            return "";
        }
        size_t end = xml.find(close, start);
        if (end == std::string::npos) {
            return "";
        }
        if (at != nullptr) {
            *at = end;
        }
        std::string text = xml.substr(start + open.size(), end - start - open.size());
        while (!text.empty() && std::isspace((unsigned char)text.back())) {
            text.pop_back();
        }
        while (!text.empty() && std::isspace((unsigned char)text.front())) {
            text.erase(text.begin());
        }
        return text;
    }

    // The routers' description URLs answering an SSDP search (about 2 s).
    std::vector<std::string> discover(const std::string& local_ip) {
        std::vector<std::string> found;
        socket_t s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s == kNoSocket) {
            return found;
        }
        in_addr out{};
        if (inet_pton(AF_INET, local_ip.c_str(), &out) == 1) {
            setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&out), sizeof(out));
        }
        set_timeout(s, 250);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(1900);
        inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
        for (const char* target : { "urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                 "urn:schemas-upnp-org:service:WANIPConnection:1" }) {
            std::string search = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") +
                target + "\r\n\r\n";
            sendto(s, search.data(), (int)search.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
        }
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
        while (std::chrono::steady_clock::now() < until) {
            char buffer[2048];
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            int got = recvfrom(s, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr*>(&from), &from_len);
            if (got <= 0 || from.sin_family != AF_INET) {
                continue;
            }
            char sender[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, sender, sizeof(sender));
            std::string reply(buffer, got);
            std::string low = lower(reply);
            size_t at = low.find("\r\nlocation:");
            if (at == std::string::npos) {
                continue;
            }
            size_t start = at + 11;
            size_t end = reply.find("\r\n", start);
            std::string location = reply.substr(start, end == std::string::npos ? std::string::npos : end - start);
            while (!location.empty() && location.front() == ' ') {
                location.erase(location.begin());
            }
            // Only a router on this network, describing itself: its address
            // private, the description on that same address.
            Url u;
            if (!private_address(sender) || !parse_url(location, u) || u.host != sender || !clean_header(location)) {
                continue;
            }
            if (std::find(found.begin(), found.end(), location) == found.end()) {
                found.push_back(location);
            }
        }
        close_socket(s);
        return found;
    }

    // From a router's description: its connection service and where to send
    // to it.
    bool find_service(const std::string& location, std::string& control, std::string& type) {
        std::string xml;
        int status = 0;
        if (!http(location, "GET", "", "", xml, status) || status != 200) {
            return false;
        }
        Url u;
        parse_url(location, u);
        std::string base = element(xml, "URLBase");
        if (base.empty()) {
            base = "http://" + u.host + ":" + u.port;
        }
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        size_t from = 0;
        while (true) {
            size_t at = 0;
            std::string service = element(xml, "service", from, &at);
            if (at == 0) {
                return false;
            }
            from = at;
            std::string service_type_text = element(service, "serviceType");
            if (service_type_text.find("WANIPConnection") == std::string::npos && service_type_text.find("WANPPPConnection") == std::string::npos) {
                continue;
            }
            std::string path = element(service, "controlURL");
            if (path.empty()) {
                continue;
            }
            control = lower(path.substr(0, 7)) == "http://" ? path : base + (path.front() == '/' ? "" : "/") + path;
            type = service_type_text;
            // They go into the request line and a header.
            Url c;
            if (!clean_header(control) || !clean_header(type) || !parse_url(control, c) || c.host != u.host) {
                return false;
            }
            return true;
        }
    }

    bool soap(const std::string& control, const std::string& type, const std::string& action, const std::string& arguments,
        std::string& answer) {
        std::string body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
            "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" + action + " xmlns:u=\"" + type + "\">" +
            arguments + "</u:" + action + "></s:Body></s:Envelope>";
        std::string headers = "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + type + "#" + action + "\"\r\n";
        int status = 0;
        return http(control, "POST", headers, body, answer, status) && status == 200;
    }

    void set_status(int job, const std::string& text) {
        std::lock_guard lock{ mutex };
        if (job == generation) {
            status_text = text;
        }
    }
}

void btga::netplay::upnp::open(uint16_t port, const std::string& local_ip) {
    int job;
    {
        std::lock_guard lock{ mutex };
        job = ++generation;
        status_text.clear();
    }
    std::thread([job, port, local_ip] {
        std::vector<std::string> routers;
        const char* test = std::getenv("BTGA_UPNP");
        if (test != nullptr && test[0] != '\0') {
            routers.push_back(test);
        }
        else if (!local_ip.empty()) {
            routers = discover(local_ip);
        }
        std::string p = std::to_string(port);
        for (const std::string& router : routers) {
            std::string control, type, answer;
            if (!find_service(router, control, type)) {
                continue;
            }
            auto add = [&](const char* lease) {
                return soap(control, type, "AddPortMapping",
                    "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + p + "</NewExternalPort><NewProtocol>UDP</NewProtocol>"
                    "<NewInternalPort>" + p + "</NewInternalPort><NewInternalClient>" + local_ip + "</NewInternalClient>"
                    "<NewEnabled>1</NewEnabled><NewPortMappingDescription>BattleTanx GA netplay</NewPortMappingDescription>"
                    "<NewLeaseDuration>" + lease + "</NewLeaseDuration>", answer);
            };
            // Some routers only take mappings that don't expire.
            if (!add("7200") && !add("0")) {
                std::fprintf(stderr, "[NET] UPnP: the router (%s) wouldn't forward UDP port %u\n", router.c_str(), (unsigned)port);
                continue;
            }
            std::string external;
            if (soap(control, type, "GetExternalIPAddress", "", answer)) {
                external = element(answer, "NewExternalIPAddress");
                in_addr check{};
                if (inet_pton(AF_INET, external.c_str(), &check) != 1) {
                    external.clear(); // only an address goes on the screen
                }
            }
            bool moot;
            {
                std::lock_guard lock{ mutex };
                moot = job != generation;
                if (!moot) {
                    mapped = true;
                    mapped_port = port;
                    control_url = control;
                    service_type = type;
                }
            }
            if (moot) {
                // Stopped hosting meanwhile: close() didn't know of this one.
                soap(control, type, "DeletePortMapping",
                    "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + p + "</NewExternalPort><NewProtocol>UDP</NewProtocol>", answer);
                return;
            }
            std::fprintf(stderr, "[NET] UPnP: UDP port %u forwarded by %s (outside address %s)\n", (unsigned)port, router.c_str(),
                external.empty() ? "unknown" : external.c_str());
            if (!external.empty() && private_address(external)) {
                set_status(job, "Your router opened UDP port " + p + " for this game (UPnP), but it's behind another router or your "
                    "internet provider's (its outside address is " + external + "): players over the internet may still not get through.");
            }
            else {
                set_status(job, "Your router opened UDP port " + p + " for this game (UPnP).");
            }
            return;
        }
        set_status(job, "Your router didn't open the port by itself (UPnP is off or not there).");
    }).detach();
}

void btga::netplay::upnp::close() {
    std::string control, type;
    uint16_t port;
    {
        std::lock_guard lock{ mutex };
        ++generation;
        status_text.clear();
        if (!mapped) {
            return;
        }
        mapped = false;
        control = control_url;
        type = service_type;
        port = mapped_port;
    }
    std::thread([control, type, port] {
        std::string answer;
        soap(control, type, "DeletePortMapping",
            "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>",
            answer);
    }).detach();
}

std::string btga::netplay::upnp::status() {
    std::lock_guard lock{ mutex };
    return status_text;
}
