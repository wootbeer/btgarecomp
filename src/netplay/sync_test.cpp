// Sync test (netplay's groundwork, from the Rage Wars port's): with
// BTGA_SYNCTEST set to a file path, the runtime runs in its deterministic
// mode and each game frame's memory is hashed in 64 KB chunks and written
// there. Two runs given the same input should write the same file;
// tools/sync_compare.py finds the first frame and the chunks where they
// differ. BTGA_SYNCDUMP=N,M,... also saves the whole of the hashed memory at
// those frames (the sync file's path + ".<frame>"), to see which words.
//
// File: per frame, the frame number (u32), 0 (u32), the retraces so far (u64,
// the runtime's deterministic count), then kChunks u64 hashes.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "recomp.h"
#include "ultramodern/ultramodern.hpp"

#include "sync_test.h"

namespace {
    // The game's 8 MB and what we added above it: the sound heap at
    // 0x80800000 (sound_channels.cpp). Not the draw item pool and display
    // lists from 0x80C00000 (draw_limits.cpp): what's drawn, not game state.
    constexpr uint32_t kHashedSize = 0xC00000;
    constexpr uint32_t kChunkSize = 0x10000;
    constexpr uint32_t kChunks = kHashedSize / kChunkSize;

    FILE* sync_file = nullptr;
    bool checked = false;
    uint32_t frame = 0;
    uint8_t* sync_rdram = nullptr; // set at the first frame

    // Left out of the hash: what isn't the game's own state and differs from
    // run to run (found with the test itself). The runtime keeps a host
    // pointer (8 bytes) in each OSThread past the game's own fields: the
    // game's five threads'.
    struct Range {
        uint32_t start, end;
    };
    // Also the three framebuffers (320 x 240 x 2 bytes each from 0x80000400,
    // up to the game's code), which the renderer writes back on its own time,
    // dithered differently from run to run.
    // And the two frames' start display lists (0x100 each), where the Fog
    // option (each player's own) writes the fog word.
    constexpr Range kSkipped[] = {
        { 0x80000400, 0x80070C00 },
        { 0x80157E80, 0x80158080 },
        { 0x80217070, 0x80217078 },
        { 0x8021BA00, 0x8021BA08 },
        { 0x8021BBB0, 0x8021BBB8 },
        { 0x80222988, 0x80222990 },
        { 0x803ADA90, 0x803ADA98 },
    };

    bool is_skipped(uint32_t addr) {
        for (const Range& range : kSkipped) {
            if (addr >= range.start && addr < range.end) {
                return true;
            }
        }
        return false;
    }

    uint64_t hash_chunk(const uint8_t* p, uint32_t base) {
        const uint64_t* w = reinterpret_cast<const uint64_t*>(p);
        uint64_t h = 0x9E3779B97F4A7C15ull;
        if (is_skipped(base) && is_skipped(base + kChunkSize - 1)) {
            return h;
        }
        for (uint32_t i = 0; i < kChunkSize / 8; i++) {
            const uint64_t word = is_skipped(base + i * 8) ? 0 : w[i];
            h = (h ^ word) * 0x100000001B3ull;
            h ^= h >> 29;
        }
        return h;
    }
}

bool btga::netplay::sync_test_requested() {
    if (!checked) {
        checked = true;
        const char* path = std::getenv("BTGA_SYNCTEST");
        if (path != nullptr && path[0] != '\0') {
            sync_file = std::fopen(path, "wb");
            if (sync_file == nullptr) {
                std::fprintf(stderr, "Sync test: can't write %s\n", path);
            }
        }
    }
    return sync_file != nullptr;
}

uint64_t btga::netplay::memory_hash() {
    if (sync_rdram == nullptr) {
        return 0; // before the first frame
    }
    ultramodern::wait_for_deterministic_tasks();
    uint64_t h = 0;
    for (uint32_t i = 0; i < kChunks; i++) {
        h = (h ^ hash_chunk(sync_rdram + i * kChunkSize, 0x80000000u + i * kChunkSize)) * 0x100000001B3ull;
    }
    return h;
}

void btga::netplay::sync_test_frame(uint8_t* rdram) {
    sync_rdram = rdram;
    if (!sync_test_requested()) {
        return;
    }
    // The RSP tasks and display lists still running write memory from other
    // threads; hashed while they run, their buffers would differ by timing
    // alone. (The game sees them finish only at its next idle point anyway.)
    ultramodern::wait_for_deterministic_tasks();
    const uint32_t header[2] = { frame++, 0 };
    const uint64_t retraces = ultramodern::deterministic_vi_count();
    std::fwrite(header, sizeof(header), 1, sync_file);
    std::fwrite(&retraces, sizeof(retraces), 1, sync_file);
    static uint64_t hashes[kChunks];
    for (uint32_t i = 0; i < kChunks; i++) {
        hashes[i] = hash_chunk(rdram + i * kChunkSize, 0x80000000u + i * kChunkSize);
    }
    std::fwrite(hashes, sizeof(hashes), 1, sync_file);
    if ((frame & 63) == 0) {
        std::fflush(sync_file);
    }
    static const char* dumps = std::getenv("BTGA_SYNCDUMP");
    if (dumps != nullptr) {
        for (const char* p = dumps; *p != '\0';) {
            char* end = nullptr;
            const unsigned long n = std::strtoul(p, &end, 10);
            if (end == p) {
                break;
            }
            if (n + 1 == frame) {
                char name[1024];
                std::snprintf(name, sizeof(name), "%s.%lu", std::getenv("BTGA_SYNCTEST"), n);
                if (FILE* f = std::fopen(name, "wb")) {
                    std::fwrite(rdram, 1, kHashedSize, f);
                    std::fclose(f);
                }
            }
            p = (*end == ',') ? end + 1 : end;
        }
    }
}
