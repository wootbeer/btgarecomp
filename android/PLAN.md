# Android port: plan

Status: steps 1 and 2 done; step 3 started (see Progress). Next: the owner
builds `libmain.so` with the real generated sources (it can't be linked in the
sandbox, which has no ROM), then the ROM launcher.
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
- NDK r27 or newer (C++20). Pin `ndkVersion`. Step 1 used **NDK r28c
  (28.2.13676358)** and SDK **CMake 3.31.6** (not 4.x: CMake 4 rejects the
  old `cmake_minimum_required` versions in several submodules).
- `minSdk 28` (Android 9), ABI `arm64-v8a` only. Was 26; raised in step 1
  because `lib/SlotMap` uses `aligned_alloc`, which bionic has from API 28.
  `cmake/Android.cmake` stops the configure below 28.
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

### Step 1 (2026-10-08) -- done

Installed in the sandbox under `/opt/android-sdk`: cmdline-tools, platform-tools,
`platforms;android-35`, `ndk;28.2.13676358`, `cmake;3.31.6` (needs `dl.google.com`
allowed; the session used Full network access).

Configure command used (host tools from a desktop build folder, see below):

    cmake -S . -B build-android -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_HOME/ndk/28.2.13676358/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
      -DBTGA_HOST_TOOLS_DIR=<desktop build folder>

Result: configures cleanly. With stand-in (empty) generated sources, `ninja -k 0`
builds everything, `libSDL2.so` included, except the two step-3 items at the end.

What was added:

- `cmake/CrossCompile.cmake`: when cross-compiling, `file_to_c` is an imported
  target from `BTGA_HOST_TOOLS_DIR` (`.exe` on a Windows host), and
  `RecompiledFuncs/`, `RecompiledPatches/patches.c`, `patches_bin.c` and
  `rsp/n_aspMain.cpp` must already exist (configure error listing what's
  missing). No N64Recomp, RSPRecomp, MIPS clang or WSL is needed.
- `cmake/Android.cmake` (only when `ANDROID`): SDL2 2.32.10 (shared,
  `libSDL2.so`) and FreeType 2.13.3 fetched and built from source, SHA256-pinned;
  `cmake/android/FindSDL2.cmake` / `FindFreetype.cmake` hand them to rt64's,
  RmlUi's and our `find_package()`; zstd's dictionary builder and program off
  (need `qsort_r`, API 36); our own `nfd` target from
  `src/android/nfd_android.cpp` (every dialog returns an error); API 28 check.
- `lib-patches/rt64/0002-host-tools-and-parent-provided-targets.patch`: DXC
  picked by host OS/CPU; `file_to_c` and `nfd` only built when the parent
  project hasn't provided them.
- Root `CMakeLists.txt`: recompui's DXC picked by host; patches `make` step on
  `CMAKE_HOST_WIN32`; no regeneration rules when cross-compiling; on Android the
  game is `SHARED` with `OUTPUT_NAME main` (`libmain.so`), links SDL2, FreeType,
  `android`, `log`, and adds the SDL include folders that rt64, recompui and
  recompinput only add on desktop platforms.
