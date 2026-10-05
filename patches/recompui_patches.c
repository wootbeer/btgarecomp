#include "patches.h"
#include "ui_funcs.h"

// func_8007AF84 (RecompiledFuncs -- BattleTanxGASyms/battletanxga.us.rev0.syms.toml,
// vram 0x8007af84) is an original, unpatched game function this patch calls
// by name; func_reference_syms_file lets N64Recomp resolve it as a genuine
// call into the rest of the recompiled game rather than a native stub.
extern void* func_8007AF84(s32 flag);

// Round 94 (src/game/screen_edge_scissor_fix.cpp): widens the game's off-by-one
// screen-edge scissors in its static viewport display lists.
extern void btga_fix_screen_edge_scissors(void);

// func_800A1858 (RecompiledFuncs/funcs_8.c) is this game's VI-swap-throttle
// routine: it's reached once per real VI tick (func_800A1290's VI-message
// dispatch -> func_800A140C -> here), and is the only place that ever calls
// osViSwapBuffer. That makes it the right, reliably-frequent hook point for
// recompui's per-frame UI callback pump (recomp_run_ui_callbacks) -- see
// STATUS.md round 61 for why a [[patches.hook]] mid-body splice at this same
// spot was tried first and reverted (it shares ctx with the surrounding
// function's own register-resident locals with no call-boundary register
// preservation, which caused a worse hang than the unresponsive UI it was
// meant to fix). A RECOMP_PATCH replaces the whole function at a genuine
// call boundary instead, so this is a full, faithful reimplementation of
// the original logic (verified against the disassembly) with the UI pump
// added unconditionally at the top so it still runs every VI tick even on
// the early-return paths below.
//
// Field layout at the offsets below isn't recovered as a real struct yet
// -- these are raw offsets into the same *state pointer
// the original code indexed with, matching the disassembly exactly:
//   0x1EC (u16): swap-due threshold, compared against 0x1F0.
//   0x1F0 (u16): pending-swap counter; cleared once a swap happens.
//   0x1F2 (u16): one-shot "unblank display" flag.
RECOMP_PATCH void func_800A1858(void* state) {
    btga_fix_screen_edge_scissors();
    recomp_run_ui_callbacks();

    u16* swap_pending = (u16*) ((u8*) state + 0x1F0);
    u16* swap_threshold = (u16*) ((u8*) state + 0x1EC);
    u16* unblank_pending = (u16*) ((u8*) state + 0x1F2);

    if (*swap_pending < *swap_threshold) {
        return;
    }

    void* frame_buffer = func_8007AF84(1);
    if (frame_buffer == 0) {
        return;
    }

    if (*unblank_pending != 0) {
        osViBlack(0);
        *unblank_pending = 0;
    }

    osViSwapBuffer(frame_buffer);
    *swap_pending = 0;
}
