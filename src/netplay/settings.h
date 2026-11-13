#ifndef __BTGA_NETPLAY_SETTINGS_H__
#define __BTGA_NETPLAY_SETTINGS_H__

#include <cstdint>
#include <string>

// The settings that change what the game does, so every PC in a session must
// have the same (settings.cpp): the host's, sent in its Welcome and Start.
// Found with the sync test (STATUS.md round 198): Draw Distance and the aspect
// ratio change what the game culls and loads, and the cheats and hacks write
// to its memory. Sound packs, the BattleTanx 1 sounds, reverb, extra sound
// channels and Controller Paks are off for everyone in a session; everything
// else (fog, volume, frame rate, the picture, controls) stays each player's.
namespace btga::netplay {
    constexpr uint32_t kMaxCheats = 160;

    struct SharedSettings {
        uint32_t draw_distance = 0;     // 0 Original, 1 Far, 2 Very Far, 3 Map Edge
        uint32_t widescreen = 0;        // 0 the game's 4:3, 1 Expand
        float widescreen_scale = 1.0f;  // Expand: how much wider the game culls (the host's window)
        uint32_t cheat_count = 0;
        uint8_t cheats[kMaxCheats] = {}; // each cheat's value, in btga::cheats::list()'s order
    };

    // This PC's own, as a host offers them.
    SharedSettings own_settings();
    // A peer's, as this game may use them: every value one the menus offer.
    SharedSettings sane_settings(const SharedSettings& settings);
    // The session's from now on (and its fixed ones: mods off, the aspect ratio
    // held). Until the program ends.
    void use_settings(const SharedSettings& settings);
    // For the joiner's prompt and the log.
    std::string describe_settings(const SharedSettings& settings);
    // The session's settings are in use.
    bool settings_in_use();
    const SharedSettings& session_settings_in_use();
}

#endif
