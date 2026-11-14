// Steady Distant Scenery (Graphics tab, on by default since round 219; round 218).
//
// The map objects (buildings, walls, fences) are drawn per view by
// func_800AF978, and only those the game finds visible: each object's ground
// rectangle, within the draw distance, is clipped to the view
// (func_800AB980 -> func_800AAEA0) and written into a coarse occlusion
// buffer at 0x80237180 (func_800AAC14: per view 3 rows of 100 columns, each
// the nearest depth and its object), and func_800ABE6C lists the objects
// nearest in at least one column. A distant building can win a column on
// one frame and lose it on the next, and is then missing for a frame: round
// 217 counted such one-frame gaps while driving mission 1 (a few in three
// minutes at Original, 13-19 at Very Far).
//
// Most of the scenery isn't drawn through that list but through the loop
// after it, which runs each object (with its per-view bit at +0x4D set)
// through func_800AC538, a test of its rectangle against this view; round
// 218 caught 38 objects in three minutes (mission 1, Very Far) failing it for
// exactly one frame between two passes, the rest of the gaps.
//
// The N64 needed these tests to keep its frame time; a PC doesn't. With the
// option on, an object the list had, or that passed the test, in the last
// kHold frames for this view is kept when it drops out, so a one-frame loss
// no longer blinks. Kept objects are drawn like any other (depth testing hides
// what's really behind something); one that has left the view for good is
// drawn kHold frames longer, off screen. Still left: objects that pick their
// detail level by distance and whose farthest level is empty can vanish while
// the camera's distance wobbles around that threshold (a few in three
// minutes).
//
// The list changes what the drawing code writes to game memory (type-2
// objects set a per-view bit at +0x4D, the draw item pool), so in netplay the
// host's setting is everyone's (src/netplay/settings.cpp); the history below
// is built from the game's own lists only, the same on every PC.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "recomp.h"

#include "btga_config.h"
#include "btga_netplay.h"

namespace {
    std::atomic<bool> steady_setting{ true };

    constexpr uint32_t kHold = 2;      // frames an object stays after it drops out
    constexpr uint32_t kListMax = 256; // func_800AF978's list at sp+0x28: 0x400 bytes
    constexpr uint32_t kViews = 4;

    // Per view: the frame each object index was last listed by the game.
    struct ViewHistory {
        uint32_t frame = 0;
        uint32_t objects = 0; // the level's object count when the history was made
        std::vector<uint32_t> last_listed;  // by the occlusion list
        std::vector<uint32_t> last_passed;  // func_800AC538's test
        std::vector<int8_t> lod;            // the detail level last picked (round 221)
        std::vector<uint32_t> lod_frame;    // and the frame it was picked in
    };
    std::array<ViewHistory, kViews> history;
}

void btga::set_steady_scenery(bool on) {
    steady_setting.store(on);
}

bool btga::steady_scenery_setting() {
    return steady_setting.load();
}

bool btga::steady_scenery() {
    return btga::netplay::steady_scenery(steady_setting.load());
}

// func_800AF978 before 0x800AFAE0, right after func_800ABE6C returned: $v0 is
// how many objects it listed at $sp+0x28, $s4 the view, and the view's map
// record (0x464($sp)) holds the object count at +0x10.
extern "C" void btga_scenery_visible_list(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t view = (uint32_t)ctx->r20;
    if (view >= kViews) {
        return;
    }
    const gpr sp = ctx->r29;
    const gpr map = (gpr)(int32_t)MEM_W(0x464, sp);
    const uint32_t objects = (uint32_t)MEM_W(0x10, map);
    uint32_t count = (uint32_t)ctx->r2;
    if (objects == 0 || objects > 0x10000 || count > kListMax) {
        return;
    }
    ViewHistory& h = history[view];
    if (h.objects != objects || h.last_listed.size() != objects) {
        // A new level (or the first frame): no history yet.
        h.objects = objects;
        h.last_listed.assign(objects, 0);
        h.last_passed.assign(objects, 0);
        h.lod.assign(objects, -1);
        h.lod_frame.assign(objects, 0);
        h.frame = kHold + 1;
    }
    h.frame++;
    std::vector<bool> listed(objects, false);
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t index = (uint32_t)MEM_W(0x28 + 4 * i, sp);
        if (index < objects) {
            listed[index] = true;
            h.last_listed[index] = h.frame;
        }
    }
    if (!btga::steady_scenery()) {
        return;
    }
    for (uint32_t index = 0; index < objects && count < kListMax; index++) {
        if (!listed[index] && h.last_listed[index] != 0 && h.frame - h.last_listed[index] <= kHold) {
            MEM_W(0x28 + 4 * count, sp) = (int32_t)index;
            count++;
        }
    }
    ctx->r2 = (gpr)(int32_t)count;
}

// func_800AF978 before 0x800AFDE4: $v0 is func_800AC538's answer for the
// object in $s0 (this view's test against the object's rectangle, run after
// its per-view bit at +0x4D), 0 to skip it. Most of the scenery is drawn
// through this loop rather than the list above.
extern "C" void btga_scenery_object_test(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t view = (uint32_t)ctx->r20;
    if (view >= kViews) {
        return;
    }
    ViewHistory& h = history[view];
    const gpr map = (gpr)(int32_t)MEM_W(0x464, ctx->r29);
    const uint32_t base = (uint32_t)MEM_W(0x14, map);
    const uint32_t object = (uint32_t)ctx->r16;
    if (object < base || (object - base) % 0x50 != 0) {
        return;
    }
    const uint32_t index = (object - base) / 0x50;
    if (index >= h.last_passed.size()) {
        return;
    }
    if (ctx->r2 != 0) {
        h.last_passed[index] = h.frame;
    }
    else if (btga::steady_scenery() && h.last_passed[index] != 0 && h.frame - h.last_passed[index] <= kHold) {
        ctx->r2 = 1;
    }
}

