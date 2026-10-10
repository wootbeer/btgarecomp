// The Android launcher: recompui's default launcher menu (ui_launcher.cpp's
// default_launcher_init_callback and add_default_options), plus Change ROM under Start Game once a
// ROM is stored -- on desktop picking another ROM file is just a file away, on Android the stored ROM
// is only reachable through the app. Change ROM opens the system document picker (nfd_android.cpp)
// and validates and stores the choice exactly as Load ROM does (GameOptionsMenu::select_rom).
#include "btga_android.h"

#include <filesystem>
#include <system_error>
#include <vector>

#include <nfd.h>

#include "librecomp/game.hpp"
#include "recompui/recompui.h"

// main.cpp; recompui's default launcher reads it by this name as well.
extern std::vector<recomp::GameEntry> supported_games;

namespace {
    void change_rom(const std::u8string& game_id) {
        nfdnchar_t* native_path = nullptr;
        if (NFD_OpenDialogN(&native_path, nullptr, 0, nullptr) != NFD_OKAY) {
            return;
        }
        const std::filesystem::path picked{ native_path };
        NFD_FreePathN(native_path);

        const recomp::RomValidationError result = recomp::select_rom(picked, game_id);
        std::error_code ec;
        std::filesystem::remove(picked, ec);

        switch (result) {
            case recomp::RomValidationError::Good:
                recompui::message_box("ROM changed.");
                break;
            case recomp::RomValidationError::FailedToOpen:
                recompui::message_box("Failed to open ROM file. The stored ROM is unchanged.");
                break;
            case recomp::RomValidationError::NotARom:
                recompui::message_box("This is not a valid ROM file. The stored ROM is unchanged.");
                break;
            case recomp::RomValidationError::IncorrectRom:
                recompui::message_box("This ROM is not the correct game. The stored ROM is unchanged.");
                break;
            case recomp::RomValidationError::NotYet:
                recompui::message_box("This game isn't supported yet. The stored ROM is unchanged.");
                break;
            case recomp::RomValidationError::IncorrectVersion:
                recompui::message_box(
                    "This ROM is the correct game, but the wrong version.\n"
                    "This project requires the NTSC-U N64 version of the game. The stored ROM is unchanged.");
                break;
            case recomp::RomValidationError::OtherError:
                recompui::message_box("An unknown error has occurred. The stored ROM is unchanged.");
                break;
        }
    }
}

void btga::android::register_launcher() {
    recompui::register_launcher_init_callback([](recompui::LauncherMenu* menu) {
        const recomp::GameEntry& game = supported_games[0];
        recompui::GameOptionsMenu* options = menu->init_game_options_menu(game.game_id, game.mod_game_id,
            game.display_name, game.thumbnail_bytes, recompui::GameOptionsMenuLayout::Center);
        recompui::update_game_mod_id(game.mod_game_id);

        options->add_start_game_or_load_rom_option();
        if (recomp::is_rom_valid(game.game_id)) {
            options->add_option("Change ROM", [game_id = game.game_id]() { change_rom(game_id); });
        }
        options->add_setup_controls_option();
        options->add_settings_option();
        options->add_mods_option();
        options->add_exit_option();
    });
}
