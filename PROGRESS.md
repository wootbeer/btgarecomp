# Progress / Roadmap

This tracks what exists so far and what's actually needed to get BattleTanx:
Global Assault running through N64Recomp. It's modeled on the process
[bdragoncore/battle-tanx-recomp](https://github.com/bdragoncore/battle-tanx-recomp)
went through for the original BattleTanx, adjusted for what's specific to
this sequel.

**Nothing here reuses the original BattleTanx's symbol table or patches.**
Global Assault is a different ROM with different code at different
addresses; every function boundary, hook and instruction patch has to be
found again from scratch by disassembling this game's binary.

## Done

- [x] Repo scaffolding: submodules (`N64ModernRuntime`, `RecompFrontend`,
      `rt64`), CMake project skeleton, folder layout, licensing.
- [x] `tools/symbols_to_n64recomp_toml.py`: converts a Ghidra function-list
      export (CSV: name, address, size) or a splat-style symbol list into
      the `[[section]].functions` TOML array N64Recomp's symbol file format
      expects.
- [x] ROM identified and normalized (USA, `NBQE`, entry point `0x80071000`)
      — see `STATUS.md` and `syms/rom_info.md`.
- [x] The correct ROM-to-RAM mapping (`vram - 0x80070000`, not the
      `0x80000400` convention every early round assumed — see
      `syms/rom_info.md`), found and verified after the wrong assumption
      produced a months-long false "overlay loader" investigation (rounds
      1-9) that round 10-12 resolved as a phantom entirely caused by the
      bad mapping. No custom overlay system was ever found once the right
      bytes were being read.
- [x] 1310 real function-start addresses recovered across the whole first
      automatically-loaded MB (ROM `0x1000`-`0x101000`) via splat/
      spimdisasm under the corrected mapping — `syms/battletanx_ga_funcs_round11.txt`.
      No sizes yet (see item 3), and this is likely only a fraction of the
      full ~8MB ROM's functions.
- [x] A good chunk of this ROM's own libultra/audio-library surface
      auto-identified by name via `n64sym` (~480 matches — `osInitialize`,
      `__osDisableInt`, the whole `Mus*`/`al*` audio library, etc.), 71 of
      which are confirmed duplicates of something librecomp already
      provides.
      This happened on the reverse-engineering side (splat/n64sym against
      the real ROM), tracked in `STATUS.md`, separately from this file's
      build-system tracking.
- [x] Confirmed (round 17) that this game's actual CPU code fits almost
      entirely in the first automatically-loaded MB — the remaining ~7MB
      is very likely asset data, not more code to find. Changes the shape
      of what's left more than any single item below does.
- [x] **`battletanxga.us.rev0.toml` exists and is well-formed** (round
      20) — `[input]`, `[patches] ignored`/`renamed` (71 entries),
      `[[patches.instruction]]` (30 cop0/eret nops), `[[patches.hook]]`
      (101 division guards). The first time this project has had an
      actual config file to hand N64Recomp, not just pieces of one.

## Blocking, needs more reverse engineering

**Updated framing (round 17):** this was scoped as "reverse-engineer an
8MB ROM," matching the scale the original BattleTanx's ~1700-line symbol
table implied. That's turned out not to be quite the right shape of the
problem. Round 17 directly tested whether code exists past the first
automatically-loaded MB (scanned the entire second MB, found ~95x fewer
resync points than the first, all of which check out as false positives in
asset data) and concluded this game's actual CPU code footprint is
concentrated almost entirely in that first MB. The remaining ~7MB is very
likely textures/audio/level data, not more code waiting to be found. If
that holds up, the real remaining work splits into two different kinds:
finishing the code-side symbol table for ~1MB (this section), and building
asset-extraction tooling for the other ~7MB (a new, separate concern — not
listed as a numbered item yet since no work has started on it, but real
and necessary before a working port; N64 texture formats, VADPCM audio,
and whatever this game's level-data format turns out to be are all
well-trodden, mechanical problems compared to open-ended disassembly).

1. ~~Identify the exact ROM revision~~ — done, see `syms/rom_info.md`.
2. ~~Find the entrypoint and boot sequence~~ — entry point, the correct
   ROM-to-RAM mapping, and the `crt0` stub are all confirmed
   (`STATUS.md` rounds 10-12). The boot-timing race documented in
   `patches/README.md` still needs locating in real disassembly before it
   can become a TOML patch entry, and it's not yet known whether Global
   Assault needs its own version of the original game's
   `stock_runtime_compat.cpp`-style shims (item 7 below) — that depends on
   code not yet reached.
3. ~~Full function boundary list (first MB)~~ — mostly done for what
   matters: 1321 code entries with real (gap-derived) sizes in
   `BattleTanxGASyms/battletanxga.us.rev0.syms.toml`, the actual format
   N64Recomp expects (`STATUS.md` rounds 13-14), covering essentially all
   of this game's actual code (round 17 — the rest of the ROM is asset
   data, not more functions to find). 71 entries
   (`syms/battletanx_ga_funcs_suspect.txt`) are still unresolved gap-guess
   artifacts; 52 more were identified as a real dispatch-table data
   structure (`syms/battletanx_ga_data_table_0x8011a8.txt`, round 15) and
   correctly excluded rather than miscounted as functions. Still needed:
   properly re-split the 71 remaining suspects (bounded probing like round
   8, now under the correct header) — mostly for completeness at this
   point, not because anything suggests they hide new code elsewhere — and
   spot-check a sample of the trusted entries against real disassembly,
   since sizes are still gap-derived rather than independently confirmed
   one by one.
