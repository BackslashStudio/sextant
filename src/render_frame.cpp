#include "render_frame.h"
#include "coord_transform.h"
#include "renderer/gl_context.h"
#include "renderer/nvg_renderer.h"
#include "renderer/data_renderer.h"
#include "renderer/figure_layout.h"
#include <sextant/figure.h>   // kMaxSupersample
#include <glad/glad.h>
#include <algorithm>
#include <vector>

namespace sextant {

// Draws every axes in fsnap into the current framebuffer, once per real
// frame: a single glClear, a single grid-layout pass, and exactly two
// NanoVG begin/end brackets total (not one pair per axes) — the grid is
// looped *inside* each bracket so N axes never means 2N nvgBeginFrame calls.
void render_frame(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                  const FigureSnapshot& fsnap, int target_w, int target_h,
                  int supersample, std::vector<AxesLayout>* out_layout)
{
    const int iw = target_w > 0 ? target_w : ctx.width();
    const int ih = target_h > 0 ? target_h : ctx.height();

    // Everything below stays in logical pixels; only the viewport (and, via
    // the device-pixel ratio, NanoVG's and DataRenderer's own pixel-sized
    // state) knows about the supersampled target. The data shaders divide
    // by a logical uResolution to reach NDC, so they map onto the enlarged
    // viewport correctly with no change at all.
    const int ss = std::clamp(supersample, 1, kMaxSupersample);
    glViewport(0, 0, iw * ss, ih * ss);
    glClearColor(0.93f, 0.93f, 0.93f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // Pixel-sized GL state that isn't expressed in the logical coordinate
    // space — scissor rectangles and line widths — has to be scaled by hand.
    data.set_pixel_ratio(static_cast<float>(ss));

    // The whole figure's geometry, from the same function the SVG path
    // calls. Text measurement is why this no longer has to happen inside a
    // NanoVG frame: layout used to need nvgTextBounds for the legend box,
    // which returns garbage outside begin/end frame (see text_metrics.h).
    const FigureLayout layout = compute_figure_layout(fsnap, iw, ih);

    // Pass 1 — NanoVG: each axes' background, under the data.
    ctx.begin_nvg_frame(iw, ih, static_cast<float>(ss));
    for (const auto& c : layout.cells)
        nvg.draw_axes_background(c.frame);
    ctx.end_nvg_frame();

    if (out_layout) {
        out_layout->clear();
        out_layout->reserve(layout.cells.size());
        for (const auto& c : layout.cells)
            out_layout->push_back({ c.slot, c.tr });
    }

    // Pass 2 — Custom GLSL: data (back to front), per axes
    for (std::size_t i = 0; i < layout.cells.size(); ++i) {
        const AllPlotData all = fsnap.axes[i].snap.all();
        const auto& c = layout.cells[i];
        // Invalidation key for DataRenderer's caches — the *data* generation,
        // so a pan/zoom (which republishes the snapshot every frame) doesn't
        // invalidate buffers whose contents depend only on the data.
        data.set_frame_key(fsnap.data_generation, c.slot.index);
        data.draw_heatmap  (all.heatmaps,  c.tr, c.frame);
        data.draw_bars     (all.bars,      c.tr, c.frame);
        data.draw_lines    (all.lines,     c.tr, c.frame);
        // After the fills, so a bar's error bar sits over its own bar; before
        // scatter, so markers stay on top of theirs. The SVG writer emits in
        // the same place, which is what keeps the two outputs agreeing.
        data.draw_error_bars(all, c.tr, c.frame);
        data.draw_scatter  (all.scatters,  c.tr, c.frame);
        data.draw_scatter_z(all.scatter_z, c.tr, c.frame);
    }

    // Pass 3 — NanoVG: every axes' border, ticks, labels, legend, colorbar
    // on top of data, plus the whole-figure suptitle once.
    ctx.begin_nvg_frame(iw, ih, static_cast<float>(ss));
    for (std::size_t i = 0; i < layout.cells.size(); ++i) {
        const auto& c    = layout.cells[i];
        const auto& snap = fsnap.axes[i].snap;
        // First, so a heatmap's contours sit over all the data the
        // pass above drew but under the border, grid and ticks. Keyed on the
        // *data* generation like DataRenderer's caches, so a pan does not
        // re-run marching squares. No-op unless a heatmap asked for contours.
        nvg.draw_contours(c, snap, fsnap.data_generation, c.slot.index);
        nvg.draw_axes_border(c.frame, snap.axes_style);
        nvg.draw_ticks(c, snap.axes_style, snap.grid_enabled, snap.grid_opts);
        nvg.draw_titles(c, snap);
        if (c.has_legend())   nvg.draw_legend(c, snap.legend_opts);
        if (c.has_colorbar()) nvg.draw_colorbar(c, snap.colorbar_opts);
    }
    nvg.draw_suptitle(iw, layout.suptitle_band, fsnap.suptitle, fsnap.suptitle_opts);
    ctx.end_nvg_frame();
}

} // namespace sextant
