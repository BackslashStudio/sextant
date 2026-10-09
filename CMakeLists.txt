# 3.21 is the real floor ($<TARGET_RUNTIME_DLLS:>); the upper bound keeps
# policies at their NEW defaults on newer CMake.
cmake_minimum_required(VERSION 3.21...4.3)

# SEXTANT_PACKAGE_BUILD: built by a package manager (the vcpkg port), which
# owns the dependencies and the linkage. One `sextant` target that follows
# BUILD_SHARED_LIBS instead of SHARED + sextant_static; FreeType and libpng from
# find_package on every platform; no triplet default, no DLL bundling; GLFW
# from FETCHCONTENT_SOURCE_DIR_GLFW, already patched, when the port gives one.
option(SEXTANT_PACKAGE_BUILD "Build for a package manager (see CMakeLists.txt)" OFF)

# Default vcpkg to the static-md triplet so FreeType/libpng/zlib link into
# sextant.dll. Must be set before project(). Override with
# -DVCPKG_TARGET_TRIPLET=x64-windows for separate DLLs.
if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET AND NOT SEXTANT_PACKAGE_BUILD)
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

project(sextant VERSION 1.1.0 LANGUAGES CXX C)
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
option(SEXTANT_WINDOW_ICON    "Give every window the sextant icon (OFF: the platform default)" ON)
option(SEXTANT_BUILD_TESTS   "Build smoke test executable" ${PROJECT_IS_TOP_LEVEL})

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# GLFW
if(SEXTANT_FETCH_GLFW)
    include(FetchContent)
    if(FETCHCONTENT_SOURCE_DIR_GLFW)
        # A local tree, e.g. from the vcpkg port. FetchContent runs no
        # PATCH_COMMAND on it, so it must already carry cmake/glfw/*.patch.
        FetchContent_Declare(glfw SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_GLFW}")
    else()
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
    endif()
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    # Always a static archive inside sextant, whatever BUILD_SHARED_LIBS says
    # (a package build sets it for a dynamic triplet).
    set(_sextant_saved_shared ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(glfw)
    set(BUILD_SHARED_LIBS ${_sextant_saved_shared})
    # Hidden: GLFW is sextant's own, and a shared object linking it -- the
    # library, or a Python extension module -- must not export it, where it
    # could meet another GLFW in the same process.
    set_target_properties(glfw PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
        OBJC_VISIBILITY_PRESET hidden)
    if(SEXTANT_PACKAGE_BUILD)
        # Installed next to sextant for a static triplet; its own name keeps it
        # clear of the glfw3 port's files.
        set_target_properties(glfw PROPERTIES OUTPUT_NAME sextant_glfw3)
    endif()
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

# SEXTANT_BUNDLE_DEPS: FreeType and libpng -- and zlib, off macOS -- built from
# source and linked into sextant, instead of found on the system. Both targets
# get them: a self-contained libsextant.dylib, and a sextant_static that links
# into a Python extension module with no second binary to ship beside it.
#
# macOS (default ON): from source rather than Homebrew's archives, which are
# built for the machine they were bottled on (macOS 27 on one, 15 on CI):
# linked into a library that claims CMAKE_OSX_DEPLOYMENT_TARGET, they would not
# honour it. Built here they follow the deployment target and the
# architectures, universal2 included. zlib comes from the SDK, as every Mac has
# it. (v1.0 steps 21.6, 22.6)
# Elsewhere (default OFF; the Python binding turns it on): zlib as well, since
# a manylinux wheel may not depend on the system's libz. FreeType's optional
# codecs (zlib, brotli, bzip2, png, harfbuzz) are off everywhere -- the fonts
# sextant loads need none of them.
# SEXTANT_MACOS_STATIC_DEPS, the v1.0 name, still seeds the macOS default.
set(_sextant_bundle_default OFF)
if(APPLE AND NOT SEXTANT_PACKAGE_BUILD)
    set(_sextant_bundle_default ON)
    if(DEFINED SEXTANT_MACOS_STATIC_DEPS)
        set(_sextant_bundle_default ${SEXTANT_MACOS_STATIC_DEPS})
    endif()
endif()
option(SEXTANT_BUNDLE_DEPS
       "Build FreeType/libpng (and zlib, off macOS) from source into sextant"
       ${_sextant_bundle_default})
if(SEXTANT_BUNDLE_DEPS AND SEXTANT_PACKAGE_BUILD)
    message(FATAL_ERROR
        "SEXTANT_BUNDLE_DEPS: a package build takes its dependencies from the package manager")
endif()

set(SEXTANT_FT_BUNDLED OFF)
set(SEXTANT_PNG_BUNDLED OFF)
set(SEXTANT_ZLIB_BUNDLED OFF)
set(SEXTANT_BUNDLED_TARGETS "")
if(SEXTANT_BUNDLE_DEPS)
    include(FetchContent)
    set(_sextant_saved_shared ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    set(SKIP_INSTALL_ALL ON)  # installed, if at all, through sextant's export set

    if(SEXTANT_USE_LIBPNG AND NOT APPLE)
        set(ZLIB_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
        set(ZLIB_BUILD_STATIC  ON  CACHE BOOL "" FORCE)
        set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(ZLIB_INSTALL       OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            sextant_zlib
            GIT_REPOSITORY https://github.com/madler/zlib.git
            GIT_TAG        v1.3.2
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(sextant_zlib)
        # libpng asks find_package(ZLIB) for ZLIB::ZLIB. Make that name this
        # archive, and preset what FindZLIB checks so it searches no further:
        # it skips the search for a set ZLIB_LIBRARY and keeps an existing
        # ZLIB::ZLIB.
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
        set(ZLIB_INCLUDE_DIR "${sextant_zlib_SOURCE_DIR}" CACHE PATH "" FORCE)
        set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
        set(SEXTANT_ZLIB_BUNDLED ON)
        list(APPEND SEXTANT_BUNDLED_TARGETS zlibstatic)
        message(STATUS "zlib built from source into sextant (1.3.2)")
    endif()

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
        # FreeType marks its API visibility("default") itself, unconditionally,
        # which the hidden preset below does not override; see the header.
        if(NOT MSVC)
            target_compile_options(freetype PRIVATE
                "SHELL:-include ${PROJECT_SOURCE_DIR}/cmake/freetype/sextant_hidden_api.h")
        endif()
        set(SEXTANT_FT_BUNDLED ON)
        list(APPEND SEXTANT_BUNDLED_TARGETS freetype)
        message(STATUS "FreeType built from source into sextant (2.14.3)")
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
        list(APPEND SEXTANT_BUNDLED_TARGETS png_static)
        message(STATUS "libpng built from source into sextant (1.6.58)")
    endif()

    # Linkable into a shared object -- a Python extension module is one -- and
    # nothing of theirs exported from it.
    if(SEXTANT_BUNDLED_TARGETS)
        set_target_properties(${SEXTANT_BUNDLED_TARGETS} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            C_VISIBILITY_PRESET hidden)
    endif()

    unset(SKIP_INSTALL_ALL)
    set(BUILD_SHARED_LIBS ${_sextant_saved_shared})
endif()

# FreeType and libpng from the system, when not bundled.
# A package build asked for them through port features, so a missing one is an
# error there rather than a quiet fallback.
set(_sextant_dep_mode QUIET)
if(SEXTANT_PACKAGE_BUILD)
    set(_sextant_dep_mode REQUIRED)
endif()
if(SEXTANT_USE_FREETYPE AND NOT SEXTANT_FT_BUNDLED)
    find_package(Freetype ${_sextant_dep_mode})
    if(NOT Freetype_FOUND)
        message(WARNING "FreeType not found — falling back to stb_truetype")
        set(SEXTANT_USE_FREETYPE OFF)
    endif()
endif()

# libpng
if(SEXTANT_USE_LIBPNG AND NOT SEXTANT_PNG_BUNDLED)
    find_package(PNG ${_sextant_dep_mode})
    if(NOT PNG_FOUND)
        message(WARNING "libpng not found — falling back to stb_image_write")
        set(SEXTANT_USE_LIBPNG OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# Third-party vendored sources (run scripts/setup_deps.sh first)
# ---------------------------------------------------------------------------
set(GLAD_SOURCE   ${PROJECT_SOURCE_DIR}/third_party/glad/src/glad.c)
set(NANOVG_SOURCE ${PROJECT_SOURCE_DIR}/third_party/nanovg/nanovg.c)
set(IMGUI_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/imgui)
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

# Dear ImGui, compiled once with sextant's config and folded into each library
# target as objects. OBJECT rather than STATIC: sextant_static is exported and
# links PUBLIC, so a STATIC imgui would enter its link interface and have to
# be installed and exported (as glfw is), and join the --exclude-libs list
# below. As objects it lands inside the archive/DLL exactly as sources did.
if(EXISTS ${IMGUI_MAIN_SOURCE})
    add_library(sextant_imgui OBJECT ${IMGUI_SOURCES})
    # The archive's objects go into shared objects too (see sextant_static).
    set_target_properties(sextant_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(sextant_imgui PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/third_party/glad/include
        ${IMGUI_SOURCE_DIR}
        ${IMGUI_SOURCE_DIR}/backends)
    # Same IMGUI_USER_CONFIG and loader switch as the library (see
    # sextant_configure_target): ImGui and sextant must agree on both.
    target_compile_definitions(sextant_imgui PRIVATE
        IMGUI_IMPL_OPENGL_LOADER_CUSTOM
        IMGUI_USER_CONFIG="widgets/sextant_imconfig.h")
    target_link_libraries(sextant_imgui PRIVATE OpenGL::GL)
    if(APPLE)
        target_compile_definitions(sextant_imgui PRIVATE GL_SILENCE_DEPRECATION)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(sextant_imgui PRIVATE -fvisibility=hidden)
    endif()
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
option(SEXTANT_BUILD_STATIC  "Also build a static sextant_static library target" ON)
# OFF: sextant_static alone, e.g. for a project that links it into a module of
# its own (the Python binding) and has no use for the shared library. Ignored by
# a package build, which makes exactly one library anyway.
option(SEXTANT_BUILD_SHARED  "Build the sextant shared library" ON)
if(NOT SEXTANT_BUILD_SHARED AND NOT SEXTANT_PACKAGE_BUILD)
    set(SEXTANT_BUILD_STATIC ON)
endif()

set(CMAKE_DEBUG_POSTFIX "d")

set(SEXTANT_SOURCES
    src/figure.cpp
    src/figure_export.cpp
    src/messages.cpp
    src/render_frame.cpp
    src/hint.cpp
    src/plot_data_view.cpp
    src/font_discovery.cpp
    src/text_metrics.cpp
    src/rich_text.cpp
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
    src/event_channel.cpp
    src/key_names.cpp
    src/plot_events.cpp
    src/renderer/gl_context.cpp
    src/renderer/render_device.cpp
    src/renderer/nvg_renderer.cpp
    src/renderer/data_renderer.cpp
    src/renderer/fbo_readback.cpp
    src/renderer/plot_fbo.cpp
    src/renderer/saved_target.cpp
    src/renderer/figure_layout.cpp
    src/renderer/box3d.cpp
    src/renderer/bar3d.cpp
    src/renderer/plane2d.cpp
    src/renderer/surface.cpp
    src/renderer/surface_tri.cpp
    src/renderer/scatter3d.cpp
    src/renderer/line3d.cpp
    src/renderer/error_bar3d.cpp
    src/renderer/text_plan.cpp
    src/renderer/painter3d.cpp
    src/output/file_write.cpp
    src/output/png_writer.cpp
    src/output/svg_writer.cpp
    src/widgets/imgui_context.cpp
    src/widgets/imgui_impl_sextant.cpp
    src/widgets/panel.cpp
    src/widgets/plot_view.cpp
    src/widgets/figure_window_shell.cpp
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
# Linux's windowless context for headless exports: EGL, dlopen()ed at run time
# rather than linked, so nothing gains a libEGL dependency (see the file).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SEXTANT_SOURCES src/platform/linux/offscreen_egl.cpp)
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
    if(TARGET sextant_imgui)
        target_sources(${target_name} PRIVATE $<TARGET_OBJECTS:sextant_imgui>)
    endif()

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/third_party/glad/include
            ${PROJECT_SOURCE_DIR}/third_party/glad/include/KHR
            ${PROJECT_SOURCE_DIR}/third_party/nanovg
            ${PROJECT_SOURCE_DIR}/third_party/stb
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
    if(SEXTANT_WINDOW_ICON)
        target_compile_definitions(${target_name} PRIVATE SEXTANT_WINDOW_ICON)
    endif()

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
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # dlopen() for libEGL; part of libc from glibc 2.34, libdl before that.
        target_link_libraries(${target_name} ${link_scope} ${CMAKE_DL_LIBS})
    endif()

    # Bundled builds (SEXTANT_BUNDLE_DEPS) go to both targets, PRIVATE: no
    # public header names them. For the archive that still puts them in its
    # link interface (as $<LINK_ONLY:>), which is why they join the export set
    # below. System ones follow link_scope, and sextant_static's consumers
    # resolve them (see cmake/sextantConfig.cmake.in). Each target gets its
    # compile definitions only with a library actually linked, so one without
    # falls back to stb on its own.
    if(SEXTANT_USE_FREETYPE)
        set(_ft_linked ON)
        if(SEXTANT_FT_BUNDLED)
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
        if(SEXTANT_PNG_BUNDLED)
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

if(SEXTANT_PACKAGE_BUILD)
    # One library, of the kind BUILD_SHARED_LIBS asks for, always named
    # sextant::sextant so consumers need not know the triplet.
    set(SEXTANT_BUILD_STATIC OFF)
    add_library(sextant ${SEXTANT_SOURCES})
    get_target_property(_sextant_type sextant TYPE)
    if(_sextant_type STREQUAL "STATIC_LIBRARY")
        set(SEXTANT_PACKAGE_STATIC ON)
        sextant_configure_target(sextant PUBLIC)
        target_compile_definitions(sextant PUBLIC SEXTANT_STATIC_DEFINE)
    else()
        set(SEXTANT_PACKAGE_STATIC OFF)
        sextant_configure_target(sextant PRIVATE)
        target_compile_definitions(sextant PRIVATE SEXTANT_BUILD_SHARED)
    endif()
elseif(SEXTANT_BUILD_SHARED)
    set(SEXTANT_PACKAGE_STATIC OFF)
    add_library(sextant SHARED ${SEXTANT_SOURCES})
    sextant_configure_target(sextant PRIVATE)
    target_compile_definitions(sextant PRIVATE SEXTANT_BUILD_SHARED)
else()
    set(SEXTANT_PACKAGE_STATIC OFF)
endif()

if(SEXTANT_BUILD_STATIC)
    # A separate target name: on Windows the SHARED target's import library is
    # already "sextant.lib".
    add_library(sextant_static STATIC ${SEXTANT_SOURCES})
    sextant_configure_target(sextant_static PUBLIC)
    target_compile_definitions(sextant_static PUBLIC SEXTANT_STATIC_DEFINE)
    # Linkable into a shared object as well as an executable: on Linux x86_64
    # an archive without -fPIC cannot go into a .so, and a Python extension
    # module is one. Free elsewhere, where code is position-independent anyway.
    set_target_properties(sextant_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # ...and nothing of it exported from that shared object. -fvisibility=hidden
    # leaves one gap with libstdc++: std templates instantiated over sextant's
    # own types (std::vector<sextant::...> and the like) stay default-visible,
    # because namespace std is marked so. The ELF linker's --exclude-libs hides
    # every symbol that comes from the archives named, so it is added to what
    # links sextant_static -- for sextant's archives only, leaving the
    # consumer's others alone. A no-op in an executable. Build tree only: the
    # installed static target keeps the compile-time hiding.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_sextant_own_archives "$<TARGET_FILE_NAME:sextant_static>")
        foreach(_t IN LISTS SEXTANT_BUNDLED_TARGETS)
            string(APPEND _sextant_own_archives ":$<TARGET_FILE_NAME:${_t}>")
        endforeach()
        if(SEXTANT_FETCH_GLFW)
            string(APPEND _sextant_own_archives ":$<TARGET_FILE_NAME:glfw>")
        endif()
        target_link_options(sextant_static INTERFACE
            "$<BUILD_INTERFACE:-Wl,--exclude-libs,${_sextant_own_archives}>")
    endif()
    if(NOT TARGET sextant)
        # SEXTANT_BUILD_SHARED=OFF: what names `sextant` -- the tests,
        # install_dist -- gets the archive.
        add_library(sextant ALIAS sextant_static)
    endif()
endif()

# The names an installed package gives, for a project that adds this one as a
# subdirectory instead: the same link line works either way.
if(SEXTANT_PACKAGE_BUILD OR SEXTANT_BUILD_SHARED)
    add_library(sextant::sextant ALIAS sextant)
endif()
if(TARGET sextant_static)
    add_library(sextant::sextant_static ALIAS sextant_static)
endif()

# In-tree only: sextant_static plus what code reaching past the public headers
# needs -- src/ (figure_impl.h and friends), the vendored headers, and ImGui
# compiled against the library's own config. For the internal tests and for an
# app built from source with add_subdirectory(). Never installed or exported:
# an installed package and the vcpkg port have no such target, and a package
# build makes no sextant_static to base it on.
if(TARGET sextant_static)
    add_library(sextant_internal INTERFACE)
    target_link_libraries(sextant_internal INTERFACE sextant_static)
    target_include_directories(sextant_internal INTERFACE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/third_party/glad/include
        ${PROJECT_SOURCE_DIR}/third_party/nanovg
        ${PROJECT_SOURCE_DIR}/third_party/stb
        ${IMGUI_SOURCE_DIR}
        ${IMGUI_SOURCE_DIR}/backends)
    target_compile_definitions(sextant_internal INTERFACE
        IMGUI_IMPL_OPENGL_LOADER_CUSTOM
        IMGUI_USER_CONFIG="widgets/sextant_imconfig.h")
    if(APPLE)
        target_compile_definitions(sextant_internal INTERFACE GL_SILENCE_DEPRECATION)
    endif()
    add_library(sextant::internal ALIAS sextant_internal)
endif()

# ---------------------------------------------------------------------------
# Install / export (CMake package config for downstream consumers)
# ---------------------------------------------------------------------------
set(SEXTANT_INSTALL_TARGETS "")
if(SEXTANT_PACKAGE_BUILD OR SEXTANT_BUILD_SHARED)
    list(APPEND SEXTANT_INSTALL_TARGETS sextant)
endif()
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
if((SEXTANT_BUILD_STATIC OR SEXTANT_PACKAGE_STATIC) AND SEXTANT_FETCH_GLFW)
    install(TARGETS glfw
        EXPORT sextantTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
# Likewise the bundled FreeType/libpng/zlib, which sextant_static carries in
# its link interface: their archives only, as with glfw.
if(SEXTANT_BUILD_STATIC AND SEXTANT_BUNDLED_TARGETS)
    install(TARGETS ${SEXTANT_BUNDLED_TARGETS}
        EXPORT sextantTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Bundle runtime DLL dependencies into the install tree. A no-op with the
# default static-md triplet; only matters with a dynamic triplet
# (e.g. -DVCPKG_TARGET_TRIPLET=x64-windows). Never in a package build, where
# the package manager installs those DLLs itself.
if(WIN32 AND NOT SEXTANT_PACKAGE_BUILD AND SEXTANT_BUILD_SHARED)
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
    ${PROJECT_SOURCE_DIR}/cmake/sextantConfig.cmake.in
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
set(SEXTANT_LOCAL_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/dist" CACHE PATH
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
    target_include_directories(sextant_gl_probe PRIVATE ${PROJECT_SOURCE_DIR}/third_party/glad/include)
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

    # Internal layout checks: linked against sextant::internal (sextant_static
    # with src/ on the include path), since what it checks isn't reachable
    # through public headers.
    if(TARGET sextant::internal)
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
            tests/layout_test/color.cpp
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
            tests/layout_test/dialogs.cpp
            tests/layout_test/render_device.cpp
            tests/layout_test/plot_view.cpp
            tests/layout_test/window_shell.cpp
            tests/layout_test/headless_export.cpp
            tests/layout_test/memory_export.cpp
            tests/layout_test/messages.cpp
            tests/layout_test/window_broker.cpp
            tests/layout_test/window_input.cpp
            tests/layout_test/read_back.cpp
            tests/layout_test/figure_impl.cpp
            tests/layout_test/object_id.cpp
            tests/layout_test/undo.cpp
            tests/layout_test/describe.cpp
            tests/layout_test/background.cpp
            tests/layout_test/ticks_hidden.cpp
            tests/layout_test/edge_markers.cpp
            tests/layout_test/rich_text.cpp
            tests/layout_test/text.cpp
            tests/layout_test/window_wait.cpp
            tests/layout_test/events.cpp)
        # cell_shading.h names ImU32/IM_COL32, so it needs the library's
        # IMGUI_USER_CONFIG too -- sextant::internal carries it.
        target_link_libraries(sextant_layout_test PRIVATE sextant::internal)

        # Diagnostic for painter splits: calls paint_order() directly (internal
        # API). Asserts nothing; prints a table.
        add_executable(sextant_split_probe tests/split_probe/main.cpp)
        target_link_libraries(sextant_split_probe PRIVATE sextant::internal)

        # Headless export with no display (v1.1 step 27): renders gallery
        # figures to files through the windowless context, or with --window
        # through the hidden-window path, so CI can compare the two byte for
        # byte. Internal only for the test switch (platform.h).
        add_executable(sextant_headless_probe tests/headless_probe/main.cpp)
        target_link_libraries(sextant_headless_probe PRIVATE sextant::internal)
    endif()
endif()
