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
#include <array>
#include <atomic>
#include <cstdint>
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
