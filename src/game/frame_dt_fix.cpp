// Round 88 (STATUS.md): frame-locked delta time.
//
// func_800BF80C, the per-frame update, measures elapsed time with
// osGetTime() since the previous frame, scales it to 1/30-second units
// (us * 30 / 1e6) and stores it as a float at 0x803A5948; object/camera
// motion multiplies by it. On hardware the CPU reaches that point at the
// same phase of every video frame, so the value is a steady 1.0 at 30 fps.
// Here osGetTime() is the host clock and the cooperative scheduler wakes
// the main thread at a slightly different moment each frame, so the value
// wobbles even though frames are presented evenly -- objects step unevenly
// and appear to jitter.
//
// Replace it with the number of VIs since the previous frame (0.5 units
// each), which is what the hardware value effectively measures. VIs are
// counted in func_800A1858's RECOMP_PATCH, which runs once per real VI
// (the same message path that drives the game's own swap counter).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

#include "recomp.h"

static std::atomic<uint32_t> btga_vi_count{0};

extern "C" void btga_vi_tick(uint8_t* rdram, recomp_context* ctx) {
    btga_vi_count.fetch_add(1, std::memory_order_relaxed);
}

// Hooked right before `swc1 $f0, 0x5948($at)` (0x800BFAC8) in func_800BF80C.
extern "C" void btga_frame_dt(uint8_t* rdram, recomp_context* ctx) {
    static bool have_last = false;
    static uint32_t last_vi = 0;

    float raw = ctx->f0.fl;
    uint32_t now_vi = btga_vi_count.load(std::memory_order_relaxed);
    uint32_t vis = now_vi - last_vi;
    bool use_vi = have_last && vis >= 1 && vis <= 8; // keep the raw value across long stalls (loads)
    last_vi = now_vi;
    have_last = true;
    if (use_vi) {
        ctx->f0.fl = vis * 0.5f;
    }

    // Once-per-second summary, to confirm the raw value was what wobbled.
    using namespace std::chrono;
    static steady_clock::time_point window_start = steady_clock::now();
    static int frames = 0, replaced = 0;
    static float raw_min = 1e9f, raw_max = 0.0f, raw_sum = 0.0f;
    static uint32_t vis_min = ~0u, vis_max = 0;
    frames++;
    if (use_vi) replaced++;
    raw_sum += raw;
    if (raw < raw_min) raw_min = raw;
    if (raw > raw_max) raw_max = raw;
    if (vis < vis_min) vis_min = vis;
    if (vis > vis_max) vis_max = vis;
    auto now = steady_clock::now();
    if (now - window_start >= seconds(1)) {
        printf("[BTGA DT] frames=%d raw_dt min/avg/max=%.2f/%.2f/%.2f vis_per_frame min/max=%u/%u replaced=%d\n",
            frames, raw_min, raw_sum / frames, raw_max, vis_min, vis_max, replaced);
        fflush(stdout);
        window_start = now;
        frames = replaced = 0;
        raw_min = 1e9f; raw_max = 0.0f; raw_sum = 0.0f;
        vis_min = ~0u; vis_max = 0;
    }
}
