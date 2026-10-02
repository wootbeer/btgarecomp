// Rounds 88-89 (STATUS.md): frame-locked delta time.
//
// func_800BF80C, the per-frame update, measures elapsed time with
// osGetTime() since the previous frame, scales it by 30 / 1e6 and stores it
// as a float at 0x803A5948; object/camera motion multiplies by it. On
// hardware the CPU reaches that point at the same phase of every video
// frame, so the value is steady. Here osGetTime() is the host clock and the
// main thread wakes at slightly different moments, so it wobbles a little.
//
// Round 89 correction: round 88 assumed 1.0 per frame and substituted
// VIs * 0.5 using a VI counter that turned out to tick ~3x per 30 fps frame
// (func_800A1858 also runs off a non-VI message) -- feeding the game 1.5
// where it expects 0.75. The game's own value is 0.75 per 30 fps frame on
// hardware too: it divides CPU-count ticks (46.875 MHz) by the CPU clock
// rate (62.5 MHz). One VI is therefore 0.375 units. The measured value was
// already steady (0.72-0.77); snap it to whole VIs instead of replacing it.
#include <cmath>
#include <cstdint>

#include "recomp.h"

static constexpr float kViUnits = 0.375f;

// Hooked right before `swc1 $f0, 0x5948($at)` in func_800BF80C (0x800BFAC8,
// gameplay) and func_800BFAEC (0x800BFC80, intro / attract demo / credits).
extern "C" void btga_obj_diag_frame(uint8_t* rdram); // TEMPORARY, projection_diag.cpp

extern "C" void btga_frame_dt(uint8_t* rdram, recomp_context* ctx) {
    btga_obj_diag_frame(rdram);
    float raw = ctx->f0.fl;
    float snapped = std::round(raw / kViUnits) * kViUnits;
    if (snapped >= kViUnits) { // keep tiny/zero first-frame values as measured
        ctx->f0.fl = snapped;
    }
}
