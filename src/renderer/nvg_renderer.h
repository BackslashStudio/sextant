#pragma once
#include "figure_layout.h"
#include "plot_rect.h"
#include "../plot_objects.h"
#include "../contour.h"
#include <vector>
#include <string>
#include <unordered_map>

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

    // Cell.colorbar is the reserved strip, carved out of the cell's own plot
    // frame rather than being additional space. Call only when
    // cell.has_colorbar().
    void draw_colorbar(const CellLayout& cell, const ColorbarOptions& opts);

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
    // Resolves a font_path to a NanoVG font handle, registering it via
    // nvgCreateFont on first use. "" returns the renderer's default font; a
    // path that fails to load falls back to it too, cached so the failure is
    // not retried every frame.
    int font_for_path(const std::string& path);

    NVGcontext* vg_;
    int         font_ = -1;
    std::unordered_map<std::string, int> font_cache_;

    // Colorbar gradient images, cached by colormap and kept alive for this
    // renderer's lifetime -- see draw_colorbar() for why.
    std::unordered_map<Colormap, int> colorbar_images_;

    // Traced contour geometry held across frames; see contour.h for the
    // invalidation rule. Marching squares is O(rows*cols) per level.
    ContourCache contour_cache_;
};

} // namespace sextant
