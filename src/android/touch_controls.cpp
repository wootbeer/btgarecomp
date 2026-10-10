// On-screen touch controls (Android). The game's layout and its N64 buttons, on top of descore's touch
// module (src/android/descore/touch), as gsr_controls.c is for Golden Sun in gsrandroid.
//
// Java (TouchControlsView) runs everything here on the UI thread: it forwards touches and the screen
// size, and draws the controls from descore_touch_get_shapes(). Their settings come from the General
// tab (src/main/game_config.cpp) and the game thread reads the buttons and stick (add_touch_input),
// both through atomics.
#include <jni.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

extern "C" {
#include "descore/touch/descore_touch.h"
}

#include "btga_android.h"
#include "recompinput/input_state.h"
#include "ultramodern/ultramodern.hpp"

namespace {
    // Buttons, in descore's button order; a held button sends its own index as its one key.
    enum Button {
        BUTTON_A,
        BUTTON_B,
        BUTTON_Z,
        BUTTON_L,
        BUTTON_R,
        BUTTON_START,
        BUTTON_C_UP,
        BUTTON_C_DOWN,
        BUTTON_C_LEFT,
        BUTTON_C_RIGHT,
        BUTTON_MENU, // a tap button: opens the game's menu (Java sends Escape)
        BUTTON_COUNT
    };

    // libultra's controller button bits, for the buttons up to BUTTON_C_RIGHT.
    constexpr uint16_t kN64Bits[] = {
        0x8000, // A_BUTTON
        0x4000, // B_BUTTON
        0x2000, // Z_TRIG
        0x0020, // L_TRIG
        0x0010, // R_TRIG
        0x1000, // START_BUTTON
        0x0008, // U_CBUTTONS
        0x0004, // D_CBUTTONS
        0x0002, // L_CBUTTONS
        0x0001, // R_CBUTTONS
    };

    const char* const kLabels[BUTTON_COUNT] = {
        "A", "B", "Z", "L", "R", "START", "C▲", "C▼", "C◀", "C▶", "MENU",
    };

    std::atomic<uint32_t> held_buttons{ 0 };
    std::atomic<float> stick_x{ 0.0f };
    std::atomic<float> stick_y{ 0.0f };

    // Settings, from the General tab; laid out again by Java when they change.
    std::atomic<bool> setting_dpad{ false };
    std::atomic<double> setting_size{ 100.0 };
    std::atomic<double> setting_opacity{ 30.0 };
    std::atomic<bool> settings_changed{ false };

    bool dpad_style = false;
    bool menu_tapped = false;

    // The descore Size step nearest to a size in percent.
    int scale_step_for(double percent) {
        int best = DESCORE_TOUCH_SCALE_DEFAULT_STEP;
        for (int i = 0; i < DESCORE_TOUCH_SCALE_NUM_STEPS; i++) {
            if (std::abs(kDescoreTouchScaleValues[i] * 100.0 - percent) <
                std::abs(kDescoreTouchScaleValues[best] * 100.0 - percent)) {
                best = i;
            }
        }
        return best;
    }

    void on_key(unsigned char key, int down) {
        if (key >= BUTTON_MENU) {
            return;
        }
        if (down) {
            held_buttons.fetch_or(kN64Bits[key]);
        }
        else {
            held_buttons.fetch_and(~(uint32_t)kN64Bits[key]);
        }
    }

    void on_tap(int button) {
        if (button == BUTTON_MENU) {
            menu_tapped = true;
        }
    }

    // descore's y grows downwards; the N64's upwards. In D-pad style the pad gives the 8 directions at
    // full deflection (ultramodern maps the diagonals onto the N64's octagon).
    void on_stick(int, float x, float y) {
        if (dpad_style) {
            const unsigned dirs = descore_touch_dpad_dirs(x, y);
            x = ((dirs & DESCORE_DPAD_RIGHT) ? 1.0f : 0.0f) - ((dirs & DESCORE_DPAD_LEFT) ? 1.0f : 0.0f);
            y = ((dirs & DESCORE_DPAD_DOWN) ? 1.0f : 0.0f) - ((dirs & DESCORE_DPAD_UP) ? 1.0f : 0.0f);
        }
        stick_x.store(x);
        stick_y.store(-y);
    }

    void set_button(Button button, float x, float y, float w, float h) {
        const unsigned char key = (unsigned char)button;
        if (button == BUTTON_MENU) {
            descore_touch_set_button(button, x, y, w, h, nullptr, 0, false);
        }
        else {
            descore_touch_set_button(button, x, y, w, h, &key, 1, false);
        }
    }

    // Stick bottom left with Z above it; A and B bottom right under the C buttons; L and R in the top
    // corners; START bottom centre, MENU top centre. Sizes follow the screen height and the Size setting.
    void layout(int width, int height) {
        const float w = (float)width;
        const float h = (float)height;
        const float scale = descore_touch_scale_value(descore_touch_scale_step());
        const float c = std::min(std::max(h * 0.12f * scale, descore_touch_min_cell_px()), h * 0.18f);
        const float m = h * 0.04f;
        const float gap = c * 0.12f;

        descore_touch_set_screen_size(width, height);

        const float r = c * 1.25f;
        const float stick_cx = m + r * 1.15f;
        const float stick_cy = h - m - r * 1.15f;
        descore_touch_set_stick(0, stick_cx, stick_cy, r, on_stick);
        descore_touch_set_stick_style(0, dpad_style ? DESCORE_TOUCH_STICK_DPAD : DESCORE_TOUCH_STICK_ANALOG);
        set_button(BUTTON_Z, m, stick_cy - r * 1.3f - gap - c, c * 1.3f, c);

        set_button(BUTTON_A, w - m - c, h - m - c, c, c);
        set_button(BUTTON_B, w - m - c * 2.0f - gap, h - m - c, c, c);

        const float s = c * 0.75f;
        const float cx = w - m - c - gap * 0.5f;
        const float cy = h - m - c - gap - s * 1.7f;
        set_button(BUTTON_C_UP, cx - s * 0.5f, cy - s * 1.5f - gap, s, s);
        set_button(BUTTON_C_DOWN, cx - s * 0.5f, cy + s * 0.5f + gap, s, s);
        set_button(BUTTON_C_LEFT, cx - s * 1.5f - gap, cy - s * 0.5f, s, s);
        set_button(BUTTON_C_RIGHT, cx + s * 0.5f + gap, cy - s * 0.5f, s, s);

        set_button(BUTTON_L, m, m, c * 1.6f, c * 0.7f);
        set_button(BUTTON_R, w - m - c * 1.6f, m, c * 1.6f, c * 0.7f);

        set_button(BUTTON_START, w * 0.5f - c * 0.7f, h - m - c * 0.6f, c * 1.4f, c * 0.6f);
        set_button(BUTTON_MENU, w * 0.5f - c * 0.7f, m, c * 1.4f, c * 0.6f);
    }

