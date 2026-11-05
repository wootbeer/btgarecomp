#ifndef __BTGA_MARKERS_H__
#define __BTGA_MARKERS_H__

#include <cstdint>

#include "recomp.h"

// The radar arrows drawn as interpolated quads (src/game/marker_interp.cpp),
// called from the HUD hooks in src/game/widescreen.cpp.
namespace btga::markers {
    // RDRAM kept out of the game's heap (src/game/heap_relocate.cpp) for the
    // arrows' display lists.
    constexpr uint32_t kScratchSize = 0x20000;
    constexpr uint32_t kScratchStart = 0x80800000 - kScratchSize;

    // At the start of each game frame.
    void new_frame();
    // Around the arrow widget's HUD script element (func_800C8484).
    void widget_begin(uint8_t* rdram);
    void widget_end(uint8_t* rdram);
    // At func_8007C364's entry (every texture rectangle).
    void rect_call(uint8_t* rdram, recomp_context* ctx);
}

#endif
