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
#include <chrono>
#include <cstdio>
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
// anchored edge goes). Commands are written straight into the display list
// at the head pointer the game is drawing with, and reset after each
// element.
//
// - Gameplay HUD: drawn by func_800BC9F4, a 2D overlay script interpreter
//   (16-byte elements: opcode, colour, x, y, ..., data/function pointer).
//   The same interpreter draws title screens, logos and menus, so only
//   scripts containing the kill-counter widget are treated as HUD scripts.
//   In those, each element is anchored by its own script x (left of 120 ->
//   left, from 200 -> right, else centred: the health bar), and only element
//   types seen in the HUD (sprites, numbers, bars, widgets), never text, so
//   strings are never split. Anchored elements also move 16 pixels further
//   out, as the original layout leaves a TV-safe margin.
// - Map: drawn by func_800C7C10 through its own object table, anchored left
//   for its whole call.
// - Letterbox bars: fill rects that touch the left or right screen edge get
//   only that edge anchored, so cutscene bars reach the window edge instead
//   of showing the stretched sky clear beside them -- both in the
//   interpreter and in func_800D56FC's box drawer, which draws the cutscenes.
namespace {
    constexpr uint16_t kOriginLeft = 0x0;    // G_EX_ORIGIN_LEFT
    constexpr uint16_t kOriginRight = 0x400; // G_EX_ORIGIN_RIGHT
    constexpr uint16_t kOriginNone = 0x800;  // G_EX_ORIGIN_NONE

    constexpr uint32_t kOverlayDlHeadPtr = 0x803A5944; // func_800BC9F4's
    constexpr uint32_t kBoxDlHeadPtr = 0x803A69E4;     // func_800D56FC's
    constexpr uint32_t kPlayerCount = 0x802194A5;

    constexpr uint32_t kKillCounterWidget = 0x800C9A3C;
    constexpr uint8_t kOpWidget = 23;
    constexpr int kHudMarginShift = 16 * 4; // 10.2 fixed point

    struct Align {
        uint16_t left = kOriginNone, right = kOriginNone;
        int16_t left_offset = 0, right_offset = 0;
        bool operator==(const Align&) const = default;
    };
    constexpr Align kAlignNone{};
    constexpr Align kAlignHudLeft{ kOriginLeft, kOriginLeft, -kHudMarginShift, -kHudMarginShift };
    constexpr Align kAlignHudRight{ kOriginRight, kOriginRight, kHudMarginShift, kHudMarginShift };

    bool hud_script = false;
    bool in_map = false;
    Align element_align = kAlignNone; // for the current interpreter element's texture rects
    Align emitted = kAlignNone;
    gpr emitted_home_ptr = 0;
    gpr map_home_ptr = 0;

    gpr kseg0(uint32_t address) {
        return (gpr)(int32_t)address;
    }

    bool anchoring_active(uint8_t* rdram) {
        // Only where RT64 widens the 3D view: 1 player, and the 2-player
        // full-width halves.
        return btga::get_widescreen_scale() > 1.0f && MEM_BU(0, kseg0(kPlayerCount)) <= 2;
    }

    // gEXEnable + gEXSetRectAlign at the display-list head, advancing it.
    // head_ptr is what the current draw call writes through -- sometimes a
    // stack-local copy of the head (func_800C7C10 copies it to sp+0x18 and
    // writes it back before returning). home_ptr is the global that copy
    // goes back to, which is the only one safe to use for the later reset:
    // the local is dead by then (round 103 -- writing through it corrupted
    // the caller's stack and crashed at level start).
    void emit(uint8_t* rdram, gpr head_ptr, gpr home_ptr, const Align& align) {
        if (align == emitted) {
            return;
        }
        gpr head = kseg0((uint32_t)MEM_W(0, head_ptr));
        MEM_W(0x00, head) = (int32_t)0xE0525464; // G_SPNOOP + RT64 magic
        MEM_W(0x04, head) = 0x10000064;          // enable, extended opcode 0x64
        MEM_W(0x08, head) = 0x64000006;          // G_EX_SETRECTALIGN_V1
        MEM_W(0x0C, head) = (int32_t)((align.left & 0xFFF) | ((align.right & 0xFFF) << 12));
        MEM_W(0x10, head) = (int32_t)((uint32_t)(uint16_t)align.left_offset << 16);
        MEM_W(0x14, head) = (int32_t)((uint32_t)(uint16_t)align.right_offset << 16);
        MEM_W(0, head_ptr) = (int32_t)(head + 0x18);
        emitted = align;
        emitted_home_ptr = home_ptr;
    }

    void emit(uint8_t* rdram, gpr head_ptr, const Align& align) {
        emit(rdram, head_ptr, head_ptr, align);
    }

    void reset(uint8_t* rdram) {
        if (!(emitted == kAlignNone)) {
            emit(rdram, emitted_home_ptr, kAlignNone);
        }
    }

    Align hud_align_for_x(int x) {
        if (x < 120) return kAlignHudLeft;
        if (x >= 200) return kAlignHudRight;
        return kAlignNone;
    }

