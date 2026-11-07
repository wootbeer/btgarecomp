// Mouse Stick on the General tab (round 163): the mouse's motion as player
// 1's analog stick, for turning the tank with the mouse. Off by default.
//
// Turn uses the mouse's left-right motion only, so the keys or a controller
// still drive forward and back. Full also uses its up-down motion (up is
// forward). While the game runs with no menu open the mouse is captured in the
// window (lib-patches/RecompFrontend/0015, from snprecomp's 0001). The stick
// follows the mouse's speed, so a faster mouse turns harder: at 100%
// sensitivity, 750 pixels a second is the stick all the way over.
//
// The speed is the motion over the last kWindow, not since the game's last
// controller read (round 164). Motion reaches the game in bursts, once per
// rendered frame, and the game's reads don't line up with them. Read by read,
// a hard turn alternated between a full stick and a partial or empty one, and
// the tank wobbled.
//
// Mouse buttons, the side buttons and the wheel's notches are bound like keys
// on the Controls tab (the same patch, from snprecomp's 0008). By default the
// left button fires the main gun (Z) and the right one is R (src/main/main.cpp).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>

#include "recompinput/input_events.h"
#include "recompinput/input_state.h"
#include "recompinput/profiles.h"

#include "btga_config.h"

namespace {
    std::atomic<int> mode{ 0 }; // 0 Off, 1 Turn, 2 Full
    std::atomic<float> sensitivity{ 1.0f };

    constexpr double kPixelsPerSecondForFullStick = 750.0;
    constexpr std::chrono::milliseconds kWindow{ 80 };

    // The mouse's total motion at each controller read, newest last (the
    // game's thread only).
    struct Sample {
        std::chrono::steady_clock::time_point time;
        double x, y;
    };
    std::deque<Sample> samples;

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
    const auto now = std::chrono::steady_clock::now();
    double total_x = 0.0, total_y = 0.0;
    recompinput::get_mouse_motion_total(&total_x, &total_y);
    samples.push_back({ now, total_x, total_y });

    // The newest sample at least kWindow old, or the oldest kept.
    while (samples.size() > 2 && now - samples[1].time >= kWindow) {
        samples.pop_front();
    }
    const Sample& start = samples.front();
    const double seconds = std::chrono::duration<double>(now - start.time).count();
    if (seconds <= 0.0) {
        return ret;
    }
    // Over a longer gap (the game paused, or a menu was open) only the last
    // window's worth counts.
    const double span = std::max(seconds, std::chrono::duration<double>(kWindow).count());
    const double scale = sensitivity.load() / kPixelsPerSecondForFullStick / span;
    *x_out = std::clamp(*x_out + (float)((total_x - start.x) * scale), -1.0f, 1.0f);
    if (cur_mode == 2) {
        *y_out = std::clamp(*y_out - (float)((total_y - start.y) * scale), -1.0f, 1.0f);
    }
    return ret;
}
