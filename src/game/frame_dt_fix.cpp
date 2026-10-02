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
#include <cstdio>

#include "recomp.h"

static constexpr float kViUnits = 0.375f;

// Hooked right before `swc1 $f0, 0x5948($at)` in func_800BF80C (0x800BFAC8,
// gameplay) and func_800BFAEC (0x800BFC80, intro / attract demo / credits).
// TEMPORARY round 107 diagnostic: per-frame camera position (as the cull
// records see it and as the camera struct holds it), draw distance and
// delta time, to check whether the position wobbles frame to frame while
// distant objects blink in the intro and credits.
static void camera_diag(uint8_t* rdram, float raw) {
    auto f32 = [&](uint32_t addr) { return *(float*)(rdram + (addr - 0x80000000u)); };
    auto s32 = [&](uint32_t addr) { return *(int32_t*)(rdram + (addr - 0x80000000u)); };
    const uint32_t cam = 0x80235F00;
    printf("[BTGA CAM] cull=%.3f,%.3f cam9c=%.3f,%.3f,%.3f camA8=%.3f,%.3f,%.3f far=%d dt=%.4f\n",
        f32(0x802194B4), f32(0x802194B8),
        f32(cam + 0x9C), f32(cam + 0xA0), f32(cam + 0xA4),
        f32(cam + 0xA8), f32(cam + 0xAC), f32(cam + 0xB0),
        s32(0x8023A060), raw);
    fflush(stdout);
}

extern "C" void btga_frame_dt(uint8_t* rdram, recomp_context* ctx) {
    float raw = ctx->f0.fl;
    camera_diag(rdram, raw);
    float snapped = std::round(raw / kViUnits) * kViUnits;
    if (snapped >= kViUnits) { // keep tiny/zero first-frame values as measured
        ctx->f0.fl = snapped;
    }
}
