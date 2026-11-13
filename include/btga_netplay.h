#ifndef __BTGA_NETPLAY_H__
#define __BTGA_NETPLAY_H__

#include <cstdint>

// Netplay (src/netplay/). Everything outside that folder reaches it only
// through this header; a build without it (BTGA_NETPLAY off in CMake, e.g.
// Android) gets these empty versions instead.
namespace btga::netplay {
#ifdef BTGA_NETPLAY
    // Before the game starts (src/main/main.cpp): the testing switches
    // (BTGA_SYNCTEST), which put the runtime in its deterministic mode.
    void startup();
    // The game's frame clear (src/game/widescreen.cpp).
    void frame(uint8_t* rdram);
    // The runtime is in its deterministic mode: the game's time is counted
    // in retraces and outside events reach it only when all its threads wait.
    bool deterministic();
    // Each port's controller read as it goes to the game (src/main/main.cpp,
    // after the controls and the mouse): `answered` and the read are this
    // PC's; netplay may record, replace or exchange them. Returns whether the
    // controller answers.
    bool controller_read(int port, bool answered, uint16_t* buttons, float* x, float* y);
#else
    inline void startup() {}
    inline void frame(uint8_t*) {}
    inline bool deterministic() { return false; }
    inline bool controller_read(int, bool answered, uint16_t*, float*, float*) { return answered; }
#endif
}

#endif
