# patches/

C code compiled for the N64 (MIPS) and recompiled alongside the game, for
changes that replace a whole game function. A `RECOMP_PATCH` function here
takes the place of the original function of the same name.

- `recompui_patches.c`: replaces `func_800A1858`, the game's per-VI swap
  routine, with the same logic plus the frontend's per-frame UI pump and the
  screen-edge scissor fix (STATUS.md rounds 61 and 94).

The build is automatic: `make` here produces `patches.elf` (through WSL on
Windows, since the Windows clang builds have no MIPS backend), then N64Recomp
recompiles it using `patches.toml` in the repo root. `syms.ld` gives native
runtime functions dummy addresses so the patches can call them.

Smaller fixes don't live here: instruction patches and hooks that run native
code at a point in a game function are in `battletanxga.us.rev0.toml`, with
the native code in `src/game/`.

## The USA boot-timing race

The USA ROM calls `osContInit` before libultra's VI timer list is set up;
emulators had to patch around it (n64js PR #123, mupen64plus-core issue
#283). No patch is needed here: `osContInit` and the VI manager are provided
by the runtime instead of being recompiled from the ROM.
