# Android port: plan

Status: step 1 partly done (host tools for cross-compiling; see Progress).
Written 2026-10-08 so a new session can start without re-deciding anything.

## Decisions (agreed with the project owner)

- **Ship the compiled game code in the APK**, the same as the Windows and
  Linux releases (and Banjo Android, WarioWare Twisted Android). The user
  still supplies their own ROM through a picker; the APK contains no ROM,
  textures, audio or other ROM data. Add a packaging guard that fails the
  build if a ROM (`*.z64`, `*.n64`, `*.v64`) ends up in the APK.
- **No code from BanjoRecomp-Android** (or BanjoRecomp). Use the official
  N64ModernRuntime, RT64 and RecompFrontend submodules, with our own
  Android changes kept as patches in `lib-patches/` (applied at configure
  time by `cmake/ApplyLibPatches.cmake`, like the RT64 flicker fix).
  BanjoRecomp-Android may be read only to see *where* changes are needed.
- **App shell**: SDL2's own `android-project` template (zlib license) as
  the skeleton (`SDLActivity` hosts the native game), plus the owner's
  **DesCore** pieces from `wootbeer/gsrandroid` (their own code): the
  launcher activity (ROM picker via the system document picker, copy into
  app storage, hash check), and later the touch overlay. DesCore's video
  and render-thread core does **not** fit (it owns a GLES surface and runs
  the game on its render thread; RT64 renders with Vulkan on its own
  thread), so only the launcher, touch and lifecycle know-how carry over.
- **Phones and handhelds.** First milestone is controller-only on the
  owner's Retroid Pocket 6 (Snapdragon / Adreno); touch controls come next
  so phones work. Reference for touch: DesCore's `descore_touch` (owner's)
  and JRickey/BattleShip's `TouchOverlay.java` / `AnalogStickView.java`
  (MIT).
- **Android Studio workflow must work**: open `android/` in Android Studio,
  pick the USB device, press Run (debug APK installed and launched).

## Project setup

- Location: `android/` in this repo (desktop builds unaffected).
- Match the owner's toolchain (from their gsrandroid project): Android
  Gradle Plugin **9.3.2**, Gradle wrapper **9.5.0**.
- CMake **3.22.1 or newer** from the SDK (our root CMakeLists needs 3.20;
  the owner currently has 3.10.2 for other projects). Pin `version` in
  `externalNativeBuild`.
- NDK r27 or newer (C++20). Pin `ndkVersion`.
- `minSdk 26` (Android 8; Vulkan support before that is unreliable),
  ABI `arm64-v8a` only.
- `externalNativeBuild` points at the repo-root `CMakeLists.txt`, building
  the game as a shared library `libmain.so` (what SDLActivity loads).

## Build-system work (root CMakeLists.txt)

- **Host tools are not cross-compiled.** `N64RecompCLI`, `RSPRecomp` and
  `file_to_c` run at build time; under the Android toolchain they would be
  built for ARM. Use host-built copies (e.g. a `-DBTGA_HOST_TOOLS_DIR=`
  pointing at the desktop build) or require the generated outputs to exist.
- **Pre-generated inputs**: `RecompiledFuncs/` (N64Recomp, done once on the
  desktop), `rsp/n_aspMain.cpp`, `RecompiledPatches/`. Gradle should stop
  with a clear message if they are missing.
- **MIPS patches**: the `make` step for `patches/` must branch on the
  *host* (`CMAKE_HOST_WIN32` -> `wsl.exe make`), not on `WIN32`, which is
  false when targeting Android. Or require `patches.elf` prebuilt.
- Executable target becomes a `SHARED` library on Android; `main()` is
  provided through SDL's `SDL_main`.
- Linux-only bits to guard for Android: GTK / nativefiledialog, `-static-libstdc++`,
  X11. Our `crash_handler.cpp` Linux path (sigaction) works on Android;
  `stack_top` uses `pthread_getattr_np`, available in bionic.

## Library changes needed (as lib-patches)

Found by reading the code; confirm while building.

- **N64ModernRuntime**: compiles for ARM already (`sse2neon`). Window
  handle on Android is already `SDL_Window*`. Needs: app pause/resume
  (backgrounding) so game threads and audio stop cleanly.
- **RecompFrontend**:
  - `recompui/src/util/file.cpp` `get_app_folder_path()`: the Linux branch
    uses `$HOME/.config/<id>`; Android needs app-private storage
    (`SDL_AndroidGetInternalStoragePath()`).
  - ROM selection uses nativefiledialog (no Android support): bridge to the
    Java launcher's picker, or let the launcher pass the ROM path in.
  - Message boxes during startup: use SDL's / Java's instead of desktop ones.
  - UI MSAA may need to be off on Android.
