// Game speed and frame-locked delta time.
//
// func_800BF80C, the per-frame update, measures elapsed time with
// osGetTime() since the previous frame, converts CPU-count ticks to
// microseconds by dividing by osClockRate (u64 at 0x80126E50), scales by
// 30 / 1e6 and stores the result as a float at 0x803A5948; object/camera
// motion multiplies by it. The frame-rate governor (func_80099FE8, the step
// at 0x80219488) and other game timers divide by osClockRate too.
//
// Round 138: osClockRate starts at 62.5 MHz in the ROM, and the game's own
// osInitialize (0x80105B10) ends with osClockRate = osClockRate * 3 / 4
// (0x80105CE0-0x80105D10), giving 46.875 MHz, the CPU count rate. The
// runtime replaces osInitialize and skipped that step, so every game timer
// ran at 3/4 speed: a VI came out as 0.375 units instead of 0.5. (Rounds
// 88-89 took 0.375 to be what hardware does; it wasn't.) btga_init_clock_rate
// redoes the step right after the game calls osInitialize. Found in the
// BattleTanx 1 port, which shares this library code.
//
// On hardware the CPU reaches the measuring point at the same phase of every
// video frame, so the value is steady. Here osGetTime() is the host clock
// and the main thread wakes at slightly different moments, so it wobbles a
// little; btga_frame_dt snaps it to whole VIs.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "recomp.h"

static constexpr float kViUnits = 0.5f; // one VI in the game's 1/30 s units

// Right after func_8009ED9C (the boot function) calls osInitialize
// (0x8009EDB4), before the game's threads start: osClockRate *= 3 / 4.
extern "C" void btga_init_clock_rate(uint8_t* rdram) {
    constexpr gpr kOsClockRate = (gpr)(int32_t)0x80126E50; // u64
    uint64_t rate = (uint64_t((uint32_t)MEM_W(0, kOsClockRate)) << 32) | (uint32_t)MEM_W(4, kOsClockRate);
    rate = rate * 3 / 4;
    MEM_W(0, kOsClockRate) = (int32_t)(rate >> 32);
    MEM_W(4, kOsClockRate) = (int32_t)rate;
}

// Hooked right before `swc1 $f0, 0x5948($at)` in func_800BF80C (0x800BFAC8,
// gameplay) and func_800BFAEC (0x800BFC80, intro / attract demo / credits).
extern "C" void btga_frame_dt(uint8_t*, recomp_context* ctx) {
    float raw = ctx->f0.fl;
    float snapped = std::round(raw / kViUnits) * kViUnits;
    if (snapped >= kViUnits) { // keep tiny/zero first-frame values as measured
        ctx->f0.fl = snapped;
    }
}

// The frame step at 0x80219488 (rounds 125, 131).
//
// func_80099FE8 (the frame-rate governor) stores the time since the previous
// frame, measured with osGetTime(): about 1.0 per 30 fps frame with the
// clock rate fixed (round 138; 0.75 before, which is why these sites
// stalled). A few sites use only its whole part:
//   - the shield-hit flash (update 0x800F11D0) loses (int)step of life
//   - func_80087DF4 turns an angle by at most (int)step * 1024 per frame
//   - func_80088030 pushes a deadline forward by (int)step per frame
// At a steady 1.0 that is what the game intends, but the host-clock wobble
// (about +/-15%) would make (int)step drop to 0 on some frames. Each site
// uses the real, fractional step instead (round 131).
//
// Round 143: the step is now snapped to whole VIs like the delta time
// above. Most of gameplay moves by it (tanks, shots, effects: about 50
// functions read it), so the wobble made objects move unevenly from frame to
// frame while frames are shown at an even 2 VIs: model jitter, and the
// player tank's logo decal failing RT64's coplanar test. Round 125's snap
// went wrong only because the clock rate was still off (0.75 per frame, a
// VI taken as 0.5, so it flipped between 0.5 and 1.0); with round 138's fix
// a 30 fps frame is exactly 1.0. The fractional-step fixes stay, as a guard.
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
        float vis = std::round(raw / kViUnits);
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
extern "C" void btga_frame_step(uint8_t*, recomp_context* ctx) {
    static const bool pacing_log = env_flag("BTGA_PACING_LOG");
    float raw = ctx->f0.fl;
    if (pacing_log) {
        log_pacing(raw);
    }
    float snapped = std::round(raw / kViUnits) * kViUnits;
    if (snapped >= kViUnits) { // keep tiny/zero first-frame values as measured
        ctx->f0.fl = snapped;
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
// real total. With the clock rate wrong (before round 138) dt was 0.75 per
// 30 fps frame, (int)0.75 = 0, and the counts never moved. Now dt is 1.0 and
// the game's own count works; this stays as a guard (count at least 1 per
// frame while dt is positive).
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
