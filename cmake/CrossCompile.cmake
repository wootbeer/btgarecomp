# Cross-compiling (the Android build, see android/PLAN.md).
#
# Some tools run during the build: file_to_c (shaders, icon, mods), N64Recomp
# and RSPRecomp (game code and audio microcode from the ROM), and the MIPS
# toolchain for patches/. Built by this project they would be built for the
# target, which the build machine can't run. So when cross-compiling:
#
# - file_to_c comes from a desktop build of this project, through
#   BTGA_HOST_TOOLS_DIR (its build folder, where file_to_c ends up), and is
#   given to rt64 and the rules below as an imported target.
# - The ROM-derived sources aren't regenerated: RecompiledFuncs/,
#   RecompiledPatches/ and rsp/n_aspMain.cpp must already exist, made by a
#   desktop build with the ROM in place.

if (NOT CMAKE_CROSSCOMPILING)
    return()
endif()

set(BTGA_HOST_TOOLS_DIR "" CACHE PATH "Build folder of a desktop build of this project, for its file_to_c (needed when cross-compiling)")

if (CMAKE_HOST_WIN32)
    set(btga_host_exe_suffix ".exe")
else()
    set(btga_host_exe_suffix "")
endif()

set(btga_host_file_to_c "${BTGA_HOST_TOOLS_DIR}/file_to_c${btga_host_exe_suffix}")
if (NOT BTGA_HOST_TOOLS_DIR OR NOT EXISTS "${btga_host_file_to_c}")
    message(FATAL_ERROR
        "Cross-compiling needs a file_to_c built for this machine. Build the desktop version "
        "first (see BUILDING.md), then pass its build folder as -DBTGA_HOST_TOOLS_DIR=<folder> "
        "(Android Studio: btga.hostToolsDir=<folder> in android/local.properties). "
        "Looked for: ${btga_host_file_to_c}")
endif()
add_executable(file_to_c IMPORTED GLOBAL)
set_target_properties(file_to_c PROPERTIES IMPORTED_LOCATION "${btga_host_file_to_c}")
message(STATUS "Cross-compiling: using host file_to_c at ${btga_host_file_to_c}")

set(btga_missing_inputs "")
file(GLOB btga_recompiled_funcs "${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.c")
if (NOT btga_recompiled_funcs)
    list(APPEND btga_missing_inputs "RecompiledFuncs/*.c")
endif()
foreach (input IN ITEMS
        RecompiledFuncs/recomp_overlays.inl
        RecompiledPatches/patches.c
        RecompiledPatches/patches_bin.c
        rsp/n_aspMain.cpp)
    if (NOT EXISTS "${CMAKE_SOURCE_DIR}/${input}")
        list(APPEND btga_missing_inputs "${input}")
    endif()
endforeach()
if (btga_missing_inputs)
    list(JOIN btga_missing_inputs "\n  " btga_missing_list)
    message(FATAL_ERROR
        "Cross-compiling uses the sources generated from the ROM by a desktop build, and these "
        "are missing:\n  ${btga_missing_list}\n"
        "Build the desktop version once with the ROM in place (see BUILDING.md); it writes them "
        "into this checkout.")
endif()
