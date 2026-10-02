// Widescreen (RT64's Expand aspect ratio).
//
// RT64 widens a 3D view automatically when its viewport spans the whole
// screen width: it stretches the viewport to the window and narrows the
// projection by the same factor, so more of the world shows unstretched
// (this started working once round 97 fixed the game's 319-pixel scissor).
// The game, though, still culls against its original view cone, so ground
// pieces, objects and effects near the new side edges popped in and out.
//
// func_800AC9E8 builds a per-player cull record each frame (0x802194B4,
// stride 0x28) from the camera's ground-plane direction scaled by a
// per-layout factor k stored at camera+0x168 (2.29 for 1 player, 1.12 for
// the 2-player halves, 2.2 for quadrants; roughly 1 / tan of the half
// horizontal field of view). Every consumer tests |k * lateral| < forward,
// so dividing k by RT64's widening factor widens all of them at once.
#include <atomic>
#include <cstdint>

#include "recomp.h"

#include "btga_config.h"

namespace {
    std::atomic<float> widescreen_scale{ 1.0f };

    // Camera fields (camera structs at 0x80235F00, stride 0x250).
    constexpr int kCameraViewportDl = 0x118;

    // Segment 1 viewport display lists whose viewport spans the full screen
    // width -- the only ones RT64 widens: full screen, and the top and
    // bottom halves of the 2-player (and 3-player top) split.
    bool is_full_width_viewport(uint32_t dl) {
        return dl == 0x01000138 || dl == 0x01000150 || dl == 0x01000168;
    }
}

void btga::set_widescreen_scale(float scale) {
    widescreen_scale.store(scale, std::memory_order_relaxed);
}

float btga::get_widescreen_scale() {
    return widescreen_scale.load(std::memory_order_relaxed);
}

// Hooked in func_800AC9E8 right after each `lwc1 $f0, 0x168($s0)` (the cull
// factor k), with the camera in $s0.
extern "C" void btga_widen_cull(uint8_t* rdram, recomp_context* ctx) {
    float scale = btga::get_widescreen_scale();
    gpr camera = ctx->r16;
    if (scale <= 1.0f || camera == 0) {
        return;
    }
    if (is_full_width_viewport((uint32_t)MEM_W(kCameraViewportDl, camera))) {
        ctx->f0.fl /= scale;
    }
}
