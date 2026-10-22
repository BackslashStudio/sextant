#pragma once
#include "export.h"
#include <compare>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
#include <optional>
#include <span>

namespace sextant {
    struct SEXTANT_API Color {
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

        // 0xRRGGBB, or 0xRRGGBBAA when the value exceeds 0xFFFFFF -- so a
        // 0xRRGGBBAA whose red is 0 reads as RGB. from_name("#...") has no
        // such ambiguity.
        static Color from_hex(uint32_t hex);

        // "#rrggbb", "#rrggbbaa", or one of red, blue, green, orange, purple,
        // cyan, black, white, gray/grey. Anything else throws std::invalid_argument.
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

    // Honored for data lines, grid lines and legend swatches in every output.
    // `None` draws no stroke, and drops a data series from the legend; its error
    // bars, limits and hover stay (to hide a whole series, LineOptions::visible).
    enum class LineStyle { Solid, Dashed, Dotted, DashDot, None };

    // How a 2D line runs between its points (matplotlib's drawstyle): Default
    // draws straight segments; the steps hold each value flat. Between points a
    // and b, StepsPre rises at a's x to b's y, then runs flat to b; StepsPost
    // runs flat at a's y to b's x, then rises; StepsMid runs flat to the
    // midpoint of the two x values (in data units, also on a log axis), rises,
    // then runs flat to b.
    enum class DrawStyle { Default, StepsPre, StepsMid, StepsPost };

    // The direction an object runs: a bar stands (Vertical) or lies along x
    // (Horizontal, matplotlib's barh); a reference line or span runs across the
    // frame along x (Horizontal, matplotlib's axhline/axhspan) or along y
    // (Vertical, axvline/axvspan); a fill runs along x between two curves of y
    // (Horizontal, matplotlib's fill_between) or along y (Vertical,
    // fill_betweenx).
    enum class Orientation { Vertical, Horizontal };

    // `size` is the marker's outer diameter. Triangle points up; Star has five
    // points; Pentagon and Hexagon stand on a corner pointing up. Cross and Plus
    // are filled bars; ThinCross, ThinPlus, VLine and HLine are line markers
    // (matplotlib's "x", "+", "|", "_"): strokes `edge_linewidth` thick (0 =
    // 1.5 px) in the fill's color and alpha, with no outline of their own.
    enum class MarkerStyle {
        None, Circle, Square, Triangle, Cross, Plus, Diamond,
        TriangleDown, TriangleLeft, TriangleRight, Star, Pentagon, Hexagon,
        ThinCross, ThinPlus, VLine, HLine
    };

    class Colormap;

    // Adds a colormap for the rest of the process: `colors` spread evenly from
    // the low end (vmin) to the high end (vmax), interpolated linearly, alpha
    // included. Channels are clamped to [0, 1]. Any thread; a registered map
    // never changes and is never removed, so the handle stays valid. Throws
    // std::invalid_argument for fewer than two colors, an empty name, a name
    // ending in "_r", or one already taken (built in or registered, any case).
    // `cyclic`: values outside [vmin, vmax] wrap instead of clamping (see
    // Colormap::is_cyclic()); give its first and last colors the same value.
    SEXTANT_API Colormap register_colormap(std::string_view name, const std::vector<Color>& colors,
                                           bool cyclic = false);

    // A colormap: one of the built-ins below, or a registered one, run either
    // way. Copy it freely; it is a small value.
    //
    // The built-ins, by kind (data from matplotlib and cmocean; names as theirs):
    //   Sequential, perceptually uniform: Viridis (default), Plasma, Inferno, Magma,
    //     Cividis (colorblind-safe).
    //   Sequential, one hue, light to dark (ColorBrewer): Blues, Greens, Reds,
    //     Oranges, Purples, Greys.
    //   Sequential, several hues: YlOrRd, YlGnBu, Hot, Gray (black to white), and
    //     the rainbows Turbo and Jet (not perceptually uniform; Turbo is the better).
    //   Diverging, around a centre: Coolwarm, RdBu, Spectral, BrBG, PiYG.
    //   Cyclic, for angles and phases (see is_cyclic()): Twilight, TwilightShifted,
    //     Hsv, Phase.
    class Colormap {
    public:
        static const Colormap Viridis, Plasma, Inferno, Magma, Cividis, Turbo, Coolwarm, Gray;
        static const Colormap Blues, Greens, Reds, Oranges, Purples, Greys, YlOrRd, YlGnBu, Hot, Jet;
        static const Colormap RdBu, Spectral, BrBG, PiYG;
        static const Colormap Twilight, TwilightShifted, Hsv, Phase;

        constexpr Colormap() = default; // Viridis

        // The same map from its high end to its low: matplotlib's
        // cmap.reversed(), its "_r" maps. Reversing twice gives the map back.
        constexpr Colormap reversed() const { return Colormap(id_, !reversed_, cyclic_); }
        constexpr bool is_reversed() const { return reversed_; }

        // The canonical name ("viridis", or as registered), with "_r" when reversed.
        SEXTANT_API std::string name() const;

        // A built-in or registered name, any case, optionally ending in "_r"
        // (reversed). Anything else throws std::invalid_argument.
        SEXTANT_API static Colormap from_name(std::string_view name);

        // Every map's canonical name, without "_r": the built-ins in declaration
        // order, then the registered ones in registration order.
        SEXTANT_API static std::vector<std::string> names();

        // A cyclic map's two ends meet, so a value outside [vmin, vmax] wraps
        // round (vmax + d draws as vmin + d) rather than clamping to the end
        // color: phase data runs on past +-pi and still reads right.
        constexpr bool is_cyclic() const { return cyclic_; }

