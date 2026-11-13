// The settings every PC in a session must share (settings.h).
#include <algorithm>
#include <atomic>
#include <cmath>

#include "librecomp/mods.hpp"
#include "ultramodern/config.hpp"

#include "btga_cheats.h"
#include "btga_config.h"
#include "settings.h"

namespace {
    std::atomic<bool> in_use{ false };
    btga::netplay::SharedSettings session{};

    const char* const kDrawDistances[] = { "Original", "Far", "Very Far", "Map Edge" };
}

btga::netplay::SharedSettings btga::netplay::own_settings() {
    SharedSettings s{};
    s.draw_distance = (uint32_t)std::clamp(btga::draw_distance_setting(), 0, 3);
    s.widescreen = ultramodern::renderer::get_graphics_config().ar_option == ultramodern::renderer::AspectRatio::Original ? 0u : 1u;
    s.widescreen_scale = s.widescreen != 0 ? btga::window_widescreen_scale() : 1.0f;
    const auto& cheats = btga::cheats::list();
    s.cheat_count = (uint32_t)std::min<size_t>(cheats.size(), kMaxCheats);
    for (uint32_t i = 0; i < s.cheat_count; i++) {
        s.cheats[i] = (uint8_t)std::min<uint32_t>(btga::cheats::get_value(i), 255);
    }
    return sane_settings(s);
}

btga::netplay::SharedSettings btga::netplay::sane_settings(const SharedSettings& in) {
    SharedSettings s = in;
    s.draw_distance = std::min(s.draw_distance, 3u);
    s.widescreen = s.widescreen != 0 ? 1u : 0u;
    if (!std::isfinite(s.widescreen_scale)) {
        s.widescreen_scale = 1.0f;
    }
    // Up to 32:9.
    s.widescreen_scale = s.widescreen != 0 ? std::clamp(s.widescreen_scale, 1.0f, 2.67f) : 1.0f;
    // Each cheat a value its menu offers (a toggle 0 or 1, a picker one of
    // its choices); this build's own list, so none it doesn't have.
    const auto& cheats = btga::cheats::list();
    s.cheat_count = (uint32_t)std::min<size_t>({ (size_t)s.cheat_count, cheats.size(), (size_t)kMaxCheats });
    for (uint32_t i = 0; i < kMaxCheats; i++) {
        if (i >= s.cheat_count) {
            s.cheats[i] = 0;
            continue;
        }
        const size_t choices = cheats[i].choices.empty() ? 2 : cheats[i].choices.size();
        if (s.cheats[i] >= choices) {
            s.cheats[i] = 0;
        }
    }
    return s;
}

void btga::netplay::use_settings(const SharedSettings& settings) {
    session = sane_settings(settings);
    in_use.store(true);
    // Both games must run the same code and data: no mods (sound packs and
    // texture packs included), for this run only (mods.json is untouched).
    recomp::mods::disable_all_mods_unsaved();
    // The picture's shape follows the host's, whatever the settings say.
    ultramodern::renderer::set_aspect_ratio_override(
        session.widescreen != 0 ? ultramodern::renderer::AspectRatio::Expand : ultramodern::renderer::AspectRatio::Original);
}

bool btga::netplay::same_settings(const SharedSettings& a, const SharedSettings& b) {
    if (a.draw_distance != b.draw_distance || a.widescreen != b.widescreen || a.widescreen_scale != b.widescreen_scale ||
        a.cheat_count != b.cheat_count) {
        return false;
    }
    for (uint32_t i = 0; i < kMaxCheats; i++) {
        if (a.cheats[i] != b.cheats[i]) {
            return false;
        }
    }
    return true;
}

bool btga::netplay::settings_in_use() {
    return in_use.load();
}

const btga::netplay::SharedSettings& btga::netplay::session_settings_in_use() {
    return session;
}

std::string btga::netplay::describe_settings(const SharedSettings& in) {
    const SharedSettings s = sane_settings(in);
    std::string text = std::string("Draw Distance: ") + kDrawDistances[s.draw_distance] + "\n";
    text += std::string("Aspect Ratio: ") + (s.widescreen != 0 ? "Expand" : "Original") + "\n";
    std::string on;
    const auto& cheats = btga::cheats::list();
    for (uint32_t i = 0; i < s.cheat_count; i++) {
        if (s.cheats[i] == 0) {
            continue;
        }
        on += on.empty() ? "" : ", ";
        on += cheats[i].name;
        if (!cheats[i].choices.empty()) {
            on += " (" + cheats[i].choices[s.cheats[i]].name + ")";
        }
    }
    text += "Cheats: " + (on.empty() ? std::string("none") : on) + "\n";
    text += "Sound packs, BattleTanx 1 sounds, reverb and extra sound channels are off online.";
    return text;
}
