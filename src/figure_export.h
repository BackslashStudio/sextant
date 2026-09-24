#pragma once
#include "plot_objects.h"
#include <sextant/figure.h>
#include <string_view>

namespace sextant {
class GLContext;
class NvgRenderer;
class DataRenderer;
struct FigureMeasure;

// Renders fsnap into its own FboReadback and writes a PNG, using the current GL
// context (safe mid-frame in a live window; never touches the default
// framebuffer). `supersample` matches on-screen antialiasing. `peel_layers`
// (0 = unchanged) is restored afterwards, since `data` may be the window's.
// `scale` (dpi / 96) multiplies the output pixels, not the layout: the file is
// round(width * scale) x round(height * scale).
void export_figure_png(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                       const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height, int supersample = 1,
                       int peel_layers = 0,
                       const FigureMeasure* on_screen = nullptr,
                       float scale = 1.0f);

// `on_screen` (LayoutStore::load()) keeps an open window's measured layout;
// null lays the figure out afresh.

// SVG export, CPU only. `report`, if given, says whether a painter bound was hit.
void export_figure_svg(const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height,
                       const SvgExportOptions& opts = {},
                       SvgSaveReport* report = nullptr,
                       const FigureMeasure* on_screen = nullptr);

} // namespace sextant
