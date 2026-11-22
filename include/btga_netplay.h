#ifndef __BTGA_NETPLAY_H__
#define __BTGA_NETPLAY_H__

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "ultramodern/renderer_context.hpp"

// Netplay (src/netplay/). Everything outside that folder reaches it only
// through this header; a build without it (BTGA_NETPLAY off in CMake, e.g.
// Android) gets these empty versions instead.
namespace btga::netplay {
#ifdef BTGA_NETPLAY
    // Before the game starts, after the settings are loaded
    // (src/main/main.cpp): `version` is the build (everyone in a session
    // needs the same). The testing switches (BTGA_SYNCTEST, BTGA_NETPLAY) put
    // the runtime in its deterministic mode.
    void startup(const char* version);
    // The program is closing: the other players are told (and after the
    // Netplay tab's Leave Game, the game starts again on the Netplay screen).
    void quitting();
    // The command line (--netplay: open the Netplay screen at the start).
    void command_line(int argc, char** argv);
    // The Chat control's description and default key, before the controls
    // are loaded (src/main/main.cpp).
    void set_control_defaults();
    // The launcher has a Netplay entry: it opens the Netplay screen for the
    // game `game_id`. launcher_update each launcher frame.
    constexpr bool kLauncherEntry = true;
    void open_screen(const std::u8string& game_id);
    void launcher_update(const std::u8string& game_id);
    // The menu's Netplay tab, shown in a session (before the config is
    // finalized).
    void create_menu_tab();
    // The renderer, wrapped: in a session each PC shows only its own
    // player's view, full screen (src/netplay/own_view.cpp).
    std::unique_ptr<ultramodern::renderer::RendererContext> wrap_renderer(
        uint8_t* rdram, std::unique_ptr<ultramodern::renderer::RendererContext> inner);
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
    // An online session is set up (its settings in use): from before the
    // game starts until the program ends.
    bool session_active();
    // The session's players (their ports have a controller), 0 outside one.
    int session_players();
    // Settings that must be the same on every PC (src/netplay/settings.h):
    // the session's, or this PC's own (`own`) outside one.
    uint32_t cheat_value(size_t index, uint32_t own);
    int draw_distance(int own);
    float widescreen_scale(float own);
    bool steady_scenery(bool own);
#else
    inline void startup(const char*) {}
    inline void quitting() {}
    inline void command_line(int, char**) {}
    inline void set_control_defaults() {}
    constexpr bool kLauncherEntry = false;
    inline void open_screen(const std::u8string&) {}
    inline void launcher_update(const std::u8string&) {}
    inline void create_menu_tab() {}
    inline std::unique_ptr<ultramodern::renderer::RendererContext> wrap_renderer(
        uint8_t*, std::unique_ptr<ultramodern::renderer::RendererContext> inner) { return inner; }
    inline void frame(uint8_t*) {}
    inline bool deterministic() { return false; }
    inline bool controller_read(int, bool answered, uint16_t*, float*, float*) { return answered; }
    inline bool session_active() { return false; }
    inline int session_players() { return 0; }
    inline uint32_t cheat_value(size_t, uint32_t own) { return own; }
    inline int draw_distance(int own) { return own; }
    inline float widescreen_scale(float own) { return own; }
    inline bool steady_scenery(bool own) { return own; }
#endif
}

#endif
