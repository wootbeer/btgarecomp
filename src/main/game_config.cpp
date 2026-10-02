// Game-specific options on the General tab: which pak each controller holds,
// and local multiplayer. See include/btga_config.h.
#include "btga_config.h"

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

    recomp::config::Config* general_config = nullptr;
}

void btga::config::add_general_options(recomp::config::Config& general) {
    general_config = &general;

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

btga::config::Accessory btga::config::get_accessory(int port) {
    if (general_config == nullptr) {
        return Accessory::None;
    }
    const std::string& id = port == 0 ? p1_accessory_id : other_accessory_id;
    return static_cast<Accessory>(std::get<uint32_t>(general_config->get_option_value(id)));
}

bool btga::config::get_local_multiplayer() {
    if (general_config == nullptr) {
        return false;
    }
    return std::get<bool>(general_config->get_option_value(local_multiplayer_id));
}
