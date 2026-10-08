#include "btga_android.h"

#include <cstdio>
#include <cstring>
#include <thread>

#include <android/log.h>
#include <unistd.h>

#include "SDL2/SDL_system.h"

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

void btga::android::startup() {
    forward_output_to_logcat();

    const char* internal_path = SDL_AndroidGetInternalStoragePath();
    if (internal_path == nullptr || chdir(internal_path) != 0) {
        fprintf(stderr, "Couldn't change to the app storage folder: %s\n", SDL_GetError());
    }
}
