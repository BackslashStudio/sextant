#include "render_frame.h"
#include "coord_transform.h"
#include "renderer/render_device.h"
#include "renderer/nvg_renderer.h"
#include "renderer/data_renderer.h"
#include "renderer/figure_layout.h"
#include "renderer/box3d.h"
#include "renderer/text_plan.h"
#include "pie_geometry.h"
#include "rich_text.h"
#include <cmath>
#include <glad/glad.h>
#include <algorithm>
#include <vector>

namespace sextant {

std::vector<AxesLayout> axes_layouts(const FigureLayout& layout) {
    std::vector<AxesLayout> out;
    out.reserve(layout.cells.size());
    for (const auto& c : layout.cells) {
        out.push_back({ c.slot, c.tr,
                        c.box3d ? std::optional<Projector3D>(c.box3d->proj) : std::nullopt,
                        c.cell, {} });
        for (const CellLayout& t : c.twins) out.back().twins.push_back({ t.layer, t.tr });
    }
    return out;
}

// Draws every axes into the current framebuffer: one glClear, one layout pass,
// and two NanoVG frames total (the grid is looped inside each).
void render_frame(RenderDevice& dev, DataRenderer& data,
                  const FigureSnapshot& fsnap, int target_w, int target_h,
                  float pixel_ratio, std::vector<AxesLayout>* out_layout,
                  const FigureLayout* given)
{
    // FigureOptions::mathtext for every label measured and drawn below.
    const MathTextScope math(fsnap.mathtext, fsnap.math_font);
    NvgRenderer& nvg = dev.renderer();
    const int iw = std::max(1, target_w);
    const int ih = std::max(1, target_h);

    // Layout stays in logical pixels; only the viewport and device-pixel ratio
    // know about the display scale and supersampling.
    const float ratio = std::isfinite(pixel_ratio) && pixel_ratio > 0.0f ? pixel_ratio : 1.0f;
    glViewport(0, 0, static_cast<int>(std::lround(iw * ratio)),
               static_cast<int>(std::lround(ih * ratio)));
    glClearColor(fsnap.background.r, fsnap.background.g, fsnap.background.b,
                 fsnap.background.a);
    // Always clear depth too (the targets already have a depth attachment).
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Pixel-sized GL state (scissor, line widths) is scaled by hand.
    data.set_pixel_ratio(ratio);

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
    dev.begin_nvg_frame(iw, ih, ratio);
    for (std::size_t i = 0; i < layout.cells.size(); ++i) {
        const auto& c = layout.cells[i];
        if (const RenderSnapshot3D* s3 = fsnap.axes[i].snap3d())
            nvg.draw_box3d_panes(box_plans[i], *s3, c.frame);
        else if (const RenderSnapshot* s2 = fsnap.axes[i].snap2d(); s2 && s2->axis_on && c.polar)
            nvg.draw_polar_background(*c.polar, s2->axes_style.background);
        else if (s2 && s2->axis_on)
            nvg.draw_axes_background(c.frame, s2->axes_style.background);
    }
    dev.end_nvg_frame();

    if (out_layout) *out_layout = axes_layouts(layout);

    // Pass 2 — GLSL data. 2D: in call order; 3D: depth-tested scene.
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
        if (c.polar) {
            // Lines in unit space through c.tr; the map changes with every
            // rotate/zoom, so no cached buffers (key 0).
            data.set_frame_key(0, c.slot.index);
            data.draw_plots(polar_sheet(*s2, c.polar->map), c.tr, c.frame);
            continue;
        }
        const auto& twins = fsnap.axes[i].twins;
        if (!twins.empty()) {
            // A twinned cell: one call order across its layers.
            std::vector<const RenderSnapshot*> snaps{s2};
            std::vector<const CoordTransform*> trs{&c.tr};
            std::vector<int> keys{c.slot.index};
            for (std::size_t k = 0; k < twins.size(); ++k) {
                snaps.push_back(&twins[k].snap);
                trs.push_back(&c.twins[k].tr);
                keys.push_back(axes_key(c.slot.index, twins[k].layer));
            }
            data.draw_layers(snaps, trs, keys, fsnap.data_generation, c.frame);
            continue;
        }
        // Cache key: the data generation, so pan/zoom keeps the buffers.
        data.set_frame_key(fsnap.data_generation, c.slot.index);
        // In call order, as the SVG writer.
        data.draw_plots(*s2, c.tr, c.frame);
    }

