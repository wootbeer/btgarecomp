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
#include <atomic>

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

// Round 66 (part 3): func_800A1290's message-type dispatch table showed
// func_800A140C (what we'd been chasing) only handles msg==0x29A (plain
// VI vsync); func_800A15F0 handles msg==0x29B and looks like the real
// "SP/RDP task complete, submit next pending task" driver -- it manages
// two pending-task slots at the same state struct's 0x200/0x204 that
// func_800A140C's own gated path also writes to, and calls func_800976AC
// (confirmed earlier to clear the 0x801147E8 gate) once both are empty.
// Hooked at this function's own entry (before its prologue runs, so
// ctx->r4 still holds the raw incoming state-struct pointer unmodified)
// to see whether/how often it's reached, and what state it sees each
// time -- first 50 calls logged individually, then a running heartbeat.
static std::atomic<long long> btga_800A15F0_call_count{0};

extern "C" void btga_debug_800A15F0_entry(uint8_t* rdram, recomp_context* ctx) {
    long long call_num = btga_800A15F0_call_count.fetch_add(1);
    uint8_t* base = rdram + ((uint32_t)ctx->r4 - 0x80000000u);
    int32_t slot_200 = *(int32_t*)(base + 0x200);
    int32_t slot_204 = *(int32_t*)(base + 0x204);
    uint16_t flags_1F4 = *(uint16_t*)(base + (0x1F4 ^ 2));

    if (call_num < 50) {
        printf("[BTGA 800A15F0] call #%lld: 0x200=0x%08x 0x204=0x%08x 0x1F4=0x%04x\n",
            call_num, (unsigned)slot_200, (unsigned)slot_204, (unsigned)flags_1F4);
        fflush(stdout);
    }

    using namespace std::chrono;
    static steady_clock::time_point last_heartbeat{};
    auto now = steady_clock::now();
    if (now - last_heartbeat >= seconds(1)) {
        last_heartbeat = now;
        printf("[BTGA 800A15F0 HEARTBEAT] total_calls=%lld 0x200=0x%08x 0x204=0x%08x 0x1F4=0x%04x\n",
            call_num + 1, (unsigned)slot_200, (unsigned)slot_204, (unsigned)flags_1F4);
        fflush(stdout);
    }
}
