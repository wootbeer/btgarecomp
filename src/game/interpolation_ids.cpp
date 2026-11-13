// Matrix IDs for RT64's frame interpolation (Refresh Rate above the game's 30 fps).
//
// Without IDs RT64 pairs each model with one in the previous frame by comparing
// position, orientation and screen position, and picks wrongly between similar
// models often enough to show: tanks driving straight jitter back and forth, and
// a tank or its gryphon decal flashes now and then (and comes loose from the
// tanks in the intro). It showed first on Android (Retroid Pocket 6), and on PC
// too once the game ran at full speed (round 138); the other recomp ports avoid
// the guess by tagging their objects, as done here. Ported from the Android
// branch in round 145.
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
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
        // A tank's extra parts (round 169): tank object + scratch slot -> the
        // Mtx built from that scratch for it this frame, in order.
        std::unordered_map<uint32_t, std::vector<uint32_t>> extra_parts;
        // Tank parts (odd IDs) -> their rotation this frame, rows normalized
        // (round 171), and the ones that turned too far since the last frame.
        std::unordered_map<uint32_t, std::array<float, 9>> rotations;
        std::unordered_set<uint32_t> snap_rotation;
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

    bool group_pushed = false;

    // The object func_800A1FDC's draw loop is drawing (its 0x44-byte record),
    // the Mtx it has queued so far in this draw, and how many parts each
    // object drew in the last three frames.
    uint32_t draw_object = 0;
    // The tank whose hull or turret this draw has queued so far (round 195), or 0.
    uint32_t draw_tank = 0;
    std::vector<uint32_t> draw_parts;
    std::unordered_map<uint32_t, uint32_t> object_parts[3];
    int object_parts_current = 0;

    // An ID for the draw object's part with this Mtx, or 0 to leave it to
    // automatic matching. Only for an object that drew the same number of
    // parts in the two frames before (as the first game's port does for its
    // debris): parts are told apart by their order, which means nothing
    // while the count changes.
    uint32_t object_part_id(uint32_t mtx) {
        size_t part = 0;
        while (part < draw_parts.size() && draw_parts[part] != mtx) {
            part++;
        }
        if (part == draw_parts.size()) {
            draw_parts.push_back(mtx);
        }
        uint32_t& count = object_parts[object_parts_current][draw_object];
        count = std::max(count, (uint32_t)part + 1);
        const auto& last = object_parts[(object_parts_current + 2) % 3];
        const auto& before = object_parts[(object_parts_current + 1) % 3];
        const auto a = last.find(draw_object), b = before.find(draw_object);
        if (part >= 16 || a == last.end() || b == before.end() || a->second != b->second || part >= a->second) {
            return 0;
        }
        // Even (the tank IDs are odd), below the RDRAM addresses used as IDs.
        return 0x10000000u | ((draw_object & 0x7FFFFFu) << 5) | ((uint32_t)part << 1);
    }

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

    // The matrix group in front of a matrix load. Tagged objects (an ID) get
    // gEXMatrixGroupDecomposedNormal(id, push, G_MTX_MODELVIEW, G_EX_EDIT_NONE), as Banjo and other
    // recomps tag theirs: position, rotation, scale, skew, perspective and tiles always
    // interpolated, vertices and texcoords skipped, look-at automatic. Not RT64's AUTO components:
    // AUTO position stops interpolating on any frame where the object's speed jumps by 10x over
    // the previous one, so an object whose per-frame movement is uneven flips between smoothed and
    // snapped frames -- the tanks' back-and-forth jitter.
    //
    // Untagged models (G_EX_ID_AUTO, matched automatically) keep RT64's defaults (all AUTO). They
    // had the tagged components too while the tanks were untagged, but forcing interpolation on
    // whatever automatic matching pairs flickered explosions on the in-between frames (gone at
    // Original), and their sprites changing size and shape every frame are just what AUTO stops.
    // A tank part that turned more than this in one game frame (round 171).
    constexpr float kTankSnapCos = 0.7071f; // 45 degrees

    // Whether this ID's rotation jumped this frame: then only its position and
    // scale are interpolated, the rotation taken as it is.
    bool snaps_rotation(uint32_t id) {
        if ((id & 1) == 0 || id == kIdAuto) {
            return false;
        }
        for (const Generation& gen : generations) {
            if (gen.snap_rotation.count(id) != 0) {
                return true;
            }
        }
        return false;
    }

    uint32_t matrix_group_params(uint32_t id, bool push) {
        constexpr uint32_t kInterpolate = 1, kAuto = 2, kSkip = 0;
        const bool tagged = (id != kIdAuto);
        const uint32_t motion = tagged ? kInterpolate : kAuto;
        const uint32_t rotation = snaps_rotation(id) ? kSkip : motion;
        const uint32_t order = tagged ? 0u : 1u; // G_EX_ORDER_LINEAR for IDs, AUTO for the automatic group
        const uint32_t params =
            (push ? 1u : 0u) |       // push
            (0u << 1) |              // proj: modelview
            (1u << 2) |              // mode: decompose
            (motion << 3) |          // position
            (rotation << 5) |        // rotation
            (motion << 7) |          // scale
            (motion << 9) |          // skew
            (motion << 11) |         // perspective
            (0u << 13) |             // vertices: skip
            (motion << 15) |         // tiles
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
    uint32_t source = (float_source != 0) ? float_source : (in_pool ? 0u : mtx);

    // Tanks: the multi-part model drawer func_800AE184 builds every tank's hull and turret
    // matrices in the same two scratch matrices, so their address says nothing. Its caller
    // loops over the tank objects (0x4CC apart, from 0x801AD128) in $s3; the drawer saves
    // that at 0x84 of its frame, which sits right above ours (0x40). The scratch matrix,
    // its own argument, is at 0x4C there -- checked, so another caller is left alone.
    //
    // Round 169: some tanks have extra parts, built in more scratch matrices (0x8021B500 and
    // 0x8021B548; round 173 added 0x8021B478, the Rattler's multi-barrel cannon), each several
    // meshes under one Mtx and sometimes built twice a frame. Named by their scratch address
    // alone, a part had an ID only on frames when no other tank drew one, so the ID moved from
    // tank to tank and RT64 blended one tank's part toward another's. They are named by the
    // tank too, and by the order the tank builds them in the frame. With tank IDs they also get
    // round 171's rotation snap.
    const bool hull_or_turret = (float_source == 0x8021B2A0u) || (float_source == 0x8021B2E8u);
    const bool extra_part = (float_source == 0x8021B500u) || (float_source == 0x8021B548u) ||
                            (float_source == 0x8021B478u);
    if (hull_or_turret || extra_part) {
        const gpr drawer_sp = ctx->r29 + 0x40;
        uint32_t object = 0;
        if ((uint32_t)MEM_W(0x4C, drawer_sp) == float_source) {
            object = (uint32_t)MEM_W(0x84, drawer_sp);
        }
        else if (extra_part && ((uint32_t)MEM_W(0x58, drawer_sp) == float_source) &&
                 ((uint32_t)MEM_W(0xA0, drawer_sp) == (uint32_t)drawer_sp + 0x88u + 0x1E8u) &&
                 ((uint32_t)MEM_W(0xA4, drawer_sp) == 1u)) {
            // Round 173: most tanks' extra parts come the other way, from func_80092C38 at
            // 0x80094590 through the generic model drawer func_800AD9A8 (frame 0x88). It is
            // given the part's matrix from the caller's $s0 and saves that at 0x58, and the
            // tank ($s3) at 0x64; its stack arguments (0x18: the caller's sp + 0x1E8, 0x1C: 1)
            // mark the call. In mission 1 only one tank at a time came through func_800AE184; the
            // rest came this way, so their parts had no tank ID.
            object = (uint32_t)MEM_W(0x64, drawer_sp);
        }
        if ((object >= 0x80000000u) && (object < 0x80800000u) && ((object & 3) == 0)) {
            // Odd, so never the address of an Mtx or float matrix used as an ID; all
            // below the next tank (0x4CC on).
            if (draw_object != 0) {
                draw_tank = object;
            }
            if (hull_or_turret) {
                source = object | ((float_source == 0x8021B2A0u) ? 1u : 3u);
            }
            else {
                // IDs tank + 5, 7 and 11 (0x8021B478, round 173), each + 8 * k.
                const uint32_t slot = (float_source == 0x8021B500u) ? 5u : (float_source == 0x8021B548u) ? 7u : 11u;
                std::vector<uint32_t>& built = generations[current].extra_parts[object + slot];
                size_t k = 0;
                while (k < built.size() && built[k] != mtx) {
                    k++;
                }
                if (k == built.size()) {
                    built.push_back(mtx);
                }
                if (k < 16) {
                    source = object + slot + 8u * (uint32_t)k;
                }
            }
        }
    }
    // Other objects drawn from the object loop (round 156): a matrix built in
    // the draw function's own stack frame is scratch shared by every object
    // of its kind (flying wreckage: func_800DA340's at sp+0x20), so the object
    // the loop is drawing names it, with the part's order within that draw.
    //
    // Round 195 (GitHub issue 4): the Annihilator boss is drawn this way, as one object: hull,
    // turret, a flash part (tank + 5) only while it fires, then its two side guns, both built in
    // one scratch matrix (0x8021B540). Counted among all the object's parts, the guns lost their
    // IDs on the frames the part count changed, then came back shifted by one, each gun named
    // as the other had been -- the guns and turret flickering when it fired. In a draw that has
    // queued a tank's hull or turret, such parts are named by that tank instead, in the order
    // they come among themselves (tank + 9 + 8 * k: odd, clear of the 1, 3, 5, 7 and 11 above).
    if ((draw_object != 0) && (source == float_source) && (float_source > (uint32_t)ctx->r29) &&
        (float_source < (uint32_t)ctx->r29 + 0x1000)) {
        if (draw_tank != 0) {
            // Only the first time this frame: the hull's Mtx comes again for the tank's decal
            // (it keeps the hull's ID, below), and counting it would shift the parts after it.
            if (generations[current].mtx_source.count(mtx) != 0) {
                return;
            }
            std::vector<uint32_t>& built = generations[current].extra_parts[draw_tank + 9u];
            size_t k = 0;
            while (k < built.size() && built[k] != mtx) {
                k++;
            }
            if (k == built.size()) {
                built.push_back(mtx);
            }
            if (k < 16) {
                source = draw_tank + 9u + 8u * (uint32_t)k;
            }
        }
        else {
            const uint32_t object_id = object_part_id(mtx);
            if (object_id != 0) {
                source = object_id;
            }
        }
    }
    Generation& gen = generations[current];
    auto it = gen.mtx_source.find(mtx);
    if (it != gen.mtx_source.end()) {
        // The same Mtx queued again this frame is the same matrix, so the same object: another
        // display list for it, or another drawer reusing it. Each tank's gryphon decal is queued
        // after the tank with the hull's Mtx, but naming the scratch matrix, which would cost
        // the tank its ID; the first owner stays, unless a tank ID replaces a shared one.
        const bool tank_over_shared = ((source & 1) != 0) && ((it->second & 1) == 0);
        if ((it->second == source) || (source == 0) || !tank_over_shared) {
            return;
        }
        if (--gen.source_uses[it->second] == 0) {
            gen.source_uses.erase(it->second);
        }
        gen.mtx_source.erase(it);
    }
    if (source != 0) {
        gen.mtx_source[mtx] = source;
        gen.source_uses[source]++;
        // Round 171: a tank part whose rotation jumps in one frame. The Rattler's cannon (its
        // turret) showed it: on two in-between frames in a row it swung off sideways, tilted,
        // while the frames around them were right -- its turret turning about half a turn
        // and back, which a round, many-barrelled cannon hides on the game's own frames, and
        // RT64 blended through the angle between. Such a part takes its new rotation as it
        // is (position and scale still smoothed), as the sprites do (round 156).
        if ((source & 1) != 0 && float_source != 0 && gen.rotations.count(source) == 0) {
            std::array<float, 9> rot;
            for (int row = 0; row < 3; row++) {
                float len = 0.0f;
                for (int col = 0; col < 3; col++) {
                    const uint32_t bits = (uint32_t)MEM_W(0x10 * row + 4 * col, (gpr)(int32_t)float_source);
                    std::memcpy(&rot[row * 3 + col], &bits, sizeof(float));
                    len += rot[row * 3 + col] * rot[row * 3 + col];
                }
                len = std::sqrt(len);
                for (int col = 0; col < 3; col++) {
                    rot[row * 3 + col] = (len > 1e-6f) ? rot[row * 3 + col] / len : 0.0f;
                }
            }
            gen.rotations[source] = rot;
            const Generation& last = generations[current ^ 1];
            auto prev = last.rotations.find(source);
            if (prev != last.rotations.end()) {
                // cos of the angle between: (trace(Rprev^T R) - 1) / 2
                float trace = 0.0f;
                for (int i = 0; i < 9; i++) {
                    trace += prev->second[i] * rot[i];
                }
                if ((trace - 1.0f) * 0.5f < kTankSnapCos) {
                    gen.snap_rotation.insert(source);
                }
            }
        }
    }
}

