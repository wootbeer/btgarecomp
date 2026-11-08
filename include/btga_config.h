#ifndef __BTGA_CONFIG_H__
#define __BTGA_CONFIG_H__

#include <cstdint>

#include "librecomp/config.hpp"
#include "recomp.h"

// Game-specific options, shown on the General and Graphics tabs
// (src/main/game_config.cpp).
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

    // Screen Filter and Bloom at the end of the Graphics tab, passed straight
    // to RT64 (lib-patches/rt64/0018, 0019), then Fog and Draw Distance
    // (src/game/view_distance.cpp). Call between
    // recompui::config::create_graphics_tab() and recompui::config::finalize().
    void add_graphics_options(recomp::config::Config& graphics);

    // BattleTanx 1 Sound Effects and its Load BattleTanx ROM button on the
    // Sound tab (src/main/bt1_sounds.cpp). Call between
    // recompui::config::create_sound_tab() and recompui::config::finalize().
    void add_bt1_sounds_option(recomp::config::Config& sound);
    // Sound Effects Reverb and its amount (src/main/game_config.cpp).
    void add_sfx_reverb_options(recomp::config::Config& sound);
    // Sound Channels (src/main/game_config.cpp).
    void add_sound_channels_option(recomp::config::Config& sound);

    Accessory get_accessory(int port);
    bool get_local_multiplayer();
}

// Widescreen (src/game/widescreen.cpp): how much wider than 4:3 RT64 is
// drawing full-width 3D views -- the window aspect / (4/3) in Expand mode,
// 1 otherwise. Updated by the frontend every frame.
namespace btga {
    void set_widescreen_scale(float scale);
    float get_widescreen_scale();

    // The game entry's on_init_callback (src/main/bt1_sounds.cpp): swaps in
    // the first game's sound effects when that option is on.
    void apply_bt1_sounds(uint8_t* rdram, recomp_context* context);

    // Graphics tab options read by the game's hooks (src/game/view_distance.cpp).
    void set_fog_enabled(bool enabled);
    // 0 Original, 1 Far, 2 Very Far, 3 Map Edge.
    void set_draw_distance(int level);
    int draw_distance_level();

    // Sound tab: Sound Effects Reverb (src/game/sfx_reverb.cpp), amount 0-100.
    void set_sfx_reverb(bool on, double percent);
    // Sound tab: Sound Channels (src/game/sound_channels.cpp), read once at boot.
    void set_sound_channels(int count);

    // General tab: Mouse Stick (src/main/mouse_stick.cpp). Mode 0 Off, 1 Turn,
    // 2 Full; sensitivity in percent.
    void set_mouse_stick(int mode, double percent);
    // Lets the mouse be captured while Mouse Stick is on. Call once at startup.
    void init_mouse_stick();
    // The input callback: recompinput's, plus the mouse for player 1.
    bool get_n64_input_with_mouse(int player_index, uint16_t* buttons_out, float* x_out, float* y_out);
}

#endif
