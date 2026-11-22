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
#else
#include <filesystem>
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {
    std::atomic_bool restart_requested = false;
    std::atomic_bool relaunched = false;
#if !defined(_WIN32)
    std::string program;                // what to start: the AppImage, or this executable
    std::vector<std::string> arguments; // the command line, argv[0] first
#endif
}

void btga::restart::install(int argc, char** argv) {
#if !defined(_WIN32)
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
    });
}

bool btga::restart::requested() {
    return restart_requested.load();
}

void btga::restart::relaunch_if_requested() {
    if (!restart_requested.load() || relaunched.exchange(true)) {
        return;
    }
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
#else
    std::vector<char*> argv;
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    pid_t child = 0;
    posix_spawn(&child, program.c_str(), nullptr, nullptr, argv.data(), environ);
#endif
}
