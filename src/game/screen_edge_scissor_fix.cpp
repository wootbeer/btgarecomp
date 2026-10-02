// Round 94 (STATUS.md): close the 1-pixel gap at the right and bottom
// screen edges.
//
// The game's static viewport display lists (0x80127F68..., one per
// 1-3 player layout) set their scissor to x <= 319 / y <= 239 instead of
// 320 / 240. Scissor lower-right corners are exclusive, so the last column
// and row are never drawn -- on a real TV that pixel sat in the overscan
// border, but here it shows as a strip of clear colour (gameplay) or stale
// pixels (menus). Only edges touching the outer screen border are widened;
// the split-screen inner seams (x 159 / y 119) are left as designed.
//
// These display lists are data loaded into RDRAM at boot, so they're fixed
// up in memory rather than in recompiled code. Each word is only rewritten
// while it still holds the ROM's original value.
#include <cstdint>

#include "recomp.h"

namespace {
    struct ScissorWordFix {
        uint32_t vram;
        uint32_t original;
        uint32_t fixed;
    };

    // w1 of each G_SETSCISSOR: (lrx << 12) | lry, in 10.2 fixed point.
    constexpr ScissorWordFix kFixes[] = {
        { 0x80127F74, 0x004FC3BC, 0x005003C0 }, // full screen: 319,239 -> 320,240
        { 0x80127F8C, 0x004FC1DC, 0x005001DC }, // 2P top:       right edge
        { 0x80127FA4, 0x004FC3BC, 0x005003C0 }, // 2P bottom:    right + bottom
        { 0x80127FD4, 0x004FC1DC, 0x005001DC }, // top-right:    right edge
        { 0x80127FEC, 0x0027C3BC, 0x0027C3C0 }, // bottom-left:  bottom edge
        { 0x80128004, 0x004FC3BC, 0x005003C0 }, // bottom-right: right + bottom
    };
}

extern "C" void btga_fix_screen_edge_scissors(uint8_t* rdram, recomp_context* ctx) {
    for (const auto& fix : kFixes) {
        uint32_t* word = (uint32_t*)(rdram + (fix.vram - 0x80000000u));
        if (*word == fix.original) {
            *word = fix.fixed;
        }
    }
}
