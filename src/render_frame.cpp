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

// Draws every axes into the current framebuffer: one glClear, one layout pass,
// and two NanoVG frames total (the grid is looped inside each).
void render_frame(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                  const FigureSnapshot& fsnap, int target_w, int target_h,
                  int supersample, std::vector<AxesLayout>* out_layout,
                  const FigureLayout* given)
{
    const int iw = target_w > 0 ? target_w : ctx.width();
    const int ih = target_h > 0 ? target_h : ctx.height();

    // Layout stays in logical pixels; only the viewport and device-pixel ratio
    // know about supersampling.
    const int ss = std::clamp(supersample, 1, kMaxSupersample);
    glViewport(0, 0, iw * ss, ih * ss);
    glClearColor(0.93f, 0.93f, 0.93f, 1.0f);
    // Always clear depth too (the targets already have a depth attachment).
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Pixel-sized GL state (scissor, line widths) is scaled by hand.
    data.set_pixel_ratio(static_cast<float>(ss));

    // Figure geometry, shared with the SVG path.
    const FigureLayout fresh  = given ? FigureLayout{} : compute_figure_layout(fsnap, iw, ih);
    const FigureLayout& layout = given ? *given : fresh;

    // The box in pixels, per 3D cell per frame, for both NanoVG passes.
    std::vector<Box3DPlan> box_plans(layout.cells.size());
    for (std::size_t i = 0; i < layout.cells.size(); ++i)
        if (const RenderSnapshot3D* s3 = fsnap.axes[i].snap3d())
            box_plans[i] = plan_box3d(layout.cells[i].box3d->proj, *s3,
                                      layout.cells[i].box3d->xticks,
                                      layout.cells[i].box3d->yticks,
                                      layout.cells[i].box3d->zticks);

    // Pass 1 — NanoVG: backgrounds (3D: back panes and their grid).
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

    // Pass 2 — GLSL data. 2D: fixed back-to-front order; 3D: depth-tested scene.
    for (std::size_t i = 0; i < layout.cells.size(); ++i) {
        const auto& c = layout.cells[i];
        if (const RenderSnapshot3D* s3 = fsnap.axes[i].snap3d()) {
            data.set_frame_key(fsnap.data_generation, c.slot.index);
            // draw_scene3d() owns the ordering across all 3D kinds.
            const float fw = static_cast<float>(iw), fh = static_cast<float>(ih);
            data.draw_scene3d(*s3, c.box3d->proj, c.frame, fw, fh);
            continue;
        }
        const RenderSnapshot* s2 = fsnap.axes[i].snap2d();
        if (!s2) continue;
        const AllPlotData all = s2->all();
        // Cache key: the data generation, so pan/zoom keeps the buffers.
        data.set_frame_key(fsnap.data_generation, c.slot.index);
        data.draw_heatmap  (all.heatmaps,  c.tr, c.frame);
        data.draw_bars     (all.bars,      c.tr, c.frame);
        data.draw_lines    (all.lines,     c.tr, c.frame);
        // After fills, before scatter (same order as the SVG writer).
        data.draw_error_bars(all, c.tr, c.frame);
        data.draw_scatter  (all.scatters,  c.tr, c.frame);
        data.draw_scatter_z(all.scatter_z, c.tr, c.frame);
    }

    // Pass 3 — NanoVG: borders, ticks, labels, legend, colorbar, suptitle.
    ctx.begin_nvg_frame(iw, ih, static_cast<float>(ss));
    for (std::size_t i = 0; i < layout.cells.size(); ++i) {
        const auto& c = layout.cells[i];
        if (const RenderSnapshot3D* s3 = fsnap.axes[i].snap3d()) {
            // Annotation is drawn in pixel space over the scene. Contours
            // first: over the scene, under the box furniture (as in SVG).
            nvg.draw_contours3d(c, *s3, fsnap.data_generation, c.slot.index);
            nvg.draw_box3d_frame(box_plans[i], *s3);
            nvg.draw_title3d(c, *s3);
            // Legend then colorbar, beside the frame, as in 2D.
            if (c.has_legend())   nvg.draw_legend(c, s3->legend_opts);
            // The axes' colorbar styling applies to every bar.
            for (const auto& cb : c.colorbars) nvg.draw_colorbar(cb, s3->colorbar_opts);
            continue;
        }
        const RenderSnapshot& snap = *fsnap.axes[i].snap2d();
        // Contours first: over the data, under border/grid/ticks. Cached on
        // the data generation.
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
