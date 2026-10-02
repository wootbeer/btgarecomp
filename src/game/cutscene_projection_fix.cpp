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
