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
#include <chrono>

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
extern "C" void btga_debug_swap_pacing(uint8_t* rdram, recomp_context* ctx) {
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
