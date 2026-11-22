#ifndef __BTGA_RESTART_H__
#define __BTGA_RESTART_H__

// The menu's Restart button and the quit prompt's Restart choice
// (lib-patches/RecompFrontend/0022, from snprecomp): the game quits as usual,
// then starts again with the same command line (src/main/restart.cpp).
namespace btga::restart {
    // Before recomp::start: gives the menu its Restart button.
    void install(int argc, char** argv);
    // Was Restart chosen? (the new start skips the command line's settings,
    // which are saved already; src/main/command_line.cpp)
    bool requested();
    // Just before the process ends (main's end, a quiet shutdown fault):
    // starts the game again if Restart was chosen. Once only.
    void relaunch_if_requested();
    // The environment variable the new start finds (and clears).
    constexpr const char* kRestartedVariable = "BTGA_RESTARTED";
}

#endif
