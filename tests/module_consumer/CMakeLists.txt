# sextant_static linked into a shared *module*, the way the Python binding's
# extension module links it: sextant added as a subdirectory with no shared
# library and its FreeType/libpng/zlib built from source, the module loaded at
# run time by sextant_module_loader the way CPython loads an extension. Then
# check_module.sh checks that the module depends on nothing but the OS and
# exports nothing of sextant's or its dependencies'. Built by the CI job.
cmake_minimum_required(VERSION 3.21)
project(sextant_module_consumer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SEXTANT_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SEXTANT_BUNDLE_DEPS  ON  CACHE BOOL "" FORCE)
set(SEXTANT_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
add_subdirectory(../.. sextant)

# Hidden by default, as nanobind_add_module builds an extension: the probe
# function is the module's one export.
add_library(sextant_module_probe MODULE probe_module.cpp)
target_link_libraries(sextant_module_probe PRIVATE sextant::sextant_static)
set_target_properties(sextant_module_probe PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

add_executable(sextant_module_loader loader.cpp)
target_link_libraries(sextant_module_loader PRIVATE ${CMAKE_DL_LIBS})
