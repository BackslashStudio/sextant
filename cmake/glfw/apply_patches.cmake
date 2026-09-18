# FetchContent's PATCH_COMMAND for GLFW, run in the fetched source tree:
#   cmake -DGIT=<git> -DPATCH_DIR=<this directory> -P apply_patches.cmake
# Restores the tree to the fetched tag, then applies every *.patch here in name
# order, so it gives the same result however often it runs, including after a
# patch is edited. All patches touch Cocoa-only files.
file(GLOB patches "${PATCH_DIR}/*.patch")
list(SORT patches)

execute_process(COMMAND "${GIT}" checkout -- .
                RESULT_VARIABLE failed ERROR_VARIABLE err)
if(failed)
    message(FATAL_ERROR "GLFW: cannot restore the fetched source tree:\n${err}")
endif()

foreach(patch IN LISTS patches)
    get_filename_component(name "${patch}" NAME)
    execute_process(COMMAND "${GIT}" apply --ignore-whitespace "${patch}"
                    RESULT_VARIABLE failed ERROR_VARIABLE err)
    if(failed)
        message(FATAL_ERROR "GLFW: patch ${name} does not apply:\n${err}")
    endif()
    message(STATUS "GLFW: applied ${name}")
endforeach()
