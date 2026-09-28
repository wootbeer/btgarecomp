# Status

Last updated: 2026-09-28, in a Claude Code cloud session (a different sandbox
from the one that wrote the entries below).

## 2026-09-28, round 54: the scheduler deadlock is fully resolved -- new bug class, a "shared tail code" merged-function variant that needs a manual_funcs registration (and a required local N64Recomp source patch)

Round 53's second yield fix worked: the game got past the entire rounds
47-53 threading deadlock and crashed with a *new* error --
`Failed to find function at 0x8009F02C` (`librecomp/src/overlays.cpp:368`'s
`get_function()`, the runtime resolver for genuinely indirect calls) --
confirming real forward progress into new code.

This looked like the same merged-function-boundary bug as rounds 39-46 at
first, but it isn't. `0x8009F02C` sits mid-instruction-stream inside the
already-declared `func_8009EFD4` (it's literally the delay slot right after
`j 0x8009F048` at `0x8009F028`). Tried the normal fix (a `syms.toml` split)
and N64Recomp itself refused it outright: `func_8009EFD4`'s own internal
control flow branches to `0x8009F030` *and* jumps to `0x8009F048` -- two
different interior offsets of what the split would have carved out --
("branching outside of the function" / "Unhandled branch"). This is a
genuinely different bug shape: real shared/reused tail code, reachable both
as a normal fallthrough continuation of `func_8009EFD4` *and* as an
independent external entry point via an indirect call elsewhere in the ROM
-- not a case of the decompiler mis-drawing one function's boundary.

