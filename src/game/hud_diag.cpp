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

// TEMPORARY round 100 diagnostic: the fill rectangles of a frame, in draw
// order, with their colours and active scissor -- to see which one RT64
// stretches into the side areas during letterboxed cutscenes in Expand.
// Walks both gfx task display lists (OSTask.data_ptr at +0x30) every 2 s.
// Called from btga_fix_screen_edge_scissors (every VI).
#include <string>
#include <vector>

namespace {
    struct DlFillScan {
        uint8_t* rdram;
        uint32_t seg[16] = {};
        uint32_t fill_color = 0, prim_color = 0, cycle = 0;
        int sc_ulx = 0, sc_uly = 0, sc_lrx = 0, sc_lry = 0;
        int cmds = 0, texrects = 0;
        std::vector<std::string> lines;

        static constexpr uint32_t kRamMask = 0x7FFFFF;
        uint32_t phys(uint32_t a) const { return (seg[(a >> 24) & 0xF] + (a & 0xFFFFFF)) & kRamMask; }
        uint32_t w32(uint32_t p) const { return *(uint32_t*)(rdram + (p & kRamMask)); }

        void walk(uint32_t addr, int depth) {
            if (depth > 18) return;
            uint32_t p = phys(addr);
            while (cmds++ < 200000) {
                uint32_t w0 = w32(p), w1 = w32(p + 4);
                p += 8;
                switch (w0 >> 24) {
                case 0xDE:
                    walk(w1, depth + 1);
                    if (((w0 >> 16) & 0xFF) != 0) return;
                    break;
                case 0xDF: return;
                case 0xDB:
                    if (((w0 >> 16) & 0xFF) == 0x06) seg[((w0 & 0xFFFF) / 4) & 0xF] = w1 & kRamMask;
                    break;
                case 0xED:
                    sc_ulx = (w0 >> 12) & 0xFFF; sc_uly = w0 & 0xFFF; sc_lrx = (w1 >> 12) & 0xFFF; sc_lry = w1 & 0xFFF;
                    break;
                case 0xF7: fill_color = w1; break;
                case 0xFA: prim_color = w1; break;
                case 0xE3: // G_SETOTHERMODE_H: cycle type is bits 20-21 of the mode word
                    if (((w0 >> 8) & 0xFF) == (32 - 20 - 2)) cycle = (w1 >> 20) & 3;
                    break;
                case 0xE4: case 0xE5: texrects++; break;
                case 0xF6: {
                    char buf[160];
                    snprintf(buf, sizeof(buf), "fill %d,%d..%d,%d cyc=%u fill=%08X prim=%08X scissor %d,%d..%d,%d",
                        ((w1 >> 12) & 0xFFF) / 4, (w1 & 0xFFF) / 4, ((w0 >> 12) & 0xFFF) / 4, (w0 & 0xFFF) / 4,
                        cycle, fill_color, prim_color, sc_ulx / 4, sc_uly / 4, sc_lrx / 4, sc_lry / 4);
                    lines.push_back(buf);
                    break;
                }
                }
            }
        }
    };
}

extern "C" void btga_dl_fill_diag(uint8_t* rdram) {
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds(2)) return;
    last = now;
    for (uint32_t task : { 0x801293E0u, 0x80129428u }) {
        uint32_t dl = *(uint32_t*)(rdram + (task - 0x80000000u) + 0x30);
        if (dl < 0x80000000u || dl >= 0x80800000u) continue;
        DlFillScan s{ rdram };
        s.walk(dl, 0);
        printf("[BTGA FILL] task %08X cmds=%d texrects=%d fills=%zu\n", task, s.cmds, s.texrects, s.lines.size());
        for (size_t i = 0; i < s.lines.size() && i < 40; i++) {
            printf("[BTGA FILL]   %s\n", s.lines[i].c_str());
        }
    }
    fflush(stdout);
}
