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

    // A 3D box from plan_box3d(): panes in pass 1 (they are back walls, so no
    // depth test is needed), frame and annotation in pass 3.
    void draw_box3d_panes(const Box3DPlan& plan, const RenderSnapshot3D& snap,
                          const PlotRect& frame);
    void draw_box3d_frame(const Box3DPlan& plan, const RenderSnapshot3D& snap);
    void draw_title3d(const CellLayout& cell, const RenderSnapshot3D& snap);

    // Positions come from compute_figure_layout() via CellLayout.

    // Heatmap contours with labels; call first in pass 3 (above data, below
    // furniture). data_generation/axes_index key the ContourCache.
    void draw_contours(const CellLayout& cell, const RenderSnapshot& snap,
                       unsigned long long data_generation, int axes_index);

    // The same for plane heatmaps, drawn in pass 3 as annotation.
    void draw_contours3d(const CellLayout& cell, const RenderSnapshot3D& snap,
                         unsigned long long data_generation, int axes_index);

    // Ticks, labels and (if grid_enabled) grid lines.
    void draw_ticks(const CellLayout& cell, const AxesStyle& style,
                    bool grid_enabled, const GridOptions& grid_opts);

    void draw_titles(const CellLayout& cell, const RenderSnapshot& snap);

    // The legend in its reserved box; call only when cell.has_legend().
    void draw_legend(const CellLayout& cell, const LegendOptions& opts);

    // One colorbar from cell.colorbars; the caller loops.
    void draw_colorbar(const ColorbarBox& box, const ColorbarOptions& opts);

    // Whole-figure title centered above the entire subplot grid.
    void draw_suptitle(int fig_w, float top_offset,
                       const std::string& text, const SuptitleOptions& opts);

    // Hover tooltip near (anchor_x, anchor_y), sized to the text and clamped
    // inside the figure. Call within a NanoVG frame, outside render_frame().
    void draw_hint(int fig_w, int fig_h, float anchor_x, float anchor_y,
                   const std::string& text);

private:
    // Stroke and label one planned contour set.
    void stroke_contours(const ContourDraw& d, const Color& color,
                         float linewidth, float fontsize,
                         const std::string& font_path);

    // font_path -> NanoVG font handle, registered on first use. "" and failed
    // loads return the default font (failures are cached).
    int font_for_path(const std::string& path);

    NVGcontext* vg_;
    int         font_ = -1;
    std::unordered_map<std::string, int> font_cache_;

    // Colorbar gradient images, cached by colormap and orientation (true =
    // horizontal) for the renderer's lifetime.
    std::map<std::pair<Colormap, bool>, int> colorbar_images_;

    // Traced contours held across frames (see contour.h).
    ContourCache contour_cache_;
};

} // namespace sextant
