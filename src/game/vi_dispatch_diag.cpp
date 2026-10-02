// Round 65 diagnostic: round 52's btga_debug_check_vi_dispatch
// (src/main/scheduler_workaround.cpp) only ever fires from inside the
// old func_800A1384/func_80079FF0 spin-loop hooks (rounds 51/53/60), which
// the game no longer revisits after round 64's fix -- so its silence
// doesn't tell us anything about mq 0x80222930's current state. This is
// the same check, but called unconditionally from func_800A1858's
// RECOMP_PATCH (patches/recompui_patches.c), which is confirmed to still
// fire every real VI tick (recomp_run_ui_callbacks needs it to for the UI
// to work at all), so it gives a live, unbiased read.
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cstdlib>

#include "recomp.h"

extern "C" void btga_debug_vi_dispatch_live(uint8_t* rdram, recomp_context* ctx) {
    using namespace std::chrono;
    static steady_clock::time_point last_print{};
    auto now = steady_clock::now();
    if (now - last_print < seconds(1)) {
        return;
    }
    last_print = now;

    uint8_t* mq_ptr = rdram + (0x80222930u - 0x80000000u);
    int32_t validCount = *(int32_t*)(mq_ptr + 8);
    int32_t msgCount = *(int32_t*)(mq_ptr + 16);

    // Round 66 (part 2): round 66's fix to func_80097844's spin made no
    // observable difference, meaning either that spin was never actually
    // hit in this run, or the real gate is stuck shut for an unrelated
    // reason. 0x801147E8 is the flag func_800A140C's gfx-task-build path
    // (via func_80097660) requires to be non-zero -- read it directly to
    // see whether it's ever actually non-zero, rather than guessing
    // further from static analysis.
    int32_t gate_flag = *(int32_t*)(rdram + (0x801147E8u - 0x80000000u));

    printf("[BTGA DEBUG v3 - live, from patch] mq 0x80222930: validCount=%d msgCount=%d gate_0x801147E8=0x%08x\n",
        validCount, msgCount, (unsigned)gate_flag);
    fflush(stdout);
}

// Round 69: 0x801147E8 turned out to be the pending-audio-OSTask* slot, not
// a gfx gate (STATUS.md round 69) -- reading 0 is expected while audio is
// stubbed. This print's real value now is as a heartbeat: it fires from the
// VI-dispatch thread, so it reappearing every second means that thread is
// getting scheduled again.

// Round 87 diagnostic: frame pacing at every real swap, summarised once per
// second. Called from func_800A1858's RECOMP_PATCH right before
// osViSwapBuffer, with (state, frame_buffer) in a0/a1. Answers whether the
// remaining "models stutter back and forth" is uneven swap timing, a
// varying VI count per swap, or frames shown out of order (A -> B -> A).
static void btga_debug_dl_extents(uint8_t* rdram);

extern "C" void btga_debug_swap_pacing(uint8_t* rdram, recomp_context* ctx) {
    btga_debug_dl_extents(rdram);
    using namespace std::chrono;
    uint32_t state = (uint32_t)ctx->r4;
    uint32_t fb = (uint32_t)ctx->r5;
    // halfword at 0x1F0: VIs elapsed since the last swap (MEM_H's ^2 addressing)
    uint16_t vis = *(uint16_t*)(rdram + (((state + 0x1F0) - 0x80000000u) ^ 2));

    static steady_clock::time_point last_swap{}, window_start{};
    static uint32_t last_fb = 0, prev_fb = 0;
    static int swaps = 0, repeats = 0, back_and_forth = 0;
    static double sum_ms = 0, min_ms = 1e9, max_ms = 0;
    static int vis_min = 1 << 30, vis_max = 0;
    // with only 2 buffers in rotation, A->B->A is normal -- report how many were seen
    static uint32_t seen[8]; static int nseen = 0;

    auto now = steady_clock::now();
    if (last_swap.time_since_epoch().count() != 0) {
        double ms = duration<double, std::milli>(now - last_swap).count();
        sum_ms += ms;
        if (ms < min_ms) min_ms = ms;
        if (ms > max_ms) max_ms = ms;
    } else {
        window_start = now;
    }
    if (fb == last_fb) repeats++;
    else if (fb == prev_fb) back_and_forth++;
    if (vis < vis_min) vis_min = vis;
    if (vis > vis_max) vis_max = vis;
    bool known = false;
    for (int i = 0; i < nseen; i++) known |= (seen[i] == fb);
    if (!known && nseen < 8) seen[nseen++] = fb;
    swaps++;
    prev_fb = last_fb;
    last_fb = fb;
    last_swap = now;

    if (now - window_start >= seconds(1)) {
        printf("[BTGA PACING] swaps=%d interval_ms min/avg/max=%.1f/%.1f/%.1f vis_per_swap min/max=%d/%d repeats=%d back_and_forth=%d distinct_fbs=%d\n",
            swaps, min_ms, swaps > 1 ? sum_ms / (swaps - 1) : 0.0, max_ms, vis_min, vis_max, repeats, back_and_forth, nseen);
        fflush(stdout);
        window_start = now;
        swaps = repeats = back_and_forth = 0;
        sum_ms = 0; min_ms = 1e9; max_ms = 0;
        vis_min = 1 << 30; vis_max = 0;
        nseen = 0;
    }
}

