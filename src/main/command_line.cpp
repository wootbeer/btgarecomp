// Command-line arguments (include/btga_command_line.h, COMMAND_LINE.md).
//
// Settings: any option of the settings tabs by its id, `--<id>=<value>` or
// `--<tab>.<id>=<value>` (or `--set <id>=<value>`), applied right after the
// saved settings load and saved with them, as a change in the menu would be:
// the tabs without an Apply button save whenever they're closed anyway, so a
// setting for one run only would end up saved the first time its tab was
// opened. Enum values by their key or their name (case and spaces ignored) or
// number, numbers in the option's range, on/off for switches.
//
// Also: --rom <file> (as the launcher's Load ROM), --play (straight into the
// game, as its Start Game), --fullscreen / --windowed, --list-settings,
// --help, --version. A file name with no option before it is taken as --rom.
// The runtime's own --game / --game-mode and netplay's --netplay are left to
// them.
//
// Portable by default (round 226): a new install keeps its settings next to
// the game (portable.txt in the game's folder); an existing settings folder in
// the user's profile (from an earlier version) is left in use.
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#endif

#include "librecomp/config.hpp"
#include "librecomp/game.hpp"
#include "recompui/config.h"
#include "recompui/recompui.h"
#include "util/file.h"

#include "btga_command_line.h"
#include "btga_restart.h"

namespace {
    struct Setting {
        std::string name;  // "draw_distance" or "graphics.draw_distance"
        std::string value;
        std::string given; // as written, for messages
    };

    std::vector<Setting> settings;
    std::string rom_argument; // UTF-8
    bool play = false;
    bool list_settings = false;
    std::vector<std::string> problems; // shown on the launcher (and printed)
    bool play_done = false;

    // The settings tabs, in the menu's order. Tabs that aren't settings lists
    // (Controls, Mods, Netplay) aren't configs, or have no options.
    const char* const kTabs[] = { "general", "graphics", "sound", "cheats", "hacks" };

    std::string version; // "0.1.12"

    // The release build is a Windows GUI program with no console: printing
    // to the console that started it, if any.
    void use_parent_console() {
#ifdef _WIN32
        static bool done = false;
        if (done) {
            return;
        }
        done = true;
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
            std::printf("\n");
        }
#endif
    }

    std::vector<std::string> utf8_arguments(int argc, char** argv) {
        std::vector<std::string> out;
#ifdef _WIN32
        // argv is in the ANSI code page there: a file name with other letters
        // would be lost. The wide command line, as UTF-8.
        int count = 0;
        if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
            for (int i = 0; i < count; i++) {
                const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
                std::string text(size > 0 ? (size_t)size - 1 : 0, '\0');
                if (size > 1) {
                    WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), size, nullptr, nullptr);
                }
                out.push_back(text);
            }
            LocalFree(wide);
            return out;
        }
