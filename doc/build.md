# Building and installing sextant

The [README](../README.md#building) has the two-command recipe for each
platform. This page covers the rest: where the dependencies come from, the build
options, where things are installed, how to link, and how to run the tests.

- [Requirements](#requirements)
- [Third-party code](#third-party-code)
- [Windows](#windows) · [Linux](#linux) · [macOS](#macos)
- [Build options](#build-options)
- [Installing](#installing)
- [Using sextant from CMake](#using-sextant-from-cmake)
- [Linking statically](#linking-statically)
- [Running the tests](#running-the-tests)
- [Troubleshooting](#troubleshooting)

## Requirements

| | Windows | Linux | macOS |
|---|---|---|---|
| Compiler | MSVC 2022 or later | GCC or Clang with C++20 | Apple Clang (Command Line Tools) |
| CMake | 3.21 or newer | 3.21 or newer | 3.21 or newer |
| OpenGL | 4.1 core | 4.1 core | 4.1 core (what macOS provides) |
| System | Windows 10 or later | X11 or Wayland | macOS 11 or later, Apple Silicon |

Git must be on the `PATH`, and the **first configure needs the network**: GLFW,
and on macOS FreeType and libpng, are downloaded and built from source then.
Later configures reuse the download.

Ninja is recommended on every platform, and is what the recipes here use; any
CMake generator works.

## Third-party code

| | How it gets there |
|---|---|
| Dear ImGui, NanoVG, stb, GLAD | Vendored in `third_party/`, nothing to do. `scripts/setup_deps.sh` (or `setup_deps.bat`) fetches fresh copies of the first three if you want them |
| GLFW 3.4 | Downloaded at configure time, patched (`cmake/glfw/`), and linked statically into sextant |
| FreeType, libpng | Optional. Windows: from vcpkg. Linux: the system packages. macOS: built from source into the library by default |

Without FreeType, text is rasterized by stb_truetype; without libpng, PNGs are
written by stb_image_write. Both fallbacks produce working output, and CMake
says when it takes one (see [Troubleshooting](#troubleshooting)). FreeType is
the recommended rasterizer: it follows each font's own choice of vertical
metrics.

## Windows

From a **`vcvars64`** shell (the "x64 Native Tools Command Prompt"), in the
source tree:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target install_dist
```

This builds with the stb fallbacks. For FreeType and libpng, install them once
with [vcpkg](https://vcpkg.io) in the static triplet, and pass its toolchain file
when configuring:

```bat
vcpkg install freetype:x64-windows-static-md libpng:x64-windows-static-md zlib:x64-windows-static-md
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --target install_dist
```

The `x64-windows-static-md` triplet is the default here: it links FreeType,
libpng and zlib *into* `sextant.dll`, so the DLL is the only file you ship. To
use vcpkg's dynamic libraries instead, configure with
`-DVCPKG_TARGET_TRIPLET=x64-windows`; the install step then copies their DLLs
into `dist/bin/` next to `sextant.dll`.

## Linux

Debian and Ubuntu (other distributions have the same packages under their own
names):

```bash
sudo apt-get install ninja-build libfreetype-dev libpng-dev libgl-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target install_dist
```

The X11 and Wayland packages are for GLFW, which is built with both. FreeType and
libpng are linked from the system, so `libsextant.so` uses the distribution's
`libfreetype` and `libpng` at runtime.

Both libstdc++ and libc++ work. For Clang with libc++, configure with
`-DCMAKE_CXX_COMPILER=clang++`, and `-stdlib=libc++` in `CMAKE_CXX_FLAGS`,
`CMAKE_EXE_LINKER_FLAGS` and `CMAKE_SHARED_LINKER_FLAGS`.

## macOS

```bash
brew install cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target install_dist
```

FreeType 2.14.3 and libpng 1.6.58 are built from source and linked into
`libsextant.dylib` (see `SEXTANT_MACOS_STATIC_DEPS` below), so the dylib needs
nothing beyond what every Mac has. zlib comes from the SDK.

- **Deployment target.** `CMAKE_OSX_DEPLOYMENT_TARGET` defaults to 11.0, the
  oldest macOS sextant supports. You can raise it; lower does not compile.
- **Universal binaries.** `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` is the
  standard switch. It needs universal dependencies, which Homebrew's are not, so
  it is untested, as are Intel Macs.
- **The static library** (`sextant_static`) does not use the bundled copies: its
  consumers resolve FreeType and libpng themselves. `brew install freetype libpng
  pkgconf` provides them; without them the static library uses the stb fallbacks.

## Build options

Pass any of these to the configure step as `-D<option>=<value>`.

| Option | Default | Effect |
|---|---|---|
| `SEXTANT_USE_FREETYPE` | `ON` | Rasterize text with FreeType. `OFF`, or FreeType not found: stb_truetype |
| `SEXTANT_USE_LIBPNG` | `ON` | Write PNGs with libpng. `OFF`, or libpng not found: stb_image_write |
| `SEXTANT_FETCH_GLFW` | `ON` | Download and build GLFW. `OFF` uses an installed GLFW 3.3 or newer (without sextant's macOS patches) |
| `SEXTANT_MACOS_STATIC_DEPS` | `ON` on macOS, else `OFF` | Build FreeType and libpng from source into `libsextant.dylib`. `OFF` links Homebrew's dylibs instead |
| `SEXTANT_BUILD_STATIC` | `ON` | Also build and install `sextant_static` |
| `SEXTANT_BUILD_TESTS` | `ON` | Build the test programs (not installed) |
| `SEXTANT_LOCAL_INSTALL_PREFIX` | `<source>/dist` | Where `install_dist` installs |
| `VCPKG_TARGET_TRIPLET` | `x64-windows-static-md` on Windows | vcpkg triplet for FreeType and libpng; only read with the vcpkg toolchain |
| `CMAKE_OSX_DEPLOYMENT_TARGET` | `11.0` | Oldest macOS the build runs on |

`install_dist` builds only the libraries it installs, so the test programs are
built only by a plain `cmake --build build`.

## Installing

sextant is meant to be consumed as an installed package, through
`find_package`, not added to your build with `add_subdirectory`.

```bash
cmake --build build --target install_dist        # into dist/ beside the source
cmake --install build --prefix /usr/local        # or anywhere, with CMake's own installer
```

The installed tree:

```
include/sextant/        the public headers (#include <sextant/sextant.h>)
lib/                    libsextant.so / libsextant.dylib, the import library sextant.lib,
                        and the static library with its GLFW archive
lib/cmake/sextant/      the package files find_package() reads
bin/                    sextant.dll (Windows)
```

**Debug and Release side by side.** Debug builds carry a `d` suffix
(`sextantd.dll`, `libsextantd.so`), so both configurations can be installed into
one prefix. Install from each build directory; `find_package` then picks the
matching one for each configuration of your project.

## Using sextant from CMake

```cmake
list(APPEND CMAKE_PREFIX_PATH "/path/to/sextant/dist")   # or -DCMAKE_PREFIX_PATH=...
find_package(sextant CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE sextant::sextant)
```

`sextant::sextant` is the shared library. Building against it needs only the
installed package: none of sextant's dependencies have to be present on your
machine.

**At runtime** the program has to find the shared library:

- **Windows** looks next to the executable, so copy `sextant.dll` there after
  each build:

  ```cmake
  if (WIN32)
      add_custom_command(TARGET my_app POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:sextant::sextant> $<TARGET_FILE_DIR:my_app>)
  endif ()
  ```

  It is the only file needed: with the default triplet it depends on nothing
  but system DLLs and the MSVC runtime.
- **Linux and macOS**: CMake records the library's directory in the executable's
  run path when you build against it, so a program run from its build tree finds
  it. For an installed program, install sextant somewhere the loader looks, or
  set your own `INSTALL_RPATH`.

## Linking statically

```cmake
target_link_libraries(my_app PRIVATE sextant::sextant_static)
```

A static library cannot carry its dependencies inside it, so your project links
GLFW (installed with sextant), FreeType and libpng itself. `find_package(sextant)`
looks for FreeType and libpng; if they are missing, the static target reports
it when your project is generated.

With vcpkg on Windows, **pin the same triplet sextant was built with**, before
your `project()` call:

```cmake
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
project(my_app CXX)
```

vcpkg resolves packages once, at the first `project()`. Without the pin your
project silently picks up the dynamic triplet's import libraries, which links
but defeats the point of linking statically. The shared library needs none of
this.

## Running the tests

A plain build (with `SEXTANT_BUILD_TESTS=ON`, the default) builds the test
programs into the build directory.

```bash
cmake --build build
./build/sextant_smoke          # saves sextant_smoke.png; with DISPLAY set, also shows it
./build/sextant_layout_test    # the full check suite; prints "N checks, 0 failures"
```

Both open an OpenGL context, so they need a display (the smoke test also waits
for ENTER when it shows its window). On a headless Linux machine, run them under
a virtual one:

```bash
xvfb-run -a -s "-screen 0 1920x1080x24" ./build/sextant_layout_test
```

`sextant_window_test` is the interactive gallery: it opens every plot kind in
windows to look at and play with.

## Troubleshooting

**"FreeType not found — falling back to stb_truetype"** or **"libpng not found —
falling back to stb_image_write"**: the build works, with the fallback. Install
the library (see your platform above) and configure again in a fresh build
directory.

**"Missing vendored file"**: a file under `third_party/` is gone. Run
`scripts/setup_deps.sh` (`setup_deps.bat` on Windows) to restore it.

**The first configure fails while fetching GLFW, FreeType or libpng**: those are
downloaded from GitHub on the first configure. Check the network and that `git`
is on the `PATH`.

**Your program starts and cannot find `sextant.dll`**: copy it next to the
executable; see [runtime](#using-sextant-from-cmake).
