cmake_minimum_required(VERSION 3.20)

# NOTE for Windows: this project requires the *64-bit-hosted* clang-cl.exe (Visual Studio ships
# a 32-bit-hosted copy too, under Tools\Llvm\bin instead of Tools\Llvm\x64\bin, and bare PATH
# resolution of "clang-cl" isn't reliable about which one wins -- see BUILDING.md and STATUS.md
# round 27/28). That used to be worked around here by forcing CMAKE_C/CXX_COMPILER_TARGET to
# x86_64-pc-windows-msvc before project(), but that forcing turned out to break clang-cl's
# implicit CRT default-library embedding even on the already-64-bit-hosted binary (lld-link:
# "undefined symbol: mainCRTStartup" on CMake's own trivial compiler self-test, round 28) --
# removed again once BUILDING.md started pinning the exact clang-cl.exe path instead, which
# fixes the actual root cause (which binary runs) without needing to fight its target-triple
# defaults at all.

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")
endif()

project(BattleTanxGARecompiled LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything /W4")
endif()

# Opt out of constexpr mutex constructor on windows to prevent vcredist issues
if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
endif()

if (APPLE)
    enable_language(OBJC OBJCXX)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    option(RECOMP_FLATPAK "Configure the build for Flatpak compatibility." OFF)
endif()



# Avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24:
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (WIN32)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/lib/")
endif()

set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
add_compile_definitions(
    HLSL_CPU
    __PRFCHWINTRIN_H
)

if (RECOMP_FLATPAK)
    add_compile_definitions(RECOMP_FLATPAK)
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/rt64 ${CMAKE_BINARY_DIR}/rt64)

set(BUILD_SHARED_LIBS OFF)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)


target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/rt64/src)

# RecompiledFuncs - Library containing the primary recompiler output
add_library(RecompiledFuncs STATIC)

# clang-cl silently drops bare GNU-style "-include <file>" (two tokens) with just a warning,
# then misparses the orphaned path token as a second input source file, which is a hard error
# ("cannot specify /Fo... when compiling multiple source files") rather than a missing include.
# The MSVC-native spelling is a single "/FI<file>" token. -falign-functions has no MSVC/clang-cl
# equivalent and is dropped too, but only as a (harmless, alignment-only) warning, so it's left
# as-is rather than special-cased.
if(MSVC)
    set(BTGA_RECOMP_HOOKS_FORCE_INCLUDE "SHELL:/FI${CMAKE_SOURCE_DIR}/include/btga_recomp_hooks.h")
else()
    set(BTGA_RECOMP_HOOKS_FORCE_INCLUDE "SHELL:-include ${CMAKE_SOURCE_DIR}/include/btga_recomp_hooks.h")
endif()
target_compile_options(RecompiledFuncs PRIVATE
    # -Wno-unused-but-set-variable
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-implicit-function-declaration
    -falign-functions=16
    # Declarations for the [[patches.hook]] text in battletanx.us.rev0.toml.
    ${BTGA_RECOMP_HOOKS_FORCE_INCLUDE}
)

target_include_directories(RecompiledFuncs PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

file(GLOB FUNC_C_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.c)
file(GLOB FUNC_CXX_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.cpp)

if (NOT FUNC_C_SOURCES AND NOT FUNC_CXX_SOURCES)
    # The recompiled game code is generated from the ROM, which this repository cannot ship, so
    # a fresh clone has none of it yet. Keep the target valid with an empty placeholder so the
    # renderer, runtime, frontend and patches still configure and build; only the game executable
    # needs the generated sources. Run N64Recomp battletanx.us.rev0.toml to produce them.
    message(STATUS "RecompiledFuncs/ is empty - building without the recompiled game code.")
    file(WRITE ${CMAKE_BINARY_DIR}/recompiled_funcs_placeholder.c "")
    set(FUNC_C_SOURCES ${CMAKE_BINARY_DIR}/recompiled_funcs_placeholder.c)
endif()

target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES} ${FUNC_CXX_SOURCES})

