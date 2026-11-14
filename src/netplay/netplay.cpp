// Netplay's entry points (include/btga_netplay.h): what the rest of the game
// asks netplay, kept to this one header so a build can leave netplay out.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX // (CMakeLists.txt sets it for Windows builds)
#define NOMINMAX // windows.h's min and max macros would break std::min and std::max
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <unistd.h>
#endif

#include "ultramodern/ultramodern.hpp"

#include "btga_netplay.h"
#include "input_record.h"
#include "messages.h"
#include "online_menu.h"
#include "session.h"
#include "settings.h"
#include "sync_test.h"

namespace {
    // The ports' reads for the current sweep, from the exchange at port 0's.
    btga::netplay::PortRead sweep_reads[4];
    uint8_t* game_rdram = nullptr; // from the first frame

    // Online nobody has a Controller Pak, so the game asks at boot (and when
    // it would save): "No Controller Pak found" or "Still no Controller Pak
    // found", B to play without saving. The prompt's text is at 0x8011F834
    // (func_800CC5..., 0x800CC6BC); when it changes to one of those two,
    // player 1 presses B a moment later, for two sweeps. Decided from the
    // game's memory at the same sweep on every PC, so every game does it.
    constexpr uint32_t kPakPromptText = 0x8011F834;
    constexpr uint32_t kNoPakFound = 0x8011F3FC, kStillNoPak = 0x8011F434;
    constexpr uint16_t kButtonB = 0x4000;
    constexpr int kPakAnswerDelay = 15; // sweeps, about half a second

    void answer_pak_prompt(btga::netplay::PortRead& player1) {
        static uint32_t last_text = 0;
        static int countdown = 0, held = 0;
        if (game_rdram == nullptr) {
            return;
        }
        const uint32_t text = *reinterpret_cast<const uint32_t*>(game_rdram + (kPakPromptText - 0x80000000u));
        if (text != last_text && (text == kNoPakFound || text == kStillNoPak)) {
            countdown = kPakAnswerDelay;
        }
        last_text = text;
        if (countdown > 0 && --countdown == 0) {
            held = 2;
        }
        if (held > 0) {
            held--;
            player1.buttons = kButtonB;
            player1.x = player1.y = 0.0f;
        }
    }
}

namespace {
    // Leave Game: the game again, on the Netplay screen, once this one has
    // closed its session. The BTGA_NETPLAY test variable stays behind.
    void start_again_on_netplay_screen() {
#ifdef _WIN32
        SetEnvironmentVariableW(L"BTGA_NETPLAY", nullptr);
        wchar_t path[MAX_PATH * 4];
        DWORD length = GetModuleFileNameW(nullptr, path, (DWORD)std::size(path));
        if (length == 0 || length >= std::size(path)) {
            return;
        }
        std::wstring command = L"\"" + std::wstring(path, length) + L"\" --netplay";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
            AllowSetForegroundWindow(process.dwProcessId);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
#else
        unsetenv("BTGA_NETPLAY");
        std::string self;
        if (const char* appimage = std::getenv("APPIMAGE"); appimage != nullptr && appimage[0] != '\0') {
            self = appimage;
        }
        else {
            std::error_code ec;
            self = std::filesystem::read_symlink("/proc/self/exe", ec).string();
            if (ec) {
                return;
            }
        }
        if (self.empty()) {
            return;
        }
        char flag[] = "--netplay";
        char* args[] = { self.data(), flag, nullptr };
        pid_t child = 0;
        posix_spawn(&child, self.c_str(), nullptr, nullptr, args, environ);
#endif
    }
}

void btga::netplay::startup(const char* version) {
    chat_install();
    if (sync_test_requested()) {
        // Before the game starts, as the runtime's deterministic mode needs.
        ultramodern::set_deterministic(true);
        std::fprintf(stderr, "Sync test on: deterministic mode.\n");
    }
    start(version);
}

void btga::netplay::quitting() {
    shutdown();
    if (leaving_to_netplay_screen()) {
        start_again_on_netplay_screen();
    }
}

void btga::netplay::frame(uint8_t* rdram) {
    game_rdram = rdram;
    sync_test_frame(rdram);
}

bool btga::netplay::deterministic() {
    return ultramodern::is_deterministic();
}

// The game reads every port in turn (osContGetReadData), port 0 first: there
// this PC's player's read (its own controls, as player 1's) goes out and every
// port's for this sweep comes back.
bool btga::netplay::controller_read(int port, bool answered, uint16_t* buttons, float* x, float* y) {
    if (!active()) {
        return input_record_read(port, answered, buttons, x, y);
    }
    if (port < 0 || port >= 4) {
        return false;
    }
    if (port == 0) {
        PortRead mine{};
        mine.response = answered ? 1 : 0;
        // Nothing pressed while this player types in the chat.
        // Nor the buttons on a key the chat took that's still held (Enter that
        // sent a line is also Start: the pause menu).
        if (!chat_typing()) {
            mine.buttons = *buttons & ~chat_held_buttons();
            mine.x = *x;
            mine.y = *y;
        }
        exchange(mine, sweep_reads);
        answer_pak_prompt(sweep_reads[0]);
    }
    const PortRead& read = sweep_reads[port];
    *buttons = (uint16_t)read.buttons;
    *x = read.x;
    *y = read.y;
    return read.response != 0;
}

bool btga::netplay::session_active() {
    return settings_in_use();
}

int btga::netplay::session_players() {
    return settings_in_use() ? players_playing() : 0;
}

uint32_t btga::netplay::cheat_value(size_t index, uint32_t own) {
    if (!settings_in_use()) {
        return own;
    }
    const SharedSettings& s = session_settings_in_use();
    return index < s.cheat_count ? s.cheats[index] : 0;
}

int btga::netplay::draw_distance(int own) {
    return settings_in_use() ? (int)session_settings_in_use().draw_distance : own;
}

float btga::netplay::widescreen_scale(float own) {
    return settings_in_use() ? session_settings_in_use().widescreen_scale : own;
}
