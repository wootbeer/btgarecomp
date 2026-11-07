// Mouse Stick on the General tab (round 163): the mouse's motion as player
// 1's analog stick, for turning the tank with the mouse. Off by default.
//
// Turn uses the mouse's left-right motion only, so the keys or a controller
// still drive forward and back. Full also uses its up-down motion (up is
// forward). While the game runs with no menu open the mouse is captured in the
// window (lib-patches/RecompFrontend/0015, from snprecomp's 0001), and the
// game reads the motion since its last controller read, so a faster mouse
// turns harder. At 100% sensitivity, 25 pixels between two reads is the stick
// all the way over.
//
// Mouse buttons, the side buttons and the wheel's notches are bound like keys
// on the Controls tab (the same patch, from snprecomp's 0008).
#include <algorithm>
#include <atomic>
#include <cstdint>

#include "recompinput/input_events.h"
#include "recompinput/input_state.h"
#include "recompinput/profiles.h"

#include "btga_config.h"

namespace {
    std::atomic<int> mode{ 0 }; // 0 Off, 1 Turn, 2 Full
    std::atomic<float> sensitivity{ 1.0f };

    constexpr float kPixelsForFullStick = 25.0f;

    recompinput::GameMouse game_mouse() {
        return mode.load() != 0 ? recompinput::GameMouse::Captured : recompinput::GameMouse::Default;
    }
}

void btga::set_mouse_stick(int new_mode, double percent) {
    mode.store(new_mode);
    sensitivity.store((float)(percent / 100.0));
}

void btga::init_mouse_stick() {
    recompinput::set_game_mouse_callback(game_mouse);
}

bool btga::get_n64_input_with_mouse(int player_index, uint16_t* buttons_out, float* x_out, float* y_out) {
    bool ret = recompinput::profiles::get_n64_input(player_index, buttons_out, x_out, y_out);
    const int cur_mode = mode.load();
    if (player_index != 0 || cur_mode == 0 || recompinput::game_input_disabled()) {
        return ret;
    }
    float dx = 0.0f, dy = 0.0f;
    recompinput::get_mouse_deltas_raw(&dx, &dy);
    const float scale = sensitivity.load() / kPixelsForFullStick;
    *x_out = std::clamp(*x_out + dx * scale, -1.0f, 1.0f);
    if (cur_mode == 2) {
        *y_out = std::clamp(*y_out - dy * scale, -1.0f, 1.0f);
    }
    return ret;
}
