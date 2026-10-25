// Game-specific options on the General tab: which pak each controller holds,
// and local multiplayer; the screen filter and bloom on the Graphics tab; and on
// Android the Touch Controls tab. See include/btga_config.h.
#include "btga_config.h"
#include "btga_android.h"

#include "recompui/config.h"
#include "rt64_screen_filter.h"

namespace {
    using Accessory = btga::config::Accessory;

    const std::string p1_accessory_id = "p1_accessory";
    const std::string other_accessory_id = "other_accessory";
    const std::string local_multiplayer_id = "local_multiplayer";

    const std::vector<recomp::config::ConfigOptionEnumOption> accessory_options = {
        {Accessory::ControllerPak, "ControllerPak", "Controller Pak"},
        {Accessory::RumblePak, "RumblePak", "Rumble Pak"},
        {Accessory::None, "None", "None"},
    };

    // Looked up by id on each read: the Config& create_general_tab() returns
    // lives in a container that moves when later tabs are created, so a
    // pointer kept from it dangles (round 96).
    bool options_added = false;
}

void btga::config::add_general_options(recomp::config::Config& general) {
    options_added = true;

    general.add_enum_option(
        p1_accessory_id,
        "Player 1 Accessory",
        "The pak plugged into player 1's controller. As on the N64, a controller holds one pak at a time: "
        "the <b>Controller Pak</b> stores saves (in the saves folder as a standard 32 KB .mpk file), "
        "the <b>Rumble Pak</b> vibrates your controller."
        "<br/><br/>"
        "Changing this is like swapping paks: the game notices the next time it checks.",
        accessory_options,
        Accessory::ControllerPak
    );

    general.add_enum_option(
        other_accessory_id,
        "Players 2-4 Accessory",
        "The pak plugged into the other players' controllers in local multiplayer. "
        "Each player's Controller Pak is a separate .mpk file.",
        accessory_options,
        Accessory::RumblePak
    );

    general.add_bool_option(
        local_multiplayer_id,
        "Local Multiplayer",
        "Lets up to 4 players use their own controllers."
        "<br/><br/>"
        "<b>Restart the game after changing this.</b> Then open the Controls tab and use "
        "<b>Assign players</b> to choose each player's controller or keyboard.",
        false
    );

}

#if defined(__ANDROID__)
// The on-screen touch controls (src/android/touch_controls.cpp), which read these through the
// change callbacks (also run when the saved settings load).
void btga::config::add_touch_options(recomp::config::Config& touch) {
    touch.add_enum_option(
        "touch_movement",
        "Movement",
        "How the on-screen controls move your tank: an analog <b>Joystick</b>, or a <b>D-pad</b> "
        "that steers in 8 directions."
        "<br/><br/>"
        "The touch controls show during a match and hide while a gamepad is in use; touch the "
        "screen to bring them back.",
        std::vector<recomp::config::ConfigOptionEnumOption>{
            {0, "Joystick", "Joystick"},
            {1, "DPad", "D-pad"},
        },
        0u
    );
    touch.add_option_change_callback("touch_movement", [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
        btga::android::set_touch_dpad(std::get<uint32_t>(value) == 1);
    });

    touch.add_number_option(
        "touch_size",
        "Size",
        "The size of the on-screen controls.",
        90, 180, 5, 0, true, 100
    );
    touch.add_option_change_callback("touch_size", [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
        btga::android::set_touch_size(std::get<double>(value));
    });

    touch.add_number_option(
        "touch_opacity",
        "Opacity",
        "How solid the on-screen controls are drawn.",
        30, 100, 5, 0, true, 30
    );
    touch.add_option_change_callback("touch_opacity", [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
        btga::android::set_touch_opacity(std::get<double>(value));
    });
}
#endif

void btga::config::add_graphics_options(recomp::config::Config& graphics) {
    const std::vector<recomp::config::ConfigOptionEnumOption> screen_filter_options = {
        {RT64::ScreenFilter::None, "None", "None"},
        {RT64::ScreenFilter::CRT, "CRT", "CRT"},
        {RT64::ScreenFilter::Scanlines, "Scanlines", "Scanlines"},
        {RT64::ScreenFilter::LCD, "LCD", "LCD"},
    };
    const std::vector<recomp::config::ConfigOptionEnumOption> bloom_options = {
        {RT64::Bloom::Off, "Off", "Off"},
        {RT64::Bloom::Low, "Low", "Low"},
        {RT64::Bloom::High, "High", "High"},
    };

    graphics.add_enum_option(
        "screen_filter",
        "Screen Filter",
        "Makes the picture look like it does on a screen of the time: "
        "<b>CRT</b> a TV, <b>Scanlines</b> just the TV's lines, <b>LCD</b> a handheld's pixel grid.",
        screen_filter_options,
        RT64::ScreenFilter::None
    );
    graphics.add_enum_option(
        "bloom",
        "Bloom",
        "A glow around bright, strongly coloured parts of the picture, like fire, explosions and energy shots.",
        bloom_options,
        RT64::Bloom::Off
    );

    // Also called when the saved settings load.
    graphics.add_option_change_callback("screen_filter",
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
            RT64::setScreenFilter(static_cast<RT64::ScreenFilter>(std::get<uint32_t>(value)));
        });
    graphics.add_option_change_callback("bloom",
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
            RT64::setBloom(static_cast<RT64::Bloom>(std::get<uint32_t>(value)));
        });
}

btga::config::Accessory btga::config::get_accessory(int port) {
    if (!options_added) {
        return Accessory::None;
    }
    const std::string& id = port == 0 ? p1_accessory_id : other_accessory_id;
    return static_cast<Accessory>(std::get<uint32_t>(recompui::config::get_general_config().get_option_value(id)));
}

bool btga::config::get_local_multiplayer() {
    if (!options_added) {
        return false;
    }
    return std::get<bool>(recompui::config::get_general_config().get_option_value(local_multiplayer_id));
}
