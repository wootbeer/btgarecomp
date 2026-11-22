#ifndef __BTGA_STEAM_H__
#define __BTGA_STEAM_H__

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Add to Steam (src/main/steam_shortcut.cpp).
namespace btga::steam {
    // Steam's folder and its accounts on this computer (userdata/<id>).
    struct Location {
        std::filesystem::path root;
        std::vector<std::string> accounts;
    };
    std::optional<Location> locate();

    // Started from Steam: it's in the library already.
    bool started_from_steam();

    bool steam_running();
    // Asks Steam to exit (steam://exit) and waits up to `seconds` for it.
    bool close_steam_and_wait(int seconds);
    // Starts Steam at its library (steam://open/games).
    void open_steam();

    struct Result {
        bool ok = false;
        bool added = false; // false: it was there already and has been updated
        int accounts = 0;
        int art = 0;        // artwork pictures saved (of the 3)
        std::string error;
    };
    // Adds this copy of the game to each account's shortcuts.vdf, or updates
    // its entry (the old file is kept as shortcuts.vdf.btga-backup), and
    // saves its artwork, downloaded from SteamGridDB.
    // Steam must be closed.
    Result add_shortcut(const Location& where);
}

#endif
