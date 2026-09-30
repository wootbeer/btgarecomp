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

    printf("[BTGA DEBUG v3 - live, from patch] mq 0x80222930: validCount=%d msgCount=%d\n",
        validCount, msgCount);
    fflush(stdout);
}
