// TEMPORARY round 99 diagnostic: which code draws which HUD sprites.
// Every texture rectangle the game draws goes through func_8007C364, called
// directly or through the wrappers func_8007C9B8 / func_8007CE64 /
// func_8007D39C. Each call site is tagged by a hook just before its jal
// (btga_hud_diag_site -- recompiled calls never write $ra), and each
// function's entry hook groups calls by that site, printing each site's
// screen-position range every 3 seconds. Remove once the HUD anchoring is
// in place.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <mutex>

#include "recomp.h"

namespace {
    struct Site {
        const char* callee;
        int count = 0;
        int min_x = 1 << 30, max_x = -(1 << 30), min_y = 1 << 30, max_y = -(1 << 30);
        std::set<uint32_t> sprites;
    };
    uint32_t current_site = 0;
    std::mutex mutex;
    std::map<uint32_t, Site> sites;
    std::chrono::steady_clock::time_point window_start = std::chrono::steady_clock::now();

    void record(recomp_context* ctx, const char* callee) {
        std::lock_guard lock{ mutex };
        Site& s = sites[current_site];
        s.sprites.insert((uint32_t)ctx->r5);
        s.callee = callee;
        int x = (int16_t)ctx->r6;
        int y = (int16_t)ctx->r7;
        s.count++;
        if (x < s.min_x) s.min_x = x;
        if (x > s.max_x) s.max_x = x;
        if (y < s.min_y) s.min_y = y;
        if (y > s.max_y) s.max_y = y;

        auto now = std::chrono::steady_clock::now();
        if (now - window_start >= std::chrono::seconds(3)) {
            for (auto& [ra, site] : sites) {
                printf("[BTGA HUD] site=%08X via %s calls=%d sprites=%zu x=%d..%d y=%d..%d\n",
                    ra, site.callee, site.count, site.sprites.size(), site.min_x, site.max_x, site.min_y, site.max_y);
            }
            printf("[BTGA HUD] ----\n");
            fflush(stdout);
            sites.clear();
            window_start = now;
        }
    }
}

extern "C" void btga_hud_diag_site(uint32_t site) {
    std::lock_guard lock{ mutex };
    current_site = site;
}

extern "C" void btga_hud_diag_C364(uint8_t* rdram, recomp_context* ctx) { record(ctx, "C364"); }
extern "C" void btga_hud_diag_C9B8(uint8_t* rdram, recomp_context* ctx) { record(ctx, "C9B8"); }
extern "C" void btga_hud_diag_CE64(uint8_t* rdram, recomp_context* ctx) { record(ctx, "CE64"); }
extern "C" void btga_hud_diag_D39C(uint8_t* rdram, recomp_context* ctx) { record(ctx, "D39C"); }
