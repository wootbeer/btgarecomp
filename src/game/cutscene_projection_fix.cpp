// Round 108: the cutscene camera's far plane.
//
// func_800D25E0 (cutscene / intro / credits camera) builds its projection
// with guPerspectiveF(fovy 37, 4:3, near 16, far = *(s32*)0x8023A060), the
// draw distance. That variable is only initialised by func_8009AE38 on the
// way into gameplay (3400 / 2866 / 1800 for 1 / 2 / 3-4 players), so in the
// intro and credits it is still 0. near 16 / far 0 gives a degenerate
// projection: (n+f)/(n-f) = 1 and 2nf/(n-f) = 0, so every vertex ends up
// with clip z == -w, exactly on the near plane. The RSP's fixed-point clip
// test is exact, so on hardware nothing is clipped; RT64 clips in floating
// point, where rounding puts distant triangles randomly on either side of
// the plane each frame -- the rapid flicker of distant buildings.
//
// Substitute a valid far plane when the variable isn't beyond the near
// plane. 32767 rather than the function's own 4096 fallback: the game's
// fog band is the last 0.5% of the depth range (G_MOVEWORD fog 0x64009D00
// in func_8007A250), which at 4096 would fog out everything past ~2500
// units, while with far 0 nothing was fogged; at 32767 it starts past
// ~5000.
#include <cstdint>

#include "recomp.h"

namespace {
    constexpr int32_t kNearPlane = 16;
    constexpr int32_t kCutsceneFarPlane = 32767;
}

// Hooked in func_800D25E0 right before `mtc1 $v1, $f0` (0x800D2AFC), which
// turns the far plane in $v1 into guPerspectiveF's far argument.
extern "C" void btga_cutscene_far_plane(uint8_t* rdram, recomp_context* ctx) {
    if ((int32_t)ctx->r3 <= kNearPlane) {
        ctx->r3 = kCutsceneFarPlane;
    }
}

// Round 110: draw distance.
//
// The intro and credits run on the gameplay engine, so their camera's far
// plane and the map-object distance cull (func_800AF978 -> func_800AB980:
// squared distance to an object's footprint vs. the draw distance squared)
// use the draw distance at 0x8023A060. func_8009AE38 resets it at every
// scene start to 3400 / 2866 / 1800 (1 / 2 / 3-4 players), and the
// frame-rate governor func_80099FE8 then adds 50 per game second when the
// N64 keeps up (>= 20 fps) up to 5000, or subtracts 50 when it doesn't.
// So during a shot it hovers just past 3400, right where the background
// buildings sit: small camera movements flip them across the cull
// distance (and the steep fog band at the far plane gives the half-faded
// frames) -- the rapid flicker of distant buildings.
//
// On PC every frame is on time, where the governor would always end at its
// 5000 maximum, so start every scene there and never let it go lower.
namespace {
    constexpr uint32_t kDrawDistance = 0x8023A060;
    constexpr int32_t kMaxDrawDistance = 5000; // func_80099FE8's cap
}

// func_8009AE38, right before the scene-start `sw $v0, draw distance`
// (0x8009AF4C).
extern "C" void btga_scene_draw_distance(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = kMaxDrawDistance;
}

// func_80099FE8 (governor), right before its `sw $v0, draw distance`
// (0x8009A1F0): never lower it.
extern "C" void btga_governor_draw_distance(uint8_t* rdram, recomp_context* ctx) {
    int32_t current = *(int32_t*)(rdram + (kDrawDistance - 0x80000000u));
    if ((int32_t)ctx->r2 < current) {
        ctx->r2 = (gpr)current;
    }
}