        // Built-ins in declaration order, then registered maps in order.
        constexpr std::uint16_t id() const { return id_; }

        friend constexpr auto operator<=>(const Colormap&, const Colormap&) = default;

    private:
        constexpr Colormap(std::uint16_t id, bool reversed, bool cyclic = false)
            : id_(id), reversed_(reversed), cyclic_(cyclic) {}

        friend SEXTANT_API Colormap register_colormap(std::string_view, const std::vector<Color>&, bool);

        std::uint16_t id_ = 0;
        bool reversed_ = false;
        bool cyclic_ = false; // fixed by the id; kept here so drawing reads it for free
    };

    inline constexpr Colormap Colormap::Viridis{0, false};
    inline constexpr Colormap Colormap::Plasma{1, false};
    inline constexpr Colormap Colormap::Inferno{2, false};
    inline constexpr Colormap Colormap::Magma{3, false};
    inline constexpr Colormap Colormap::Cividis{4, false};
    inline constexpr Colormap Colormap::Turbo{5, false};
    inline constexpr Colormap Colormap::Coolwarm{6, false};
    inline constexpr Colormap Colormap::Gray{7, false};
    inline constexpr Colormap Colormap::Blues{8, false};
    inline constexpr Colormap Colormap::Greens{9, false};
    inline constexpr Colormap Colormap::Reds{10, false};
    inline constexpr Colormap Colormap::Oranges{11, false};
    inline constexpr Colormap Colormap::Purples{12, false};
    inline constexpr Colormap Colormap::Greys{13, false};
    inline constexpr Colormap Colormap::YlOrRd{14, false};
    inline constexpr Colormap Colormap::YlGnBu{15, false};
    inline constexpr Colormap Colormap::Hot{16, false};
    inline constexpr Colormap Colormap::Jet{17, false};
    inline constexpr Colormap Colormap::RdBu{18, false};
    inline constexpr Colormap Colormap::Spectral{19, false};
    inline constexpr Colormap Colormap::BrBG{20, false};
    inline constexpr Colormap Colormap::PiYG{21, false};
    inline constexpr Colormap Colormap::Twilight{22, false, true};
    inline constexpr Colormap Colormap::TwilightShifted{23, false, true};
    inline constexpr Colormap Colormap::Hsv{24, false, true};
    inline constexpr Colormap Colormap::Phase{25, false, true};

    // Error-bar whisker end: `Flat` crossbar ("to here") or `Arrow` head pointing
    // away from the point ("at least this far"). Shared by 2D and 3D.
    enum class CapStyle { Flat, Arrow };

    // Error-bar data for the 2D kinds: per-point spans passed to
    // line()/scatter()/scatter_z()/bar(). Kept out of the options struct because
    // options are copied into every snapshot. Write with designated initializers,
    // since a bare `{}` is ambiguous with the overload without error bars:
    //
    //     ax.scatter(x, y, {.y_cap_lo = err}, opts);
    //
    //   - Offsets from the point, magnitude-valued (a negative is a spread).
    //   - One end given means symmetric.
    //   - Cap data draws the capped whisker, box data the box
    //     ([p - box_lo, p + box_hi]); without box data a direction uses
    //     ErrorBarOptions::boxwidth pixels.
    //   - Zero draws nothing on that side (one-sided bars); non-finite reads as zero.
    //
    // A non-empty span must hold one entry per point, or the method throws. The
    // spans are copied during the call.
    struct ErrorBar {
        std::span<const double> x_cap_lo, x_cap_hi, x_box_lo, x_box_hi;
        std::span<const double> y_cap_lo, y_cap_hi, y_box_lo, y_box_hi;

        bool any() const {
            for (std::span<const double> s: {
                     x_cap_lo, x_cap_hi, x_box_lo, x_box_hi,
                     y_cap_lo, y_cap_hi, y_box_lo, y_box_hi
                 })
                if (!s.empty()) return true;
            return false;
        }
    };

    // Error-bar style (a field of the series' options). 3D uses ErrorBar3DOptions.
    struct ErrorBarOptions {
        // Unset = the series' color: `color` for line/scatter, `edgecolor` for bar.
        std::optional<Color> color;

        float linewidth = 1.0f;

        // Total cap length across each whisker end, in pixels. 0 = no caps.
        float capsize = 6.0f;

        // Flat crossbar, or an open chevron `capsize` wide and 0.87 x `capsize`
        // long, shrunk so it never reaches back past the point.
        CapStyle capstyle = CapStyle::Flat;

        // Box width across the whisker, in pixels, for a direction without box data.
        float boxwidth = 10.0f;

        // Box fill opacity as a fraction of `color`'s alpha; 0 = outline only.
        // The whole error bar also takes the series' `alpha`.
        float box_alpha = 0.25f;
    };

    // No markers: add Axes::scatter() on the same data for a marked series.
    // A legend key is drawn iff `show_legend && !name.empty()`.
    struct LineOptions {
        // Unset = the axes' next color-cycle color, taken at the call (see
        // Axes::set_color_cycle()). The Data panel shows the color taken.
        std::optional<Color> color;
        float linewidth = 1.5f;
        LineStyle linestyle = LineStyle::Solid;
        std::string name;
        bool show_legend = true;
        // false hides the object as if it were absent: not drawn or exported,
        // no legend key or colorbar, no hover or pick, and the auto limits leave
        // it out. Its data stays (read back and replaced as usual), and so do its
        // color, its place in the draw order and its index. Axes::set_line_visible()
        // and the like change it after the call; the Data panel has a checkbox.
        bool visible = true;
        // Paint order within the axes: objects draw in ascending zorder, those with an equal
        // zorder in the order of the calls (a later call over an earlier one). Hover follows
        // it; the legend and colorbars stay in call order. Finite. Axes::set_zorder() changes it
        // after the call; the Data panel has a field.
        double zorder = 0.0;
        // Multiplies `color`'s own alpha, and the error bars'.
        float alpha = 1.0f;

