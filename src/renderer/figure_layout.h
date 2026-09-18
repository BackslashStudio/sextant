#pragma once
#include "plot_rect.h"
#include "../coord_transform.h"
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "../tick.h"
#include "sextant/style.h"
#include <memory>
#include <mutex>
#include <optional>
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
    std::string name;
    LegendKind  kind  = LegendKind::Line;
    LineStyle   style = LineStyle::Solid;   // Line entries only
    // Marker entries only. Without it the key cannot be the shape it keys:
    // both renderers drew a hardcoded circle, so a diamond series was keyed by
    // a circle in the window *and* in the SVG -- the two outputs agreeing with
    // each other while both disagreed with the picture.
    MarkerStyle marker = MarkerStyle::Circle;

    // Marker entries only, and alpha 0 (the default) means no edge at all --
    // which every kind but scatter_z wants, since a key filled with the
    // series' own colour needs nothing around it. A continuous-colour series
    // has no one colour to fill with, so its key is white with a black edge;
    // the *shape* is what the key says, and the colorbar carries the colours.
    //
    // A `Cross` or `Plus` is strokes with no interior, so an edged one is
    // stroked in `edge` rather than in `color`: filling it white would draw
    // white-on-white and key the series with nothing.
    Color edge = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Line entries only: draw the swatch as `cmap` swept along its length
    // instead of a flat stroke in `color` (v1.0 step 13.4).
    //
    // This is the *line's* form of the answer `scatter_z` gave in 11.5, and it
    // is a different answer to the same question because a line has a
    // different swatch. A continuous-colour series has no one colour a swatch
    // could honestly show; a marker solves that by keying the shape (white
    // with a black edge) and leaving the colours to the bar, but every line's
    // swatch is the same shape, so shape cannot be what distinguishes two of
    // them. Sweeping the map along the stroke names the scale instead -- which
    // is what actually tells two colormapped paths apart, and reads as a
    // miniature of the bar it points at.
    //
    // A colormapped path is keyed at all, rather than dropping out as a
    // colormapped *surface* does, for §7c's reason: a sheet has a shape in the
    // picture to be recognized by and a path has only where it goes.
    bool     swept = false;
    Colormap cmap  = Colormap::Viridis;
};

std::vector<LegendEntry> collect_legend_entries(const RenderSnapshot& snap);

// A 3D axes' keys are its planes' keys, in plane order -- the same function
// asked about each plane's own sheet, since a plane holds a RenderSnapshot.
std::vector<LegendEntry> collect_legend_entries(const RenderSnapshot3D& snap);

// Legend box metrics, shared by the sizing here and by both renderers' own
// drawing passes so a swatch can never land outside the box reserved for it.
inline constexpr float kLegendPad     = 8.0f;
inline constexpr float kLegendSwatchW = 20.0f;
inline constexpr float kLegendGap     = 6.0f;
// Between two entries on one row of a legend laid out in rows (the Outside
// top/bottom anchors).
inline constexpr float kLegendColGap  = 12.0f;

inline float legend_row_height(float fontsize) { return fontsize + 8.0f; }

// ---------------------------------------------------------------------------
// Spacing constants
// ---------------------------------------------------------------------------
// Gap between a tick mark and its label.
inline constexpr float kTickLabelGap = 4.0f;
// Gap between a title and whatever it titles (the frame, for an axes title;
// the tick labels, for an x/y title).
inline constexpr float kTitleGap = 6.0f;
// Gap between a colorbar and its vmin/vmax numbers, and between the numbers
// and the bar's name. The bar's own margin and thickness are ColorbarOptions';
// the numbers' size is measured.
inline constexpr float kColorbarLabelGap = 5.0f;
// A plot frame is never allowed to collapse or invert, however little room
// is left after the decorations take theirs — a zero-width frame divides by
// zero in CoordTransform and a negative one draws inside-out.
inline constexpr float kMinFrameSize = 4.0f;

// ---------------------------------------------------------------------------
// Insets
// ---------------------------------------------------------------------------
// Space on each side of a rectangle: what a cell gives up between its edge and
// its plot frame, or what an axes' furniture takes around its frame.
struct PlotInsets {
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
};

// ---------------------------------------------------------------------------
// Per-cell decorations, and the inverse layout
// ---------------------------------------------------------------------------
// What a cell's legend and colorbars measure, independent of the figure's
// size. Computed here once so the forward and inverse directions subtract and
// add exactly the same numbers.
struct ColorbarSpec {
    Colormap    cmap = Colormap::Viridis;
    float       vmin = 0.0f, vmax = 1.0f;
    std::string name;
    // The bar's whole thickness away from what it stacks against: margin +
    // width + number gap + the numbers' extent (their widest width beside a
    // vertical bar, one line beside a horizontal one), and when there is a
    // name a further gap + one line height for it.
    float       block = 0.0f;
};

