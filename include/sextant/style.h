#pragma once
#include "export.h"
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
#include <optional>
#include <span>

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

// How an error bar's whisker ends. `Flat` is the crossbar every error bar in
// this library has drawn so far; `Arrow` is a head pointing away from the
// point, which reads as "at least this far" where a flat cap reads as "to
// here". Defined here rather than beside either error-bar struct because 2D
// and 3D must mean the same thing by it -- see v1.0 steps 13 and 16.
enum class CapStyle    { Flat, Arrow };

// Error-bar **data** for the 2D kinds (v1.0 step 16): eight per-point spans,
// four per direction, passed to line()/scatter()/scatter_z()/bar() as a
// parameter of their own. Not a field of the options struct, for the reason
// ErrorBar3D records: `opts` is deep-copied into every snapshot, so per-point
// vectors there would put a memcpy of them on every frame of a pan. `opts`
// holds what a panel can edit, parameters hold what the caller measured.
// Always written with designated initializers:
//
//     ax.scatter(x, y, {.y_cap_lo = err}, opts);
//
// A bare `{}` or an undesignated `{err}` is ambiguous against the overload
// without error bars, and that is the price of the overload.
//
//   - **Offsets from the point, not absolute coordinates**, and
//     magnitude-valued: a negative is a spread, not an inverted bar.
//   - **One end given means symmetric**: `y_cap_lo` alone puts the same
//     distance either side, and so does `y_cap_hi` alone.
//   - **Cap data draws the capped whisker, box data draws the box**, and each
//     is omittable. A direction with box data spans `[p - box_lo, p + box_hi]`;
//     a direction without falls back to ErrorBarOptions::boxwidth pixels.
//   - **A zero offset draws nothing on that side** -- no stem and no cap --
//     which is how a one-sided bar is written. A non-finite entry is read as
//     zero, so a single point's bar can be masked out without a throw.
//
// Both directions on all four kinds. A non-empty span must hold exactly one
// entry per point, or the method throws. hist() has no such overload: a bin's
// height is a count it derived, not a measurement with an uncertainty.
// The spans are read during the call and copied; nothing refers to them after.
struct ErrorBar {
    std::span<const double> x_cap_lo, x_cap_hi, x_box_lo, x_box_hi;
    std::span<const double> y_cap_lo, y_cap_hi, y_box_lo, y_box_hi;

    bool any() const {
        for (std::span<const double> s : { x_cap_lo, x_cap_hi, x_box_lo, x_box_hi,
                                           y_cap_lo, y_cap_hi, y_box_lo, y_box_hi })
            if (!s.empty()) return true;
        return false;
    }
};

// Error-bar **style**, a field of the series' own options struct -- what a
// panel can edit, as against the measured spans in ErrorBar. 3D has its own,
// ErrorBar3DOptions (an alias of this one in step 16, separate since step 17,
// when the 3D box became a block with edges of their own opacity).
struct ErrorBarOptions {
    // Unset = the series' own drawing color: `color` for a line or scatter,
    // BarOptions::edgecolor for a bar (a bar's own fill color would be
    // invisible against the bar).
    std::optional<Color> color;

    float linewidth = 1.0f;

    // Total cap length crossing each whisker end, in pixels (so capsize/2
    // either side). 0 draws no caps.
    float capsize = 6.0f;

    // Flat crossbar, or an open chevron whose tip is the whisker's end:
    // `capsize` wide, 0.87 x `capsize` long, scaled down whole when the
    // whisker is shorter than that so it never reaches back past the point.
    CapStyle capstyle = CapStyle::Flat;

    // Total box width across the whisker, in pixels, for whichever direction
    // has no box data of its own.
    float boxwidth = 10.0f;

    // Fill opacity of the box, as a fraction of `color`'s own alpha. The box
    // is always outlined at `linewidth`; 0 leaves it unfilled.
    float box_alpha = 0.25f;
};

// No marker/markersize: use Axes::scatter() on the same data for a marked
// series.
// `show_legend` (v1.0 step 11.5) is the second gate on a key, and every kind
// that can be keyed has one. An empty `name` already meant no key -- this
// exists so a control can switch a key off without destroying the text the
// caller wrote, which is the only thing a checkbox bound to `name` could do.
// A key is drawn iff `show_legend && !name.empty()`, so the default `true`
// leaves every existing figure exactly as it was.
struct LineOptions {
    Color       color      = Color::Blue;
    float       linewidth  = 1.5f;
    LineStyle   linestyle  = LineStyle::Solid;
    std::string name;
    bool        show_legend = true;
    float       alpha      = 1.0f;

    // Close the path with one more segment from the last point back to the
    // first -- Line3DOptions::loop for a 2D line, with the same meaning in
    // both (v1.0 step 18). It draws a segment and nothing else: no point is
    // added, so the limits an auto-scaled axes resolves to are exactly what
    // they were, the hover still answers at the points the caller gave, and
    // the legend key is unchanged. The seam is joined like any other interior
    // point rather than butt-capped, and a dashed line's pattern carries on
    // across it from the arc length it had reached.
    //
    // Honoured by Plane2D::line() too, on the same terms as every other 2D
    // option a plane takes -- except that a plane strokes solid, so what the
    // seam inherits there is the width and nothing else.
    bool        loop       = false;

