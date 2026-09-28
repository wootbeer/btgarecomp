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

Visual Studio 2022 with the "Desktop development with C++" workload,
including its "C++ Clang Compiler for Windows" and "C++ CMake tools for
Windows" optional components. Also install `make` (e.g. `choco install
make`) — only needed once `patches/*.c` has real content to cross-compile
for MIPS (PROGRESS.md item 8, not started yet), skip it for now if you just
want to build and run.

No separate Vulkan SDK install needed: on Windows this project's renderer
(`plume`, RT64's GPU backend layer) builds against **D3D12**, not Vulkan
(`CMakeLists.txt` only turns Vulkan on for Linux) — D3D12 ships with
Windows/the Windows SDK already. SDL2 is fetched automatically by CMake on
Windows (`FetchContent`), so there's nothing to install for it either.

Run all commands below from an **x64 Native Tools Command Prompt for VS
2022** (or equivalent Developer PowerShell), so `clang-cl`/`ninja` resolve
correctly. This project has only actually been built and run on Linux so
far in this session (no Windows machine available) — the code has been
read through carefully for Windows-specific issues (two real ones were
found and fixed just from that review: a missing Windows window-handle
path in `src/main/main.cpp`, and a `CMakeLists.txt` reference to an icon
resource file that doesn't exist yet), but there has been no actual
Windows build to confirm against. If something else breaks, report the
exact error back and it can very likely be fixed the same way.

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

**Linux/macOS:**
```bash
cmake -S lib/N64ModernRuntime/N64Recomp -B lib/N64ModernRuntime/N64Recomp/build -G Ninja
cmake --build lib/N64ModernRuntime/N64Recomp/build --target N64Recomp -j$(nproc)
```

**Windows** (from an x64 Native Tools Command Prompt, or Developer
PowerShell, for VS 2022 — both work, just match the syntax below to
whichever one you actually have open):

Visual Studio ships *two* `clang-cl.exe` copies — a 32-bit-hosted one under
`VC\Tools\Llvm\bin\` and a 64-bit-hosted one under `VC\Tools\Llvm\x64\bin\`.
Passing bare `-DCMAKE_C_COMPILER=clang-cl` lets Windows' PATH search pick
whichever one comes first, and that has turned out to be inconsistent even
from the correct x64 dev environment — the wrong one produces a build that
fails in confusing ways deep into compiling or linking (see STATUS.md round
27 for what that looked like). Set the full path explicitly instead, so
there's no ambiguity (adjust `Community` to `Professional`/`Enterprise` and
the drive/path if your Visual Studio install differs):

Developer PowerShell:
```powershell
$env:BTGA_CLANGCL = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"
cmake -S lib\N64ModernRuntime\N64Recomp -B lib\N64ModernRuntime\N64Recomp\build -G Ninja -DCMAKE_C_COMPILER="$env:BTGA_CLANGCL" -DCMAKE_CXX_COMPILER="$env:BTGA_CLANGCL"
cmake --build lib\N64ModernRuntime\N64Recomp\build --target N64Recomp
```

x64 Native Tools Command Prompt (cmd.exe):
```bat
set "BTGA_CLANGCL=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"
cmake -S lib\N64ModernRuntime\N64Recomp -B lib\N64ModernRuntime\N64Recomp\build -G Ninja -DCMAKE_C_COMPILER="%BTGA_CLANGCL%" -DCMAKE_CXX_COMPILER="%BTGA_CLANGCL%"
cmake --build lib\N64ModernRuntime\N64Recomp\build --target N64Recomp
```
(Ninja parallelizes automatically using all cores — no `-j` flag needed.
`$env:BTGA_CLANGCL`/`%BTGA_CLANGCL%` don't carry over to a new shell window
— re-set it, or just re-paste the full path, if you closed and reopened.)

Then, from the repo root, with the ROM in place from step 3:

**Linux/macOS:**
```bash
./lib/N64ModernRuntime/N64Recomp/build/N64Recomp battletanxga.us.rev0.toml
```

**Windows** (PowerShell or cmd.exe — same command either way):
```
lib\N64ModernRuntime\N64Recomp\build\N64Recomp.exe battletanxga.us.rev0.toml
```

This produces `RecompiledFuncs/` (1300 recompiled functions as of round 22 —
gitignored, regenerate any time from the ROM + this repo's own symbol
table/config).

## 5. Build

**Linux/macOS:**
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BattleTanxGARecompiled -j$(nproc)
```

**Windows** (same shell as step 4, reusing the `BTGA_CLANGCL` variable set
there — re-set it first if this is a new window):

Developer PowerShell:
```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$env:BTGA_CLANGCL" -DCMAKE_CXX_COMPILER="$env:BTGA_CLANGCL"
cmake --build build --target BattleTanxGARecompiled
```

x64 Native Tools Command Prompt (cmd.exe):
```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="%BTGA_CLANGCL%" -DCMAKE_CXX_COMPILER="%BTGA_CLANGCL%"
cmake --build build --target BattleTanxGARecompiled
```

If you need to wipe a stale `build\` directory first (e.g. after switching
which compiler binary gets used), in PowerShell that's `Remove-Item -Recurse
-Force build`, not `rmdir /s /q build` (that's cmd.exe-only syntax).

This also builds `PatchesLib` as an empty placeholder for now (no
`patches/*.c` content or `patches.toml` exist yet — PROGRESS.md item 8) and
`src/main/main.cpp`/`src/game/*.cpp` (the entry point and stock-runtime
compat shims written in round 23/24).

Run the resulting binary — `build/BattleTanxGARecompiled` on Linux/macOS,
`build\BattleTanxGARecompiled.exe` on Windows — from the repo root (so it
can find the ROM and, once one exists, an `assets/` folder next to it).
This has only been run in a display-less cloud sandbox so far, where it
correctly falls back through "no audio device" to a clean failure at
window/renderer creation (no GPU there) — on a real machine with a display,
this is the point where whether the launcher menu appears and the game
actually boots becomes testable for the first time, on Windows for the
first time ever in this project's history. If you hit a crash or hang past
that point, check STATUS.md's round 24 entry first — the stock-runtime
compat shims and RSP microcode gap (PROGRESS.md items 6-7) are the most
likely places for a real bug to be hiding, and several of the choices there
are explicitly flagged as unverified against a running game.
