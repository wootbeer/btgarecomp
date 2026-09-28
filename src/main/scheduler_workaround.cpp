// Workaround for a gap in N64Recomp/ultramodern's cooperative thread
// scheduler (see STATUS.md rounds 47-51). Several busy-wait polling loops
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
// Temporarily dropping this thread's own priority to the minimum before a
// poll makes check_running_queue's strict `>` check trivially true for
// virtually any other ready thread, forcing a real handoff; the original
// priority is restored once control returns. Called from `[[patches.hook]]`
// sites in battletanxga.us.rev0.toml wherever a polling loop with no OS
// yield call is found.

#include <cstdint>

extern "C" int32_t osGetThreadPri(uint8_t* rdram, int32_t t);
extern "C" void osSetThreadPri(uint8_t* rdram, int32_t t, int32_t pri);

extern "C" void btga_yield_via_priority_drop(uint8_t* rdram) {
    int32_t saved_pri = osGetThreadPri(rdram, 0);
    osSetThreadPri(rdram, 0, 0);
    osSetThreadPri(rdram, 0, saved_pri);
}