    // Style of the error bars passed as an ErrorBar. See ErrorBarOptions.
    ErrorBarOptions errorbar;

    // Optional hover text per point, index-aligned with x/y, appended below
    // the default "x=.., y=.." line. Empty or out of range = no custom line.
    std::vector<std::string> hint_labels;
};

struct ScatterOptions {
    Color       color  = Color::Blue;
    float       size   = 20.0f;
    MarkerStyle marker = MarkerStyle::Circle;
    std::string name;
    bool        show_legend = true;   // see LineOptions::show_legend
    float       alpha  = 0.8f;

    // Style of the error bars passed as an ErrorBar. See ErrorBarOptions.
    ErrorBarOptions errorbar;

    // See LineOptions::hint_labels.
    std::vector<std::string> hint_labels;
};

// Continuous-color scatter (Axes::scatter_z) — each point's color comes from
// mapping its own z-value through cmap/vmin/vmax, not a fixed Color. Its
// `name` names that scale on the colorbar rather than keying one swatch,
// since a swatch could only ever show one of the series' colours -- which is
// why the field was absent entirely until v1.0 step 11.4.
struct ScatterZOptions {
    Colormap    cmap     = Colormap::Viridis;
    float       size     = 20.0f;
    MarkerStyle marker   = MarkerStyle::Circle;
    float       alpha    = 0.8f;
    float       vmin     = 0.0f;
    float       vmax     = 1.0f;
    bool        colorbar = false;

    // Names the colour scale, drawn rotated along the outer side of the bar
    // this series asks for (v1.0 step 11.4). Empty draws nothing and reserves
    // nothing. Since step 11.1 an axes can carry several bars, and two
    // unnamed ones side by side are ambiguous in a way one never was.
    //
    // It also keys the legend (step 11.5) -- the same string for the same
    // series. The key is the marker shape **filled white with a black edge**,
    // not the series' colour, because a continuous-colour series has no one
    // colour a swatch could honestly show; the bar is what carries those, and
    // the key is there to say which shape belongs to which name.
    std::string name;
    bool        show_legend = true;   // see LineOptions::show_legend

    // Style of the error bars passed as an ErrorBar. An unset color falls
    // back to black, since the series has no one colour of its own.
    ErrorBarOptions errorbar;

    // See LineOptions::hint_labels. Default hover text is "x=.., y=.., z=..".
    std::vector<std::string> hint_labels;
};

struct BarOptions {
    Color       color     = Color::Blue;
    float       width     = 0.8f;
    float       alpha     = 1.0f;
    std::string name;
    bool        show_legend = true;   // see LineOptions::show_legend
    Color       edgecolor = Color::Black;
    float       linewidth = 0.5f;

    // Style of the error bars passed as an ErrorBar, which hang off the bar's
    // tip rather than its baseline. hist() takes no ErrorBar, so it never
    // draws what this styles.
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

// An inclusive span of one axis in data coordinates. Used for a heatmap's
// extent, where `lo` and `hi` are the outer *edges* of the first and last
// cell -- not the cell centres -- so an N-cell axis has a cell size of
// (hi - lo) / N. A reversed span (lo > hi) is legal and mirrors the image on
// that axis; a degenerate one (lo == hi) is not.
struct Range {
    double lo = 0.0, hi = 1.0;
};

struct HeatmapOptions {
    Colormap    cmap     = Colormap::Viridis;
    float       vmin     = 0.0f;
    float       vmax     = 1.0f;
    bool        colorbar = false;

    // See ScatterZOptions::name -- names the colour scale on the bar this
    // heatmap asks for, and is read for nothing else.
    std::string name;

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
// Where an axis line sits along a coordinate that is not its own (v1.0 steps
// 19 and 20). An x axis needs a y; in 3D it needs a y *and* a z, which is why
// the fields below name both coordinates rather than saying "position".
//
//   Auto  the library places it: bottom/left in 2D, and in 3D the silhouette
//         edge the camera picks, so labels migrate around the box as it turns
//         and never land inside the solid. Auto and Low are the same thing in
//         2D and genuinely different in 3D.
//   Low   that coordinate's minimum -- the frame's bottom/left edge in 2D, a
//         face of the box in 3D, fixed as you orbit.
//   Mid   the midpoint of that coordinate's range, so the axis crosses the
//         middle of the data without the caller computing where that is.
//   High  that coordinate's maximum.
//
// An AxesStyle::origin_* component supersedes the matching enum.
enum class AxisPosition { Auto, Low, Mid, High };

struct AxesStyle {
    Color spine_color      = {0.3f, 0.3f, 0.3f, 1.0f};
    float spine_linewidth  = 1.0f;

    // The four edges of the 2D plot frame, each drawn or not on its own. Tick
    // marks and tick labels are *not* tied to these: hiding the bottom spine
    // while the x axis is at Low leaves the ticks and their numbers with no
    // line, which is matplotlib's behaviour and occasionally what is wanted.
    // 3D ignores them -- a box has twelve edges and no "top spine"; there the
    // box outline is Box3DStyle's pane edges.
    bool  spine_bottom = true, spine_left = true, spine_top = true, spine_right = true;

