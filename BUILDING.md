# Building Guide

This mirrors the process used by
[bdragoncore/battle-tanx-recomp](https://github.com/bdragoncore/battle-tanx-recomp)
for the original BattleTanx. **It is not yet possible to complete step 4**
for this project — see [PROGRESS.md](PROGRESS.md) — because the symbol table
and config for Global Assault haven't been produced yet. The steps below are
what building will look like once that work is done.

## 1. Clone with submodules

```bash
git clone --recurse-submodules <this repo's URL>
# if you forgot --recurse-submodules:
cd /path/to/cloned/repo && git submodule update --init --recursive
```

## 2. Install dependencies

### Linux (Ubuntu)

```bash
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev lld llvm clang
```

### Arch Linux (paru)

```bash
paru -S cmake ninja llvm clang lld sdl2-compat freetype2 gtk3
# MIPS cross toolchain, for ROM analysis / the MIPS patches
paru -S mips64-elf-gcc mips64-elf-binutils mips64-elf-newlib mips-linux-gnu-binutils
```

### Windows

Visual Studio 2022 with "Desktop development with C++", the C++ Clang
compiler for Windows, and C++ CMake tools for Windows. Also install `make`
(e.g. via `choco install make`).

## 3. Obtain the target ROM

**Not yet pinned down.** Once the exact revision this project targets is
confirmed (region, version, SHA-1), it'll be documented here the same way
the original project documents its target:
`BattleTanx (USA) 1.0`, SHA-1 `535860d941738ac1210c20a9b80114fea0e0ff17`.

The ROM is never committed to this repository. Place your own dump at the
repo root once the target filename is set here.

## 4. Generate the C code

Build `N64Recomp` from `lib/N64ModernRuntime/N64Recomp` (see its own README)
and copy the executable to the repo root. Then, once
`battletanxga.us.rev0.toml` and `BattleTanxGASyms/battletanxga.us.rev0.syms.toml`
exist and are populated (see PROGRESS.md — this is the part that isn't done
yet):

```bash
./N64Recomp battletanxga.us.rev0.toml
```

This produces `RecompiledFuncs/`, which is gitignored.

## 5. Build

```bash
cmake -S . -B build-cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --target BattleTanxGARecompiled -j$(nproc) --config Release
```

This also compiles `patches/*.c` targeting MIPS with clang+lld, recompiles
them with N64Recomp, and embeds the result in the executable — see
`patches.toml` and `CMakeLists.txt`.

Run the resulting executable from the repo root (or copy `assets/` next to
it) once there's game code to run.
