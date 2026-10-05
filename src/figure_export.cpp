#include "figure_export.h"
#include "render_frame.h"
#include "coord_transform.h"
#include "renderer/render_device.h"
#include "renderer/data_renderer.h"
#include "renderer/fbo_readback.h"
#include "renderer/figure_layout.h"
#include "renderer/box3d.h"
#include "renderer/surface.h"
#include "output/file_write.h"
#include "output/png_writer.h"
#include "output/svg_writer.h"
#include "messages.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>

namespace sextant {

// Scoped peel-layer override; `data` may be the live window's renderer.
namespace {
struct PeelLayerScope {
    DataRenderer& d;
    int           saved;
    PeelLayerScope(DataRenderer& r, int want)
        : d(r), saved(r.peel_layers_override()) {
        if (want > 0) d.set_peel_layers_override(want);
    }
    ~PeelLayerScope() { d.set_peel_layers_override(saved); }
};

// A window's furniture over this snapshot's own measurements, or a fresh fit.
FigureLayout layout_for_export(const FigureSnapshot& fsnap, const FigureMeasure* on_screen,
                               int width, int height) {
    if (!on_screen) return compute_figure_layout(fsnap, width, height);
    return compute_figure_layout(fsnap, measure_figure(fsnap, on_screen), width, height);
}
} // namespace

RgbaImage render_figure_rgba(RenderDevice& dev, DataRenderer& data,
                             const FigureSnapshot& fsnap,
                             int width, int height, int supersample, int peel_layers,
                             const FigureMeasure* on_screen, float scale) {
    const PeelLayerScope peel(data, peel_layers);
    if (!(std::isfinite(scale) && scale > 0.0f)) scale = 1.0f;
    RgbaImage img;
    img.width  = std::max(1, static_cast<int>(std::lround(width * scale)));
    img.height = std::max(1, static_cast<int>(std::lround(height * scale)));
    FboReadback fbo(img.width, img.height, supersample);
    fbo.bind();
    // width/height stay logical; render_frame scales the viewport and
    // read_pixels() filters back down.
    const FigureLayout layout = layout_for_export(fsnap, on_screen, width, height);
    render_frame(dev, data, fsnap, width, height,
                 scale * static_cast<float>(fbo.supersample()), nullptr, &layout);
    img.pixels = fbo.read_pixels();
    fbo.unbind();
    return img;
}

void export_figure_png(RenderDevice& dev, DataRenderer& data,
                       const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height, int supersample, int peel_layers,
                       const FigureMeasure* on_screen, float scale) {
    const RgbaImage img = render_figure_rgba(dev, data, fsnap, width, height,
                                             supersample, peel_layers, on_screen, scale);
    write_png(path, img.width, img.height, img.pixels);
}

void export_figure_svg(const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height,
                       const SvgExportOptions& opts, SvgSaveReport* report,
                       const FigureMeasure* on_screen) {
    write_file(path, render_figure_svg(fsnap, width, height, opts, report, on_screen),
               "write_svg");
}

SaveResult perform_save(RenderDevice& dev, DataRenderer& data,
                        const FigureSnapshot& fsnap, const SaveRequest& req,
                        const FigureMeasure* on_screen, int supersample, float png_scale) {
    SaveResult res;
    try {
        if (req.width <= 0 || req.height <= 0)
            throw std::invalid_argument("the figure size must be positive");
        const std::string& path = req.path;
        const auto dot = path.rfind('.');
        const auto ext = (dot == std::string::npos) ? "" : path.substr(dot);
        if (ext == ".svg" || ext == ".SVG") {
            SvgSaveReport report;
            export_figure_svg(fsnap, path, req.width, req.height,
                              { .max_splits = static_cast<std::size_t>(std::max(0, req.max_splits)) },
                              &report, on_screen);
            // The file is written either way.
            if (!report.scene_order_exact) {
                res.exact   = false;
                res.warning = report.warning;
            }
        } else if (ext == ".png" || ext == ".PNG") {
            export_figure_png(dev, data, fsnap, path, req.width, req.height,
                              supersample, req.peel_layers, on_screen, png_scale);
        } else {
            throw std::invalid_argument("'" + path + "': the file name must end in "
                                        ".png or .svg");
        }
        res.written = true;
    } catch (const std::exception& ex) {
        res = SaveResult{ .written = false, .exact = true, .warning = ex.what() };
        emit_message(std::string("Save failed: ") + ex.what());
    } catch (...) {
        res = SaveResult{ .written = false, .exact = true, .warning = "unknown error" };
        emit_message("Save failed: unknown error");
    }
    return res;
}

std::string render_figure_svg(const FigureSnapshot& fsnap,
                              int width, int height,
                              const SvgExportOptions& opts, SvgSaveReport* report,
                              const FigureMeasure* on_screen) {
    // Same layout call as render_frame().
    const FigureLayout layout = layout_for_export(fsnap, on_screen, width, height);

    SvgFigureData fd;
    fd.width  = width;
    fd.height = height;
    fd.suptitle      = fsnap.suptitle;
    fd.suptitle_opts = fsnap.suptitle_opts;
    fd.axes.reserve(fsnap.axes.size());

    for (std::size_t i = 0; i < fsnap.axes.size(); ++i) {
        SvgAxesData sd;
        sd.width  = width;
        sd.height = height;
        sd.layout = layout.cells[i];

        if (const RenderSnapshot3D* s3 = fsnap.axes[i].snap3d()) {
            // Same plan_box3d() call as render_frame(), so both outputs agree.
            const Box3DLayout& b = *sd.layout.box3d;
            sd.box3d           = plan_box3d(b.proj, *s3, b.xticks, b.yticks, b.zticks);
            sd.bars3d          = plan_bars3d(b.proj, s3->bars3d);
            sd.surfaces3d      = plan_surfaces3d(b.proj, s3->surfaces);
            sd.planes3d        = plan_planes3d(b.proj, s3->planes);
            sd.markers3d       = plan_scatter3d(b.proj, s3->scatter3d);
            sd.lines3d         = plan_lines3d(b.proj, s3->lines3d);
            sd.meshes3d        = plan_surface_tri3d(b.proj, s3->surface_tri);
            sd.errbars3d       = plan_errorbars3d(b.proj, s3->scatter3d, s3->lines3d);
            // One order across all kinds by Newell's algorithm with splits;
            // done here because splits must be re-projected.
            PaintOrderStats paint_stats;
            sd.scene3d         = plan_scene3d(b.proj, sd.bars3d, sd.surfaces3d,
                                              sd.planes3d, sd.markers3d, sd.lines3d,
                                              sd.meshes3d, sd.errbars3d,
                                              &paint_stats,
                                              opts.max_tests, opts.max_splits);
            // Bounds are per cell; report the worst cell's counts.
            if (report) {
                report->splits = std::max(report->splits, paint_stats.splits);
                report->tests  = std::max(report->tests,  paint_stats.tests);
            }
            if (paint_stats.bailed) {
                // Name the bound that was hit and the option that raises it.
                const bool split_bound = paint_stats.bailed_on_splits;
                sd.scene3d_warning =
                    "scene order: subplot " + std::to_string(i + 1) +
                    " gave up after " + std::to_string(paint_stats.tests) +
                    " tests and " + std::to_string(paint_stats.splits) +
                    " splits; the rest of this scene is in depth order and some of it "
                    "is on the wrong side of something. Raise SvgExportOptions::" +
                    (split_bound ? "max_splits" : "max_tests") + " above " +
                    std::to_string(split_bound ? paint_stats.splits : paint_stats.tests) +
                    " to let it finish";
                if (report) {
                    report->scene_order_exact = false;
                    if (report->warning.empty()) report->warning = sd.scene3d_warning;
                }
            }
            // No cache: an export traces once.
            sd.contours3d      = plan_plane_contours(b.proj, s3->planes,
                                                     s3->axes_style.font_path,
                                                     nullptr, 0, 0);
            sd.box3d_style     = s3->box_style;
            sd.box3d_grid_opts = s3->grid_opts;
            sd.axes_style      = s3->axes_style;
            sd.title           = s3->title;
            // Boxes come from compute_figure_layout(); only cosmetics here.
            sd.colorbar_opts   = s3->colorbar_opts;
            sd.legend_opts     = s3->legend_opts;
            fd.axes.push_back(std::move(sd));
            continue;
        }

        const RenderSnapshot& snap = *fsnap.axes[i].snap2d();

        sd.lines     = snap.lines;
        sd.scatters  = snap.scatters;
        sd.bars      = snap.bars;
        sd.scatter_z = snap.scatter_z;
        sd.heatmaps  = snap.heatmaps;

        sd.title           = snap.title;
        sd.xtitle          = snap.xtitle;
        sd.ytitle          = snap.ytitle;
        sd.grid_enabled    = snap.grid_enabled;
        sd.grid_opts       = snap.grid_opts;
        sd.axes_style      = snap.axes_style;
        sd.legend_opts     = snap.legend_opts;
        sd.colorbar_opts   = snap.colorbar_opts;

        fd.axes.push_back(std::move(sd));
    }

    return svg_document(fd);
}

} // namespace sextant
