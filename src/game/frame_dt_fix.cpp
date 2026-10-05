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
extern "C" void btga_frame_dt(uint8_t* rdram, recomp_context* ctx) {
    float raw = ctx->f0.fl;
    float snapped = std::round(raw / kViUnits) * kViUnits;
    if (snapped >= kViUnits) { // keep tiny/zero first-frame values as measured
        ctx->f0.fl = snapped;
    }
}

// Round 125: the frame step at 0x80219488.
//
// func_80099FE8 (the frame-rate governor) also stores the time since the
// previous frame in 1/30 s units: osGetTime() microseconds * 30 / 1e6. Some
// effects use only its whole part -- the shield-hit flash (update at
// 0x800F11D0) loses (int)step of its 10.0 life each frame and is deleted
// once life <= step. On hardware a 30 fps frame is two 59.826 Hz VIs, so
// step = 1.003 and (int)step = 1. Here VIs are exactly 60 Hz, so step comes
// out just under 1.0, (int)step = 0, and the flashes never fade or go away:
// they pile up around a shielded tank. Snap the step to whole VIs at the
// N64's own rate.
static constexpr float kStepPerVi = 30.0f / 59.826f;

// Hooked right before `swc1 $f0, -0x6B78($at)` in func_80099FE8 (0x8009A398).
extern "C" void btga_frame_step(uint8_t* rdram, recomp_context* ctx) {
    float vis = std::round(ctx->f0.fl / (30.0f / 60.0f));
    if (vis >= 1.0f) { // keep tiny/zero first-frame values as measured
        ctx->f0.fl = vis * kStepPerVi;
    }
}

// Round 127: the end-of-level score screen's Kills and Tanks Lost count-ups.
//
// func_800CF41C / func_800CF4E4 (Kills, players 1/2) and func_800CF5C8 /
// func_800CF690 (Tanks Lost) each add (int)dt per frame until they reach the
// real total. dt is ~0.375 per VI, so at 30 fps (2 VIs) (int)0.75 = 0: the
// counts never move and the screen shows 0. On hardware the RDP is slower
// and the screen runs at 3+ VIs per frame, where (int)dt >= 1. Count at
// least 1 per frame while dt is positive.
//
// Hooked at each routine's join label before `addu $v0, $v0, $v1`
// (0x800CF478, 0x800CF534, 0x800CF624, 0x800CF6E0), with the truncated
// step in $v1.
extern "C" void btga_score_count_step(uint8_t* rdram, recomp_context* ctx) {
    float dt = *(float*)(rdram + (0x803A5948u - 0x80000000u));
    if ((int32_t)ctx->r3 == 0 && dt > 0.0f) {
        ctx->r3 = 1;
    }
}
