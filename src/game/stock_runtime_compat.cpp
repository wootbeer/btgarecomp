// Stock-runtime compatibility shims for functions N64Recomp's own built-in
// `ignored_funcs` list (N64Recomp/src/symbol_lists.cpp) expects some
// runtime to provide under a `_recomp` suffix, but that stock
// N64ModernRuntime doesn't implement (confirmed by grepping its whole
// source -- see PROGRESS.md item 7 and STATUS.md round 23 for how this was
// found and diagnosed). Mirrors the role `bdragoncore/battle-tanx-recomp`'s
// own `src/game/stock_runtime_compat.cpp` plays for that project -- see
// CMakeLists.txt's BTGA_FORKED_RUNTIME check, which is why this file lives
// at exactly this path.
//
// None of this has been exercised against a running game yet (no display/
// GPU in this environment, and the ROM can't be shipped with the repo) --
// every implementation below is a best-effort reading of real libultra
// behavior (either well-known/documented, or read directly from this ROM's
// own raw bytes at the relevant address, which are never recompiled since
// N64Recomp ignores them, but are still sitting right there in the ROM
// file to disassemble by hand). Flagged individually below where
// confidence is lower.

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "recomp.h"

// --- COP0 Status register read -------------------------------------------
// __osGetSR() just does `mfc0 $v0, C0_SR` on real hardware. Nothing in this
// runtime emulates COP0 state (see battletanxga.us.rev0.toml's cop0/mfc0
// instruction patches -- every other read/write of it in this ROM's own
// code is nopped for the same reason), so there's no real value to return.
// librecomp's own equivalent for the sibling function __osSetFpcCsr_recomp
// (lib/N64ModernRuntime/librecomp/src/ultra_translation.cpp) does the same
// thing for the same reason.
extern "C" void __osGetSR_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}

// --- libultra thread scheduler internals ---------------------------------
// Real prototypes (confirmed against this ROM's own call sites -- e.g.
// __osEnqueueThread and __osDequeueThread are each called with the queue
// head in $a0 and the thread pointer in $a1, matching libultra's public
// prototype ordering): `__osEnqueueThread(OSThread **queue, OSThread *t)`,
// `__osDequeueThread(OSThread **queue, OSThread *t)`,
// `OSThread *__osPopThread(OSThread **queue)`, `void __osDispatchThread(void)`.
// ultramodern already implements the same priority-queue-of-OSThread
// operations these need (ultramodern/include/ultramodern/ultramodern.hpp)
// for its own osStartThread/osCreateThread/etc., operating directly on the
// guest OSThread queue linked list in rdram -- not ultramodern's own
// private state -- so calling straight through to those should be
// behaviorally equivalent regardless of which queue (a message queue's
// blocked-thread list, the run queue, etc.) is being manipulated.
extern "C" void __osEnqueueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::thread_queue_insert(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4, (PTR(OSThread))(int32_t)ctx->r5);
}

extern "C" void __osDequeueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::thread_queue_remove(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4, (PTR(OSThread))(int32_t)ctx->r5);
}

extern "C" void __osPopThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = ultramodern::thread_queue_pop(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4);
}

// __osDispatchThread's real job (pick the next ready thread off the run
// queue and context-switch into it) is done very differently here than on
// real hardware: real hardware does it with a raw register/COP0 context
// restore, while ultramodern runs each N64 thread as a real host thread
// and switches via semaphore signaling. run_next_thread_and_wait is
// ultramodern's own equivalent entry point for "give up the CPU and let
// the scheduler pick who runs next."
extern "C" void __osDispatchThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::run_next_thread_and_wait(rdram);
}

// --- SI access serialization ----------------------------------------------
// __osSiCreateAccessQueue(void) takes no arguments on real hardware or in
// this ROM's own (never-recompiled, but still readable) bytes at its own
// address -- it creates a 1-slot OSMesgQueue used as a mutex around SI bus
// access, then immediately posts one message so the "mutex" starts
// available. Confirmed by reading this ROM's own raw bytes at its ignored
// __osSiCreateAccessQueue symbol: it calls this ROM's own (real,
// recompiled) osCreateMesgQueue(&0x803B04F8, &0x803B04F0, 1) followed by
// osSendMesg(&0x803B04F8, NULL, OS_MESG_NOBLOCK) -- textbook libultra.
// Replicated here against the same two addresses so anything in this ROM
// that later blocks on that queue (expecting the mutex-primed message)
// doesn't deadlock waiting for a message that would otherwise never come.
extern "C" void __osSiCreateAccessQueue_recomp(uint8_t* rdram, recomp_context* ctx) {
    constexpr int32_t si_access_queue = 0x803B04F8;
    constexpr int32_t si_access_queue_msgs = 0x803B04F0;
    osCreateMesgQueue(rdram, si_access_queue, si_access_queue_msgs, 1);
    osSendMesg(rdram, si_access_queue, 0, 0);
}

// --- Timer / VI internals --------------------------------------------------
// Both are called with no arguments from this ROM's VI manager thread code
// (vimgr_text_0188), matching their real void-void libultra prototypes.
// ultramodern manages both VI framebuffer swaps (via the renderer context,
// tied to the real osViSwapBuffer/osViGetCurrentFramebuffer exports) and
// timer expiry (ultramodern::init_timers, backing the real
// osSetTimer/osStopTimer exports) through its own separate mechanism
// already, so these ROM-internal calls -- which just update the N64-side
// bookkeeping structures those subsystems would otherwise touch on real
// hardware -- are made no-ops here rather than risk them fighting with
// ultramodern's own tracking of the same state. Unverified: whether any of
// this ROM's own code reads that N64-side bookkeeping directly afterward
// in a way a no-op would break -- flag this file first if VI timing or
// timer-driven gameplay logic misbehaves once this can actually be tested.
extern "C" void __osTimerInterrupt_recomp(uint8_t* rdram, recomp_context* ctx) {
}

extern "C" void __osViSwapContext_recomp(uint8_t* rdram, recomp_context* ctx) {
}
