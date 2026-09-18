#include "figure_export.h"
#include "render_frame.h"
#include "coord_transform.h"
#include "renderer/gl_context.h"
#include "renderer/nvg_renderer.h"
#include "renderer/data_renderer.h"
#include "renderer/fbo_readback.h"
#include "renderer/figure_layout.h"
#include "renderer/box3d.h"
#include "renderer/surface.h"
#include "output/png_writer.h"
#include "output/svg_writer.h"
#include <algorithm>

namespace sextant {

// One export's peel-layer override, restored on the way out. `data` is
// commonly the live window's own renderer -- savefig() from a caller thread
// borrows it through the window thread -- so setting the count and leaving it
// set would change how the window draws for the rest of its life.
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

void export_figure_png(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                       const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height, int supersample, int peel_layers,
                       const FigureMeasure* on_screen) {
    const PeelLayerScope peel(data, peel_layers);
    FboReadback fbo(width, height, supersample);
    fbo.bind();
    // width/height stay logical: render_frame sizes the viewport itself from
    // the same factor the FBO was allocated with, and read_pixels() filters
    // the result back down to width x height.
    const FigureLayout layout = layout_for_export(fsnap, on_screen, width, height);
    render_frame(ctx, nvg, data, fsnap, width, height, fbo.supersample(), nullptr, &layout);
    auto pixels = fbo.read_pixels();
    fbo.unbind();

    write_png(path, width, height, pixels);
}

void export_figure_svg(const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height,
                       const SvgExportOptions& opts, SvgSaveReport* report,
                       const FigureMeasure* on_screen) {
    // The same call render_frame() makes, which is the point: this function
    // used to carry its own transcription of the grid, colorbar and legend
    // arithmetic, kept in step with the raster path by comments alone.
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
            // The same plan_box3d() call render_frame() makes, from the same
            // projector -- which is the whole point of the plan existing: the
            // vector path cannot project the box differently from the raster
            // one, because it does not project it at all.
            const Box3DLayout& b = *sd.layout.box3d;
            sd.box3d           = plan_box3d(b.proj, *s3, b.xticks, b.yticks, b.zticks);
            sd.bars3d          = plan_bars3d(b.proj, s3->bars3d);
            sd.surfaces3d      = plan_surfaces3d(b.proj, s3->surfaces);
            sd.planes3d        = plan_planes3d(b.proj, s3->planes);
            sd.markers3d       = plan_scatter3d(b.proj, s3->scatter3d);
            sd.lines3d         = plan_lines3d(b.proj, s3->lines3d);
            sd.meshes3d        = plan_surface_tri3d(b.proj, s3->surface_tri);
            sd.errbars3d       = plan_errorbars3d(b.proj, s3->scatter3d, s3->lines3d);
            // The one order across all three, by Newell's algorithm, splitting
            // where no order exists otherwise (v1.0 step 9). It runs here and
            // not in the writer because a split has to be re-projected and the
            // writer never sees a projector.
            PaintOrderStats paint_stats;
            sd.scene3d         = plan_scene3d(b.proj, sd.bars3d, sd.surfaces3d,
                                              sd.planes3d, sd.markers3d, sd.lines3d,
                                              sd.meshes3d, sd.errbars3d,
                                              &paint_stats,
                                              opts.max_tests, opts.max_splits);
            // The report is per *figure* and the bounds are per *cell*, so
            // what it carries is the worst cell's counts -- which is the
            // number a caller has to beat when they raise max_splits, since
            // every cell is bounded separately.
            if (report) {
                report->splits = std::max(report->splits, paint_stats.splits);
                report->tests  = std::max(report->tests,  paint_stats.tests);
            }
            if (paint_stats.bailed) {
                // One sentence, built once, and it says which bound bound and
                // what to do about it -- a warning that reports a number
                // without saying which knob takes it is a warning the reader
                // has to come here to act on.
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
            // No cache: an export traces once and exits, which is the same
            // bargain the 2D contour path strikes in the writer.
            sd.contours3d      = plan_plane_contours(b.proj, s3->planes,
                                                     s3->axes_style.font_path,
                                                     nullptr, 0, 0);
            sd.box3d_style     = s3->box_style;
            sd.box3d_grid_opts = s3->grid_opts;
            sd.axes_style      = s3->axes_style;
            sd.title           = s3->title;
            // Hoisted decoration: the boxes are already in sd.layout, carved
            // by compute_figure_layout(); only the cosmetics come from here.
            // A colorbar's styling travels with the plane that asked for it;
            // a legend's belongs to the axes, since a legend keys the cell.
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

    write_svg(path, fd);
}

} // namespace sextant
