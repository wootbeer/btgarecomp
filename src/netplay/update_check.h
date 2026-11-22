#ifndef __BTGA_NETPLAY_UPDATE_CHECK_H__
#define __BTGA_NETPLAY_UPDATE_CHECK_H__

#include <string>

// Is a newer version out? (update_check.cpp, from PaxDino): one anonymous
// request to GitHub for the latest release, off the UI's thread, at most once
// a launch, when the Netplay screen first opens. Nothing is downloaded or
// installed; the Netplay screen just says so, since everyone in a game needs
// the same version.
namespace btga::netplay::update {
    constexpr const char* kReleasesUrl = "github.com/wootbeer/btgarecomp/releases";

    // This build's version ("0.1.11"), at startup.
    void set_current(const std::string& version);
    const std::string& current();
    // Starts the check (once; later calls do nothing).
    void check();
    // The newer release's version ("0.1.12"), once the check has found one;
    // "" while it runs, if this build is the latest, or if GitHub couldn't be
    // reached.
    std::string newer();
}

#endif
