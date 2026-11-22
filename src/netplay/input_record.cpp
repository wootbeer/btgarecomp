// Input record and replay (netplay's groundwork, from the Rage Wars port's):
// with BTGA_INPUTREC set to a file path, each controller read the game gets
// (each port's, in the order the game reads them) is written there; with
// BTGA_INPUTPLAY set to such a file, the game gets those instead of the live
// input. With the sync test on (BTGA_SYNCTEST, the deterministic mode), a
// replay should hash the same as the run it recorded.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "input_record.h"

namespace {
    struct Read {
        int32_t port;
        int32_t answered;
        uint32_t buttons;
        float x, y;
    };

    FILE* record_file = nullptr;
    FILE* play_file = nullptr;
    bool checked = false;
    bool play_ended = false;

    void check() {
        if (checked) {
            return;
        }
        checked = true;
        if (const char* path = std::getenv("BTGA_INPUTPLAY"); path != nullptr && path[0] != '\0') {
            play_file = std::fopen(path, "rb");
            if (play_file == nullptr) {
                std::fprintf(stderr, "Input replay: can't read %s\n", path);
            }
        }
        else if (const char* path = std::getenv("BTGA_INPUTREC"); path != nullptr && path[0] != '\0') {
            record_file = std::fopen(path, "wb");
            if (record_file == nullptr) {
                std::fprintf(stderr, "Input record: can't write %s\n", path);
            }
        }
    }
}

bool btga::netplay::input_record_read(int port, bool answered, uint16_t* buttons, float* x, float* y) {
    check();
    if (play_file != nullptr) {
        Read read{};
        if (std::fread(&read, sizeof(read), 1, play_file) != 1) {
            if (!play_ended) {
                play_ended = true;
                std::fprintf(stderr, "Input replay: end of the recording\n");
            }
            *buttons = 0;
            *x = *y = 0.0f;
            return port == 0;
        }
        if (read.port != port) {
            std::fprintf(stderr, "Input replay: read for port %d, recorded port %d\n", port, read.port);
        }
        *buttons = (uint16_t)read.buttons;
        *x = read.x;
        *y = read.y;
        return read.answered != 0;
    }
    if (record_file != nullptr) {
        const Read read{ port, answered ? 1 : 0, *buttons, *x, *y };
        std::fwrite(&read, sizeof(read), 1, record_file);
        std::fflush(record_file);
    }
    return answered;
}