    // Pass 3 — NanoVG: borders, ticks, labels, texts, legend, colorbar, suptitle.
    dev.begin_nvg_frame(iw, ih, ratio);
    for (std::size_t i = 0; i < layout.cells.size(); ++i) {
        const auto& c = layout.cells[i];
        if (const RenderSnapshot3D* s3 = fsnap.axes[i].snap3d()) {
            // Annotation is drawn in pixel space over the scene. Contours
            // first: over the scene, under the box furniture (as in SVG).
            nvg.draw_contours3d(c, *s3, data.contour_cache(), fsnap.data_generation, c.slot.index);
            nvg.draw_box3d_frame(box_plans[i], *s3);
            nvg.draw_title3d(c, *s3);
            // Texts over the box furniture, under the legend.
            if (!s3->texts.empty())
                nvg.draw_texts(plan_texts3d(s3->texts, c.box3d->proj, c.frame), c.frame);
            // Legend then colorbar, beside the frame, as in 2D.
            if (c.has_legend())   nvg.draw_legend(c, s3->legend_opts);
            // The axes' colorbar styling applies to every bar.
            for (const auto& cb : c.colorbars) nvg.draw_colorbar(cb, s3->colorbar_opts);
            continue;
        }
        const RenderSnapshot& snap = *fsnap.axes[i].snap2d();
        if (c.polar) {
            if (snap.axis_on) nvg.draw_polar_frame(*c.polar, snap.axes_style, snap.grid_enabled, snap.grid_opts);
            nvg.draw_titles(c, snap);
            if (c.has_legend()) nvg.draw_legend(c, snap.legend_opts);
            continue;
        }
        const auto& twins = fsnap.axes[i].twins;
        // Contours first: over the data, under border/grid/ticks. Cached on
        // the data generation.
        nvg.draw_contours(c, snap, data.contour_cache(), fsnap.data_generation, c.slot.index);
        for (std::size_t k = 0; k < twins.size(); ++k)
            nvg.draw_contours(c.twins[k], twins[k].snap, data.contour_cache(), fsnap.data_generation,
                              axes_key(c.slot.index, twins[k].layer));
        nvg.draw_contour_labels(c, snap);
        for (std::size_t k = 0; k < twins.size(); ++k) nvg.draw_contour_labels(c.twins[k], twins[k].snap);
        // A twin's frame is its host's: drawn once, the twinned edges in their
        // axes' colors.
        // Axes::set_axis_visible(false) skips a layer's border, ticks and grid.
        if (snap.axis_on) {
            nvg.draw_axes_border(c.frame, snap.axes_style, twin_edge_colors(fsnap.axes[i]));
            nvg.draw_ticks(c, snap.axes_style, snap.grid_enabled, snap.grid_opts);
        }
        for (std::size_t k = 0; k < twins.size(); ++k) {
            const RenderSnapshot& t = twins[k].snap;
            if (t.axis_on) nvg.draw_ticks(c.twins[k], t.axes_style, t.grid_enabled, t.grid_opts);
        }
        nvg.draw_titles(c, snap);
        for (std::size_t k = 0; k < twins.size(); ++k) nvg.draw_titles(c.twins[k], twins[k].snap);
        // Texts over the frame furniture, under the legend and colorbar; a
        // layer's pie labels and percents under its own texts.
        const auto draw_texts_of = [&](const RenderSnapshot& s, const CoordTransform& tr) {
            if (!s.pies.empty())
                nvg.draw_texts(plan_texts(pie_texts(s.pies, s.axes_style.label_fontsize), tr, c.frame), c.frame);
            if (!s.texts.empty()) nvg.draw_texts(plan_texts(s.texts, tr, c.frame), c.frame);
        };
        draw_texts_of(snap, c.tr);
        for (std::size_t k = 0; k < twins.size(); ++k) draw_texts_of(twins[k].snap, c.twins[k].tr);
        if (c.has_legend())   nvg.draw_legend(c, snap.legend_opts);
        for (const auto& cb : c.colorbars) nvg.draw_colorbar(cb, snap.colorbar_opts);
    }
    nvg.draw_suptitle(iw, layout.suptitle_band, fsnap.suptitle, fsnap.suptitle_opts);
    dev.end_nvg_frame();
}

} // namespace sextant