// From the frame clear (src/game/widescreen.cpp): start a new generation.
void btga_interp_sprites_new_frame();

void btga_interp_new_frame() {
    btga_interp_sprites_new_frame();
    object_parts_current = (object_parts_current + 1) % 3;
    object_parts[object_parts_current].clear();
    current ^= 1;
    generations[current].mtx_source.clear();
    generations[current].source_uses.clear();
    generations[current].extra_parts.clear();
    generations[current].rotations.clear();
    generations[current].snap_rotation.clear();
}

// func_800A1FDC, around each object's draw call (0x800A20F8 and after it): the
// object record in $a0.
extern "C" void btga_interp_draw_object_begin(uint8_t* rdram, recomp_context* ctx) {
    draw_object = (uint32_t)ctx->r4;
    draw_tank = 0;
    draw_parts.clear();
}

extern "C" void btga_interp_draw_object_end(uint8_t* rdram, recomp_context*) {
    draw_object = 0;
    draw_tank = 0;
}

// func_8007B65C, at L_8007B738: the start of one bucket's record list (the walker goes
// through 32 buckets of lists, for each view). It loads a record's Mtx only when it differs
// from the last one loaded (0x3C($sp)), and that carries over from the previous list. So
// whether an object's records in two buckets share one load depends on which records happen
// to end one list and start the next -- the player's tank decal (its own bucket, the hull's
// Mtx) sometimes did, sometimes not. RT64 pairs the loads of an ID in order, so the decal
// then went unpaired and snapped while the hull was interpolated: a flash on sharp turns.
// Forget the last Mtx here, so each list starts with a load (the same matrix again changes
// nothing drawn) and an object's loads are the same from frame to frame.
extern "C" void btga_interp_bucket_start(uint8_t* rdram, recomp_context* ctx) {
    MEM_W(0x3C, ctx->r29) = 0;
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

// --- Effect sprites (round 155) -------------------------------------------
//
// Fire, smoke and the other effect sprites go through a second queue:
// func_8007B498 records each with its world position (+0xC/+0x10/+0x14),
// angle (+0xA) and size (+0x18/+0x1C), and func_8007B8EC, after the main
// walker, builds a camera-facing matrix for each in every view, converts it
// into the per-frame Mtx pool and loads it (func_8007AC34 at 0x8007BC3C). No
// address identifies a sprite there, so they were all on RT64's automatic
// matching, which paired look-alike sprites with each other (explosions
// flickering on in-between frames) and, since round 147, gives up on the
// biggest groups of them (explosions at the game's own rate).
//
// As in the first game's port (its rounds 8 and 11): each sprite is paired
// with the previous frame's sprite in the same view closest to where that
// one was heading (its position plus its last movement), within 64 units,
// twice that for one seen only once, and keeps its ID; the rest get new IDs.
// Position and size are interpolated, and the angle when it changed by less
// than 45 degrees (round 156: wreckage spins steadily; a fireball can get a
// new angle every frame, and turning through those showed in the first game).
//
// Round 165: a sprite also pairs only with one about its own size (within
// kSpriteSizeRatio either way). Fire and smoke puffs die and new ones start
// small where they were, and pairing the two showed a puff swelling or
// shrinking through the in-between frames: the flashes in explosions. In Tank
// Wars about 2% of pairs changed size more than 2.5 times in a frame, against
// 1.2-1.7 times for a growing fireball.
namespace {
    constexpr uint32_t kSpriteIdBase = 0x5F000000u; // clear of addresses and the arrows' IDs
    constexpr float kSpritePairDistance = 64.0f;
    constexpr float kSpriteSizeRatio = 2.0f;
    constexpr size_t kSpriteViews = 4;

    struct Sprite {
        float x, y, z;
        float vx, vy, vz;
        float width, height;
        bool moved; // seen in the frame before too
        uint16_t angle;
        bool turn_smoothly;
        uint32_t id;
    };
    std::vector<Sprite> sprites_prev[kSpriteViews], sprites_cur[kSpriteViews];
    uint32_t next_sprite_id = 0;
    bool sprite_group_pushed = false;

    float read_float(uint8_t* rdram, gpr address) {
        const uint32_t bits = (uint32_t)MEM_W(0, address);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    bool similar_size(float a, float b) {
        a = std::fabs(a);
        b = std::fabs(b);
        return a <= b * kSpriteSizeRatio && b <= a * kSpriteSizeRatio;
    }

    const Sprite& pair_sprite(size_t view, float x, float y, float z, float width, float height, uint16_t angle) {
        std::vector<Sprite>& prev = sprites_prev[view];
        int best = -1;
        float best_dist = 0.0f;
        for (size_t i = 0; i < prev.size(); i++) {
            const Sprite& p = prev[i];
            const float dx = p.x + p.vx - x, dy = p.y + p.vy - y, dz = p.z + p.vz - z;
            const float d = dx * dx + dy * dy + dz * dz;
            const float room = p.moved ? kSpritePairDistance : 2.0f * kSpritePairDistance;
            if (!similar_size(p.width, width) || !similar_size(p.height, height)) {
                continue;
            }
            if (d <= room * room && (best < 0 || d < best_dist)) {
                best = (int)i;
                best_dist = d;
            }
        }
        Sprite s{ x, y, z, 0.0f, 0.0f, 0.0f, width, height, false, angle, false, 0 };
        if (best >= 0) {
            const Sprite& p = prev[best];
            s.vx = x - p.x;
            s.vy = y - p.y;
            s.vz = z - p.z;
            s.moved = true;
            s.id = p.id;
            // Spinning wreckage turns a little each frame; a fireball can get
            // a new angle every frame, and turning through the angles in
            // between showed in the first game.
            const int16_t turn = (int16_t)(uint16_t)(angle - p.angle);
            s.turn_smoothly = (turn > -0x2000) && (turn < 0x2000);
            prev.erase(prev.begin() + best);
        }
        else {
            next_sprite_id = (next_sprite_id + 1) & 0xFFFFFF;
            s.id = kSpriteIdBase | next_sprite_id;
        }
        sprites_cur[view].push_back(s);
        return sprites_cur[view].back();
    }

    // gEXMatrixGroupDecomposed(id, push, G_MTX_MODELVIEW): position and scale
    // interpolated, the angle when it changed little, skew, perspective,
    // vertices, tiles and texcoords skipped, order linear, not editable,
    // look-at automatic.
    uint32_t sprite_group_params(bool push, bool turn) {
        return (push ? 1u : 0u) | (1u << 2) | (1u << 3) | (turn ? (1u << 5) : 0u) | (1u << 7) | (2u << 24);
    }
}

void btga_interp_sprites_new_frame() {
    for (size_t v = 0; v < kSpriteViews; v++) {
        sprites_prev[v].swap(sprites_cur[v]);
        sprites_cur[v].clear();
    }
}

// func_8007B8EC before 0x8007BA50: a new view's own matrix load; nothing in it
// is a sprite.
extern "C" void btga_interp_sprite_view(uint8_t* rdram, recomp_context*) {
    if (sprite_group_pushed) {
        write_matrix_group(rdram, kIdAuto, false);
    }
}

// func_8007B8EC before 0x8007BC3C (gSPMatrix through func_8007AC34): the record
// in $s0, the view in $s1.
extern "C" void btga_interp_sprite_load(uint8_t* rdram, recomp_context* ctx) {
    const size_t view = (size_t)(uint32_t)ctx->r17;
    if (view >= kSpriteViews) {
        return;
    }
    // Same limit as the main walker's, at 0xEC($sp) here.
    const gpr ctx_struct = kseg0((uint32_t)MEM_W(0, kseg0(kGfxContextPtr)));
    const uint32_t head = (uint32_t)MEM_W(0xC8, ctx_struct);
    const uint32_t limit = (uint32_t)MEM_W(0xEC, ctx->r29);
    if (!sprite_group_pushed && (head + 0x400 > limit)) {
        return;
    }
    const gpr record = ctx->r16;
    const float x = read_float(rdram, record + 0xC);
    const float y = read_float(rdram, record + 0x10);
    const float z = read_float(rdram, record + 0x14);
    const float width = read_float(rdram, record + 0x18);
    const float height = read_float(rdram, record + 0x1C);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(width) || !std::isfinite(height)) {
        return;
    }
    const Sprite& sprite = pair_sprite(view, x, y, z, width, height, (uint16_t)MEM_HU(0xA, record));
    if (!sprite_group_pushed) {
        write_cmd(rdram, 0xE0525464u, 0x10000064u); // gEXEnable
        write_cmd(rdram, 0x6400000Cu, sprite.id);
        write_cmd(rdram, sprite_group_params(true, sprite.turn_smoothly), 0);
        sprite_group_pushed = true;
    }
    else {
        write_cmd(rdram, 0x6400000Cu, sprite.id);
        write_cmd(rdram, sprite_group_params(false, sprite.turn_smoothly), 0);
    }
}

// func_8007B8EC's epilogue (0x8007BCB8, also its early exit).
extern "C" void btga_interp_sprite_end(uint8_t* rdram, recomp_context*) {
    if (sprite_group_pushed) {
        write_cmd(rdram, 0x6400000Du, 1u); // gEXPopMatrixGroup(G_MTX_MODELVIEW)
        sprite_group_pushed = false;
    }
}
