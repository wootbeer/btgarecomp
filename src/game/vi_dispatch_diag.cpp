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
