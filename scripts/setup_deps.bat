@echo off
setlocal enabledelayedexpansion

:: setup_deps.bat -- fetch vendored third-party dependencies
:: Run once from the repo root:  scripts\setup_deps.bat
:: Requires: curl (built into Windows 10+), Python + pip (for GLAD)

:: Resolve repo root from this script's location
pushd "%~dp0.." || goto :error
set "REPO_ROOT=%CD%"
popd
set "THIRD_PARTY=%REPO_ROOT%\third_party"

:: ---------------------------------------------------------------------------
:: stb headers
:: ---------------------------------------------------------------------------
echo =^> Fetching stb headers...
if not exist "%THIRD_PARTY%\stb" mkdir "%THIRD_PARTY%\stb"
curl -fsSL -o "%THIRD_PARTY%\stb\stb_image_write.h" "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" || goto :error
curl -fsSL -o "%THIRD_PARTY%\stb\stb_truetype.h"    "https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h"    || goto :error
echo     stb_image_write.h + stb_truetype.h OK

:: ---------------------------------------------------------------------------
:: NanoVG
:: ---------------------------------------------------------------------------
echo =^> Fetching NanoVG...
if not exist "%THIRD_PARTY%\nanovg" mkdir "%THIRD_PARTY%\nanovg"
set "BASE=https://raw.githubusercontent.com/memononen/nanovg/master/src"
curl -fsSL -o "%THIRD_PARTY%\nanovg\nanovg.h"          "%BASE%/nanovg.h"          || goto :error
curl -fsSL -o "%THIRD_PARTY%\nanovg\nanovg.c"          "%BASE%/nanovg.c"          || goto :error
curl -fsSL -o "%THIRD_PARTY%\nanovg\nanovg_gl.h"       "%BASE%/nanovg_gl.h"       || goto :error
curl -fsSL -o "%THIRD_PARTY%\nanovg\nanovg_gl_utils.h" "%BASE%/nanovg_gl_utils.h" || goto :error
curl -fsSL -o "%THIRD_PARTY%\nanovg\fontstash.h"       "%BASE%/fontstash.h"       || goto :error
:: Download stb headers nanovg uses directly into its directory.
:: This avoids relying on symlinks, which break on Windows (core.symlinks=false).
curl -fsSL -o "%THIRD_PARTY%\nanovg\stb_image.h"    "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"    || goto :error
curl -fsSL -o "%THIRD_PARTY%\nanovg\stb_truetype.h" "https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h" || goto :error
echo     nanovg OK

:: ---------------------------------------------------------------------------
:: Dear ImGui
:: ---------------------------------------------------------------------------
echo =^> Fetching Dear ImGui (v1.92.8-docking)...
if not exist "%THIRD_PARTY%\imgui\backends" mkdir "%THIRD_PARTY%\imgui\backends"
:: Docking branch, not mainline — the widget panel uses DockSpaceOverViewport
:: + DockBuilder for a live-draggable plot/controls split (see spec_widgets.md).
set "IMGUI_BASE=https://raw.githubusercontent.com/ocornut/imgui/v1.92.8-docking"
for %%f in (imgui.h imgui.cpp imgui_draw.cpp imgui_widgets.cpp imgui_tables.cpp imgui_demo.cpp imgui_internal.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h imconfig.h) do (
    curl -fsSL -o "%THIRD_PARTY%\imgui\%%f" "%IMGUI_BASE%/%%f" || goto :error
)
for %%f in (imgui_impl_glfw.h imgui_impl_glfw.cpp imgui_impl_opengl3.h imgui_impl_opengl3.cpp imgui_impl_opengl3_loader.h) do (
    curl -fsSL -o "%THIRD_PARTY%\imgui\backends\%%f" "%IMGUI_BASE%/backends/%%f" || goto :error
)
echo     imgui OK

:: ---------------------------------------------------------------------------
:: Dear ImGui font assets (Roboto-Medium, panel theming)
:: ---------------------------------------------------------------------------
echo =^> Fetching Dear ImGui font assets (Roboto-Medium, panel theming)...
if not exist "%THIRD_PARTY%\imgui\misc\fonts" mkdir "%THIRD_PARTY%\imgui\misc\fonts"
:: Roboto-Medium.ttf is Google's Roboto font (Apache License 2.0), bundled
:: inside the imgui repo for its own examples/tools (imgui itself is MIT --
:: see imgui's top-level LICENSE.txt). No separate README.txt exists at this
:: path in the docking-branch tag.
curl -fsSL -o "%THIRD_PARTY%\imgui\misc\fonts\Roboto-Medium.ttf" "%IMGUI_BASE%/misc/fonts/Roboto-Medium.ttf" || goto :error
curl -fsSL -o "%THIRD_PARTY%\imgui\misc\fonts\binary_to_compressed_c.cpp" "%IMGUI_BASE%/misc/fonts/binary_to_compressed_c.cpp" || goto :error
echo     Roboto-Medium.ttf OK

:: ---------------------------------------------------------------------------
:: GLAD  (GL 4.1 Core)
:: ---------------------------------------------------------------------------
echo =^> Generating GLAD (GL 4.1 Core)...
where python >nul 2>&1
if errorlevel 1 (
    echo     WARNING: Python not found -- skipping GLAD generation.
    echo     Install Python then run:
    echo       pip install glad2
    echo       glad --api gl:core=4.1 --out-path third_party\glad c
    goto :deps_done
)
python -c "import glad" >nul 2>&1
if errorlevel 1 (
    echo     Installing glad2 via pip...
    pip install --quiet glad2 || goto :error
)
if not exist "%THIRD_PARTY%\glad" mkdir "%THIRD_PARTY%\glad"
glad --api gl:core=4.1 --out-path "%THIRD_PARTY%\glad" c || goto :error
echo     GLAD OK  (third_party\glad\include\glad\glad.h)

:deps_done
echo.
echo All dependencies ready. You can now build:
echo   cmake -S . -B cmake-build-debug -G Ninja
echo   cmake --build cmake-build-debug
exit /b 0

:error
echo.
echo ERROR: a step failed -- check the output above.
exit /b 1
