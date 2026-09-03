#!/usr/bin/env bash
# setup_deps.sh — fetch vendored third-party dependencies
# Run once from the repo root: bash scripts/setup_deps.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY="$REPO_ROOT/third_party"

echo "==> Fetching stb headers..."
mkdir -p "$THIRD_PARTY/stb"
curl -fsSL -o "$THIRD_PARTY/stb/stb_image_write.h" \
    "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"
curl -fsSL -o "$THIRD_PARTY/stb/stb_truetype.h" \
    "https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h"
echo "    stb_image_write.h + stb_truetype.h OK"

echo "==> Fetching NanoVG..."
mkdir -p "$THIRD_PARTY/nanovg"
BASE="https://raw.githubusercontent.com/memononen/nanovg/master/src"
curl -fsSL -o "$THIRD_PARTY/nanovg/nanovg.h"    "$BASE/nanovg.h"
curl -fsSL -o "$THIRD_PARTY/nanovg/nanovg.c"    "$BASE/nanovg.c"
curl -fsSL -o "$THIRD_PARTY/nanovg/nanovg_gl.h" "$BASE/nanovg_gl.h"
curl -fsSL -o "$THIRD_PARTY/nanovg/nanovg_gl_utils.h" "$BASE/nanovg_gl_utils.h"
# fontstash.h is bundled inside the NanoVG repo (version-matched)
curl -fsSL -o "$THIRD_PARTY/nanovg/fontstash.h"  "$BASE/fontstash.h"
# Copy stb headers nanovg needs directly into its directory.
# This avoids relying on symlinks, which break on Windows (core.symlinks=false).
curl -fsSL -o "$THIRD_PARTY/nanovg/stb_image.h" \
    "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
curl -fsSL -o "$THIRD_PARTY/nanovg/stb_truetype.h" \
    "https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h"
echo "    nanovg OK"

echo "==> Fetching Dear ImGui (v1.92.8-docking)..."
mkdir -p "$THIRD_PARTY/imgui/backends"
# Docking branch, not mainline — the widget panel uses DockSpaceOverViewport
# + DockBuilder to let the plot/controls split be dragged live (see
# spec_widgets.md). Plain (non-docking) tags don't have these symbols.
IMGUI_BASE="https://raw.githubusercontent.com/ocornut/imgui/v1.92.8-docking"
for f in imgui.h imgui.cpp imgui_draw.cpp imgui_widgets.cpp imgui_tables.cpp imgui_demo.cpp \
         imgui_internal.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h imconfig.h; do
    curl -fsSL -o "$THIRD_PARTY/imgui/$f" "$IMGUI_BASE/$f"
done
for f in imgui_impl_glfw.h imgui_impl_glfw.cpp imgui_impl_opengl3.h imgui_impl_opengl3.cpp imgui_impl_opengl3_loader.h; do
    curl -fsSL -o "$THIRD_PARTY/imgui/backends/$f" "$IMGUI_BASE/backends/$f"
done
echo "    imgui OK"

echo "==> Fetching Dear ImGui font assets (Roboto-Medium, panel theming)..."
mkdir -p "$THIRD_PARTY/imgui/misc/fonts"
# Roboto-Medium.ttf is Google's Roboto font (Apache License 2.0), bundled
# inside the imgui repo for its own examples/tools (imgui itself is MIT —
# see imgui's top-level LICENSE.txt). No separate README.txt exists at this
# path in the docking-branch tag.
curl -fsSL -o "$THIRD_PARTY/imgui/misc/fonts/Roboto-Medium.ttf" "$IMGUI_BASE/misc/fonts/Roboto-Medium.ttf"
curl -fsSL -o "$THIRD_PARTY/imgui/misc/fonts/binary_to_compressed_c.cpp" "$IMGUI_BASE/misc/fonts/binary_to_compressed_c.cpp"
echo "    Roboto-Medium.ttf OK"

echo "==> Generating GLAD (GL 4.1 Core)..."
if ! python3 -c "import glad" 2>/dev/null; then
    echo "    Installing glad2 via pip..."
    pip install --quiet glad2
fi
mkdir -p "$THIRD_PARTY/glad"
glad --api gl:core=4.1 --out-path "$THIRD_PARTY/glad" c
echo "    GLAD OK  (third_party/glad/include/glad/gl.h)"

echo ""
echo "All dependencies ready. You can now build:"
echo "  cmake -S . -B cmake-build-debug -G Ninja"
echo "  cmake --build cmake-build-debug"
