// Game-specific options on the General tab: which pak each controller holds,
// and local multiplayer; on Android also the Touch Controls tab. See
// include/btga_config.h.
#include "btga_config.h"
#include "btga_android.h"

#include "recompui/config.h"

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
