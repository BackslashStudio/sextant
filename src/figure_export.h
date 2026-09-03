#pragma once
#include "plot_objects.h"
#include <string_view>

namespace sextant {
class GLContext;
class NvgRenderer;
class DataRenderer;

// Renders fsnap at width x height into a throwaway FboReadback and writes the
// result as a PNG. Uses whatever GL context is already current (ctx/nvg/data
// are borrowed), so it is safe both with a dedicated headless context and with
// an already-visible window's context mid-frame: it only ever touches its own
// FBO, never the default framebuffer.
//
// supersample is the antialiasing factor: the plot is rasterized that many
// times larger in each axis and box-filtered back down before being written,
// so the file matches the antialiasing of the on-screen plot.
void export_figure_png(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                       const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height, int supersample = 1);

// Pure CPU-side SVG export — no GL context involved at all.
void export_figure_svg(const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height);

} // namespace sextant