        // Add a closing segment from the last point to the first. No point is
        // added (limits, hover and legend unchanged); the seam is joined and a dash
        // pattern continues across it. On a Plane2D only the width carries over,
        // since planes stroke solid.
        bool loop = false;

        // Step drawing (see DrawStyle). Only the stroke changes: limits, hover,
        // error bars and the legend key stay on the points. A segment with a
        // NaN end draws none of its steps; `loop` steps the closing segment too.
        DrawStyle drawstyle = DrawStyle::Default;

        // Style of the error bars passed as an ErrorBar.
        ErrorBarOptions errorbar;

        // Optional hover text per point, index-aligned with x/y, shown below the
        // default "x=.., y=.." line. Empty or out of range = none.
        std::vector<std::string> hint_labels;
    };

    struct ScatterOptions {
        std::optional<Color> color; // see LineOptions::color
        float size = 20.0f;
        // Per-point diameters in pixels (a bubble chart): empty = `size` for every
        // point, else one per point (any other length throws
        // std::invalid_argument). A point whose size is NaN or <= 0 is neither
        // drawn nor hovered. Kept as the object's data: read back and replaced
        // through ScatterData::sizes, not through these options.
        std::vector<double> sizes;
        MarkerStyle marker = MarkerStyle::Circle;
        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder

        // Opacity of the whole marker (fill, outline, error bars), multiplying
        // each color's own alpha.
        float alpha = 0.8f;

        // False draws the outline only (`edge_linewidth` > 0), the fill left out:
        // a hollow marker. A line marker has no fill and ignores it.
        bool filled = true;

        // Marker outline, drawn inside the marker's boundary, so `size` stays the
        // outer diameter. Width in pixels; 0 (the default) draws none. Unset
        // `edgecolor` outlines in the fill's color (each point's own, for
        // scatter_z). Every filled marker (Cross and Plus too) is outlined along
        // its boundary. A line marker (ThinCross, ThinPlus, VLine, HLine) has no
        // outline: `edge_linewidth` is its stroke width instead.
        std::optional<Color> edgecolor;
        float edge_linewidth = 0.0f;

        // Style of the error bars passed as an ErrorBar.
        ErrorBarOptions errorbar;

        // See LineOptions::hint_labels.
        std::vector<std::string> hint_labels;
    };

    // Continuous-color scatter (Axes::scatter_z): each point's color maps its z
    // through cmap/vmin/vmax.
    struct ScatterZOptions {
        Colormap cmap = Colormap::Viridis;
        float size = 20.0f;
        std::vector<double> sizes; // see ScatterOptions::sizes
        MarkerStyle marker = MarkerStyle::Circle;
        float alpha = 0.8f;
        bool filled = true; // see ScatterOptions::filled

        // Marker outline; see ScatterOptions::edgecolor. Unset color = each point's
        // own fill color.
        std::optional<Color> edgecolor;
        float edge_linewidth = 0.0f;

        float vmin = 0.0f;
        float vmax = 1.0f;
        bool colorbar = false;

        // Names the color scale, drawn along the outer side of this series'
        // colorbar; empty draws and reserves nothing. Also the legend key, shown
        // as the marker shape filled white with a black edge.
        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder

        // Style of the error bars passed as an ErrorBar; unset color = black.
        ErrorBarOptions errorbar;

        // See LineOptions::hint_labels. Default hover text is "x=.., y=.., z=..".
        std::vector<std::string> hint_labels;
    };

    struct BarOptions {
        std::optional<Color> color; // see LineOptions::color

        // Horizontal reads bar(x, height) as positions along y and lengths along
        // x (matplotlib's barh(y, width)); `width` is then each bar's thickness
        // along y, and the value errors are the ErrorBar's x_* fields.
        Orientation orientation = Orientation::Vertical;

        // Where each bar starts (its left edge when horizontal): empty = 0, one
        // value = every bar, else one per bar (any other length throws
        // std::invalid_argument). A bar runs from its bottom to bottom + height,
        // which is where error bars hang. Kept as the object's data: read back
        // and replaced through BarData::bottom, not through these options.
        std::vector<double> bottom;

        float width = 0.8f;
        // Multiplies the own alpha of `color`, `edgecolor` and the error bars.
        float alpha = 1.0f;
        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder
        Color edgecolor = Color::Black;
        float linewidth = 0.5f;

        // Style of the error bars passed as an ErrorBar, hung off the bar's tip.
        ErrorBarOptions errorbar;

        // See LineOptions::hint_labels (index-aligned with centers/heights).
        std::vector<std::string> hint_labels;
    };

    // Binning options for hist(); bar drawing is BarOptions.
    struct HistOptions {
        bool density = false;
        bool cumulative = false;
    };

    // A reference line across the frame (Axes::axline()).
    struct AxLineOptions {
        // Horizontal: a line at y = value, running along x (matplotlib's
        // axhline); Vertical: at x = value (axvline).
        Orientation orientation = Orientation::Horizontal;
        std::optional<Color> color; // see LineOptions::color
        float linewidth = 1.5f;
        LineStyle linestyle = LineStyle::Solid; // None draws nothing and keys nothing
        float alpha = 1.0f;

