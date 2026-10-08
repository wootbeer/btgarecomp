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
#include <cstdio>
#include <cstring>

#include "recomp.h"

#include "btga_config.h"

// TEMPORARY diagnostic (Android port, nuke flash in the intro covering the whole
// window in Expand): logs full-width fills and the frame clear colour, each source
// only when its line changes, at most 300 lines. Remove once found.
namespace {
    void flash_log(int source, const char* line) {
        static char last[3][160];
        static int count = 0;
        if (count >= 300 || std::strcmp(line, last[source]) == 0) {
            return;
        }
        std::snprintf(last[source], sizeof(last[source]), "%s", line);
        count++;
        std::fprintf(stderr, "[BTGA FLASH] %s\n", line);
    }
}

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
    constexpr uint32_t kPlayerCount = 0x802194A5;

    constexpr uint32_t kKillCounterWidget = 0x800C9A3C;
    constexpr uint8_t kOpWidget = 23;
    constexpr int kHudMarginShift = 16 * 4; // 10.2 fixed point
    // RT64 adds the framebuffer width to right-anchored coordinates
    // (RDP::movedFromOrigin), so they must be given relative to the right
    // edge: offset by -320 (in 10.2 fixed point, -320 * 4).
    constexpr int kRightOriginShift = -320 * 4;

    struct Align {
        uint16_t left = kOriginNone, right = kOriginNone;
        int16_t left_offset = 0, right_offset = 0;
        bool operator==(const Align&) const = default;
    };
    constexpr Align kAlignNone{};
    constexpr Align kAlignHudLeft{ kOriginLeft, kOriginLeft, -kHudMarginShift, -kHudMarginShift };
    constexpr Align kAlignHudRight{ kOriginRight, kOriginRight, kRightOriginShift + kHudMarginShift, kRightOriginShift + kHudMarginShift };

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

    // Writes RT64 extended-GBI commands at the display-list head, advancing
    // it. head_ptr is what the current draw call writes through -- sometimes
    // a stack-local copy of the head (func_800C7C10 copies it to sp+0x18 and
    // writes it back before returning). home_ptr is the global that copy
    // goes back to, the only one safe to use for the later reset: the local
    // is dead by then (round 103 -- writing through it corrupted the
    // caller's stack and crashed at level start).
    //
    // RT64 clips every rect to the current scissor, which (unanchored) is
    // squeezed into the centred 4:3 area, so anchored rects past it were
    // cut off (round 104). While anything is anchored, the scissor is
    // pushed and widened to the whole window, then popped on reset.
    struct DlWriter {
        uint8_t* rdram;
        gpr head;
        void cmd(uint32_t w0, uint32_t w1) {
            MEM_W(0, head) = (int32_t)w0;
            MEM_W(4, head) = (int32_t)w1;
            head += 8;
        }
    };

    bool scissor_pushed = false;

    void emit(uint8_t* rdram, gpr head_ptr, gpr home_ptr, const Align& align) {
        if (align == emitted) {
            return;
        }
        DlWriter dl{ rdram, kseg0((uint32_t)MEM_W(0, head_ptr)) };
        dl.cmd(0xE0525464, 0x10000064); // gEXEnable (G_SPNOOP + RT64 magic, opcode 0x64)
        bool anchored = !(align == kAlignNone);
        if (anchored && !scissor_pushed) {
            dl.cmd(0x64000017, 0);      // gEXPushScissor
            // gEXSetScissor(G_SC_NON_INTERLACE, LEFT, RIGHT, 0, 0, 0, 240):
            // the whole window.
            dl.cmd(0x64000005, (kOriginLeft << 2) | (kOriginRight << 14));
            dl.cmd(0, (0u << 16) | (240u * 4));
            scissor_pushed = true;
        }
        dl.cmd(0x64000006, (align.left & 0xFFF) | ((align.right & 0xFFF) << 12)); // gEXSetRectAlign
        dl.cmd((uint32_t)(uint16_t)align.left_offset << 16, (uint32_t)(uint16_t)align.right_offset << 16);
        if (!anchored && scissor_pushed) {
            dl.cmd(0x64000018, 0);      // gEXPopScissor
            scissor_pushed = false;
        }
        MEM_W(0, head_ptr) = (int32_t)(uint32_t)dl.head;
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

    // Letterbox bars: anchor only the edge that touches the screen edge.
    Align bar_align(int ulx, int lrx) {
        Align a;
        if (ulx <= 0 && lrx < 320) {
            a.left = kOriginLeft;
        } else if (lrx >= 320 && ulx > 0) {
            a.right = kOriginRight;
            a.right_offset = kRightOriginShift;
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
extern "C" void btga_hud_script_end(uint8_t* rdram, recomp_context*) {
    element_align = kAlignNone;
    hud_script = false;
    reset(rdram);
}

// func_800C7C10(Gfx** head, ...) -- draws through a local copy of *head.
extern "C" void btga_hud_map_begin(uint8_t* rdram, recomp_context* ctx) {
    in_map = anchoring_active(rdram);
    map_home_ptr = ctx->r4;
}

extern "C" void btga_hud_map_end(uint8_t* rdram, recomp_context*) {
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
    if (ulx <= 0 && lrx >= 320) {
        gpr el = ctx->r30;
        char line[160];
        std::snprintf(line, sizeof(line), "interp fill x %d..%d el %02X %02X%02X%02X%02X y %d hud %d",
            ulx, lrx, MEM_BU(0, el), MEM_BU(1, el), MEM_BU(2, el), MEM_BU(3, el), MEM_BU(4, el), (int)MEM_H(4, el), hud_script ? 1 : 0);
        flash_log(0, line);
    }
    if (!anchoring_active(rdram)) {
        return;
    }
    Align a = bar_align(ulx, lrx);
    if (a == kAlignNone) {
        a = element_align; // a HUD panel
    }
    emit(rdram, kseg0(kOverlayDlHeadPtr), a);
}

extern "C" void btga_hud_fillrect_end(uint8_t* rdram, recomp_context*) {
    reset(rdram);
}

// --- Letterboxed cutscenes ---------------------------------------------
//
// Every frame starts (func_8007A250) with a fill-mode clear of 0..319 x
// 0..239 in the scene's sky colour, which RT64 stretches across the whole
// window. Cutscenes then draw black letterbox bars (func_800D56FC's box
// drawer) around a smaller 3D view, but only inside the 4:3 area, so the
// stretched sky showed beside them. Anchoring the bars outward (round 104)
// only works when the HUD Ratio lets anchored elements reach the window
// edge -- at Original they stay in 4:3 by design.
//
// Instead, while the previous frame was letterboxed, the clear is followed
// by a black fill of the whole screen (stretched to the window) and the sky
// colour again over just the 3D view rectangle (not full width, so RT64
// keeps it in place). The view rectangle is taken from the bars the box
// drawer drew: left bar's right edge, right bar's left edge, top bar's
// bottom, bottom area's top.
namespace {
    struct Letterbox {
        int ulx = 0, uly = 0, lrx = 320, lry = 240;
        int bars = 0;
    };
    Letterbox letterbox_building, letterbox_last;
    uint32_t frame_counter = 0, letterbox_frame = 0;
}

// func_800D56FC's box drawer, before it reads its head: box in $s1
// (x, y at +4/+6, width, height at +8/+0xA).
extern "C" void btga_box_fillrect_begin(uint8_t* rdram, recomp_context* ctx) {
    int ulx = (int16_t)MEM_H(4, ctx->r17);
    int uly = (int16_t)MEM_H(6, ctx->r17);
    int lrx = ulx + (uint16_t)MEM_HU(8, ctx->r17);
    int lry = uly + (uint16_t)MEM_HU(0xA, ctx->r17);
    if (ulx <= 0 && lrx >= 320) {
        char line[160];
        std::snprintf(line, sizeof(line), "box fill %d,%d..%d,%d data %08X %08X %08X %08X",
            ulx, uly, lrx, lry, (uint32_t)MEM_W(0, ctx->r17), (uint32_t)MEM_W(4, ctx->r17), (uint32_t)MEM_W(8, ctx->r17), (uint32_t)MEM_W(0xC, ctx->r17));
        flash_log(1, line);
    }
    Letterbox& lb = letterbox_building;
    if (ulx <= 0 && lrx > 0 && lrx < 160 && uly <= 0) {
        lb.ulx = lrx; lb.bars++;           // left bar
    } else if (lrx >= 320 && ulx > 160 && uly <= 0) {
        lb.lrx = ulx; lb.bars++;           // right bar
    } else if (uly <= 0 && lry > 0 && lry < 120 && ulx > 0 && lrx < 320) {
        lb.uly = lry; lb.bars++;           // top bar
    } else if (ulx <= 0 && lrx >= 320 && uly > 120 && lry >= 240) {
        lb.lry = uly; lb.bars++;           // bottom area
    } else {
        return;
    }
    letterbox_frame = frame_counter;
}

extern "C" void btga_box_fillrect_end(uint8_t*, recomp_context*) {
}

// func_8007A250, right after the frame clear's G_FILLRECT (fill colour set
// just before it): the display-list head is the stack variable 0x24($fp).
extern "C" void btga_frame_clear(uint8_t* rdram, recomp_context* ctx) {
    frame_counter++;
    if (letterbox_building.bars >= 2) {
        letterbox_last = letterbox_building;
    }
    letterbox_building = Letterbox{};

    {
        uint32_t clear_head = (uint32_t)MEM_W(0x24, ctx->r30);
        gpr clear_cmd = kseg0(clear_head - 16);
        if ((uint32_t)MEM_W(0, clear_cmd) == 0xF7000000u) {
            char line[160];
            std::snprintf(line, sizeof(line), "clear colour %08X rect %08X %08X letterbox %d",
                (uint32_t)MEM_W(4, clear_cmd), (uint32_t)MEM_W(8, clear_cmd), (uint32_t)MEM_W(12, clear_cmd),
                (frame_counter - letterbox_frame <= 3 && letterbox_last.bars >= 2) ? 1 : 0);
            flash_log(2, line);
        }
    }

    // Letterboxed within the last couple of frames, and in Expand.
    if (btga::get_widescreen_scale() <= 1.0f || frame_counter - letterbox_frame > 3 || letterbox_last.bars < 2) {
        return;
    }
    const Letterbox& lb = letterbox_last;
    if (!(lb.ulx >= 0 && lb.ulx < lb.lrx && lb.lrx <= 320 && lb.uly >= 0 && lb.uly < lb.lry && lb.lry <= 240)) {
        return;
    }

    gpr frame = ctx->r30;
    uint32_t head = (uint32_t)MEM_W(0x24, frame);
    gpr fill_color_cmd = kseg0(head - 16);
    // Expect exactly the clear the game just wrote: G_SETFILLCOLOR, G_FILLRECT 0..319 x 0..239.
    if ((uint32_t)MEM_W(0, fill_color_cmd) != 0xF7000000u || (uint32_t)MEM_W(8, fill_color_cmd) != 0xF64FC3BCu) {
        return;
    }
    uint32_t sky = (uint32_t)MEM_W(4, fill_color_cmd);

    DlWriter dl{ rdram, kseg0(head) };
    dl.cmd(0xF7000000, 0x00010001);                 // black (RGBA5551, both pixels)
    dl.cmd(0xF64FC3BC, 0);                          // whole screen
    dl.cmd(0xF7000000, sky);
    uint32_t lrx = (uint32_t)(lb.lrx * 4 - 4), lry = (uint32_t)(lb.lry * 4 - 4); // fill mode: inclusive
    dl.cmd(0xF6000000 | (lrx << 12) | lry, ((uint32_t)(lb.ulx * 4) << 12) | (uint32_t)(lb.uly * 4));
    MEM_W(0x24, frame) = (int32_t)(uint32_t)dl.head;
}
