#pragma once
#include "plot_objects.h"
#include <sextant/figure.h>
#include <string_view>

namespace sextant {
class GLContext;
class NvgRenderer;
class DataRenderer;
struct FigureMeasure;

// Renders fsnap at width x height into a throwaway FboReadback and writes the
// result as a PNG. Uses whatever GL context is already current (ctx/nvg/data
// are borrowed), so it is safe both with a dedicated headless context and with
// an already-visible window's context mid-frame: it only ever touches its own
// FBO, never the default framebuffer.
//
// supersample is the antialiasing factor: the plot is rasterized that many
// times larger in each axis and box-filtered back down before being written,
// so the file matches the antialiasing of the on-screen plot.
//
// peel_layers overrides the depth-peeling layer count for this export alone
// (0 = leave whatever the renderer is using). It is set and restored around
// the render, because `data` is frequently the *live window's* renderer and an
// export must not change how the window draws afterwards.
void export_figure_png(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                       const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height, int supersample = 1,
                       int peel_layers = 0,
                       const FigureMeasure* on_screen = nullptr);

// Both exports take `on_screen`, the measurements an open window is drawing
// with (LayoutStore::load()): given, the export keeps that window's axis
// furniture, so a figure saved while it is on screen is laid out as it is
// shown, at whatever size is asked for (v1.0 step 15.2). Null is a fresh fit,
// as a window just opened would make.

// Pure CPU-side SVG export — no GL context involved at all.
//
// `opts` carries the painter's two bounds and `report`, when given, comes back
// saying whether either of them bound. The warning sentence is built here, in
// one place, because it is written into three: the struct, the XML comment in
// the file, and the line Figure::savefig_svg() prints.
void export_figure_svg(const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height,
                       const SvgExportOptions& opts = {},
                       SvgSaveReport* report = nullptr,
                       const FigureMeasure* on_screen = nullptr);

} // namespace sextant