        // Where the line starts and ends along the frame, as fractions of it
        // (0 = left or bottom edge, 1 = right or top edge), whatever the limits:
        // matplotlib's xmin/xmax.
        double from = 0.0, to = 1.0;

        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder
    };

    // A band across the frame between two values (Axes::axspan()).
    struct AxSpanOptions {
        // Horizontal: a band over y in [lo, hi], running along x (matplotlib's
        // axhspan); Vertical: over x (axvspan).
        Orientation orientation = Orientation::Horizontal;
        // Unset = Color::Blue; a span takes no color from the color cycle.
        std::optional<Color> color;
        float alpha = 0.25f;

        // The band's outline, `linewidth` pixels wide (0, the default, draws
        // none) in `edgecolor` (unset = `color`); `alpha` fades both.
        std::optional<Color> edgecolor;
        float linewidth = 0.0f;

        // See AxLineOptions::from.
        double from = 0.0, to = 1.0;

        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder
    };

    // The area between two curves (Axes::fill()).
    struct FillOptions {
        // Horizontal: the band runs along x, between y1(x) and y2(x)
        // (matplotlib's fill_between); Vertical: along y, between x1(y) and
        // x2(y) (fill_betweenx).
        Orientation orientation = Orientation::Horizontal;
        std::optional<Color> color; // see LineOptions::color
        float alpha = 1.0f;

        // The outline of each filled region, `linewidth` pixels wide (0, the
        // default, draws none) in `edgecolor` (unset = `color`), dashed by
        // `linestyle`; `alpha` fades both.
        std::optional<Color> edgecolor;
        float linewidth = 0.0f;
        LineStyle linestyle = LineStyle::Solid;

        // Steps both curves, as a line's drawstyle steps its points.
        DrawStyle drawstyle = DrawStyle::Default;

        // Points left out of the fill (the inverse of matplotlib's `where`):
        // empty = none, else one per point (any other length throws
        // std::invalid_argument). The filled regions are the runs of points
        // neither excluded nor holding a NaN. Kept as the object's data: read
        // back and replaced through FillData::exclude, not through these options.
        std::vector<bool> exclude;

        // Where a region ends at an excluded point and the curves cross between
        // that point and the region, the region runs on to the crossing.
        bool interpolate = false;

        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder
    };

    // Where the bottom layer of a stack() starts, as matplotlib's stackplot
    // baselines: Zero at 0; Sym centres the stack on 0; Wiggle minimises the
    // sum of the layers' squared slopes; WeightedWiggle weights those slopes
    // by layer size (a streamgraph).
    enum class StackBaseline { Zero, Sym, Wiggle, WeightedWiggle };

    struct StackOptions {
        // Horizontal: the layers stack up along y over x; Vertical: along x
        // over y (the spans then read as y and the layers' widths).
        Orientation orientation = Orientation::Horizontal;
        StackBaseline baseline = StackBaseline::Zero;

        // Empty = one color from the cycle per layer, else one per layer (any
        // other length throws std::invalid_argument).
        std::vector<Color> colors;
        // Empty = none, else one legend name per layer (likewise).
        std::vector<std::string> names;

        // As FillOptions, for every layer.
        float alpha = 1.0f;
        std::optional<Color> edgecolor;
        float linewidth = 0.0f;
        LineStyle linestyle = LineStyle::Solid;
        DrawStyle drawstyle = DrawStyle::Default;
        bool show_legend = true;
        bool visible = true;     // every layer's FillOptions::visible
        double zorder = 0.0;       // every layer's FillOptions::zorder
    };

    // Which part of a quiver arrow sits on its (x, y): the tail, the middle or
    // the tip.
    enum class QuiverPivot { Tail, Middle, Tip };

    // An arrow field (Axes::quiver()).
    struct QuiverOptions {
        // Unset = the next cycle color. With a `c` per arrow (quiver(x, y, u, v,
        // c)) each arrow is its c through cmap/vmin/vmax instead (a cyclic map
        // wraps), `color` is unused, and the cycle is not advanced.
        std::optional<Color> color;
        float alpha = 1.0f; // multiplies either color's own alpha
        Colormap cmap = Colormap::Viridis;
        float vmin = 0.0f;
        float vmax = 1.0f;
        // A colorbar for the mapped colors (only with `c`); `name` labels it.
        bool colorbar = false;

        // Data units of arrow per unit of vector: an arrow runs from (x, y) to
        // (x + amp*u, y + amp*v), so it points along (u, v) only where one
        // unit is as long on both axes (Axes::set_aspect). Unset = chosen at the
        // call from the data, as matplotlib does: the mean arrow becomes the x
        // extent of the points / (1.8 * max(10, sqrt(N))). The chosen value is
        // kept (read back through the panel; set_quiver_data() leaves it), so the
        // frames of an animation stay comparable. matplotlib's `scale` is this
        // one's inverse (named apart from it, so pyplot keeps `scale`).
        std::optional<double> amp;
        QuiverPivot pivot = QuiverPivot::Tail;

        // The arrow's shape in pixels: a shaft `width` wide, then a triangular
        // head `head_length` long and `head_width` across. An arrow shorter than
        // its head is all head, scaled down to the arrow's length.
        float width = 2.0f;
        float head_width = 7.0f;
        float head_length = 8.0f;

        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder

        // See LineOptions::hint_labels. Default hover text is "x=.., y=..,
        // u=.., v=.." (plus ", c=.." with `c`), at the arrow's (x, y).
        std::vector<std::string> hint_labels;
    };

    // An axis span in data coordinates. For a heatmap, `lo`/`hi` are the outer
    // cell edges, so cell size is (hi - lo) / N. lo > hi mirrors the image;
    // lo == hi is invalid.
    struct Range {
        double lo = 0.0, hi = 1.0;
    };

