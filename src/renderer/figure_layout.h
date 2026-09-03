#pragma once
#include "plot_rect.h"
#include "../coord_transform.h"
#include "../plot_objects.h"
#include "../tick.h"
#include "sextant/style.h"
#include <string>
#include <vector>

namespace sextant {

// The figure's whole layout, computed once per frame from the snapshot and the
// target size -- the single definition shared by the raster path
// (render_frame.cpp) and the SVG path (figure_export.cpp).
//
// Decoration space is *measured* from the text that goes in it, via
// text_metrics.h, so a font-size change moves the text and the space it
// occupies together. A *margin* is therefore not per-cell padding but the gap
// between the figure's edge and the subplot grid as a whole; see
// FigureMargins in sextant/style.h.
//
// Two properties this file exists to guarantee: the raster and vector outputs
// cannot disagree about where anything is, because they call this rather than
// two transcriptions of the same arithmetic; and text position and reserved
// space are computed from one set of numbers, so a title cannot be centred in
// a band sized for a different font.

// ---------------------------------------------------------------------------
// Which series get a key, in which order. One definition, because the list
// determines the reserved box size, the drawn contents and the plot frame's
// width -- it used to exist once per output path.

enum class LegendKind { Line, Marker, Bar };

struct LegendEntry {
    Color       color;
    std::string label;
    LegendKind  kind  = LegendKind::Line;
    LineStyle   style = LineStyle::Solid;   // Line entries only
};

std::vector<LegendEntry> collect_legend_entries(const RenderSnapshot& snap);

// Legend box metrics, shared by the sizing here and by both renderers' own
// drawing passes so a swatch can never land outside the box reserved for it.
inline constexpr float kLegendPad     = 8.0f;
inline constexpr float kLegendSwatchW = 20.0f;
inline constexpr float kLegendGap     = 6.0f;

inline float legend_row_height(float fontsize) { return fontsize + 8.0f; }

// ---------------------------------------------------------------------------
// Spacing constants
// ---------------------------------------------------------------------------
// Gap between a tick mark and its label.
inline constexpr float kTickLabelGap = 4.0f;
// Gap between a title and whatever it titles (the frame, for an axes title;
// the tick labels, for an x/y title).
inline constexpr float kTitleGap = 6.0f;
// Colorbar strip: gap from the plot frame, bar width, and the gap between
// the bar and its vmin/vmax numbers. The numbers' own width is measured.
inline constexpr float kColorbarGap      = 15.0f;
inline constexpr float kColorbarWidth    = 15.0f;
inline constexpr float kColorbarLabelGap = 5.0f;
// A plot frame is never allowed to collapse or invert, however little room
// is left after the decorations take theirs — a zero-width frame divides by
// zero in CoordTransform and a negative one draws inside-out.
inline constexpr float kMinFrameSize = 4.0f;

// ---------------------------------------------------------------------------
// Insets
// ---------------------------------------------------------------------------
// Space each cell gives up to its own decorations: tick marks, tick labels,
// axis titles. Uniform across the whole grid (the per-side maximum over every
// axes), which is what keeps rows and columns aligned. Legend and colorbar are
// deliberately not here: they are per-axes opt-ins carved out of their own
// cell, so one axes' legend does not shrink every other axes' frame.
struct PlotInsets {
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
};

PlotInsets compute_uniform_insets(const FigureSnapshot& fsnap);

// ---------------------------------------------------------------------------
// Per-cell decorations, and the inverse layout
// ---------------------------------------------------------------------------
// Legend and colorbar are carved out of the cell that owns them rather than
// reserved uniformly (see PlotInsets), so their widths are what stands between
// a cell's size and its plot frame's. Computed here once so the forward and
// inverse directions subtract and add exactly the same number.
struct CellDecorations {
    bool     has_colorbar   = false;
    Colormap colorbar_cmap  = Colormap::Viridis;
    float    colorbar_vmin  = 0.0f, colorbar_vmax = 1.0f;
    float    colorbar_block = 0.0f;   // gap + bar + label gap + widest number

    std::vector<LegendEntry> legend_entries;
    float legend_box_w = 0.0f, legend_box_h = 0.0f;
    float legend_block = 0.0f;        // box width + a positive offset_x

    float total_carve() const { return colorbar_block + legend_block; }
};

CellDecorations compute_cell_decorations(const RenderSnapshot& snap);

struct LayoutSize { float width = 0.0f, height = 0.0f; };

// Inverse of compute_figure_layout(): the figure size at which slot
// `slot_index`'s plot frame comes out `frame_w` x `frame_h`.
//
// Exact and closed-form, because nothing layout measures depends on the
// figure's size -- tick values come from the resolved limits, not from how
// much room the axis has, so the insets and decoration widths are the same at
// every figure size and there is nothing to iterate towards. Only integer
// rounding at the call site keeps the achieved frame from being exact.
//
// Which slot matters: legend and colorbar are per-cell, so two subplots of one
// grid can have frames of different widths.
LayoutSize figure_size_for_frame(const FigureSnapshot& fsnap, int slot_index,
                                 float frame_w, float frame_h);

// ---------------------------------------------------------------------------
// Per-cell and per-figure layout
// ---------------------------------------------------------------------------
// Every text anchor below is the *centre* of the line, the point NanoVG's
// NVG_ALIGN_MIDDLE takes. SVG wants a baseline, which is that centre plus
// middle_baseline_offset(), so the two paths share the anchor and differ only
// in the idiom applied to it.
struct CellLayout {
    AxesSlot       slot;
    PlotRect       frame;    // the data area, i.e. what the spine outlines
    CoordTransform tr;
    std::vector<Tick> xticks, yticks;

    // Carved out of this cell only; w <= 0 means absent.
    PlotRect                 legend{};
    std::vector<LegendEntry> legend_entries;

    PlotRect colorbar{};
    Colormap colorbar_cmap = Colormap::Viridis;
    float    colorbar_vmin = 0.0f, colorbar_vmax = 1.0f;

    // Title anchors. Each sits flush against the edge of the (uniform) inset
    // rather than a fixed distance from its own frame, so titles line up
    // across a row or column even when one cell's labels are wider.
    float title_x = 0.0f,  title_y = 0.0f;
    float xtitle_x = 0.0f, xtitle_y = 0.0f;
    float ytitle_x = 0.0f, ytitle_y = 0.0f;   // centre of the rotated line

    // Tick labels: x labels hang from xlabel_top (NVG_ALIGN_TOP), y labels
    // end at ylabel_right (NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE).
    float xlabel_top   = 0.0f;
    float ylabel_right = 0.0f;

    bool has_legend()   const { return legend.w   > 0.0f; }
    bool has_colorbar() const { return colorbar.w > 0.0f; }
};

struct FigureLayout {
    std::vector<CellLayout> cells;   // index-parallel with FigureSnapshot::axes
    float      suptitle_band = 0.0f;
    PlotInsets insets;
};

FigureLayout compute_figure_layout(const FigureSnapshot& fsnap, int fig_w, int fig_h);

// ---------------------------------------------------------------------------
// Baseline conversions
// ---------------------------------------------------------------------------
// NanoVG positions text by an alignment point; SVG positions it by the
// baseline. These are the exact offsets between the two, from the same
// vertical metrics fontstash uses, so a string lands on the same pixel row in
// both outputs.
float middle_baseline_offset(const std::string& font_path, float fontsize);
float top_baseline_offset(const std::string& font_path, float fontsize);

} // namespace sextant