# PatchesLib - recompiled patches (RT64 overlay hook via osViSwapBuffer -> recomp_run_ui_callbacks)
add_library(PatchesLib STATIC)
target_compile_options(PatchesLib PRIVATE -fno-strict-aliasing -Wno-unused-variable -Wno-implicit-function-declaration -falign-functions=16)
target_include_directories(PatchesLib PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

# The patch pipeline (patches/*.c cross-compiled to a MIPS ELF, then run back
# through N64Recomp to produce patches.c/patches_bin.c) needs real patch
# source and a patches.toml, neither of which exist yet (see PROGRESS.md
# item 8 -- this is game behavior needing source-level rewriting, e.g. how
# the UI is driven each frame, that hasn't been reverse-engineered). Keep
# PatchesLib valid with an empty placeholder until then, same approach as
# RecompiledFuncs/BattleTanxGARecompiled below for their own not-yet-written
# sources.
file(GLOB PATCH_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/patches/*.c)
if (NOT PATCH_SOURCES OR NOT EXISTS ${CMAKE_SOURCE_DIR}/patches.toml)
    message(STATUS "patches/ has no sources yet (or patches.toml is missing) - building PatchesLib as a placeholder only.")
    file(WRITE ${CMAKE_BINARY_DIR}/patcheslib_placeholder.c "")
    target_sources(PatchesLib PRIVATE ${CMAKE_BINARY_DIR}/patcheslib_placeholder.c)
else()
    # patches.c is generated by N64Recomp patches.toml from patches/patches.elf
    # patches_bin.c is generated by file_to_c from patches/patches.bin (embed)
    target_sources(PatchesLib PRIVATE ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c PROPERTIES COMPILE_FLAGS -fno-strict-aliasing)
    add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/patches/patches.bin ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches.c ${CMAKE_SOURCE_DIR}/RecompiledPatches/recomp_overlays.inl ${CMAKE_SOURCE_DIR}/RecompiledPatches/funcs.h
        COMMAND $<TARGET_FILE:N64RecompCLI> patches.toml
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${CMAKE_SOURCE_DIR}/patches/patches.elf N64RecompCLI
    )

    # patches.elf is built by the Makefile in patches/, with clang targeting MIPS.
    set(PATCHES_C_COMPILER "clang" CACHE STRING "C compiler used to build the game patches")
    set(PATCHES_LD "ld.lld" CACHE STRING "Linker used to build the game patches")
    file(GLOB PATCH_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/patches/*.h)
    add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/patches/patches.elf
        COMMAND make CC=${PATCHES_C_COMPILER} LD=${PATCHES_LD}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/patches
        DEPENDS ${PATCH_SOURCES} ${PATCH_HEADERS} ${CMAKE_SOURCE_DIR}/patches/Makefile ${CMAKE_SOURCE_DIR}/patches/patches.ld ${CMAKE_SOURCE_DIR}/patches/syms.ld
    )

    # patches.bin is embedded in the executable.
    add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.h
        COMMAND file_to_c ${CMAKE_SOURCE_DIR}/patches/patches.bin btx_patches_bin ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.c ${CMAKE_SOURCE_DIR}/RecompiledPatches/patches_bin.h
        DEPENDS ${CMAKE_SOURCE_DIR}/patches/patches.bin file_to_c
    )
endif()


# Main executable
add_executable(BattleTanxGARecompiled)

# None of this game's own source exists yet -- see STATUS.md/PROGRESS.md.
# `bdragoncore/battle-tanx-recomp`'s equivalent list (src/main/*.cpp,
# src/game/*.cpp, rsp/*.cpp) is deeply specific to that game's own discovered
# function addresses and RSP microcode, so it isn't something to port ahead
# of doing the same reverse-engineering work against this ROM. List real
# files here as they're written; until then, keep the target configurable
# with an empty placeholder so the rest of this file (RT64/N64ModernRuntime/
# RecompFrontend wiring) can still be exercised.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/main/*.cpp
    ${CMAKE_SOURCE_DIR}/src/game/*.cpp
    ${CMAKE_SOURCE_DIR}/rsp/*.cpp
)
if (NOT SOURCES)
    message(STATUS "src/ and rsp/ are empty - building BattleTanxGARecompiled as a placeholder only.")
    file(WRITE ${CMAKE_BINARY_DIR}/battletanxga_placeholder.cpp
        "int main() { return 0; } // No game source yet -- see STATUS.md/PROGRESS.md.\n")
    set(SOURCES ${CMAKE_BINARY_DIR}/battletanxga_placeholder.cpp)
    set(BTGA_FORKED_RUNTIME OFF)
else()
    # The compat shims (src/game/stock_runtime_compat.cpp, controller_pak.cpp and the force-included
    # include/rsp_stock_compat.hpp) provide things a game-specific runtime fork would otherwise
    # already have, such as osSp*Status_recomp, SI/PFS and the SP_STATUS/DPC helpers, and building
    # both gives redefinition and duplicate symbol errors. Detect a fork by the SP_STATUS helpers it
    # would add to librecomp/rsp.hpp and leave the shims out.
    file(READ ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include/librecomp/rsp.hpp BTGA_RSP_HPP)
    if(BTGA_RSP_HPP MATCHES "get_sp_status")
        set(BTGA_FORKED_RUNTIME ON)
        message(STATUS "Forked N64ModernRuntime detected -- stock compat shims disabled")
        target_compile_definitions(BattleTanxGARecompiled PRIVATE BTGA_FORKED_RUNTIME)
        list(REMOVE_ITEM SOURCES
            ${CMAKE_SOURCE_DIR}/src/game/stock_runtime_compat.cpp
            ${CMAKE_SOURCE_DIR}/src/game/controller_pak.cpp)
    else()
        set(BTGA_FORKED_RUNTIME OFF)
        # On the stock runtime the game will need whatever [[patches.hook]] entries this project's
        # own TOML config ends up requiring (see bdragoncore/battle-tanx-recomp's
        # cmake/check_stock_funcs.cmake and battletanx.us.rev0.toml for the shape of this on that
        # project) -- not yet meaningful here since that config doesn't exist yet.
        if(EXISTS ${CMAKE_SOURCE_DIR}/rsp/battletanx_audio.cpp)
            # See the BTGA_RECOMP_HOOKS_FORCE_INCLUDE comment above: clang-cl needs "/FI<file>",
            # not GNU "-include;<file>", or it misparses the header path as a second source file.
            if(MSVC)
                set(BTGA_RSP_COMPAT_FORCE_INCLUDE "/FI${CMAKE_SOURCE_DIR}/include/rsp_stock_compat.hpp")
            else()
                set(BTGA_RSP_COMPAT_FORCE_INCLUDE "-include;${CMAKE_SOURCE_DIR}/include/rsp_stock_compat.hpp")
            endif()
            set_source_files_properties(${CMAKE_SOURCE_DIR}/rsp/battletanx_audio.cpp ${CMAKE_SOURCE_DIR}/rsp/f3dex.cpp
                PROPERTIES COMPILE_OPTIONS "${BTGA_RSP_COMPAT_FORCE_INCLUDE}")
        endif()
    endif()
endif()

target_include_directories(BattleTanxGARecompiled PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty/sse2neon
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/hlslpp/include
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/inc
    ${CMAKE_SOURCE_DIR}/lib/rt64/src
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/rhi
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/render
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompinput/include
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/include
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/src
    ${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/nativefiledialog-extended/src/include
    ${CMAKE_SOURCE_DIR}/lib/SlotMap
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Generate icon_bytes.c from the app icon PNG, once this project has its own
# (not the original BattleTanx's -- see PROGRESS.md).
if (EXISTS ${CMAKE_SOURCE_DIR}/icons/app.png)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
        COMMAND file_to_c ${CMAKE_SOURCE_DIR}/icons/app.png icon_bytes ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
        DEPENDS ${CMAKE_SOURCE_DIR}/icons/app.png
    )
    target_sources(BattleTanxGARecompiled PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_options(BattleTanxGARecompiled PRIVATE
        -march=nehalem
        -fno-strict-aliasing
        -fms-extensions
    )
else()
    target_compile_options(BattleTanxGARecompiled PRIVATE
        -fno-strict-aliasing
        -fms-extensions
    )
endif()

if (MSVC)
    # Disable identical code folding, since this breaks mod function patching as multiple functions can get merged into one.
    target_link_options(BattleTanxGARecompiled PRIVATE /OPT:NOICF)
elseif (APPLE)
    # Use a wrapper around ld64 that respects segprot's `max_prot` value in order
    # to make our executable memory writable (required for mod function patching)
    target_link_options(BattleTanxGARecompiled PRIVATE
        "-fuse-ld=${CMAKE_SOURCE_DIR}/.github/macos/ld64"
    )
endif()

if (WIN32)
    include(FetchContent)

    if (DEFINED ENV{SDL2_VERSION})
        set(SDL2_VERSION $ENV{SDL2_VERSION})
    else()
        set(SDL2_VERSION "2.30.3")
    endif()

    # Fetch SDL2 on windows
    FetchContent_Declare(
        sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip
    )
    FetchContent_MakeAvailable(sdl2)
    target_include_directories(BattleTanxGARecompiled PRIVATE
        ${sdl2_SOURCE_DIR}/include
    )
    target_link_directories(BattleTanxGARecompiled PRIVATE
        ${sdl2_SOURCE_DIR}/lib/x64
    )

    # Copy SDL2 and dxc DLLs to output folder as post build step
    add_custom_command(TARGET BattleTanxGARecompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxil.dll"
            "${CMAKE_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            $<TARGET_FILE_DIR:BattleTanxGARecompiled>)

    set_target_properties(
        BattleTanxGARecompiled
        PROPERTIES
            LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
            LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_RELWITHDEBINFO  "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
    )

    # icons/app.rc (the .exe icon resource) doesn't exist yet -- this
    # project has no game art of its own yet (asset extraction from the ROM
    # hasn't started, see PROGRESS.md). Same conditional-on-existence
    # approach as icons/app.png below; add app.rc once there's an icon to
    # embed.
    if (EXISTS ${CMAKE_SOURCE_DIR}/icons/app.rc)
        target_sources(BattleTanxGARecompiled PRIVATE ${CMAKE_SOURCE_DIR}/icons/app.rc)
    endif()
    target_link_libraries(BattleTanxGARecompiled PRIVATE SDL2 Winmm.lib)
endif()

if (APPLE)
    find_package(SDL2 REQUIRED)
    target_include_directories(BattleTanxGARecompiled PRIVATE ${SDL2_INCLUDE_DIRS})

    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)

    target_link_libraries(BattleTanxGARecompiled PRIVATE ${CMAKE_DL_LIBS} Threads::Threads SDL2::SDL2)

    include(${CMAKE_SOURCE_DIR}/.github/macos/apple_bundle.cmake)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SDL2 REQUIRED)

    add_compile_definitions("PLUME_SDL_VULKAN_ENABLED")
    add_compile_definitions("RT64_SDL_WINDOW_VULKAN")

    message(STATUS "SDL2_FOUND = ${SDL2_FOUND}")
    message(STATUS "SDL2_INCLUDE_DIRS = ${SDL2_INCLUDE_DIRS}")

    target_include_directories(BattleTanxGARecompiled PRIVATE ${SDL2_INCLUDE_DIRS})

    find_package(Freetype REQUIRED)

    message(STATUS "FREETYPE_FOUND = ${FREETYPE_FOUND}")
    message(STATUS "FREETYPE_INCLUDE_DIRS = ${FREETYPE_INCLUDE_DIRS}")
    message(STATUS "FREETYPE_LIBRARIES = ${FREETYPE_LIBRARIES}")

    include_directories(${FREETYPE_LIBRARIES})
    target_link_libraries(BattleTanxGARecompiled PRIVATE ${FREETYPE_LIBRARIES} SDL2::SDL2 X11)

    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)

    target_link_libraries(BattleTanxGARecompiled PRIVATE "-latomic -static-libstdc++" ${CMAKE_DL_LIBS} Threads::Threads)
endif()

# TODO fix the rt64 CMake script so that this doesn't need to be duplicated here
# For DXC
set (DXC_COMMON_OPTS "-I${PROJECT_SOURCE_DIR}/src")
set (DXC_DXIL_OPTS "-Wno-ignored-attributes")
set (DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set (DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_0" "-D DYNAMIC_RENDER_PARAMS")
set (DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_0" "-D DYNAMIC_RENDER_PARAMS" "-fvk-invert-y")
set (DXC_CS_OPTS "${DXC_COMMON_OPTS}" "-E" "CSMain" "-T cs_6_0")
set (DXC_GS_OPTS "${DXC_COMMON_OPTS}" "-E" "GSMain" "-T gs_6_0")
set (DXC_RT_OPTS "${DXC_COMMON_OPTS}" "-D" "RT_SHADER" "-T" "lib_6_3" "-fspv-target-env=vulkan1.1spirv1.4" "-fspv-extension=SPV_KHR_ray_tracing" "-fspv-extension=SPV_EXT_descriptor_indexing")

if (${WIN32})
    set (DXC "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc.exe")
    add_compile_definitions(NOMINMAX)
else()
    if (APPLE)
        # Apple's binary is universal, so it'll work on both x86_64 and arm64
        set (DXC "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/arm64" "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/arm64/dxc-macos")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set(SPIRVCROSS "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/spirv-cross/lib/x64" "${PROJECT_SOURCE_DIR}/lib/rt64//src/contrib/spirv-cross/bin/x64/spirv-cross")
        else()
            set(SPIRVCROSS "DYLD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/spirv-cross/lib/arm64" "${PROJECT_SOURCE_DIR}/lib/rt64//src/contrib/spirv-cross/bin/x64/spirv-cross")
        endif()
    else()
        if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set (DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/x64" "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc-linux")
        else()
            set (DXC "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/arm64" "${PROJECT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/arm64/dxc-linux")
        endif()
    endif()
endif()

set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)
set(RECOMP_FRONTEND_RT64_PATH ${CMAKE_SOURCE_DIR}/lib/rt64)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/RecompFrontend)

# Upstream RmlUi 6.0 uses uint32_t and friends without including <cstdint>, which clang with a
# current libstdc++ rejects, and recompui includes those headers. Add the include from here
# rather than patching the submodule. clang-cl (MSVC frontend variant, incl. Windows) needs the
# native /FI spelling instead of GNU -include: the latter's quote-style header lookup fails to
# resolve bare STL header names (e.g. "cstdint") when clang-cl is generating a precompiled header,
# such as RmlUi's own cmake_pch.cxx, even though INCLUDE-based system search works everywhere else.
if(MSVC)
    set(BTGA_CSTDINT_FORCE_INCLUDE "/FI cstdint")
else()
    set(BTGA_CSTDINT_FORCE_INCLUDE "-include cstdint")
endif()
foreach(_btx_rml_target rmlui_core rmlui_debugger recompui)
    if(TARGET ${_btx_rml_target})
        target_compile_options(${_btx_rml_target} PRIVATE "SHELL:$<$<COMPILE_LANGUAGE:CXX>:${BTGA_CSTDINT_FORCE_INCLUDE}>")
    endif()
endforeach()

# These libraries reference each other's symbols in both directions (e.g.
# recompinput's player-assignment code calls into recompui's
# ConfigPageControls, RecompiledFuncs calls into librecomp/ultramodern's
# os*_recomp reimplementations) without CMake knowing about the cycle at
# the target level, so a single left-to-right archive scan can leave real,
# resolvable symbols undefined depending on which .o a given archive
# happens to pull in first. --start-group/--end-group makes the linker
# keep re-scanning this whole set until nothing new resolves, same fix as
# for any other circular static-library dependency. This has to be a single
# target_link_libraries call with the group flags as literal arguments --
# CMake collects link *options* (target_link_options) and link
# *libraries* into separate lists and concatenates them at fixed positions
# in the final command line regardless of call order, so two separate
# target_link_options calls do NOT end up wrapping the libraries below.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    set(BTGA_LINK_GROUP_START "-Wl,--start-group")
    set(BTGA_LINK_GROUP_END "-Wl,--end-group")
else()
    set(BTGA_LINK_GROUP_START "")
    set(BTGA_LINK_GROUP_END "")
endif()

target_link_libraries(BattleTanxGARecompiled PRIVATE
    ${BTGA_LINK_GROUP_START}
    # PatchesLib must come before RecompiledFuncs. Both define the functions that a RECOMP_PATCH
    # replaces (for example the patched func_80079530) as weak symbols, and the linker keeps the
    # first definition it finds, so listing RecompiledFuncs first would silently drop the patches.
    PatchesLib
    RecompiledFuncs
    recompui
    recompinput
    librecomp
    ultramodern
    rt64
    nfd
    ${BTGA_LINK_GROUP_END}
)


# Embed all .nrm files in the "mods" directory
file(GLOB NRM_FILES "${CMAKE_SOURCE_DIR}/mods/*.nrm")

set(GENERATED_NRM_SOURCES "")

foreach(NRM_FILE ${NRM_FILES})
    get_filename_component(NRM_NAME ${NRM_FILE} NAME_WE)
    set(OUT_C "${CMAKE_CURRENT_BINARY_DIR}/mods/${NRM_NAME}.c")
    set(OUT_H "${CMAKE_CURRENT_BINARY_DIR}/mods/${NRM_NAME}.h")

    add_custom_command(
        OUTPUT ${OUT_C} ${OUT_H}
        COMMAND file_to_c ${NRM_FILE} ${NRM_NAME} ${OUT_C} ${OUT_H}
        DEPENDS ${NRM_FILE}
    )

    list(APPEND GENERATED_NRM_SOURCES ${OUT_C})
endforeach()

target_sources(BattleTanxGARecompiled PRIVATE ${GENERATED_NRM_SOURCES})

target_sources(BattleTanxGARecompiled PRIVATE ${SOURCES})

set_property(TARGET BattleTanxGARecompiled PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