    bool is_anchorable_hud_op(uint8_t op) {
        switch (op) {
        case 3: case 10: case 14: // sprites
        case 8:                   // numbers
        case 16:                  // bars
        case kOpWidget:
            return true;
        default:
            return false;
        }
    }

    // TEMPORARY round 103 diagnostic: what the fill-rect hooks see, every 2 s.
    struct BarDiag {
        std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
        int calls = 0;
    };
    BarDiag overlay_diag, box_diag;

    void bar_diag(uint8_t* rdram, BarDiag& d, const char* path, int ulx, int lrx) {
        d.calls++;
        auto now = std::chrono::steady_clock::now();
        if (now - d.last >= std::chrono::seconds(2)) {
            printf("[BTGA BAR] %s calls=%d last x=%d..%d players=%u scale=%.3f\n", path, d.calls, ulx, lrx,
                (unsigned)MEM_BU(0, kseg0(kPlayerCount)), btga::get_widescreen_scale());
            fflush(stdout);
            d.last = now;
            d.calls = 0;
        }
    }

    // Letterbox bars: anchor only the edge that touches the screen edge.
    Align bar_align(int ulx, int lrx) {
        Align a;
        if (ulx <= 0 && lrx < 320) {
            a.left = kOriginLeft;
        } else if (lrx >= 320 && ulx > 0) {
            a.right = kOriginRight;
        }
        return a;
    }
}

// func_800BC9F4 entry: a0 -> { ?, script, ... }. A script is a HUD script
// if it contains the kill-counter widget.
extern "C" void btga_hud_script_begin(uint8_t* rdram, recomp_context* ctx) {
    hud_script = false;
    uint32_t script = (uint32_t)MEM_W(4, ctx->r4);
    if (script < 0x80000000u || script >= 0x80800000u) {
        return;
    }
    for (int i = 0; i < 128; i++) {
        gpr element = kseg0(script + i * 0x10);
        uint8_t op = MEM_BU(0, element);
        if (op == 0) {
            break;
        }
        if (op == kOpWidget && (uint32_t)MEM_W(8, element) == kKillCounterWidget) {
            hud_script = true;
            break;
        }
    }
}

// Interpreter dispatch, current element in $fp.
extern "C" void btga_hud_element(uint8_t* rdram, recomp_context* ctx) {
    reset(rdram);
    element_align = kAlignNone;
    if (!hud_script || !anchoring_active(rdram)) {
        return;
    }
    gpr element = ctx->r30;
    if (is_anchorable_hud_op(MEM_BU(0, element))) {
        element_align = hud_align_for_x(MEM_H(2, element));
    }
}

// End of the script.
extern "C" void btga_hud_script_end(uint8_t* rdram, recomp_context* ctx) {
    element_align = kAlignNone;
    hud_script = false;
    reset(rdram);
}

// func_800C7C10(Gfx** head, ...) -- draws through a local copy of *head.
extern "C" void btga_hud_map_begin(uint8_t* rdram, recomp_context* ctx) {
    in_map = anchoring_active(rdram);
    map_home_ptr = ctx->r4;
}

extern "C" void btga_hud_map_end(uint8_t* rdram, recomp_context* ctx) {
    in_map = false;
    reset(rdram);
}

// Every texture rectangle: func_8007C364(Gfx** head, sprite, x, y, ...).
extern "C" void btga_hud_texrect(uint8_t* rdram, recomp_context* ctx) {
    if (in_map) {
        emit(rdram, ctx->r4, map_home_ptr, kAlignHudLeft);
    } else if (!(element_align == kAlignNone)) {
        // Interpreter elements (and the widgets/number drawers they call)
        // all write back to the overlay head.
        emit(rdram, ctx->r4, kseg0(kOverlayDlHeadPtr), element_align);
    }
}

// Interpreter fill-rect element, before it reads the head: x in $s1, the
// element's width at $s0.
extern "C" void btga_hud_fillrect_begin(uint8_t* rdram, recomp_context* ctx) {
    int ulx = (int16_t)ctx->r17;
    int lrx = ulx + (uint16_t)MEM_HU(0, ctx->r16);
    bar_diag(rdram, overlay_diag, "overlay", ulx, lrx);
    if (!anchoring_active(rdram)) {
        return;
    }
    Align a = bar_align(ulx, lrx);
    if (a == kAlignNone) {
        a = element_align; // a HUD panel
    }
    emit(rdram, kseg0(kOverlayDlHeadPtr), a);
}

extern "C" void btga_hud_fillrect_end(uint8_t* rdram, recomp_context* ctx) {
    reset(rdram);
}

// func_800D56FC's box drawer, before it reads its head: box in $s1
// (x at +4, width at +8).
extern "C" void btga_box_fillrect_begin(uint8_t* rdram, recomp_context* ctx) {
    int ulx = (int16_t)MEM_H(4, ctx->r17);
    int lrx = ulx + (uint16_t)MEM_HU(8, ctx->r17);
    bar_diag(rdram, box_diag, "box", ulx, lrx);
    if (!anchoring_active(rdram)) {
        return;
    }
    emit(rdram, kseg0(kBoxDlHeadPtr), bar_align(ulx, lrx));
}

extern "C" void btga_box_fillrect_end(uint8_t* rdram, recomp_context* ctx) {
    reset(rdram);
}