**The right tool turned out to already exist in N64Recomp**: `manual_funcs`,
a top-level config array (`N64Recomp/src/config.cpp`'s `get_manual_funcs`)
that registers an *additional*, independently-compiled function at a given
vram+size, coexisting with an already-declared overlapping function rather
than replacing it (`N64Recomp/src/main.cpp`'s `add_manual_functions`) --
exactly the "same bytes, reachable from two different entry addresses"
case here. Added it (`battletanxga.us.rev0.toml`'s `[input]` table):
```
manual_funcs = [
    { name = "func_8009F02C", section = ".resident_first_mb", vram = 0x8009f02c, size = 0x38 },
]
```
(size 0x38 chosen by disassembling forward from `0x8009F02C` to its own
clean `jr $ra` at `0x8009F05C` -- straight-line code, no branches leaving
that range, so it compiles as a fully self-contained unit on its own.)

**Caught by testing before telling the user, not by inspection**: adding
this to the TOML alone did nothing (`Function count` stayed at 1310,
identical with or without the entry) -- traced it to `add_manual_functions`
only being called from the *ELF-input* branch of `N64Recomp/src/main.cpp`
(guarded by `if (!config.elf_path.empty())`), never from the *ROM +
symbols-file* branch this project actually uses. Since N64RecompCLI is a
pure local codegen tool in this sandbox -- only the `RecompiledFuncs/*.c`
it generates gets committed/built, never the tool's own source -- patched
`N64Recomp/src/main.cpp` locally to also call `add_manual_functions` in the
ROM branch, rebuilt N64RecompCLI locally, regenerated, and confirmed
`Function count` went 1310 -> 1311 with `func_8009F02C` now present in
both `RecompiledFuncs/funcs_23.c` (a clean, self-contained translation
matching the hand-disassembled bytes exactly) and `recomp_overlays.inl`'s
lookup table. Full `ninja BattleTanxGARecompiled` build succeeded
end-to-end. Reverted the local `N64Recomp/src/main.cpp` patch afterward
(`RecompiledFuncs/` is gitignored -- nothing from this sandbox's own
regeneration gets committed either way).

**This local patch is required for the user's own regeneration too** --
their `N64Recomp.exe` is built from the same unmodified upstream submodule
source, so without this same source change, their build would silently
ignore `manual_funcs` exactly like this sandbox did on the first attempt,
and they'd hit the identical crash again. Gave them the equivalent
PowerShell script (same idempotent, marker-checked pattern used for the
RT64 submodule edits earlier in this project) to apply locally before
regenerating, since `lib/N64ModernRuntime` (and its nested `N64Recomp`) are
real git submodules whose edits can't be committed/pushed through this
project's normal workflow.

Not yet confirmed against a real run.

## 2026-09-28, round 53: found the real reason func_800A1290 never runs -- round 51's yield only covered one of func_800A1384's two loop-back paths

The round 52 (part 4) VI-dispatch diagnostic printed once
(`is_game_started=1`, `mq 0x80222930: validCount=0 msgCount=8`) and then
never printed again even after 15-20+ seconds of the game genuinely
running (confirmed debugger-free, confirmed CPU still pegged at ~10%
i.e. one core, so something was still actively spinning). That combination
-- still spinning, but our own diagnostic (inside
`btga_yield_via_priority_drop`) never firing again -- meant the active
code had stopped calling our yield helper entirely, despite still being in
`func_800A1384` (confirmed via a fresh debugger stack: still
`func_800A1384` -> `func_800BF80C` -> `func_8009D3A4` -> `func_8009EEA0`,
same as every round since 51).

Root cause: `func_800A1384` has a second loop-back path round 51 missed.
After the bounded 32-iteration inner copy (`L_800A139C`,
`RecompiledFuncs/funcs_8.c`) finishes one pass, a field check at vram
`0x800A13F8` (`sp+0x1F0 < 5`) can jump directly back to `L_800A139C`
*without* ever passing back through `L_800A1398` (the outer label round 51
put the only yield at) -- so once this specific branch starts getting
taken repeatedly, the function falls into a tight, entirely un-yielding
cycle that our round 51 fix never touches again. That's a "not unique to
this one spin site" case within a single already-patched function, not
a new function.

**Fix:** added a second `[[patches.hook]]` for `func_800A1384`, this time
at `before_vram = 0x800A139C` (the inner loop's own label) -- both loop-back
paths converge on this exact address, so there's no way to target only the
bypass path; the yield now also fires on every 16-byte inner-copy
iteration (up to 32x per outer pass) rather than just once. That's a real
per-pass slowdown on this one copy loop, but negligible next to hanging
indefinitely, and it stops firing at all once whatever this loop polls for
is finally satisfied. Verified: regenerated via the local `N64RecompCLI`,
confirmed both hooks land correctly at their respective labels
(`funcs_8.c:219-226`), and a full `ninja BattleTanxGARecompiled` build
succeeded end-to-end. Not yet confirmed against a real run.

## 2026-09-28, round 52 (part 4): the corrected diagnostic shows the queue is properly created; traced the full delivery chain and added a live VI-dispatch diagnostic

The corrected (sign-extension-fixed) diagnostic printed `msgCount=2,
msg=0x80217048` for mq `0x80217030` -- exactly matching `func_800985A0`'s
`osCreateMesgQueue(0x80217030, 0x80217048, count=2)` call
(`RecompiledFuncs/funcs_5.c:7717-7733`). So the queue is genuinely fine;
it's just never receiving anything. Traced the actual sender by searching
every `osSendMesg`/`osJamMesg` and `osSetEventMesg`/`osViSetEvent` call
site in the whole recompiled codebase (grepping for the literal offset
`0X7030` across all files, not just the known functions) rather than
assuming: found `func_80098AFC` (`funcs_5.c:8613+`, right next to
`func_80098B2C`, the flag-setter round 47 already identified) is the only
place that calls `osSendMesg(0x80217030, ...)` anywhere in the ROM. Its
only caller is `func_800A140C` (`RecompiledFuncs/funcs_8.c:300+`), whose
only caller is `func_800A1290` -- the audio dispatcher thread that has
shown up "correctly idle, blocked in osRecvMesg" in *every* debugger dump
since round 47. Reading `func_800A1290` in full (`funcs_8.c:18+`) shows
it's a message dispatcher: it blocks on `osRecvMesg` for mq `0x80222930`,
then jumps through a 5-entry table keyed on `(msg - 0x29A)`; case 0
(msg == `0x29A`, the VI event value registered via `func_800A1150`'s
`osViSetEvent` call, round 48) calls exactly `func_800A140C` ->
`func_80098AFC` -> sends to `0x80217030`.

So the full chain is: ultramodern's VI thread enqueues an external message
(`0x29A`) for mq `0x80222930` -> `func_800A1290` receives it and dispatches
-> `func_800A140C` -> `func_80098AFC` -> `func_800988E8` finally unblocks.
Round 52's drain-then-priority-drop fix should make this flow end-to-end,
but `func_800A1290` still hasn't moved in any dump taken since. Rather than
guess further, added a throttled (~1/sec) live diagnostic directly in
`src/main/scheduler_workaround.cpp` (a real C++ file with direct access to
`ultramodern::is_game_started()` and raw `rdram`, no sign-extension pitfall
this time -- used plain `vram - 0x80000000` unsigned arithmetic instead of
replicating `MEM_W`'s macro by hand) that prints `is_game_started()` plus
mq `0x80222930`'s live `validCount`/`msgCount` on every call to
`btga_yield_via_priority_drop`, throttled so it doesn't flood. This will
show empirically whether the VI thread is even sending anything yet, or
whether messages are arriving but something else is preventing
`func_800A1290` specifically from ever being scheduled to consume them.
Verified: full `ninja BattleTanxGARecompiled` build succeeded end-to-end.
Not yet confirmed against a real run.

## 2026-09-28, round 52 (part 3): the first diagnostic read was garbage -- fixed a sign-extension bug in my own patch, and traced the real creator function

The round 52 part 2 diagnostic printed all-zero fields for the queue at
`0x80217030`, which looked like confirmation it was never initialized --
but before trusting that, traced its actual creator to rule out a race:
`func_800985A0` (`RecompiledFuncs/funcs_5.c:7683+`) calls
`osCreateMesgQueue(0x80217010, ...)`, then `osCreateMesgQueue(0x80217030,
...)` (exactly this queue), then `osCreateThread`+`osStartThread` targeting
entry point `0x800988E8` -- i.e. `func_800988E8` itself. Its sole caller,
`func_8009D270` (`RecompiledFuncs/funcs_6.c:12551`), calls it unconditionally
and synchronously, strictly *before* the already-confirmed-working
`func_800A1150` call later in the same straight-line function -- so by
construction the queue must already be initialized before this thread's
own recv could ever run. That contradiction meant the diagnostic itself was
suspect, not the queue.

Found the bug: `MEM_W`'s address math (`recomp.h`) requires its address
operand to be a *sign-extended* 64-bit KSEG0 address (upper 32 bits all 1s) --
every real call site gets this by assigning through the `S32()` macro into a
64-bit `gpr`, which sign-extends automatically in C. The diagnostic instead
passed the bare literal `0x80217030` directly, which C types as a positive
32-bit `unsigned int` that never gets sign-extended, so `MEM_W`'s internal
`- 0xFFFFFFFF80000000` subtraction landed roughly 4GB off from the real
queue and silently read unrelated (zeroed) memory. The "never initialized"
finding was an artifact of my own patch, not a real result.

**Fix:** route the address constant through `S32()` into a `gpr` local
first (`battletanxga.us.rev0.toml`, same hook site), exactly matching how
the generated code builds every other address. Verified: regenerated,
confirmed the fixed version lands correctly
(`RecompiledFuncs/funcs_5.c:8244-8264`), passed `clang -fsyntax-only`
clean, and a full `ninja BattleTanxGARecompiled` build succeeded end-to-end.
Still purely diagnostic -- waiting on the corrected printout from a real
run before drawing any conclusion about why this thread is actually stuck.

## 2026-09-28, round 52 (part 2): traced func_800988E8's stuck queue to a missing osCreateViManager implementation; added a one-shot diagnostic print to confirm before writing a fix

After the drain-then-priority-drop fix (round 52 part 1), the game still
hangs -- confirmed via debugger this is genuine progress-then-restall, not
a no-op: the active thread now visibly cycles between `func_80098B40` and
`func_800A1384` (both caught mid-`btga_yield_via_priority_drop`, not bare
spinning), but `func_800988E8` -- the thread that needs to run to actually
clear things -- remains frozen at its very first `osRecvMesg` (mq vram
`0x80217030`), completely unmoved across all of rounds 47-52.

Traced this queue's likely registration path by reading `func_800A1150`
(`RecompiledFuncs/funcs_7.c:11098+`, the function round 48 already
identified as doing all of this thread's startup registration) in full:
it creates an `OSMesgQueue` at vram `0x80222930`, then calls
`osCreateViManager(0xFE)` -- and `osCreateViManager_recomp`
(`librecomp/src/vi.cpp:13-15`) is a complete no-op, same as
`osCreatePiManager_recomp` (`pi.cpp:60-62`). Real libultra's VI/PI Manager
threads exist specifically to fan a single hardware event out to *multiple*
application-registered queues, since raw `osSetEventMesg`/`osViSetEvent`
only support one global consumer each (confirmed by reading
`ultramodern/src/events.cpp` in full: `vi_thread_func` only ever sends to
`events_context.vi`/`.ai`'s single registered queue, nothing else). If this
game relies on the Manager pattern to route ticks to `0x80217030` (a
separate, per-thread queue, distinct from `0x80222930`), it would never
receive anything now that the Manager is a stub -- which exactly matches
what's observed. (Ruled out PI DMA completion as a factor: `osPiStartDma`/
`osEPiStartDma`, `pi.cpp:312-344`, deliver directly to a queue passed
per-call via `do_dma`, entirely independent of the broken
`osSetEventMesg(OS_EVENT_PI, ...)` path, so that part is *not* broken.)

Rather than keep guessing from static analysis, added a one-shot diagnostic
(`battletanxga.us.rev0.toml`, `[[patches.hook]]` on `func_800988E8` at its
own entry, guarded by a `static` so it only ever prints once) that dumps
the real `OSMesgQueue` struct fields at `0x80217030` (`blocked_on_recv`,
`blocked_on_send`, `validCount`, `first`, `msgCount`, `msg`) to stdout the
first time this function runs -- before it hits the blocking recv. This
will confirm empirically whether the queue was ever initialized
(`osCreateMesgQueue`, i.e. `msgCount` nonzero) and whether anything has
ever been sent to it (`validCount` nonzero at any point), rather than
continuing to infer from source alone. Verified: regenerated via the local
`N64RecompCLI`, confirmed the patch landed at the very top of
`func_800988E8` (`RecompiledFuncs/funcs_5.c:8244-8263`, before the register
save even completes), and a full `ninja BattleTanxGARecompiled` build
succeeded end-to-end. Purely diagnostic -- no behavior change yet, pending
the printed values from a real run.

## 2026-09-28, round 52: found why round 50/51's fix still hangs -- it never drains the external-message queue that VI/AI event delivery depends on

Round 51's fix built and ran, but the game still hung (same `AppHangB1`).
Debugger dump this round: the previously-fixed thread is now back at
`func_80098B40`, but genuinely cycling through the yield each iteration
(caught mid-`osSetThreadPri` inside `btga_yield_via_priority_drop`, not a
bare unyielding spin) -- so the mechanism runs, but the flag it waits on
(vram `0x80229230`) still never clears. All other threads unchanged from
every prior round, and critically: `func_800988E8` (the thread rounds
47-48 identified as needing to run to clear that flag) is still parked at
the *exact same* `osRecvMesg` call (`RecompiledFuncs/funcs_5.c` line 8301)
it has been at since round 47 -- meaning it has made zero progress across
five straight rounds, regardless of what round 50/51 changed.

Root cause: read `ultramodern/src/mesgqueue.cpp` in full. `osSendMesg`
skips `do_send` (the function that actually writes into a target's
`OSMesgQueue` and moves a `blocked_on_recv` thread into the ready queue) for
any non-game thread and instead calls `enqueue_external_message`
(`mesgqueue.cpp:34-36`), which just pushes onto a completely separate
`external_messages` concurrent queue. Ultramodern's VI/AI event thread
(`events.cpp`) is exactly such a non-game thread. That external queue is
only ever drained -- and the message actually delivered to the real target
queue -- by a game thread calling `wait_for_external_message`/
`_timed` (`mesgqueue.cpp:53-68`), which is what round 49's original
`yield_self_1ms` did but round 50/51's `btga_yield_via_priority_drop`
*doesn't*: it only calls `osGetThreadPri`/`osSetThreadPri`, which merely
rescans the already-ready `running_queue` via `check_running_queue` --
it never touches `external_messages` at all. So `func_800988E8`'s VI/AI
message was very likely sitting in that queue the entire time, and nothing
in rounds 50-51's fix ever pulled it out to actually deliver it.

**Fix (`src/main/scheduler_workaround.cpp`):** `btga_yield_via_priority_drop`
now calls `yield_self_1ms` first (drains `external_messages`, delivering
any pending event message and moving its receiver into the ready queue,
plus its own strict-priority `check_running_queue`), and only then does the
priority-drop trick -- so a thread that just became ready from a drained
message also gets an actual handoff despite not being strictly
higher-priority than the poller. Both steps are necessary: round 49 proved
draining alone isn't enough (strict priority check), and rounds 50-51 proved
the priority drop alone isn't enough (nothing to drain the external queue).
No TOML/generated-code changes needed this round, just the shared helper.
Verified: full `ninja BattleTanxGARecompiled` rebuild succeeded end-to-end
with no errors. Not yet confirmed against a real run.

## 2026-09-28, round 51: round 50 confirmed working via debugger; found and fixed a second, structurally identical scheduler gap

Round 50's priority-drop fix genuinely resolved the `func_80098B40` spin --
confirmed via a fresh debugger dump: that thread's call stack no longer
shows `func_80098B40` at all. The same two threads remain correctly idle
(`func_800A1290` and `func_800977DC`/`func_800FF698`, both blocked in
`osRecvMesg` on their own queues, unchanged from before), one thread is
still normally parked after a self-directed `osSetThreadPri`
(`func_8009EE08`, same as every prior round), and `func_800988E8`'s thread
is still sitting at the exact same `osRecvMesg` (line 8301) it was at
before round 50 -- meaning it genuinely hasn't progressed yet, consistent
with it still waiting on a VI/AI message rather than on the flag round 50
fixed.

The game still hangs (same `AppHangB1`), but the *active* thread moved to a
new call site: `func_800A1384` (`RecompiledFuncs/funcs_8.c:206`), called via
`func_800BF80C` -> `func_8009D3A4` -> `func_8009EEA0` (same overall thread
as before). This is a different code shape from `func_80098B40`'s plain
`while (flag != 0) {}`, but the same underlying bug: it copies a 0x208-byte
struct from a fixed shared address (vram `0x80222930` -- the same "plain
data accessor" pointer round 46 identified next to the audio DMA callback
thread `func_800A1290`) into a stack buffer, then re-loops back to the copy
start if fields inside the just-copied data (offsets `0x1F4`/`0x1FC`)
haven't reached an expected value -- i.e. polling another thread's write via
a copy-and-recheck pattern instead of a plain `while`, with no OS call
inside it, so it never gives the scheduler a chance either. Exactly the
"not unique to this one spin site" risk round 48 flagged.

**Fix:** factored round 50's priority-drop logic out of the inline
`[[patches.hook]]` text into a real shared helper
(`src/main/scheduler_workaround.cpp`, `btga_yield_via_priority_drop`, plain
`extern "C"`) instead of duplicating it per site, since this is now used in
two places and will likely be needed again. Both `func_80098B40` and the
new `func_800A1384` site (`battletanxga.us.rev0.toml`, `before_vram =
0x800A1398` -- the outer loop's own re-entry label, so it runs every full
copy pass but not inside the bounded 32-iteration inner copy) now just
forward-declare and call it. Verified: regenerated via the local
`N64RecompCLI`, confirmed both call sites landed correctly in the generated
source, and a full `cmake -S . -B build && ninja BattleTanxGARecompiled`
succeeded end-to-end -- including linking the final executable, confirming
the new helper resolves correctly against `osGetThreadPri`/`osSetThreadPri`
across translation units. Not yet confirmed against a real run (still
pending the user's machine).

## 2026-09-28, round 50: round 49 confirmed insufficient via debugger -- real fix: drop the spinning thread's own priority to force the swap

Round 49's `yield_self_1ms` injection did not resolve the hang (user report:
"same sort of result as before"). Confirmed why by attaching the debugger to
`build-dbg\` (the Release `build\` exe shows no Call Stack symbols -- a
recurring gotcha in this session, always use `build-dbg\` for debugging) and
walking every Game N thread's call stack while hung:

- Three threads are correctly idle, blocked in `wait_for_resumed`/
  `Semaphore::wait()` after a normal `osRecvMesg` (`func_800A1290`,
  `func_800988E8` -- the same thread round 47/48 identified as the one that
  needs to run to clear the `0x80229230` flag, still blocked on its VI/AI
  message queue) or after a self-directed `osSetThreadPri` call inside
  `func_8009EE08` (which *did* correctly trigger a real swap-and-park --
  confirming `osSetThreadPri`'s priority-change-triggers-`check_running_queue`
  path genuinely works).
- One thread's stack has `func_80098B40` directly at the top with nothing
  above it (`func_8009EEA0` -> `func_8009D3A4` -> `func_8009D270` ->
  `func_80098B40`) -- proof it's still spinning: it already returned from
  the injected `yield_self_1ms()` call and is back in the busy-wait, exactly
  the failure mode round 49's entry flagged as a risk.

Root cause confirmed by reading the runtime directly: `check_running_queue`
(`ultramodern/src/scheduling.cpp:24`) only swaps when
`next_thread->priority > self->priority` (strictly greater). Real libultra's
`osYieldThread` -- which round-robins to an *equal*-or-higher-priority ready
thread, not just a strictly-higher one -- is entirely unimplemented in this
runtime fork: `librecomp/src/ultra_translation.cpp:31-34`'s
`osYieldThread_recomp` just `assert(false)`s with the real call commented
out, and `ultramodern::osYieldThread` (declared in `ultra64.h:272`) has no
definition anywhere in the tree. So `yield_self_1ms` alone can never hand
off to a same-or-lower-priority thread, which is exactly the situation here.

**Fix (`battletanxga.us.rev0.toml`, same `[[patches.hook]]` site,
`before_vram = 0x80098B40`):** replaced the `yield_self_1ms()` call with a
temporary self-priority drop using only public, already-proven-working
functions (`osGetThreadPri`/`osSetThreadPri`, both plain `extern` forward
declarations, no runtime/submodule edit needed): save the current priority,
set it to 0 (`osSetThreadPri(rdram, 0, 0)` -- `t_ == 0` means "self" per
`threads.cpp:309-311`), which triggers `check_running_queue` via the
priority-actually-changed path (`threads.cpp:314-322`) and makes the strict
`>` check trivially true for virtually any other ready thread, then restore
the saved priority once control returns. Verified: regenerated via the
locally-built `N64RecompCLI` in this sandbox, confirmed the patch text lands
correctly right after the loop's label (`RecompiledFuncs/funcs_6.c:7-14`,
wrapped in `{ }` same as round 49 for the label/declaration C rule), passed
a clean `clang -fsyntax-only`, and built the real `RecompiledFuncs` ninja
target end-to-end with zero errors (only pre-existing unrelated warnings).

**Not yet confirmed against a real run** (this sandbox has no GPU/display).
If this still doesn't resolve it, the next things to check: whether
`func_800988E8`'s thread is even in `running_queue` yet at all when
`func_80098B40`'s thread yields (if it hasn't been inserted there -- e.g.
still blocked on the VI/AI message itself rather than ready -- no priority
trick helps, and the real question becomes why the VI thread's message
hasn't reached it), and whether `thread_queue_insert`'s priority-based
ordering could still starve the now-lowered-priority spinning thread longer
than expected once it's the one waiting to be resumed.

## 2026-09-28, round 49: attempted fix for round 48's deadlock -- inject a scheduler-yield call into the spin loop via [[patches.hook]]

Round 48 identified two real paths forward; this is an attempt at the
scoped one (option 2), avoiding a shared-runtime change. Ultramodern
already has exactly the primitive needed:
`yield_self_1ms` (`ultramodern/src/scheduling.cpp:45-48`, `extern "C"`,
not exposed in a public header) waits briefly for an external message, then
calls `check_running_queue` to switch to a higher-priority ready thread if
one exists -- precisely the "give the scheduler a chance" operation the
bare `while (flag != 0) {}` in `func_80098B40` never does.

Added a `[[patches.hook]]` entry (`battletanxga.us.rev0.toml`) at
`func_80098B40`'s `before_vram = 0x80098B40` (the loop's own re-entry
label, so it runs every iteration including the first) that calls it:
```
{ extern void yield_self_1ms(uint8_t *rdram); yield_self_1ms(rdram); }
```
Wrapped in a compound statement because a label in C can't be directly
followed by a declaration pre-C23 (first attempt without the braces
compiled only via a clang extension, `-Wc23-extensions`; confirmed clean
under a real `-fsyntax-only` check once wrapped). Verified the patch
applies and the generated code is both syntactically correct and
positioned exactly where intended by regenerating with a locally-built
`N64RecompCLI` in this cloud sandbox (this session has the ROM staged
locally too, same as previous rounds) -- something previous rounds always
had to defer to the user's machine for.

**Not yet confirmed against a real run.** If `yield_self_1ms` only
switches when a strictly higher-priority thread is ready
(`check_running_queue`'s condition, `scheduling.cpp:24`), and the thread
that needs to clear the flag isn't higher-priority than this one, this
specific fix won't be enough on its own -- worth checking thread
priorities if this doesn't resolve it.

## 2026-09-28, round 48: root cause of round 47's hang found -- a real hardware-interrupt-dependent busy-wait is incompatible with ultramodern's purely-cooperative thread scheduler

Continued from round 47. Two more pieces confirmed the actual mechanism:

**The VI/AI event registration round 47 wondered about does happen.**
Traced `func_800A1150`'s only caller: it's called from inside
`func_8009D270` (`RecompiledFuncs/funcs_6.c:12616-12619`), on the exact
same thread (entry point `func_8009EEA0`) that later reaches the
`func_80098B40` spin, and *before* it gets there. `func_800A1150` itself
(`RecompiledFuncs/funcs_7.c:11098+`) calls `osSetEventMesg_recomp` three
times (message values `0x29B`/`0x29C`/`0x29E`) and `osViSetEvent_recomp`
once (message value `0x29A`) -- exactly the message range
`func_800988E8`'s dispatcher checks for. So VI/AI event registration is
not the blocker; it already happened.

**The real blocker: `func_80098B40` is a plain busy-wait with no yield,
and ultramodern's N64-thread scheduler has no preemption.** Checked
`ultramodern/src/timer.cpp` (the "Timer Thread" seen in every thread
dump): it only exists to service game-requested `osSetTimer`/`osStopTimer`
timers, not to time-slice between N64 threads. Cross-referencing every
`resume_thread_and_wait`/`run_next_thread_and_wait`/`wait_for_resumed`
call site confirms the cooperative model is entirely voluntary: an N64
thread only ever hands off control at specific recognized library calls
(`osRecvMesg`, `osSetThreadPri`, etc.). `func_80098B40` is a raw
`while (*(int32_t*)0x80229230 != 0) {}` loop (`RecompiledFuncs/funcs_6.c`)
-- it calls nothing recognized, so once its host thread becomes "the
active N64 thread," no other N64 thread (including whichever one is
supposed to write `0` to that address and let this one continue) can ever
become active again. This also cleanly explains the steady ~10% CPU
reported: one thread pegged at 100% on its own core on a multi-core
machine, not intermittent scheduling -- a genuine hard wait, not a slow
one.

On real N64 hardware this same busy-wait pattern works because the
SI/controller-read completion is delivered by an actual hardware
interrupt, which preempts whatever's running unconditionally, regardless
of whether the interrupted code "cooperates." Ultramodern's software
model has no equivalent for this: nothing here preempts a thread that
doesn't voluntarily yield. This is very likely not unique to this one spin
site -- any similar `while (mem_flag) {}` polling pattern elsewhere in the
recompiled code would hit the same wall.

**Not yet resolved; two real options, neither of which is a quick fix:**
1. Add genuine preemption to ultramodern's thread scheduler (e.g. a
   periodic forced-yield check), which is a runtime-level change affecting
   every N64Recomp project built on this fork, not something scoped to
   this project alone.
2. Find exactly which thread is supposed to write `0` to vram `0x80229230`
   (search for the second write site beyond `func_80098B2C`'s `sw $v0` at
   `funcs_5.c:8636` -- the clearing code around `funcs_5.c:8412`,
   vram `0x80098A00`, is inside `func_800988E8`, itself currently blocked
   on its own `osRecvMesg`) and understand precisely why *that* thread
   hasn't run since the lock was set -- if it turns out to be reachable
   from a thread that isn't itself downstream of the spin, a targeted
   `[[patches.hook]]` TOML patch inserting a yield check inside the spin
   loop's address range could resolve this one call site without touching
   the shared runtime, but confirming that requires more thread-dependency
   tracing than done so far.

## 2026-09-28, round 47: game now runs real multi-threaded N64 logic -- hangs waiting for VI/AI event registration that hasn't happened yet

With round 46's fix in, the missing-function crashes stopped entirely.
Clicking "Play" now runs the game far enough to spawn multiple real N64
`osCreateThread` threads (seen in the debugger as "Game 1"/"Game 2"/
"Game 3" x2/"Game 5" -- the `t->id`-based naming from `get_game_thread_name`
in `main.cpp`) and hang instead of crash, with the whole process sitting at
a steady ~10% CPU.

Diagnosed by attaching the debugger, Break All, and walking every non-pool
thread's call stack (same technique used throughout this session):

- Two threads are legitimately idle, correctly blocked in `osRecvMesg`
  inside `func_800A1290` (round 46's split -- confirmed to be the audio
  driver's `__CallBackDmaNew`-adjacent worker thread, its own entry point)
  and `func_800988E8` (round 45's split), each waiting on their own message
  queue. Nothing wrong with these on their own -- they're supposed to sit
  idle until something sends them a message.
- One thread (entry point `func_8009EEA0`, round 39's split) is the one
  actually consuming CPU: it's spinning in a tight busy-wait loop,
  `func_80098B40` (`RecompiledFuncs/funcs_6.c`), on a flag at vram
  `0x80229230` (`while (*(int32_t*)0x80229230 != 0) {}`). That flag is set
  by `func_80098B2C` (called from 4 sites across the recompiled game code)
  and is supposed to be cleared after `osContGetReadData` (`vram
  0x80103254`, a real libultra function, correctly identified by name in
  `syms.toml`) completes -- i.e. this is a mutex protecting a synchronous
  controller-read, not something that should ever spin for long.

Traced the likely root cause one level further: `func_800988E8`'s own main
loop (`RecompiledFuncs/funcs_5.c:8244+`) starts by blocking on *two*
sequential `osRecvMesg` calls (queues at vram `0x80217030` and
`0x80217010`) before it ever reaches the controller-read/lock-clear code
further down -- and this thread is the one currently sitting in the first
of those two `osRecvMesg` calls. Those two queues are almost certainly the
game's own VI (frame tick) and AI (audio) event queues. Ultramodern's VI
thread (`ultramodern/src/events.cpp` `vi_thread_func`, lines 236-255) only
sends VI/AI messages once `ultramodern::is_game_started()` is true (which
it is, confirmed via `recomp::start_game()`/`game_status`) *and* the
target `OSMesgQueue` has actually been registered via `osSetEventMesg`/
`osViSetEvent` (`events.cpp:149-177`, both correctly implemented as
native functions here) -- so if the game's own startup code hasn't yet
called those to register its VI/AI queues, this thread will wait forever,
exactly matching what's observed.

**Not yet resolved.** The open question is *why* that registration hasn't
happened -- almost certainly because whichever game thread is supposed to
call `osSetEventMesg`/`osViSetEvent` hasn't been scheduled yet in the
cooperative (single-thread-active-at-a-time) emulated threading model,
possibly entangled with the controller-read spinlock above. This is
qualitatively different from every fix in rounds 39-46: those were
concrete, one-shot infrastructure/decompilation bugs found by following a
crash address to its exact cause; this needs mapping out this specific
game's own multi-thread startup order (which thread runs first, what each
one is blocked on, and why the registration thread either hasn't run or
silently failed) -- open-ended reverse engineering, not a quick fix.

Next steps for whoever picks this up:
1. Find where in the recompiled code `osSetEventMesg`/`osViSetEvent` are
   actually called from (search `RecompiledFuncs/*.c` for
   `osSetEventMesg_recomp`/`osViSetEvent_recomp` call sites), and trace
   backwards to find which thread/function is supposed to reach that call
   and why it hasn't yet.
2. Consider whether the controller-read spinlock (`func_80098B2C`/
   `func_80098B40`) is itself blocking that registration (e.g. if the
   registering code is gated behind the same lock, or scheduled after it).
3. Worth checking `ultramodern::run_next_thread_and_wait`/the cooperative
   scheduler's thread-priority ordering (several `osSetThreadPri` calls
   were seen in these same call stacks) in case a priority-ordering bug is
   preventing the right thread from ever getting scheduled.

## 2026-09-28, round 46: sixth confirmed merged-function boundary

Same bug class as rounds 39/41/42/44/45, found via the next runtime crash
address (`0x800A1290`) after round 45's fix. `__CallBackDmaNew` (declared
size `0xcc`) is really two functions: itself, a tiny 4-instruction
trampoline (`lui/addiu/jr/nop`, real size `0x10`) that returns some other
constant data pointer (`0x80222930`, not code -- just a plain accessor,
not a function-pointer-table entry like some of the earlier trampolines
found), and a separate jump-table dispatcher function right after it at
`0x800A1290` (real size `0xbc`, named `func_800A1290` -- no known original
symbol) ending exactly at the existing `func_800A134C` boundary.
`0x10 + 0xbc = 0xcc`, matching the original total exactly.

## 2026-09-28, round 45: fifth confirmed merged-function boundary

Same bug class as rounds 39/41/42/44, found via the next runtime crash
address (`0x800988E8`) after round 44's fix. `func_800985A0` (declared size
`0x55c`) disassembles to two complete functions back to back --
`func_800985A0` (`0x348`) and `func_800988E8` (`0x214`, the missing
address) -- ending exactly at the original declared boundary.
`0x348 + 0x214 = 0x55c`, matching the original total exactly.

## 2026-09-28, round 44: fourth confirmed merged-function boundary

Same bug class as rounds 39/41/42, found via the next runtime crash address
(`0x80097794`) after round 43's entrypoint sign-extension fix got past
`do_rom_read` and into real gameplay code. `func_800976AC` (declared size
`0x260`) disassembles to four complete functions back to back --
`func_800976AC` (`0xe8`), `func_80097794` (`0x48`, the missing address),
`func_800977DC` (`0x68`), and `func_80097844` (`0xc8`) -- ending exactly at
the original declared boundary with no leftover padding.
`0xe8+0x48+0x68+0xc8 = 0x260`, matching the original total exactly.

## 2026-09-28, round 43: entrypoint_address wasn't sign-extended -- crashed on the very first RDRAM write in do_rom_read(), immediately after the game actually started

With rounds 39-42 clearing every startup/render/lookup-table bug, clicking
"Play" after loading the ROM finally reached real game-boot code -- and hit
a new crash immediately: access violation inside `recomp::do_rom_read`
(`librecomp/src/pi.cpp:72`, `MEM_B(i, ram_address) = *rom_addr;`), on the
very first loop iteration (`i=0`). This call happens inside `init()`
(`recomp.cpp:494-502`), which runs once per game boot, well before
`recomp_entrypoint` (the actual recompiled game code) is ever reached --
so this is a distinct code path from anything exercised so far, not a
regression in previously-working code.

Locals at the crash: `ram_address` (the entrypoint address passed through
from `GameEntry::entrypoint_address`) showed as decimal `2147946496`, i.e.
hex `0x80071000` -- correct in *value*, but zero-extended
(`0x0000000080071000` as the actual 64-bit `gpr` bit pattern) rather than
sign-extended (`0xFFFFFFFF80071000`). `rom_addr` and `rdram` both looked
individually valid (non-null, plausible contents), which is what pointed
at the address *computation* rather than either raw pointer.

Root cause: `MEM_B`/`MEM_W`/`MEM_H` (`N64Recomp/include/recomp.h:95-108`)
compute `rdram + (((reg + offset) ^ N) - 0xFFFFFFFF80000000)` -- that
constant is the KSEG0 base (`0x80000000`) in its *sign-extended* 64-bit
form, matching real MIPS64: a 32-bit value loaded into a 64-bit register is
always sign-extended, so every register value these macros are normally
fed with with is already in `0xFFFFFFFF80xxxxxx` form. `entrypoint_address`
is declared `gpr` (`librecomp/include/librecomp/game.hpp:34`, a `uint64_t`
typedef) but this project's `main.cpp` set it from a plain
`0x80071000` literal, which the compiler zero-extends on implicit
conversion to `uint64_t` (`0x0000000080071000`), not sign-extends. Feeding
that zero-extended form into the macro's subtraction computes an offset
~4GB too large (`0x0000000080071000 - 0xFFFFFFFF80000000` wraps to
`0x0000000100071003` after the `^3` and subtraction, not the intended
`0x71000`), landing far outside the actual RDRAM allocation and segfaulting
on the very first byte write.

N64Recomp's own generator already produces the fix for this, just never
used: `RecompiledFuncs/lookup.cpp`'s auto-generated
`get_entrypoint_address()` returns `(gpr)(int32_t)0x80071000u` -- the cast
through `int32_t` (a signed type) before converting to `gpr` forces sign
extension. Confirmed this is exactly BanjoRecomp's own pattern too
(`src/main/main.cpp`: `.entrypoint_address = get_entrypoint_address()`).
This project's `main.cpp` never called that generated function at all,
just hardcoded the raw literal.

Fixed with the same cast applied inline in `main.cpp`'s
`supported_games` entry (`.entrypoint_address = (gpr)(int32_t)0x80071000u`)
rather than depending on the generated `get_entrypoint_address()` directly,
since that function only exists once `RecompiledFuncs/` has real content --
depending on it directly would break the project's existing
"builds fine without the ROM yet" placeholder path (same reasoning as the
`BTGA_HAS_RECOMPILED_FUNCS` guard added in round 40 for
`register_overlays.cpp`).

**Not yet confirmed against a real run.**

## 2026-09-28, round 42: third confirmed merged-function boundary

Same bug class as rounds 39/41, found via the next runtime crash address
(`0x800FF698`) after round 41's fix. `func_800FF560` (declared size
`0x2c0`) disassembles to two complete functions back to back --
`func_800FF560` (real size `0x138`) and `func_800FF698` (real size
`0x17c`) -- followed by `0xc` bytes of zero-word padding already correctly
excluded by the original boundary (the next declared function,
`func_800FF820`, already started at the right place). `0x138 + 0x17c + 0xc
= 0x2c0`, matching the original total exactly.

## 2026-09-28, round 41: second confirmed merged-function boundary, found once register_overlays() actually started working -- and a systematic scan attempt that didn't pan out

Round 40's `register_overlays()` fix worked -- confirmed by a completely
different crash address (`0x800FF1A4`, not `0x8009EE08`) on the very next
run, meaning the lookup table is genuinely live now. Same bug class as
round 39: `func_800FF0D4` (declared size `0x148`) disassembles to three
functions back to back -- `func_800FF0D4` (real size `0xd0`), a tiny
3-instruction trampoline at `func_800FF1A4` (size `0xc`, just
`lui $v0, HI / jr $ra / addiu $v0, $v0, LO` -- computes a constant address
into `$v0`, exactly the shape of a jump-table/function-pointer-table
entry, consistent with being reached only by an indirect call) and
`func_800FF1B0` (size `0x6c`), matching the original total exactly
(`0xd0 + 0xc + 0x6c = 0x148`).

Tried writing a systematic scanner (`scan_merged_funcs.py`, not checked in)
to find more of these across all 1304 declared functions at once rather
than one crash at a time: flag any function containing a `jr $ra` +
delay-slot pair followed by what looks like another function's own
prologue (`addiu $sp, $sp, -N`) before the declared end. Produced 313
candidates out of 1304 functions -- far too high a rate to be trustworthy;
almost certainly mostly false positives from inline jump-table data whose
raw words coincidentally decode as a matching instruction pattern. Both
confirmed splits so far were found by following the actual runtime crash
address as ground truth, not by static heuristic guessing, so abandoned
the batch approach and went back to fixing these reactively as the user
hits them -- slower per-instance but far more reliable, and the process
itself (disassemble the declared range, find the real `jr $ra`/prologue
boundaries, split the `syms.toml` entry, regenerate, rebuild) is now fast
and well-practiced.

## 2026-09-28, round 40: round 39's fix didn't actually take -- recomp::overlays::register_overlays() was never called at all, so the function lookup table was never wired up in the first place

Round 39's `syms.toml` split was correct (confirmed: regenerating produced
`func_8009EE08` with the right offset/size in
`RecompiledFuncs/recomp_overlays.inl`), but the exact same
`"Failed to find function at 0x8009EE08"` error persisted anyway, even
against a freshly-deleted-and-rebuilt exe. Root cause was one level up:
`recomp::overlays::register_overlays()` (`librecomp/src/overlays.cpp`) --
which populates the `func_map` that `get_function()` searches -- was never
called anywhere in this project at all. Confirmed by grepping the entire
repo for the call and finding only the declaration/definition in the
library itself.

N64Recomp generates `RecompiledFuncs/recomp_overlays.inl` (a `static
SectionTableEntry section_table[]` plus per-section function/reloc arrays)
on every run, but that file is meant to be `#include`d and wired up by the
*game project's own code* -- N64Recomp itself never does this, and this
project never had that glue. It mostly didn't matter: N64Recomp resolves
direct `jal` calls into direct C function calls at recompile time, so only
genuinely *indirect* calls (function pointers, jump tables) ever needed the
runtime `func_map` lookup at all -- and this is apparently the first (or
one of very few) indirect calls the game makes, which is why everything up
to this point ran fine despite the lookup table being permanently empty.

Confirmed via `BanjoRecomp` (github.com/BanjoRecomp/BanjoRecomp, same
toolchain) that this glue is expected to be hand-written per-project: its
`src/main/register_overlays.cpp` `#include`s its own generated
`recomp_overlays.inl` and calls `register_overlays()` with the resulting
`section_table`/`num_sections`/`overlay_sections_by_index` symbols, called
from `main()` before `recomp::start()`.

Added the same pattern here: `src/main/register_overlays.cpp` (declares
`void register_btga_overlays()`, `#include`s
`../../RecompiledFuncs/recomp_overlays.inl`, calls
`recomp::overlays::register_overlays(...)`), called from `main()` before
anything else. Since `recomp_overlays.inl` doesn't exist on a fresh clone
before the ROM/N64Recomp step (same as the rest of `RecompiledFuncs/`),
gated the real implementation behind a new `BTGA_HAS_RECOMPILED_FUNCS`
compile definition (`CMakeLists.txt`, set when
`RecompiledFuncs/recomp_overlays.inl` exists at configure time) with a
no-op fallback, so the existing "builds fine without the ROM yet" placeholder
path still works.

Also found and fixed, same debugging session: the "RT64 Idle" GPU
power-throttling-prevention thread
(`lib/rt64/src/hle/rt64_workload_queue.cpp:1179`, `WorkloadQueue::
idleThreadLoop`) crashed with an access violation deep inside
`amdxc64.dll`'s driver code specifically on the user's AMD Radeon RX 5700 XT
(RDNA1) -- not covered by RT64's existing AMD driver workaround table in
`rt64_application.cpp`, which only handles RDNA3/RDNA4-era cards. It's
explicitly an optional feature (its own comment: "not required if the
driver is configured to be at the Max Performance power state"), so
disabled it via a one-line local edit to `set_application_user_config`
(`RecompFrontend/recompui/src/renderer/rt64_render_context.cpp`,
`application->userConfig.idleWorkActive = false;`) rather than debugging
the driver crash itself. This is a submodule edit and can't be pushed
through this repo's normal git flow (`RecompFrontend` is a real git
submodule pointing at N64Recomp's own upstream repo) -- it needs to be
reapplied locally after any fresh clone/submodule reset until a better
place for it is found (a project-level patch-on-configure step, or
upstreaming a fix to RT64's own AMD workaround table).

**Not yet confirmed fixed against a real run** -- both changes need a
rebuild and test on the user's machine.

## 2026-09-28, round 39: first real function-boundary bug found and fixed -- func_8009ED9C was actually three separate functions merged into one

With rounds 37-38's fixes in, the game finally boots, opens a responsive
window, loads the user's ROM, and starts executing real recompiled N64 code
-- reaching, for the first time in this project's history, an honest
reverse-engineering-content crash instead of an infrastructure one:
`"Failed to find function at 0x8009EE08"` (`librecomp/src/overlays.cpp:364-372`,
`get_function`'s deliberate `assert(false); std::exit(EXIT_FAILURE);` path
for an indirect call/jump target with no matching entry in `func_map`).

`0x8009EE08` isn't a gap between declared functions -- it falls *inside*
the declared range of `func_8009ED9C` (`BattleTanxGASyms/battletanxga.us.rev0.syms.toml`,
vram `0x8009ed9c`, declared size `0x13c`, i.e. `0x8009ed9c`-`0x8009eed8`).
Disassembled the whole range by hand with `tools/mini_mips_disasm.py`
(rom offset = vram - `0x80070000`, per `tools/splat.yaml`'s documented
mapping) against the ROM staged locally in this sandbox
(`BattleTanx Global Assault (USA).z64`, gitignored, never committed).

The disassembly shows three complete, independent functions back to back,
not one function with internal control flow:
- `0x8009ed9c`-`0x8009ee04`: standard prologue (`addiu $sp,$sp,-0x28` /
  `sw $ra,...`) through its own `jr $ra`/`nop` epilogue at `0x8009ee00`/
  `0x8009ee04`. Real size `0x6c`.
- `0x8009ee08`-`0x8009ee9c`: another standalone prologue immediately after,
  own epilogue at `0x8009ee98`/`0x8009ee9c`. Real size `0x98`. This is the
  one the game calls indirectly and N64Recomp couldn't resolve, since only
  `0x8009ed9c` was a declared function entry.
- `0x8009eea0`-`0x8009eed4`: third standalone prologue/epilogue pair. Real
  size `0x38`.
- `0x8009eed8`-`0x8009eee0`: 8 bytes of non-code (decodes as garbage/data,
  e.g. `.word 0x4D504149`), matching the existing `func_8009EEE0` entry's
  own start -- this padding was already correctly excluded by the original
  boundary, which is why the total (`0x6c + 0x98 + 0x38 = 0x13c`) matches
  the original declared size exactly. Nothing was gained or lost; the
  original pass just found the right *outer* boundary and missed the two
  real splits inside it.

This is a concrete instance of the "sizes are still mostly gap-derived...
rather than checked one-by-one" caveat `PROGRESS.md` item 3 already flagged
as an open risk. Fixed by splitting the one `syms.toml` entry into three
(`func_8009ED9C` size `0x6c`, `func_8009EE08` size `0x98`, `func_8009EEA0`
size `0x38`).

**Not yet regenerated/rebuilt or confirmed fixed** -- this requires
re-running `N64RecompCLI` (`BUILDING.md` step 4) to regenerate
`RecompiledFuncs/*.c` from the corrected symbols, a CMake reconfigure
(`file(GLOB ...)` is configure-time-only, per earlier rounds), and a
rebuild, all on the user's Windows machine. There are almost certainly more
of these elsewhere in the 1288-entry symbol table -- this was one found by
following the exact runtime crash address, not a systematic sweep.

## 2026-09-28, round 38: window opened but was permanently "(Not Responding)" -- update_gfx was reading input state, not pumping SDL/Win32 events

With round 37's crash fixed, the game window opened for the first time ever
but Windows immediately marked it "(Not Responding)" and it never rendered
anything, despite the process clearly still running (non-zero, moving CPU
usage; nothing hung at the OS level).

Root cause: `update_gfx` (`src/main/main.cpp`, the
`ultramodern::gfx_callbacks_t::update_gfx` callback, invoked every
iteration of `recomp::start`'s main loop) was calling
`recompinput::poll_inputs()`. That function
(`RecompFrontend/recompinput/src/input_state.cpp:34`) only reads
*already-buffered* SDL state (`SDL_GetKeyboardState`, controller state,
mouse deltas) -- it never calls `SDL_PollEvent`, so the window's Win32
message queue was never being serviced at all, which is exactly what makes
Windows mark a window unresponsive regardless of whether the app is
otherwise looping fine.

Found by comparing against `BanjoRecomp` (github.com/BanjoRecomp/BanjoRecomp,
same author as N64Recomp itself, same RecompFrontend/N64ModernRuntime/RT64
stack) at the user's suggestion -- its own `update_gfx` calls
`recompinput::handle_events()` instead, which does call `SDL_PollEvent` in
a loop (`RecompFrontend/recompinput/src/input_events.cpp:249-253`).
`poll_inputs()` is already correctly wired elsewhere as
`ultramodern::input::callbacks_t::poll_input` and doesn't need to also run
from `update_gfx`. Fixed by switching `update_gfx` to call
`handle_events()` instead, matching BanjoRecomp exactly. Confirmed fixed:
the window became interactive immediately after (mouse hover/highlight and
clicks started working, including opening a native file-picker dialog for
ROM selection).

## 2026-09-28, round 37: the actual root cause of the whole-startup crash -- missing recompui::config::finalize() call, found by diffing against BanjoRecomp

Round 36 ended with the theory that a clean `Release` rebuild had fixed the
"cannot use 'throw' with exceptions disabled" build failures; it hadn't
resolved the real symptom, which turned out to be a separate runtime crash
entirely, and took a long debugging session (checkpoint-instrumented
startup logging in `main.cpp`, a `std::set_terminate` handler to surface
otherwise-lost exception messages, and eventually live debugger sessions
under `devenv /debugexe` with `RelWithDebInfo`/`Debug` builds) to actually
pin down.

The exe reliably crashed inside `RT64::Application::setup()` ->
`UIState::create_menus()` -> `recompui::config::init_modal()`
(`RecompFrontend/recompui/src/config/ui_config.cpp:138-145`), which throws
`"Config modal has already been initialized."` if `config_modal` (a
file-scope static, legitimately assignable only once, from inside this
exact function) is already non-null. Breakpoint-and-count confirmed
`init_modal()` is entered exactly once before the crash -- `config_modal`
was already non-null on its *first* call, which is inconsistent with any
normal double-invocation theory. In `Release` builds this same underlying
issue surfaced as a raw, uncatchable `0xC0000409` fail-fast in
`ucrtbase.dll` instead of a catchable C++ exception (same root cause,
different manifestation depending on build-specific memory/heap layout,
which is what made this so hard to pin down purely from stack traces and
crash offsets -- the `ucrtbase.dll` offset that kept recurring across many
unrelated bugs today turned out to just be the CRT's generic internal
abort-family entry point, not a fingerprint of one specific issue).

Root cause, found by cloning and reading `BanjoRecomp`
(github.com/BanjoRecomp/BanjoRecomp) at the user's suggestion -- another
project on this exact same toolchain. Its `main()`
(`src/main/main.cpp:745`) always calls `banjo::init_config()`
(`src/game/config.cpp:248-277`) before `recomp::start()`, which creates
several config tabs and then calls `recompui::config::finalize()`. This
project's `main.cpp` never called `finalize()` (or created any config
tabs) at all. `finalize()`'s own doc comment
(`RecompFrontend/recompui/include/recompui/config.h:90,129-131`) says it
"loads the config from disk" and "must be called after all tabs have been
created" -- skipping it left the config system in a state `init_modal()`
didn't expect.

Fixed by adding the missing call in `src/main/main.cpp`, before
`recomp::start()`: `recompui::config::create_general_tab()`,
`create_graphics_tab()`, `create_controls_tab()`, `create_sound_tab()`,
`create_mods_tab()` (the library's own prefab tabs, no game-specific
options exist yet per `PROGRESS.md` item 8), then `finalize()`. Confirmed
fixed: the startup crash is gone and the game reaches `create_render_context`
successfully.

## 2026-09-28, round 36: round 35's placeholder was too empty -- comment-only RCSS parses as failure, not success, crashing the same unguarded dereference from the other side

The user hit a `RelWithDebInfo` build regression while chasing a symbolized
stack trace for round 35's fix (several files -- rabbitizer, rmlui_debugger,
N64Recomp/cgenerator.cpp -- failed with "cannot use 'throw' with exceptions
disabled" under that build type specifically; not investigated further,
since it's an unrelated build-type quirk, not the actual bug). Went back to
`Release`, which doesn't have this problem, and later did a full clean
`build/` wipe to rule out object-file inconsistency from having switched
build types mid-stream on the same build directory.

With that clean Release build, the crash after "Loaded font face
'LatoLatin'..." changed from round 35's `std::length_error` to a genuine
access violation (`0xC0000005`, reading address `0x8` -- a null-pointer-plus-
small-offset pattern). Root cause, one call further into the same code path:
`Rml::Factory::InstanceStyleSheetStream` (`.../RmlUi/Source/Core/Factory.cpp:572-580`)
returns `nullptr` when parsing fails, and `init_styling`
(`ui_context.cpp:249`) dereferences that return value directly --
`MergeStyleSheetContainer(*Rml::Factory::InstanceStyleSheetStream(...))` --
with no null check. Same missing-defensive-check bug class as round 35's
`resize(tellg())` issue, in the very next line of the same function.

Why parsing failed: round 35's placeholder `assets/recomp.rcss` was
comment-only, and RmlUi's `StyleSheetParser::Parse`
(`StyleSheetParser.cpp:758`) returns `!style_sheets.empty()` -- a stylesheet
that never contained an actual rule block (only a `/* ... */` comment)
produces an empty `style_sheets` list, which counts as a parse *failure*,
not an empty-but-valid success. A truly empty or comment-only RCSS file is
not a safe placeholder here, contrary to what round 35 assumed.

Fixed by adding one trivial real rule (`body {}`) to `assets/recomp.rcss`,
which is enough for the parser to close out a non-empty stylesheet and
return `true`, avoiding the null dereference. Not yet confirmed against an
actual run.

## 2026-09-28, round 35: same story, next asset -- missing recomp.rcss crashes with std::length_error via an unguarded resize(tellg())

Round 34's font fix got past the `std::runtime_error`, but immediately hit
a new, different unhandled exception -- `std::length_error` -- right after
`UIState`'s constructor finished (confirmed by the console output: the
crash now comes right after "Loaded font face 'LatoLatin'..."). Visual
Studio's debugger never actually produced a symbolized call stack for this
one despite multiple attempts at a `RelWithDebInfo` rebuild (kept resolving
to "Module was built without symbols" for `BattleTanxGARecompiled.exe`, and
even a build that visibly reconfigured with symbols didn't get devenv to
load them) -- not worth chasing further, since reading the source directly
from the last known-good point (right after `UIState`'s constructor) found
the real bug first anyway.

`create_menus()` (called immediately after `UIState` is constructed, per
`ui_state.cpp`) starts with
`recompui::init_styling(recompui::file::get_asset_path("recomp.rcss"))`.
`init_styling` (`lib/RecompFrontend/recompui/src/core/ui_context.cpp:238-241`):

```cpp
std::ifstream style_stream{rcss_file};
style_stream.seekg(0, std::ios::end);
style.resize(style_stream.tellg());
style_stream.seekg(0, std::ios::beg);
```

`assets/recomp.rcss` doesn't exist in this project either (same missing-
asset class as round 34's fonts). `std::ifstream` doesn't throw on a
missing file by default -- it just opens in a failed state. `seekg` on a
failed stream is a no-op, and `tellg()` on a failed stream returns
`streampos(-1)` per the standard. That `-1` gets passed straight into
`std::string::resize()`, which takes an unsigned `size_t` -- so `-1`
becomes `SIZE_MAX`, and `resize(SIZE_MAX)` throws exactly
`std::length_error`. This is a real, reproducible bug in recompui's own
`init_styling` (no existence check, no `std::ifstream::failbit` handling),
not anything specific to this project or to Windows -- it would hit any
recompui-based project that doesn't ship this exact file. Not patching the
vendored submodule for it, consistent with this project's existing
practice (e.g. the cstdint/RmlUi PCH fix in `CMakeLists.txt` was done from
this project's side rather than editing the submodule) -- the missing
input is what's actually ours to fix.

Fixed the same way as round 34: supplied the missing asset rather than
patching the library. Added `assets/recomp.rcss` as an empty placeholder
(with a comment explaining why it exists and pointing back here) -- an
empty stylesheet is valid RCSS and merges cleanly with recompui's own base
styling via `MergeStyleSheetContainer`, so this doesn't silently break
anything, it just means this game has no styling on top of recompui's
defaults yet (unsurprising, since no UI assets have been extracted from
the ROM at all).

Not yet confirmed against an actual run. If this clears the crash, the
next real signal is still the same one round 34 was waiting on: does an
actual launcher menu render and respond to input.

## 2026-09-28, round 34: found the black-window crash -- the long-flagged missing-font gap, hit for the first time now that a real display exists

Got a real crash location via Visual Studio's debugger (`devenv.exe
/debugexe build\BattleTanxGARecompiled.exe`), since the exe links
`/SUBSYSTEM:WINDOWS` and produces no visible console output at all even
when run from a terminal: an unhandled `std::runtime_error`, thrown about a
second after launch, right after two "Failed to load font face ...
could not open file" log lines for `assets\NotoEmoji-Regular.ttf` and
`assets\promptfont/promptfont.ttf`.

Traced to `lib/RecompFrontend/recompui/src/base/ui_state.cpp:263-265`:
`UIState`'s constructor throws `std::runtime_error("No primary font was
registered with recompui::register_primary_font")` if
`recompui::register_primary_font(...)` was never called before
`recomp::start()`. It never was -- `src/main/main.cpp` has had this
exact line commented out with a TODO since round 23, and the file-level
comment has said so explicitly the whole time ("No font is registered...
The launcher menu will very likely be visually broken until then"). This
isn't a Windows bug, a regression, or anything round 25-33's toolchain
fixes touched -- `UIState`'s constructor runs as part of window/renderer
creation, which is the exact point every single earlier run in this
session (the display-less cloud sandbox) already failed at for unrelated
reasons (no GPU). This is genuinely the first time this line has ever
executed, on any platform, in this project's history -- round 33's black
window *was* the missing-font gap turning from "visually broken" (the old,
too-optimistic prediction) into "hard crash" the moment there was finally
a real window for it to matter in.

Fixed as a bootstrap placeholder, not final game UI work: copied
`LatoLatin-Regular.ttf` and `NotoEmoji-Regular.ttf` (both already vendored
under `lib/RecompFrontend/recompui/lib/RmlUi/Samples/assets/`, SIL Open
Font License 1.1) into this project's own `assets/` directory, alongside
their license text (`assets/FONT_LICENSE.txt`, required by OFL's
attribution terms), and uncommented/filled in the
`recompui::register_primary_font("LatoLatin-Regular.ttf", "Lato")` call
`main.cpp` already had a slot for. `assets/promptfont/promptfont.ttf` (a
controller-button icon font) is still missing and will still log its
"could not open file" warning -- that one was always tolerated gracefully
(only the *primary* font is a hard requirement) and isn't part of this fix.

Not yet confirmed against an actual run -- this needs only a rebuild (no
CMake reconfigure: `main.cpp` is already a tracked source, and asset files
aren't part of any glob), not a full pipeline redo. If this clears the
crash, the real next signal is whether a launcher menu actually renders
and is interactive -- the first opportunity in this project's history to
find out.

## 2026-09-28, round 33: first successful Windows build and run, ever -- BattleTanxGARecompiled.exe launches, shows a black window instead of a crash

After round 32's `N64RecompCLI` target fix and one more full reconfigure
(needed because the `build/` directory that finally linked successfully had
been configured *before* `RecompiledFuncs/` was regenerated with real
content -- CMake's `file(GLOB ...)` is configure-time-only, so the stale
empty-placeholder result was still in effect until a fresh `cmake -S . -B
build` re-ran the glob against the now-populated directory), the user got a
real `BattleTanxGARecompiled.exe`, ran it, and it launched: a window opened
instead of crashing or failing to start. This is the very first time this
project has run on an actual display anywhere -- everything through round
32 was validated only up to "fails cleanly at window/renderer creation" in
this session's own display-less cloud sandbox.

The window shows solid black rather than a launcher menu or the game.
Investigating next: whether this is the known RSP microcode gap
(PROGRESS.md item 6 -- `get_rsp_microcode` in `src/main/main.cpp` returns
`nullptr` unconditionally, so nothing this ROM's own display lists reference
is recognized, though RT64 ships generic F3D-family GBI fallback walkers
that may or may not cover this game without it) versus something more basic
failing in the recompui/RmlUi launcher UI itself (which should render
independently of any in-game RSP work). Asked the user whether the black
window persists (renderer running, nothing drawn) or exits/crashes shortly
after, and whether any console/diagnostic text is visible anywhere, before
narrowing further -- `BattleTanxGARecompiled.exe` links with
`/SUBSYSTEM:WINDOWS` (see the round-27-33 link commands), so this may be a
genuinely silent failure with no stderr visible at all regardless of cause,
which would itself need addressing before this is debuggable further from
the outside.

## 2026-09-28, round 32: real BUILDING.md bug, unrelated to Windows -- step 4 was building the wrong CMake target this whole time

With the x64 shell finally sorted (round 31), the Windows build got all the
way to the final `BattleTanxGARecompiled.exe` link and failed on exactly
one missing symbol: `recomp_entrypoint`. Traced it to `RecompiledFuncs/`
being empty on the user's machine -- this repo's own `CMakeLists.txt` has a
deliberate placeholder fallback for that (`file(GLOB ...)` finds nothing,
so it silently substitutes an empty `.c` file so the rest of the project
still configures on a fresh clone). That meant step 4 -- actually running
`N64Recomp.exe battletanxga.us.rev0.toml` to generate the recompiled game
code -- had never completed on this machine.

Tracing that down surfaced a real, standing bug in `BUILDING.md` itself,
present on **both** platforms, not a Windows-specific issue: step 4's
`cmake --build ... --target N64Recomp` builds the wrong CMake target.
`lib/N64ModernRuntime/N64Recomp/CMakeLists.txt` defines the actual CLI tool
as `add_executable(N64RecompCLI)` (line 117), which links against a
separate static library also confusingly named `N64Recomp`
(`add_library(N64Recomp ...)` elsewhere in that file -- also visible
directly in the main project's own final link command, as
`N64Recomp.lib` alongside `LiveRecomp.lib`/`SymbolLists.lib`). The CLI
executable gets its user-facing filename via
`set_target_properties(N64RecompCLI PROPERTIES OUTPUT_NAME N64Recomp)`
(line 129) -- so the *file* is correctly named `N64Recomp`/`N64Recomp.exe`,
but the *CMake target* you have to ask Ninja to build is `N64RecompCLI`.
Building `--target N64Recomp` instead silently builds only the static
library and stops -- no error, just the wrong, much smaller output, which
is exactly what the user's pasted log showed ("Linking CXX static library
N64Recomp.lib", 49/49, done -- no `.exe` anywhere).

This was wrong in `BUILDING.md` since it was first written (round 24-ish),
on both the Linux/macOS and Windows command blocks -- it likely went
unnoticed on Linux specifically in this session because earlier rounds'
Linux verification never re-ran step 4 from a truly fresh clone/build
directory after `BUILDING.md` was written; whatever `N64Recomp` binary was
used for those checks was almost certainly built earlier, before this
target-naming detail mattered, by whatever ad hoc command produced it at
the time. Fixed both command blocks in `BUILDING.md` to
`--target N64RecompCLI`, with a note explaining the executable's output
filename is still `N64Recomp.exe`/`N64Recomp` so the later "run it" step
doesn't need to change.

Not yet confirmed against an actual Windows build -- this fix should let
step 4 finally produce a real `N64Recomp.exe`, regenerate `RecompiledFuncs/`
for real, and get the final link past the `recomp_entrypoint` symbol. Next
real signal is whether `BattleTanxGARecompiled.exe` links successfully with
the real recompiled code in it.

## 2026-09-28, round 31: found it -- the dev shell itself was initialized for x86, not x64

Asked the user to check `$env:LIB` and confirm the base MSVC v143 build
tools component was installed (it was). The `LIB` answer was the whole
story:

```
...\VC\Tools\MSVC\14.44.35207\ATLMFC\lib\x86;...\VC\Tools\MSVC\14.44.35207\lib\x86;
...\Windows Kits\10\lib\10.0.26100.0\ucrt\x86;...\Windows Kits\10\lib\10.0.26100.0\um\x86
```

Every single entry ends in `\x86`, not `\x64`. The shell this whole session
had been troubleshooting in was never actually an x64 dev environment,
regardless of which shortcut or prompt name the user believed they'd
opened. That single fact explains the `mainCRTStartup` failure completely
and rules out every theory from rounds 27-30: `clang-cl` correctly resolved
to the x64-hosted binary (round 28's fix), correctly compiled and asked
`lld-link` for a `/machine:x64` link (visible in every failing log), and
`-MDd` correctly requested the debug CRT -- but `LIB` only listed the
**x86** copies of `msvcrtd.lib`/`vcruntimed.lib`/etc, so lld-link's
`/DEFAULTLIB:`-driven search for the x64 CRT import libraries found
nothing byte-compatible with a `/machine:x64` object and silently
contributed nothing, rather than erroring with an explicit architecture
mismatch. Every prior fix in this saga (rounds 25-29) was real and correct
for the bug it targeted, but none of them could have fixed this, because
none of them touched which dev-environment script had run.

No repo change for this one -- it's purely about which Start Menu shortcut
gets launched. Added a `BUILDING.md` sanity-check step (verify `$env:LIB`/
`%LIB%` contains `\x64` segments, not `\x86`, before doing anything else)
so a future reader hits this fast instead of chasing it through five
misleading compiler/linker errors the way this session did. If the user's
`build\` directory picks up a working x64 dev shell from here, the next
real signal is whether `BattleTanxGARecompiled` actually finishes linking.

## 2026-09-28, round 30: round 29's revert changed nothing -- this isn't a repo bug, it's the user's local toolchain/environment

Pulled round 29's revert, reconfigured. Identical failure, and this time
the compile command shows `--target=` is genuinely gone
(`clang-cl.exe  /nologo   /DWIN32 /D_WINDOWS  /Zi /Ob0 /Od /RTC1 -MDd ...`,
no target triple at all) -- yet `lld-link: error: <root>: undefined symbol:
mainCRTStartup` reproduces byte-for-byte identically. That rules out every
`CMakeLists.txt` change made in rounds 27-29: none of them were ever the
actual cause. This failure is happening entirely inside CMake's own
minimal one-.c-file "Check for working C compiler" self-test, which uses
none of this repository's own compile flags, include paths, or link
settings -- it's generated fresh by CMake itself every time, using only
`/DWIN32 /D_WINDOWS /Zi /Ob0 /Od /RTC1 -MDd` (CMake's fixed default ABI-
check flags) against a trivial `int main()`. There is nothing left in this
repo's build configuration that could be causing this.

Working theory, not yet confirmed: this is a local Visual Studio
installation/environment problem, not a code or CMakeLists.txt problem.
Two candidates flagged for the user to check directly (this session has no
Windows machine to check them on):
1. Whether `LIB` is actually populated in that shell (`$env:LIB` in
   PowerShell / `%LIB%` in cmd) -- if it's empty or missing the VC Tools
   MSVC lib directory specifically, the CRT import libraries that supply
   `mainCRTStartup` (`msvcrtd.lib`, `vcruntimed.lib`, etc.) wouldn't
   resolve even though they're normally pulled in automatically via a
   `/DEFAULTLIB:` directive clang-cl embeds for `-MDd`, rather than being
   named explicitly on the link line (the actually-named libs in the
   failing command -- kernel32.lib, user32.lib, etc. -- come from the
   Windows SDK, a separate component, and those aren't failing).
2. Whether the base "MSVC v143 - VS 2022 C++ x64/x86 build tools"
   component (not just "C++ Clang Compiler for Windows") is actually
   installed -- clang-cl doesn't ship its own copies of the CRT import
   libraries; it relies on the co-installed MSVC toolset's
   `VC\Tools\MSVC\<version>\lib\x64\` for those. If that base component
   didn't get installed alongside the Clang one, the referenced default
   libs may not exist on disk anywhere, causing exactly this failure
   pattern with no "cannot open file" diagnostic (since the reference
   itself may not even be getting embedded/considered, vs. embedded-but-
   unresolvable -- both would look similar from the outside without
   further isolation, e.g. a bare clang-cl+lld-link repro outside CMake).

## 2026-09-28, round 29: round 27's --target forcing was itself the bug -- reverted now that round 28's exact-path pin is the real fix

The user pulled round 28's fix (explicit full path to the x64-hosted
`clang-cl.exe`) and reconfigured. CMake's "Check for working C compiler"
step now correctly resolved and invoked
`.../VC/Tools/Llvm/x64/bin/clang-cl.exe` (confirmed in the log) -- and
still hit the *exact same* failure as round 28:

```
lld-link: error: <root>: undefined symbol: mainCRTStartup
```

This is the tell: round 27's `CMAKE_C_COMPILER_TARGET`/
`CMAKE_CXX_COMPILER_TARGET` forcing (`x86_64-pc-windows-msvc`) was never
actually necessary, and is the thing causing this failure. Before round 27
existed, this exact CMake self-test (and everything after it) passed
without any explicit `--target=` flag -- rounds 25/26's real failures were
never about this trivial test-compile step at all, only about which
physical clang-cl got invoked for the *project's own* source files later
in the build. Once round 28 pinned the exact x64-hosted binary by full
path, that binary's own natural default target is already
`x86_64-pc-windows-msvc` -- forcing the same value explicitly, redundantly,
via `CMAKE_C_COMPILER_TARGET` apparently changes clang-cl's internal
toolchain-selection path enough to break its implicit embedding of the CRT
default-library directive (the `/DEFAULTLIB:` COFF directive derived from
`-MD`/`-MDd`/etc that normally tells the linker which import library
supplies `mainCRTStartup`) -- reproduced identically on CMake's own
minimal one-file test program, with zero project-specific code involved,
so this isn't specific to anything in this repo's own sources.

Reverted round 27's `CMakeLists.txt` change entirely. The real, sufficient
fix is round 28's: pin `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER` to the exact
`Tools\Llvm\x64\bin\clang-cl.exe` path (already in `BUILDING.md`). No
`--target=` forcing needed once the right binary is the one actually being
invoked -- it already knows its own architecture.

Not yet confirmed against an actual Windows build -- same standing caveat
as rounds 25-28. If `mainCRTStartup` still comes up undefined on the next
attempt even without the reverted flag, the next thing to check is whether
`LIB` (the environment variable naming where the CRT/Windows SDK import
libraries live, normally set by the VS dev shell launcher itself) is
actually populated in that shell -- `echo $env:LIB` in PowerShell or
`echo %LIB%` in cmd should show several semicolon-separated paths; if it's
empty, the dev environment wasn't actually initialized for that window
despite its name/shortcut.

## 2026-09-28, round 28: round 27's --target fix wasn't enough on its own -- the wrong clang-cl binary needs to be avoided, not compensated for

After deleting `build/` and reconfiguring fresh (picking up round 27's
`CMAKE_C_COMPILER_TARGET`/`CMAKE_CXX_COMPILER_TARGET` forcing), CMake's own
"Check for working C compiler" step got further -- the test object file
compiled fine this time with `--target=x86_64-pc-windows-msvc` visibly in
the command line -- but then failed at **link**:

```
lld-link: error: <root>: undefined symbol: mainCRTStartup
```

The compiler CMake resolved for bare `-DCMAKE_C_COMPILER=clang-cl` was, once
again, `VC\Tools\Llvm\bin\clang-cl.exe` (the 32-bit-hosted copy), not
`VC\Tools\Llvm\x64\bin\clang-cl.exe`. Forcing the target triple (round 27)
was necessary but not sufficient: it fixed the actual code generation
(hence the successful compile), but apparently that 32-bit-hosted binary's
default-CRT-library selection (the `/DEFAULTLIB:` directive normally
embedded into the object file based on the `/MDd`/`/MD` flag, which is what
tells the linker which CRT startup object provides `mainCRTStartup`) either
isn't emitted correctly, or isn't the x64 variant, when that particular
binary is forced to cross-target x64 via `--target=`. Whatever the exact
mechanism, compensating for the wrong binary via flags is fragile; the real
fix is to not invoke that binary at all.

Changed course: instead of relying on PATH to resolve bare `clang-cl` (which
has now picked the wrong one on this exact machine at least twice, in two
different shell sessions, despite both allegedly being launched from an
"x64 Native Tools Command Prompt for VS 2022"), `BUILDING.md`'s Windows
section now has the user set `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER` to the
literal full path of `VC\Tools\Llvm\x64\bin\clang-cl.exe`, removing the
ambiguity entirely rather than trying to out-flag it. Left round 27's
`CMAKE_C_COMPILER_TARGET`/`CMAKE_CXX_COMPILER_TARGET` forcing in
`CMakeLists.txt` in place too, as a harmless (no-op when the right binary is
already used) second line of defense.

Not yet confirmed against an actual Windows build -- same standing caveat
as rounds 25-27. If explicitly pointing at the x64-hosted binary doesn't
clear this on the next attempt, the next thing to check is whether `LIB`
(the environment variable that tells `link.exe`/`lld-link.exe` where the
CRT/Windows SDK import libraries live) is actually set in that shell --
`vcvarsall.bat`/the Native Tools shortcut should set it, but if the user's
shortcut or script is stale or was edited, it might not be.

## 2026-09-28, round 27: round 26's fix worked too -- next failure was a silent 32-bit build, not a code bug

The user's next attempt got past `RecompiledFuncs` and reached `librecomp`
(`config_option.cpp`, `eep.cpp`, `config.cpp`), failing with:

```
recomp.h(62): error: use of undeclared identifier '_mul128'
recomp.h(66): error: use of undeclared identifier '_umul128'
mods.hpp(54): error: static assertion failed due to requirement 'sizeof(unsigned int) == 8'
    static_assert(sizeof(std::size_t) == 8);
```

Both are symptoms of one root cause, not two separate bugs: the build was
compiling for **32-bit x86, not 64-bit x64**. `_mul128`/`_umul128` are
128-bit-multiply intrinsics that only exist for x64 targets -- on x86 they
are genuinely undeclared, by design, not a missing include. `size_t` being
4 bytes only happens on a 32-bit target. Corroborating evidence: the
compiler path in this failing log was
`...\VC\Tools\Llvm\bin\clang-cl.exe` (no `\x64\`), whereas round 25/26's
working steps used `...\VC\Tools\Llvm\x64\bin\clang-cl.exe`. Visual Studio
ships both a 32-bit-hosted and a 64-bit-hosted clang-cl.exe under
`VC\Tools\Llvm\`; which one wins on PATH -- and what target architecture it
defaults to without an explicit `--target=`/`-m64` -- isn't fully pinned
down by running in an "x64 Native Tools Command Prompt" alone, at least not
reliably enough to trust across a whole multi-hundred-target build.

Fixed at the CMake level rather than by telling the user to pass yet more
manual flags: `CMakeLists.txt` now sets `CMAKE_C_COMPILER_TARGET` /
`CMAKE_CXX_COMPILER_TARGET` to `x86_64-pc-windows-msvc` on `WIN32`, **before**
the `project()` call. This has to be before `project()` -- that's where
CMake runs its own compiler ABI detection (`CMAKE_SIZEOF_VOID_P`), and this
repo's own `CMakeLists.txt` already branches on
`CMAKE_SIZEOF_VOID_P EQUAL 8` in a couple of places (the `-march=nehalem
-fms-extensions` block, and the Linux/Vulkan compile-definitions block) --
setting the target only via `CMAKE_CXX_FLAGS` after `project()` would have
fixed the actual compiled output's architecture while leaving CMake's own
internal ABI bookkeeping (and anything gated on it) still thinking it's a
32-bit build. Explicitly forcing the target this way is a no-op when the
correct x64 clang-cl was already being used, so it's safe either way.

This is a CMake **cache** issue too, not just a source fix: the user's
existing `build/` directory has stale (wrong-architecture) ABI detection
results baked into its `CMakeCache.txt`, generated before this fix existed.
Told the user to delete `build/` entirely and reconfigure from scratch
rather than relying on ninja's automatic incremental reconfigure, since
that wouldn't redo compiler ABI detection on its own.

Not yet confirmed against an actual Windows build -- same standing caveat
as rounds 25-26.

## 2026-09-28, round 26: round 25's fix worked -- and surfaced the same clang-cl `-include` bug a second time, on `RecompiledFuncs`

After round 25's `/FI cstdint` fix, the user's next Windows build attempt got
past `rmlui_core`'s PCH entirely (confirming that fix) and reached a new,
different failure at `RecompiledFuncs`'s own placeholder source:

```
clang-cl: warning: unknown argument ignored in clang-cl: '-include'
clang-cl: error: cannot specify '/Fo...' when compiling multiple source files
```

Same root cause as round 25, different symptom. `CMakeLists.txt`'s
`target_compile_options(RecompiledFuncs ...)` force-includes
`include/btga_recomp_hooks.h` (declarations for `[[patches.hook]]` entries)
via bare GNU-style `-include <path>` passed as two separate command-line
tokens. clang-cl doesn't recognize that two-token form at all -- it emits a
non-fatal "unknown argument ignored" warning for the bare `-include` token,
then treats the now-orphaned path token (`.../btga_recomp_hooks.h`) as a
second **input source file** rather than part of an include flag, which
makes it a genuine second source file on the command line alongside
`recompiled_funcs_placeholder.c` -- and clang-cl refuses to combine an
explicit `/Fo<output>` with multiple inputs. Round 25's case looked
different (a missing-file error, not a multiple-inputs error) only because
that flag was a single SHELL-quoted token (`-include cstdint`) rather than
two separate tokens; the underlying incompatibility (clang-cl not
understanding GNU `-include` at all) is the same.

Fixed the same way as round 25: `/FI<path>` (single token, no space needed
since a full path has no spaces to worry about here) when `MSVC` is set,
keeping `-include <path>` on GCC/Clang. Also proactively applied the same
fix to the other, currently-dormant `-include` usage in `CMakeLists.txt`
(the `rsp_stock_compat.hpp` force-include on `rsp/battletanx_audio.cpp` and
`rsp/f3dex.cpp` -- guarded by `if(EXISTS .../rsp/battletanx_audio.cpp)`,
which doesn't exist yet per PROGRESS.md item 6, so it wasn't hit in this
build, but it's the identical pattern and would have hit the identical bug
the moment RSP microcode work starts).

Not yet confirmed against an actual Windows build -- same caveat as round
25, this session has no Windows machine, so this is reasoned from the exact
same evidence pattern (a working `/FI` flag for the PCH header sitting right
next to a broken `-include` flag in round 25's failing command) rather than
verified directly. Next Windows build attempt is the real test.

## 2026-09-28, round 25: first real Windows build attempt -- found and fixed a clang-cl/PCH bug in our own `-include cstdint` workaround

The user attempted the actual Windows build for the first time (this project
had only ever been built on Linux before this). Two Windows-specific bugs
found earlier by code review alone (missing `SDL_SysWMinfo`-based HWND path
in `src/main/main.cpp`, unconditional reference to the nonexistent
`icons/app.rc`) did NOT surface as build errors, i.e. those fixes held.

A new, real error did surface, at the very first C++ file to build:
`rmlui_core`'s CMake-generated `cmake_pch.cxx`, with clang-cl reporting
`error: no such file or directory: 'cstdint'`. Traced to our own
`CMakeLists.txt` fix (added earlier this project, for a *different*,
Linux-side problem: upstream RmlUi 6.0 uses `uint32_t`/`uint8_t` etc.
without including `<cstdint>`, which a current libstdc++ rejects under
Clang) -- `target_compile_options(... "-include cstdint")` on
`rmlui_core`/`rmlui_debugger`/`recompui`.

That GNU-style `-include <bare-name>` flag apparently doesn't reliably
resolve a standard-library header by name under clang-cl specifically while
it's *also* generating a precompiled header (`/Yc`) in the same invocation --
even though the same command's `/FI` flag (MSVC-native forced-include,
already used by CMake's own PCH machinery to force-include `cmake_pch.hxx`
in that exact command) presumably works fine, and even though plain
`#include <...>` resolution elsewhere in the same build (e.g. `lunasvg`'s
`svgelement.cpp`, which uses `std::unique_ptr`) was never a problem. The two
other `-include` usages in this repo's own `CMakeLists.txt` (the
`btga_recomp_hooks.h` force-include on `RecompiledFuncs`, and the
`rsp_stock_compat.hpp` force-include for stock RSP microcode source files)
were not touched -- both pass a full absolute path rather than a bare
standard-library header name, so they don't route through the same
name-lookup path and aren't expected to hit this.

Fix: made the force-include compiler-aware. When `MSVC` is true (CMake sets
this for clang-cl too, via its MSVC-compatible frontend variant, not just
real `cl.exe`), use `/FI cstdint` instead of `-include cstdint`; keep the
GNU spelling for GCC/Clang on Linux, where it was already working.

This was found and fixed from the user's pasted build log alone -- no
Windows machine available in this session either, so the fix is reasoned
from the evidence (the failing flag, and the working `/FI` flag right next
to it in the same failing command) rather than confirmed by a green
Windows build yet. If the next attempt gets further, that's the confirmation;
if `/FI cstdint` itself turns out not to work the same way, the fallback is
to drop precompiled headers for these three targets on Windows entirely
(`set(RMLUI_PRECOMPILED_HEADERS OFF)` before `add_subdirectory(RecompFrontend)`)
rather than fighting the force-include mechanism further.

## 2026-09-28, round 24: wrote the 12 stock-runtime compat shims -- BattleTanxGARecompiled links, launches, and fails exactly where this sandbox's missing GPU says it should

Wrote `src/game/stock_runtime_compat.cpp` (COP0 status read, the 4 thread-
scheduler internals, SI access-queue creation, timer/VI internals) and
`src/game/controller_pak.cpp` (the 7 Controller Pak filesystem internals),
filling every symbol round 23 found missing. `bdragoncore/battle-tanx-
recomp`'s own files of the same name/path are the reason these exact paths
were already anticipated in `CMakeLists.txt`'s `BTGA_FORKED_RUNTIME` check --
same reasoning as round 23, only the generic shape carries over, not any of
that project's own values or logic.

None of this could be derived from public documentation alone with
confidence, so each function's real argument registers were checked
directly against how *this ROM's own code* actually calls it (found via the
same rabbitizer-based disassembly used throughout rounds 21-22):

- `__osGetSR_recomp`: returns 0, matching the established pattern for every
  other COP0 access in this ROM (already nopped via instruction patches) --
  nothing anywhere emulates real COP0 state.
- `__osEnqueueThread`/`__osDequeueThread` confirmed to take `(queue, thread)`
  in `$a0`/`$a1` at their real call sites (e.g. `jal __osEnqueueThread` at
  `0x80110614` with `$a0 = lw 0x8($t6)`, `$a1 = move $a1, $t6`) --
  implemented as thin wrappers around `ultramodern::thread_queue_insert`/
  `_remove`/`_pop`, which already do the same guest-memory OSThread-queue
  manipulation for the exported `osStartThread`/etc. `__osDispatchThread`
  maps to `ultramodern::run_next_thread_and_wait` (real hardware does this
  with a raw register/COP0 context switch; ultramodern does it with host
  threads + semaphores instead, so this is a translation of intent, not a
  literal port).
- `__osSiCreateAccessQueue`: real behavior read directly from this ROM's
  own bytes at its address (never recompiled since N64Recomp ignores it,
  but the raw bytes are still sitting right there in the ROM to
  disassemble) -- it's textbook libultra: `osCreateMesgQueue(&0x803B04F8,
  &0x803B04F0, 1)` then `osSendMesg(&0x803B04F8, NULL, 0)`, both already-
  real (not stubbed) exported functions in this ROM's own trusted symbol
  table. Replicated verbatim against the same two addresses so anything
  that later blocks on that queue doesn't deadlock waiting for a message
  that would otherwise never be posted.
- `__osTimerInterrupt`/`__osViSwapContext`: both called with zero arguments
  from this ROM's VI manager thread, matching their real void-void
  prototypes -- made no-ops, since ultramodern already manages both VI
  swaps and timer expiry through its own separate mechanism and duplicating
  that bookkeeping risked the two fighting each other. Flagged as the least
  certain of the six stock_runtime_compat.cpp shims -- revisit first if VI
  timing or timer-driven gameplay ever misbehaves once this is testable.
- The 7 Controller Pak functions: `src/main/main.cpp` already reports no
  pak connected on any port, so none of these need real protocol/storage
  behavior -- they return a consistent non-zero failure (`__osContAddressCrc`
  aside, which is pure arithmetic with no I/O and gets a real implementation
  from general familiarity with the public N64 pak-addressing CRC, flagged
  as not independently checked against a primary source). Revisit if real
  Controller Pak support (rumble or pak saves) is ever wanted.

**Result**: `BattleTanxGARecompiled` links (18.8MB, up from round 22's 15.8KB
placeholder -- confirms all 1300 recompiled functions are now actually
referenced and included) and, when run in this sandbox, correctly falls back
to "no audio device" (no ALSA card here) and then exits cleanly with
`Failed to create window: Vulkan support is either not configured in SDL or
not available` -- exactly the right failure, in exactly the right place,
for a container with no GPU/display. This confirms the boot path (SDL init
-> audio fallback -> window/renderer creation -> `recomp::start`) all runs
correctly up to the point real graphics hardware is required. Testing
further (does the launcher menu appear, does `recomp_entrypoint` run, does
the game boot) needs a machine with a real display and the actual ROM,
which this cloud sandbox is not.

## 2026-09-28, round 23: wrote the real entry point -- and hit exactly the wall PROGRESS.md item 7 predicted

Wrote `src/main/main.cpp`, the piece round 22 flagged as missing (nothing
called `recomp_entrypoint`, so the linker dropped all the recompiled game
code as unreferenced). Structurally modeled on
`bdragoncore/battle-tanx-recomp`'s own `src/main/main.cpp` (cloned to
`/home/user/bdragoncore/battle-tanx-recomp` for reference, same as before --
only the generic ultramodern/librecomp/recompui plumbing carries over, none
of BattleTanx's own game logic, addresses, or polish like its audio
resampling bridge or launcher theming, which this file deliberately leaves
out for now):

- Registers one `recomp::GameEntry` for this ROM: real entry point
  (`0x80071000`), and a real `rom_hash` -- **not** the N64 header CRC1/CRC2,
  but `XXH3_64` of the whole normalized big-endian `.z64` (computed directly
  against the ROM this project has been developed against:
  `0x9c7467e763553529`, `pip install xxhash`), since that's what
  `librecomp/src/recomp.cpp`'s `check_hash` actually compares against.
- `save_type` is `SaveType::AllowAll` -- this ROM's real save type
  (EEPROM/SRAM/FlashRAM) has never been determined; that's real
  undone work, not a considered choice.
- `get_rsp_microcode` returns `nullptr` unconditionally. RT64 ships its own
  generic F3DEX-family GBI interpreters (`lib/rt64/src/gbi/*.cpp` -- F3D,
  F3DEX, F3DEX2, F3DGolden, F3DPD, F3DWave, F3DZEX2, L3DEX2, S2DEX, S2DEX2,
  Extended -- all already built successfully as part of round 22's RT64
  build) that handle GFX tasks via HLE without needing this game's own
  recompiled microcode, so graphics may work without any RSP work at all.
  Audio tasks have no such fallback -- the game will hit
  `quick_exit` printing the unhandled task type the first time it submits
  an `M_AUDTASK`, which is expected until PROGRESS.md item 6 happens.
- No font is registered (`register_primary_font` call is commented out) --
  there's no font file under `assets/` yet, so RmlUi has nothing to render
  UI text with. The launcher menu will likely be visually broken (invisible
  or fallback-glyph text) until one is added.
- Audio playback is a plain `SDL_QueueAudio` push with no resampling --
  functional enough to tell whether audio comes out at all, not tuned to
  sound clean.
- Discovered by trying to actually link it: `recompui`'s own code declares
  `extern SDL_Window* window;` (`ui_state.cpp`) and `default_launcher_init_
  callback` (`ui_launcher.cpp`) reads a global `std::vector<recomp::
  GameEntry> supported_games` **by that exact name** -- these aren't
  optional customization points, they're required extern symbols any game
  project using this frontend must define. Neither is documented anywhere
  outside the source itself; found both only from the linker's undefined-
  reference output naming them.
- Fixed a real `CMakeLists.txt` bug this surfaced: `recompui` and
  `recompinput` (and, separately, `RecompiledFuncs` and `librecomp`/
  `ultramodern`) reference each other's/each layer's symbols without CMake
  knowing about the cross-target cycle, so a single left-to-right static-
  archive scan left resolvable symbols (`recompui::controls_page`) undefined
  depending on which object a given `.a` happened to pull in first on its
  one pass. Wrapped the whole `target_link_libraries(BattleTanxGARecompiled
  ...)` list in `-Wl,--start-group`/`--end-group`. Learned the hard way that
  this has to be literal arguments *inside* the same `target_link_libraries`
  call -- two separate `target_link_options` calls (one for
  `--start-group`, one for `--end-group`) do NOT interleave with the
  library list in call order; CMake collects link options and link
  libraries into separate property lists and concatenates them at fixed
  positions in the final command regardless of when each was called, so
  both flags landed adjacent to each other before the library list instead
  of wrapping it.
- Corrected `patches/recompui_event_structs.h` (added in round 22) to
  exactly match `lib/RecompFrontend/recompui/include/recompui/
  event_structs.h` -- an actual reference copy sitting in that include
  directory for exactly this purpose that a plain grep for the filename
  `ui_types.h`'s own comment names ("must be kept in sync with
  patches/recompui_event_structs.h") had missed on the first pass. My
  hand-derived version used different enum names/types (`int32_t` instead
  of `bool`, `RECOMPUI_EVENT_TEXT` instead of `UI_EVENT_RESERVED1`, etc.) --
  functionally equivalent, but there was no reason to diverge from the
  library's own canonical copy once it was found.

**Where it stands now**: the link fails on exactly 12 undefined
`*_recomp` symbols, all C functions our recompiled code calls that N64Recomp's
built-in `reimplemented_funcs` list expects some runtime to provide, and
neither `librecomp` nor `ultramodern` do (confirmed by grepping their
entire source for each name -- zero matches, not a link-order problem this
time). This is precisely PROGRESS.md item 7,
"stock-runtime compatibility shims" -- the original BattleTanx needed its
own hand-written `stock_runtime_compat.cpp`/`controller_pak.cpp` for the
same reason, and it's now confirmed (not just suspected) that Global
Assault needs its own equivalent too. The 12 symbols split into four real
subsystems, none implemented yet:
  - `__osGetSR_recomp` -- COP0 Status register read.
  - `__osDequeueThread_recomp`, `__osDispatchThread_recomp`,
    `__osPopThread_recomp`, `__osEnqueueThread_recomp` -- libultra thread
    scheduler internals (called from this ROM's own `osDestroyThread`/
    interrupt-handler code, not just from the exception-vector dead code
    already stubbed in `battletanxga.us.rev0.toml`).
  - `__osContAddressCrc_recomp`, `__osPfsSelectBank_recomp`,
    `__osContRamWrite_recomp`, `__osContRamRead_recomp`,
    `__osCheckPackId_recomp`, `__osPfsRWInode_recomp`,
    `__osRepairPackId_recomp` -- Controller Pak (memory card) filesystem
    internals, exactly the `controller_pak.cpp`-shaped gap the CMakeLists.txt
    comment already flagged as unknown.
  - `__osSiCreateAccessQueue_recomp` -- SI (controller port) access queue
    setup.
  - `__osTimerInterrupt_recomp`, `__osViSwapContext_recomp` -- timer
    interrupt and VI (video interface) context-swap internals.

Next step: write real implementations for these 12 functions (a genuine
new round of work, not a quick patch -- four distinct subsystems, most
needing to be understood from libultra's real behavior rather than this
ROM's own disassembly, since these are OS-layer internals the game calls
into rather than game logic).

## 2026-09-27, round 22: first full build -- `BattleTanxGARecompiled` links and runs

Checked out the submodules `.gitmodules` had listed but that had never
actually been added (`git submodule add` for N64ModernRuntime,
RecompFrontend, rt64, plus all of rt64's own ~16 nested submodules and
N64ModernRuntime's/RecompFrontend's own few), installed the missing system
deps (`libvulkan-dev`, `libsdl2-dev`, `libgtk-3-dev`), and iterated the full
CMake + ninja build against round 21's clean N64Recomp output until it
linked. `./build/BattleTanxGARecompiled` now exists and runs (exit 0 --
it's still the placeholder `main()`, see below for what that means).

Two kinds of work this round:

**More symbol-table bugs, found because N64Recomp not erroring during its
own analysis pass doesn't mean the C it generates actually compiles.**
Round 21 declared victory at `N64Recomp`'s exit code 0; round 22 found 9
more function-boundary problems that only surfaced once `gcc` tried to
compile `RecompiledFuncs/*.c`:
- A branch/jump leaving its function is only turned into a proper tail
  call when the target is the exact START of some known function
  (`recompilation.cpp`'s `functions_by_vram.find`); otherwise N64Recomp
  emits `goto L_<addr>` to a label that's never defined, and `gcc` fails
  with "label ... used but not defined". Found and fixed 9 of these by
  splitting the target function at that exact address, including one
  chain reaction (splitting `func_800DB1B0` revealed a *second* branch
  into what became the new `func_800DB258`, needing a further split) --
  wrote a small script that repeats the branch-target scan and
  auto-splits until none remain, rather than fixing them one rebuild at a
  time.
- A write to `$zero` (other than the literal canonical nop encoding)
  compiles to invalid C (`0 = ...;`), since N64Recomp's codegen doesn't
  special-case it. `rabbitizer`'s `outputsToGprZero()`/`isNop()` catch
  this class directly -- found `and $zero,$zero,$zero`, `mfhi $zero`,
  `sllv/srlv $zero,...` and more, all the same trailing-garbage-word
  pattern as round 21's fixes, plus five more fully-fake ~0x10-0x20-byte
  entries in the same persistently bad 0x8011axxx neighborhood.
- The hardest class: a garbage word that decodes as a **fully plausible,
  syntactically valid instruction with real-looking operands** --
  `beql $s2, $s5, ...`/`bne $t3, $t6, ...` built from what's actually
  mid-string ASCII bytes. Nothing in a static per-instruction check flags
  these; the only tell is where their *computed branch target* lands.
  Found 3 this way (`func_800E5BB8`, `func_800F7EC0`, and
  `func_800F8660` -- the last one had actually been logging as an
  "Indirect tail call" during N64Recomp's own analysis with no warning at
  all, then still broke the C compile; **N64Recomp's own log output not
  complaining about a function is not proof it will compile**). Also
  found two ENTIRE functions this way that round 21 had wrongly kept as
  real code (`func_8014749C`, `func_80147740` -- every single instruction
  in both was this same repeating-word garbage, `0x52945294`, matching
  round 16's already-known texture/palette data pattern; deleted both).
- Wrote a proactive full-corpus sweep for "a branch/jump target that is
  neither inside its own function nor the start of any known function" to
  catch the rest of this category in one pass instead of one rebuild
  error at a time -- converged to 0 remaining after applying its findings
  (except the one deliberately-left-alone jump-table case in
  `func_800F8660`, later fixed for real once the C compiler caught it too).
- `n_alEnvmixerPull`'s own stub (round 21) needed to extend to a second
  function, `func_801000B0`, split out of it by the branch-target sweep --
  it contains the same unanalyzable jump table, so it's stubbed for the
  same reason.

**Missing pieces in the actual CMake/build wiring**, none of which had
ever been exercised against a real build before:
- `include/btga_recomp_hooks.h` (force-included into every
  `RecompiledFuncs/*.c`, per `CMakeLists.txt`) didn't exist. Everything
  the current hooks need (`ctx`, `S32`/`S64`/`U32`/`U64`) already comes
  from N64Recomp's own generated `recomp.h`, so this is a placeholder for
  now -- add real declarations here as future hooks need them.
- The `patches/*.c` -> `patches.elf` -> `N64RecompCLI patches.toml` ->
  `RecompiledPatches/patches.c` pipeline (`PatchesLib` in
  `CMakeLists.txt`) assumed real patch sources and a `patches.toml` that
  don't exist yet (PROGRESS.md item 8 is explicitly not started).
  Guarded it the same way `RecompiledFuncs`/`BattleTanxGARecompiled`
  already guard their own not-yet-written sources: build `PatchesLib` as
  an empty placeholder until real patches exist, instead of failing
  outright (`ld.lld: error: no input files`).
- `lib/RecompFrontend/recompui/src/api/ui_api_events.cpp` unconditionally
  `#include`s `patches/ui_funcs.h` (marked `// TODO: Forced game
  includes`) for a `RecompuiEventData` struct/enum set that's meant to be
  generated per-game. Wrote `patches/recompui_event_structs.h` by hand
  from the actual field usage in that file plus the enum values in
  `lib/RecompFrontend/recompui/src/elements/ui_types.h` (which even names
  the expected filename in a comment -- "must be kept in sync with
  patches/recompui_event_structs.h"), and had `ui_funcs.h` include it.
  This is real, needed-now content (unlike the hooks placeholder above),
  not a stub -- but still has no actual game callback declarations in it
  yet, since there are no UI-driving patches to declare.

**What "runs" means right now**: `BattleTanxGARecompiled` links
successfully with all 1300 recompiled functions in `RecompiledFuncs`, all
of RT64/N64ModernRuntime/RecompFrontend, and executes -- but `src/main/`
and `rsp/` are still empty (see `CMakeLists.txt`'s own placeholder-`main()`
fallback), so nothing calls `recomp_entrypoint` or drives the
ultramodern runtime loop yet, and the linker drops the unreferenced
`RecompiledFuncs`/`PatchesLib` object code entirely (hence the ~15KB
binary). Writing that entry point (`bdragoncore/battle-tanx-recomp`'s
equivalent is `src/main/*.cpp`) is the next real step toward the game
actually running, ahead of or alongside PROGRESS.md's items 6-8.

## 2026-09-27, round 21: first clean N64Recomp run -- `N64Recomp battletanxga.us.rev0.toml` exits 0

The big one. Round 20 produced a well-formed config; this round is the
whole debugging loop of actually running it against real `N64Recomp` until
it stopped erroring, function by function. Ends with exit code 0, 1288
functions, 27 output `.c`/`.h` files in `RecompiledFuncs/` (gitignored, not
committed -- regenerate with `N64Recomp battletanxga.us.rev0.toml` from the
repo root once the ROM is in place). Every fix below is applied to
`battletanxga.us.rev0.toml`, `BattleTanxGASyms/battletanxga.us.rev0.syms.toml`,
and the two source `BattleTanxGASyms/*.toml` pieces it's assembled from.

Fixed, roughly in the order N64Recomp's own errors surfaced them:

1. **The whole `[patches] ignored`/`renamed` list from round 19 was
   redundant and actively broke the build.** N64Recomp has its own
   built-in `reimplemented_funcs`/`ignored_funcs`/`renamed_funcs` lists
   (`src/symbol_lists.cpp`, ~440 names) that it applies to matching
   functions *before* it even reads the config's own ignored/renamed
   lists, renaming them to `name_recomp`. Listing a name in both places
   caused "Function X is set as ignored in the config file but does not
   exist!" (it had already been renamed by the time the config-driven
   pass ran). Diffed our 71 names against N64Recomp's built-in set: all
   71 overlap, 0 remaining that need declaring ourselves. Deleted that
   whole section. Any instruction patch or hook targeting one of those 71
   names needed its `func` updated to `name_recomp` to match.
2. **`__libm_qnan_f`** (in the trusted function list from n64sym) turned
   out to be a libm quiet-NaN float *data* constant, not a function ("Unhandled
   instruction: INVALID" trying to disassemble it as MIPS). Removed.
3. **`sync` and `cache`**: N64Recomp's recompiler doesn't implement either
   instruction at all. Nopped every real occurrence (`sync`: 4 icache/
   dcache-init loops; `cache`: 2 more of the same -- the other `cache`
   users, `osInvalDCache`/`osInvalICache`/`osWritebackDCache*`, are in
   N64Recomp's built-in `reimplemented_funcs`, so their bodies are never
   recompiled at all and don't need patches). Finding the real `sync`
   instructions needed a slightly non-obvious scan: its encoding allows a
   nonzero "stype" hint in bits that a naive all-zero-word search would
   miss (confirmed against capstone's decode of the real bytes).
4. **`mfc0`/`mtc0` for anything but cop0 register 12 (Status)**: N64Recomp
   only implements register 12. Everything else ("Unhandled cop0 register
   in mfc0/mtc0: N") is exception-vector dead code under a recompiled
   runtime (EPC/Cause/ErrorEPC reads and writes, and one function that
   dumps every single cop0 register as part of an exception-context save)
   -- nopped 27 mtc0 + 3 eret (round 18) + 41 more mfc0 (this round). Two
   whole functions (`func_8007919C`, `func_80079260`) and a second
   exception-dispatcher copy (`__osException_80104FB0`) had unconditional
   jumps INTO other functions' interiors that no per-function static
   recompiler can handle ("Unhandled branch ... to <mid-function
   address>") -- stubbed all three outright (`[patches] stubs`) rather
   than patching every individual instruction, since none of it can ever
   run under the recompiled runtime anyway.
5. **`movz`/`movn`, trap instructions (`tltu`/`tgeu`), `dmtc0`, `jalr`
   with a non-`$ra` link register**: none of these are implemented by
   N64Recomp's recompiler. `tltu`/`tgeu` were two genuine compiler-inserted
   assertion checks (nopped, safe -- they only ever fire on a bug). Every
   `movz`/`movn`/`dmtc0`/degenerate-`jalr` occurrence turned out to be a
   **data misdecode**, not real code (see next point).
6. **The recurring pattern, by far the most work this round: gap-guessed
   function sizes swallowing whatever came after the real code.** Round
   14's splat-based sizing used "distance to the next known symbol" as a
   function's size, which is only right when nothing sits between two
   real functions. In practice there's often a short string constant, a
   float/jump-table literal pool, or (worse) a completely separate second
   function packed into that same gap. N64Recomp's recompiler disassembles
   a function's *entire* declared byte range, so any of that trailing
   junk being mistaken for code is fatal the moment it decodes into
   something N64Recomp can't handle (or, worse, into something that
   parses as a *plausible-looking but nonsensical* instruction --
   `movn $zero, $zero, ...`, `jalr $zero, $zero`, `j 0x8C000000`, a branch
   whose computed target lands outside this ROM's entire loaded segment --
   which a simple "is this a valid MIPS word" check doesn't catch).
   Resolved ~30 functions this way, in three shapes:
     - **Pure trailing junk, no real second function**: truncate the
       function's size to end right after its own last real `jr $ra`
       return (confirmed by direct disassembly with `rabbitizer`, the
       same MIPS decoder N64Recomp itself uses -- `pip install
       rabbitizer`). ~20 functions (`func_80089E84`, `func_8008A8C4`,
       `func_800A9B64`, `func_800D84DC`, `func_800EAF6C`, `func_800EFC28`,
       `func_800F28AC`, `func_80113E30`, `func_800EA14C`, `func_800CAE10`
       -- this last one the other direction, undershooting by 0x18 bytes
       and cutting off a shared switch-statement epilogue several branches
       target -- and more).
     - **A real second function hiding in the same declared range**:
       split into two symbols, e.g. `func_800E16D8`/`func_800E1BB0`,
       `func_800F38C0`/`func_800F3B80`, `func_800F1770`/`func_800F17B0`,
       `func_800F6650`/`func_800F6C70`, `func_80099784`/`func_80099830`.
       Any instruction patch or hook whose target address moved into the
       second half needed its `func` reference updated.
     - **A handful of ~2-20 byte entries that were entirely fake** --
       n64sym or the round-11 gap scan produced a "function" at an
       address that's actually a pure ASCII string (`func_8011A3F0`,
       `func_8011A484`, `func_8011A4C0` -- literally in-game UI text like
       "Select one Button..." and "...fire All..."), a plain data
       variable (`osViClock`, `__OSGlobalIntMask`, `__osPiAccessQueueEnabled`
       -- real libultra names, but for globals, not functions), or a
       pointer/dispatch table indistinguishable from the round-15 one at
       0x8011a8 (`func_80119628`, `func_8011A5F0`, both in the same
       0x8011axxx-0x8011bxxx neighborhood). Deleted outright.
7. **Missing functions in gaps our round-11 scan never generated a symbol
   for at all** (not a sizing problem -- no entry existed there). Found
   two ways: (a) reactively, from N64Recomp's own `static_0_<addr>`
   auto-analysis when something's `jal` target had no name
   (`func_8007E118`, `func_800A10E0`, `func_800B95C8`/`func_800B99AC`,
   `func_80114470`); (b) proactively, once the pattern was clear enough to
   be worth automating -- swept every gap between two consecutive known
   functions for a clean, valid instruction run ending in a real `jr $ra`
   before waiting for N64Recomp to trip over it (`func_800BB53C`/
   `func_800BBDC0`, `func_800D0070`, `func_800DEEDC`, `func_800EC778`,
   `func_800F1900`). The proactive sweep needed the branch/jump-target
   plausibility check from point 6 (a `j`/branch to an address outside
   this ROM's own 0x80070000-0x80180000 loaded segment is data, not a
   real control-flow edge) to avoid false negatives.
8. **`n_alEnvmixerPull` stubbed, not fixed**: N64Recomp's static analysis
   couldn't determine the size of a computed jump table this function
   uses ("Failed to determine size of jump table at 0x80077720 for
   instruction at 0x80100120" -- the table isn't in this function's own
   byte range, which the analysis requires). This is CPU-side audio
   envelope-mixing code (the software counterpart to the RSP audio
   microcode's own mixing). Stubbed to unblock the build; unlike
   everything else stubbed this round, this one is **not** known-dead
   code and needs real attention once audio is being worked on (item 6 in
   PROGRESS.md).

Net effect on the symbol table: started this round at 1321 trusted
functions (post round-14), ended at 1288 (many 1:1 replacements from
splits, net loses from the outright-fake deletions, net gains from the
newly-found missing functions -- see `git diff` on
`BattleTanxGASyms/battletanxga.us.rev0.syms.toml` for the exact set).

**Tooling note for next time**: `pip install rabbitizer` gives Python
bindings for the *exact* MIPS decoder N64Recomp itself uses
(`rabbitizer.Instruction(word).isValid()`, `.getOpcodeName()`,
`.disassemble()`) -- far more reliable for this kind of validation than
`capstone`, which is more lenient and both missed real problems (e.g. it
happily decoded a `madd` where rabbitizer correctly said `INVALID`, since
VR4300 doesn't have `madd`) and shares the "syntactically valid but
nonsensical" blind spot for data that happens to decode as some real
instruction. Any future symbol-table cleanup should validate against
rabbitizer specifically, not capstone.

## 2026-09-27, round 20: assembled the real battletanxga.us.rev0.toml -- this project has an actual N64Recomp config file for the first time

Merged rounds 13-19's pieces (the symbol table, the 71-entry ignored/
renamed list, the 30 cop0/eret instruction patches, the 101 division
hooks) into one file, `battletanxga.us.rev0.toml` at the repo root,
matching `bdragoncore/battle-tanx-recomp`'s exact structure
(`[input]` / `[patches]` / `[[patches.instruction]]` / `[[patches.hook]]`
in one config). Validated it parses as well-formed TOML and that every
section round-trips to the right counts (Python's `tomllib`: 71 ignored,
71 renamed, 30 instruction patches, 101 hooks).

This is the first time this project has had an actual config file to hand
N64Recomp -- everything before this was symbol-table/patch-list pieces
that hadn't been assembled into the thing the tool actually reads. Still
only covers the first MB's code (see round 17 for why that's believed to
be ~all of it), and still missing the by-inspection stubs
`bdragoncore/battle-tanx-recomp`'s own list has a couple of (not found by
name-matching, so not caught by anything done so far). Running this
config through a real `N64Recomp` build is the natural next checkpoint,
once the toolchain itself is built (`lib/N64ModernRuntime/N64Recomp` per
`BUILDING.md` — not yet done this session, since the submodules aren't
checked out).

## 2026-09-27, round 19: found the real ignored/renamed list -- 71 functions where this ROM's own copy should defer to librecomp

Round 18 deliberately left `stubs`/`ignored`/`renamed` alone rather than
guess at librecomp's API surface. Fixed that properly this round: cloned
`N64Recomp/N64ModernRuntime` and grepped `librecomp/src/*.cpp` for every
function name ending in `_recomp` -- that suffix marks something librecomp
implements natively (139 total, `osInitialize`, `osPiStartDma`,
`__osDisableInt`, the whole `osPfs*`/`osVi*`/`osCont*`/`osFlash*`/
`osEeprom*`/`osVoice*` surface, etc.), not something a recompiled game
should run its own copy of.

Cross-referenced against the trusted symbol table's ~430 n64sym-identified
names: **71 direct matches** -- real functions in this ROM, at real
addresses, that duplicate something librecomp already provides. Generated
`[patches] ignored = [...] renamed = [...]` for all 71
(`BattleTanxGASyms/battletanxga.us.rev0.renamed_ignored.toml`), matching
`bdragoncore/battle-tanx-recomp`'s exact pattern for this (same name in
both lists: `ignored` skips recompiling this ROM's own copy, `renamed`
points calls at librecomp's implementation instead).

**Caveat**: this only covers names n64sym already matched by signature.
`bdragoncore/battle-tanx-recomp`'s own list also stubs functions found by
inspection rather than name-matching (e.g. two cache-invalidate loops the
host doesn't need) -- nothing here does the equivalent search yet, so this
71-entry list is a solid start, not a complete `[patches]` section.

## 2026-09-27, round 18: generated the instruction-level patches N64Recomp's config needs -- cop0/eret nops and guarded div hooks, for real addresses this time

With a trusted, sized symbol table in hand (round 14), did the mechanical
scan `bdragoncore/battle-tanx-recomp`'s own config comments describe as
needed: every `cop0` write and `eret` needs a nop (nothing is emulated),
every `div`/`divu`/`ddiv`/`ddivu` needs a guarded hook instead of running
raw (a real divide-by-zero in the game would otherwise be a host
`SIGFPE`). Decoded these directly from each trusted function's raw
instruction words (same technique as every raw-byte scan this session):

- 27 `mtc0` (cop0 write) instructions, 3 `eret` -- all in places that make
  complete sense (`__osException`, `__osDispatchThread`, `osMapTLBRdb`,
  `__osDisableInt`/`__osRestoreInt`), which is itself a good sign the
  underlying symbol table holds up.
- 101 divisions: 62 `div`, 30 `divu`, 3 `ddiv`, 6 `ddivu`.

Generated real `[[patches.instruction]]` entries for the cop0/eret nops
(`BattleTanxGASyms/battletanxga.us.rev0.instruction_patches.toml`) and
`[[patches.hook]]` entries with the guarded division C for all 101 divides
(`BattleTanxGASyms/battletanxga.us.rev0.div_hooks.toml`), decoding each
instruction's actual `rs`/`rt` operands so the hook text references the
right `ctx->rN` registers rather than being copy-pasted boilerplate.

**Caveat on the div hooks**: the `div`/`divu` (32-bit) hook text exactly
mirrors a confirmed-real pattern from `bdragoncore/battle-tanx-recomp`'s
own config. The `ddiv`/`ddivu` (64-bit) hooks are this project's own
extrapolation -- no 64-bit division example existed in the reference to
confirm the exact syntax/available macros against. Marked inline in the
file; verify before trusting those 9 specifically.

**Not done yet**: `stubs`/`ignored`/`renamed` (which of the ~430
n64sym-identified functions should defer to librecomp's own
implementations instead of being recompiled from this game's copy) needs
real knowledge of librecomp's exact API surface to get right -- guessing
here risks silently wrong config rather than an honest gap, so left for
when that can be checked properly rather than fabricated. Same for the
main `battletanxga.us.rev0.toml` `[input]`/full config file itself, which
these two files are pieces of but don't yet assemble into.

## 2026-09-27, round 17: tested the "code mostly fits in the first MB" hypothesis directly against the second MB -- confirmed. This changes what "finishing the RE" even means for this project.

Round 16 ended on a hypothesis rather than a fact: no evidence of code
past `0x80171000`, so maybe the game's CPU code footprint just mostly fits
in the first automatically-loaded MB. Tested it directly: ran the same
splat/spimdisasm scan (no symbol seeding, since we have none there) across
the entire second MB (rom `0x101000`-`0x201000`, vram `0x80171000`-
`0x80271000`).

**Result: 12 resync points in the whole MB**, versus 2388 in the first
MB -- roughly a 95x drop in apparent code density. Spot-checked the first
one (`func_8017BA38`): the exact same repeating-nibble texture/asset
pattern (`0x6319`, `0x5AD7`, `0x4A53`, `0x4211`) seen everywhere else
non-code data has turned up in this project. It's a false-positive resync
inside pure data, not a real function, and there's no reason to expect the
other 11 are different.

**This confirms the hypothesis rather than just failing to falsify it**:
this game's actual CPU code is concentrated almost entirely in the first
automatically-loaded MB. The remaining ~7MB is overwhelmingly non-code
asset data -- textures, audio samples, level/model data -- not more
undiscovered game logic. Combined with rounds 14-16 (no overlay system, no
DMA-triggered code loading found, PI DMA usage that does exist is
ordinary), the picture that's emerged is a fairly conventional one for an
N64 game of this era: one resident code segment, everything else is
assets.

**This reframes what's actually left to do on this project.** It was
never "reverse-engineer 8MB of unknown code" -- it's "finish mapping
~1MB of code" (already ~85% function-bounded) plus "build asset-extraction
tooling for the other ~7MB" (textures, audio, levels), which is a
different, generally more mechanical kind of work (known N64 texture/
audio formats, not open-ended disassembly). See `PROGRESS.md` for the
updated roadmap reflecting this.

## 2026-09-27, round 16: closed out the "does code reach outside the first MB" question -- no evidence found, likely because there isn't any (yet)

Checked the rest of round 14's 71 remaining suspects. The large ones
(several 0x2000-0x5000 bytes) turned out to be a third category, not
gap-guessed code: raw bytes at the top few (`func_80164CC4`,
`func_8014CFE4`, `func_80160E58`) are the exact same dense repeating-
nibble pattern (`0x63196319`, `0x5AD75AD7`, ...) round 7 originally
flagged as texture/asset data -- not code, not microcode, just ordinary
non-code asset bytes that happened to fall between two known symbols with
nothing splat or n64sym recognized in between.

Checked the small ones (`<=0x200` bytes, the ones most likely to be real
functions with a real external call) individually. All their out-of-range
`j`/`jal`-shaped targets are either suspiciously round addresses
(`0x88000000`, `0x88080000`, `0x8c000000` -- multiples that don't occur in
real code, a signature of a false opcode match against data/padding) or
single isolated one-off hits with no corroborating pattern. None read as
a real call to real code outside the segment.

**Conclusion**: nothing in the first MB's function table -- trusted or
suspect -- shows genuine evidence of code reaching past
`0x80171000`. Combined with rounds 14-15's findings (the suspicious
regions were RSP microcode, a dispatch data table, and plain asset data,
not further code), the most likely explanation is simply that this game's
actual CPU code footprint mostly fits in the first automatically-loaded
MB, with the remaining ~7MB being predominantly assets (textures, audio,
level data) -- not proof, but the working hypothesis until the rest of the
ROM is actually scanned. That scan -- extending the same corrected-header
splat pass past `0x101000` -- is the natural next step now that this
question has a real answer instead of an open loop.

## 2026-09-27, round 15: found a real dispatch-table data structure inside round 14's "suspect" pile; it's the first concrete trace of the message/event dispatcher the 2026-09-19 screenshots hypothesized

Looked closer at round 14's 123 suspect entries rather than treating them
as uniformly bad. The small ones (many exactly `0x20` bytes) turned out to
be a different problem entirely: not gap-guessed code, but a **real data
table misread as a run of tiny functions**. Dumping the raw words at e.g.
`0x8011A280` shows values like `0x80116E80`, `0x80117DDC`,
`0x8011A8DC` -- valid pointers into our own known function range, sitting
as plain data -- interleaved with what my `j`/`jal` opcode scanner
mistook for branch instructions (a data word starting with byte `0x0A`
has the same top-6-bit pattern as a real `j` opcode, by coincidence).

Mapped the actual extent: `0x80118900`-`0x8011B300` (~10.5KB) is ~37%
in-range pointers, consistent with a structured table (pointer/pointer/
flag/zero-style entries) rather than code. A dense, pure 52-pointer run
within it (`0x8011B1A4`-`0x8011B274`) alternates between two tight address
clusters -- `~0x8011A8xx` and `~0x80116Exx`/`0x80116Fxx` -- repeating with
minor variation, which reads like a genuine state-machine/dispatch table
(pairs of e.g. condition-check and action function pointers). This is the
first *concrete, address-level* trace of the "message/event dispatcher"
pattern the 2026-09-19 screenshot session hypothesized from Ghidra's
decompiler view, rather than just a plausible-sounding read of one
function's C-level logic.

**Still doesn't show code reaching outside the first MB**: every pointer
found in this table stays within the known `0x80071000`-`0x80171000`
segment. Moved the 52 addresses this table covers out of the suspect pile
into `syms/battletanx_ga_data_table_0x8011a8.txt` (documented as data, not
carried in the function symbol table at all -- distinct treatment from
round 14's RSP-microcode exclusions, since this is genuine data rather
than a different instruction set). 71 suspects remain genuinely
unclassified -- likely still a mix of real gap-guessed code and more
undiscovered data tables, not yet sorted.

**Where this leaves the "does anything call outside the first MB"
question**: still no confirmed evidence either way. Of the original 123
suspects, 52 are now explained as this data table (in-range) and the
`rspbootTextStart` region as RSP microcode (round 14). The remaining 71
are the only place such evidence could still be hiding, and they haven't
been individually resolved.

## 2026-09-27, round 14: sanity-checked round 13's symbol table -- found and removed 123 gap-guess artifacts, confirmed round 9's RSP microcode hypothesis

Before extending the scan past the first MB, checked whether any code in
round 13's function list calls out beyond the segment (`0x80071000`-
`0x80171000`) -- if the whole game's code fit in the first MB, there'd be
nothing left to chase. Decoding `j`/`jal` directly from raw ROM bytes
within each known function's byte range (not from spimdisasm's text
output, which renders this whole run as raw `.word` throughout -- a
rendering-confidence quirk unrelated to whether the underlying label/
address data is correct, confirmed by re-deriving the exact same 1509
boundaries from it a second time) found real problems, not real overlay
leads:

- `rspbootTextStart` (n64sym's own name!) is full of `j`/`jal`-shaped
  words with nonsensical targets (`0x8c000000`, `0x84001xxx`, ...) --
  because it's genuinely RSP microcode, a different instruction set
  entirely, not CPU code. This **confirms round 9's original hypothesis**
  about this region, which round 11-12 had provisionally walked back after
  finding it densely function-packed under the corrected header. Round
  9 was right about *what* it is; round 10-12 were right that it's
  legitimately resident (both can be true).
- 123 other entries (mostly large `func_XXXXXXXX` placeholders, 0x620-
  0x16F0 bytes each, clustered `0x80118000`-`0x80153000`, plus two n64sym
  *data* symbols wrongly carried as functions -- `__osCurrentTime`,
  `__osTimerList_80134D00`, libultra state variables, not code) show the
  same garbage-jump signature. These are gap-guess artifacts: not enough
  known boundaries in that stretch for splat to have sub-divided it
  correctly, so each absorbed neighboring microcode/data/unfound-function
  bytes into one oversized "function."

Pulled all 123 out to `syms/battletanx_ga_funcs_suspect.txt` rather than
leave them in the trusted table -- N64Recomp would eventually choke trying
to recompile RSP microcode or data as CPU code. Regenerated
`BattleTanxGASyms/battletanxga.us.rev0.syms.toml` with the remaining 1321
entries only (still not independently verified one-by-one, but at least
self-consistent -- every entry's own body only jumps within the segment).

**Real answer to the original question**: after excluding the known-bad
123, the remaining functions collectively contain zero calls leaving the
first-MB segment. Some of what's *in* the suspect list might still reach
outside the segment once properly re-split (can't tell with garbage
boundaries) -- so this doesn't yet prove the whole game fits in 1MB, but
it does mean there's no clean evidence otherwise either. Re-splitting the
suspect region properly (probably needs bounded sub-probing like round 8
did, now under the correct header) would settle it either way, and is
higher-priority than blindly extending the scan into unexplored ROM.

## 2026-09-27, round 13: a real, sized symbol table for the first MB -- BattleTanxGASyms/battletanxga.us.rev0.syms.toml now has actual content

Merged round 11's 1310-function scan with n64sym's ~480 name matches
(preferring the real name where both cover the same address; 20 name
collisions where n64sym matched the same function twice at different
addresses, disambiguated with an address suffix), filtered to the 1509
entries that fall inside the scanned segment (`0x80071000`-`0x80171000`;
52 matches -- fixed low-memory OS state like `osTvType`, plus a few past
the first MB -- saved separately in
`syms/battletanx_ga.symbol_addrs_outofrange.txt` for later), and fed the
result back into splat as `symbol_addrs_path` seed points.

Took three tries to get the run itself right (splat's symbol file format
rejects `#` comments outright; a duplicate name from two n64sym matches
needed disambiguating; two overlapping background invocations sharing one
log path raced and produced a corrupted-looking "complete" log while the
real job was still running for another 8+ minutes -- killed the stray and
reran clean with a fresh log path). Once it ran cleanly: all 1509 seeds
got individual labels with correct sizes (computed from the gap to the
next known boundary), 430 with n64sym's real name instead of a
`func_XXXXXXXX` placeholder.

Split the result into 1444 likely-code entries and 65 data-shaped ones
(`_rodata_`/`_bss_`/`D_`/`jtbl_`-style names, filtered out by pattern --
N64Recomp's `functions` array should only ever list actual code, not
data, or it'll try to recompile rodata as MIPS instructions). The 1444
code entries are now in `BattleTanxGASyms/battletanxga.us.rev0.syms.toml`,
in the actual format N64Recomp expects -- the first time this file has
had real content instead of being an empty placeholder.

**Caveats, spelled out in the file's own header**: sizes are gap-to-next-
known-symbol, not confirmed function ends -- an unfound real boundary
between two known symbols would make the earlier one's listed size too
large. Names ending `_text_XXXX` mark an n64sym signature match at an
offset *inside* a larger function, not necessarily a real separate
function start. Still only covers the first MB (`1/8` of the ROM). Treat
this as a strong first pass, not a verified split -- the next real step
(beyond extending coverage past the first MB) is spot-checking a sample of
these against real disassembly the way round 8/10 did for individual
functions, now that doing so isn't fighting a wrong header.

## 2026-09-27, round 12: confirmed -- the overlay mystery was entirely the wrong header. There is no overlay system here (at least not in the first MB).

Round 11's queued corrected-header scan finished: a single splat pass over
the full first MB (ROM `0x1000`-`0x101000`) recovered **1310 real function
boundaries** via spimdisasm re-syncing, running continuously from the crt0
entry (`0x80071000`) through to `0x8016FC5C` -- past both the old "overlay
wall" and the full "overlay blob" range with no gap, no renewed swallow,
nothing resembling a wall at all.

Checked function density specifically in the two ranges this entire
investigation has spent the most effort on:

| Region | Functions | Density |
|---|---|---|
| Old "overlay wall" (`0x80105000`-`0x80106500`) | 18 | 3.43/KB |
| `OverlayScan6`'s claimed "overlay blob" (`0x800F8000`-`0x80112000`) | 302 | 2.90/KB |
| Whole scanned window (average) | 1310 | 1.28/KB |

Both "mystery" regions are **denser** with clean function boundaries than
the scan's own average -- the opposite of what non-code/garbage data would
look like. There is no overlay wall, no overlay blob, no custom
per-object-type loading slot at `0x800F81CC`. Every round-7-through-9
finding framed around "why does this region look like garbage" was
answering a question created entirely by scanning the wrong ROM bytes.
This also retroactively explains why `OverlayScan1`-`6` (2026-09-19,
Ghidra) never found a loader despite exhaustively searching for one
(direct calls, stored pointers, register-loaded indirect calls, hardware
DMA register use): there wasn't one to find in that range. Their own
Ghidra project may have had a comparable mapping issue for this address
range, or simply never got a working disassembly of it at all -- either
way, "no loader found" was the correct result, just not for the reason
anyone thought at the time.

**Saved**: the full 1310-function list is now in
`syms/battletanx_ga_funcs_round11.txt` (vram addresses only, no sizes yet
-- next real splat pass should use these as seed points via
`symbol_addrs_path` to get a properly bounded, per-function split instead
of one giant swallowed file). This is the first genuinely trustworthy,
broad function-boundary dataset this project has produced.

**Reframing what's actually left to do**, now that the phantom is cleared:
this project doesn't have a special overlay-loader mystery to solve. It
has the completely ordinary (if large) task any from-scratch N64 recomp
has: turn 1310 anonymous `func_XXXXXXXX` addresses into a real symbol
table (start + size + eventually names), find the actual entrypoint/boot
sequence details needed for N64Recomp's config (stubs, instruction
patches, stock-runtime compat shims -- see `PROGRESS.md`), and only then
start standing up the N64Recomp build proper. Round 11's DMA-registers
finding (PI hardware DMA is real and used ~59 places) remains true and
useful independent of the overlay question -- osPiStartDma-family calls
are ordinary and expected in any N64 game, not evidence of anything
exotic.

## 2026-09-27, round 11: the "overlay" mystery itself may be a phantom -- two more pieces of convergent evidence

Following up on round 10's header fix. `OverlayScan6.java` (never actually
run, but its file header documents what `OverlayScan1-5` already found)
places "the overlay blob" this whole investigation has been hunting a
loader for at **VRAM `0x800F8000`-`0x80112000`** -- which is, almost
exactly, the range round 9's n64sym scan identified as ordinary resident
libultra: `rspbootTextStart`, the `Mus*`/`al*` audio library, `osViBlack`,
`guMtxIdent`, `memcpy`, `sqrtf`, `osStartThread`, dozens more, all with
clean signature matches under the corrected header. If that whole range is
just the normal OS library (which round 10 already directly byte-verified
for part of it -- `__osDisableInt`/`__osRestoreInt`), there was never
custom game code there needing a loader in the first place. Worth treating
"there's a custom overlay system in this game" itself as unconfirmed again,
not just the specific addresses examined so far.

**Second, independent thread: re-checked the "DMA hardware registers are
confirmed unused" claim from the 2026-09-19 entry**, since `OverlayScan6`'s
own file header repeats it as settled ("already confirmed the four real
N64 hardware DMA-trigger registers... have ZERO references anywhere in
this ROM"). Wrote a from-scratch, mapping-independent scanner (works
directly on raw ROM bytes, no header assumption needed) for `lui`+`ori`/
`addiu` pairs constructing a full 32-bit address, mirroring
`OverlayScan6`'s own technique. Result matched their claim exactly at
first: zero hits for `PI_DRAM_ADDR`/`PI_CART_ADDR`/`PI_RD_LEN`/`PI_WR_LEN`
(the four DMA-trigger registers), five hits for `PI_STATUS` alone (the
polling register, not a trigger).

But then checked for just a bare `lui $reg, 0xA460` (a PI-register-space
base pointer), **without** requiring an immediate second instruction
completing the same register into one specific address -- and found **59**
occurrences across the ROM, clustered in groups (e.g. seven around rom
`0x4D0`-`0x6F8`, several around `0x95368`-`0x961F4`). This is exactly what
compiled code looks like when it loads a PI-register-block base pointer
once (`lui $t0, 0xA460`) and then hits `PI_DRAM_ADDR`/`CART_ADDR`/`RD_LEN`/
`WR_LEN` via small fixed offsets from that one register (`sw $v0,
0x0($t0)` / `sw $v1, 0x4($t0)`, etc.) -- a pattern the original
lui+ori-pair search (both `OverlayScan6`'s and my own first attempt) is
structurally blind to, since no single instruction pair completes a full
address for those specific registers. **The "DMA is confirmed unused"
conclusion looks like a false negative from a search technique that only
checked one addressing pattern, not an actual absence.** Real PI hardware
DMA is very likely used somewhere in this game after all (consistent with
this ROM's OS library including `osPiStartDma`/`osEPiStartDma`/
`osPiRawStartDma`, all real per round 9's n64sym scan) -- which reopens
the possibility that ordinary `osPiStartDma`-style loading, not an exotic
KSEG1 read-loop, is how any real overlay/asset system here works, if one
exists at all.

**Queued**: a corrected-header splat scan of the full first MB is running
(background, started this round) to get a real function-boundary map like
round 8 tried to build, but on the right bytes this time -- results not in
as of this entry. Once it lands, the actual next step is tracing
`func_8009ED9C`'s real call graph under the corrected header and checking,
address by address, whether any of those `lui $reg, 0xA460` sites sit
inside code that's actually reachable from the game's main loop (as
opposed to, say, buried inside seldom-hit save/EEPROM code) -- that's what
would distinguish "the game uses ordinary PI DMA for something mundane"
from "this is how levels/assets get loaded."

## 2026-09-27, round 10: the ROM-to-RAM mapping every prior round used was wrong -- found and fixed

**This is the most important entry in this file.** Every specific ROM
file-offset claim in every entry below (main_probe's "confirmed 19KB
block," the "overlay wall," `SUB_80087570`'s trampoline, everything in
`syms/screenshot_recovered_funcs.txt`) was computed with the wrong
rom<->vram formula. The *vram* addresses and the reasoning about what
functions call what are mostly unaffected (they came from Ghidra
screenshots or from `jal`/`j` instruction encodings, neither of which
depends on this formula) -- what's wrong is specifically "which ROM file
offset holds the bytes for vram X," which is exactly the thing splat needs
right to disassemble anything correctly. See `syms/rom_info.md` for the
corrected formula and `tools/splat.yaml` for the fixed segment config.

**How this surfaced**: round 9's n64sym scan (built while checking the
reference projects Matt pointed at) returned matches like `__osDisableInt
= 0x80105DB0` -- the exact vram address round 7 had already probed and
called "texture-like data, an overlay slot." Rather than trust either
tool, checked the raw ROM bytes directly:

- At the old-header rom offset for `0x80105DB0` (`vram - 0x7FFFF400` =
  `0x1069B0`, what round 7 and round 8 both probed): genuinely garbage --
  `33333335 6BCDF677 77888AAC ...`, the same repeating-nibble pattern
  round 7 originally flagged. Round 7 wasn't wrong that this specific spot
  is garbage.
- At `vram - 0x80070000` = `0x95DB0` instead: `40086000 2401FFFE 01014824
  40896000 31020001 00000000 03E00008 00000000` -- `mfc0 $t0,$12 / addiu
  $at,$zero,-2 / and $t1,$t0,$at / mtc0 $t1,$12 / andi $v0,$t0,1 / nop /
  jr $ra / nop`. That's not a plausible-looking coincidence; it's the
  textbook compiled form of `__osDisableInt`, instruction-for-instruction.
  `0x80105DD0` (`__osRestoreInt`) matches just as exactly at the
  corresponding offset under the same header.

Where `0x80070000` comes from: `shygoo/n64sym`'s own source
(`src/n64sym.cpp:140`) computes ROM-mode header displacement as
`entryPoint - 0x1000`, reading `entryPoint` from the ROM header itself
(`0x80071000` here, per `syms/rom_info.md` -- not assumed). This game
apparently doesn't use the "boot loads to a fixed 0x80000400" convention
every round of this investigation (including the 2026-09-18 entry that
found the original "crt0 stub") assumed without checking against the
ROM's own header field.

**Closing the loop -- re-examined the crt0 stub itself**: the exact same
physical bytes at rom `0x1000` that round 1/2 originally read (stack
setup, a BSS-clear loop, then `jal 0x8009ED9C`) decode identically under
either header, since none of that depends on the rom<->vram formula --
only the *label* attached to them changes. Under the corrected header
those bytes are `lui $sp,0x8022 / addiu $sp,$sp,-0x1F48` (`$sp =
0x8021E0B8`), a BSS-clear loop for `0x80127E30`-`0x803B17B0`, then `jal
0x8009ED9C` -- and **that address's own first call, under the corrected
header, resolves to `osInitialize`** (vram `0x80105B10`, matching n64sym's
match for that address exactly). A very-first-boot-function calling
`osInitialize` immediately is exactly what should happen; under the old
header this same crt0 is still real (round 1/2 read it correctly), but
its label was wrong -- it genuinely runs at vram `0x80071000`, this ROM's
declared entry point, not `0x80000400`.

**Scale check**: a broad splat scan of the first 320KB under the corrected
header recovered 526 real function boundaries (spimdisasm re-syncing
throughout) -- meaningfully denser than the ~1136-in-~1MB the old header
produced, consistent with the corrected header actually being right rather
than both being comparably-accidental.

**What this means for everything else in this file**: round 7's "overlay
wall" at `func_80105DB0`/`func_801055CC`/`func_80105DD0` is resolved --
there was no overlay mystery there, splat was just being pointed at the
wrong bytes the whole time. Round 8's specific findings about
`SUB_80087570`, `func_800F81CC`, the object-pool functions, etc. all need
to be re-derived from scratch under the corrected header before being
trusted -- they may still turn out to be roughly right (the *vram*
addresses came from Ghidra, independent of this bug), but their content
was read from the wrong bytes, so treat every specific claim about what's
*at* those addresses as unconfirmed again. `syms/screenshot_recovered_funcs.txt`
carries a warning to this effect now.

**Next step**: redo round 8's bounded-probe methodology under
`tools/splat.yaml`'s now-corrected `resident` segment, starting with the
genuinely still-open question -- what does `func_8009ED9C` (now confirmed
to really be the game's first substantial function) actually do, and does
tracing its real call graph (not the wrong-header one) lead anywhere near
an actual overlay/asset-loading mechanism this time.

## 2026-09-27, round 9: checked reference projects Matt pointed at -- a real methodology gap, and the right splat feature for the overlay slot

Matt asked to check the original BattleTanx's recomp repo for reusable
names, and separately pointed at
[RevoSucks/BMHeroRecomp](https://github.com/RevoSucks/BMHeroRecomp)
(Bomberman Hero) as an example of this toolchain done well. Both led
somewhere more useful than literal names.

**bdragoncore/battle-tanx-recomp has no game-specific names to borrow.**
Checked its full symbol table: every non-generic name in it (`osCreateThread`,
`sprintf`, `memcpy`, `cosf`, ~150 total) is a standard libultra/libc name,
almost certainly auto-identified by a signature-matching tool rather than
found by hand -- there is not one manually-named game-specific function
(no `player_update`-style name anywhere). So there's nothing to transplant
address-for-address (the two games don't share code layout anyway), but it
pointed at the actual reusable thing: the *tool* that generates exactly
that kind of match automatically.

**Found and built that tool: `n64sym`** (https://github.com/shygoo/n64sym).
Ships a built-in signature database covering OS 2.0c through 2.0L
`libultra`/`libgultra`/`libleo`/`libnos`/audio libraries, matches them
against a ROM by compiled-instruction signature (tolerant of relocations),
and can emit results directly in splat's `symbol_addrs.txt` format. Built
cleanly in this sandbox (`make n64sym`, plain g++/make, no special
dependencies) and kicked off a thorough scan
(`n64sym rom/battletanx_ga_usa.z64 -s -t -f splat -o ...`) against the GA
ROM -- if this finds real matches, it should identify a good chunk of GA's
own libultra surface automatically, the same way bdragoncore's ~150 names
likely got found. Results not in yet as of this entry; check the next one.

**BMHeroRecomp turned out to be a bigger methodological finding than
expected: it's not a from-scratch reverse-engineering effort at all.**
It's built on top of an existing, separate **full matching decompilation**
project, [bomberhackers/bmhero](https://github.com/bomberhackers/bmhero)
(splat + `asm-differ`, the standard N64 decomp workflow -- rewrite each
function in C until it compiles back to byte-identical machine code),
and only uses that decomp's headers/function definitions where needed for
patches. That's a fundamentally stronger foundation than anything possible
here: **no decompilation project exists for BattleTanx: Global Assault**
(confirmed by the 2026-09-18 entry's own README research, and nothing
found since contradicts that). So this project is necessarily doing the
harder, lower-rigor tier of recomp -- closer to what
`bdragoncore/battle-tanx-recomp` itself did (address+size symbols only,
no matching decomp behind it) -- which is a real, previously-shipped
approach, just slower and more error-prone without a full decomp's
byte-level verification to catch mistakes. Worth being upfront about that
gap rather than implying this project has BMHeroRecomp-level rigor.

**The concretely useful part**: bomberhackers/bmhero's `splat.yaml` shows
the correct splat feature for exactly the "fixed VRAM slot with swappable
content" pattern round 8 (below) found for GA at `~0x800F81CC`:
`exclusive_ram_id: overlay`. Multiple segments, each with a different ROM
`start` but the *same* `vram`, tagged with a shared `exclusive_ram_id`,
tell splat these are mutually-exclusive overlays sharing one VRAM window.
Bomberman Hero has 59 such overlay segments across 8 distinct VRAM slots
(the largest hosting 36 different overlays -- almost certainly one per
level or actor type). This is the right target shape for GA's own
`splat.yaml` once the loader/mapping table is found (round 8's queued next
step: trace what writes into the `800F81CC` slot) -- convert whatever
table is found into one `exclusive_ram_id: overlay` group per distinct
VRAM destination, matching this pattern rather than inventing a new one.

## 2026-09-27, round 8: verified the transcribed leads directly against the ROM -- mixed results, but a real structural finding

Went back to the ROM with splat/spimdisasm (confirmed working in this
sandbox per round 7 below) to directly verify the addresses transcribed
from screenshots in the entry below, rather than trusting the transcription.
Method: bind small splat segments exactly at each address of interest
(same technique round 3 originally used for `main_probe`), since the whole
first-MB region swallows into one giant unreturning blob otherwise --
confirmed again this round (scanning ROM `0x1000`-`0x1061CC` as one
segment recovered 1136 real function boundaries via spimdisasm re-syncing
partway through, but a ~455KB span from `0x8000859C` never re-synced and
had to be probed individually).

**Corrections to the 2026-09-19 transcription** (my own transcription
error, not the original session's -- I conflated an instruction's address
with its enclosing function's address when reading the screenshot):
`0x8002D40C` and `0x8002D4DC` are not function starts. They're a `jal` and
an epilogue instruction respectively, inside two unrelated, ordinary
vector-math functions: `func_8002D3AC` (distance/normalize: `sqrt(x^2+y^2)`
then divide) and `func_8002D468` (accumulate a delta into a stored
position: `pos += delta`). Likewise `func_80027DF0` is real and matches
its transcribed address exactly, but its actual content -- iterating
`D_80216FD0`, decrementing counters, calling `func_80107F10`/
`func_80108078`/`func_8010F6D0` -- reads like generic scheduler/thread
queue cleanup, nothing to do with the asset table (`DAT_800a3b14`) the
transcription associated it with. Net effect: the specific claim "these
call sites read the asset table" doesn't hold up for these three
addresses. `FUN_80048EB0` *did* verify correctly -- it genuinely calls
`func_800F80E8` (see below), matching the transcription exactly.

**What's confirmed real and matches** (all read directly off clean,
individually-bounded splat output, not transcribed):
- `func_80087570` -- confirmed real, exactly as transcribed. But its
  entire body is just `j func_800F81CC` with `sw $v0, 0x0($a2)` in the
  delay slot: a 2-instruction tail-call trampoline, not a "distance
  check" itself.
- `func_80087A80` -- allocates a slot from a 592-byte-stride table
  (`D_80235F00`, indexed by an 8-bit type tag), fills it from another
  object's position fields, and stamps it with the current frame counter
  (`D_8021945C`) at offset `0x2C`. One type value (`0x7F`) short-circuits
  with `j func_800F8700` instead. Reads like an object/effect pool
  allocator.
- `func_80087EF0` -- reads that same `0x2C`-offset frame stamp, computes
  `D_8021945C - stamp`, and compares against `0x1E` (30) before calling
  `func_800AD14C` with args from offsets `0xC`/`0x10`/`0x24` of the
  record. **This matches `FUN_80087eac` from the uploaded `dec1.txt`
  almost exactly** (same `func_0x800ad14c` call, same `+0xc`/`+0x10`/
  `+0x24`/`+0x2c` offsets, same 30-frame-style threshold pattern) --
  they're sibling functions on the same object-record layout. Read
  together, this is an object/effect spawn-and-cooldown system: allocate
  a slot, stamp its spawn frame, later check elapsed frames before acting
  on it. "Distance-gated" in the 2026-09-19 entry's framing was most
  likely describing this frame-age gate, not a ROM/overlay distance --
  **the "loader entry point" read on `SUB_80087570` looks like a wrong
  turn**, not the overlay mechanism.

**The actual structural finding this round**: followed `func_80087570`'s
tail-jump to its target, `func_800F81CC`. It's **not valid code** -- it
disassembles to the same kind of dense, repeating-nibble garbage
(`0x4A534211`, `0x5AD75AD7`, `0x52955295`, ...) that round 7 originally
found at the `probe_105cc`/`probe_105db` "overlay wall" addresses, plus a
long run of zero-padding. Same for `func_800F8700`, the fallback target
`func_80087A80` jumps to for type `0x7F`. **Both addresses are still well
inside the first automatically-loaded MB** (`0x800F81CC`, `0x800F8700` <<
`0x80100400`), which the 2026-09-27 (round 7) entry below hadn't
anticipated -- that entry's "past the 1MB boundary" framing for the
overlay wall doesn't hold here. The more precise picture: there's a fixed
VRAM code slot around `0x800F81CC`-`0x800F87xx` that legitimately holds
non-code data most of the time and gets overlay-loaded with real,
per-object-type handler code at runtime, with stable call-through points
(`func_80087570`, the `func_800F8700` fallback) that always exist and
just jump into whatever's currently loaded there. This is a materially
different (and more specific) theory than either "overlay slots only
exist past 1MB" (round 7) or "DMA is unused, it's a KSEG1 read loop"
(2026-09-19 entry) -- it doesn't resolve the open DMA-vs-no-DMA
contradiction from that entry, but it does explain why a fixed, frequently
-called address can be garbage at one moment (as found here) and real code
at another (as the `dec1.txt`/`ss.txt` decompiles, presumably captured
while something legitimate *was* loaded there, suggest).

**Not yet done**: `func_80087A80`'s type-indexed table (`D_80235F00`,
592-byte stride) is the natural next place to look for how a type maps to
what gets loaded into the `800F81CC` slot -- haven't traced what writes to
that VRAM range yet, which is the actual loader.

## 2026-09-19 (transcribed 2026-09-27): recovered findings from screenshots -- a real overlay/asset table, and a direct link to the "wall" functions

**This entire section was reconstructed from screenshots** Matt uploaded to
this repo (`memorymap.png`, `scree2/4/5/6.png`, `screenpart.png`, `1.png`,
`3.png`, `4.png`, `new1/2/3/34/345.png`, and others), not from any file.
They show a Ghidra session (project `globalrecomp`, imported successfully
with the N64 loader by Warranty Voider -- MIPS:BE:64:64-32addr, o32,
Ghidra 12.1.2) and, in the `new*.png` files, a chat with a Claude Code
session on Matt's own machine driving that Ghidra session interactively,
timestamped the morning of 2026-09-19 -- i.e. **after** the 2026-09-18
entry below, and with real progress that entry doesn't mention. That
session said more than once that it was writing its findings into
`STATUS.md` as it went; that version of the file was never uploaded here,
only these screenshots were, so this section is a best-effort reconstruction
of what it must have said. Treat the addresses/values below as read off
screenshots, not verified against the ROM directly.

**Confirmed real, non-splat-block functions found via Ghidra's own
analysis** (separate from -- and in some cases earlier in VRAM than -- the
19KB block splat confirmed on 2026-09-18):

- `FUN_80007078` (VRAM `0x80007078`) -- calls `FUN_80078e34` and
  `FUN_80078e40`. Real, clean code, well before the splat-confirmed block.
- A cluster of functions (`FUN_80095470`, `FUN_80095b68`, `FUN_80095b90`,
  `FUN_80095cf0`, `FUN_8009c9c0`, `FUN_8009ca10`, `FUN_8009cb40`,
  `FUN_8009cc20`, and more -- 20+ total per Ghidra's own xref list) that
  all read or write `PI_STATUS` (the real N64 Peripheral Interface DMA
  status register, `0xA4600010`). `FUN_80095b68` decompiles cleanly:
  ```c
  void FUN_80095b68(void) {
      do {} while ((PI_STATUS & 3) != 0);   // wait for PI DMA/IO idle
      uStack00000018 = PI_STATUS;
      ASIC_BM_STATUS = *(undefined4 *)(in_stack_0000001c + 0x10);
      FUN_801067fc();
      PI_STATUS = 2;                         // clear/ack
      DAT_80126e60 = DAT_80126e60 | 0x100401;
  }
  ```
  This is a textbook PI-DMA wait/kick routine -- real hardware DMA *is*
  used somewhere in this game, contrary to the "DMA unused" note below.
  Reconciling that contradiction is unresolved (see "Open contradiction").
- **`FUN_80095470` calls `FUN_80105dd0` directly** -- one of the three
  "overlay wall" functions from the 2026-09-18 entry
  (`func_801055CC`/`func_80105DB0`/`func_80105DD0` there, same addresses,
  Ghidra's auto-naming just capitalizes differently). This is the first
  real evidence connecting the wall functions to the PI/DMA status-handling
  code, rather than just being called blind from `func_8009ED9C` as the
  2026-09-27 entry above found.
- `FUN_8009f3b4`: checks flag `DAT_80126ed0`, conditionally calls
  `FUN_80110750` and clears the flag.
- **`FUN_80110750` and `FUN_800f80e8` both fail to decompile** -- Ghidra
  reports "Control flow encountered bad instruction data" and falls
  through to `halt_baddata()`. Cross-referenced from `FUN_8009f3b4`,
  `FUN_80048eb0`, and `FUN_80087664`. These read exactly like the "overlay
  slot" symptom from the 2026-09-18 entry (garbage at the naive address),
  but now with real, confirmed callers instead of just a bare `jal` in
  isolation.
- `FUN_800923b0`: calls `FUN_80105db0()` (another wall function, no args),
  stores the result, and compares it against an internal ROM header
  pointer -- reads like a "re-initialize if the header pointer changed"
  guard, i.e. more real control flow built around the wall functions.

**The actual overlay/asset table, found by address, not guessed:**
tightly packed 8-byte entries of `{ uint32 size; uint32 romAddr; }`
starting at `DAT_800a3b14` (values noted: size `0x0000349B` + a romAddr
around `0x8059xxxx`, size `0x00000AAC`, size `0x00000970`, and more -- a
couple KB to ~20KB each, i.e. **individual asset chunks, not one big
overlay blob**). Real Ghidra xrefs, not guesses:
- `DAT_800a3b14` read by `FUN_8002d4dc` (at ROM/ref `0x80028240`)
- `DAT_800a3b1c` / `DAT_800a3b2c` read by `FUN_8002d40c` (at
  `0x80026d20` / `0x80026d24`)
- `PTR_DAT_800a3b38` / `DAT_800a3b3c` read by `FUN_80027df1`

The working theory in that thread: each call site loads one specific known
asset by passing its `(size, romAddr)` pair into a **shared copy/decompress
routine**, and that shared routine -- not any of these specific call sites
-- is the real target, since it's very likely the same primitive the
overlay loader itself uses.

**A jump-table misdecoding theory for the "bad instruction data" crashes**:
`FUN_80087570` (also referred to as the "distance-check / loader entry
point" -- see below) has an unresolved register-indirect jump (`jr $reg`)
right after what looks like a critical-section guard
(`getCopReg(2, 0x3010)` / `getCopReg(2, 0x300e)`). Ghidra doesn't
recognize the case-address table this jump presumably reads from as data,
so it tries to disassemble those bytes as instructions and produces
exactly the "Unimplemented instruction" / "bad instruction data" garbage
seen at `FUN_80110750`, `FUN_800f80e8`, and `FUN_80087570` itself. If
right, the fix is marking those byte spans as data instead of code, not
finding a DMA loader.

**A dispatcher lead pointing at the real activation code**: a
message/event dispatcher (a common N64-era pattern -- one function fielding
many per-object event IDs) has a case that calls three functions back to
back: `SUB_80087570` (the distance-gated loader entry point being traced),
then `SUB_80087ef0`, then `SUB_80087a80`. The latter two hadn't shown up
before that point and were flagged as strong candidates for the code that
actually performs the overlay copy/activation, since `SUB_80087570` itself
only looked like a distance check plus a call onward.

**Open contradiction, unresolved**: that thread stated "the actual DMA
hardware registers are confirmed unused anywhere in this ROM, so the real
overlay copy is almost certainly a plain software loop reading straight
from the memory-mapped cartridge space (`0xB0xxxxxx`, direct-mapped per
Ghidra's own memory map, no DMA controller involved) rather than a
hardware-triggered transfer." That directly conflicts with the confirmed
`PI_STATUS`-using functions above, which unambiguously do use real PI DMA
registers. Possible reconciliations, neither confirmed: (a) that claim was
scoped to the specific asset-table loader being traced at the time, while
PI DMA is genuinely used elsewhere (e.g. controller pak / EEPROM save
access, matching `patches/README.md`'s boot-race note about `osContInit`),
or (b) the claim needs re-checking against `FUN_80095b68`/`FUN_80095470`
directly. Worth resolving before trusting either theory fully.

**Queued next steps in that thread** (not yet done, per the screenshots):
decompile `SUB_80087ef0`, `SUB_80087a80`, `FUN_8002d40c`, `FUN_8002d4dc`,
and `FUN_80027df1`; and fix the `FUN_80087570` jump table by marking the
misread span as data.

**Also recovered, likely unrelated to the overlay question**:
`FUN_800a1b80` and neighbors look like a `%`-escape string formatter
(printf-style); `FUN_80048eb0` parses a byte-packed, `\x02`-terminated
value out of `DAT_80219590`/`91`/`92`, reading like a compressed
command/animation-list decoder. Both are real, confirmed code, just not
obviously part of the loading path.

## 2026-09-27: splat now runs in a cloud sandbox; the "overlay wall" is probably not overlays

Picked up the prior scaffold and analysis (uploaded by Matt to this repo's
`main` branch as archives) and continued from where the 2026-09-18 entry
left off.

**The PyPI/npm/crates.io block from the previous entry is not universal.**
In this session, `pip install splat64[mips]` (which pulls in `spimdisasm`
and `rabbitizer`, splat's real dependency chain) worked with no issues,
after two packaging speed bumps: `pylibyaml`/`intervaltree`'s legacy
`setup.py` fails to build under a too-new `setuptools` (`AttributeError:
install_layout`) unless you pin `setuptools<60` in a venv first, and the
plain `pip install splat64` alone leaves `spimdisasm`/`rabbitizer`/`n64img`
unresolved even though splat needs them at import time -- `splat64[mips]`
pulls the full set. Once installed, `python3 -m splat split tools/splat.yaml`
ran cleanly and reproduced the confirmed 19KB resident block
(`0x9F99C`-`0xA449C` ROM, VRAM `0x8009ED9C`-`0x800A389C`) from the
2026-09-18 entry exactly, plus function-split suggestions splat had never
gotten to run before.

**Read `func_8009ED9C` in full** (the very first function `crt0` calls, and
per the round-6 findings the first instruction of the confirmed resident
block). It:

- Wraps its body in a call to `func_80105DB0` (return value kept in `$s0`)
  at the top, and `func_80105DD0` (called with that same value in `$a0`) at
  the very bottom -- a plain acquire/release or begin/end pair, not
  something that reads like a DMA kickoff.
- In between, does list/queue-style manipulation on two globals,
  `D_80126EF0` and `D_80126EE8` -- consistent with the "list-traversal/
  dispatch logic" read from the round-3 notes.
- Also calls `func_80110EE0` and `func_801056CC` in passing, both similarly
  unconditional.

**Why this matters**: `func_80105DB0`, `func_801055CC`, `func_80105DD0` and
`func_80110EE0` all sit at VRAM addresses past `0x80100400` -- the edge of
the ~1 MiB block the N64's IPL3 bootcode loads automatically for the
standard CIC chips (6101/6102/7101/7102), which is also, not coincidentally,
almost exactly where the confirmed-clean resident block ends
(`0x800A389C`). Code past that boundary normally has to be DMA'd in
explicitly by the game before first use. But here, `func_80105DB0` is
called completely unconditionally in the first few instructions of the
*first function the game ever runs* -- and `crt0` itself is only 14
instructions (set `$sp`, clear BSS, `jal 0x8009ED9C`), leaving no room for
a loader call before that. That's hard to reconcile with "this is a
runtime-loaded overlay slot," which was the working theory in the
2026-09-18 entry after round 7 found texture-like garbage at the naive
linear ROM offset for these addresses.

Checked one alternative explanation and ruled it out: if there were a
static jump/overlay table listing these functions' real ROM locations as
raw data words, the literal 4-byte big-endian value for their VRAM address
(e.g. `80 10 5D B0`) ought to show up somewhere in the ROM as data. It
doesn't -- not for any of `func_80105DB0`, `func_801055CC`,
`func_80105DD0`, `func_80110EE0`, `func_801056CC`, or even `func_8009ED9C`
itself (checked all six against the full 8 MiB ROM). That's expected for
plain `jal` call sites (the encoding doesn't carry a full 32-bit address),
so it doesn't disprove an overlay table, but it does rule out "there's an
easy-to-find table of raw pointers to grep for."

**Leading hypothesis now**: the simple `rom_offset = vram - 0x7FFFF400`
mapping that correctly located the confirmed 19KB block probably just
stops applying past that block for a structural reason -- padding, a
second linked segment, or a non-contiguous section layout the linker
produced -- rather than these specifically being demand-loaded overlays.
Round 7's "texture-like data" finding at the naive offset would then mean
"wrong offset," not "not code." This doesn't yet rule the overlay theory
back in either; it's genuinely unresolved.

**Not attempted this round**: downloading and running Ghidra headless
against `tools/OverlayScan6.java` (the script from 2026-09-18, written for
exactly this search). Java 21 and network access are both available in
this sandbox, so it's likely feasible here too, but pulling down a Ghidra
distribution is a bigger, slower step worth checking with Matt about
before spending the time.

### Consolidated into the repo this round

- `syms/rom_info.md`, `syms/undefined_funcs_auto.txt`,
  `syms/undefined_syms_auto.txt`, `tools/*.py`, `tools/OverlayScan6.java`,
  `tools/splat.yaml`, `battletanx_ga.ld`, `src_probes/*.c` (renamed from
  `src/` to avoid colliding with the N64Recomp-style `src/` this repo also
  has, from an earlier, more N64Recomp-build-centric scaffolding pass done
  in parallel -- see below) -- all from the 2026-09-18 upload.
- `tools/symbols_to_n64recomp_toml.py` -- new. Converts a Ghidra CSV export
  or a splat-style `name = 0xADDR;` list into the `[[section]].functions`
  TOML array N64Recomp's own symbol file format expects (see
  `bdragoncore/battle-tanx-recomp`'s `BattleTanxSyms/*.syms.toml` for the
  target format). Not yet run against anything real -- there's no confirmed
  full function list yet to feed it.
- The ROM (`rom/battletanx_ga_usa.z64`) and the large raw probe dumps
  (`assets/unk_*.bin`, several megabytes each, essentially fragments of the
  ROM itself) were deliberately **not** brought into this repo's tracked
  tree -- see "Heads up" below.

### Also present in this repo: a from-scratch N64Recomp-style scaffold

Before finding this uploaded work, a parallel scaffolding pass (same
session) set up `CMakeLists.txt`, `.gitmodules` (N64Recomp,
N64ModernRuntime, RecompFrontend, rt64), `patches/`, `include/`, and
`BattleTanxGASyms/` following `bdragoncore/battle-tanx-recomp`'s structure
directly -- the build-system side of what this project will eventually
need, once real symbols exist. That's still in the repo and still correct;
it just hasn't been exercised against anything yet, since the symbol table
it expects doesn't exist. The splat-based work above is the actual path to
producing that symbol table. See `PROGRESS.md` for that side's status.

### Heads up: the ROM ended up in this repo's history

The uploaded `battletanx-recomp.7z` / `battletanx-recomp-scaffold.zip`
archives (commits `d1abf2d` and `52b0132` on `main`) contain the full ROM
dump and several multi-megabyte raw excerpts of it, because they're
straight archives of a working directory that had those files present
locally (correctly gitignored *within* that nested project, but the
archive tool doesn't know about `.gitignore`). They're sitting in this
repo's git history on GitHub now. Worth deciding whether to scrub that
history (e.g. rewriting `main`, or just deleting-and-force-pushing once
the useful bits are extracted) -- didn't do this myself since rewriting
`main`'s history isn't something to do without asking first.

## 2026-09-18 (evening, after seven rounds of real splat runs)

### Today's arc: crt0 stub -> confirmed resident code -> the overlay wall

Ran splat for real on Matt's machine (see "Blocked" below for why that
couldn't happen in the sandbox that built this scaffold). Getting it
running took a few config bugs (top-level segments must be `type: code`
with an explicit `subsegments` entry; splat resolves `base_path` relative
to the config file's own directory, not the invocation directory) -- all
fixed in `tools/splat.yaml`.

From there, seven rounds of narrowing (full history is in the comments at
the top of `tools/splat.yaml` -- worth reading in full, it's a genuinely
useful log of what worked and what didn't):

1. Scanning the whole ROM as one segment hung -- no `jr $ra` ever found.
2. Bounding it at the hang point produced a wall of raw `.word` output.
   Reading it by hand: real code is a **0x38-byte crt0 stub**
   (`0x80000400`-`0x80000438`) that clears BSS then does
   `jal 0x8009ED9C ; nop` -- strong additional confirmation the compiler is
   IDO/SN64-family, not GCC (this is the textbook idiom).
3. Probed the `jal` target (ROM `0x9F99C`) in a small window. Fully clean.
4-6. Widened progressively (8KB, then 128KB, then ~1.1MB) chasing a second
   hang, which turned out to be a misread: a `Select-String` count of 7,522
   "invalid instruction" hits at 128KB was wrongly written off as "small
   isolated data pockets" before confirming where output actually stopped.
   Checking the output directory directly (not just the progress bar) found
   it frozen writing a 0-byte file at the exact address those samples
   started at. **Confirmed real resident code: `0x9F99C`-`0xA449C` (~19KB),
   VRAM `0x8009ED9C`-`0x800A389C`.**
7. That confirmed 19KB block calls out to three further addresses
   (`func_801055CC`, `func_80105DB0`, `func_80105DD0`). Probed each
   individually rather than extending the linear scan again. All three
   turned out to be **texture/pixel-looking data**, not code, from byte
   zero -- not a partial corruption like every previous wall. This is the
   signature of an **overlay slot**: the real code the game expects at
   these RAM addresses gets DMA'd in at runtime from elsewhere in the ROM;
   what's sitting at the naive "linear" ROM offset is just whatever
   unrelated asset happens to occupy that byte range.

   (2026-09-27 note: re-examined above -- the unconditional, argument-free
   way `func_80105DB0` gets called from the very first instructions of the
   very first function the game runs doesn't fit a demand-loaded overlay
   well. Leading theory now is a wrong ROM-offset mapping past this point,
   not necessarily an overlay. Still open.)

**Where this leaves us**: this ROM has a real, non-trivial statically-
resident block (crt0 stub + ~19KB of dispatcher-style logic), which is more
than nothing, but code reachable beyond that isn't reliably at
`vram - 0x7FFFF400` in the ROM file. Further progress needs the actual
**overlay loader** -- almost certainly a DMA/file-load routine somewhere in
that confirmed 19KB block -- which is a Ghidra job (reading real code for
`osPiStartDma`-shaped calls), not something splat's config can find by
guessing more addresses.

## Done

- Confirmed the supplied ROM is v64 byte-swapped despite its `.n64`
  extension; normalized to big-endian `.z64`; header matches expected USA
  identification (`NBQE`) -- see `syms/rom_info.md`.
- Scaffolded the repo: `lib/` submodules for N64Recomp, N64ModernRuntime,
  RT64, RecompFrontend; `src/`, `include/`, `syms/`, `patches/`, `tools/`
  layout following VPW64Recomp/GGA-Recomp.
- Documented the known USA-ROM boot-timing race as a patch to expect --
  see `patches/README.md`.
- Got real, clean disassembly confirming the crt0 stub and ~19KB of
  resident dispatcher code, and confirmed (empirically, not by assumption)
  that this game uses an overlay system for code beyond that.

## Blocked / needs to happen elsewhere

The cloud sandbox this scaffold was built in has PyPI, npm, and crates.io
blocked by egress policy (confirmed genuine 403s). splat's real dependency
chain (`spimdisasm`, `rabbitizer`) couldn't be installed there. All the
actual disassembly work above happened on Matt's machine instead, driven
interactively from this session.

(2026-09-27 note: not true in every cloud sandbox -- see above.)

## Next steps, in order

1. Ghidra pass (N64 loader plugin) over the confirmed resident block
   (`0x9F99C`-`0xA449C`). Specifically look for DMA/file-read calls
   (`osPiStartDma` or equivalent) -- that's the overlay loader, and finding
   it is what unblocks reading any code beyond the resident block.
   (2026-09-27: `tools/OverlayScan6.java` is a Ghidra script written for
   this; not yet run. Worth trying headless in a cloud sandbox before
   assuming it needs Matt's machine.)
2. Once the loader is found, figure out its table format (what maps an
   overlay ID/address to a ROM source location) -- that turns "guess a jal
   target and hope" into "look it up properly."
3. Check whether the original 1998 BattleTanx N64 ROM is available, to
   byte-match shared engine/libultra functions against it (the trick both
   VPW64Recomp and GGA-Recomp used against their own sister titles) --
   still useful for identifying functions within the confirmed resident
   block, independent of the overlay question.
4. Locate the USA boot-race branch (`patches/README.md`) in the real
   symbol map and turn it into a real N64Recomp TOML patch entry.
5. Once the overlay mechanism is understood, start standing up the
   N64Recomp TOML config proper and get a first compile against
   N64ModernRuntime + RT64.
