// Matrix IDs for RT64's frame interpolation (Refresh Rate above the game's 30 fps).
//
// Without IDs RT64 pairs each model with one in the previous frame by comparing
// position, orientation and screen position, and picks wrongly between similar
// models often enough to show: tanks driving straight jitter back and forth, and
// a tank or its gryphon decal flashes now and then. It showed on Android
// (Retroid Pocket 6) and not on the owner's PC; the other recomp ports avoid
// the guess by tagging their objects, as done here.
//
// The game draws models in two steps:
// - func_8007B1F0 queues a draw. When given a float matrix ($s1, the caller's
//   matrix, which lives at the same address every frame), it converts it to a
//   fixed-point Mtx from a per-frame pool, caches it at $s1+0x40, and queues
//   the record with that Mtx ($s5).
// - func_8007B65C walks the queue and, whenever the record's Mtx changes, emits
//   gSPMatrix through func_8007AC34 into the main display list (head at
//   *(0x80114500)+0xC8), then the record's display lists.
//
// The first step finds each queued Mtx's owner -- the float matrix it came from,
// or for a ready Mtx the Mtx itself (see btga_interp_queue_mtx) -- and the walker
// puts a gEXMatrixGroup with that address as ID in front of every matrix load. A
// model drawn more than once per frame repeats its ID, and RT64 pairs repeats in
// order (G_EX_ORDER_LINEAR). Anything without a stable owner keeps RT64's
// automatic matching.
#include <cstdint>
#include <cstdio>
#include <unordered_map>

#include "recomp.h"

namespace {
    constexpr uint32_t kGfxContextPtr = 0x80114500; // -> struct with the main DL head at +0xC8
    constexpr uint32_t kIdAuto = 0xFFFFFFFFu;        // G_EX_ID_AUTO

    // Per frame: pool Mtx address -> float matrix it was converted from, and how many
    // distinct Mtx each float matrix produced. Some callers build several objects' matrices
    // in one scratch matrix; its address is no object's identity, so those keep RT64's
    // automatic matching (round 1 of this gave them all one ID, and RT64 blended between
    // different objects, scaling them up and back). Two generations, swapped at the frame
    // clear, since queueing and drawing need not fall in the same one.
    struct Generation {
        std::unordered_map<uint32_t, uint32_t> mtx_source;
        std::unordered_map<uint32_t, uint32_t> source_uses;
    };
    Generation generations[2];
    int current = 0;

    uint32_t id_for(uint32_t mtx) {
        for (int g = 0; g < 2; g++) {
            const Generation& gen = generations[(current + 2 - g) % 2];
            auto it = gen.mtx_source.find(mtx);
            if (it != gen.mtx_source.end()) {
                auto uses = gen.source_uses.find(it->second);
                return (uses != gen.source_uses.end() && uses->second == 1) ? it->second : 0xFFFFFFFFu;
            }
        }
        return 0xFFFFFFFFu;
    }

    // TEMPORARY (Android port): per 60 frames, matrix loads tagged with an ID, left automatic
    // because their source is shared, and left automatic with no known source.
    struct Stats {
        int frames = 0, tagged = 0, shared = 0, unknown = 0, lines = 0;
    };
    Stats stats;

    bool group_pushed = false;

    gpr kseg0(uint32_t address) {
        return (gpr)(int32_t)address;
    }

    void write_cmd(uint8_t* rdram, uint32_t w0, uint32_t w1) {
        gpr ctx_struct = kseg0((uint32_t)MEM_W(0, kseg0(kGfxContextPtr)));
        gpr head = kseg0((uint32_t)MEM_W(0xC8, ctx_struct));
        MEM_W(0, head) = (int32_t)w0;
        MEM_W(4, head) = (int32_t)w1;
        MEM_W(0xC8, ctx_struct) = (int32_t)((uint32_t)head + 8);
    }

