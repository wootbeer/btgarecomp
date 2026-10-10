#ifndef __BTGA_CONFIG_H__
#define __BTGA_CONFIG_H__

#include <cstdint>

#include "librecomp/config.hpp"

// Game-specific options, shown on the General tab (src/main/game_config.cpp).
namespace btga::config {
    // What's plugged into a controller's accessory slot. Like on the N64, one
    // controller holds one pak at a time.
    enum class Accessory : uint32_t {
        ControllerPak, // saves -- src/game/controller_pak_hle.cpp
        RumblePak,
        None,
    };

    // Adds this game's options to the General tab. Call between
    // recompui::config::create_general_tab() and recompui::config::finalize().
    void add_general_options(recomp::config::Config& general);

#if defined(__ANDROID__)
    // Fills the Touch Controls tab (Android): the on-screen controls' movement style, size and
    // opacity. Call on the tab's Config before recompui::config::finalize().
    void add_touch_options(recomp::config::Config& touch);
#endif

    Accessory get_accessory(int port);
    bool get_local_multiplayer();
}

// Widescreen (src/game/widescreen.cpp): how much wider than 4:3 RT64 is
// drawing full-width 3D views -- the window aspect / (4/3) in Expand mode,
// 1 otherwise. Updated by the frontend every frame.
namespace btga {
    void set_widescreen_scale(float scale);
    float get_widescreen_scale();
}

#endif
