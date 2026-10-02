// Workaround for a gap in N64Recomp/ultramodern's cooperative thread
// scheduler (see STATUS.md rounds 47-52). Several busy-wait polling loops
// in the original N64 code relied on a real hardware interrupt
// unconditionally preempting whatever was running so the thread clearing
// the poll flag could get a turn. Ultramodern only ever switches threads at
// specific recognized library calls, and only to a *strictly*
// higher-priority ready thread (check_running_queue,
// ultramodern/src/scheduling.cpp:24) -- real libultra's osYieldThread,
// which would round-robin to an equal-priority thread too, is unimplemented
// in this runtime fork (librecomp/src/ultra_translation.cpp:31-34, just
// asserts false). A bare polling loop with no OS call inside it never gives
// the scheduler a chance at all, and even a plain yield can't hand off to a
// same-or-lower-priority thread.
//
// Round 52: a priority drop alone isn't enough when the thread that needs
// to run is blocked in osRecvMesg on a hardware event (VI/AI/etc) rather
// than merely lower-priority-and-ready. Those events are delivered by a
// real (non-N64) host thread via ultramodern::enqueue_external_message,
// which just pushes onto a separate `external_messages` queue
// (ultramodern/src/mesgqueue.cpp:34-36) -- it is NOT written directly into
// the target's OSMesgQueue. That queue is only drained, and the message
// actually delivered (moving the blocked receiver into the ready queue),
// when some game thread calls wait_for_external_message/_timed. Debugger
// confirmed a thread (func_800988E8, the one that needs to run to clear
// the round-49/50 spin's flag) was stuck at the same osRecvMesg the entire
// time rounds 50-51 were tested -- because this function alone never drains
// that queue, so nothing was ever there for the priority drop to swap to.
//
// Fixed by draining external messages first (via yield_self_1ms,
// scheduling.cpp:45-48, which does exactly that plus its own strict-priority
// check_running_queue) and only then doing the priority-drop trick, so any
// thread that just became ready from a drained message also gets an actual
// handoff despite not being strictly higher priority. Both steps are
// necessary; either alone was proven insufficient by debugger observation.

#include <cstdint>

#include "ultramodern/ultramodern.hpp"

extern "C" int32_t osGetThreadPri(uint8_t* rdram, int32_t t);
extern "C" void osSetThreadPri(uint8_t* rdram, int32_t t, int32_t pri);

// Round 65 (part 2): not extern "C" and not declared in any header, but has
// ordinary external C++ linkage (ultramodern/src/mesgqueue.cpp:40) -- drains
// every currently-pending message in one pass, unlike yield_self_1ms's own
// wait_for_external_message_timed (mesgqueue.cpp:61-68), which pops at most
// one. See btga_yield_via_priority_drop below for why that distinction
// turned out to matter.
void dequeue_external_messages(uint8_t* rdram);

// Round 65 (part 2, STATUS.md): the round 65 fix (a fourth hook, at
// func_8009D3A4's loop-back label) unblocked func_800A1290 exactly once,
// then it went straight back to being permanently stuck at the same
// osRecvMesg. Root cause: external_messages is one shared FIFO fed by
// *every* source (VI, AI, SP, DP, Timer, SI -- mesgqueue.cpp's own
// enqueue_external_message_src callers), and yield_self_1ms only pops a
// single entry per call. With other sources producing faster than our
// once-per-loop-iteration drain rate, the VI message func_800A1290 is
// waiting for can get stuck arbitrarily far back in FIFO order behind a
// growing backlog of unrelated messages -- explaining a single lucky
// early delivery (before any backlog existed) followed by permanent
// starvation once one built up. dequeue_external_messages drains the
// entire queue in one pass instead of one entry, which can't starve this
// way regardless of relative production rates.
extern "C" void btga_yield_via_priority_drop(uint8_t* rdram) {
    dequeue_external_messages(rdram);

    int32_t saved_pri = osGetThreadPri(rdram, 0);
    osSetThreadPri(rdram, 0, 0);
    osSetThreadPri(rdram, 0, saved_pri);
}
