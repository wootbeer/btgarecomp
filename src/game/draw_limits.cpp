// Room for the farther draw distances (round 161).
//
// Drawing out to Map Edge queues about three times as many meshes. Two of the
// game's per-frame limits then drop draws, so distant objects blink out:
//   - func_8007B65C / func_8007B8EC write the queued draws into the frame's
//     display-list buffer and give up on the rest of the queue once it's past
//     base + 0xADE0. The buffers (descriptor G+0x90+i*16, +4, set up in
//     func_80079CB8) are 0xAEE0 bytes at 0x801420C0 and 0x8014CFA0. Mission
//     1's start at Map Edge passed it (round 120 had found the same limit).
//   - func_8007B1F0 queues each mesh draw as a 24-byte item, 0x8F0 (2288) of
//     them a frame at 0x80168090 (count 0x80168080). Mission 1 reached 1745.
//
// The display-list buffers move to unused memory above 8 MB (RT64 reads
// display lists anywhere in the first 16 MB; mods start at 0x81000000), 0x40000
// bytes each. That alone changes nothing: the cut-off still applies. When the
// draw distance is above Original, the cut-off becomes 0x30000 and the item
// pool moves to 0x80C00000 with room for 0x2000 items. The choice is latched
// once a frame at the pool reset (func_8007B03C), so one frame never mixes the
// two pools. At Original the game keeps its own limits.
//
// Round 120 had moved these buffers for distant flicker that turned out to be
// RT64's (round 122), and round 121 reverted it.
#include <cstdint>

#include "recomp.h"

#include "btga_config.h"

namespace {
    constexpr uint32_t kOldBuffer0 = 0x801420C0;
    constexpr uint32_t kOldBufferSize = 0xAEE0;
    constexpr uint32_t kNewBuffer0 = 0x80C40000;
    constexpr uint32_t kNewBufferSize = 0x40000;
    constexpr int32_t kOldCutoff = 0xADE0;
    constexpr int32_t kNewCutoff = 0x30000;

    constexpr uint32_t kNewItemPool = 0x80C00000; // 0x2000 x 24 = 0x30000 bytes
    constexpr int32_t kOldItemLimit = 0x8F0;
    constexpr int32_t kNewItemLimit = 0x2000;

    bool extended_frame = false;
}

// func_80079CB8 before 0x80079E9C `sw $v1, 0x4($v0)`: $v1 is frame i's
// display-list buffer.
extern "C" void btga_dl_buffer_base(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t old_base = (uint32_t)ctx->r3;
    if (old_base < kOldBuffer0) {
        return;
    }
    const uint32_t i = (old_base - kOldBuffer0) / kOldBufferSize;
    if (i > 1 || old_base != kOldBuffer0 + i * kOldBufferSize) {
        return;
    }
    ctx->r3 = (gpr)(int32_t)(kNewBuffer0 + i * kNewBufferSize);
}

// func_8007B03C's entry: the frame's pools are emptied.
extern "C" void btga_draw_limits_frame(uint8_t* rdram, recomp_context* ctx) {
    extended_frame = btga::draw_distance_level() > 0;
}

// func_8007B65C before 0x8007B6E8 and func_8007B8EC before 0x8007B984: $v0 is
// the cut-off, about to be added to the buffer base.
extern "C" void btga_dl_buffer_cutoff(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = extended_frame ? kNewCutoff : kOldCutoff;
}

// func_8007B1F0 before 0x8007B258 `beql $v1, $v0`: $v0 is the item limit,
// compared for equality with the count in $v1.
extern "C" void btga_draw_item_limit(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = extended_frame ? kNewItemLimit : kOldItemLimit;
}

// func_8007B1F0 before 0x8007B3A8: $v0 is the item pool base, added to the
// item's offset in the branch's delay slot.
extern "C" void btga_draw_item_pool(uint8_t* rdram, recomp_context* ctx) {
    if (extended_frame) {
        ctx->r2 = (gpr)(int32_t)kNewItemPool;
    }
}