- **RT64**: Vulkan backend (plume) needs the Android surface
  (`VK_KHR_android_surface`) from the SDL window, and swapchain recreation
  when the Surface is destroyed and recreated (pause/resume). RT64's own
  standalone window code says "Android unimplemented", but recomp passes
  its own SDL window, so that path is not used. Shader tools (DXC,
  spirv-cross) run on the host at build time. Our `lib-patches/rt64/`
  flicker fix must still apply.

## Game-specific

- Controller Pak saves (`.mpk`) live in the app folder; consider
  import/export later.
- Settings and saves path: app-private storage; `portable.txt` irrelevant.
- `src/main/main.cpp` window creation: the Linux branch (`SDL_WINDOW_VULKAN`)
  is the Android base; fullscreen, landscape.

## Environment note

`dl.google.com` (Android SDK / NDK downloads) was blocked by this cloud
environment's network policy. The owner added it to Allowed domains on
2026-10-08; the change applies to new sessions. `maven.google.com` and
`services.gradle.org` were already reachable.

## Order of work

1. Install SDK cmdline-tools, NDK, CMake, platform 35 in the sandbox; check
   the root CMake configures with the Android toolchain.
2. Skeleton: SDL android-project + Gradle files, `libmain.so` from the root
   CMake, debug APK builds.
3. Library patches until it links; then the DesCore launcher for the ROM.
4. Owner tests on the Retroid (USB, Android Studio Run), sends `adb logcat`.
5. Touch controls, then phone testing.

## Progress

### Step 1 (2026-10-08, second session)

`dl.google.com` was still refused by the environment's network policy (403
from the proxy), so the SDK / NDK couldn't be installed and the real Android
toolchain hasn't been tried yet. Done meanwhile, verified with a simulated
cross build on Linux (`-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64`,
which sets `CMAKE_CROSSCOMPILING` and reproduced `Exec format error` from the
arm64 `dxc-linux`):

- `cmake/CrossCompile.cmake`: when cross-compiling, `file_to_c` is an imported
  target from `-DBTGA_HOST_TOOLS_DIR=<desktop build folder>` (host `.exe`
  suffix on Windows), and `RecompiledFuncs/`, `RecompiledPatches/patches.c`,
  `patches_bin.c` and `rsp/n_aspMain.cpp` must already exist (clear
  configure error otherwise). The patch and RSP regeneration rules are left
  out, so no N64Recomp, RSPRecomp, MIPS clang or WSL is needed.
- `lib-patches/rt64/0002-host-build-tools.patch`: rt64 picks DXC by host
  (`CMAKE_HOST_WIN32` / `CMAKE_HOST_APPLE` / `CMAKE_HOST_SYSTEM_PROCESSOR`),
  and only builds `file_to_c` if the parent hasn't provided one.
- Root `CMakeLists.txt`: the `DXC` used by recompui's shaders is picked by
  host too; the patches `make` step branches on `CMAKE_HOST_WIN32`.
- Desktop Linux configure and shader build unchanged (same x64 DXC, rt64
  patch applies cleanly from a fresh submodule).

Still to do for step 1, with the NDK (found by reading, not yet confirmed):

- **SDL2**: rt64 does `find_package(SDL2 REQUIRED)` on anything not Windows;
  on Android SDL2 must be built from source (`add_subdirectory` of SDL,
  which the SDL `android-project` also needs for `libSDL2.so`) and
  `SDL2_INCLUDE_DIRS` / `SDL2_LIBRARIES` set before rt64 is added.
- **nativefiledialog-extended**: picks `PLATFORM_LINUX` and requires GTK3
  through pkg-config; rt64 links `nfd` unconditionally. Needs an Android
  stub (lib-patch) or rt64 not linking it on Android.
- **"Linux" checks that miss Android** (`CMAKE_SYSTEM_NAME` is `Android`):
  plume's `PLUME_SDL_VULKAN_ENABLED` option (`IS_LINUX`), rt64's
  `RT64_SDL_WINDOW_VULKAN` definitions, root's SDL2 / Freetype / Threads link
  block, recompui's and recompinput's SDL include dirs
  (`elseif (APPLE OR ... "Linux")`).
- rt64 also builds `texture_hasher` / `texture_packer` executables; harmless
  if they compile for Android, otherwise skip them there.