struct CellDecorations {
    // One per request, in find_colorbar_requests() order, which is the order
    // they are placed outward from the frame.
    std::vector<ColorbarSpec> colorbars;
    float colorbar_block = 0.0f;      // the sum of their blocks

    std::vector<LegendEntry> legend_entries;
    // Each entry's swatch + gap + text width, for laying entries out in rows.
    std::vector<float> legend_entry_w;
    // The box laid out as one column, which is its shape at every anchor but
    // the Outside top/bottom ones; those wrap to the extended frame's width,
    // which only the figure's size decides (see layout_legend_rows()).
    float legend_box_w = 0.0f, legend_box_h = 0.0f;
    // margin + legend_box_w: what a side-anchored legend reserves.
    float legend_block = 0.0f;
};

// A legend laid out in rows no wider than `max_w`: its box size and each
// entry's swatch position relative to the box's top-left corner. At least one
// entry per row, so an entry wider than `max_w` overflows rather than looping.
struct LegendSlot { float x = 0.0f, cy = 0.0f; };   // swatch left, row centre
struct LegendRows {
    float w = 0.0f, h = 0.0f;
    std::vector<LegendSlot> slots;
};
LegendRows layout_legend_rows(const CellDecorations& dec, float fontsize, float max_w);

CellDecorations compute_cell_decorations(const RenderSnapshot& snap);

// The 3D overload. A colorbar asked for by a heatmap on one of the axes'
// planes is *hoisted* to the cell and carved out of it exactly as a 2D axes'
// own would be -- it is pixel-space decoration beside the frame, and a bar put
// into the scene would foreshorten, turn with the camera and be occluded by
// what it explains. A legend is not hoisted until step 6 brings the kinds that
// can produce entries; see the definition.
CellDecorations compute_cell_decorations(const RenderSnapshot3D& snap);

// ---------------------------------------------------------------------------
// Measurements: the stored half of the layout (v1.0 step 15.2)
// ---------------------------------------------------------------------------
// Everything the layout measures that does not depend on the figure's size.
// The window keeps one of these and lays every frame out from it, re-measuring
// only on an event (see LayoutStore), so a pan that changes the width of the y
// tick labels does not move the frame under the cursor. Limits, ticks, the
// CoordTransform and the Projector3D are *not* here: they are resolved from
// the snapshot on every layout, so a stored measure never draws stale data.
enum class DecorSide { None, Left, Right, Top, Bottom };

struct CellMeasure {
    AxesSlot        slot;
    bool            is3d = false;
    PlotInsets      axis;              // furniture around the frame; zero for 3D
    float           title = 0.0f;      // band above everything else
    float           frame_margin = 0.0f;
    CellDecorations dec;
    DecorSide       legend = DecorSide::None;   // None also for an Inside legend
    DecorSide       bars   = DecorSide::Right;
    float           legend_margin = 0.0f;
    float           legend_fontsize = 10.0f;
};

// What lines up across the grid, solved from the cells: 2D axis furniture per
// column (left/right) and per row (top/bottom), and the axes title band per
// row. See solve_grid() for the rule.
struct GridMeasure {
    int rows = 1, cols = 1;
    std::vector<float> ax_l, ax_r, ax_t, ax_b;
    std::vector<float> title;
};

struct FigureMeasure {
    std::vector<CellMeasure> cells;   // index-parallel with FigureSnapshot::axes
    GridMeasure              grid;
    float                    suptitle_band = 0.0f;
};

// Measures `fsnap`. With `frozen`, each 2D cell whose slot `frozen` also holds
// as a 2D cell keeps `frozen`'s axis furniture instead of measuring its own --
// the one part of a measure navigation changes -- and everything else is
// measured from `fsnap`. That is how an export made while a window is open
// saves the layout on screen: the same furniture, applied to the snapshot
// being saved, whose other measurements equal the window's unless the caller
// changed something without a refresh().
FigureMeasure measure_figure(const FigureSnapshot& fsnap, const FigureMeasure* frozen = nullptr);

// Whether `m` was measured from a snapshot with `fsnap`'s grid: the same slots
// in the same order, each of the same kind, with the same number of legend
// entries and colorbars. A measure that does not fit is never laid out --
// the layout calls below measure afresh instead.
bool measure_fits(const FigureMeasure& m, const FigureSnapshot& fsnap);

// ---------------------------------------------------------------------------
// Grid weights (v1.0 step 15.3)
// ---------------------------------------------------------------------------
// The weights a grid of `n` tracks is shared out by: `ratios` when it has one
// finite positive entry per track, otherwise equal weights. Figure's setters
// refuse anything else, so the fallback only ever meets an empty vector -- or a
// grid whose shape changed under a stored one, which it must not crash on.
std::vector<float> grid_weights(const std::vector<float>& ratios, int n);

