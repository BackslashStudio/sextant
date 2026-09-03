#pragma once
#include "export.h"
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
#include <optional>

namespace sextant {

struct SEXTANT_API Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    static Color from_hex(uint32_t hex);
    static Color from_name(std::string_view name);

    static const Color Blue;
    static const Color Red;
    static const Color Green;
    static const Color Orange;
    static const Color Purple;
    static const Color Cyan;
    static const Color Black;
    static const Color White;
    static const Color Gray;
};

// Honored in every output (window, PNG, SVG) for data lines, grid lines and
// legend swatches. `None` means no stroke at all, and a data series set to it
// also drops out of the legend.
enum class LineStyle   { Solid, Dashed, Dotted, DashDot, None };
enum class MarkerStyle { None, Circle, Square, Triangle, Cross, Plus, Diamond };

enum class Colormap    { Viridis };

// Error bars, carried by the options struct of the series they annotate.
// Two shapes drawn together, either omittable:
//
//   - a capped whisker from `ymin[i]` to `ymax[i]`. These are **absolute data
//     coordinates**, not magnitudes off the point, so a range need not be
//     centred on the plotted value. An empty end reads as the point's own
//     coordinate, giving a one-sided whisker; both empty draws no whisker.
//   - a box spanning `y[i] +/- yvar[i]`, drawn exactly as given (not
//     square-rooted), `boxwidth` pixels across.
//
// Direction support is not uniform, and is enforced: line() and bar() take
// the y fields only and **throw** if an x field is set; scatter() and
// scatter_z() take both and draw a real 2D box (`xvar` wide, `yvar` tall);
// hist() supports none and silently drops whatever is set.
//
// A non-empty vector must hold exactly one entry per point, or the ingesting
// method throws. The six vectors are moved out of this struct at ingest, so a
// stored plot object's copy of them is always empty; the style fields below
// stay and are what the renderers read.
struct ErrorBarOptions {
    std::vector<double> ymin, ymax;   // whisker span, absolute data coords
    std::vector<double> yvar;         // box half-height, data units

    // x direction: scatter() and scatter_z() only — line() and bar() throw.
    std::vector<double> xmin, xmax;
    std::vector<double> xvar;

    // Unset = the series' own drawing color: `color` for a line or scatter,
    // BarOptions::edgecolor for a bar (a bar's own fill color would be
    // invisible against the bar).
    std::optional<Color> color;

    float linewidth = 1.0f;

    // Total cap length crossing each whisker end, in pixels (so capsize/2
    // either side). 0 draws no caps.
    float capsize = 6.0f;

    // Total box width across the whisker, in pixels. Used for whichever
    // direction has no `*var` of its own: always for a line, and for a
    // scatter only when `xvar` is unset.
    float boxwidth = 10.0f;

    // Fill opacity of the box, as a fraction of `color`'s own alpha. The box
    // is always outlined at `linewidth`; 0 leaves it unfilled.
    float box_alpha = 0.25f;
};

// No marker/markersize: use Axes::scatter() on the same data for a marked
// series.
struct LineOptions {
    Color       color      = Color::Blue;
    float       linewidth  = 1.5f;
    LineStyle   linestyle  = LineStyle::Solid;
    std::string label;
    float       alpha      = 1.0f;

    // See ErrorBarOptions. y direction only; an x field throws.
    ErrorBarOptions errorbar;

    // Optional hover text per point, index-aligned with x/y, appended below
    // the default "x=.., y=.." line. Empty or out of range = no custom line.
    std::vector<std::string> hint_labels;
};

struct ScatterOptions {
    Color       color  = Color::Blue;
    float       size   = 20.0f;
    MarkerStyle marker = MarkerStyle::Circle;
    std::string label;
    float       alpha  = 0.8f;

    // See ErrorBarOptions. Both directions; the box is a real 2D rectangle.
    ErrorBarOptions errorbar;

    // See LineOptions::hint_labels.
    std::vector<std::string> hint_labels;
};

// Continuous-color scatter (Axes::scatter_z) — each point's color comes from
// mapping its own z-value through cmap/vmin/vmax, not a fixed Color. Unlike
// ScatterOptions, no `label`: a colorbar (not a legend swatch) conveys the
// color mapping — see `colorbar` below.
struct ScatterZOptions {
    Colormap    cmap     = Colormap::Viridis;
    float       size     = 20.0f;
    MarkerStyle marker   = MarkerStyle::Circle;
    float       alpha    = 0.8f;
    float       vmin     = 0.0f;
    float       vmax     = 1.0f;
    bool        colorbar = false;

    // See ErrorBarOptions. Both directions, as with ScatterOptions.
    ErrorBarOptions errorbar;

    // See LineOptions::hint_labels. Default hover text is "x=.., y=.., z=..".
    std::vector<std::string> hint_labels;
};

struct BarOptions {
    Color       color     = Color::Blue;
    float       width     = 0.8f;
    float       alpha     = 1.0f;
    std::string label;
    Color       edgecolor = Color::Black;
    float       linewidth = 0.5f;

    // See ErrorBarOptions. y direction only, index-aligned with
    // centers/heights, and anchored on the bar's tip rather than the
    // baseline. hist() ignores this field entirely.
    ErrorBarOptions errorbar;

    // See LineOptions::hint_labels (index-aligned with centers/heights).
    std::vector<std::string> hint_labels;
};

// A histogram is a bar plot, so hist() takes both structs: how the bars are
// drawn is BarOptions, what binning means is here. See Axes::hist() for the
// `width` default-argument wart.
struct HistOptions {
    bool density    = false;
    bool cumulative = false;
};

