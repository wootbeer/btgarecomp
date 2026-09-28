# Building Guide

This mirrors the process used by
[bdragoncore/battle-tanx-recomp](https://github.com/bdragoncore/battle-tanx-recomp)
for the original BattleTanx. All the steps below are now actually possible
(as of round 24 — see STATUS.md/PROGRESS.md) — this was not true earlier in
the project's history, when the symbol table and config for Global Assault
hadn't been produced yet.

**This work lives on the `claude/optimistic-cray-t17tfo` branch, not yet
merged to `main`.** Clone/checkout that branch specifically, or these steps
won't find any of it.

## 1. Clone with submodules

```bash
git clone --recurse-submodules -b claude/optimistic-cray-t17tfo <this repo's URL>
# if you forgot --recurse-submodules or -b:
cd /path/to/cloned/repo
git checkout claude/optimistic-cray-t17tfo
git submodule update --init --recursive
```

This pulls in `lib/N64ModernRuntime`, `lib/RecompFrontend`, and `lib/rt64`
(plus rt64's own ~16 nested submodules) — several hundred MB, expect this to
take a while.

## 2. Install dependencies

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev libvulkan-dev lld llvm clang
```

`libvulkan-dev` and `libgtk-3-dev` are required even for a first build (RT64
needs Vulkan headers, and the native file dialog library needs GTK on
Linux). `lld`/`llvm`/`clang` are only needed once `patches/*.c` has real
content to cross-compile for MIPS (PROGRESS.md item 8, not started yet) —
skip them for now if you just want to build and run.

### Arch Linux (paru)

```bash
paru -S cmake ninja llvm clang lld sdl2-compat freetype2 gtk3 vulkan-headers
# MIPS cross toolchain, for ROM analysis / the MIPS patches (not needed yet)
paru -S mips64-elf-gcc mips64-elf-binutils mips64-elf-newlib mips-linux-gnu-binutils
```

### Windows

Visual Studio 2022 with "Desktop development with C++", the C++ Clang
compiler for Windows, and C++ CMake tools for Windows. Also install `make`
(e.g. via `choco install make`). Untested against this project's actual
CMakeLists.txt as of round 24 — the build has only been exercised on Linux
so far; if something Windows-specific breaks, it hasn't been hit yet.

### macOS

Not yet set up/tested — `CMakeLists.txt` has some `APPLE` branches from the
original reference project, but they haven't been exercised for this one.

## 3. Obtain the target ROM

- **Region/revision**: USA, `NBQE`, revision 1.0 — see `syms/rom_info.md`.
- **Filename**: place your dump at the repo root, named exactly
  `BattleTanx Global Assault (USA).z64` (matches `rom_file_path` in
  `battletanxga.us.rev0.toml`).
- **Byte order**: must be normalized big-endian `.z64` (header magic
  `80 37 12 40`), not `.v64`/`.n64`. If your dump isn't already in that
  format, normalize it first:
  ```bash
  python3 tools/normalize_rom.py "your dump.n64" "BattleTanx Global Assault (USA).z64"
  ```
- **Hash check**: `src/main/main.cpp` registers this exact ROM by its
  `XXH3_64` hash (`0x9c7467e763553529`, computed over the whole normalized
  file — not the N64 header CRC1/CRC2). If your dump doesn't match, the
  game will refuse it as an unrecognized ROM at runtime rather than fail to
  build; double check normalization if that happens.

The ROM is never committed to this repository (see `.gitignore`) — this
step always has to happen locally, on every machine.

## 4. Generate the recompiled C code

Build `N64Recomp` from `lib/N64ModernRuntime/N64Recomp`:

```bash
cmake -S lib/N64ModernRuntime/N64Recomp -B lib/N64ModernRuntime/N64Recomp/build -G Ninja
cmake --build lib/N64ModernRuntime/N64Recomp/build --target N64Recomp -j$(nproc)
```

Then, from the repo root, with the ROM in place from step 3:

```bash
./lib/N64ModernRuntime/N64Recomp/build/N64Recomp battletanxga.us.rev0.toml
```

This produces `RecompiledFuncs/` (1300 recompiled functions as of round 22 —
gitignored, regenerate any time from the ROM + this repo's own symbol
table/config).

## 5. Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BattleTanxGARecompiled -j$(nproc)
```

This also builds `PatchesLib` as an empty placeholder for now (no
`patches/*.c` content or `patches.toml` exist yet — PROGRESS.md item 8) and
`src/main/main.cpp`/`src/game/*.cpp` (the entry point and stock-runtime
compat shims written in round 23/24).

Run the resulting `build/BattleTanxGARecompiled` from the repo root (so it
can find the ROM and, once one exists, an `assets/` folder next to it).
This has only been run in a display-less cloud sandbox so far, where it
correctly falls back through "no audio device" to a clean failure at
window/renderer creation (no GPU there) — on a real machine with a display,
this is the point where whether the launcher menu appears and the game
actually boots becomes testable for the first time. If you hit a crash or
hang past that point, check STATUS.md's round 24 entry first — the
stock-runtime compat shims and RSP microcode gap (PROGRESS.md items 6-7) are
the most likely places for a real bug to be hiding, and several of the
choices there are explicitly flagged as unverified against a running game.