#endif
        for (int i = 0; i < argc; i++) {
            out.emplace_back(argv[i]);
        }
        return out;
    }

    std::filesystem::path path_from_utf8(const std::string& text) {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }

    // For matching enum names: lower case, no spaces, dashes or underscores.
    std::string folded(const std::string& text) {
        std::string out;
        for (char c : text) {
            if (c == ' ' || c == '-' || c == '_') {
                continue;
            }
            out.push_back((char)std::tolower((unsigned char)c));
        }
        return out;
    }

    void print_help() {
        std::printf(
            "BattleTanx: Global Assault Recompiled%s%s\n"
            "\n"
            "Usage: BattleTanxGARecompiled [options] [ROM file]\n"
            "\n"
            "  --rom <file>          Load this ROM, as the launcher's Load ROM does (a file\n"
            "                        name on its own does the same)\n"
            "  --play                Start the game straight away, skipping the launcher\n"
            "  --fullscreen          Fullscreen (graphics.wm_option=Fullscreen)\n"
            "  --windowed            Windowed (graphics.wm_option=Windowed)\n"
            "  --<setting>=<value>   Change a setting, e.g. --draw_distance=Far or\n"
            "                        --graphics.res_option=Original2x. Saved, as in the menu.\n"
            "  --set <setting>=<value>  The same\n"
            "  --list-settings       Every setting, its values and what it's set to now\n"
            "  --netplay             Open the Netplay screen once the launcher is up\n"
            "  --game btga           Start the game (the runtime's own option)\n"
            "  --version             The version\n"
            "  --help                This list\n"
            "\n"
            "Settings are the options in the menu's General, Graphics, Sound, Cheats and\n"
            "Hacks tabs, by their id. Values: a choice by its name (any case, spaces\n"
            "optional) or number, a number in the option's range, on/off for switches.\n"
            "See COMMAND_LINE.md.\n",
            version.empty() ? "" : " ", version.c_str());
    }

    std::string value_text(const recomp::config::ConfigOption& option, const recomp::config::ConfigValueVariant& value) {
        using namespace recomp::config;
        switch (option.type) {
        case ConfigOptionType::Enum: {
            const auto& e = std::get<ConfigOptionEnum>(option.variant);
            if (const uint32_t* v = std::get_if<uint32_t>(&value)) {
                auto it = e.find_option_from_value(*v);
                if (it != e.options.end()) {
                    return it->key;
                }
                return std::to_string(*v);
            }
            break;
        }
        case ConfigOptionType::Number:
            if (const double* v = std::get_if<double>(&value)) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%g", *v);
                return buf;
            }
            break;
        case ConfigOptionType::String:
            if (const std::string* v = std::get_if<std::string>(&value)) {
                return "\"" + *v + "\"";
            }
            break;
        case ConfigOptionType::Bool:
            if (const bool* v = std::get_if<bool>(&value)) {
                return *v ? "on" : "off";
            }
            break;
        default:
            break;
        }
        return "?";
    }

    std::string choices_text(const recomp::config::ConfigOption& option) {
        using namespace recomp::config;
        switch (option.type) {
        case ConfigOptionType::Enum: {
            std::string out;
            for (const auto& choice : std::get<ConfigOptionEnum>(option.variant).options) {
                out += (out.empty() ? "" : ", ") + choice.key;
                if (folded(choice.key) != folded(choice.name)) {
                    out += " (" + choice.name + ")";
                }
            }
            return out;
        }
        case ConfigOptionType::Number: {
            const auto& n = std::get<ConfigOptionNumber>(option.variant);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%g to %g%s", n.min, n.max, n.percent ? " (percent)" : "");
            return buf;
        }
        case ConfigOptionType::String:
            return "text";
        case ConfigOptionType::Bool:
            return "on, off";
        default:
            return "";
        }
    }

    recomp::config::Config* find_tab(const std::string& id) {
        try {
            return &recompui::config::get_config(id);
        }
        catch (const std::exception&) {
            return nullptr;
        }
    }

    void print_settings() {
        std::printf("Settings (--<setting>=<value>). The id alone works when no other tab has it.\n");
        for (const char* tab_id : kTabs) {
            recomp::config::Config* tab = find_tab(tab_id);
            if (tab == nullptr) {
                continue;
            }
            std::printf("\n[%s]\n", tab_id);
            for (const auto& option : tab->get_config_schema().options) {
                if (option.hidden || option.type == recomp::config::ConfigOptionType::None) {
                    continue;
                }
                std::printf("  %s.%s  (%s)\n      values: %s\n      now: %s\n", tab_id, option.id.c_str(), option.name.c_str(),
                    choices_text(option).c_str(), value_text(option, tab->get_option_value(option.id)).c_str());
            }
        }
    }

    // The value for an option from text, or monostate with the reason.
    recomp::config::ConfigValueVariant parse_value(const recomp::config::ConfigOption& option, const std::string& text, std::string& error) {
        using namespace recomp::config;
        switch (option.type) {
        case ConfigOptionType::Enum: {
            const auto& e = std::get<ConfigOptionEnum>(option.variant);
            for (const auto& choice : e.options) {
                if (folded(choice.key) == folded(text) || folded(choice.name) == folded(text)) {
                    return choice.value;
                }
            }
            char* end = nullptr;
            errno = 0;
            const unsigned long number = std::strtoul(text.c_str(), &end, 10);
            if (!text.empty() && end != nullptr && *end == '\0' && errno == 0 && e.find_option_from_value((uint32_t)number) != e.options.end()) {
                return (uint32_t)number;
            }
            error = "isn't one of: " + choices_text(option);
            return std::monostate{};
        }
        case ConfigOptionType::Number: {
            const auto& n = std::get<ConfigOptionNumber>(option.variant);
            std::string t = text;
            if (!t.empty() && t.back() == '%') {
                t.pop_back();
            }
            char* end = nullptr;
            const double number = std::strtod(t.c_str(), &end);
            if (t.empty() || end == nullptr || *end != '\0' || !std::isfinite(number)) {
                error = "isn't a number";
                return std::monostate{};
            }
            if (n.min < n.max && (number < n.min || number > n.max)) {
                error = "is out of range (" + choices_text(option) + ")";
                return std::monostate{};
            }
            return number;
        }
        case ConfigOptionType::String:
            return text;
        case ConfigOptionType::Bool: {
            const std::string f = folded(text);
            if (f == "on" || f == "true" || f == "yes" || f == "1") {
                return true;
            }
            if (f == "off" || f == "false" || f == "no" || f == "0") {
                return false;
            }
            error = "isn't on or off";
            return std::monostate{};
        }
        default:
            error = "can't be set";
            return std::monostate{};
        }
    }

    void add_problem(std::string text) {
        if (!text.empty()) {
            text[0] = (char)std::toupper((unsigned char)text[0]);
        }
        problems.push_back(text);
        std::fprintf(stderr, "Command line: %s\n", text.c_str());
    }

    // One --<setting>=<value>: found in its tab (or the only tab that has it)
    // and set, as the menu would. The tabs it changed, to save.
    void apply_setting(const Setting& setting, std::vector<recomp::config::Config*>& changed) {
        std::string tab_id;
        std::string option_id = setting.name;
        const size_t dot = setting.name.find('.');
        if (dot != std::string::npos) {
            tab_id = setting.name.substr(0, dot);
            option_id = setting.name.substr(dot + 1);
        }
        recomp::config::Config* tab = nullptr;
        int matches = 0;
        for (const char* id : kTabs) {
            if (!tab_id.empty() && tab_id != id) {
                continue;
            }
            recomp::config::Config* candidate = find_tab(id);
            if (candidate != nullptr && candidate->has_option(option_id)) {
                tab = candidate;
                matches++;
            }
        }
        if (tab == nullptr) {
            add_problem("there's no setting \"" + setting.name + "\" (" + setting.given + "). --list-settings shows them all.");
            return;
        }
        if (matches > 1) {
            add_problem("\"" + setting.name + "\" is in more than one tab: give it as <tab>." + option_id + ".");
            return;
        }
        const recomp::config::ConfigOption& option = tab->get_option(option_id);
        std::string error;
        const recomp::config::ConfigValueVariant value = parse_value(option, setting.value, error);
        if (std::holds_alternative<std::monostate>(value)) {
            add_problem(setting.given + ": \"" + setting.value + "\" " + error + ".");
            return;
        }
        tab->set_option_value(option_id, value);
        if (tab->requires_confirmation) {
            tab->apply_option_value(option_id); // as its Apply button
        }
        if (std::find(changed.begin(), changed.end(), tab) == changed.end()) {
            changed.push_back(tab);
        }
    }

    bool looks_like_file(const std::string& arg) {
        std::error_code ec;
        return !arg.empty() && arg[0] != '-' && std::filesystem::is_regular_file(path_from_utf8(arg), ec);
    }
}

