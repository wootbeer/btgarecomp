#pragma once

#include <cstdint>
#include <string>

#if defined(__ANDROID__)
namespace btga::android {
    // Called first thing in main(): sends stdout/stderr to logcat (tag "BTGA") and makes the
    // app's private storage folder the working directory, which is where the activity has
    // copied assets/ and where relative paths such as crash_log.txt land.
    void startup();

    // If the launcher (RomPickerActivity) left a picked file, validates it as the ROM for
    // game_id and stores it where the game loads it from (recomp::select_rom), then deletes the
    // picked copy. Says what's wrong in a message box if it isn't the right ROM. Call after the
    // config path and games are registered, before recomp::start().
    void import_picked_rom(const std::u8string& game_id);

    // The on-screen touch controls (src/android/touch_controls.cpp): ORs in the buttons they hold,
    // and replaces the stick while theirs is off centre.
    void add_touch_input(uint16_t* buttons, float* x, float* y);

    // The touch controls' settings from the General tab (src/main/game_config.cpp): D-pad rather
    // than joystick, size in percent (90-180), opacity in percent (30-100).
    void set_touch_dpad(bool dpad);
    void set_touch_size(double percent);
    void set_touch_opacity(double percent);

    // Builds the launcher menu: recompui's default one plus Change ROM
    // (src/android/android_launcher.cpp). Call before recomp::start().
    void register_launcher();
}
#endif
