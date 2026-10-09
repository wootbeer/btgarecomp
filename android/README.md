# Android build

Work in progress (see [PLAN.md](PLAN.md)). The app is SDL's `SDLActivity` hosting the game,
built by Android Studio from the repo-root `CMakeLists.txt` as `libmain.so`. Like the
Windows and Linux releases, the APK carries the compiled game code but no ROM: the build
fails if a file in it is named or headed like an N64 ROM.

## One-time setup

1. **Build the desktop version first, with the ROM in place** ([BUILDING.md](../BUILDING.md)).
   The Android build reuses two things from it:
   - the files generated from the ROM: `RecompiledFuncs/`, `RecompiledPatches/` and
     `rsp/n_aspMain.cpp`, in the checkout itself;
   - `file_to_c(.exe)`, a build tool that ends up in the desktop build folder (`build/`).

   If the Android build is in a separate checkout or worktree, copy `RecompiledFuncs/`,
   `RecompiledPatches/` and `rsp/` into it, and run `git submodule update --init --recursive`
   there.

2. **Android Studio, SDK Manager, SDK Tools** (tick "Show Package Details"):
   NDK **28.2.13676358** and CMake **3.31.6**. Gradle tries to install these itself when
   missing, but the SDK Manager is the reliable way.

3. **`android/local.properties`** (Android Studio creates it with `sdk.dir`; it isn't
   committed). Add the desktop build folder, with forward slashes:

       btga.hostToolsDir=C:/Users/<you>/source/repos/btgarecomp/btgarecomp/build

## Build and run

Open the `android/` folder in Android Studio, let the Gradle sync finish, pick the device
(USB debugging on), press **Run**. The debug build compiles the game optimised (with debug
info), so the first build takes a while.

From a terminal instead: `gradlew assembleDebug` in `android/` (the APK lands in
`app/build/outputs/apk/debug/`).

Requirements: Android 9 or newer (API 28), 64-bit ARM, Vulkan.

On first launch the app asks for the ROM (system file picker; .z64, .v64 or .n64). It's
validated like on desktop and copied into the app's private storage, which is excluded
from backups and device transfers.

## Layout

- `app/src/main/java/org/libsdl/app/`: SDL's Java sources, unmodified, from SDL 2.32.10
  (zlib license, `licenses/SDL2.txt`). Must match the SDL version in `cmake/Android.cmake`.
- `app/src/main/java/io/github/wootbeer/btgarecomp/BattleTanxActivity.java`: the activity.
- `cmake/Android.cmake`, `cmake/CrossCompile.cmake` (repo root): the Android side of the
  native build.

## Vulkan validation (testing aid)

To have Khronos' Vulkan validation layer check what RT64 sends the GPU driver, add
`btga.vulkanValidation=true` to `android/local.properties` and press Run. The debug APK then
carries the layer (about 22 MB, downloaded once and checked against a fixed SHA-256). Then turn on
Android's GPU debug layers for the app (phone plugged in, `adb` from the SDK's `platform-tools`):

    adb shell settings put global enable_gpu_debug_layers 1
    adb shell settings put global gpu_debug_app io.github.wootbeer.btgarecomp
    adb shell settings put global gpu_debug_layers VK_LAYER_KHRONOS_validation
    adb shell setprop debug.vulkan.khronos_validation.enables VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT

Restart the game; the layer's reports go to logcat (search for `Validation`). It slows the game
down. To turn it off again:

    adb shell settings delete global enable_gpu_debug_layers
    adb shell settings delete global gpu_debug_app
    adb shell settings delete global gpu_debug_layers
    adb shell setprop debug.vulkan.khronos_validation.enables ""

and remove the line from `local.properties`.
