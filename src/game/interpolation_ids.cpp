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
// So the float matrix's address identifies the object across frames. The
// first step remembers which float matrix each Mtx came from; the walker then
// puts a gEXMatrixGroup with that ID in front of every matrix load. A model
// drawn more than once per frame repeats its ID, and RT64 pairs repeats in
// order (G_EX_ORDER_LINEAR). Matrices without a float source keep RT64's
// automatic matching.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
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
    std::unordered_map<uint32_t, std::array<int, 2>> site_stats;

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

    // gEXMatrixGroup(id, G_EX_INTERPOLATE_DECOMPOSE, push, G_MTX_MODELVIEW, ...) with RT64's
    // default components: position, rotation, scale, skew, perspective, tiles and look-at
    // AUTO, vertices and texcoords SKIP, aspect AUTO, not editable.
    void write_matrix_group(uint8_t* rdram, uint32_t id, bool push) {
        constexpr uint32_t kAuto = 2;
        const uint32_t order = (id == kIdAuto) ? 1u : 0u; // G_EX_ORDER_AUTO for the automatic group, LINEAR for IDs
        const uint32_t params =
            (push ? 1u : 0u) |       // push
            (0u << 1) |              // proj: modelview
            (1u << 2) |              // mode: decompose
            (kAuto << 3) |           // position
            (kAuto << 5) |           // rotation
            (kAuto << 7) |           // scale
            (kAuto << 9) |           // skew
            (kAuto << 11) |          // perspective
            (0u << 13) |             // vertices: skip
            (kAuto << 15) |          // tiles
            (order << 17) |          // ordering
            (0u << 19) |             // not editable
            (0u << 20) |             // aspect: auto
            (0u << 22) |             // texcoords: skip
            (kAuto << 24);           // look-at
        write_cmd(rdram, 0x6400000Cu, id); // G_EX_MATRIXGROUP_V1
        write_cmd(rdram, params, 0);
    }
}

// func_8007B1F0, at L_8007B2B0 (both paths join there): $s5 is the Mtx about to be
// queued, $s1 the float matrix it came from (0 when the caller passed a ready Mtx).
extern "C" void btga_interp_queue_mtx(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t mtx = (uint32_t)ctx->r21;
    const uint32_t source = (uint32_t)ctx->r17;
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

    // TEMPORARY (Android port): which call sites queue the models (return address saved at
    // 0x3C($sp)), with and without a float matrix.
    site_stats[(uint32_t)MEM_W(0x3C, ctx->r29)][source != 0 ? 1 : 0]++;
}

// From the frame clear (src/game/widescreen.cpp): start a new generation.
void btga_interp_new_frame() {
    current ^= 1;
    generations[current].mtx_source.clear();
    generations[current].source_uses.clear();
    if (++stats.frames >= 60) {
        if (stats.lines < 200) {
            std::fprintf(stderr, "[BTGA IDS] 60 frames: %d matrix loads tagged, %d shared source (automatic), %d no source (automatic)\n",
                stats.tagged, stats.shared, stats.unknown);
        }
        if (stats.lines < 200) {
            std::vector<std::pair<uint32_t, std::array<int, 2>>> sites(site_stats.begin(), site_stats.end());
            std::sort(sites.begin(), sites.end(), [](const auto& a, const auto& b) { return (a.second[0] + a.second[1]) > (b.second[0] + b.second[1]); });
            std::string line = "[BTGA SITES]";
            for (size_t i = 0; (i < sites.size()) && (i < 10); i++) {
                char part[64];
                std::snprintf(part, sizeof(part), " %08X: %d/%d", sites[i].first, sites[i].second[1], sites[i].second[0]);
                line += part;
            }
            std::fprintf(stderr, "%s (site: with float matrix/ready Mtx, per 60 frames)\n", line.c_str());
        }
        site_stats.clear();
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
