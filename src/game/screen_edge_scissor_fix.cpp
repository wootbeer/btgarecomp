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
// These display lists are data, so they're fixed up in memory rather than
// in recompiled code. Each word is only rewritten while it still holds the
// ROM's original value.
//
// Round 97: they're drawn from a copy, not from where they sit in the
// loaded image. At init (0x80079F70-0x80079FD0) the game DMAs the 0x400-byte
// block at ROM 0xB7E30 -- the image's 0x80127E30..0x80128230 -- to a buffer
// (0x803B17B0), stores that address at 0x801144F0, and points segment 1 at
// it; the frame calls these lists as 0x01000138 etc. Round 94 patched the
// unused original, so the strip stayed.
#include <cstdint>

#include "recomp.h"

namespace {
    // Start of the segment 1 block in the loaded image, and the variable
    // holding the address of the live copy.
    constexpr uint32_t kSegment1ImageStart = 0x80127E30;
    constexpr uint32_t kSegment1BufferPtr = 0x801144F0;
    constexpr uint32_t kSegment1Size = 0x400;

    struct ScissorWordFix {
        uint32_t vram; // in the loaded image; offset from kSegment1ImageStart applies to the copy
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

extern "C" void btga_crash_note_rdram(uint8_t* rdram); // src/main/crash_handler.cpp

extern "C" void btga_fix_screen_edge_scissors(uint8_t* rdram, recomp_context*) {
    btga_crash_note_rdram(rdram);
    uint32_t buffer = *(uint32_t*)(rdram + (kSegment1BufferPtr - 0x80000000u));
    // Not loaded yet, or not a KSEG0 RDRAM address.
    if (buffer < 0x80000000u || buffer + kSegment1Size > 0x80800000u) {
        return;
    }
    for (const auto& fix : kFixes) {
        uint32_t* word = (uint32_t*)(rdram + (buffer + (fix.vram - kSegment1ImageStart) - 0x80000000u));
        if (*word == fix.original) {
            *word = fix.fixed;
        }
    }
}
