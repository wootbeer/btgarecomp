// Fog and Draw Distance on the Graphics tab (round 158). Both default to the
// game's own behaviour.
//
// Fog: every frame func_8007A250 sets the fog with one
// G_MOVEWORD G_MW_FOG, 0x64009D00 = gSPFogPosition(995, 1000): multiplier
// 25600, offset -25344, so geometry fades into the fog colour over the last
// 0.5% of the depth range, ending at the far plane. With the fog off, the
// word becomes multiplier 0, offset 0: every vertex's fog factor is 0 and
// nothing is blended toward the fog colour. Nothing else writes the fog
// position (checked: 0x8007A674 is the game's only G_MW_FOG).
//
// Draw distance: the int at 0x8023A060 is how far objects are drawn (the
// map-object drawer culls against it, round 110) and sets the far plane, so
// the fog's position too. func_8009AE38 resets it at every scene start to
// 3400 / 2866 / 1800 by screen layout (0x8009AF4C), and the frame-rate
// governor func_80099FE8 then adds 50 a second while it's under 5000, or
// takes 50 off while the game runs under 20 fps. Campaign camera scripts set
// it too (func_800D25E0): a shot's own value (0x800D289C), and 5000 when the
// script ends (0x800D28B0), so a mission's camera sequence used to undo Far
// (round 160; Tank Wars and mission 1 have none). Far triples all three
// stores and Very Far multiplies them by 6; Map Edge sets them all to
// kMaxDrawDistance (round 161). The governor leaves anything over 5000 alone
// on a PC.
//
// The ceiling: the scenery cull (func_800AF978 -> func_800AB980) and
// func_800E2F9C / func_800E3460 square the draw distance in a 32-bit int,
// which overflows past 46,340.
//
// Gun emplacements (round 227): the turrets' range comes from the draw
// distance too. Its range checks (func_800E3460 at 0x800E36A8 and 0x800E3874;
// a third copy at 0x800E3404 is never called) are (draw distance)^2 * 0.92, a
// third of that for one kind: about 96% of the draw distance, 3400 at a
// one-player level's start growing to 5000 as the governor raises it. With
// Far and beyond they opened fire from three times as far or more (reported
// on GitHub). They're given the game's own draw distance instead: the value
// the game would have, kept beside the real one (set at the same stores,
// stepped by the governor's rule once a second: +50 under 5000, or -50 down
// to 1801 under 20 fps).
//
// The "Maximum Visibility" GameShark code doesn't do this: it patches the
// RSP graphics microcode at 0x800F9522, which RT64 doesn't run.
#include <atomic>
#include <cstdint>

#include "recomp.h"

#include "btga_netplay.h"
#include "btga_config.h"

namespace {
    std::atomic<bool> fog_enabled{ true };
    std::atomic<int> distance_level{ 0 };

    constexpr int32_t kMaxDrawDistance = 45000;

    // The draw distance the game would have without the option (game thread
    // only), 0 before the first scene.
    int32_t game_distance = 0;
}

void btga::set_fog_enabled(bool enabled) {
    fog_enabled.store(enabled);
}

void btga::set_draw_distance(int level) {
    distance_level.store(level);
}

int btga::draw_distance_level() {
    return btga::netplay::draw_distance(distance_level.load());
}

int btga::draw_distance_setting() {
    return distance_level.load();
}

// func_8007A250 before 0x8007A688: $v1 is the fog word about to be stored.
extern "C" void btga_fog_word(uint8_t* rdram, recomp_context* ctx) {
    if (!fog_enabled.load()) {
        ctx->r3 = 0; // gSPFogFactor(0, 0)
    }
}

// Before each store of the draw distance from $v0: the scene start
// (func_8009AE38, 0x8009AF4C) and the camera scripts (func_800D25E0,
// 0x800D289C and 0x800D28B0).
extern "C" void btga_scene_draw_distance(uint8_t* rdram, recomp_context* ctx) {
    int32_t distance = (int32_t)ctx->r2;
    game_distance = distance;
    switch (btga::draw_distance_level()) {
        case 1: distance *= 3; break; // Far
        case 2: distance *= 6; break; // Very Far
        case 3: distance = kMaxDrawDistance; break; // Map Edge
        default: return;
    }
    if (distance > kMaxDrawDistance) {
        distance = kMaxDrawDistance;
    }
    ctx->r2 = (gpr)distance;
}

// func_80099FE8 at L_8009A1B4: the governor's once-a-second step, $a0 the
// frames counted in the last second (the paths that skip it don't reach here).
// The same rule on the game's own draw distance.
extern "C" void btga_governor_draw_distance(uint8_t* rdram, recomp_context* ctx) {
    if (game_distance == 0) {
        return;
    }
    if ((int32_t)ctx->r4 < 20) {
        if (game_distance >= 0x709) {
            game_distance -= 50;
        }
    }
    else if (game_distance < 5000) {
        game_distance += 50;
    }
}

// Right after the turrets' range checks load the draw distance into $v0
// (func_800E3460, 0x800E36B0 and 0x800E387C): the game's own.
extern "C" void btga_turret_draw_distance(uint8_t* rdram, recomp_context* ctx) {
    if (btga::draw_distance_level() > 0 && game_distance > 0) {
        ctx->r2 = (gpr)game_distance;
    }
}
