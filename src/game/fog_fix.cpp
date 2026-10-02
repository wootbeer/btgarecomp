// Round 117: distant geometry flickering at the edge of the fog.
//
// The game hides its draw distance with fog: every frame func_8007A250
// sets G_MOVEWORD G_MW_FOG 0x64009D00 = gSPFogPosition(995, 1000), so fog
// starts at depth 0.995 and is fully opaque exactly at the far plane, which
// the game ties to its draw distance (the projection's far argument is the
// draw distance at 0x8023A060 / 4096).
//
// RT64 reproduces the F3D microcodes' early far clipping by discarding every
// fragment deeper than 1022/1024 (~0.998; RasterPS.hlsl,
// simulateDepthClipF3D). That lies inside the fog band, so geometry around
// 60% fogged was cut off, and the smallest depth change flipped it across
// the line: whole distant buildings blinking in the intro and credits (the
// game sends identical geometry every frame -- round 114 -- and they look
// hazy when visible, i.e. they are in the fog band). Round 115 only pulled
// the fog in, which looked too close and still left geometry at the line.
//
// Fix, keeping the original look:
//   - the projections' far plane is doubled (the game still culls at its
//     own draw distance; only the depth mapping changes), so geometry the
//     game draws stays well short of RT64's clip;
//   - the fog band becomes gSPFogPosition(992.5, 997.5) (0x64009D80), which
//     with the doubled far plane starts and ends at almost the original
//     distances (e.g. ~1630 to ~3300 units at the usual 3400 draw distance,
//     vs. 1655 to 3400), and is fully opaque before the clip depth.
#include <cstdint>
#include <cstring>

#include "recomp.h"

namespace {
    constexpr uint32_t kOriginalFog = 0x64009D00; // gSPFogPosition(995, 1000)
    constexpr uint32_t kFixedFog = 0x64009D80;    // gSPFogPosition(992.5, 997.5)
    constexpr int kFarArgOffset = 0x14;           // guPerspectiveF's far, on the stack
}

// Hooked in func_8007A250 right before `sw $v1, 4($v0)` (0x8007A688), which
// stores the fog word into the frame's G_MOVEWORD G_MW_FOG command.
extern "C" void btga_fog_position(uint8_t* rdram, recomp_context* ctx) {
    if ((uint32_t)ctx->r3 == kOriginalFog) {
        ctx->r3 = (gpr)(int32_t)kFixedFog;
    }
}

// Hooked right before the guPerspectiveF calls of the gameplay camera
// (func_800A72C0, func_800A7664) and the cutscene camera (func_800D25E0),
// after the far argument has been stored at sp+0x14.
extern "C" void btga_double_far_plane(uint8_t* rdram, recomp_context* ctx) {
    uint32_t bits = (uint32_t)MEM_W(kFarArgOffset, ctx->r29);
    float far_plane;
    memcpy(&far_plane, &bits, sizeof(far_plane));
    if (far_plane > 0.0f) {
        far_plane *= 2.0f;
        memcpy(&bits, &far_plane, sizeof(bits));
        MEM_W(kFarArgOffset, ctx->r29) = (int32_t)bits;
    }
}
