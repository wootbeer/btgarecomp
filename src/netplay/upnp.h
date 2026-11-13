#ifndef __BTGA_NETPLAY_UPNP_H__
#define __BTGA_NETPLAY_UPNP_H__

#include <cstdint>
#include <string>

// UPnP port mapping (upnp.cpp): the netplay host asks the router
// to forward its UDP port, so the other player can reach it without the
// player setting up port forwarding. Not every router answers, or allows it.
namespace btga::netplay::upnp {
    // Starts asking on a thread of its own; `port` both outside and on this
    // PC, to `local_ip`.
    void open(uint16_t port, const std::string& local_ip);
    // Takes the mapping away again (if there is one), also off the UI's thread.
    void close();
    // How it went, for the host's screen: "" while asking or if not tried.
    std::string status();
}

#endif