// Where each column and row of the grid is, in figure pixels: the extent left
// after the margins, the suptitle band and the gaps, shared out by weight. A
// cell is its first track's position to its last track's far edge, so a span
// takes in the gaps inside it. Also what the plot panel's boundary drag
// hit-tests against.
struct GridTracks {
    std::vector<float> col_x, col_w;
    std::vector<float> row_y, row_h;
};
GridTracks grid_tracks(const FigureSnapshot& fsnap, float suptitle_band, int fig_w, int fig_h);

struct LayoutSize { float width = 0.0f, height = 0.0f; };

// Inverse of compute_figure_layout(): the figure size at which slot
// `slot_index`'s plot frame comes out `frame_w` x `frame_h`.
//
// Exact and closed-form. Everything the horizontal reservations measure is
// independent of the figure's size -- tick values come from the resolved
// limits, not from how much room the axis has. The one thing that does depend
// on size is a row-wrapped legend's height, which depends only on its own
// frame's width, and that is the width being asked for; so there is nothing to
// iterate towards. Only integer rounding at the call site keeps the achieved
// frame from being exact.
//
// Which slot matters: legend and colorbar are per-cell, so two subplots of one
// grid can have frames of different sizes.
LayoutSize figure_size_for_frame(const FigureSnapshot& fsnap, int slot_index,
                                 float frame_w, float frame_h);

// The same inverse from stored measurements, so a window's resize dialog
// answers for the layout it is showing.
LayoutSize figure_size_for_frame(const FigureSnapshot& fsnap, const FigureMeasure& measure,
                                 int slot_index, float frame_w, float frame_h);

// ---------------------------------------------------------------------------
// Per-cell and per-figure layout
// ---------------------------------------------------------------------------
// Every text anchor below is the *centre* of the line, the point NanoVG's
// NVG_ALIGN_MIDDLE takes. SVG wants a baseline, which is that centre plus
// middle_baseline_offset(), so the two paths share the anchor and differ only
// in the idiom applied to it.
// What a 3D cell has instead of a CoordTransform and two tick lists. Present
// exactly when the cell's snapshot is a RenderSnapshot3D; the fields it does
// not replace (frame, title anchor, legend, colorbar) stay in CellLayout and
// mean the same thing in both kinds, which is what lets a 3D cell reuse the
// grid, margin, suptitle and decoration-carve arithmetic unchanged.
struct Box3DLayout {
    Projector3D       proj;
    std::vector<Tick> xticks, yticks, zticks;
};

// One colorbar as the layout hands it to a drawing path: where it goes and
// which scale it explains. Its cosmetics are not here -- those are the cell's
// one ColorbarOptions, which every bar beside that frame shares.
struct ColorbarBox {
    PlotRect    rect{};
    Colormap    cmap = Colormap::Viridis;
    float       vmin = 0.0f, vmax = 1.0f;

    // Top/Bottom bars: vmin at the left end. Left/Right bars: vmin at the
    // bottom. Everything below is already placed for it, offset included.
    bool        horizontal = false;

    // The two numbers' anchors, vertically centred on the line, with
    // `num_align` the horizontal alignment both are drawn with: Left beside a
    // Right bar, Right beside a Left one, Center under or over a horizontal one.
    float       vmin_x = 0.0f, vmin_y = 0.0f;
    float       vmax_x = 0.0f, vmax_y = 0.0f;
    HAlign      num_align = HAlign::Left;

    // The bar's name and the centre of the line that draws it. Beside a
    // vertical bar that line is rotated, and the anchor is in the *unrotated*
    // frame -- the point both renderers rotate about, exactly as
    // CellLayout::ytitle_x/y is for an axis title. Empty name: no text, and
    // the block reserved none.
    std::string name;
    float       name_x = 0.0f, name_y = 0.0f;
};

struct CellLayout {
    AxesSlot       slot;
    PlotRect       cell;     // the whole subplot: the grid's share, before insets
    PlotRect       frame;    // the data area, i.e. what the spine outlines
    CoordTransform tr;
    std::vector<Tick> xticks, yticks;

    // 3D cells only; `tr`, the two tick lists above and the x/y title anchors
    // below are 2D-only in exactly the complementary way.
    std::optional<Box3DLayout> box3d;
    bool is_3d() const { return box3d.has_value(); }

    // The frame plus its axis furniture plus AxesStyle::frame_margin: what an
    // Outside legend and every colorbar are placed against (v1.0 step 15.1).
    PlotRect       extended{};

    // Between the cell's edge and the frame on each side. Two parts: the axis
    // furniture (aligned per column on the left/right and per row on the
    // top/bottom) and the axes title band (aligned per row), which line frames
    // up across the grid; plus what this cell alone opts into -- its
    // frame_margin, legend and colorbars -- which never moves another cell.
    PlotInsets     reserved{};

