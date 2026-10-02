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

// --- HUD anchoring ------------------------------------------------------
//
// In Expand, RT64 keeps 2D rectangles inside the centred 4:3 area unless the
// display list tells it to anchor them to a screen edge (extended GBI
// gEXSetRectAlign; the Graphics tab's HUD Ratio then sets how far out an
// anchored edge goes). The gameplay HUD and the cutscene letterbox bars are
// both drawn by func_800BC9F4, a 2D overlay script interpreter, so hooks
// around its element handlers (battletanxga.us.rev0.toml) choose an anchor
// per element:
//   - map widget (func_800C7C10): left
//   - kill counter widget (func_800C9A3C): right
//   - sprites and numbers: by x -- left of x 120 left, from x 200 right, else
//     centred (health bar). Text strings are never anchored, so centred
//     cutscene text stays whole.
//   - fill rects: a bar touching the left or right screen edge gets only that
//     edge anchored, so letterbox bars reach the window edge instead of
//     showing the stretched sky clear beside them.
// The commands are written straight into the frame's display list at the
// head pointer the game passes (always 0x803A5944 here) and reset to
// unanchored after each element.
namespace {
    constexpr uint16_t kOriginLeft = 0x0;    // G_EX_ORIGIN_LEFT
    constexpr uint16_t kOriginRight = 0x400; // G_EX_ORIGIN_RIGHT
    constexpr uint16_t kOriginNone = 0x800;  // G_EX_ORIGIN_NONE

    constexpr uint32_t kDlHeadPtr = 0x803A5944;
    constexpr uint32_t kPlayerCount = 0x802194A5;

    constexpr uint32_t kMapWidget = 0x800C7C10;
    constexpr uint32_t kKillCounterWidget = 0x800C9A3C;

    enum class Mode { Off, ByX, Left, Right };
    Mode mode = Mode::Off;
    uint16_t emitted_left = kOriginNone;
    uint16_t emitted_right = kOriginNone;

    gpr kseg0(uint32_t address) {
        return (gpr)(int32_t)address;
    }

    bool anchoring_active(uint8_t* rdram) {
        // Only where RT64 widens the 3D view: 1 player, and the 2-player
        // full-width halves.
        return btga::get_widescreen_scale() > 1.0f && MEM_BU(0, kseg0(kPlayerCount)) <= 2;
    }

    // gEXEnable + gEXSetRectAlign at the display-list head, advancing it.
    void emit_rect_align(uint8_t* rdram, gpr head_ptr, uint16_t left, uint16_t right) {
        if (left == emitted_left && right == emitted_right) {
            return;
        }
        gpr head = kseg0((uint32_t)MEM_W(0, head_ptr));
        MEM_W(0x00, head) = (int32_t)0xE0525464; // G_SPNOOP + RT64 magic
        MEM_W(0x04, head) = 0x10000064;          // enable, extended opcode 0x64
        MEM_W(0x08, head) = 0x64000006;          // G_EX_SETRECTALIGN_V1
        MEM_W(0x0C, head) = (int32_t)((left & 0xFFF) | ((right & 0xFFF) << 12));
        MEM_W(0x10, head) = 0;                   // no offsets
        MEM_W(0x14, head) = 0;
        MEM_W(0, head_ptr) = (int32_t)(head + 0x18);
        emitted_left = left;
        emitted_right = right;
    }

    uint16_t origin_for_x(int x) {
        if (x < 120) return kOriginLeft;
        if (x >= 200) return kOriginRight;
        return kOriginNone;
    }
}

// Every texture rectangle: func_8007C364(Gfx** head, sprite, x, y, ...).
extern "C" void btga_hud_texrect(uint8_t* rdram, recomp_context* ctx) {
    if (mode == Mode::Off || !anchoring_active(rdram)) {
        return;
    }
    uint16_t origin = kOriginNone;
    switch (mode) {
    case Mode::ByX: origin = origin_for_x((int16_t)ctx->r6); break;
    case Mode::Left: origin = kOriginLeft; break;
    case Mode::Right: origin = kOriginRight; break;
    case Mode::Off: break;
    }
    emit_rect_align(rdram, ctx->r4, origin, origin);
}

extern "C" void btga_hud_begin_by_x(uint8_t* rdram, recomp_context* ctx) {
    mode = Mode::ByX;
}

extern "C" void btga_hud_begin_widget(uint8_t* rdram, recomp_context* ctx) {
    uint32_t target = (uint32_t)ctx->r2;
    mode = target == kMapWidget ? Mode::Left : target == kKillCounterWidget ? Mode::Right : Mode::Off;
}

// Fill-rect element, before it reads the head: x in $s1, y in $s3, and the
// element's width/height halfwords at $s0.
extern "C" void btga_hud_fillrect_begin(uint8_t* rdram, recomp_context* ctx) {
    if (!anchoring_active(rdram)) {
        return;
    }
    int ulx = (int16_t)ctx->r17;
    int lrx = ulx + (uint16_t)MEM_HU(0, ctx->r16);
    uint16_t left = kOriginNone, right = kOriginNone;
    if (ulx <= 0 && lrx < 320) {
        left = kOriginLeft;           // left letterbox bar
    } else if (lrx >= 320 && ulx > 0) {
        right = kOriginRight;         // right letterbox bar
    } else if (lrx <= 120) {
        left = right = kOriginLeft;   // HUD panel on the left
    } else if (ulx >= 200) {
        left = right = kOriginRight;  // HUD panel on the right
    }
    emit_rect_align(rdram, kseg0(kDlHeadPtr), left, right);
}

extern "C" void btga_hud_end(uint8_t* rdram, recomp_context* ctx) {
    mode = Mode::Off;
    if (emitted_left != kOriginNone || emitted_right != kOriginNone) {
        emit_rect_align(rdram, kseg0(kDlHeadPtr), kOriginNone, kOriginNone);
    }
}