struct HeatmapOptions {
    Colormap    cmap     = Colormap::Viridis;
    float       vmin     = 0.0f;
    float       vmax     = 1.0f;
    bool        colorbar = false;
    std::string origin   = "lower";

    // Contour overlay. Levels are z values in the data's own units (not the
    // 0..1 scale vmin/vmax map onto); empty draws nothing and computes
    // nothing. Sorted and de-duplicated at ingest; a non-finite level throws.
    std::vector<double> contours;
    Color contour_color     = Color::Black;
    float contour_linewidth = 1.0f;

    // Label each line with its own level, rotated to follow it and breaking
    // the line to make room. A line too short to break keeps the line and
    // drops the label.
    bool  contour_labels    = false;
    float contour_fontsize  = 10.0f;   // pixels as drawn; not AxesStyle's

    // See LineOptions::hint_labels. Row-major, size rows*cols (same layout
    // as the heatmap data itself); index = row*cols + col.
    std::vector<std::string> hint_labels;
};

struct GridOptions {
    Color     color     = {0.8f, 0.8f, 0.8f, 1.0f};

    // `None` draws no grid lines at all, as it does for a data line.
    LineStyle linestyle = LineStyle::Solid;
    float     linewidth = 0.5f;
};

// Axes-frame cosmetics: spine (border), tick marks, per-tick labels and the
// title / x-title / y-title text. "Title" names an axis or the whole axes;
// "label" is the text under an individual tick. Every font size is in pixels
// as drawn.
struct AxesStyle {
    Color spine_color      = {0.3f, 0.3f, 0.3f, 1.0f};
    float spine_linewidth  = 1.0f;
    Color tick_color       = {0.3f, 0.3f, 0.3f, 1.0f};
    float tick_length      = 5.0f;
    float tick_linewidth   = 1.0f;
    Color label_color      = {0.2f, 0.2f, 0.2f, 1.0f};
    float label_fontsize   = 11.0f;
    Color title_color      = {0.15f, 0.15f, 0.15f, 1.0f};
    float title_fontsize   = 18.0f;
    Color xtitle_color     = {0.15f, 0.15f, 0.15f, 1.0f};
    float xtitle_fontsize  = 16.5f;
    Color ytitle_color     = {0.15f, 0.15f, 0.15f, 1.0f};
    float ytitle_fontsize  = 16.5f;

    // Empty = use the renderer's default font. Otherwise an absolute path to
    // a .ttf/.ttc/.otf file (see discover_system_fonts()) applied to the
    // title / x-title / y-title and per-tick label text.
    std::string font_path;
};

struct LegendOptions {
    // The legend renders outside the plot frame (like a colorbar), not
    // overlaid on top of the data. Default position (0,0) puts the legend
    // box's top-left corner exactly on the plot frame's top-right corner;
    // offset_x/offset_y (pixels) shift it from there — e.g. offset_x=10 adds
    // a gap between the plot and the legend.
    float offset_x = 10.0f;
    float offset_y = 0.0f;
    float fontsize = 10.0f;
    bool  frameon  = true;

    Color text_color       = {0.15f, 0.15f, 0.15f, 1.0f};
    Color frame_color      = {1.0f, 1.0f, 1.0f, 0.85f};   // frameon fill
    Color border_color     = {0.5f, 0.5f, 0.5f, 1.0f};
    float border_linewidth = 1.0f;

    // "" = renderer default. NOT AxesStyle::font_path -- the legend does not
    // follow that one.
    std::string font_path;
};

// Cosmetics for the colorbar a HeatmapOptions/ScatterZOptions opts into via
// its `colorbar` flag; the bar.s data (cmap/vmin/vmax) stays on the plot
// object that asked for it. Only one colorbar per axes is drawn.
struct ColorbarOptions {
    float fontsize         = 10.0f;
    Color text_color       = {0.2f, 0.2f, 0.2f, 1.0f};
    Color border_color     = {0.3f, 0.3f, 0.3f, 1.0f};
    float border_linewidth = 1.0f;
    std::string font_path;   // "" = renderer default; see LegendOptions
};

// Horizontal placement of a piece of figure-level text.
enum class HAlign { Left, Center, Right };

// Cosmetics for Figure::suptitle(). Figure-level, unlike everything above:
// one suptitle spans the whole subplot grid.
struct SuptitleOptions {
    float fontsize = 21.0f;
    Color color    = {0.1f, 0.1f, 0.1f, 1.0f};
    std::string font_path;   // "" = renderer default

    // Left/Right anchor to the figure's own edge (inset by a fixed margin),
    // not to the plot area — the plot area's left edge moves with the y-tick
    // label widths, which would make the suptitle drift as the data changes.
    HAlign align = HAlign::Center;

    // Pixel nudge from that anchor. offset_y shifts within the reserved band
    // and does NOT enlarge it, so a large value will push the text out of the
    // band and over the first subplot row; the band itself is sized from
    // `fontsize` alone.
    float offset_x = 0.0f;
    float offset_y = 0.0f;
};

// Whitespace between the figure's edge and the subplot grid, in pixels. This
// is the border around the whole grid, not per-subplot padding: the space a
// decoration needs is measured from its own text and is not a setting. Gaps
// *between* subplots are FigureOptions::subplot_col_gap / subplot_row_gap.
struct FigureMargins {
    float left   = 10.0f;
    float right  = 10.0f;
    float top    = 10.0f;
    float bottom = 10.0f;
};

} // namespace sextant