    // gEXMatrixGroupDecomposedNormal(id, push, G_MTX_MODELVIEW, G_EX_EDIT_NONE), as Banjo and
    // other recomps tag their objects: position, rotation, scale, skew, perspective and tiles
    // always interpolated, vertices and texcoords skipped, look-at automatic.
    //
    // Not RT64's AUTO components (its default): AUTO position stops interpolating on any frame
    // where the object's speed jumps by 10x over the previous frame, so an object whose
    // per-frame movement is uneven flips between smoothed and snapped frames -- the
    // back-and-forth jitter. Untagged models (id G_EX_ID_AUTO) keep automatic matching but get
    // the same components.
    uint32_t matrix_group_params(uint32_t id, bool push) {
        constexpr uint32_t kInterpolate = 1, kAuto = 2;
        const uint32_t order = (id == kIdAuto) ? 1u : 0u; // G_EX_ORDER_AUTO for the automatic group, LINEAR for IDs
        const uint32_t params =
            (push ? 1u : 0u) |       // push
            (0u << 1) |              // proj: modelview
            (1u << 2) |              // mode: decompose
            (kInterpolate << 3) |    // position
            (kInterpolate << 5) |    // rotation
            (kInterpolate << 7) |    // scale
            (kInterpolate << 9) |    // skew
            (kInterpolate << 11) |   // perspective
            (0u << 13) |             // vertices: skip
            (kInterpolate << 15) |   // tiles
            (order << 17) |          // ordering
            (0u << 19) |             // not editable
            (0u << 20) |             // aspect: auto
            (0u << 22) |             // texcoords: skip
            (kAuto << 24);           // look-at
        return params;
    }

    void write_matrix_group(uint8_t* rdram, uint32_t id, bool push) {
        write_cmd(rdram, 0x6400000Cu, id); // G_EX_MATRIXGROUP_V1
        write_cmd(rdram, matrix_group_params(id, push), 0);
    }
}

// func_8007B1F0, at L_8007B2B0 (both paths join there): $s5 is the Mtx about to be
// queued, $s1 the float matrix it came from (0 when the caller passed a ready Mtx).
//
// The ready Mtx (tanks and most other objects) are each object's own, in object memory
// around 0x802A0000-0x802EFFFF, reused every frame: about 100 distinct ones per second
// queued thousands of times. Their address is the object's ID. Only those in the per-frame
// matrix pools (0x801298C0, 2 x 0xC000) say nothing about the object; they stay automatic.
extern "C" void btga_interp_queue_mtx(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t mtx = (uint32_t)ctx->r21;
    const uint32_t float_source = (uint32_t)ctx->r17;
    const bool in_pool = (mtx >= 0x801298C0u) && (mtx < 0x801298C0u + 2 * 0xC000u);
    const uint32_t source = (float_source != 0) ? float_source : (in_pool ? 0u : mtx);
    Generation& gen = generations[current];
    auto it = gen.mtx_source.find(mtx);
    if (it != gen.mtx_source.end()) {
        if (it->second == source) {
            return; // the same object queued again (another display list for it)
        }
        if (--gen.source_uses[it->second] == 0) {
            gen.source_uses.erase(it->second);
        }
        gen.mtx_source.erase(it);
    }
    if (source != 0) {
        gen.mtx_source[mtx] = source;
        gen.source_uses[source]++;
    }

    // TEMPORARY (Android port): for the shared scratch matrices (tanks), the registers that
    // func_800AE184 (the multi-part model drawer, frame 0xA0 just above ours) saved for its
    // caller, and that caller's stack arguments -- to find the tank's own record. One frame in 60.
    if (((float_source & 0xFFFFF000u) == 0x8021B000u) && (stats.frames == 30) && (stats.lines < 30)) {
        const gpr up = ctx->r29 + 0x40;
        std::fprintf(stderr, "[BTGA TANK] src %08X mesh %08X | s0 %08X s1 %08X s2 %08X s3 %08X s4 %08X s5 %08X s6 %08X s7 %08X fp %08X | args %08X %08X %08X %08X %08X %08X %08X\n",
            float_source, (uint32_t)ctx->r19,
            (uint32_t)MEM_W(0x78, up), (uint32_t)MEM_W(0x7C, up), (uint32_t)MEM_W(0x80, up), (uint32_t)MEM_W(0x84, up), (uint32_t)MEM_W(0x88, up),
            (uint32_t)MEM_W(0x8C, up), (uint32_t)MEM_W(0x90, up), (uint32_t)MEM_W(0x94, up), (uint32_t)MEM_W(0x98, up),
            (uint32_t)MEM_W(0xA0, up), (uint32_t)MEM_W(0xA4, up), (uint32_t)MEM_W(0xA8, up), (uint32_t)MEM_W(0xAC, up),
            (uint32_t)MEM_W(0xB0, up), (uint32_t)MEM_W(0xB4, up), (uint32_t)MEM_W(0xC4, up));
    }
}

