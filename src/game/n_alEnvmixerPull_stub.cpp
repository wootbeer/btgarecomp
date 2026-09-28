// Hand-written stand-in for n_alEnvmixerPull (see STATUS.md round 57).
// N64Recomp can't statically decompile the real function -- it contains a
// computed jump table whose size can't be determined ("Failed to determine
// size of jump table at 0x80077720 for instruction at 0x80100120"), so it's
// marked `ignored` in battletanxga.us.rev0.toml rather than `stubbed`: a
// plain empty stub left its return value ($v0/ctx->r2) holding whatever was
// there before the call, and callers (func_800FFA90) use that as an
// advanced output-buffer pointer, propagating a stale/garbage value two
// calls further up (func_80101320) into a write address and crashing.
//
// This is CPU-side audio envelope-mixing (the software counterpart to what
// the RSP audio microcode normally does) -- real mixing isn't implemented
// here, only enough to keep the pointer chain valid: pass the output
// buffer pointer ($a2/ctx->r6) through unchanged in $v0/ctx->r2, matching
// "wrote zero bytes" rather than advancing by a guessed (and possibly
// wrong) amount. Silence instead of real audio, but no crash. A real gap
// to revisit once real audio is being worked on.

#include "recomp.h"

extern "C" void n_alEnvmixerPull(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = ctx->r6;
}
