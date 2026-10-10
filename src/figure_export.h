#pragma once
#include "plot_objects.h"
#include <sextant/figure.h>
#include <string>
#include <string_view>

namespace sextant {
class RenderDevice;
class DataRenderer;
struct FigureMeasure;

// Renders fsnap into its own FboReadback and returns the pixels, with `dev` on
// the current GL context (safe mid-frame in a live window; leaves the bound
// framebuffer and viewport as they were). `supersample` matches on-screen
// antialiasing. `peel_layers` (0 = unchanged) is restored afterwards, since
// `data` may be the window's. `scale` (dpi / 96) multiplies the output pixels,
// not the layout: the image is round(width * scale) x round(height * scale).
RgbaImage render_figure_rgba(RenderDevice& dev, DataRenderer& data,
                             const FigureSnapshot& fsnap,
                             int width, int height, int supersample = 1,
                             int peel_layers = 0,
                             const FigureMeasure* on_screen = nullptr,
                             float scale = 1.0f);

// render_figure_rgba() encoded as PNG and written to `path`.
void export_figure_png(RenderDevice& dev, DataRenderer& data,
                       const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height, int supersample = 1,
                       int peel_layers = 0,
                       const FigureMeasure* on_screen = nullptr,
                       float scale = 1.0f);

// `on_screen` (LayoutStore::load()) keeps an open window's measured layout;
// null lays the figure out afresh.

// SVG export, CPU only. `report`, if given, says whether a painter bound was hit.
std::string render_figure_svg(const FigureSnapshot& fsnap,
                              int width, int height,
                              const SvgExportOptions& opts = {},
                              SvgSaveReport* report = nullptr,
                              const FigureMeasure* on_screen = nullptr);

// render_figure_svg() written to `path`, byte for byte.
void export_figure_svg(const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height,
                       const SvgExportOptions& opts = {},
                       SvgSaveReport* report = nullptr,
                       const FigureMeasure* on_screen = nullptr);

// A save asked for in the window (the Save dialog's result), with
// every size resolved: the figure size in logical pixels, never <= 0.
struct SaveRequest {
    std::string path;
    int width = 0;
    int height = 0;
    int max_splits = 0;  // SvgExportOptions::max_splits (0 = automatic)
    int peel_layers = 0; // PngExportOptions::peel_layers (0 = automatic)
};

// What a save did. `written`: the file is there; if not, `warning` says why.
// `exact`: false when an SVG gave up ordering part of a 3D scene (written all
// the same), with the exporter's sentence in `warning`.
struct SaveResult {
    bool written = false;
    bool exact = true;
    std::string warning;
};

// Writes `req` as SVG or PNG by its extension. Never throws (nothing above a
// window frame may): a failure comes back as written = false and also goes to
// the message handler as "Save failed: ...". A PNG is `png_scale` (dpi / 96)
// times the figure size, at `supersample`.
SaveResult perform_save(RenderDevice& dev, DataRenderer& data,
                        const FigureSnapshot& fsnap, const SaveRequest& req,
                        const FigureMeasure* on_screen, int supersample, float png_scale);

} // namespace sextant