    struct HeatmapOptions {
        Colormap cmap = Colormap::Viridis;
        float vmin = 0.0f;
        float vmax = 1.0f;
        bool colorbar = false;

        // Names the color scale on this heatmap's colorbar (see ScatterZOptions::name).
        std::string name;
        bool visible = true; // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder

        // Where row 0 is drawn: "lower" (bottom, the default) or "upper" (top, as
        // an image). Anything else throws std::invalid_argument.
        std::string origin = "lower";

        // Contour levels in data units (not the vmin/vmax scale); empty = none.
        // Sorted and de-duplicated at ingest; a non-finite level throws.
        std::vector<double> contours;
        Color contour_color = Color::Black;
        float contour_linewidth = 1.0f;

        // Label each contour with its level, breaking the line to fit; a line too
        // short to break keeps no label.
        bool contour_labels = false;
        float contour_fontsize = 10.0f; // pixels as drawn; not AxesStyle's

        // See LineOptions::hint_labels. Row-major, index = row*cols + col.
        std::vector<std::string> hint_labels;
    };

    // An RGB or RGBA image (Axes::imshow_rgb()). It carries its own colours, so
    // it has no colormap, vmin/vmax, colorbar or contours.
    struct ImageOptions {
        // Where the image sits, as heatmap()'s ranges (cell edges; reversed
        // mirrors). Unset = the index extent, x in [0, cols] and y in [0, rows].
        std::optional<Range> xrange, yrange;

        // Where row 0 is drawn: "upper" (top, as a photo; the default) or
        // "lower". Anything else throws std::invalid_argument.
        std::string origin = "upper";

        bool visible = true; // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder

        // See LineOptions::hint_labels. Row-major, index = row*cols + col.
        std::vector<std::string> hint_labels;
    };

    struct GridOptions {
        Color color = {0.8f, 0.8f, 0.8f, 1.0f};

        // `None` draws no grid lines.
        LineStyle linestyle = LineStyle::Solid;
        float linewidth = 0.5f;

        // 2D: also a line at each minor tick (see AxesStyle::minor_xticks), in
        // `minor_color` with the same style and width.
        bool minor = false;
        Color minor_color = {0.9f, 0.9f, 0.9f, 1.0f};
    };

    // Where an axis line sits along each coordinate that is not its own (an x axis
    // needs y, and in 3D also z).
    //
    //   Auto  bottom/left in 2D; in 3D the camera's silhouette edge, so labels
    //         follow the box as it turns. Same as Low in 2D only.
    //   Low   that coordinate's minimum (frame edge in 2D, fixed box face in 3D).
    //   Mid   the midpoint of that coordinate's range.
    //   High  that coordinate's maximum.
    //
    // An AxesStyle::origin_* component supersedes the matching enum.
    enum class AxisPosition { Auto, Low, Mid, High };

    // How a 2D axis maps data to its length (Axes::set_xscale()), as matplotlib's.
    //
    //   Linear  evenly.
    //   Log     log_base(v). Values <= 0 have no place on it: drawn clipped far
    //           below the frame (a line through 0 runs off its edge, a bar
    //           stands on the frame's bottom), ignored by the automatic limits.
    //   Symlog  linear within +-linthresh, logarithmic outside it, so it takes
    //           0 and negative values.
    enum class Scale { Linear, Log, Symlog };

    // What Axes::set_aspect() changes to keep its ratio: the frame (Box, shrunk
    // and centred in the space the grid gives it) or the limits (Datalim, one
    // axis widened about its centre), as matplotlib's `adjustable`.
    enum class Adjustable { Box, Datalim };

    // Read by the scales that use them; each must be finite.
    struct ScaleOptions {
        // Log and Symlog; > 1.
        double base = 10.0;
        // Symlog: the half-width of the linear part around 0; > 0.
        double linthresh = 1.0;
        // Symlog: the linear part's length, in decades; > 0.
        double linscale = 1.0;
    };

    // Axes-frame cosmetics: spine, ticks, tick labels and titles. Font sizes are
    // in pixels as drawn.
    struct AxesStyle {
        // Plot-area fill, under the data. 2D only: a 3D box colors its panes with
        // Box3DStyle::pane_color. Alpha 0 shows the figure background.
        Color background = {1.0f, 1.0f, 1.0f, 1.0f};

        Color spine_color = {0.3f, 0.3f, 0.3f, 1.0f};
        float spine_linewidth = 1.0f;

        // The four 2D frame edges. Ticks and tick labels are independent of these
        // (as in matplotlib). Ignored in 3D; see Box3DStyle.
        bool spine_bottom = true, spine_left = true, spine_top = true, spine_right = true;

        // 2D, a twinned cell only (Axes::twinx()/twiny()): the frame edge this
        // axes' twinned axis lies on, drawn in this color instead of the host's
        // spine_color -- the host's y on the left and the twinx's on the right,
        // the host's x at the bottom and the twiny's on top (so each axis can
        // take its series' color). Unset, or with no twin, spine_color.
        std::optional<Color> xaxis_color, yaxis_color;

        // A 2D Axes reads xaxis_y and yaxis_x only; an Axes3D reads all six.
        AxisPosition xaxis_y = AxisPosition::Auto, xaxis_z = AxisPosition::Auto;
        AxisPosition yaxis_x = AxisPosition::Auto, yaxis_z = AxisPosition::Auto;
        AxisPosition zaxis_x = AxisPosition::Auto, zaxis_y = AxisPosition::Auto;

