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
- [x] **`N64Recomp battletanxga.us.rev0.toml` runs clean — exit code 0,
      1288 functions, 27 output files** (round 21). This took a full
      debugging pass against the real tool: dropped the whole
      ignored/renamed list (redundant with N64Recomp's own built-in
      one and actively broke the build), nopped several more
      unhandled-instruction classes (`sync`, `cache`, most `mfc0`/`mtc0`,
      two stray trap instructions), stubbed three dead exception-vector
      functions and one real-but-not-yet-supported audio function
      (`n_alEnvmixerPull`), and — the bulk of the work — found and fixed
      ~30 function-boundary bugs where a gap-guessed size had swallowed
      trailing string/table data or an entire second function, plus 8
      functions missing from the symbol table entirely. Full breakdown in
      `STATUS.md` round 21. `RecompiledFuncs/` isn't committed (gitignored,
      build output) — regenerate with `N64Recomp battletanxga.us.rev0.toml`
      from the repo root once the ROM is present locally.
- [x] **Full build: `./build/BattleTanxGARecompiled` links and runs**
      (round 22). Checked out the real submodules for the first time
      (N64ModernRuntime, RecompFrontend, rt64 and their own nested
      submodules — `.gitmodules` had listed them since the start but
      `git submodule add` had never actually been run), installed the
      missing system packages (`libvulkan-dev`, `libsdl2-dev`,
      `libgtk-3-dev`), fixed 9 more symbol-table bugs that N64Recomp's own
      exit code hadn't caught (it only caught them once `gcc` tried to
      compile the generated C — its own "no error" isn't proof the output
      compiles), and filled in three pieces of CMake/build wiring that had
      never been exercised: a placeholder `include/btga_recomp_hooks.h`,
      an empty-`PatchesLib` guard for the not-yet-written `patches/*.c`
      pipeline, and a real (not placeholder) `patches/
      recompui_event_structs.h` for the UI event ABI recompui's own
      "forced game include" needs regardless of whether this game has any
      UI-driving patches yet. Full breakdown in `STATUS.md` round 22.
      **What this doesn't mean yet**: `src/main/` and `rsp/` are still
      empty, so the linker drops all the recompiled game code as
      unreferenced and the binary just runs an empty placeholder `main()`
      — writing the real entry point (item 7 below, and
      `bdragoncore/battle-tanx-recomp`'s `src/main/*.cpp` for the shape of
      it) is what makes the game itself start running.
- [x] **`src/main/main.cpp` written — the game code is now actually
      referenced and the link runs into a real, well-defined wall**
      (round 23). Registers this ROM's `GameEntry` (real entry point and
      ROM hash; save type is `AllowAll` since the real one isn't known),
      wires up SDL-based graphics/audio/input using RecompFrontend's own
      library functions, and fixes a real `CMakeLists.txt` static-link
      ordering bug it surfaced. The link now fails on exactly 12 missing
      `*_recomp` functions — see item 7, this is that item's blocker,
      confirmed rather than just suspected. Full detail in `STATUS.md`
      round 23.
- [x] **The 12 stock-runtime shims are written and `BattleTanxGARecompiled`
      links and runs** (round 24) — `src/game/stock_runtime_compat.cpp`
      and `src/game/controller_pak.cpp`, item 7 below. 18.8MB binary (up
      from round 22's 15.8KB placeholder — every recompiled function is
      now actually linked in), and running it in this sandbox falls back
      cleanly through "no audio device" to a graphics-hardware failure
      (`Vulkan support is either not configured...`) — the correct outcome
      for a container with no GPU, and confirms the boot path runs
      correctly up to that point. Whether the launcher menu actually
      appears and the game boots needs a machine with a real display and
      the ROM, which this environment isn't. Full derivation (each shim's
      real behavior, checked against this ROM's own call sites, not
      guessed) in `STATUS.md` round 24.

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
3. ~~Full function boundary list (first MB)~~ — 1288 code entries in
   `BattleTanxGASyms/battletanxga.us.rev0.syms.toml`, the actual format
   N64Recomp expects, covering essentially all of this game's actual code
   (round 17 — the rest of the ROM is asset data, not more functions to
   find). This has now been validated the way that matters most: every
   single one of these entries' bytes decodes and recompiles cleanly
   through the real `N64Recomp` tool (round 21), not just a heuristic
   disassembler check. Round 21 fixed ~30 boundary bugs (gap-guessed sizes
   that had swallowed trailing data or a whole second function) and added
   8 functions that were missing from the table entirely — see `STATUS.md`
   round 21 for the full list and how each was found. 52 entries were
   identified as a real dispatch-table data structure
   (`syms/battletanx_ga_data_table_0x8011a8.txt`, round 15) and correctly
   excluded rather than miscounted as functions. Remaining rough edges,
   real but no longer blocking: sizes are still mostly gap-derived (just
   independently confirmed to at least be *valid, self-consistent* code
   now) rather than checked one-by-one against a full manual
   disassembly, and it's not yet known whether any of the ~30
   round-21 fixes changed real behavior versus just satisfying the
   recompiler (the guarded-division hooks in particular deserve a
   second look now that a couple of the functions they were attached to
   turned out to be data, not code — see round 21's point 6).
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
6. ~~RSP microcode identification~~ — graphics tasks run through RT64's
   own HLE; the audio microcode (`n_aspMain`) is recompiled with RSPRecomp
   from `n_aspMain.us.rev0.toml` at build time (`STATUS.md` round 90).
7. ~~Stock-runtime compatibility shims~~ — all 12 written (round 24,
   `src/game/stock_runtime_compat.cpp` and `src/game/controller_pak.cpp`)
   and the binary links and runs. Not fully verified: none of this has
   been exercised against an actual running game (no display/GPU in this
   environment) — `__osTimerInterrupt`/`__osViSwapContext` (made no-ops)
   are flagged as the least certain, and `__osContAddressCrc`'s CRC
   algorithm wasn't checked against a primary libultra source. Revisit
   both if VI timing, timer-driven logic, or (if ever wanted) real
   Controller Pak support misbehave once this is testable on a machine
   with a real display and the ROM.
8. ~~Patches~~ (`patches/*.c`) — the ELF-based patch toolchain works on
   the user's Windows build: `func_800A1858` (VI-swap throttle) is
   `RECOMP_PATCH`ed to pump recompui's UI callbacks and apply the
   screen-edge scissor fix every VI (`STATUS.md` rounds 62, 94).

## Playable — feature status (round 95)

Confirmed working on the user's Windows PC: boot,
menus, the intro/demo, campaign levels, credits, the recompui menus with
fonts and icons, audio (music and effects), and frame pacing.

Added in round 95, untested on a real run yet:

- **Controller Pak saves** — `src/game/controller_pak_hle.cpp` emulates
  libultra's osPfs* API over a standard 32 KB `.mpk` image per controller
  in the saves folder (Project64-compatible layout).
- **Rumble** and the per-controller pak choice — General tab, *Player 1
  Accessory* / *Players 2-4 Accessory* (`src/main/game_config.cpp`).
- **Local multiplayer** — General tab, *Local Multiplayer* (restart, then
  Controls → Assign players).
- Audio: stereo channels were swapped, the Sound tab volume was ignored,
  and underruns caused crackle — all fixed in `main.cpp`.

Not done:

- Higher-framerate interpolation (RT64's "Display" framerate option):
  needs per-game matrix/display-list tagging.
- Widescreen beyond RT64's Expand mode.
- Launcher/UI art specific to this game (currently BanjoRecomp's assets).

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