    // Where each axis sits along each coordinate that is not its own. A 2D
    // Axes reads xaxis_y and yaxis_x and ignores the rest, exactly as it
    // ignores ztitle_*; an Axes3D reads all six.
    AxisPosition xaxis_y = AxisPosition::Auto, xaxis_z = AxisPosition::Auto;
    AxisPosition yaxis_x = AxisPosition::Auto, yaxis_z = AxisPosition::Auto;
    AxisPosition zaxis_x = AxisPosition::Auto, zaxis_y = AxisPosition::Auto;

    // The crossing point, one component at a time. Each axis uses the
    // components that are not its own -- so origin_y places the x axis (and,
    // in 3D, the z axis) -- and a set component supersedes the matching enum
    // above. Setting one and not the others is meaningful: origin_x alone
    // moves the y axis and leaves the x axis wherever its enum put it.
    //
    // A component outside the visible range clamps: explicit limits and the
    // user's zoom both win, and the axis slides to the frame edge it went off
    // rather than vanishing. Nothing is misrepresented by that -- the tick
    // labels still carry their true values. When the axis it is measured
    // along is on automatic limits, the component is folded into the data
    // bounds first, so pinning to a value outside the data widens the view to
    // show it (see resolve_limits()).
    std::optional<double> origin_x, origin_y, origin_z;

    // Pixels of clear space around the *extended frame* -- the plot frame plus
    // its axis furniture (tick marks, tick labels, x/y titles) -- before any
    // outside legend or colorbar starts (v1.0 step 15.1). A 3D cell keeps its
    // axis furniture inside the frame, so there it is space around the frame
    // itself. Not Box3DStyle::margin, which is a fraction of the frame the box
    // holds back for its own labels.
    float frame_margin     = 0.0f;

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

    // The third axis title, and 3D-only -- an Axes has no z axis and never
    // reads these. Here rather than in Box3DStyle so that all axis-title
    // styling stays in one struct: splitting x/y from z would mean a caller
    // restyling "the axis titles" had to touch two places and could get half
    // of it. Defaults match ytitle's, which is what the two shared before
    // they were separated.
    Color ztitle_color     = {0.15f, 0.15f, 0.15f, 1.0f};
    float ztitle_fontsize  = 16.5f;

    // Empty = use the renderer's default font. Otherwise an absolute path to
    // a .ttf/.ttc/.otf file (see discover_system_fonts()) applied to the
    // title / x-title / y-title and per-tick label text.
    std::string font_path;
};

// Where a legend sits (v1.0 step 15.1). Every anchor is a corner of one of two
// rectangles: the plot frame for Inside, the *extended frame* (see
// AxesStyle::frame_margin) for Outside.
//
//   InsideXY    over the data, in that corner of the frame. Reserves nothing.
//   OutsideTL/TR/BL/BR
//               above (T) or below (B) the extended frame, flush with its left
//               (L) or right (R) edge. Entries run in a row, wrapping onto a
//               new row past the extended frame's width.
//   OutsideLT/LB/RT/RB
//               left (L) or right (R) of the extended frame, flush with its top
//               (T) or bottom (B) edge. Entries run in a column.
enum class LegendAnchor {
    InsideTL, InsideTR, InsideBL, InsideBR,
    OutsideTL, OutsideTR, OutsideBL, OutsideBR,
    OutsideLT, OutsideLB, OutsideRT, OutsideRB,
};

struct LegendOptions {
    LegendAnchor anchor = LegendAnchor::OutsideRT;

    // The box's distance from the anchor corner, the same on both axes. An
    // outside legend's margin is part of the space the layout reserves for it.
    float margin   = 10.0f;

    // Moves the drawn box only: the layout reserves exactly what it would at
    // (0,0), so an offset can push the legend over the data or out of the
    // figure, and it is drawn there.
    float offset_x = 0.0f;
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
// object that asked for it, along with its `name`, which names the scale on
// the bar. Every object that asks gets its own bar, placed outward from the
// frame in plot order -- these cosmetics style all of them, which is why they
// belong to the axes while the name belongs to the series.
//
// Which side of the extended frame the bars go on (v1.0 step 15.1). Left and
// Right bars run the frame's height; Top and Bottom bars are horizontal, run
// its width and read vmin on the left. Several bars stack outward, and outboard
// of a legend on the same side.
enum class ColorbarAnchor { Left, Right, Top, Bottom };

struct ColorbarOptions {
    ColorbarAnchor anchor = ColorbarAnchor::Right;

    // The bar's thickness, across its length.
    float width            = 15.0f;

    // Space between a bar's block and whatever it stacks against -- the
    // extended frame, a legend, or the previous bar. Reserved, per bar.
    float margin           = 15.0f;

    // Moves every bar of the axes as drawn; reserves nothing (see
    // LegendOptions::offset_x).
    float offset_x         = 0.0f;
    float offset_y         = 0.0f;

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
