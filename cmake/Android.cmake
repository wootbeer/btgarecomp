# Android build (android/PLAN.md). The NDK has no SDL2 or FreeType, so both are built from
# source here, and cmake/android/ supplies Find modules that hand the built targets to rt64's,
# RmlUi's and our own find_package() calls. SDL2 is a shared library, libSDL2.so, which
# SDLActivity loads before the game's libmain.so.

if (NOT ANDROID)
    return()
endif()

# API 28 (Android 9) is the oldest with aligned_alloc, which lib/SlotMap uses.
if (ANDROID_PLATFORM_LEVEL LESS 28)
    message(FATAL_ERROR "The Android build needs API level 28 or newer (minSdk 28), not ${ANDROID_PLATFORM_LEVEL}.")
endif()

# rt64 only uses the zstd library. Its dictionary builder (and the zstd program, which needs it)
# would need qsort_r, which bionic only has from API 36.
set(ZSTD_BUILD_DICTBUILDER OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)

# Clang on arm64 fuses a*b+c into one FMA by default, rounding once instead of twice. The N64
# (no FMA) and the desktop builds (no FMA by default) round twice; keep Android the same, for the
# recompiled game maths and for RT64's matrix decomposition used by frame interpolation.
add_compile_options(-ffp-contract=off)

# EXPERIMENT (Android port, interpolation jitter): RT64's maths library hlsl++ has an ARM NEON
# path; frame interpolation jitters on the device and is smooth on the PC (SSE path), even with
# every model matched by ID. Use hlsl++'s portable scalar path everywhere, to see whether the
# NEON path is the difference. Must be the same for every target (its types cross libraries).
add_compile_definitions(HLSLPP_SCALAR)

include(FetchContent)

# The SDL Java sources in the Android app (org.libsdl.app) must come from this same release.
set(BTGA_ANDROID_SDL2_VERSION "2.32.10")
FetchContent_Declare(sdl2
    URL https://github.com/libsdl-org/SDL/releases/download/release-${BTGA_ANDROID_SDL2_VERSION}/SDL2-${BTGA_ANDROID_SDL2_VERSION}.tar.gz
    URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sdl2)

set(BTGA_ANDROID_FREETYPE_VERSION "2.13.3")
FetchContent_Declare(freetype
    URL https://download.savannah.gnu.org/releases/freetype/freetype-${BTGA_ANDROID_FREETYPE_VERSION}.tar.xz
    URL_HASH SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289
)
foreach (dep IN ITEMS ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${dep} ON CACHE BOOL "" FORCE)
endforeach()
# Static FreeType (it follows BUILD_SHARED_LIBS, which the root only sets OFF later).
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(freetype)
unset(BUILD_SHARED_LIBS)

# nativefiledialog-extended has no Android backend; rt64 takes this nfd instead of building its own.
add_library(nfd STATIC "${CMAKE_SOURCE_DIR}/src/android/nfd_android.cpp")
target_include_directories(nfd PUBLIC "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/nativefiledialog-extended/src/include")

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/android")
# Sets SDL2_INCLUDE_DIRS here too (rt64's find_package() only sets them in its own scope).
find_package(SDL2 REQUIRED)
