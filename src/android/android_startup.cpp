#include "btga_android.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <system_error>

#include "SDL2/SDL_events.h"
#include "SDL2/SDL_gamecontroller.h"
#include "SDL2/SDL_hints.h"
#include "SDL2/SDL_messagebox.h"
#include "SDL2/SDL_system.h"

#include "librecomp/game.hpp"
#include "plume/plume_render_interface_types.h"

// Set by the runtime's ultramodern::quit() (librecomp/src/recomp.cpp) as soon as quitting starts.
extern std::atomic_bool exited;

namespace {
    // log.txt stops growing past this; what's wrong usually shows early, or ends in a crash
    // report (crash_log.txt).
    constexpr long max_log_bytes = 8 * 1024 * 1024;

    // Android discards a native process's stdout and stderr; the game reports its problems
    // there, so forward both to logcat line by line, and to log_path (log.txt in the app's
    // storage), which players can share (BattleTanxActivity, LogFiles.java).
    void forward_output(const std::string& log_path) {
        int pipe_fds[2];
        if (pipe(pipe_fds) != 0) {
            return;
        }
        FILE* log = log_path.empty() ? nullptr : fopen(log_path.c_str(), "w");

        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        std::thread([read_fd = pipe_fds[0], log]() {
            long logged = 0;
            auto write_line = [&](const char* line) {
                __android_log_write(ANDROID_LOG_INFO, "BTGA", line);
                if (log != nullptr && logged < max_log_bytes) {
                    // Flushed every line: what was written survives a crash.
                    fputs(line, log);
                    fputc('\n', log);
                    fflush(log);
                    logged += long(strlen(line)) + 1;
                }
            };
            char buf[1024];
            size_t used = 0;
            ssize_t got;
            while ((got = read(read_fd, buf + used, sizeof(buf) - 1 - used)) > 0) {
                used += got;
                size_t start = 0;
                for (size_t i = 0; i < used; i++) {
                    if (buf[i] == '\n') {
                        buf[i] = '\0';
                        write_line(buf + start);
                        start = i + 1;
                    }
                }
                // Keep a partial line for the next read, or flush it if the buffer is full.
                if (start == 0 && used == sizeof(buf) - 1) {
                    buf[used] = '\0';
                    write_line(buf);
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

namespace {
    // SDL's activity class and its getNativeSurface(), looked up on SDL's main thread: a thread
    // that native code attached to Java (like RT64's present thread) can't find app classes.
    jclass sdl_activity_class = nullptr;
    jmethodID get_native_surface = nullptr;
    jmethodID surface_is_valid = nullptr;

    // plume's window provider: the window behind SDL's current surface, with a reference of its
    // own, or nullptr while the app is in the background and has no surface. Taken through Java
    // instead of SDL's own pointer, which the UI thread releases when the surface goes away.
    // After surfaceDestroyed() the Java Surface still hands out its old, abandoned window; any
    // swap chain work on that fails, and on the device crashed the present thread, so a surface
    // that isn't valid counts as none.
    ANativeWindow* current_native_window() {
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        if (env == nullptr || sdl_activity_class == nullptr) {
            return nullptr;
        }
        jobject surface = env->CallStaticObjectMethod(sdl_activity_class, get_native_surface);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }
        if (surface == nullptr) {
            return nullptr;
        }
        const bool valid = env->CallBooleanMethod(surface, surface_is_valid);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(surface);
            return nullptr;
        }
        if (!valid) {
            env->DeleteLocalRef(surface);
            return nullptr;
        }
        ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
        env->DeleteLocalRef(surface);
        return window;
    }

    void install_window_provider() {
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jclass local_class = env->FindClass("org/libsdl/app/SDLActivity");
        if (local_class == nullptr) {
            env->ExceptionClear();
            fprintf(stderr, "Couldn't find SDLActivity; the picture won't come back after the app is in the background.\n");
            return;
        }
        sdl_activity_class = static_cast<jclass>(env->NewGlobalRef(local_class));
        env->DeleteLocalRef(local_class);
        get_native_surface = env->GetStaticMethodID(sdl_activity_class, "getNativeSurface", "()Landroid/view/Surface;");
        jclass surface_class = env->FindClass("android/view/Surface");
        if (surface_class != nullptr) {
            surface_is_valid = env->GetMethodID(surface_class, "isValid", "()Z");
            env->DeleteLocalRef(surface_class);
        }
        if (get_native_surface == nullptr || surface_is_valid == nullptr) {
            env->ExceptionClear();
            sdl_activity_class = nullptr;
            return;
        }
        plume::setAndroidWindowProvider(current_native_window);
    }
}

void btga::android::startup() {
    const char* internal_path = SDL_AndroidGetInternalStoragePath();
    forward_output(internal_path != nullptr ? std::string(internal_path) + "/log.txt" : std::string());
    SDL_AddEventWatch(log_input_devices, nullptr);
    install_window_provider();

    // Landscape only, either way up. Without the hint SDL lets a resizable window take any
    // orientation, overriding the manifest's sensorLandscape, so the launcher could open in portrait.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

    if (internal_path == nullptr || chdir(internal_path) != 0) {
        fprintf(stderr, "Couldn't change to the app storage folder: %s\n", SDL_GetError());
    }

    // A quit, not a crash: no "closed unexpectedly" at the next start (LogFiles.java). The
    // marker goes as soon as quitting starts, as the game's threads aren't always joined after.
    if (internal_path != nullptr) {
        std::thread([marker = std::string(internal_path) + "/running"]() {
            // Polled: quit() stores the flag without notifying waiters.
            while (!exited.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            std::remove(marker.c_str());
        }).detach();
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
