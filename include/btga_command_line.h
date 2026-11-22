#ifndef __BTGA_COMMAND_LINE_H__
#define __BTGA_COMMAND_LINE_H__

#include <string>

// Command-line arguments (src/main/command_line.cpp; COMMAND_LINE.md has the
// player's guide). Every option in the settings tabs can be set by its id,
// e.g. `--graphics.wm_option=Fullscreen` or `--draw_distance=Far`; they're
// saved like a change in the menu. Also --rom, --play, --fullscreen,
// --windowed, --list-settings, --help and --version.
namespace btga::cli {
    // First thing in main(). --help and --version print and exit here.
    void parse(int argc, char** argv, const std::string& version);

    // New installs keep their settings, saves and mods next to the game
    // (portable.txt), unless the game already has a settings folder in the
    // user's profile. After the program id is set, before anything reads
    // the app folder.
    void default_to_portable();

    // After recompui::config::finalize() and the games are registered:
    // --list-settings (prints and exits), the settings, --rom.
    void apply_after_config(const std::u8string& game_id);

    // Each launcher frame: --play starts the game once the launcher is up;
    // problems with the arguments are shown in a prompt otherwise.
    void launcher_update(const std::u8string& game_id, const std::string& mod_game_id);
}

#endif
