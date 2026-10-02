// Distant objects blinking out for single frames (intro, credits, gameplay).
//
// func_8007B1F0 doesn't draw a mesh: it queues it in per-frame pools, which
// func_8007B65C later walks (by pointer, from the hash table at 0x801777E0)
// to build the display list. func_8007B03C empties the pools every frame.
// A draw is silently dropped (returns -1) when a pool is full:
//   - draw items: 0x8F0 (2288) x 24 bytes at 0x80168090, count 0x80168080
//   - mesh entries: 300 x 28 bytes at 0x80175710, count 0x80168084
//   - the matrix buffer: 0xC000 bytes (checked at 0x8007B27C)
// Which draws miss out depends on submission order and on how much else
// (smoke, particles) was queued that frame, so groups of distant objects
// blink in and out together. Widescreen culls a wider area, queuing more.
//
// Fix: the two pools move to unused extended RAM (0x80C00000 up; the patch
// segment at 0x80801000 holds only a few bytes) with larger limits -- the
// limit immediates are instruction patches in the TOML, the bases are set
// by the hooks below. Nothing else addresses these pools directly (the
// 0x80168090 block func_8007B020/func_8007B030 hand out is general scratch
// for other code, which keeps using it).
#include <chrono>
#include <cstdint>
#include <cstdio>

#include "recomp.h"

namespace {
    constexpr uint32_t kItemPool = 0x80C00000;  // 0x2000 items x 24 = 0x30000
    constexpr uint32_t kMeshPool = 0x80C30000;  // 0x800 entries x 28 = 0xE000
    constexpr int32_t kItemLimit = 0x2000;      // TOML patch at 0x8007B254
    constexpr int32_t kMeshLimit = 0x800;       // TOML patch at 0x8007B308

    constexpr uint32_t kItemCount = 0x80168080;
    constexpr uint32_t kMeshCount = 0x80168084;

    // TEMPORARY: per-frame usage and drops, to confirm the cause.
    int drops_item = 0, drops_mesh = 0, drops_matrix = 0;
    int32_t matrix_bytes = 0;
    int peak_items = 0, peak_meshes = 0, peak_matrix = 0, frames = 0, drop_lines = 0;
}

// Before 0x8007B3A8, after `addiu $v0, $v0, -0x7F70`: $v0 = item pool base.
extern "C" void btga_draw_item_pool(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = (gpr)(int32_t)kItemPool;
}

// Before 0x8007B344 `addu $a2, $a0, $a2`: $a2 = mesh entry pool base (after
// it was used for the hash table address at 0x8007B32C).
extern "C" void btga_draw_mesh_pool(uint8_t* rdram, recomp_context* ctx) {
    ctx->r6 = (gpr)(int32_t)kMeshPool;
}

// TEMPORARY diagnostic hooks.

// Before 0x8007B27C: $v0 = matrix buffer base, $a1 = next free matrix.
extern "C" void btga_draw_pool_matrix(uint8_t* rdram, recomp_context* ctx) {
    int32_t used = (int32_t)((uint32_t)ctx->r5 - (uint32_t)ctx->r2);
    if (used > matrix_bytes) matrix_bytes = used;
}

// At 0x8007B464, func_8007B1F0's common exit: $v0 = -1 for a dropped draw.
extern "C" void btga_draw_pool_exit(uint8_t* rdram, recomp_context* ctx) {
    if ((int32_t)ctx->r2 != -1) return;
    if ((int32_t)MEM_W(0, (gpr)(int32_t)kItemCount) >= kItemLimit) drops_item++;
    else if ((int32_t)MEM_W(0, (gpr)(int32_t)kMeshCount) >= kMeshLimit) drops_mesh++;
    else drops_matrix++;
}

// Before 0x8007B054 in func_8007B03C, the per-frame reset: report last frame.
extern "C" void btga_draw_pool_frame(uint8_t* rdram, recomp_context* ctx) {
    int items = (int32_t)MEM_W(0, (gpr)(int32_t)kItemCount);
    int meshes = (int32_t)MEM_W(0, (gpr)(int32_t)kMeshCount);
    frames++;
    if (items > peak_items) peak_items = items;
    if (meshes > peak_meshes) peak_meshes = meshes;
    if (matrix_bytes > peak_matrix) peak_matrix = matrix_bytes;
    int drops = drops_item + drops_mesh + drops_matrix;
    if (drops > 0 && drop_lines < 300) {
        drop_lines++;
        printf("[BTGA POOL] frame %d: items=%d meshes=%d matrix=0x%X dropped item=%d mesh=%d matrix=%d\n",
            frames, items, meshes, matrix_bytes, drops_item, drops_mesh, drops_matrix);
    }
    if (frames % 120 == 0) {
        printf("[BTGA POOL] %d frames: peak items=%d/%d (was 2288) meshes=%d/%d (was 300) matrix=0x%X/0xC000\n",
            frames, peak_items, kItemLimit, peak_meshes, kMeshLimit, peak_matrix);
        peak_items = peak_meshes = peak_matrix = 0;
    }
    fflush(stdout);
    drops_item = drops_mesh = drops_matrix = 0;
    matrix_bytes = 0;
}