- Desktop Linux configure/build unchanged (rt64 patch applies from a fresh
  submodule; real nfd; `src/android/` isn't compiled). Windows not re-tested.

Notes:

- plume already supports Android natively: on `__ANDROID__` its `RenderWindow`
  is `ANativeWindow*` and it uses `VK_KHR_android_surface`
  (`PLUME_SDL_VULKAN_ENABLED` stays off there). So the game passes the
  `ANativeWindow*` from `SDL_GetWindowWMInfo` (`info.android.window`), not
  the `SDL_Window*`.
- The SDL Java sources for step 2 must come from the same SDL release
  (2.32.10, `android-project/app/src/main/java/org/libsdl/app/`).
- The GitHub archive URL for FreeType was refused (403) here; the official
  savannah tarball works.

Left for step 3 (the only compile errors):

- `lib/rt64/src/hle/rt64_application_window.cpp` includes
  `X11/extensions/Xrandr.h` on any non-Windows, non-Apple target; needs an
  Android branch (lib-patch).
- `lib/RecompFrontend/recompui/src/renderer/rt64_render_context.cpp:229`
  assigns ultramodern's `WindowHandle` (`SDL_Window*`) to plume's
  `RenderWindow` (`ANativeWindow*` on Android); convert there (lib-patch).

### Step 2 (2026-10-08) -- done

- `android/`: Gradle project (Kotlin DSL), AGP 9.3.2, Gradle wrapper 9.5.0
  (distribution SHA256-pinned), `compileSdk`/`targetSdk` 35, `minSdk` 28,
  `arm64-v8a`, `ndkVersion` 28.2.13676358, CMake 3.31.6, `ANDROID_STL=c++_shared`.
  `externalNativeBuild` points at the repo-root `CMakeLists.txt` and only builds
  the `BattleTanxGARecompiled` (`libmain.so`) and `SDL2` targets. The debug
  variant passes `CMAKE_BUILD_TYPE=RelWithDebInfo` (AGP honours it: `-O2 -g`),
  since `-O0` recompiled code is unplayable.
- `btga.hostToolsDir` from `android/local.properties` (or `-P`) becomes
  `-DBTGA_HOST_TOOLS_DIR`; without it the CMake configure says what to set.
- App id / namespace `io.github.wootbeer.btgarecomp`; activity
  `BattleTanxActivity extends SDLActivity`, loads `SDL2` + `main`, and returns
  `"main"` from `getMainFunction()` because `src/main/main.cpp` uses
  `SDL_MAIN_HANDLED` (SDL dlsyms the name and has already called
  `SDL_SetMainReady()`). Fullscreen, `sensorLandscape`. Icon from `icons/app.png`.
- SDL 2.32.10 Java sources unmodified in `app/src/main/java/org/libsdl/app/`,
  license in `licenses/SDL2.txt`.
- ROM guard: `check<Variant>ApkHasNoRom` runs before every `assemble<Variant>`
  and fails on any APK entry named `.z64/.n64/.v64` or starting with an N64 ROM
  header in any byte order (tested: a `.Z64` asset and a renamed headed file
  both fail the build).
- `android/README.md`: setup and Android Studio steps for the owner.
- Verified in the sandbox: `gradlew assembleDebug` configures and compiles all
  libraries; with `targets` cut to `SDL2` the APK packages (`libSDL2.so`,
  `libc++_shared.so`, dex). `libmain.so` needs the real `RecompiledFuncs/` etc.

### Step 3 (started)

- `lib-patches/rt64/0003-android-application-window.patch`: no X11 on Android
  (Android also defines `__linux__`); RT64's own SDL window path works there
  (handle from `wmInfo.info.android.window`); refresh rate from SDL's display
  mode; window never "moves".
- `lib-patches/RecompFrontend/0001-android-native-window.patch` (applied from
  the root `CMakeLists.txt` like the rt64 ones): `RT64Context` hands plume the
  `ANativeWindow*` behind the SDL window instead of the `SDL_Window*`.
- Desktop Linux still compiles both patched files.

Next in step 3:

- Owner: build on the PC with the real generated sources; send the first
  link errors, if any.
- App folder (`get_app_folder_path()` -> app-private storage), ROM selection
  (DesCore launcher passes the ROM in; nfd stand-in returns errors today),
  startup message boxes, pause/resume (surface destroyed/recreated: the
  `ANativeWindow*` handed to plume goes stale).

### Step 4, first device run (2026-10-08)

Owner built on the PC (real generated sources) and ran it on the Retroid
Pocket 6: `libmain.so` linked, SDL started `main`, audio opened, the window
came up, then `recomp::mods::initialize_mods()` threw from
`create_directories` (the app folder was `$HOME/.config/btgarecomp`).

Fixed:

- `lib-patches/RecompFrontend/0002-android-app-folder.patch`:
  `get_app_folder_path()` is `SDL_AndroidGetInternalStoragePath()`
  (`/data/data/<package>/files`) on Android.
- `src/android/android_startup.cpp` (`btga::android::startup()`, first thing in
  `main()`): stdout/stderr go to logcat (tag `BTGA`), and the working directory
  is the same private folder, so `assets/...` and `crash_log.txt` resolve there.
- The repo's `assets/` folder is packaged as the APK's assets;
  `BattleTanxActivity` copies it to `files/assets/` whenever the app was
  installed or updated since the last copy (stamp: `lastUpdateTime`).

Second run: got past that; `RT64Context` then threw creating `/data/.rt64`
(RT64's `UserPaths::detectDataPath()` uses `$HOME`, which is `/data`).
`lib-patches/rt64/0004-android-user-paths.patch`: on Android RT64's data folder
is `<private storage>/rt64`. No other `$HOME`, `/tmp` or temp-dir lookups remain
in the libraries or the game.

Third run (after making lib patches re-trigger the configure, see
`cmake/ApplyLibPatches.cmake`): Vulkan device created on the Adreno 740, all
fonts loaded from `files/assets/`, then `vkCreateAndroidSurfaceKHR` failed
(`native_window_api_connect: already connected`). SDL2 on Android defaults a
window with no graphics flag to OpenGL and connects an EGL surface to it.
`src/main/main.cpp` now creates the window with `SDL_WINDOW_VULKAN` on Android.
The later `vkAllocateDescriptorSets` errors and the segfault on "RT64 Present"
followed from having no swap chain; recheck after this fix.

Fourth run: no crash, black screen. `No compatible surface formats were found`:
RT64 asks for a `B8G8R8A8_UNORM` swap chain, Android surfaces offer RGBA8.
`lib-patches/rt64/0005-android-swap-chain-format.patch` (swap chain + the video
interface pipelines) and `lib-patches/RecompFrontend/0003-android-swap-chain-format.patch`
(recompui's `SwapChainFormat`) use `R8G8B8A8_UNORM` on Android. Without a swap
chain the Gfx thread spun at ~98% CPU until Android's ANR.

Watch: `vkAllocateDescriptorSets failed with error code 0xC4642878` (twice,
in runs three and four) is `VK_ERROR_OUT_OF_POOL_MEMORY`. Plume sizes each
set's pool to its own counts (boundless ranges via variable descriptor count,
correct per spec), so it may be Adreno-specific; plume is a nested submodule
of rt64 (`lib/rt64/src/contrib/plume`), so a fix there needs its own patch
plumbing. Recheck once the swap chain exists.

Fifth run: the launcher menu draws and is navigable with the controller.
"Load ROM" does nothing (nfd stand-in).

ROM import (instead of DesCore's launcher, which the plan had for this): the
game already validates and stores ROMs (`recomp::select_rom`: byte order, hash,
writes `<app folder>/btga.n64.us.1.0.z64`), so the Java side only delivers the
file.

- `RomPickerActivity` is the launcher activity. With a stored ROM it starts the
  game at once; otherwise a dialog, then `ACTION_OPEN_DOCUMENT`, then the file
  is copied to `files/rom-import.bin` and the game starts.
- `btga::android::import_picked_rom()` (called in `main()` after the games
  are registered, before `recomp::start()`) runs `select_rom` on it, deletes
  it, and shows an SDL message box saying what's wrong if it isn't the ROM.
- Picking happens before the game runs because a picker covering the game
  destroys its surface, and the renderer can't recover from that yet (see
  pause/resume below). The in-game "Load ROM" button still does nothing.
- `allowBackup="false"` plus `dataExtractionRules` excluding everything, so the
  stored ROM never leaves the device through backup or device transfer.

Sixth run: ROM imported, the game started (`Initializing recomp heap`, a blip
of sound), then a third `VK_ERROR_OUT_OF_POOL_MEMORY` and a segfault reading
0x0 on the Gfx thread. The only variable-count ("boundless") descriptor set is
RT64's `FramebufferRendererDescriptorTextureSet` (layout count 8192, pool sized
to the textures in use); the Adreno driver evidently wants the pool to hold
the layout's full count. `lib-patches/plume/0001-android-boundless-descriptor-pool.patch`
sizes it so on Android. `btga_apply_lib_patches()` takes an optional folder
for nested submodules (`plume` is `lib/rt64/src/contrib/plume`).

The owner also reports the built-in controls don't drive the menus.
`btga::android::startup()` now logs input devices as SDL sees them (game
controller / joystick with or without mapping) and the first 30 presses
(`Input: ...` lines, tag BTGA).

Seventh run: the game runs (no descriptor errors since the plume patch).
Input: SDL only ever adds "Android Accelerometer" as a joystick; the built-in
pad is never added and no press reaches SDL, not even as a key. SDL's Java
filter (`isDeviceSDLJoystick`) is standard, so the next build logs, in Java,
every Android input device (name, id, sources) and the first 40 key/motion
events the activity receives (tag BTGA).

Next:

- Pause/resume: on `surfaceDestroyed` the `ANativeWindow*` plume holds goes
  stale (home button, notifications, any covering activity). Needs the game to
  stop presenting and the swap chain (and its `VkSurfaceKHR`) recreated on the
  new window.
- Re-picking a ROM: today only by clearing the app's storage.
- Touch controls.

