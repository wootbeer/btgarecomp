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
      and a confirmed-clean 19KB resident code block found via splat — see
      `STATUS.md` and `syms/rom_info.md`. This happened on the
      reverse-engineering side (splat/Ghidra against the real ROM), tracked
      in `STATUS.md`, separately from this file's build-system tracking.

## Blocking, needs more reverse engineering

The ROM is in hand and splat now runs against it (see `STATUS.md`), but the
bulk of the actual disassembly work is still ahead — the same scale of
effort the original game's recomp needed (its symbol table alone runs
~1700 lines), and current progress covers only ~19KB of what's an 8MB ROM.

1. ~~Identify the exact ROM revision~~ — done, see `syms/rom_info.md`.
2. ~~Find the entrypoint and boot sequence~~ — entry point and the `crt0`
   stub are confirmed (`STATUS.md`). The boot-timing race documented in
   `patches/README.md` still needs locating in real disassembly before it
   can become a TOML patch entry, and it's not yet known whether Global
   Assault needs its own version of the original game's
   `stock_runtime_compat.cpp`-style shims (item 7 below) — that depends on
   code not yet reached.
3. **Full function boundary list** — the actual blocker right now is
   `STATUS.md`'s "overlay wall": code past the confirmed 19KB block isn't
   reliably at the naive linear ROM offset, and splitting the rest of the
   ROM depends on resolving that first. Once real function boundaries exist
   for a section, `tools/symbols_to_n64recomp_toml.py` turns them into the
   symbol table (`BattleTanxGASyms/battletanxga.us.rev0.syms.toml`)
   N64Recomp needs.
4. **libultra call identification** — which known OS functions (`osSp*`,
   `osSi*`, `osInvalICache`, etc.) exist in the binary and at what
   addresses, so they can be `renamed`/`ignored` in favor of librecomp's
   implementations instead of being recompiled from the game's copy.
5. **Instruction-level patches** — every `cop0` write and `eret` needs a nop
   (nothing is emulated), and every `div`/`divu`/`ddiv`/`ddivu` needs the
   guarded hook version. The original project generated most of this
   mechanically from the ROM + symbol table; that generator script wasn't
   published in the reference repo, so either write an equivalent against
   this ROM or find each one by hand/disassembler search.
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