4. ~~libultra call identification~~ — done for the first MB: 430 function
   entries carry n64sym's real name, and 71 of those are confirmed
   (cross-referenced directly against `N64ModernRuntime`'s
   `librecomp/src/*.cpp`) to duplicate something librecomp already
   provides. `[patches] ignored`/`renamed` generated for all 71
   (`BattleTanxGASyms/battletanxga.us.rev0.renamed_ignored.toml`,
   `STATUS.md` round 19). Not complete: the reference project's own list
   also stubs a couple of functions found by inspection rather than
   name-matching (e.g. cache-invalidate loops the host doesn't need) —
   no equivalent search has been done here yet.
5. ~~Instruction-level patches~~ — done for the first-MB code: 27 cop0
   nops, 3 eret nops
   (`BattleTanxGASyms/battletanxga.us.rev0.instruction_patches.toml`), and
   101 guarded division hooks
   (`BattleTanxGASyms/battletanxga.us.rev0.div_hooks.toml`) — written by
   an equivalent of the original project's unpublished generator, built
   against this ROM directly (`STATUS.md` round 18). The 9 ddiv/ddivu
   hooks are an unverified extrapolation (no reference example existed for
   64-bit division) — check those specifically before trusting them.
6. **RSP microcode identification** — which F3DEX/audio microcode
   variant(s) Global Assault ships (hash them and check against RT64's and
   N64ModernRuntime's known microcode tables), and whether the checked-in
   recompiled microcode from the original project applies or new ones need
   generating with RSPRecomp.
7. **Stock-runtime compatibility shims** — the original game needed
   hand-written compat code (`stock_runtime_compat.cpp`,
   `rsp_stock_compat.hpp`) to run on stock N64ModernRuntime instead of a
   game-specific fork: SP status bit translation, bounds-checked RSP DMA,
   a `cop0_status_write` shim, and a yield wrapper. Global Assault will
   very likely need its own version of some of these, but which ones and
   what they need to do can only be determined by reading its actual
   disassembly.
8. **Patches** (`patches/*.c`) — game behavior that needs source-level
   rewriting rather than a binary patch (e.g. how the UI is driven each
   frame, controller pak access, cheat/level-select hooks), written in C
   against the discovered function addresses and cross-compiled with
   clang+lld targeting MIPS via N64Recomp's patch pipeline.

## How to help this along right now

If you have Ghidra analysis already done against a Global Assault dump:

- Export the function list (Ghidra: **Window → Functions**, or a script
  dumping name/address/size to CSV) and hand it over — that's directly
  usable input to `tools/symbols_to_n64recomp_toml.py`, and doesn't require
  sharing the ROM itself.
- Any labeled libultra (`os*`) functions, known data structures, or notes
  on the boot sequence are equally useful.

The ROM itself should stay local — it's never committed to this repo (see
`.gitignore`), matching how the reference project handles it.
