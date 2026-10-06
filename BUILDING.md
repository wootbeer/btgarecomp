# Building Guide

Most players want the prebuilt Windows zip from the
[releases page](https://github.com/wootbeer/btgarecomp/releases). This guide
is for building from source.

You need your own copy of the game: BattleTanx: Global Assault, USA, revision
1.0 (`syms/rom_info.md`). The ROM is never committed to this repository; the
recompiled code is generated from it locally.

## 1. Clone with submodules

```bash
git clone --recurse-submodules https://github.com/wootbeer/btgarecomp.git
# if you forgot --recurse-submodules:
git submodule update --init --recursive
```

`main` holds the latest release. Newer work may be on a development branch;
check it out before the submodule update if you want it.

This pulls in `lib/N64ModernRuntime`, `lib/RecompFrontend` and `lib/rt64`
(plus rt64's own nested submodules), several hundred MB in all.

## 2. Install dependencies

### Windows

- Visual Studio 2022 with the "Desktop development with C++" workload,
  including the "C++ Clang Compiler for Windows" and "C++ CMake tools for
  Windows" components.
- WSL with `clang`, `lld` and `make` installed in it (for Ubuntu:
  `sudo apt install clang lld make`). The small MIPS patch library in
  `patches/` has to be cross-compiled for the N64, and the Windows builds of
  clang don't include the MIPS backend, so CMake runs that one step through
  `wsl.exe`.

Nothing else is needed: the renderer uses D3D12 on Windows, and CMake fetches
SDL2 itself.

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev libvulkan-dev lld llvm clang
```

### Arch Linux

```bash
paru -S cmake ninja llvm clang lld sdl2-compat freetype2 gtk3 vulkan-headers
```

Linux builds and runs in development, but no Linux release is packaged yet.
macOS is not set up.

## 3. Put the ROM in place

Place your dump at the repo root, named exactly
`BattleTanx Global Assault (USA).z64`. The recompilers read it from there
(`battletanxga.us.rev0.toml`, `n_aspMain.us.rev0.toml`).

It must be big-endian `.z64` (header bytes `80 37 12 40`). To convert a
`.n64` or `.v64` dump:

```bash
python3 tools/normalize_rom.py "your dump.n64" "BattleTanx Global Assault (USA).z64"
```

The game itself also asks for the ROM on first launch (it accepts `.z64`,
`.n64` and `.v64`) and checks it against this exact release's hash
(`0x9c7467e763553529`, XXH3-64 of the normalized file).

## 4. Generate the recompiled code

Build N64Recomp and run it on the config. This writes `RecompiledFuncs/`
(gitignored; regenerate it after any change to the `.toml` or the symbol
file).

**Windows** (in a Developer PowerShell for VS 2022, see "Windows shell"
below):

```powershell
$env:BTGA_CLANGCL = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"
cmake -S lib\N64ModernRuntime\N64Recomp -B lib\N64ModernRuntime\N64Recomp\build -G Ninja -DCMAKE_C_COMPILER="$env:BTGA_CLANGCL" -DCMAKE_CXX_COMPILER="$env:BTGA_CLANGCL"
cmake --build lib\N64ModernRuntime\N64Recomp\build --target N64RecompCLI
lib\N64ModernRuntime\N64Recomp\build\N64Recomp.exe battletanxga.us.rev0.toml
```

**Linux:**

```bash
cmake -S lib/N64ModernRuntime/N64Recomp -B lib/N64ModernRuntime/N64Recomp/build -G Ninja
cmake --build lib/N64ModernRuntime/N64Recomp/build --target N64RecompCLI -j$(nproc)
./lib/N64ModernRuntime/N64Recomp/build/N64Recomp battletanxga.us.rev0.toml
```

The CMake target is `N64RecompCLI`; its output file is named `N64Recomp`.

The main build (next step) generates the rest itself: the RSP audio microcode
(`rsp/`, via RSPRecomp) and the patch library (`RecompiledPatches/`).

## 5. Build and run

**Windows:**

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$env:BTGA_CLANGCL" -DCMAKE_CXX_COMPILER="$env:BTGA_CLANGCL"
cmake --build build --target BattleTanxGARecompiled
.\build\BattleTanxGARecompiled.exe
```

**Linux:**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BattleTanxGARecompiled -j$(nproc)
./build/BattleTanxGARecompiled
```

Run it from the repo root: the game loads `assets/` relative to the working
directory. Use `-DCMAKE_BUILD_TYPE=Debug` for a build a debugger can step
through.

## 6. Package a release

### Windows

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\tools\package-windows.ps1 -Version 0.1.0
```

This runs from any PowerShell (it sets up Visual Studio's x64 environment
itself), builds Release in `build-release\` so your normal `build\` is left
alone, and writes `dist\BattleTanxGARecompiled-<version>-windows.zip` with
the exe, its DLLs, `assets\`, README and license. Steps 1-4 must be done
first.

### Linux (experimental)

```bash
tools/package-linux.sh 0.1.1
```

Builds Release in `build-release-linux/` (or `$BUILD_DIR`) and writes
`dist/BattleTanxGARecompiled-<version>-linux-x86_64.tar.gz`: the binary, a
`BattleTanxGARecompiled.sh` launcher that runs it from its own folder, the
assets, README, license and third-party license notices. SDL2, GTK 3,
FreeType and Vulkan come from the system; the binary needs glibc 2.38 or
newer if built on Ubuntu 24.04. Settings and saves go to
`~/.config/btgarecomp` (or the game folder with `portable.txt`).

## Troubleshooting (Windows)

**Windows shell.** The build must use the x64 toolchain. A Developer
PowerShell or "Developer Command Prompt" can default to x86, which fails
late with `lld-link: undefined symbol: mainCRTStartup`. Check with
`$env:LIB` (PowerShell) or `echo %LIB%` (cmd): it must contain `\x64`
paths, not `\x86`. If not, open "x64 Native Tools Command Prompt for VS
2022" instead (in cmd, use `set "BTGA_CLANGCL=..."` and `%BTGA_CLANGCL%`).

**Which clang-cl.** Visual Studio ships a 32-bit-hosted `clang-cl.exe` in
`VC\Tools\Llvm\bin\` and a 64-bit one in `VC\Tools\Llvm\x64\bin\`. Pass the
x64 one by full path as above; a bare `clang-cl` can pick the wrong one.
Adjust `Community` to your edition and the drive if your install differs.
`$env:BTGA_CLANGCL` doesn't carry over to a new window.

**Starting over.** After changing compiler or architecture, delete the build
folder (`Remove-Item -Recurse -Force build` in PowerShell) and configure
again.

**Missing generated files.** If `RecompiledFuncs\` is missing, CMake builds a
placeholder that does nothing; rerun step 4. If the ROM isn't at the repo
root, the build has no audio microcode.