        // Crossing point, per component; each axis uses the components that are not
        // its own (origin_y places the x axis, and in 3D the z axis). A set component
        // supersedes the enum above. Values outside the visible range clamp to the
        // frame edge; on auto limits the value is folded into the data bounds first.
        std::optional<double> origin_x, origin_y, origin_z;

        // Clear space in pixels around the extended frame (frame + ticks, labels,
        // titles) before an outside legend or colorbar. In 3D, around the frame.
        float frame_margin = 0.0f;

        // Per axis: draw its tick marks and tick labels, or neither. A hidden axis
        // reserves no room for them, and the grid lines stay (see grid()). This is
        // not set_xticks({}), which means "back to automatic ticks". `show_zticks`
        // is read by an Axes3D only.
        bool show_xticks = true, show_yticks = true, show_zticks = true;

        // 2D, per axis: draw its tick labels (the marks follow show_xticks). Unset
        // hides them on an inner axes of a share group (Axes::sharex()): x labels
        // at the bottom when another axes sharing x sits lower in the grid under
        // the same columns (at the top: higher), y labels on the left when one
        // sharing y sits to the left in the same rows (on the right: to the
        // right), as matplotlib's subplots(sharex=True). Hidden labels reserve no
        // room.
        std::optional<bool> show_xtick_labels, show_ytick_labels;

        Color tick_color = {0.3f, 0.3f, 0.3f, 1.0f};
        float tick_length = 5.0f;
        float tick_linewidth = 1.0f;

        // 2D: unlabelled marks between the major ticks, `minor_tick_length` long.
        // Unset = on for a Log or Symlog axis, off for Linear (as matplotlib). A
        // linear axis with set_xticks() positions has none.
        std::optional<bool> minor_xticks, minor_yticks;
        float minor_tick_length = 2.5f;
        Color label_color = {0.2f, 0.2f, 0.2f, 1.0f};
        float label_fontsize = 11.0f;

        // Per axis, over the shared tick and label fields above: unset takes
        // the shared one, so tick_color still styles every axis that has no
        // color of its own. The z ones are read by an Axes3D only, the minor
        // ones in 2D only.
        std::optional<Color> xtick_color, ytick_color, ztick_color;
        std::optional<float> xtick_length, ytick_length, ztick_length;
        std::optional<float> xtick_linewidth, ytick_linewidth, ztick_linewidth;
        std::optional<float> minor_xtick_length, minor_ytick_length;
        std::optional<Color> xlabel_color, ylabel_color, zlabel_color;
        std::optional<float> xlabel_fontsize, ylabel_fontsize, zlabel_fontsize;

        Color title_color = {0.15f, 0.15f, 0.15f, 1.0f};
        float title_fontsize = 18.0f;
        Color xtitle_color = {0.15f, 0.15f, 0.15f, 1.0f};
        float xtitle_fontsize = 16.5f;
        Color ytitle_color = {0.15f, 0.15f, 0.15f, 1.0f};
        float ytitle_fontsize = 16.5f;

        // z-axis title, 3D only.
        Color ztitle_color = {0.15f, 0.15f, 0.15f, 1.0f};
        float ztitle_fontsize = 16.5f;

        // Empty = default font; otherwise an absolute .ttf/.ttc/.otf path, applied
        // to titles and tick labels.
        std::string font_path;
    };

    // Legend placement. Inside* anchors to a corner of the plot frame and reserves
    // nothing. Outside* anchors to the extended frame (see AxesStyle::frame_margin):
    //   OutsideTL/TR/BL/BR  above (T) or below (B), flush left (L) or right (R);
    //                       entries run in wrapping rows.
    //   OutsideLT/LB/RT/RB  left (L) or right (R), flush top (T) or bottom (B);
    //                       entries run in a column.
    enum class LegendAnchor {
        InsideTL, InsideTR, InsideBL, InsideBR,
        OutsideTL, OutsideTR, OutsideBL, OutsideBR,
        OutsideLT, OutsideLB, OutsideRT, OutsideRB,
    };

    struct LegendOptions {
        LegendAnchor anchor = LegendAnchor::OutsideRT;

        // Distance from the anchor corner; reserved space for an outside legend.
        float margin = 10.0f;

        // Moves the drawn box only; the reserved space does not change.
        float offset_x = 0.0f;
        float offset_y = 0.0f;

        float fontsize = 10.0f;
        bool frameon = true;

        Color text_color = {0.15f, 0.15f, 0.15f, 1.0f};
        Color frame_color = {1.0f, 1.0f, 1.0f, 0.85f}; // frameon fill
        Color border_color = {0.5f, 0.5f, 0.5f, 1.0f};
        float border_linewidth = 1.0f;

        // "" = renderer default. Independent of AxesStyle::font_path.
        std::string font_path;
    };

    // Which side of the extended frame colorbars go on. Top/Bottom bars are
    // horizontal with vmin on the left. Several bars stack outward, outside any
    // legend on the same side.
    enum class ColorbarAnchor { Left, Right, Top, Bottom };

    // Cosmetics shared by every colorbar of the axes. The data (cmap/vmin/vmax)
    // and `name` stay on the plot object that requested the bar.
    struct ColorbarOptions {
        ColorbarAnchor anchor = ColorbarAnchor::Right;

        // Thickness across the bar.
        float width = 15.0f;

        // Reserved space between a bar and the frame, legend or previous bar.
        float margin = 15.0f;

        // Moves all bars as drawn; reserves nothing.
        float offset_x = 0.0f;
        float offset_y = 0.0f;

        float fontsize = 10.0f;
        Color text_color = {0.2f, 0.2f, 0.2f, 1.0f};
        Color border_color = {0.3f, 0.3f, 0.3f, 1.0f};
        float border_linewidth = 1.0f;
        std::string font_path; // "" = renderer default; see LegendOptions
    };

