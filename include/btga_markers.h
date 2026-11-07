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
    // Around the arrow widget's HUD script element (func_800C8484). The
    // start also re-projects the arrows with the frame's camera.
    void widget_begin(uint8_t* rdram, recomp_context* ctx);
    void widget_end(uint8_t* rdram);
    // At func_8007C364's entry (every texture rectangle).
    void rect_call(uint8_t* rdram, recomp_context* ctx);
    // func_800C877C: before its func_800ACCB4 call (0x800C89F0), and after it
    // stored the arrow's position (0x800C8A40, table offset in $s0).
    void projection_call(uint8_t* rdram, recomp_context* ctx);
    void projection_stored(uint8_t* rdram, recomp_context* ctx);
}

#endif
