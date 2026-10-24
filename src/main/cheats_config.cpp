// The Cheats tab: the game's own codes as toggles, then the GameShark and
// Action Replay codes, each under its own heading so it's clear which are
// part of the game. The game side is src/game/cheats.cpp.
#include "btga_cheats.h"

#include "recompui/config.h"

namespace {
    const std::string cheats_id = "cheats";

    struct Heading {
        btga::cheats::Section section;
        const char* title;
        const char* note;
    };

    const Heading headings[] = {
        { btga::cheats::Section::Stock, "Stock Codes",
          "The game's own codes, the same as typing them in on the code screen." },
        { btga::cheats::Section::GameShark, "GameShark Codes",
          "Codes players made for the GameShark. They're not part of the game, so expect glitches, "
          "and turn them off if something goes wrong. Some only take effect when a level starts." },
        { btga::cheats::Section::ActionReplay, "Action Replay Codes",
          "Codes players made for the Action Replay. Same as GameShark codes: not part of the game, "
          "so expect glitches." },
    };
}

void btga::cheats::add_cheats_tab() {
    recomp::config::Config& cheats = recompui::config::create_config_tab("Cheats", cheats_id, false);
    const std::vector<Cheat>& all = list();
    for (size_t i = 0; i < all.size(); i++) {
        const Cheat& cheat = all[i];
        if (i == 0 || all[i - 1].section != cheat.section) {
            for (const Heading& heading : headings) {
                if (heading.section == cheat.section) {
                    recompui::config::add_option_heading(cheats_id, cheat.id, heading.title, heading.note);
                }
            }
        }

        if (cheat.choices.empty()) {
            cheats.add_bool_option(cheat.id, cheat.name, cheat.description, false);
        }
        else {
            std::vector<recomp::config::ConfigOptionEnumOption> options;
            for (size_t c = 0; c < cheat.choices.size(); c++) {
                options.emplace_back((uint32_t)c, cheat.choices[c].key, cheat.choices[c].name);
            }
            cheats.add_enum_option(cheat.id, cheat.name, cheat.description, options, 0u);
        }

        // Also called when the saved settings load.
        cheats.add_option_change_callback(cheat.id,
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
