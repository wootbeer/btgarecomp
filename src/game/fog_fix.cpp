// Round 115: distant geometry flickering at the edge of the fog.
//
// Every frame func_8007A250 sets the fog range with G_MOVEWORD G_MW_FOG
// 0x64009D00: multiplier 0x6400 / offset -0x6300, i.e.
// gSPFogPosition(995, 1000) -- fog starts at depth 0.995 and is only fully
// opaque at the far plane (1.0).
//
// RT64 reproduces the F3D microcodes' early far clipping by discarding every
// fragment deeper than 1022/1024 (~0.998; RasterPS.hlsl,
// simulateDepthClipF3D). That falls inside the game's fog band, where
// geometry is only ~60% fogged, so distant buildings were visibly cut off
// while half-faded, and the smallest depth change between frames or between
// stacked objects flipped them across the line: the "rapid flicker" of
// distant buildings in the intro and credits (round 114 confirmed the game
// sends identical geometry every frame).
//
// Fix: end the fog band at the clip instead -- gSPFogPosition(994, 998):
// multiplier 128000 / 4 = 0x7D00, offset (500 - 994) * 256 / 4 = -31616
// (0x8480). Geometry is now fully fogged before it is discarded, so it
// fades out instead of popping. Fog starts very slightly closer.
#include <cstdint>

#include "recomp.h"

namespace {
    constexpr uint32_t kOriginalFog = 0x64009D00;  // gSPFogPosition(995, 1000)
    constexpr uint32_t kFogBeforeClip = 0x7D008480; // gSPFogPosition(994, 998)
}

// Hooked in func_8007A250 right before `sw $v1, 4($v0)` (0x8007A688), which
// stores the fog word into the frame's G_MOVEWORD G_MW_FOG command.
extern "C" void btga_fog_position(uint8_t* rdram, recomp_context* ctx) {
    if ((uint32_t)ctx->r3 == kOriginalFog) {
        ctx->r3 = (gpr)(int32_t)kFogBeforeClip;
    }
}
