#pragma once
#include "figure_layout.h"
#include "plot_rect.h"
#include "../plot_objects.h"
#include "../contour.h"
#include "box3d.h"
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <utility>

struct NVGcontext;

namespace sextant {
struct Tick;

// Only draw_hint() sizes a box of its own; everything else is measured and
// positioned by figure_layout.h.
struct BoxSize { float w = 0.0f, h = 0.0f; };

class NvgRenderer {
public:
    explicit NvgRenderer(NVGcontext* vg);
    ~NvgRenderer();

    NvgRenderer(const NvgRenderer&) = delete;
    NvgRenderer& operator=(const NvgRenderer&) = delete;

    void draw_axes_background(const PlotRect& r);  // white fill — call before data
    void draw_axes_border(const PlotRect& r, const AxesStyle& style);  // outline only — call after data

    // A 3D axes' box, split across the same two NanoVG brackets a 2D axes
    // uses -- which is the whole reason 3D needs no fourth pass. The panes are
    // by construction the box's *back* walls, so nothing in the scene is ever
    // behind them and drawing them without a depth test costs nothing; the
    // frame and its annotation go over everything, which is what keeps text at
    // one size however far away the box edge it labels is (spec_3d.md §4).
    //
    // Both take a plan already reduced to pixels by plan_box3d(), shared with
    // the SVG writer, so neither renderer ever sees the camera.
    void draw_box3d_panes(const Box3DPlan& plan, const RenderSnapshot3D& snap,
                          const PlotRect& frame);
    void draw_box3d_frame(const Box3DPlan& plan, const RenderSnapshot3D& snap);
    void draw_title3d(const CellLayout& cell, const RenderSnapshot3D& snap);

    // Everything below takes a CellLayout rather than a rect plus loose
    // coordinates: every tick label, title and decoration box is positioned
    // once by compute_figure_layout() and shared with the SVG writer, so a
    // renderer draws at the given anchors rather than deriving them.

    // Contour lines over a heatmap, with their inline level labels. Draws
    // nothing unless a heatmap in the axes set HeatmapOptions::contours. Call
    // first in the post-data NanoVG pass, so contours land above the data and
    // below the axes furniture -- the slot the SVG writer emits them in too.
    // data_generation/axes_index key the traced geometry (ContourCache below).
    void draw_contours(const CellLayout& cell, const RenderSnapshot& snap,
                       unsigned long long data_generation, int axes_index);

    // The same, for the heatmaps on a 3D axes' planes. Drawn in pass 3 with
    // the box's own annotation rather than in the scene, because a contour is
    // annotation: it keeps a fixed width and label size on screen and is not
    // occluded by geometry in front of its plane (memory/spec_3d.md §4).
    void draw_contours3d(const CellLayout& cell, const RenderSnapshot3D& snap,
                         unsigned long long data_generation, int axes_index);

    // Grid lines are drawn alongside tick marks (they share pixel positions)
    // — gated by grid_enabled, styled from grid_opts. style governs the tick
    // marks/labels themselves (see AxesStyle).
    void draw_ticks(const CellLayout& cell, const AxesStyle& style,
                    bool grid_enabled, const GridOptions& grid_opts);

    void draw_titles(const CellLayout& cell, const RenderSnapshot& snap);

    // The legend renders outside the plot frame (like the colorbar), in the
    // box the layout already reserved and sized — cell.legend, filled from
    // cell.legend_entries. Call only when cell.has_legend().
    void draw_legend(const CellLayout& cell, const LegendOptions& opts);

    // One entry of cell.colorbars: a reserved strip carved out of the cell's
    // own plot frame rather than being additional space. Per bar rather than
    // per cell, because a cell can hold several and they share one
    // ColorbarOptions -- so the caller loops and the styling is passed once.
    void draw_colorbar(const ColorbarBox& box, const ColorbarOptions& opts);

    // Whole-figure title centered above the entire subplot grid.
    void draw_suptitle(int fig_w, float top_offset,
                       const std::string& text, const SuptitleOptions& opts);

    // Mouse hint: a floating tooltip near (anchor_x, anchor_y), auto-sized to
    // text (which may contain '\n') and clamped inside the figure. Callers
    // invoke this in their own begin_nvg_frame()/end_nvg_frame() bracket,
    // outside render_frame().
    void draw_hint(int fig_w, int fig_h, float anchor_x, float anchor_y,
                   const std::string& text);

private:
    // One planned contour set, stroked and labelled. Shared by the 2D and
    // plane paths, which differ only in what produced the plan.
    void stroke_contours(const ContourDraw& d, const Color& color,
                         float linewidth, float fontsize,
                         const std::string& font_path);

    // Resolves a font_path to a NanoVG font handle, registering it via
    // nvgCreateFont on first use. "" returns the renderer's default font; a
    // path that fails to load falls back to it too, cached so the failure is
    // not retried every frame.
    int font_for_path(const std::string& path);

    NVGcontext* vg_;
    int         font_ = -1;
    std::unordered_map<std::string, int> font_cache_;

    // Colorbar gradient images, cached by colormap and orientation (true =
    // horizontal) and kept alive for this renderer's lifetime -- see
    // draw_colorbar() for why.
    std::map<std::pair<Colormap, bool>, int> colorbar_images_;

    // Traced contour geometry held across frames; see contour.h for the
    // invalidation rule. Marching squares is O(rows*cols) per level.
    ContourCache contour_cache_;
};

} // namespace sextant
