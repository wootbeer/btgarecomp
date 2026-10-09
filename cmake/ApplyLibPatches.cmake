# Local fixes to the upstream submodules, kept as patch files in
# lib-patches/<name>/ and applied to the submodule's working tree at
# configure time (each only once: skipped when it already reverse-applies). When a patch
# changed since it was applied, the submodule's working tree is reset and all
# of its patches are applied again.
# The submodule is lib/<name> unless a folder (relative to the repo root) is
# given, as for submodules nested in another one.
find_package(Git REQUIRED)

function(btga_apply_one_patch dir patch)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${patch}"
        WORKING_DIRECTORY "${dir}"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "Couldn't apply ${patch} to ${dir}:\n${error}")
    endif()
    message(STATUS "Applied ${patch} to ${dir}")
endfunction()

function(btga_apply_lib_patches_from_scratch dir patches)
    foreach(patch IN LISTS patches)
        btga_apply_one_patch("${dir}" "${patch}")
    endforeach()
endfunction()

function(btga_apply_lib_patches submodule)
    if (ARGC GREATER 1)
        set(dir "${CMAKE_SOURCE_DIR}/${ARGV1}")
    else()
        set(dir "${CMAKE_SOURCE_DIR}/lib/${submodule}")
    endif()
    # CONFIGURE_DEPENDS and CMAKE_CONFIGURE_DEPENDS: adding, removing or changing a patch
    # re-runs the configure (and so this) on the next build, not just on a manual reconfigure.
    file(GLOB patches CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/lib-patches/${submodule}/*.patch")
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${patches})
    list(SORT patches)
    foreach(patch IN LISTS patches)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace --reverse --check "${patch}"
            WORKING_DIRECTORY "${dir}"
            RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
        if (already_applied EQUAL 0)
            continue()
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace --check "${patch}"
            WORKING_DIRECTORY "${dir}"
            RESULT_VARIABLE applies OUTPUT_QUIET ERROR_QUIET)
        if (NOT applies EQUAL 0)
            # Neither applied nor applicable: an older version of a patch is in the working
            # tree. Start over from the submodule's own sources and apply them all again.
            message(STATUS "Patches changed: resetting ${dir} and applying them again")
            execute_process(COMMAND "${GIT_EXECUTABLE}" checkout -- . WORKING_DIRECTORY "${dir}")
            btga_apply_lib_patches_from_scratch("${dir}" "${patches}")
            return()
        endif()
        btga_apply_one_patch("${dir}" "${patch}")
    endforeach()
endfunction()
