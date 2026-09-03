#include "figure_export.h"
#include "render_frame.h"
#include "coord_transform.h"
#include "renderer/gl_context.h"
#include "renderer/nvg_renderer.h"
#include "renderer/data_renderer.h"
#include "renderer/fbo_readback.h"
#include "renderer/figure_layout.h"
#include "output/png_writer.h"
#include "output/svg_writer.h"
#include <algorithm>

namespace sextant {

void export_figure_png(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                       const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height, int supersample) {
    FboReadback fbo(width, height, supersample);
    fbo.bind();
    // width/height stay logical: render_frame sizes the viewport itself from
    // the same factor the FBO was allocated with, and read_pixels() filters
    // the result back down to width x height.
    render_frame(ctx, nvg, data, fsnap, width, height, fbo.supersample());
    auto pixels = fbo.read_pixels();
    fbo.unbind();

    write_png(path, width, height, pixels);
}

void export_figure_svg(const FigureSnapshot& fsnap, std::string_view path,
                       int width, int height) {
    // The same call render_frame() makes, which is the point: this function
    // used to carry its own transcription of the grid, colorbar and legend
    // arithmetic, kept in step with the raster path by comments alone.
    const FigureLayout layout = compute_figure_layout(fsnap, width, height);

    SvgFigureData fd;
    fd.width  = width;
    fd.height = height;
    fd.suptitle      = fsnap.suptitle;
    fd.suptitle_opts = fsnap.suptitle_opts;
    fd.axes.reserve(fsnap.axes.size());

    for (std::size_t i = 0; i < fsnap.axes.size(); ++i) {
        const auto& snap = fsnap.axes[i].snap;

        SvgAxesData sd;
        sd.width  = width;
        sd.height = height;
        sd.layout = layout.cells[i];

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
