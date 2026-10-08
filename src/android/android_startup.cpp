#include "btga_android.h"

#include <cstdio>
#include <cstring>
#include <thread>

#include <android/log.h>
#include <unistd.h>

#include <filesystem>
#include <system_error>

#include "SDL2/SDL_events.h"
#include "SDL2/SDL_gamecontroller.h"
#include "SDL2/SDL_messagebox.h"
#include "SDL2/SDL_system.h"

#include "librecomp/game.hpp"

namespace {
    // Android discards a native process's stdout and stderr; the game reports its problems
    // there, so forward both to logcat line by line.
    void forward_output_to_logcat() {
        int pipe_fds[2];
        if (pipe(pipe_fds) != 0) {
            return;
        }

        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        std::thread([read_fd = pipe_fds[0]]() {
            char buf[1024];
            size_t used = 0;
            ssize_t got;
            while ((got = read(read_fd, buf + used, sizeof(buf) - 1 - used)) > 0) {
                used += got;
                size_t start = 0;
                for (size_t i = 0; i < used; i++) {
                    if (buf[i] == '\n') {
                        buf[i] = '\0';
                        __android_log_write(ANDROID_LOG_INFO, "BTGA", buf + start);
                        start = i + 1;
                    }
                }
                // Keep a partial line for the next read, or flush it if the buffer is full.
                if (start == 0 && used == sizeof(buf) - 1) {
                    buf[used] = '\0';
                    __android_log_write(ANDROID_LOG_INFO, "BTGA", buf);
                    used = 0;
                }
                else {
                    used -= start;
                    memmove(buf, buf + start, used);
                }
            }
        }).detach();
    }
}

namespace {
    // Logs the input devices as SDL sees them (game controller, or joystick with or without a
    // controller mapping), for controller questions.
    int log_input_devices(void*, SDL_Event* event) {
        switch (event->type) {
            case SDL_CONTROLLERDEVICEADDED:
                fprintf(stderr, "Input: game controller added: %s\n", SDL_GameControllerNameForIndex(event->cdevice.which));
                break;
            case SDL_JOYDEVICEADDED:
                fprintf(stderr, "Input: joystick added: %s (game controller mapping: %s)\n",
                    SDL_JoystickNameForIndex(event->jdevice.which),
                    SDL_IsGameController(event->jdevice.which) ? "yes" : "no");
                break;
        }
        return 1;
    }
}

void btga::android::startup() {
    forward_output_to_logcat();
    SDL_AddEventWatch(log_input_devices, nullptr);

    const char* internal_path = SDL_AndroidGetInternalStoragePath();
    if (internal_path == nullptr || chdir(internal_path) != 0) {
        fprintf(stderr, "Couldn't change to the app storage folder: %s\n", SDL_GetError());
    }
}

void btga::android::import_picked_rom(const std::u8string& game_id) {
    // Written by RomPickerActivity into the app's storage, the working directory.
    const std::filesystem::path picked = "rom-import.bin";
    std::error_code ec;
    if (!std::filesystem::exists(picked, ec)) {
        return;
    }

    recomp::RomValidationError result = recomp::select_rom(picked, game_id);
    std::filesystem::remove(picked, ec);

    const char* problem = nullptr;
    switch (result) {
        case recomp::RomValidationError::Good:
            return;
        case recomp::RomValidationError::FailedToOpen:
            problem = "The chosen file couldn't be read.";
            break;
        case recomp::RomValidationError::NotARom:
            problem = "The chosen file isn't an N64 ROM.";
            break;
        case recomp::RomValidationError::IncorrectRom:
            problem = "The chosen ROM isn't BattleTanx: Global Assault.";
            break;
        case recomp::RomValidationError::IncorrectVersion:
        case recomp::RomValidationError::NotYet:
            problem = "This is a different version of BattleTanx: Global Assault. Only the USA version is supported.";
            break;
        case recomp::RomValidationError::OtherError:
            problem = "The ROM couldn't be imported.";
            break;
    }
    fprintf(stderr, "ROM import failed: %s\n", problem);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ROM not imported",
        (std::string(problem) + " Restart the app to choose another file.").c_str(), nullptr);
}
