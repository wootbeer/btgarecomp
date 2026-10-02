// Distant objects blinking out for single frames (intro, credits, gameplay).
//
// Queued mesh draws (func_8007B1F0) are written into the frame's display
// list by func_8007B65C / func_8007B8EC, which give up on the rest of the
// queue -- silently -- once the display-list buffer is past base + 0xADE0.
// The buffers (one per frame, set up in func_80079CB8: descriptor
// G+0x90+i*16, +4) are only 0xAEE0 bytes each, at 0x801420C0 and 0x8014CFA0.
// How full a frame gets depends on what else is on screen (smoke,
// particles, widescreen's wider view), so whatever was queued last -- the
// distant scenery -- dropped out on busy frames.
//
// Fix: the two buffers move to unused extended RAM (the patch segment at
// 0x80801000 holds only a few bytes; mods start at 0x81000000; RT64 masks
// display-list addresses to 16 MB, so 0x00C40000 is reachable), 0x40000
// bytes each, and the consumers' cut-off becomes 0x30000 (instruction
// patches at 0x8007B6E0 and 0x8007B97C in the TOML). Nothing else refers to
// the old buffers.
#include <cstdint>
#include <cstdio>

#include "recomp.h"

namespace {
    constexpr uint32_t kOldBuffer0 = 0x801420C0;
    constexpr uint32_t kOldBufferSize = 0xAEE0;
    constexpr uint32_t kNewBuffer0 = 0x80C40000;
    constexpr uint32_t kNewBufferSize = 0x40000;
    constexpr uint32_t kOldCutoff = 0xADE0;

    // TEMPORARY: per-frame usage, to confirm the cause.
    uint32_t peak_used = 0;
    int frames = 0, over_old_cutoff = 0, aborts = 0;
}

// Before 0x80079E9C `sw $v1, 0x4($v0)`: $v1 = display-list buffer for frame i.
extern "C" void btga_dl_buffer_base(uint8_t* rdram, recomp_context* ctx) {
    uint32_t old_base = (uint32_t)ctx->r3;
    uint32_t i = (old_base - kOldBuffer0) / kOldBufferSize;
    if (old_base < kOldBuffer0 || i > 1 || old_base != kOldBuffer0 + i * kOldBufferSize) {
        return;
    }
    ctx->r3 = (gpr)(int32_t)(kNewBuffer0 + i * kNewBufferSize);
}

// TEMPORARY diagnostic hooks.

// Before 0x8007A194 in func_8007A0A0, the end-of-frame check: $v0 = head,
// $a0 = buffer base.
extern "C" void btga_dl_buffer_frame(uint8_t* rdram, recomp_context* ctx) {
    uint32_t used = (uint32_t)ctx->r2 - (uint32_t)ctx->r4;
    frames++;
    if (used > peak_used) peak_used = used;
    if (used > kOldCutoff) over_old_cutoff++;
    if (frames % 120 == 0) {
        printf("[BTGA DLBUF] %d frames: peak 0x%X bytes (old buffer 0x%X, old cut-off 0x%X), "
               "%d frames past the old cut-off, %d queue cut-offs\n",
            frames, peak_used, kOldBufferSize, kOldCutoff, over_old_cutoff, aborts);
        fflush(stdout);
        peak_used = 0;
        over_old_cutoff = 0;
        aborts = 0;
    }
}

// At the exits of func_8007B65C (0x8007B8B8) and func_8007B8EC (0x8007BCB8):
// $v0 = -1 when the queue was cut off.
extern "C" void btga_dl_buffer_queue_exit(uint8_t* rdram, recomp_context* ctx) {
    if ((int32_t)ctx->r2 == -1) aborts++;
}
