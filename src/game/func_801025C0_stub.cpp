// Hand-written stand-in for func_801025C0 (see STATUS.md round 58).
//
// This is the entry point into a deep "build an RSP audio command list"
// call chain (func_801025C0 -> func_801028B0 -> func_80102900 ->
// [per-voice-type dispatch] -> func_80101320 -> n_alEnvmixerPull/
// _n_saveBuffer), each step advancing a running output-buffer pointer.
// Round 57 found the first crash in that chain (n_alEnvmixerPull's stub
// corrupting a pointer); this round found a second, one call deeper,
// inside _n_saveBuffer -- a fully real, undocumented DSP function, not a
// stub. Rather than keep patching individual internals of genuinely
// unimplemented audio DSP code one crash at a time, this short-circuits
// the whole chain at its entry using the real function's own existing
// "nothing to process" contract.
//
// Confirmed via disassembly that the real func_801025C0 already takes an
// output pointer (a1) to a caller-local "command count" and, when there's
// nothing to submit, just writes 0 there and returns; its caller
// (RecompiledFuncs funcs_18.c, around the 0x800FF788 call site) reads
// that count back afterward and skips submitting anything further when
// it's 0. Forcing that same path unconditionally reuses the game's own
// real no-op behavior instead of guessing at replacement logic -- no
// audio commands built this frame, but no crash. A real gap to revisit
// alongside n_alEnvmixerPull once real audio is being implemented.

#include "recomp.h"

extern "C" void func_801025C0(uint8_t* rdram, recomp_context* ctx) {
    MEM_W(0, ctx->r5) = 0;
}
