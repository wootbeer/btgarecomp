// Round 67 (STATUS.md): root cause of the permanent post-3-frames freeze.
//
// func_800A140C gates building a new gfx task on flag 0x801147E8 being
// non-zero (checked via func_80097660); func_800A15F0 clears it
// (func_800976AC) once both pending-task slots empty, and is confirmed to
// run exactly 3 times then never again -- consistent with the flag never
// reopening. The only other writer is func_80097844, reached exclusively
// through a 3-entry function-pointer table at 0x801147EC/F0/F4 (registered
// once at boot by func_80097560 -> func_800FBDBC into global 0x8012686C).
// Its *only* real caller, found by tracing that table (RecompiledFuncs/
// funcs_19.c:1440-1445, inside func_800FF698's audio-processing loop), is
// gated on an output "command count" written by func_801025C0 (an a1
// out-param): the call is skipped whenever that count is 0.
//
// func_801025C0 itself is a round-58 hand-written stub
// (src/game/func_801025C0_stub.cpp) that unconditionally writes 0 there,
// to short-circuit a real crash deeper in the unimplemented audio-DSP
// chain (n_alEnvmixerPull / _n_saveBuffer). That's the right call for
// avoiding the crash, but it has the side effect of permanently starving
// this call site -- confirmed via live diagnostics that func_80097844 is
// never invoked at all in a real run, so 0x801147E8 never reopens.
//
// func_80097844 isn't safe to call directly from here: its real argument
// is a pointer into its caller's own stack frame (dereferenced at offsets
// 0/4/8/0xC before any gating check runs), which we have no equivalent
// of. But the value it stores into 0x801147E8 on success is just
// `sp + 0x10` from *its own* frame -- a transient address never
// dereferenced by any reader (func_80097660 only returns it, and
// func_800A140C only tests it for non-zero) -- i.e. a non-null sentinel,
// not real data. Reopening the gate only needs *some* non-zero value
// there, not a call into func_80097844 itself.
//
// Round 67 (first attempt, REVERTED): called this unconditionally every
// real VI tick from func_800A1858's RECOMP_PATCH -- fixed the freeze, but
// crashed the game instead (access violation within a few ticks of
// starting). Forcing the gate open ~60x/sec let func_800A140C build and
// submit new SP tasks far faster than the SP could actually drain them,
// with none of the real backpressure the two-slot pending-task state
// (0x200/0x204, func_800A15F0) is built around -- corrupting that state.
//
// Fixed version: func_800976AC is the ONLY place that ever clears
// 0x801147E8 (confirmed: its one call site is func_800A15F0's own "both
// task slots empty" path, RecompiledFuncs/funcs_8.c:789), i.e. real SP
// task completion, not a fixed timer -- the correct pacing signal for
// this flag, matching whatever rate the SP actually drains tasks at.
// Hooked immediately after that clear (battletanxga.us.rev0.toml,
// func_800976AC before_vram = 0x800976D0, right after the `sw $zero,
// 0x47E8($at)` at 0x800976CC) so the gate reopens at exactly the same
// cadence the real game's own code already decided it was safe to clear
// it, rather than racing ahead of the SP.
#include <cstdint>

#include "recomp.h"

extern "C" void btga_reopen_gfx_gate(uint8_t* rdram, recomp_context* ctx) {
    uint8_t* gate_ptr = rdram + (0x801147E8u - 0x80000000u);
    *(int32_t*)gate_ptr = 1;
}