void btga::cli::parse(int argc, char** argv, const std::string& version_text) {
    version = version_text;
    const std::vector<std::string> args = utf8_arguments(argc, argv);
    for (size_t i = 1; i < args.size(); i++) {
        const std::string& arg = args[i];
        auto next = [&](std::string& out) {
            if (i + 1 < args.size()) {
                out = args[++i];
                return true;
            }
            add_problem(arg + " needs a value after it.");
            return false;
        };
        if (arg == "--help" || arg == "-h" || arg == "-?" || arg == "/?") {
            use_parent_console();
            print_help();
            std::fflush(stdout);
            std::exit(EXIT_SUCCESS);
        }
        else if (arg == "--version") {
            use_parent_console();
            std::printf("%s\n", version.c_str());
            std::fflush(stdout);
            std::exit(EXIT_SUCCESS);
        }
        else if (arg == "--list-settings") {
            list_settings = true;
        }
        else if (arg == "--play" || arg == "--start" || arg == "--skip-launcher") {
            play = true;
        }
        else if (arg == "--fullscreen") {
            settings.push_back({ "graphics.wm_option", "Fullscreen", arg });
        }
        else if (arg == "--windowed") {
            settings.push_back({ "graphics.wm_option", "Windowed", arg });
        }
        else if (arg == "--rom") {
            next(rom_argument);
        }
        else if (arg.rfind("--rom=", 0) == 0) {
            rom_argument = arg.substr(6);
        }
        else if (arg == "--set") {
            std::string pair;
            if (next(pair)) {
                const size_t eq = pair.find('=');
                if (eq == std::string::npos || eq == 0) {
                    add_problem("--set " + pair + ": write it as <setting>=<value>.");
                }
                else {
                    settings.push_back({ pair.substr(0, eq), pair.substr(eq + 1), "--set " + pair });
                }
            }
        }
        // Left to the runtime (it reads them from argv itself) and netplay.
        else if (arg == "--game" || arg == "--game-mode") {
            i++;
        }
        else if (arg == "--netplay") {
        }
        else if (arg.rfind("--", 0) == 0 && arg.find('=') != std::string::npos && arg.find('=') > 2) {
            const size_t eq = arg.find('=');
            settings.push_back({ arg.substr(2, eq - 2), arg.substr(eq + 1), arg });
        }
        else if (rom_argument.empty() && looks_like_file(arg)) {
            rom_argument = arg; // a frontend's "game.exe %ROM%"
        }
        else {
            add_problem("unknown option \"" + arg + "\". --help lists them.");
        }
    }
    // Started again by the menu's Restart (src/main/restart.cpp): the settings
    // given were saved at the first start, and the menu may have changed them
    // since; --rom and --play still count. Its problems were shown then too.
    if (const char* restarted = std::getenv(btga::restart::kRestartedVariable); restarted != nullptr) {
#if defined(_WIN32)
        _putenv_s(btga::restart::kRestartedVariable, "");
#else
        unsetenv(btga::restart::kRestartedVariable);
#endif
        settings.clear();
        problems.clear();
    }
}

