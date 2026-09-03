# Range form: 3.21 is the real floor ($<TARGET_RUNTIME_DLLS:> is the newest
# feature used), while the upper bound keeps policies at their NEW defaults on
# modern CMake instead of silently reverting to 3.21-era behaviour.
cmake_minimum_required(VERSION 3.21...4.3)

# Default vcpkg to the static-md triplet so FreeType/libpng/zlib link
# statically into sextant.dll instead of shipping as separate DLLs beside
# it. Must be set before project() — vcpkg's toolchain file (when passed via
# -DCMAKE_TOOLCHAIN_FILE) reads this during the first project()/
# enable_language() call. Override with -DVCPKG_TARGET_TRIPLET=x64-windows
# to get the old separate-DLLs layout back.
if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "Vcpkg target triplet")
endif()

project(sextant VERSION 0.3.0 LANGUAGES CXX C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)  # not using C++20 modules, avoids MSVC scan failures
set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(SEXTANT_USE_FREETYPE   "Use FreeType for font rendering (stb fallback if OFF)" ON)
option(SEXTANT_USE_LIBPNG     "Use libpng for PNG output (stb_image_write fallback if OFF)" ON)
option(SEXTANT_FETCH_GLFW     "Download GLFW via FetchContent (set OFF if installed system-wide)" ON)
option(SEXTANT_BUILD_TESTS    "Build smoke test executable" ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# GLFW
if(SEXTANT_FETCH_GLFW)
    include(FetchContent)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
else()
    find_package(glfw3 3.3 REQUIRED)
endif()

# FreeType
if(SEXTANT_USE_FREETYPE)
    find_package(Freetype QUIET)
    if(NOT Freetype_FOUND)
        message(WARNING "FreeType not found — falling back to stb_truetype")
        set(SEXTANT_USE_FREETYPE OFF)
    endif()
endif()

# libpng
if(SEXTANT_USE_LIBPNG)
    find_package(PNG QUIET)
    if(NOT PNG_FOUND)
        message(WARNING "libpng not found — falling back to stb_image_write")
        set(SEXTANT_USE_LIBPNG OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# Third-party vendored sources
# Requires running scripts/setup_deps.sh first to populate third_party/
# ---------------------------------------------------------------------------
set(GLAD_SOURCE   ${CMAKE_SOURCE_DIR}/third_party/glad/src/glad.c)
set(NANOVG_SOURCE ${CMAKE_SOURCE_DIR}/third_party/nanovg/nanovg.c)
set(IMGUI_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_MAIN_SOURCE ${IMGUI_SOURCE_DIR}/imgui.cpp)
set(IMGUI_SOURCES
    ${IMGUI_SOURCE_DIR}/imgui.cpp
    ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
    ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
    ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
    ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# Warn if third-party files are missing (will be resolved after setup_deps.sh)
foreach(_f ${GLAD_SOURCE} ${NANOVG_SOURCE} ${IMGUI_MAIN_SOURCE})
    if(NOT EXISTS ${_f})
        message(WARNING "Missing vendored file: ${_f}\nRun: bash scripts/setup_deps.sh")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
option(SEXTANT_BUILD_STATIC  "Also build a static sextant_static library target" ON)

set(CMAKE_DEBUG_POSTFIX "d")

set(SEXTANT_SOURCES
    src/figure.cpp
    src/figure_export.cpp
    src/render_frame.cpp
    src/hint.cpp
    src/plot_data_view.cpp
    src/font_discovery.cpp
    src/text_metrics.cpp
    src/axes.cpp
    src/style.cpp
    src/colormaps.cpp
    src/contour.cpp
    src/window_thread.cpp
    src/renderer/gl_context.cpp
    src/renderer/nvg_renderer.cpp
    src/renderer/data_renderer.cpp
    src/renderer/fbo_readback.cpp
    src/renderer/plot_fbo.cpp
    src/renderer/figure_layout.cpp
    src/output/png_writer.cpp
    src/output/svg_writer.cpp
    src/widgets/imgui_context.cpp
    src/widgets/panel.cpp
    src/widgets/data_panel.cpp
)

# Configures everything common to both the SHARED and STATIC sextant
# targets. link_scope controls how glfw/Freetype/PNG are attached:
#   - SHARED target: PRIVATE — their object code is merged into sextant.dll,
#     so consumers don't need to (and shouldn't) link them separately.
#   - STATIC target: PUBLIC — a static library can't embed its dependencies'
#     object code, so consumers linking sextant_static must pull those libs
#     in transitively via CMake's usage requirements instead.
function(sextant_configure_target target_name link_scope)
    if(EXISTS ${GLAD_SOURCE})
        target_sources(${target_name} PRIVATE ${GLAD_SOURCE})
    endif()
    if(EXISTS ${NANOVG_SOURCE})
        target_sources(${target_name} PRIVATE ${NANOVG_SOURCE})
    endif()
    if(EXISTS ${IMGUI_MAIN_SOURCE})
        target_sources(${target_name} PRIVATE ${IMGUI_SOURCES})
    endif()

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/third_party/glad/include
            ${CMAKE_SOURCE_DIR}/third_party/glad/include/KHR
            ${CMAKE_SOURCE_DIR}/third_party/nanovg
            ${CMAKE_SOURCE_DIR}/third_party/stb
            ${IMGUI_SOURCE_DIR}
            ${IMGUI_SOURCE_DIR}/backends
    )

    # IMGUI_USER_CONFIG gives every translation unit that sees imgui.h the same
    # thread-local GImGui — required because each Figure::show() drives its own
    # ImGui context from its own window thread. See src/widgets/sextant_imconfig.h.
    # IMGUI_IMPL_OPENGL_LOADER_CUSTOM suppresses the gl3w-derived loader that
    # imgui_impl_opengl3.cpp bundles, so the backend uses the GLAD loader
    # sextant_imconfig.h includes instead. (This replaced IMGUI_IMPL_OPENGL_LOADER_GLAD,
    # which imgui removed back in 2021 and had silently been a no-op ever since —
    # the bundled loader was in use, and its unconditional teardown crashed
    # sibling windows. See src/widgets/sextant_imconfig.h.)
    target_compile_definitions(${target_name}
        PRIVATE
            IMGUI_IMPL_OPENGL_LOADER_CUSTOM
            IMGUI_USER_CONFIG="widgets/sextant_imconfig.h"
    )

    target_link_libraries(${target_name}
        PUBLIC  OpenGL::GL
        ${link_scope} glfw
    )

    if(SEXTANT_USE_FREETYPE AND TARGET Freetype::Freetype)
        target_link_libraries(${target_name} ${link_scope} Freetype::Freetype)
        # FONS_USE_FREETYPE switches nanovg's vendored fontstash.h (see
        # third_party/nanovg/fontstash.h) from its built-in stb_truetype
        # rasterizer to FreeType — this is the macro that actually wires
        # FreeType in; SEXTANT_USE_FREETYPE alone gates nothing at the source level.
        target_compile_definitions(${target_name} PRIVATE SEXTANT_USE_FREETYPE FONS_USE_FREETYPE)
    endif()

    if(SEXTANT_USE_LIBPNG AND TARGET PNG::PNG)
        target_link_libraries(${target_name} ${link_scope} PNG::PNG)
        target_compile_definitions(${target_name} PRIVATE SEXTANT_USE_LIBPNG)
    endif()

    # Hide non-API symbols by default on GCC/Clang
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -fvisibility=hidden)
    endif()
endfunction()

add_library(sextant SHARED ${SEXTANT_SOURCES})
sextant_configure_target(sextant PRIVATE)
target_compile_definitions(sextant PRIVATE SEXTANT_BUILD_SHARED)

if(SEXTANT_BUILD_STATIC)
    # Separate target name (not just a differently-suffixed output file) —
    # on Windows the SHARED target's import library is also a "sextant.lib",
    # so reusing that basename for the static lib would collide with it.
    add_library(sextant_static STATIC ${SEXTANT_SOURCES})
    sextant_configure_target(sextant_static PUBLIC)
    target_compile_definitions(sextant_static PUBLIC SEXTANT_STATIC_DEFINE)
endif()

# ---------------------------------------------------------------------------
# Install / export (CMake package config for downstream consumers)
# ---------------------------------------------------------------------------
set(SEXTANT_INSTALL_TARGETS sextant)
if(SEXTANT_BUILD_STATIC)
    list(APPEND SEXTANT_INSTALL_TARGETS sextant_static)
endif()

install(TARGETS ${SEXTANT_INSTALL_TARGETS}
    EXPORT sextantTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# sextant_static links glfw PUBLIC (a static lib can't embed its
# dependencies' object code the way sextant.dll does), so consumers of
# sextant::sextant_static need glfw's archive transitively linked in too.
# GLFW's own install() is disabled (GLFW_INSTALL OFF above) so it doesn't
# install its headers/cmake-config — we only need the compiled archive here,
# since GLFW types never appear in sextant's public headers.
if(SEXTANT_BUILD_STATIC)
    install(TARGETS glfw
        EXPORT sextantTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Bundle sextant's own runtime DLL dependencies into the install tree so
# downstream consumers get a self-contained package.
#
# With the default x64-windows-static-md triplet (set above), FreeType/
# libpng/zlib are linked statically and baked directly into sextant.dll —
# there are no separate DLLs for them to bundle, so the block below is a
# no-op (its EXISTS checks simply never find a .dll to install). It only
# does real work if VCPKG_TARGET_TRIPLET is overridden back to a dynamic
# triplet (e.g. -DVCPKG_TARGET_TRIPLET=x64-windows).
if(WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:sextant> DESTINATION ${CMAKE_INSTALL_BINDIR})

    # TARGET_RUNTIME_DLLS above can't see PNG::PNG/ZLIB::ZLIB — vcpkg's PNG/ZLIB
    # Find-modules model them as UNKNOWN IMPORTED targets with only a per-config
    # IMPORTED_LOCATION_<CONFIG> (CONFIG suffix must be UPPERCASE, e.g. _DEBUG,
    # not _Debug) pointing at the .lib import stub rather than a plain
    # IMPORTED_LOCATION, which CMake can't resolve to a runtime DLL on its own.
    # Derive each one's actual .dll from its .lib path (same directory, vcpkg's
    # install layout) and install just those two files — NOT a directory glob,
    # since classic-mode vcpkg's bin/ holds every package ever installed on
    # this machine, not just this project's dependencies.
    if(SEXTANT_USE_LIBPNG AND TARGET PNG::PNG)
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _sextant_cfg_upper)
        foreach(_sextant_dep_target PNG::PNG ZLIB::ZLIB)
            if(TARGET ${_sextant_dep_target})
                get_target_property(_sextant_dep_lib ${_sextant_dep_target} IMPORTED_LOCATION_${_sextant_cfg_upper})
                if(NOT _sextant_dep_lib)
                    get_target_property(_sextant_dep_lib ${_sextant_dep_target} IMPORTED_LOCATION)
                endif()
                if(_sextant_dep_lib)
                    string(REPLACE "/lib/" "/bin/" _sextant_dep_dll "${_sextant_dep_lib}")
                    string(REPLACE ".lib" ".dll" _sextant_dep_dll "${_sextant_dep_dll}")
                    if(NOT EXISTS "${_sextant_dep_dll}")
                        # zlib's DLL is historically named zlib1.dll / zlibd1.dll
                        # (a numeric suffix the .lib name doesn't have) — try that too.
                        string(REPLACE ".dll" "1.dll" _sextant_dep_dll_alt "${_sextant_dep_dll}")
                        if(EXISTS "${_sextant_dep_dll_alt}")
                            set(_sextant_dep_dll "${_sextant_dep_dll_alt}")
                        endif()
                    endif()
                    if(EXISTS "${_sextant_dep_dll}")
                        install(FILES "${_sextant_dep_dll}" DESTINATION ${CMAKE_INSTALL_BINDIR})
                    endif()
                endif()
            endif()
        endforeach()
    endif()
endif()

install(EXPORT sextantTargets
    FILE sextantTargets.cmake
    NAMESPACE sextant::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sextant
)

configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/sextantConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/sextantConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sextant
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/sextantConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/sextantConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/sextantConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sextant
)

# ---------------------------------------------------------------------------
# Convenience target: build + install into a fixed local prefix.
# CMake's built-in "install" target is a phony rule the generator injects
# directly into the build files — it has no entry in the CMake File API
# codemodel, so IDEs (CLion) that enumerate targets from that model (as it
# does for the Run/Debug "CMake Application" target dropdown) can't offer it.
# Wrapping `cmake --install` in a real add_custom_target makes it show up
# there, the same way GLFW's own "uninstall" custom target does.
# ---------------------------------------------------------------------------
set(SEXTANT_LOCAL_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/dist" CACHE PATH
    "Prefix used by the 'install_dist' convenience target")
add_custom_target(install_dist
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${SEXTANT_LOCAL_INSTALL_PREFIX}
    COMMENT "Installing sextant into ${SEXTANT_LOCAL_INSTALL_PREFIX}"
    VERBATIM
)
add_dependencies(install_dist ${SEXTANT_INSTALL_TARGETS})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SEXTANT_BUILD_TESTS)
    # Step 2: GL stack / window open test
    add_executable(sextant_window_test tests/window_test/main.cpp)
    target_link_libraries(sextant_window_test PRIVATE sextant)

    # Final integration smoke test (requires full pipeline)
    add_executable(sextant_smoke tests/basic_smoke/main.cpp)
    target_link_libraries(sextant_smoke PRIVATE sextant)

    # Performance harness — RAM/ingest/refresh/render scaling at large data
    # sizes (see spec_test.md). Opens live windows, so it is not a CI test.
    add_executable(sextant_perf_test tests/perf_test/main.cpp)
    target_link_libraries(sextant_perf_test PRIVATE sextant)

    # Step 31: internal layout checks. Deliberately linked against
    # sextant_static and given src/ on its include path — what it verifies
    # (text measurement, computed layout rects) is not reachable through the
    # public headers, and the SHARED target exports only SEXTANT_API symbols.
    if(SEXTANT_BUILD_STATIC)
        add_executable(sextant_layout_test tests/layout_test/main.cpp)
        target_link_libraries(sextant_layout_test PRIVATE sextant_static)
        target_include_directories(sextant_layout_test PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/third_party/glad/include
            ${CMAKE_SOURCE_DIR}/third_party/nanovg
            ${CMAKE_SOURCE_DIR}/third_party/stb
            # Step 38: src/widgets/cell_shading.h names ImU32/IM_COL32 (no ImGui
            # *calls*, so nothing extra is linked). It must be compiled under
            # the same IMGUI_USER_CONFIG the library uses, or this target would
            # see a differently-configured imgui.h than sextant_static did.
            ${IMGUI_SOURCE_DIR})
        target_compile_definitions(sextant_layout_test PRIVATE
            IMGUI_USER_CONFIG="widgets/sextant_imconfig.h")
    endif()
endif()