// Detail levels (round 221). Type 2 and 3 objects pick a detail level by the
// squared distance from the camera to their position, against thresholds at
// 0x80072F68 on (each of func_800AF978's three loops has its own): type 2 two
// levels, type 3 three. Some objects' farthest level is empty, so one standing
// at about that distance vanishes and comes back as the camera's distance
// wobbles across the threshold (and a level swap of a drawn one pops back and
// forth). With the option on, a level change goes through only once the
// distance is kLodMargin past the threshold crossed (squared: about 5% in
// distance), as long as the object had a level the frame before.
namespace {
    constexpr float kLodMargin = 0.1f;

    float rdram_float(uint8_t* rdram, uint32_t address) {
        const uint32_t bits = (uint32_t)MEM_W(0, (gpr)(int32_t)address);
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    // The level the game's own test gives for this squared distance, with the
    // thresholds scaled.
    int lod_for(float dist2, int type, float near_far, float far_far, float two_far, float scale) {
        if (type == 2) {
            return (dist2 > two_far * scale) ? 1 : 0;
        }
        if (dist2 > far_far * scale) {
            return 2;
        }
        return (dist2 > near_far * scale) ? 1 : 0;
    }

    // At the loop's join after the level test: $v1 the level, $f2 the squared
    // distance (types 2 and 3), `model` the object's model record (type at +0xC),
    // $s0 the object, $s4 the view. `thresholds`: the loop's type 2 threshold, then
    // type 3's far and near ones (0x80072F68, F6C, F70 for the first loop).
    void hold_lod(uint8_t* rdram, recomp_context* ctx, uint32_t model, uint32_t thresholds) {
        const uint32_t view = (uint32_t)ctx->r20;
        if (view >= kViews || model == 0) {
            return;
        }
        const int type = (int)(uint8_t)MEM_B(0xC, (gpr)(int32_t)model);
        if (type != 2 && type != 3) {
            return;
        }
        ViewHistory& h = history[view];
        const gpr map = (gpr)(int32_t)MEM_W(0x464, ctx->r29);
        const uint32_t base = (uint32_t)MEM_W(0x14, map);
        const uint32_t object = (uint32_t)ctx->r16;
        if (object < base || (object - base) % 0x50 != 0) {
            return;
        }
        const uint32_t index = (object - base) / 0x50;
        if (index >= h.lod.size()) {
            return;
        }
        int lod = (int)ctx->r3;
        const int last = h.lod[index];
        // TEMP measurement
        static const bool lodlog = std::getenv("BTGA_LOD_LOG") != nullptr;
        static uint32_t calls = 0;
        calls++;
        static uint32_t req = 0, held_n = 0, to_empty = 0, wobble = 0, frames = 0, last_frame = 0;
        static std::vector<uint32_t> changed_at;
        const int game_lod = lod;
        if (lodlog && view == 0) {
            if (changed_at.size() != h.lod.size()) changed_at.assign(h.lod.size(), 0);
            if (h.frame != last_frame) { last_frame = h.frame; if (++frames % 100 == 0) std::fprintf(stderr, "[LOD] frames %u changes %u to-empty %u wobble %u held %u calls %u\n", frames, req, to_empty, wobble, held_n, calls); }
            if (last >= 0 && lod != last && h.frame - h.lod_frame[index] <= 1) {
                req++;
                const gpr lv = (gpr)(int32_t)((uint32_t)MEM_W(0, (gpr)(int32_t)model) + 8 * lod);
                if ((uint8_t)MEM_B(4, lv) == 0) to_empty++;
                if (h.frame - changed_at[index] <= 2) wobble++;
                changed_at[index] = h.frame;
            }
        }
        if (btga::steady_scenery() && last >= 0 && lod != last && h.frame - h.lod_frame[index] <= 1) {
            const float dist2 = ctx->f2.fl;
            const float two = rdram_float(rdram, thresholds);
            const float far = rdram_float(rdram, thresholds + 4);
            const float near = rdram_float(rdram, thresholds + 8);
            // Farther: the thresholds moved out; nearer: in. Never past the game's level.
            const int held = lod_for(dist2, type, near, far, two, (lod > last) ? 1.0f + kLodMargin : 1.0f - kLodMargin);
            lod = (lod > last) ? std::clamp(held, last, lod) : std::clamp(held, lod, last);
            ctx->r3 = (gpr)lod;
            if (lodlog && view == 0 && lod != game_lod) held_n++;
        }
        h.lod[index] = (int8_t)lod;
        h.lod_frame[index] = h.frame;
    }
}

// func_800AF978's three loops (the occlusion list's, then the two over every
// object), at their joins after the level test.
extern "C" void btga_scenery_lod_list(uint8_t* rdram, recomp_context* ctx) {
    hold_lod(rdram, ctx, (uint32_t)ctx->r5, 0x80072F68u);
}

extern "C" void btga_scenery_lod_objects(uint8_t* rdram, recomp_context* ctx) {
    hold_lod(rdram, ctx, (uint32_t)ctx->r4, 0x80072F78u);
}

extern "C" void btga_scenery_lod_objects2(uint8_t* rdram, recomp_context* ctx) {
    hold_lod(rdram, ctx, (uint32_t)ctx->r4, 0x80072F84u);
}