    // Horizontal placement of a piece of text: the suptitle, or a text's block
    // against its anchor (TextOptions::ha).
    enum class HAlign { Left, Center, Right };

    // Vertical placement of a text's block against its anchor. Baseline is the
    // last line's baseline.
    enum class VAlign { Top, Center, Baseline, Bottom };

    // What one coordinate of a position (or a length) is measured in: data units,
    // a fraction of the plot frame from its left (x) or bottom (y) edge, or
    // pixels from the same edge (y up). A length (a radius) in pixels is just
    // that many pixels.
    enum class Coords { Data, Fraction, Pixels };

    // One coordinate of a text's position (Axes::text(), Axes::annotate()). A
    // bare number converts to a data value; Pos::fraction() makes a frame
    // fraction. x and y each take their own, so a text can sit at a data x
    // and the top of the frame:
    //
    //     ax.text("threshold", 2.5, Pos::fraction(0.95));
    //
    // Pos::pixels() measures from the frame's left or bottom edge (y up); a fraction of
    // 0 or 1 plus TextOptions::dx/dy puts a text a pixel offset from a corner too.
    struct Pos {
        double v = 0.0;
        Coords space = Coords::Data;

        Pos() = default;

        Pos(double value) : v(value) {}

        static Pos fraction(double f) {
            Pos p(f);
            p.space = Coords::Fraction;
            return p;
        }

        static Pos pixels(double px) {
            Pos p(px);
            p.space = Coords::Pixels;
            return p;
        }
    };

    // An arrow end: none, an open chevron, a filled triangle, or a bar across
    // the shaft.
    enum class ArrowHead { None, Open, Filled, Bar };

    // A filled polygon (Axes::shape_poly()). The outline (`linewidth` > 0, none
    // by default) is `edgecolor` (unset = `color`), dashed by `linestyle`;
    // `alpha` fades both.
    struct PolyOptions {
        std::optional<Color> color; // see LineOptions::color
        float alpha = 1.0f;
        std::optional<Color> edgecolor;
        float linewidth = 0.0f;
        LineStyle linestyle = LineStyle::Solid;

        // What the vertices are measured in: data units, a fraction of the
        // plot frame, or pixels from its bottom left corner (see Pos). One
        // space for all of them.
        Coords space = Coords::Data;

        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder
    };

    // A filled circle (Axes::shape_circle()): the look of a PolyOptions.
    struct CircleOptions {
        std::optional<Color> color; // see LineOptions::color
        float alpha = 1.0f;
        std::optional<Color> edgecolor;
        float linewidth = 0.0f;
        LineStyle linestyle = LineStyle::Solid;

        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder
    };

    // A filled sector or ring segment (Axes::shape_wedge()): the look of a
    // PolyOptions.
    struct WedgeOptions {
        std::optional<Color> color; // see LineOptions::color
        float alpha = 1.0f;
        std::optional<Color> edgecolor;
        float linewidth = 0.0f;
        LineStyle linestyle = LineStyle::Solid;

        // The radius of a hole through the sector, in the space of `r` (0 = none).
        // Kept as the wedge's data, read back and replaced through WedgeData::inner,
        // not through these options.
        double inner = 0.0;

        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible
        double zorder = 0.0;     // see LineOptions::zorder
    };

    // A text's look (Axes::text(), annotate(), and the Axes3D ones). Sizes are
    // pixels as drawn; a text keeps its size under zoom and in 3D at any
    // distance.
    struct TextOptions {
        float fontsize = 12.0f;
        Color color = Color::Black;
        // Opacity of everything the text draws (text, box and arrow), on top of
        // each color's own alpha.
        float alpha = 1.0f;
        std::string font_path; // "" = renderer default

        // Where the block of lines sits against its anchor.
        HAlign ha = HAlign::Left;
        VAlign va = VAlign::Baseline;

        // Degrees counter-clockwise, about the anchor.
        float rotation = 0.0f;

        // Pixel nudge of the whole text (box included) from where its position
        // puts it, y up.
        float dx = 0.0f, dy = 0.0f;

        // '\n' starts a new line, each aligned by `ha`; lines are `linespacing`
        // x fontsize apart.
        float linespacing = 1.2f;

        // A box around the text, `pad` pixels out from it: filled with
        // `background` (alpha 0 = no fill) and outlined `edge_linewidth` pixels
        // wide (0 = no outline) in `edgecolor` (unset = the text's color).
        Color background = {1.0f, 1.0f, 1.0f, 0.0f};
        std::optional<Color> edgecolor;
        float edge_linewidth = 0.0f;
        float pad = 4.0f;

        // Cut the text (and its arrow) at the plot frame. Off by default: a text
        // whose data position leaves the view is hidden whole instead.
        bool clip_to_frame = false;

        // Draw $...$ spans as math (FigureOptions::mathtext, per text; only
        // when the figure's is on too). false draws this text as written, as
        // matplotlib's parse_math=False.
        bool parse_math = true;

        // false hides the text, an annotate()'s arrow with it: not drawn or
        // exported. Axes::set_text_visible() changes it after the call.
        bool visible = true;
    };

    // The arrow annotate() draws from its text to the point.
    struct ArrowOptions {
        // At the point, and at the text.
        ArrowHead head = ArrowHead::Filled;
        ArrowHead tail = ArrowHead::None;
        float head_length = 10.0f;
        float head_width = 7.0f;

        float linewidth = 1.25f;
        // Unset = the text's color.
        std::optional<Color> color;
        LineStyle linestyle = LineStyle::Solid;

