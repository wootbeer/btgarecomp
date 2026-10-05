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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

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

// The frame step at 0x80219488 (rounds 125, 131).
//
// func_80099FE8 (the frame-rate governor) stores the time since the previous
// frame, measured with osGetTime(): about 0.75 per 30 fps frame (0.63-0.86
// measured, round 130). A few sites use only its whole part, which is >= 1
// on hardware, where gameplay ran slower (about 20 fps, step ~1.1), but 0 at
// our 30 fps -- so they stall:
//   - the shield-hit flash (update 0x800F11D0) never loses life and piles up
//   - func_80087DF4 turns an angle by at most (int)step * 1024 per frame
//   - func_80088030 pushes a deadline forward by (int)step per frame
// Each is fixed below to use the real, fractional step -- proportional to
// time, as the game intends, and within ~12% of hardware's truncated rate.
//
// (Round 125 instead snapped the step to whole VIs, assuming it was ~1.0 per
// frame. At 0.75 that flipped it between 0.5 and 1.0 every other frame and
// made motion visibly uneven -- reverted in round 131.)
//
// Testing switch: BTGA_PACING_LOG=1 prints, every 150 gameplay frames, how
// many VIs frames took and the host frame interval spread.
namespace {
    bool env_flag(const char* name) {
        const char* v = std::getenv(name);
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }

    float frame_step(uint8_t* rdram) {
        return *(float*)(rdram + (0x80219488u - 0x80000000u));
    }

    struct PacingStats {
        int frames = 0;
        int vis[5] = {}; // 0, 1, 2, 3, 4+ VIs
        float raw_min = 1e9f, raw_max = 0.0f;
        double ms_min = 1e9, ms_max = 0.0, ms_sum = 0.0;
        std::chrono::steady_clock::time_point last{};
        std::chrono::steady_clock::time_point window_start{};
    };

    void log_pacing(float raw) {
        static PacingStats st;
        float vis = std::round(raw / 0.375f);
        auto now = std::chrono::steady_clock::now();
        if (st.last.time_since_epoch().count() != 0) {
            double ms = std::chrono::duration<double, std::milli>(now - st.last).count();
            if (ms < st.ms_min) st.ms_min = ms;
            if (ms > st.ms_max) st.ms_max = ms;
            st.ms_sum += ms;
        }
        else {
            st.window_start = now;
        }
        st.last = now;
        st.vis[vis >= 4.0f ? 4 : (int)vis]++;
        if (raw < st.raw_min) st.raw_min = raw;
        if (raw > st.raw_max) st.raw_max = raw;
        if (++st.frames >= 150) {
            double secs = std::chrono::duration<double>(now - st.window_start).count();
            printf("[BTGA PACING] %d frames in %.2f s (%.1f fps): VIs/frame 1:%d 2:%d 3:%d 4+:%d; "
                   "frame interval %.1f-%.1f ms (avg %.1f); raw step %.3f-%.3f\n",
                st.frames, secs, st.frames / secs, st.vis[1], st.vis[2], st.vis[3], st.vis[4],
                st.ms_min, st.ms_max, st.ms_sum / (st.frames - 1), st.raw_min, st.raw_max);
            fflush(stdout);
            st = PacingStats{};
            st.last = now;
            st.window_start = now;
        }
    }
}

// Hooked right before `swc1 $f0, -0x6B78($at)` in func_80099FE8 (0x8009A398).
// The step is left as measured.
extern "C" void btga_frame_step(uint8_t* rdram, recomp_context* ctx) {
    static const bool pacing_log = env_flag("BTGA_PACING_LOG");
    if (pacing_log) {
        log_pacing(ctx->f0.fl);
    }
}

// Shield-hit flash update (func_800F11D0): before `sub.s $f0, $f0, $f2`
// (0x800F1268), $f2 = (float)(int)step. Subtract the real step instead.
extern "C" void btga_shield_flash_step(uint8_t* rdram, recomp_context* ctx) {
    ctx->f2.fl = frame_step(rdram);
}

// func_80087DF4: at L_80087E90, $a2 = (int)step << 10, the most an angle may
// turn this frame (masked to 0xFC00 in the call's delay slot, patched to
// 0xFFFF in the TOML). Use step * 1024.
extern "C" void btga_turn_step(uint8_t* rdram, recomp_context* ctx) {
    float step = frame_step(rdram);
    int32_t amount = step > 0.0f ? (int32_t)(step * 1024.0f) : 0;
    ctx->r6 = amount > 0xFFFF ? 0xFFFF : amount;
}

// func_80088030: before `addu $v0, $v0, $v1` (0x800885CC), $v1 = (int)step is
// added to an integer deadline at +0xC of the object in $s4. Add the real
// step, carrying the fraction per object between frames.
extern "C" void btga_deadline_step(uint8_t* rdram, recomp_context* ctx) {
    static std::unordered_map<uint32_t, float> remainders;
    float& rem = remainders[(uint32_t)ctx->r20];
    float total = frame_step(rdram) + rem;
    int32_t whole = total > 0.0f ? (int32_t)total : 0;
    rem = total - (float)whole;
    ctx->r3 = whole;
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