    bool initialized = false;
}

void btga::android::set_touch_dpad(bool dpad) {
    setting_dpad.store(dpad);
    settings_changed.store(true);
}

void btga::android::set_touch_size(double percent) {
    setting_size.store(percent);
    settings_changed.store(true);
}

void btga::android::set_touch_opacity(double percent) {
    setting_opacity.store(percent);
    settings_changed.store(true);
}

void btga::android::add_touch_input(uint16_t* buttons, float* x, float* y) {
    *buttons |= (uint16_t)held_buttons.load();
    const float tx = stick_x.load();
    const float ty = stick_y.load();
    if ((tx != 0.0f) || (ty != 0.0f)) {
        *x = tx;
        *y = ty;
    }
}

extern "C" {

// Screen size; lays the controls out again with the current settings.
JNIEXPORT void JNICALL Java_io_github_wootbeer_btgarecomp_TouchControlsView_nativeLayout(
    JNIEnv*, jclass, jint width, jint height, jfloat density) {
    if (!initialized) {
        descore_touch_init(BUTTON_COUNT, on_key, on_tap);
        initialized = true;
    }
    settings_changed.store(false);
    descore_touch_set_display_density(density);
    descore_touch_set_scale_step(scale_step_for(setting_size.load()));
    descore_touch_set_opacity((float)(setting_opacity.load() / 100.0));
    dpad_style = setting_dpad.load();
    layout(width, height);
}

// One pointer of a touch event. Bit 0: taken by the controls; bit 1: MENU was tapped.
JNIEXPORT jint JNICALL Java_io_github_wootbeer_btgarecomp_TouchControlsView_nativeTouch(
    JNIEnv*, jclass, jint action, jint pointer_id, jfloat x, jfloat y) {
    menu_tapped = false;
    const bool taken = initialized && descore_touch_handle(action, pointer_id, x, y);
    return (taken ? 1 : 0) | (menu_tapped ? 2 : 0);
}

JNIEXPORT jboolean JNICALL Java_io_github_wootbeer_btgarecomp_TouchControlsView_nativeHitTest(
    JNIEnv*, jclass, jfloat x, jfloat y) {
    return initialized && descore_touch_hit_test(x, y);
}

// Hides the controls (releasing whatever they held) while a gamepad is in use.
JNIEXPORT void JNICALL Java_io_github_wootbeer_btgarecomp_TouchControlsView_nativeSetGamepadConnected(
    JNIEnv*, jclass, jboolean connected) {
    descore_touch_set_gamepad_connected(connected);
}

// Shapes to draw, 7 floats each (kind, button, x, y, w, h, alpha); returns how many.
JNIEXPORT jint JNICALL Java_io_github_wootbeer_btgarecomp_TouchControlsView_nativeGetShapes(
    JNIEnv* env, jclass, jfloatArray out) {
    if (!initialized) {
        return 0;
    }
    DescoreTouchShape shapes[DESCORE_TOUCH_MAX_BUTTONS + 16];
    const jsize capacity = env->GetArrayLength(out) / 7;
    const int count = descore_touch_get_shapes(shapes, std::min<int>(capacity, (int)(sizeof(shapes) / sizeof(shapes[0]))));
    jfloat* values = env->GetFloatArrayElements(out, nullptr);
    for (int i = 0; i < count; i++) {
        jfloat* v = values + i * 7;
        v[0] = (jfloat)shapes[i].kind;
        v[1] = (jfloat)shapes[i].button;
        v[2] = shapes[i].x;
        v[3] = shapes[i].y;
        v[4] = shapes[i].w;
        v[5] = shapes[i].h;
        v[6] = shapes[i].alpha;
    }
    env->ReleaseFloatArrayElements(out, values, 0);
    return count;
}

JNIEXPORT jstring JNICALL Java_io_github_wootbeer_btgarecomp_TouchControlsView_nativeButtonLabel(
    JNIEnv* env, jclass, jint button) {
    return env->NewStringUTF((button >= 0 && button < BUTTON_COUNT) ? kLabels[button] : "");
}

// Bit 0: the controls are for now -- a match takes input (not on the launcher, nor while one of the
// game's menus has it). Bit 1: the settings changed, lay out again. Bit 2: the game has started.
JNIEXPORT jint JNICALL Java_io_github_wootbeer_btgarecomp_TouchControlsView_nativePoll(
    JNIEnv*, jclass) {
    const bool started = ultramodern::is_game_started();
    const bool active = started && !recompinput::game_input_disabled();
    return (active ? 1 : 0) | (settings_changed.load() ? 2 : 0) | (started ? 4 : 0);
}

}