        // Pixels left clear between the arrow and the text's box, and before
        // the point.
        float gap_text = 2.0f;
        float gap_point = 0.0f;

        // 0 draws a straight arrow. Otherwise it bows to one side: the control
        // point sits `arc` x the arrow's length off its midpoint (negative bows
        // the other way), as matplotlib's arc3.
        float arc = 0.0f;
    };

    // Cosmetics for Figure::suptitle(), which spans the whole grid.
    struct SuptitleOptions {
        float fontsize = 21.0f;
        Color color = {0.1f, 0.1f, 0.1f, 1.0f};
        std::string font_path; // "" = renderer default

        // Left/Right anchor to the figure edge, not the plot area (which moves
        // with tick label widths).
        HAlign align = HAlign::Center;

        // Pixel nudge. offset_y does not enlarge the band (sized from `fontsize`),
        // so a large value overlaps the first row.
        float offset_x = 0.0f;
        float offset_y = 0.0f;
    };

    // Space in pixels between the figure edge and the subplot grid. Gaps between
    // subplots are FigureOptions::subplot_col_gap / subplot_row_gap.
    struct FigureMargins {
        float left = 10.0f;
        float right = 10.0f;
        float top = 10.0f;
        float bottom = 10.0f;
    };

    // Read-back: copies of one plot object's data as its axes holds it now (see
    // Axes::line_data()). Fields are named after the arguments that made it.
    struct LineData {
        // line(y) stored x as 0, 1, 2, ...
        std::vector<double> x, y;
    };

    struct ScatterData {
        std::vector<double> x, y;
        // Per-point diameters (ScatterOptions::sizes); empty = the uniform `size`.
        // set_scatter_data(i, ScatterData) takes them as given (empty clears
        // them); the span overload keeps them while the point count is unchanged.
        std::vector<double> sizes;
    };

    struct ScatterZData {
        std::vector<double> x, y, z;
        std::vector<double> sizes; // see ScatterData::sizes
    };

    // Also what hist() made: bin centers and (possibly normalised) counts.
    struct BarData {
        std::vector<double> x, height;
        // Each bar's base (BarOptions::bottom), one per bar; empty = 0. A single
        // value given to bar() comes back repeated per bar. set_bar_data(i,
        // BarData) takes it as given (empty clears it, one value is every bar);
        // the span overload keeps it while the bar count is unchanged.
        std::vector<double> bottom;
    };

    // Row-major rows x cols, as passed. Values are stored in single precision,
    // so they come back rounded to float. `channels` is 1 for a heatmap (values
    // through the colormap) and 3 or 4 for an imshow_rgb() image, whose `data`
    // is then rows x cols x channels, each in [0, 1] (bytes come back / 255).
    struct HeatmapData {
        std::vector<double> data;
        int rows = 0, cols = 0;
        Range xrange, yrange;
        int channels = 1;
    };

    // How an axline() is given: a value on one axis, a point and a slope, or
    // two points.
    enum class AxLineMode { Value, PointSlope, TwoPoints };

    // Axes::axline(). Value: `value`, on the axis its orientation names.
    // PointSlope: through (x, y) with `slope` (dy/dx). TwoPoints: through (x, y)
    // and (x2, y2). The unused fields are 0. A sloped line whose points are all in
    // data units is the straight line of the data (it bends on a log or symlog
    // axis); otherwise the points are positions on the frame and the line is
    // straight on screen, its slope the screen's (y up).
    struct AxLineData {
        double value = 0.0;
        AxLineMode mode = AxLineMode::Value;
        Pos x, y, x2, y2;
        double slope = 0.0;
    };

    // Axes::axspan(): the band's ends, lo <= hi as axspan() stored them.
    struct AxSpanData {
        double lo = 0.0, hi = 1.0;
    };

    // Axes::fill(): `x` is the run coordinate (y when the fill is
    // Vertical), `y1` and `y2` the curves, one value each per point (a scalar
    // y2 comes back repeated). `exclude` as FillOptions::exclude;
    // set_fill_data(i, FillData) takes it as given (empty clears it), the span
    // overload keeps it while the point count is unchanged.
    struct FillData {
        std::vector<double> x, y1, y2;
        std::vector<bool> exclude;
    };

    // Axes::quiver(): each arrow's position and vector, one value each per
    // arrow, and `c`, the values its colors map (empty = one flat color).
    // set_quiver_data(i, QuiverData) takes `c` as given (empty or one per
    // arrow); the span overload keeps it while the arrow count is unchanged.
    struct QuiverData {
        std::vector<double> x, y, u, v, c;
    };

    // Axes::shape_poly(): the vertices, in the options' `space`.
    struct PolyData {
        std::vector<double> x, y;
    };

    // Axes::shape_circle(): the centre and radius, each in its own space. A
    // Data radius needs a Data centre (x and y both); a Fraction radius is a
    // fraction of the frame's shorter side.
    struct CircleData {
        Pos cx, cy, r;
    };

    // Axes::shape_wedge(): a circle's numbers, the sweep in degrees counter-
    // clockwise from +x, and `inner`, the radius of the hole (0 = none) in the
    // space of `r`.
    struct WedgeData {
        Pos cx, cy, r;
        double theta1 = 0.0, theta2 = 90.0, inner = 0.0;
    };

    // Axes::text() and annotate(): the string and where it is. `arrow` is set
    // for annotate(), whose arrow points at (px, py) in data coordinates.
    struct TextData {
        std::string text;
        Pos x, y;
        bool arrow = false;
        double px = 0.0, py = 0.0;
    };
} // namespace sextant
