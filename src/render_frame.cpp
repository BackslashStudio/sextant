#include "render_frame.h"
#include "coord_transform.h"
#include "renderer/gl_context.h"
#include "renderer/nvg_renderer.h"
#include "renderer/data_renderer.h"
#include "renderer/figure_layout.h"
#include "renderer/box3d.h"
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
                  int supersample, std::vector<AxesLayout>* out_layout,
                  const FigureLayout* given)
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
    // Depth joins the clear unconditionally. Both offscreen targets already
    // carry a GL_DEPTH24_STENCIL8 attachment (for NanoVG's stencil) and the
    // window defaults to 24-bit depth, so nothing had to be allocated for
    // this -- and clearing a buffer no 2D pass reads or writes costs a
    // measured nothing while removing a whole class of "the second 3D frame
    // looks different from the first".
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Pixel-sized GL state that isn't expressed in the logical coordinate
    // space — scissor rectangles and line widths — has to be scaled by hand.
    data.set_pixel_ratio(static_cast<float>(ss));

    // The whole figure's geometry, from the same function the SVG path
    // calls. Text measurement is why this no longer has to happen inside a
    // NanoVG frame: layout used to need nvgTextBounds for the legend box,
    // which returns garbage outside begin/end frame (see text_metrics.h).
    const FigureLayout fresh  = given ? FigureLayout{} : compute_figure_layout(fsnap, iw, ih);
    const FigureLayout& layout = given ? *given : fresh;

    // The box reduced to pixels, once per 3D cell per frame, shared by both
    // NanoVG brackets below. Cheap (a few hundred projected points) and
    // camera-dependent, so there is nothing to cache across frames the way
    // contours are cached.
    std::vector<Box3DPlan> box_plans(layout.cells.size());
    for (std::size_t i = 0; i < layout.cells.size(); ++i)
        if (const RenderSnapshot3D* s3 = fsnap.axes[i].snap3d())
            box_plans[i] = plan_box3d(layout.cells[i].box3d->proj, *s3,
                                      layout.cells[i].box3d->xticks,
                                      layout.cells[i].box3d->yticks,
                                      layout.cells[i].box3d->zticks);

    // Pass 1 — NanoVG: each axes' background, under the data. For a 3D cell
    // that is the three back panes and their grid, which are behind
    // everything in the scene by construction.
    ctx.begin_nvg_frame(iw, ih, static_cast<float>(ss));
    for (std::size_t i = 0; i < layout.cells.size(); ++i) {
        const auto& c = layout.cells[i];
        if (const RenderSnapshot3D* s3 = fsnap.axes[i].snap3d())
            nvg.draw_box3d_panes(box_plans[i], *s3, c.frame);
        else
            nvg.draw_axes_background(c.frame);
    }
    ctx.end_nvg_frame();

    if (out_layout) {
        out_layout->clear();
        out_layout->reserve(layout.cells.size());
        for (const auto& c : layout.cells)
            out_layout->push_back({ c.slot, c.tr,
                                    c.box3d ? std::optional<Projector3D>(c.box3d->proj)
                                            : std::nullopt,
                                    c.cell });
    }

    // Pass 2 — Custom GLSL: data, per axes. For a 2D cell that is a
    // painter's stack in a fixed back-to-front order; for a 3D one it is a
    // depth-tested scene, which is the one thing the fixed order cannot
    // express -- planes and bars included, both drawn here in pass 2.
    for (std::size_t i = 0; i < layout.cells.size(); ++i) {
        const auto& c = layout.cells[i];
        if (const RenderSnapshot3D* s3 = fsnap.axes[i].snap3d()) {
            data.set_frame_key(fsnap.data_generation, c.slot.index);
            // One call, not a sequence of per-kind ones. The scene's opaque
            // half needs no order and its translucent half needs a single one
            // across every kind, which is a decision about the scene rather
            // than about any plot type in it -- so it lives in
            // DataRenderer::draw_scene3d() and this loop no longer expresses
            // an ordering at all. It used to, by the order of four calls, and
            // that is exactly how a surface came to be drawn after a bar grid
            // regardless of which was in front.
            const float fw = static_cast<float>(iw), fh = static_cast<float>(ih);
            data.draw_scene3d(*s3, c.box3d->proj, c.frame, fw, fh);
            continue;
        }
        const RenderSnapshot* s2 = fsnap.axes[i].snap2d();
        if (!s2) continue;
        const AllPlotData all = s2->all();
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
        const auto& c = layout.cells[i];
        if (const RenderSnapshot3D* s3 = fsnap.axes[i].snap3d()) {
            // Over the scene, not in it: the box edges, ticks and labels are
            // pixel-space geometry and text, so nothing here can be scaled by
            // a camera. That is the whole of the annotation-invariance
            // requirement, and it is structural rather than a special case.
            // First, so a plane's contours sit over the scene and under the
            // box's own furniture -- the slot the 2D contours occupy, and the
            // slot the SVG writer emits them in.
            nvg.draw_contours3d(c, *s3, fsnap.data_generation, c.slot.index);
            nvg.draw_box3d_frame(box_plans[i], *s3);
            nvg.draw_title3d(c, *s3);
            // Hoisted decoration, out of the scene and beside the frame: the
            // same calls, the same boxes and the same styling a 2D cell's take
            // (spec_3d.md §6). Legend then colorbar, as in 2D.
            if (c.has_legend())   nvg.draw_legend(c, s3->legend_opts);
            // One styling for every bar of the cell -- the axes' own, as in 2D.
            for (const auto& cb : c.colorbars) nvg.draw_colorbar(cb, s3->colorbar_opts);
            continue;
        }
        const RenderSnapshot& snap = *fsnap.axes[i].snap2d();
        // First, so a heatmap's contours sit over all the data the
        // pass above drew but under the border, grid and ticks. Keyed on the
        // *data* generation like DataRenderer's caches, so a pan does not
        // re-run marching squares. No-op unless a heatmap asked for contours.
        nvg.draw_contours(c, snap, fsnap.data_generation, c.slot.index);
        nvg.draw_axes_border(c.frame, snap.axes_style);
        nvg.draw_ticks(c, snap.axes_style, snap.grid_enabled, snap.grid_opts);
        nvg.draw_titles(c, snap);
        if (c.has_legend())   nvg.draw_legend(c, snap.legend_opts);
        for (const auto& cb : c.colorbars) nvg.draw_colorbar(cb, snap.colorbar_opts);
    }
    nvg.draw_suptitle(iw, layout.suptitle_band, fsnap.suptitle, fsnap.suptitle_opts);
    ctx.end_nvg_frame();
}

} // namespace sextant