// From the frame clear (src/game/widescreen.cpp), at the head of the frame's display list:
// give the base of RT64's model matrix-group stack (reset every frame) the same components,
// for everything drawn without passing the queue walker (terrain, effects, ...). Returns the
// new head.
uint32_t btga_interp_frame_base_group(uint8_t* rdram, uint32_t head) {
    const gpr h = kseg0(head);
    MEM_W(0x00, h) = (int32_t)0xE0525464u; // gEXEnable
    MEM_W(0x04, h) = (int32_t)0x10000064u;
    MEM_W(0x08, h) = (int32_t)0x6400000Cu; // gEXMatrixGroup, no push: replaces the base entry
    MEM_W(0x0C, h) = (int32_t)kIdAuto;
    MEM_W(0x10, h) = (int32_t)matrix_group_params(kIdAuto, false);
    MEM_W(0x14, h) = 0;
    return head + 0x18;
}

// From the frame clear (src/game/widescreen.cpp): start a new generation.
void btga_interp_new_frame() {
    current ^= 1;
    generations[current].mtx_source.clear();
    generations[current].source_uses.clear();
    if (++stats.frames >= 60) {
        if (stats.lines < 200) {
            std::fprintf(stderr, "[BTGA IDS] 60 frames: %d matrix loads tagged, %d with a shared float matrix (automatic), %d from the per-frame pool (automatic)\n",
                stats.tagged, stats.shared, stats.unknown);
        }
        const int lines = stats.lines + 1;
        stats = Stats();
        stats.lines = lines;
    }
}

// func_8007B65C, before 0x8007B7B4 (ahead of the gSPMatrix through func_8007AC34): $v0 is
// the record's Mtx.
extern "C" void btga_interp_matrix_load(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t mtx = (uint32_t)ctx->r2;
    // The walker stops drawing once the head passes the limit at 0x5C($sp) (buffer base +
    // 0xADE0, 0x100 short of its end). Don't start tagging that close to it; once tagging
    // has started, a few more commands still fit.
    const gpr ctx_struct = kseg0((uint32_t)MEM_W(0, kseg0(kGfxContextPtr)));
    const uint32_t head = (uint32_t)MEM_W(0xC8, ctx_struct);
    const uint32_t limit = (uint32_t)MEM_W(0x5C, ctx->r29);
    if (!group_pushed && (head + 0x400 > limit)) {
        return;
    }
    const uint32_t id = id_for(mtx);
    if (id != kIdAuto) {
        stats.tagged++;
    }
    else {
        bool known = false;
        for (const Generation& gen : generations) {
            known = known || (gen.mtx_source.find(mtx) != gen.mtx_source.end());
        }
        (known ? stats.shared : stats.unknown)++;
    }
    if (!group_pushed) {
        write_cmd(rdram, 0xE0525464u, 0x10000064u); // gEXEnable
        write_matrix_group(rdram, id, true);
        group_pushed = true;
    }
    else {
        write_matrix_group(rdram, id, false);
    }
}

// func_8007B65C's epilogue (0x8007B8B8): back to automatic matching for whatever is
// drawn next.
extern "C" void btga_interp_queue_end(uint8_t* rdram, recomp_context*) {
    if (group_pushed) {
        write_cmd(rdram, 0x6400000Du, 1u); // gEXPopMatrixGroup(G_MTX_MODELVIEW)
        group_pushed = false;
    }
}
