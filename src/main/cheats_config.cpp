// The Cheats tab (the game's own codes as toggles) and the Hacks tab (the
// GameShark and Action Replay codes, each under its own heading), kept apart
// so it's clear which are part of the game. A long tab is also slow to
// update (round 140). The game side is src/game/cheats.cpp.
#include "btga_cheats.h"

#include "recompui/config.h"

namespace {
    const std::string cheats_id = "cheats";
    const std::string hacks_id = "hacks";

    struct Heading {
        btga::cheats::Section section;
        const char* title;
        const char* note;
    };

    const Heading headings[] = {
        { btga::cheats::Section::GameShark, "GameShark Codes",
          "Codes players made for the GameShark. They're not part of the game, so expect glitches, "
          "and turn them off if something goes wrong. Some only take effect when a level starts." },
        { btga::cheats::Section::ActionReplay, "Action Replay Codes",
          "Codes players made for the Action Replay. Same as GameShark codes: not part of the game, "
          "so expect glitches." },
    };
}

void btga::cheats::add_cheats_tabs() {
    recompui::config::create_config_tab("Cheats", cheats_id, false);
    recompui::config::create_config_tab("Hacks", hacks_id, false);
    const std::vector<Cheat>& all = list();
    for (size_t i = 0; i < all.size(); i++) {
        const Cheat& cheat = all[i];
        // Looked up by id: the Config& create_config_tab() returns lives in a
        // container that moves when later tabs are made.
        recomp::config::Config& tab = recompui::config::get_config(cheat.section == Section::Stock ? cheats_id : hacks_id);
        if (i == 0 || all[i - 1].section != cheat.section) {
            for (const Heading& heading : headings) {
                if (heading.section == cheat.section) {
                    recompui::config::add_option_heading(hacks_id, cheat.id, heading.title, heading.note);
                }
            }
        }

        if (cheat.choices.empty()) {
            tab.add_bool_option(cheat.id, cheat.name, cheat.description, false);
        }
        else {
            std::vector<recomp::config::ConfigOptionEnumOption> options;
            for (size_t c = 0; c < cheat.choices.size(); c++) {
                options.emplace_back((uint32_t)c, cheat.choices[c].key, cheat.choices[c].name);
            }
            tab.add_enum_option(cheat.id, cheat.name, cheat.description, options, 0u);
        }

        // Also called when the saved settings load.
        tab.add_option_change_callback(cheat.id,
            [i](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
                if (const bool* on = std::get_if<bool>(&value)) {
                    set_value(i, *on ? 1 : 0);
                }
                else if (const uint32_t* choice = std::get_if<uint32_t>(&value)) {
                    set_value(i, *choice);
                }
            });
    }
}
