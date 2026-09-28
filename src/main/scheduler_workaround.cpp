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

extern "C" void yield_self_1ms(uint8_t* rdram);
extern "C" int32_t osGetThreadPri(uint8_t* rdram, int32_t t);
extern "C" void osSetThreadPri(uint8_t* rdram, int32_t t, int32_t pri);

extern "C" void btga_yield_via_priority_drop(uint8_t* rdram) {
    yield_self_1ms(rdram);

    int32_t saved_pri = osGetThreadPri(rdram, 0);
    osSetThreadPri(rdram, 0, 0);
    osSetThreadPri(rdram, 0, saved_pri);
}