// Round 93 diagnostic: what horizontal extent does a real frame's display list
// draw to? Walks the F3DEX2 display list of both gfx task structs
// (0x801293E0 / 0x80129428, OSTask.data_ptr at +0x30) every 2 seconds and
// prints the distinct scissors and viewports plus the widest fill/texture
// rectangles, to find what leaves the right-edge columns undrawn.
#include <set>
#include <string>

namespace {
    struct DlScan {
        uint8_t* rdram;
        uint32_t seg[16] = {};
        std::set<std::string> scissors, viewports, cimgs;
        int fill_max_lrx = -1, tex_max_lrx = -1, cmds = 0;

        static constexpr uint32_t kRamMask = 0x7FFFFF;
        uint32_t phys(uint32_t a) const { return (seg[(a >> 24) & 0xF] + (a & 0xFFFFFF)) & kRamMask; }
        uint32_t w32(uint32_t p) const { return *(uint32_t*)(rdram + (p & kRamMask)); }
        int16_t h16(uint32_t p) const { return *(int16_t*)(rdram + ((p & kRamMask) ^ 2)); }

        void walk(uint32_t addr, int depth) {
            if (depth > 18) return;
            uint32_t p = phys(addr);
            while (cmds++ < 200000) {
                uint32_t w0 = w32(p), w1 = w32(p + 4);
                p += 8;
                char buf[96];
                switch (w0 >> 24) {
                case 0xDE: // G_DL
                    walk(w1, depth + 1);
                    if (((w0 >> 16) & 0xFF) != 0) return; // branch, no return
                    break;
                case 0xDF: return; // G_ENDDL
                case 0xDB: // G_MOVEWORD
                    if (((w0 >> 16) & 0xFF) == 0x06) seg[((w0 & 0xFFFF) / 4) & 0xF] = w1 & kRamMask;
                    break;
                case 0xDC: // G_MOVEMEM
                    if ((w0 & 0xFF) == 0x08) { // viewport
                        uint32_t v = phys(w1);
                        int sx = h16(v), sy = h16(v + 2), tx = h16(v + 8), ty = h16(v + 10);
                        snprintf(buf, sizeof(buf), "x %.2f..%.2f y %.2f..%.2f", (tx - abs(sx)) / 4.0, (tx + abs(sx)) / 4.0, (ty - abs(sy)) / 4.0, (ty + abs(sy)) / 4.0);
                        viewports.insert(buf);
                    }
                    break;
                case 0xED: // G_SETSCISSOR
                    snprintf(buf, sizeof(buf), "%.2f,%.2f..%.2f,%.2f", ((w0 >> 12) & 0xFFF) / 4.0, (w0 & 0xFFF) / 4.0, ((w1 >> 12) & 0xFFF) / 4.0, (w1 & 0xFFF) / 4.0);
                    scissors.insert(buf);
                    break;
                case 0xF6: // G_FILLRECT
                    fill_max_lrx = std::max(fill_max_lrx, int((w0 >> 12) & 0xFFF));
                    break;
                case 0xE4: case 0xE5: // G_TEXRECT / FLIP
                    tex_max_lrx = std::max(tex_max_lrx, int((w0 >> 12) & 0xFFF));
                    break;
                case 0xFF: // G_SETCIMG
                    snprintf(buf, sizeof(buf), "width %u @%08x", (w0 & 0xFFF) + 1, w1);
                    cimgs.insert(buf);
                    break;
                }
            }
        }
    };
}

static void btga_debug_dl_extents(uint8_t* rdram) {
    using namespace std::chrono;
    static steady_clock::time_point last{};
    auto now = steady_clock::now();
    if (now - last < seconds(2)) return;
    last = now;
    for (uint32_t task : { 0x801293E0u, 0x80129428u }) {
        uint32_t dl = *(uint32_t*)(rdram + (task - 0x80000000u) + 0x30);
        if (dl < 0x80000000u || dl >= 0x80800000u) continue;
        DlScan s{ rdram };
        s.walk(dl, 0);
        std::string sc, vp, ci;
        for (auto& x : s.scissors) sc += "[" + x + "]";
        for (auto& x : s.viewports) vp += "[" + x + "]";
        for (auto& x : s.cimgs) ci += "[" + x + "]";
        printf("[BTGA DL] task %08x dl %08x cmds=%d cimg=%s scissor=%s viewport=%s fill_max_lrx=%.2f tex_max_lrx=%.2f\n",
            task, dl, s.cmds, ci.c_str(), sc.c_str(), vp.c_str(), s.fill_max_lrx / 4.0, s.tex_max_lrx / 4.0);
    }
    fflush(stdout);
}