void btga::cli::default_to_portable() {
#if defined(__ANDROID__)
    // Android keeps everything in the app's own storage either way
    // (lib-patches/RecompFrontend/0002).
    return;
#endif
    namespace fs = std::filesystem;
    std::error_code ec;
    // Places where the game's folder isn't the player's to write in, or the
    // data folder is chosen some other way.
    if (std::getenv("APPIMAGE") != nullptr || std::getenv("FLATPAK_ID") != nullptr || std::getenv("APP_FOLDER_PATH") != nullptr) {
        return;
    }
    // Already portable, or not started from the game's folder (its assets
    // are loaded from the working directory, as is portable.txt).
    if (fs::exists("portable.txt", ec) || !fs::is_directory("assets", ec)) {
        return;
    }
    // The folder in the user's profile (no portable.txt yet, so that's what
    // this returns): in use since an earlier version, it stays in use.
    const fs::path profile_folder = recompui::file::get_app_folder_path();
    if (profile_folder.empty() || fs::exists(profile_folder, ec)) {
        return;
    }
    std::ofstream file("portable.txt");
    if (!file) {
        return; // a folder that can't be written to (Program Files): the profile, as before
    }
    file << "BattleTanx: Global Assault Recompiled keeps its settings, saves and mods in\n"
            "this folder while this file is here. Delete it to keep them in your user\n"
            "profile instead (%LOCALAPPDATA%\\btgarecomp on Windows, ~/.config/btgarecomp\n"
            "on Linux).\n";
}

void btga::cli::apply_after_config(const std::u8string& game_id) {
    std::vector<recomp::config::Config*> changed;
    for (const Setting& setting : settings) {
        apply_setting(setting, changed);
    }
    for (recomp::config::Config* tab : changed) {
        tab->save_config();
    }
    // After the settings given with it, so the list shows them.
    if (list_settings) {
        use_parent_console();
        print_settings();
        std::fflush(stdout);
        std::exit(EXIT_SUCCESS);
    }
    if (!rom_argument.empty()) {
        const recomp::RomValidationError result = recomp::select_rom(path_from_utf8(rom_argument), game_id);
        switch (result) {
        case recomp::RomValidationError::Good:
            break;
        case recomp::RomValidationError::FailedToOpen:
            add_problem("couldn't open the ROM \"" + rom_argument + "\".");
            break;
        case recomp::RomValidationError::NotARom:
            add_problem("\"" + rom_argument + "\" isn't an N64 ROM.");
            break;
        case recomp::RomValidationError::IncorrectRom:
            add_problem("\"" + rom_argument + "\" is a different game's ROM.");
            break;
        case recomp::RomValidationError::IncorrectVersion:
            add_problem("\"" + rom_argument + "\" is another version of the game; the USA version is needed.");
            break;
        default:
            add_problem("\"" + rom_argument + "\" can't be used.");
            break;
        }
    }
}

void btga::cli::launcher_update(const std::u8string& game_id, const std::string& mod_game_id) {
    // A few frames in, once the launcher is laid out (as netplay's --netplay).
    static int frames = 5;
    if (play_done || --frames > 0) {
        return;
    }
    play_done = true;
    if (play) {
        if (recomp::is_rom_valid(game_id)) {
            // As the launcher's Start Game.
            recompui::update_game_mod_id(mod_game_id);
            recomp::start_game(game_id, {});
            recompui::hide_all_contexts();
            return;
        }
        add_problem("--play: there's no ROM loaded yet. Load one (Load ROM, or --rom <file>) first.");
    }
    if (!problems.empty()) {
        std::string text;
        for (const std::string& p : problems) {
            text += (text.empty() ? "" : "\n") + p;
        }
        recompui::open_info_prompt("Command line", text, "OK", nullptr);
    }
}
