#ifndef __BTGA_NETPLAY_HTTP_H__
#define __BTGA_NETPLAY_HTTP_H__

#include <string>

// A small HTTPS GET (http_get.cpp), for the netplay join codes:
// WinHTTP on Windows, the curl command elsewhere. Blocks, so it's called off
// the UI's thread. False if there was no answer or it wasn't 200.
namespace btga::netplay::http {
    bool get(const std::string& url, std::string& body, int timeout_seconds = 10);
    // A query string value, with anything but letters, digits and -._~ as %XX.
    std::string escape(const std::string& text);
}

#endif
