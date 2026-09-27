# Status

Last updated: 2026-09-27, in a Claude Code cloud session (a different sandbox
from the one that wrote the entries below).

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