    // As drawn, offset included; w <= 0 means absent. `legend_slots` is
    // index-parallel with the entries: each one's swatch left edge and row
    // centre, in figure pixels.
    PlotRect                 legend{};
    std::vector<LegendEntry> legend_entries;
    std::vector<LegendSlot>  legend_slots;

    // One per request the cell's snapshot made, placed outward from the
    // extended frame in that order. Empty means none; a cell can carry several
    // since v1.0 step 11.1, and they share one ColorbarOptions because the
    // styling is cosmetics of the cell rather than of the series.
    std::vector<ColorbarBox> colorbars;

    // Title anchors. The axes title sits at the top of the cell, above
    // everything the cell reserves, so titles line up across a row. The x/y
    // titles sit at the outer edge of the axis furniture, which is aligned per
    // row and per column, so they line up too even when one cell's labels are
    // wider.
    float title_x = 0.0f,  title_y = 0.0f;
    float xtitle_x = 0.0f, xtitle_y = 0.0f;
    float ytitle_x = 0.0f, ytitle_y = 0.0f;   // centre of the rotated line

    // Where the two axis lines landed, in pixels, and which way their ticks
    // point (v1.0 step 19). `xaxis_y` is the x axis line's y and `yaxis_x` the
    // y axis line's x; the tick directions are +1 for the low side of the line
    // (down for x, left for y) and -1 for the high side, so a High axis' marks
    // and numbers fall outside the frame rather than over the data.
    //
    // `*_interior` says the line is not on a frame edge and so needs a stroke
    // of its own: at Low and High it *is* an edge, already drawn -- or
    // deliberately not drawn -- by that edge's AxesStyle::spine_* flag. This
    // is resolved after clamping, so an origin pinned outside the visible
    // range lands on an edge and is interior no longer.
    float xaxis_y = 0.0f,   yaxis_x = 0.0f;
    float xtick_dir = 1.0f, ytick_dir = -1.0f;
    bool  xaxis_interior = false, yaxis_interior = false;

    // Tick labels: x labels hang from xlabel_top (NVG_ALIGN_TOP), which is the
    // top of the text box on whichever side of the line they fell, so the
    // alignment never changes and only the number does. y labels are centred
    // on the tick (NVG_ALIGN_MIDDLE) at ylabel_x, horizontally aligned by
    // ylabel_align -- Right beside a left-hand axis, Left beside a right-hand
    // one, so a column of numbers lines up on the edge facing its axis.
    float  xlabel_top   = 0.0f;
    float  ylabel_x     = 0.0f;
    HAlign ylabel_align = HAlign::Right;

    bool has_legend()   const { return legend.w   > 0.0f; }
    bool has_colorbar() const { return !colorbars.empty(); }
};

struct FigureLayout {
    std::vector<CellLayout> cells;   // index-parallel with FigureSnapshot::axes
    float      suptitle_band = 0.0f;
};

// A fresh measure, laid out at fig_w x fig_h. With `out_measure`, the measure
// is handed back too, from the same one pass over the data -- which is what a
// refit wants, since resolving limits is O(N) in the point count.
FigureLayout compute_figure_layout(const FigureSnapshot& fsnap, int fig_w, int fig_h,
                                   FigureMeasure* out_measure = nullptr);

// Stored measurements laid out at fig_w x fig_h: no text is measured. Limits,
// ticks and transforms still come from `fsnap`. A `measure` that does not fit
// `fsnap` (measure_fits()) is ignored and a fresh one taken.
FigureLayout compute_figure_layout(const FigureSnapshot& fsnap, const FigureMeasure& measure,
                                   int fig_w, int fig_h);

// ---------------------------------------------------------------------------
// The window's stored layout
// ---------------------------------------------------------------------------
// Owned by the window's render thread. Re-measures when the snapshot's
// layout_generation differs from the one last measured (every submitted change
// except navigation -- see FigureSnapshot::layout_generation), when the size
// differs (window or plot panel resize), or when a refit was asked for
// (File > Refit layout); otherwise lays out from what it holds.
class LayoutStore {
public:
    // Render thread: this frame's layout.
    FigureLayout fit(const FigureSnapshot& fsnap, int fig_w, int fig_h);

    // Render thread: the next fit() re-measures.
    void request_refit() { refit_ = true; }

    // Any thread: what the window is drawing with, null before its first
    // frame. Immutable once published, so the caller can hold it.
    std::shared_ptr<const FigureMeasure> load() const;

private:
    mutable std::mutex                   mutex_;
    std::shared_ptr<const FigureMeasure> measure_;
    unsigned long long                   generation_ = 0;
    int                                  w_ = 0, h_ = 0;
    bool                                 refit_ = false;
};

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
