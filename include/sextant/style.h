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

    // Honored for data lines, grid lines and legend swatches in every output.
    // `None` draws no stroke, and drops a data series from the legend.
    enum class LineStyle { Solid, Dashed, Dotted, DashDot, None };

    enum class MarkerStyle { None, Circle, Square, Triangle, Cross, Plus, Diamond };

    enum class Colormap { Viridis };

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
        float box_alpha = 0.25f;
    };

    // No markers: add Axes::scatter() on the same data for a marked series.
    // A legend key is drawn iff `show_legend && !name.empty()`.
    struct LineOptions {
        Color color = Color::Blue;
        float linewidth = 1.5f;
        LineStyle linestyle = LineStyle::Solid;
        std::string name;
        bool show_legend = true;
        float alpha = 1.0f;

        // Add a closing segment from the last point to the first. No point is
        // added (limits, hover and legend unchanged); the seam is joined and a dash
        // pattern continues across it. On a Plane2D only the width carries over,
        // since planes stroke solid.
        bool loop = false;

        // Style of the error bars passed as an ErrorBar.
        ErrorBarOptions errorbar;

        // Optional hover text per point, index-aligned with x/y, shown below the
        // default "x=.., y=.." line. Empty or out of range = none.
        std::vector<std::string> hint_labels;
    };

    struct ScatterOptions {
        Color color = Color::Blue;
        float size = 20.0f;
        MarkerStyle marker = MarkerStyle::Circle;
        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        float alpha = 0.8f;

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
        MarkerStyle marker = MarkerStyle::Circle;
        float alpha = 0.8f;
        float vmin = 0.0f;
        float vmax = 1.0f;
        bool colorbar = false;

        // Names the color scale, drawn along the outer side of this series'
        // colorbar; empty draws and reserves nothing. Also the legend key, shown
        // as the marker shape filled white with a black edge.
        std::string name;
        bool show_legend = true; // see LineOptions::show_legend

        // Style of the error bars passed as an ErrorBar; unset color = black.
        ErrorBarOptions errorbar;

        // See LineOptions::hint_labels. Default hover text is "x=.., y=.., z=..".
        std::vector<std::string> hint_labels;
    };

    struct BarOptions {
        Color color = Color::Blue;
        float width = 0.8f;
        float alpha = 1.0f;
        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
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

    struct GridOptions {
        Color color = {0.8f, 0.8f, 0.8f, 1.0f};

        // `None` draws no grid lines.
        LineStyle linestyle = LineStyle::Solid;
        float linewidth = 0.5f;
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

    // Axes-frame cosmetics: spine, ticks, tick labels and titles. Font sizes are
    // in pixels as drawn.
    struct AxesStyle {
        Color spine_color = {0.3f, 0.3f, 0.3f, 1.0f};
        float spine_linewidth = 1.0f;

        // The four 2D frame edges. Ticks and tick labels are independent of these
        // (as in matplotlib). Ignored in 3D; see Box3DStyle.
        bool spine_bottom = true, spine_left = true, spine_top = true, spine_right = true;

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

        Color tick_color = {0.3f, 0.3f, 0.3f, 1.0f};
        float tick_length = 5.0f;
        float tick_linewidth = 1.0f;
        Color label_color = {0.2f, 0.2f, 0.2f, 1.0f};
        float label_fontsize = 11.0f;
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

    // Horizontal placement of a piece of figure-level text.
    enum class HAlign { Left, Center, Right };

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
    };

    struct ScatterZData {
        std::vector<double> x, y, z;
    };

    // Also what hist() made: bin centers and (possibly normalised) counts.
    struct BarData {
        std::vector<double> x, height;
    };

    // Row-major rows x cols, as passed. Values are stored in single precision,
    // so they come back rounded to float.
    struct HeatmapData {
        std::vector<double> data;
        int rows = 0, cols = 0;
        Range xrange, yrange;
    };
} // namespace sextant
