// Restart (STATUS.md round 228, ported from snprecomp's round 12): the menu's
// header has a Restart button between Quit and Close, and the quit prompt
// (also Alt+F4 or closing the window) a Restart choice
// (lib-patches/RecompFrontend/0022). Choosing it quits the way Quit does --
// in netplay the other players are told first (btga::netplay::quitting) --
// and then, just before the process ends, starts the game again with the
// same command line. The new start skips the command line's settings: they
// were saved when the game started, and a change made in the menu since
// shouldn't be undone by the restart.

#include "btga_restart.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "recompui/recompui.h"
#include "ultramodern/ultramodern.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__ANDROID__)
#include <filesystem>
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {
    std::atomic_bool restart_requested = false;
    // 0: not started, 1: starting the new process, 2: done. Quitting from a
    // game frees its memory while the game's threads still run; their faults
    // end the process (crash_handler.cpp's shutdown faults) and call this too,
    // so a second caller waits for the first to finish starting the new
    // process instead of ending this one under it (as happened on Windows,
    // where starting a process takes a while: round 228).
    std::atomic_int relaunch_state = 0;
#if !defined(_WIN32) && !defined(__ANDROID__)
    std::string program;                // what to start: the AppImage, or this executable
    std::vector<std::string> arguments; // the command line, argv[0] first
#endif
}

void btga::restart::install(int argc, char** argv) {
#if defined(__ANDROID__)
    // Not on Android, where the system starts the app (as snprecomp): no
    // callback, so no Restart button or choice.
    (void)argc;
    (void)argv;
    return;
#endif
#if !defined(_WIN32) && !defined(__ANDROID__)
    arguments.assign(argv, argv + argc);
    // An AppImage's executable lives in its mount, which goes away with
    // this process: start the AppImage itself.
    if (const char* appimage = std::getenv("APPIMAGE"); appimage != nullptr && appimage[0] != '\0') {
        program = appimage;
    }
    else {
        std::error_code ec;
        program = std::filesystem::read_symlink("/proc/self/exe", ec).string();
        if (ec) {
            program.clear();
        }
    }
    if (program.empty()) {
        return; // no way to start again: no Restart button
    }
    if (!arguments.empty()) {
        arguments[0] = program;
    }
    else {
        arguments.push_back(program);
    }
#else
    (void)argc;
    (void)argv;
#endif
    recompui::set_restart_callback([]() {
        restart_requested = true;
#if defined(_WIN32)
        SetEnvironmentVariableA(kRestartedVariable, "1");
#else
        setenv(kRestartedVariable, "1", 1);
#endif
        ultramodern::quit();
        // If quitting gets stuck (a game thread that never finishes), start
        // again and end this process anyway after a few seconds, as
        // snprecomp's quit watchdog does.
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            btga::restart::relaunch_if_requested();
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(EXIT_SUCCESS);
        }).detach();
    });
}

bool btga::restart::requested() {
    return restart_requested.load();
}

namespace {
    void start_again() {
#if defined(_WIN32)
        wchar_t exe[MAX_PATH * 4];
        const DWORD length = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
        if (length == 0 || length >= sizeof(exe) / sizeof(exe[0])) {
            return;
        }
        std::wstring command_line = GetCommandLineW();
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(exe, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
            // This process has the foreground: let the new window take it.
            AllowSetForegroundWindow(process.dwProcessId);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
#elif !defined(__ANDROID__)
        std::vector<char*> argv;
        for (std::string& argument : arguments) {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);
        pid_t child = 0;
        posix_spawn(&child, program.c_str(), nullptr, nullptr, argv.data(), environ);
#endif
    }
}

void btga::restart::relaunch_if_requested() {
    if (!restart_requested.load()) {
        return;
    }
    int expected = 0;
    if (!relaunch_state.compare_exchange_strong(expected, 1)) {
        // Another thread is starting it: wait (up to 10 s) until it has.
        for (int i = 0; i < 1000 && relaunch_state.load() != 2; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return;
    }
    start_again();
    relaunch_state.store(2);
}
