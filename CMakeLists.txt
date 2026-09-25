# 3.21 is the real floor ($<TARGET_RUNTIME_DLLS:>); the upper bound keeps
# policies at their NEW defaults on newer CMake.
cmake_minimum_required(VERSION 3.21...4.3)

# Default vcpkg to the static-md triplet so FreeType/libpng/zlib link into
# sextant.dll. Must be set before project(). Override with
# -DVCPKG_TARGET_TRIPLET=x64-windows for separate DLLs.
if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "Vcpkg target triplet")
endif()

# macOS 11.0 is the first release with arm64, and the first whose libc++ dylib
# carries the atomic-wait symbols std::binary_semaphore (window_thread.h) is
# marked as needing — below it the header refuses to compile. Must be set before
# project(). Universal binaries are the standard variable and need universal
# dependencies: -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64".
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(sextant VERSION 0.3.0 LANGUAGES CXX C)
if(APPLE)
    # OBJCXX for src/platform/macos/. OBJC first: enabled alone, OBJCXX claims
    # .m files too and compiles GLFW's Cocoa sources as Objective-C++, which
    # they are not.
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

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
    find_package(Git REQUIRED)
    # Patched for macOS (cmake/glfw/): a software-renderer fallback for GPU-less
    # machines, and the CGL lock around [NSOpenGLContext update]. The hash puts
    # the patches' content into the declared details, so editing one re-patches.
    file(GLOB glfw_patches "${CMAKE_CURRENT_SOURCE_DIR}/cmake/glfw/*.patch")
    list(SORT glfw_patches)
    set(glfw_patch_text "")
    foreach(patch IN LISTS glfw_patches)
        file(READ "${patch}" text)
        string(APPEND glfw_patch_text "${text}")
    endforeach()
    string(SHA256 glfw_patch_hash "${glfw_patch_text}")
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
        PATCH_COMMAND  ${CMAKE_COMMAND} -DGIT=${GIT_EXECUTABLE}
                       -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/glfw
                       -DPATCH_HASH=${glfw_patch_hash}
                       -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/glfw/apply_patches.cmake
    )
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
else()
    find_package(glfw3 3.3 REQUIRED)
    if(APPLE)
        # A system GLFW carries neither patch, and both matter here: without the
        # first, a Mac with no usable GPU gets no window at all; without the
        # second, a resize can run [NSOpenGLContext update] in the middle of a
        # frame. Buildable, not recommended.
        message(WARNING
            "SEXTANT_FETCH_GLFW=OFF on macOS: the patches in cmake/glfw/ are not "
            "applied — no software-renderer fallback, and no lock around "
            "[NSOpenGLContext update] while a window is resized.")
    endif()
endif()

# macOS: FreeType and libpng built from source into libsextant.dylib, instead
# of depending on Homebrew dylibs at run time -- the bargain the Windows
# static-md triplet makes. From source rather than Homebrew's archives, which
# are built for the machine they were bottled on (macOS 27 on one, 15 on CI):
# linked into a library that claims CMAKE_OSX_DEPLOYMENT_TARGET, they would not
# honour it. Built here they follow the deployment target and the
# architectures, universal2 included. zlib comes from the SDK, as every Mac has
# it; FreeType's optional codecs (brotli, bzip2, png, harfbuzz) are off -- the
# fonts sextant loads need none of them. The SHARED target only: sextant_static's
# consumers resolve these themselves. (v1.0 steps 21.6, 22.6)
set(SEXTANT_MACOS_STATIC_DEPS_DEFAULT OFF)
if(APPLE)
    set(SEXTANT_MACOS_STATIC_DEPS_DEFAULT ON)
endif()
option(SEXTANT_MACOS_STATIC_DEPS
       "Build FreeType/libpng from source into libsextant.dylib on macOS"
       ${SEXTANT_MACOS_STATIC_DEPS_DEFAULT})

set(SEXTANT_FT_BUNDLED OFF)
set(SEXTANT_PNG_BUNDLED OFF)
if(APPLE AND SEXTANT_MACOS_STATIC_DEPS)
    include(FetchContent)
    set(_sextant_saved_shared ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    set(SKIP_INSTALL_ALL ON)  # neither is installed; both live inside the dylib

    if(SEXTANT_USE_FREETYPE)
        set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)  # FreeType's own copy
        set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            sextant_freetype
            GIT_REPOSITORY https://github.com/freetype/freetype.git
            GIT_TAG        VER-2-14-3
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(sextant_freetype)
        set(SEXTANT_FT_BUNDLED ON)
        message(STATUS "FreeType built from source into libsextant.dylib (2.14.3)")
    endif()

    if(SEXTANT_USE_LIBPNG)
        set(PNG_SHARED    OFF CACHE BOOL "" FORCE)
        set(PNG_STATIC    ON  CACHE BOOL "" FORCE)
        set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
        set(PNG_TESTS     OFF CACHE BOOL "" FORCE)
        set(PNG_TOOLS     OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            sextant_libpng
            GIT_REPOSITORY https://github.com/pnggroup/libpng.git
            GIT_TAG        v1.6.58
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(sextant_libpng)
        set(SEXTANT_PNG_BUNDLED ON)
        message(STATUS "libpng built from source into libsextant.dylib (1.6.58)")
    endif()

    unset(SKIP_INSTALL_ALL)
    set(BUILD_SHARED_LIBS ${_sextant_saved_shared})
endif()

# FreeType, for everything the bundled copy does not serve (sextant_static,
# and every target off macOS).
if(SEXTANT_USE_FREETYPE)
    find_package(Freetype QUIET)
    if(NOT Freetype_FOUND AND NOT SEXTANT_FT_BUNDLED)
        message(WARNING "FreeType not found — falling back to stb_truetype")
        set(SEXTANT_USE_FREETYPE OFF)
    endif()
endif()

# libpng
if(SEXTANT_USE_LIBPNG)
    find_package(PNG QUIET)
    if(NOT PNG_FOUND AND NOT SEXTANT_PNG_BUNDLED)
        message(WARNING "libpng not found — falling back to stb_image_write")
        set(SEXTANT_USE_LIBPNG OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# Third-party vendored sources (run scripts/setup_deps.sh first)
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
    # No imgui_impl_glfw: src/widgets/imgui_impl_sextant.cpp is the platform
    # backend, and nothing in the tree compiles the GLFW one any more.
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# Warn if third-party files are missing
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
    src/axes3d.cpp
    src/plane2d.cpp
    src/style.cpp
    src/colormaps.cpp
    src/contour.cpp
    src/delaunay.cpp
    src/window_thread.cpp
    src/window_registry.cpp
    src/window_link.cpp
    src/window_broker.cpp
    src/renderer/gl_context.cpp
    src/renderer/nvg_renderer.cpp
    src/renderer/data_renderer.cpp
    src/renderer/fbo_readback.cpp
    src/renderer/plot_fbo.cpp
    src/renderer/figure_layout.cpp
    src/renderer/box3d.cpp
    src/renderer/bar3d.cpp
    src/renderer/plane2d.cpp
    src/renderer/surface.cpp
    src/renderer/surface_tri.cpp
    src/renderer/scatter3d.cpp
    src/renderer/line3d.cpp
    src/renderer/error_bar3d.cpp
    src/renderer/painter3d.cpp
    src/output/png_writer.cpp
    src/output/svg_writer.cpp
    src/widgets/imgui_context.cpp
    src/widgets/imgui_impl_sextant.cpp
    src/widgets/panel.cpp
    src/widgets/data_panel.cpp
)

# The three things that differ on macOS -- which thread may own a window, the
# render thread's context lock, and reading the clipboard off it. Kept to one
# file so everything around it stays covered by the Windows and Linux suites.
if(APPLE)
    list(APPEND SEXTANT_SOURCES src/platform/macos/platform_macos.mm)
else()
    list(APPEND SEXTANT_SOURCES src/platform/platform_generic.cpp)
endif()

# Settings common to the SHARED and STATIC targets. link_scope: PRIVATE for
# SHARED (deps are merged into sextant.dll), PUBLIC for STATIC (consumers must
# link the deps transitively).
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

    # IMGUI_USER_CONFIG: a thread-local GImGui per window thread, and GLAD as
    # the backend loader (see src/widgets/sextant_imconfig.h).
    # IMGUI_IMPL_OPENGL_LOADER_CUSTOM disables imgui's bundled gl3w loader,
    # whose shared teardown crashed sibling windows.
    target_compile_definitions(${target_name}
        PRIVATE
            IMGUI_IMPL_OPENGL_LOADER_CUSTOM
            IMGUI_USER_CONFIG="widgets/sextant_imconfig.h"
    )

    target_link_libraries(${target_name}
        PUBLIC  OpenGL::GL
        ${link_scope} glfw
    )

    if(APPLE)
        # NSPasteboard, for the clipboard read on the render thread. CGL, for
        # the context lock, comes with OpenGL::GL.
        target_link_libraries(${target_name} ${link_scope} "-framework AppKit")
        # CGL and the GL entry points are deprecated on macOS and are still the
        # only ones there are. The 4.1 ceiling exists for exactly this: every
        # shader is `#version 410 core` and nothing uses a removed entry point.
        target_compile_definitions(${target_name} PRIVATE GL_SILENCE_DEPRECATION)
    endif()

    # The bundled builds go to the SHARED target only: a self-contained
    # sextant.dylib is the whole point, while sextant_static's consumers resolve
    # FreeType and libpng themselves (see cmake/sextantConfig.cmake.in). Each
    # target gets its compile definitions only with a library actually linked,
    # so one without falls back to stb on its own.
    get_target_property(_sextant_target_type ${target_name} TYPE)
    set(_use_bundled OFF)
    if(_sextant_target_type STREQUAL "SHARED_LIBRARY")
        set(_use_bundled ON)
    endif()

    if(SEXTANT_USE_FREETYPE)
        set(_ft_linked ON)
        if(_use_bundled AND SEXTANT_FT_BUNDLED)
            target_link_libraries(${target_name} PRIVATE freetype)
        elseif(TARGET Freetype::Freetype)
            target_link_libraries(${target_name} ${link_scope} Freetype::Freetype)
        else()
            set(_ft_linked OFF)
        endif()
        if(_ft_linked)
            # FONS_USE_FREETYPE switches nanovg's fontstash.h from stb_truetype to
            # FreeType (SEXTANT_USE_FREETYPE alone changes nothing in the sources).
            target_compile_definitions(${target_name} PRIVATE SEXTANT_USE_FREETYPE FONS_USE_FREETYPE)
        endif()
    endif()

    if(SEXTANT_USE_LIBPNG)
        set(_png_linked ON)
        if(_use_bundled AND SEXTANT_PNG_BUNDLED)
            target_link_libraries(${target_name} PRIVATE png_static)
        elseif(TARGET PNG::PNG)
            target_link_libraries(${target_name} ${link_scope} PNG::PNG)
        else()
            set(_png_linked OFF)
        endif()
        if(_png_linked)
            target_compile_definitions(${target_name} PRIVATE SEXTANT_USE_LIBPNG)
        endif()
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
    # A separate target name: on Windows the SHARED target's import library is
    # already "sextant.lib".
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

# sextant_static links glfw PUBLIC, so install its archive for consumers
# (GLFW_INSTALL is OFF; GLFW types never appear in public headers).
if(SEXTANT_BUILD_STATIC)
    install(TARGETS glfw
        EXPORT sextantTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Bundle runtime DLL dependencies into the install tree. A no-op with the
# default static-md triplet; only matters with a dynamic triplet
# (e.g. -DVCPKG_TARGET_TRIPLET=x64-windows).
if(WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:sextant> DESTINATION ${CMAKE_INSTALL_BINDIR})

    # TARGET_RUNTIME_DLLS can't see vcpkg's PNG/ZLIB (UNKNOWN IMPORTED targets
    # with only IMPORTED_LOCATION_<CONFIG>, uppercase, pointing at the .lib).
    # Derive each .dll from its .lib path and install just those files (not a
    # glob: classic-mode vcpkg's bin/ holds every installed package).
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
                        # zlib's DLL may be named zlib1.dll / zlibd1.dll.
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
# Convenience target: build + install into a fixed local prefix. CMake's
# built-in "install" isn't in the File API codemodel, so IDEs like CLion can't
# list it; this custom target can be.
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
    # GL stack / window open test
    add_executable(sextant_window_test tests/window_test/main.cpp)
    target_link_libraries(sextant_window_test PRIVATE sextant)

    # Integration smoke test
    add_executable(sextant_smoke tests/basic_smoke/main.cpp)
    target_link_libraries(sextant_smoke PRIVATE sextant)

    # Performance harness at large data sizes. Opens windows; not a CI test.
    add_executable(sextant_perf_test tests/perf_test/main.cpp)
    target_link_libraries(sextant_perf_test PRIVATE sextant)

    # Reports the OpenGL driver a machine gives for sextant's 4.1 core
    # context. Raw GLFW + GLAD, deliberately not linked against sextant, so it
    # runs even where the library does not build.
    add_executable(sextant_gl_probe tests/gl_probe/main.cpp ${GLAD_SOURCE})
    target_include_directories(sextant_gl_probe PRIVATE ${CMAKE_SOURCE_DIR}/third_party/glad/include)
    target_link_libraries(sextant_gl_probe PRIVATE glfw)
    if(APPLE)
        # Windowless CGL probe: the context headless export uses, without GLFW.
        target_sources(sextant_gl_probe PRIVATE tests/gl_probe/cgl_probe.cpp)
        target_link_libraries(sextant_gl_probe PRIVATE OpenGL::GL)
        target_compile_definitions(sextant_gl_probe PRIVATE GL_SILENCE_DEPRECATION)
    endif()

    # The threading design driven through the public API: figures shown from
    # the main thread and from a worker, the main thread pumping, windows closed
    # out of order. Scripted with a watchdog, so a deadlock fails instead of
    # hanging; --manual leaves two live windows to a person.
    find_package(Threads REQUIRED)
    add_executable(sextant_window_probe tests/window_probe/main.cpp)
    target_link_libraries(sextant_window_probe PRIVATE sextant Threads::Threads)

    # Internal layout checks: linked against sextant_static with src/ on the
    # include path, since what it checks isn't reachable through public headers.
    if(SEXTANT_BUILD_STATIC)
        # One translation unit per subject (see tests/layout_test/layout_test.h).
        # Listed rather than globbed, like SEXTANT_SOURCES.
        add_executable(sextant_layout_test
            tests/layout_test/main.cpp
            tests/layout_test/common.cpp
            tests/layout_test/text_metrics.cpp
            tests/layout_test/glyph_boxes.cpp
            tests/layout_test/gl_poison.cpp
            tests/layout_test/layout.cpp
            tests/layout_test/frame_size.cpp
            tests/layout_test/placement.cpp
            tests/layout_test/stored_layout.cpp
            tests/layout_test/grid_ratios.cpp
            tests/layout_test/errorbar2d.cpp
            tests/layout_test/line2d.cpp
            tests/layout_test/axis_position.cpp
            tests/layout_test/errorbar3d.cpp
            tests/layout_test/contour.cpp
            tests/layout_test/heatmap.cpp
            tests/layout_test/cell_shading.cpp
            tests/layout_test/axes3d.cpp
            tests/layout_test/camera3d.cpp
            tests/layout_test/bar3d.cpp
            tests/layout_test/surface3d.cpp
            tests/layout_test/scatter3d.cpp
            tests/layout_test/line3d.cpp
            tests/layout_test/surface_tri.cpp
            tests/layout_test/plane2d.cpp
            tests/layout_test/plane2d_render.cpp
            tests/layout_test/plane2d_panel.cpp
            tests/layout_test/scene3d.cpp
            tests/layout_test/scene3d_svg.cpp
            tests/layout_test/panel.cpp
            tests/layout_test/headless_export.cpp
            tests/layout_test/window_broker.cpp
            tests/layout_test/window_input.cpp
            tests/layout_test/read_back.cpp
            tests/layout_test/window_wait.cpp)
        target_link_libraries(sextant_layout_test PRIVATE sextant_static)
        target_include_directories(sextant_layout_test PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/third_party/glad/include
            ${CMAKE_SOURCE_DIR}/third_party/nanovg
            ${CMAKE_SOURCE_DIR}/third_party/stb
            # cell_shading.h names ImU32/IM_COL32, so compile with the same
            # IMGUI_USER_CONFIG as the library.
            ${IMGUI_SOURCE_DIR})
        target_compile_definitions(sextant_layout_test PRIVATE
            IMGUI_USER_CONFIG="widgets/sextant_imconfig.h")

        # Diagnostic for painter splits: calls paint_order() directly (internal
        # API). Asserts nothing; prints a table.
        add_executable(sextant_split_probe tests/split_probe/main.cpp)
        target_link_libraries(sextant_split_probe PRIVATE sextant_static)
        target_include_directories(sextant_split_probe PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/third_party/glad/include
            ${CMAKE_SOURCE_DIR}/third_party/nanovg
            ${CMAKE_SOURCE_DIR}/third_party/stb
            ${IMGUI_SOURCE_DIR})
        target_compile_definitions(sextant_split_probe PRIVATE
            IMGUI_USER_CONFIG="widgets/sextant_imconfig.h")
    endif()
endif()
