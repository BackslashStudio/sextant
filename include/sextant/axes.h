#pragma once
#include "events.h"
#include "export.h"
#include "style.h"
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
#include <memory>

namespace sextant {
    // Part of its Figure's object graph: any thread, one call at a time per graph,
    // window open or not (see Figure).
    class SEXTANT_API Axes {
    public:
        ~Axes();

        // ----------------------------------------------------------------
        // Plot methods — all return *this for chaining
        // ----------------------------------------------------------------

        Axes& line(std::span<const double> x, std::span<const double> y,
                   LineOptions opts = {});

        Axes& line(std::span<const double> y, LineOptions opts = {});

        Axes& scatter(std::span<const double> x, std::span<const double> y,
                      ScatterOptions opts = {});

        // Each point's color maps z[i] through opts.cmap/vmin/vmax.
        Axes& scatter_z(std::span<const double> x, std::span<const double> y,
                        std::span<const double> z, ScatterZOptions opts = {});

        Axes& bar(std::span<const double> x, std::span<const double> height,
                  BarOptions opts = {});

        // The same four, with error bars. Write `err` with designated initializers;
        // a bare `{}` is ambiguous.
        Axes& line(std::span<const double> x, std::span<const double> y,
                   const ErrorBar& err, LineOptions opts = {});

        Axes& line(std::span<const double> y, const ErrorBar& err, LineOptions opts = {});

        Axes& scatter(std::span<const double> x, std::span<const double> y,
                      const ErrorBar& err, ScatterOptions opts = {});

        Axes& scatter_z(std::span<const double> x, std::span<const double> y,
                        std::span<const double> z, const ErrorBar& err,
                        ScatterZOptions opts = {});

        Axes& bar(std::span<const double> x, std::span<const double> height,
                  const ErrorBar& err, BarOptions opts = {});

        // BarOptions::width is a fraction of the bin width; the default argument
        // sets it to 1.0 (bins touch). Passing your own BarOptions restores 0.8.
        // BarOptions::orientation Horizontal lays the bins along y, and
        // BarOptions::bottom takes one value or one per bin.
        Axes& hist(std::span<const double> data, int bins = 10,
                   BarOptions bar_opts = {.width = 1.0f},
                   HistOptions hist_opts = {});

        // `data` is row-major rows x cols, drawn as a uniform mesh over
        // xrange x yrange (cell edges, see Range): even in data units, so on a Log
        // or Symlog axis the cells follow the scale. Throws if either range is
        // non-finite or degenerate. On a Log axis the part of a cell at or below 0
        // is not drawn.
        Axes& heatmap(std::span<const double> data, int rows, int cols,
                      Range xrange, Range yrange, HeatmapOptions opts = {});

        // heatmap() over x in [0, cols], y in [0, rows]: unit cells, so tick 3 is
        // the boundary between columns 2 and 3.
        Axes& imshow(std::span<const double> data, int rows, int cols,
                     HeatmapOptions opts = {});

        // An RGB (channels 3) or RGBA (4) image, row-major rows x cols x channels:
        // each pixel is its own colour, no colormap. Doubles are clamped to [0, 1]
        // when drawn, bytes span 0..255; a pixel with a NaN channel is transparent.
        // Over the index extent unless ImageOptions gives ranges. For read-back
        // and picking it is a heatmap (heatmap_count(), heatmap_data(),
        // set_heatmap_data(), remove(), ...), with HeatmapData::channels 3 or 4.
        Axes& imshow_rgb(std::span<const double> data, int rows, int cols, int channels,
                         ImageOptions opts = {});

        Axes& imshow_rgb(std::span<const std::uint8_t> data, int rows, int cols, int channels,
                         ImageOptions opts = {});

        // Row-major rows x cols values over cells of any size, as matplotlib's
        // pcolormesh: column j spans x_edges[j]..x_edges[j+1] (cols + 1 edges),
        // row i y_edges[i]..y_edges[i+1] (rows + 1), so row 0 is at y_edges[0].
        // Edges are finite and strictly increasing or decreasing. Log-spaced edges
        // give cells even on a log axis. Throws std::invalid_argument for a wrong
        // edge count or order or too little data, and adds nothing. It is a
        // heatmap for read-back and picking (heatmap_count(), heatmap_data() with
        // HeatmapData::x_edges / y_edges, set_heatmap_data(), remove(), ...).
        Axes& heatmap_rec(std::span<const double> data, int rows, int cols,
                          std::span<const double> x_edges, std::span<const double> y_edges,
                          HeatmapRecOptions opts = {});

        // A heatmap over triangles, as matplotlib's tripcolor(shading="gouraud"): one
        // value per vertex (x[i], y[i]), interpolated across each triangle and
        // coloured per pixel through the colormap. `tri` holds three vertex indices
        // per triangle, row-major. Edges are straight on screen, so on a Log or
        // Symlog axis a triangle follows its vertices, not the scale. A NaN value, or
        // a vertex a Log axis cannot place (<= 0), leaves out its triangles. Throws
        // std::invalid_argument if x or y is non-finite, the lengths differ, there
        // are fewer than three vertices, `tri` is empty or not a multiple of three,
        // or an index is >= |x|; nothing is added then. A heatmap_tri has its own
        // count, read-back and pick kind (PickKind::HeatmapTri).
        Axes& heatmap_tri(std::span<const double> x, std::span<const double> y,
                          std::span<const double> values, std::span<const std::uint32_t> tri,
                          HeatmapTriOptions opts = {});

        // The same with the triangles derived: a Delaunay triangulation of (x, y),
        // once, at this call. Duplicate points keep their slots, a concave domain
        // fills to its convex hull (pass `tri` for one), and collinear points throw.
        // `heatmap_tri(x, y, values, {})` is ambiguous with the form above: leave the
        // options out, or name a field.
        Axes& heatmap_tri(std::span<const double> x, std::span<const double> y,
                          std::span<const double> values, HeatmapTriOptions opts = {});

        // Contour lines over a grid of points, as matplotlib's contour(X, Y, Z) with 1D
        // X and Y: z is row-major, y.size() rows of x.size() values, z[i * cols + j]
        // at (x[j], y[i]). x and y are finite and strictly monotone (either way), at
        // least 2 each; z may hold NaN, which leaves out the squares it touches.
        // Each square is split into four triangles around its centre (the corner
        // mean), and the lines are the levels' crossings of that surface, straight
        // on screen between grid edges. ContourOptions picks the levels (or their
        // number), colours, width and labels. Throws std::invalid_argument for wrong
        // lengths, fewer than 2 x or y, unordered or non-finite x or y, a
        // non-finite level, more than 16384 levels, nlevels < 1, or a norm that
        // doesn't fit; nothing is added then. Its own kind: contour_count(),
        // contour_data(), PickKind::Contour.
        Axes& contour(std::span<const double> x, std::span<const double> y, std::span<const double> z,
                      ContourOptions opts = {});

        // Filled contours, as matplotlib's contourf: contour() with
        // `opts.filled` set. Each band between neighbouring levels takes one
        // colour, decided per pixel on the same triangles, so contour() over the
        // same data follows the band edges.
        Axes& contourf(std::span<const double> x, std::span<const double> y, std::span<const double> z,
                       ContourOptions opts = {});

        // A 2D histogram of the points (x[i], y[i]), as matplotlib's hist2d: the
        // count (or density) of each bin, drawn as a heatmap() over even bins, or
        // as a heatmap_rec() when Hist2dOptions gives edges for either axis. Not a
        // kind of its own: the result counts in heatmap_count(), and its values
        // read back through heatmap_data() (row 0 the lowest y bin). A point
        // outside the bins or with a NaN coordinate is not counted. Throws
        // std::invalid_argument if x and y differ in length, a bin count is < 1, a
        // range is non-finite or empty, edges are too few, non-finite or not
        // increasing, or the norm doesn't fit vmin/vmax; nothing is added then.
        // Axes only, not Plane2D.
        Axes& hist2d(std::span<const double> x, std::span<const double> y, Hist2dOptions opts = {});

        // ----------------------------------------------------------------
        // Filled areas
        // ----------------------------------------------------------------
        // The area between y1(x) and y2(x) (matplotlib's fill_between); with
        // FillOptions::orientation Vertical, between x1(y) and x2(y), the spans
        // then read as y, x1, x2 (fill_betweenx). One value each per point, any
        // other lengths throw std::invalid_argument; fewer than two points fill
        // nothing. Each run of points neither excluded (FillOptions::exclude)
        // nor holding a NaN is one region, its curves straight between the points
        // (or stepped, FillOptions::drawstyle); where they cross the region
        // pinches to the crossing. On a log axis a point at or below 0 along the
        // run breaks it, while a curve at or below 0 reaches past the frame's
        // low edge. Automatic limits take every point of every region on both
        // curves. Drawn with the other plot objects in call order; not hovered
        // or picked.
        Axes& fill(std::span<const double> x, std::span<const double> y1,
                           std::span<const double> y2, FillOptions opts = {});

        // The same down to (or across to) one value: 0 by default, the area
        // under a curve.
        Axes& fill(std::span<const double> x, std::span<const double> y1,
                           double y2 = 0.0, FillOptions opts = {});

        // ----------------------------------------------------------------
        // Shapes
        // ----------------------------------------------------------------
        // A filled polygon through the vertices (x[i], y[i]), closed implicitly,
        // in PolyOptions::space (data units by default). Needs at least 3
        // finite vertices and one y per x, else std::invalid_argument and
        // nothing is added. Simple polygons only: a self-intersecting one
        // is drawn without a promise about its fill. Automatic limits take Data
        // vertices; the colour comes from the cycle like line(). Drawn in call
        // order, hovered and picked where it covers.
        Axes& shape_poly(std::span<const double> x, std::span<const double> y, PolyOptions opts = {});

        // A filled circle at (cx, cy) with radius r, each a Pos: data units,
        // a fraction of the frame, or pixels (Pos::pixels()). A Data radius needs
        // a Data centre and is measured along each axis, so the circle is an
        // ellipse where the axes' scales differ and bends on a log axis; a
        // Fraction radius is a fraction of the frame's shorter side, a Pixels one
        // a length; both are round. Throws std::invalid_argument for a
        // non-finite number, r <= 0, or a Data radius with a non-Data centre.
        // Limits take a circle whose centre and radius are all Data.
        Axes& shape_circle(Pos cx, Pos cy, Pos r, CircleOptions opts = {});

        // A sector of the circle, from theta1 to theta2 degrees counter-clockwise
        // from +x (theta2 < theta1 wraps; 360 or more apart is the whole circle;
        // equal draws nothing), with a hole of radius WedgeOptions::inner in
        // r's space when set (a ring segment). Checks as shape_circle(), plus
        // finite angles and 0 <= inner < r.
        Axes& shape_wedge(Pos cx, Pos cy, Pos r, double theta1, double theta2, WedgeOptions opts = {});

        // Stacked areas (matplotlib's stackplot): layer k fills from the top of
        // the layers below it (the baseline, StackOptions::baseline, for the
        // first) up by ys[k], one value per point of `x` (any other length
        // throws std::invalid_argument, before anything is added). The curves
        // are computed here, once, and each layer is added as an ordinary fill,
        // in order: read back and edited as any other, and editing one does not
        // restack the rest. A NaN carries up the stack: with Zero it leaves that
        // point out of its layer and every layer above; the other baselines take
        // every layer's value, so the point leaves every layer (WeightedWiggle,
        // as matplotlib's, every later point as well).
        Axes& stack(std::span<const double> x, std::span<const std::vector<double>> ys,
                    StackOptions opts = {});

        // ----------------------------------------------------------------
        // Arrow fields
        // ----------------------------------------------------------------
        // An arrow per point (matplotlib's quiver with angles='xy' and
        // scale_units='xy'): from (x, y) along (u, v) times QuiverOptions::amp,
        // in data units, shifted by QuiverOptions::pivot. One value each per
        // arrow, any other lengths throw std::invalid_argument. An arrow with a
        // NaN, a zero vector, or an end at or below 0 on a log axis is not drawn.
        // The arrow is sized in pixels across (QuiverOptions::width and the head)
        // and in data units along, so it points the way the data does only at
        // equal aspect. Automatic limits take both ends of every arrow drawn.
        // Hovered at (x, y); drawn with the other plot objects in call order.
        Axes& quiver(std::span<const double> x, std::span<const double> y,
                     std::span<const double> u, std::span<const double> v,
                     QuiverOptions opts = {});

        // The same, each arrow colored by its value in `c` (one per arrow, else
        // std::invalid_argument) through QuiverOptions::cmap/vmin/vmax, as
        // matplotlib's quiver(X, Y, U, V, C). An arrow whose c is NaN is not
        // drawn; QuiverOptions::colorbar explains the colors.
        Axes& quiver(std::span<const double> x, std::span<const double> y,
                     std::span<const double> u, std::span<const double> v,
                     std::span<const double> c, QuiverOptions opts = {});

        // ----------------------------------------------------------------
        // Reference lines and spans
        // ----------------------------------------------------------------
        // A line across the frame at `value`: at y = value by default
        // (matplotlib's axhline), at x = value with AxLineOptions::orientation
        // Vertical (axvline). It runs over the frame between the options' `from`
        // and `to` fractions whatever the limits, and stays in place through pan
        // and zoom along it. Automatic limits take `value` as a point on its axis
        // (on a log axis only a value > 0), the other axis nothing; on a log axis
        // a value <= 0 is never drawn. Drawn with the other plot objects in call
        // order; not hovered or picked. Throws std::invalid_argument for a
        // non-finite value, `from` or `to`.
        Axes& axline(double value, AxLineOptions opts = {});

        // A line through (x, y) with slope dy/dx, or through two points: the
        // infinite line (matplotlib's axline), the same kind as the value form
        // (cycle colour, legend, zorder); `orientation`, `from` and `to` do not
        // apply. With every point in data units it is the straight line of the
        // data: it bends on a log or symlog axis, where a straight line in data
        // is a curve on screen, and is drawn so. With any point a Fraction or
        // Pixels Pos it is a straight screen line through them, the slope then
        // the screen's (y up). Two points with equal x give a vertical line.
        // Automatic limits take the data points. Throws std::invalid_argument
        // for a non-finite number, or two equal points.
        Axes& axline(Pos x, Pos y, double slope, AxLineOptions opts = {});

        Axes& axline(Pos x1, Pos y1, Pos x2, Pos y2, AxLineOptions opts = {});

        // A band across the frame from `lo` to `hi`: over y by default
        // (matplotlib's axhspan), over x with Vertical (axvspan); `lo > hi` is
        // stored swapped. Limits take both ends as axline() takes its value; on a
        // log axis an end <= 0 reaches past the frame's low edge. Otherwise as
        // axline().
        Axes& axspan(double lo, double hi, AxSpanOptions opts = {});

        // ----------------------------------------------------------------
        // Text
        // ----------------------------------------------------------------
        // `s` at (x, y): each a data value or Pos::fraction() of the plot frame.
        // Drawn over the data at a fixed pixel size; it never widens the auto
        // limits and reserves no room. A text with a data coordinate is hidden
        // while that coordinate is out of view (see TextOptions::clip_to_frame).
        // Throws std::invalid_argument for a non-finite coordinate.
        Axes& text(std::string_view s, Pos x, Pos y, TextOptions opts = {});

        // `s` at (tx, ty) as text() places it, with an arrow to the point
        // (px, py) in data coordinates. Hidden while the point is out of view.
        Axes& annotate(double px, double py, std::string_view s, Pos tx, Pos ty,
                       TextOptions opts = {}, ArrowOptions arrow = {});

        // ----------------------------------------------------------------
        // Decoration
        // ----------------------------------------------------------------

        // "Title" names an axis or the axes; "label" is per-tick text.
        // fontsize is in pixels and stored in AxesStyle, so a later
        // set_axes_style() resets it.
        Axes& set_title(std::string_view text, float fontsize = 18.0f);

        Axes& set_xtitle(std::string_view text, float fontsize = 16.5f);

        Axes& set_ytitle(std::string_view text, float fontsize = 16.5f);

        Axes& set_xlim(double lo, double hi);

        Axes& set_ylim(double lo, double hi);

        // The axis' scale (see Scale); the limits stay in data units. Throws
        // std::invalid_argument for options the scale cannot use. Switching to Log
        // drops set limits that reach 0 or below back to automatic. On a log axis, set_xlim()/set_ylim() throw for an end <= 0.
        Axes& set_xscale(Scale scale, ScaleOptions opts = {});

        Axes& set_yscale(Scale scale, ScaleOptions opts = {});

        // Share this axes' x (y) axis with `other`'s, as matplotlib's sharex():
        // the two, and every axes either already shares it with, then hold one
        // set of x limits and one x scale. This side takes `other`'s. A
        // set_xlim(), set_xscale(), pan or zoom on any of them moves them all,
        // and automatic limits cover all their data. Tick positions, formats and
        // titles stay per axes. Inner tick labels hide by themselves (see
        // AxesStyle::show_xtick_labels). Throws std::invalid_argument for an axes
        // of another Figure; nothing changes then. There is no unsharing.
        Axes& sharex(Axes& other);

        Axes& sharey(Axes& other);

        // A second axes over this one's frame, as matplotlib's twinx(): it shares
        // this axes' x axis (sharex()) and has its own y axis on the right of
        // the frame, this one's y axis then staying on the left whatever
        // AxesStyle::yaxis_x or origin_x either holds. Plot into it as into any
        // axes; objects of the two draw in call order, whichever took them. The
        // cell's title, legend, colorbars and x axis stay one: set_title(),
        // legend(), set_colorbar_style(), set_xtitle(), set_xticks() and
        // set_xtick_format() on the twin set this axes', title() and xtitle()
        // read it, and the legend lists both axes' series. set_axes_style() on
        // the twin likewise keeps the fields of its y axis (and the shared tick
        // and label fields that axis falls back on), gives those of the x axis
        // and of the cell (background, frame, axes title) to this axes, and
        // drops what a twin can't use (axis positions, origins, z). The twin
        // takes this axes' color cycle, so its series continue the colors. Its
        // own x axis, frame and background are never drawn, its grid draws its
        // y lines only, and its y title sits on the right; AxesStyle::yaxis_color
        // colors each one's frame edge. Pan and zoom move both axes. A second
        // call returns the same twin; cla() on this axes keeps it. The twin
        // lives as long as this axes. Throws std::invalid_argument on a twin.
        std::shared_ptr<Axes> twinx();

        // The same with the axes swapped: shares the y axis, its own x axis at
        // the top of the frame (this one's then at the bottom, whatever
        // xaxis_y or origin_y hold), its x title above it.
        std::shared_ptr<Axes> twiny();

        // Draw one y unit `ratio` times as long as one x unit on screen (1 =
        // equal, a circle stays round), as matplotlib's set_aspect(); 0 = auto,
        // the default: fill the frame. Units are the scales' (on log-log a
        // decade is as long on both axes). Box shrinks the frame, centred in its
        // space, with its ticks, titles, legend and colorbars (the frame then
        // leaves its column's and row's alignment); Datalim keeps the frame and
        // widens one axis' limits about their centre as drawn -- the unshared
        // one when the other is shared, else whichever must grow. xlim()/ylim()
        // report the limits before widening; a pan or zoom keeps the widened
        // ones. Pan and zoom keep the ratio either way. Throws
        // std::invalid_argument for a negative or non-finite ratio, on a twin
        // (its host's aspect sets the cell), and for Datalim with a ratio when
        // both axes are shared (a twin shares one); sharex(), sharey(), twinx()
        // and twiny() throw when they would make that so. cla() resets it.
        Axes& set_aspect(double ratio, Adjustable adjustable = Adjustable::Box);

        Axes& grid(bool enable = true, GridOptions opts = {});

        Axes& set_axes_style(AxesStyle opts = {});

        Axes& legend(LegendOptions opts = {});

        // Styling only; a colorbar is requested by HeatmapOptions/ScatterZOptions::colorbar.
        Axes& set_colorbar_style(ColorbarOptions opts = {});

        Axes& set_xticks(std::span<const double> positions,
                         std::vector<std::string> labels = {});

        Axes& set_yticks(std::span<const double> positions,
                         std::vector<std::string> labels = {});

        // printf's format for one double, used for every tick label this axes
        // makes: the generated ones, and set_xticks() positions given without a
        // label. Exactly one %f, %F, %e, %E, %g or %G (flags, width and
        // precision allowed), plus any %% and other text, math included:
        // "%.1f", "%.0f%%", "$%.2g$ s". Throws std::invalid_argument otherwise.
        // Empty restores the default: "%g" on a linear axis, powers ("10^3") on
        // a log or symlog one.
        Axes& set_xtick_format(std::string_view fmt);

        Axes& set_ytick_format(std::string_view fmt);

        // The colors an unset `color` takes in turn, one per call that draws in a
        // flat color: line(), scatter(), bar(), hist(), fill(), quiver(), axline()
        // (scatter_z(), heatmap() and axspan() take none). Restarts from the first
        // color; empty restores the default,
        // matplotlib's tab10, which starts with Color::Blue. Objects already
        // plotted keep theirs.
        Axes& set_color_cycle(std::vector<Color> colors);

        // Clear all plot objects and reset limits and the color cycle. A shared
        // axis stays shared and keeps the group's limits and scale.
        Axes& cla();

        // ----------------------------------------------------------------
        // Read-back
        // ----------------------------------------------------------------
        // What this axes holds now: the caller's own calls, plus panel edits to
        // titles, limits (pan/zoom included) and plot data once Figure::refresh()
        // has folded them in. Other panel edits are live preview and not reflected.
        std::string title() const;

        std::string xtitle() const;

        std::string ytitle() const;

        // The limits as drawn: set_xlim()'s, or the auto scale of the data.
        Range xlim() const;

        Range ylim() const;

        Scale xscale() const;

        Scale yscale() const;

        // set_aspect()'s values.
        double aspect() const;

        Adjustable adjustable() const;

        // Identity. Every plot object has an ObjectId (object_id.h), handed out by the
        // plotting call that makes it: last_object() is the latest call's (the last
        // one when a call makes several, as stack()), 0 before any. remove() takes the
        // object out for good and returns true; an id that is not this axes's
        // (unknown, removed already, another axes') changes nothing, returns false and
        // sends a message (Figure::set_message_handler()). Removing shifts the indices
        // of the later objects of its kind, so `*_data(i)` and `set_*_data(i)` follow
        // the new order; the ids stay. find_object() is where an id is now: nothing for
        // an unknown id, and for a text, axline or axspan (remove() takes those too).
        ObjectId last_object() const;
        bool remove(ObjectId id);
        std::optional<ObjectLocation> find_object(ObjectId id) const;

        // Paint order (the `zorder` option of every 2D kind): set_zorder() changes
        // object `id`'s, zorder() reads it. Objects draw in ascending zorder, those
        // of an equal one in the order of the calls; hover follows it, while the
        // legend and colorbars stay in call order. A text, an unknown id or a removed
        // one changes nothing, sends a message and gives false (nothing from
        // zorder()), as remove(). A zorder that is not finite throws
        // std::invalid_argument.
        bool set_zorder(ObjectId id, double zorder);
        std::optional<double> zorder(ObjectId id) const;

        // Plot objects of each kind, in the order they were added. `*_data(i)`
        // returns a copy and throws std::out_of_range for i >= `*_count()`.
        std::size_t line_count() const;
        LineData line_data(std::size_t i) const;

        std::size_t scatter_count() const;
        ScatterData scatter_data(std::size_t i) const;

        std::size_t scatter_z_count() const;
        ScatterZData scatter_z_data(std::size_t i) const;

        // bar() and hist() alike.
        std::size_t bar_count() const;
        BarData bar_data(std::size_t i) const;

        // heatmap() and imshow() alike.
        std::size_t heatmap_count() const;
        HeatmapData heatmap_data(std::size_t i) const;

        std::size_t fill_count() const;
        FillData fill_data(std::size_t i) const;

        std::size_t shape_poly_count() const;
        PolyData shape_poly_data(std::size_t i) const;

        std::size_t shape_circle_count() const;
        CircleData shape_circle_data(std::size_t i) const;

        std::size_t shape_wedge_count() const;
        WedgeData shape_wedge_data(std::size_t i) const;

        std::size_t heatmap_tri_count() const;
        HeatmapTriData heatmap_tri_data(std::size_t i) const;

        std::size_t contour_count() const;
        ContourData contour_data(std::size_t i) const;

        std::size_t quiver_count() const;
        QuiverData quiver_data(std::size_t i) const;

        std::size_t axline_count() const;
        AxLineData axline_data(std::size_t i) const;

        std::size_t axspan_count() const;
        AxSpanData axspan_data(std::size_t i) const;

        // text() and annotate() alike.
        std::size_t text_count() const;
        TextData text_data(std::size_t i) const;

        // ----------------------------------------------------------------
        // Updating plotted data
        // ----------------------------------------------------------------
        // Replace object i's data in place, keeping its options and the rest
        // of the axes, which cla() would reset: the way to animate a plot, with
        // Figure::refresh() after. Takes what `*_data(i)` returns, validated as
        // the plotting call would. Error bars and hint_labels stay while the
        // point count (a heatmap's rows and cols) is unchanged and are dropped
        // otherwise. Throws std::out_of_range for i >= `*_count()` and
        // std::invalid_argument for bad data; either way nothing changes.
        Axes& set_line_data(std::size_t i, const LineData& data);

        // Each also takes its data as spans, in the plotting call's argument
        // order: pass a caller's own arrays (an arma::vec, a column of a
        // matrix) without building the struct. Same checks and behaviour.
        Axes& set_line_data(std::size_t i, std::span<const double> x, std::span<const double> y);

        Axes& set_scatter_data(std::size_t i, const ScatterData& data);

        Axes& set_scatter_data(std::size_t i, std::span<const double> x, std::span<const double> y);

        Axes& set_scatter_z_data(std::size_t i, const ScatterZData& data);

        Axes& set_scatter_z_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                 std::span<const double> z);

        // The bar width is kept unless x changes; then it is re-derived from
        // the spacing, as bar() does. The bases follow BarData::bottom.
        Axes& set_bar_data(std::size_t i, const BarData& data);

        Axes& set_bar_data(std::size_t i, std::span<const double> x, std::span<const double> height);

        // A heatmap_rec() takes its new edges from HeatmapData::x_edges / y_edges
        // (xrange / yrange are not read), checked as heatmap_rec() checks them; a
        // heatmap() or image takes xrange / yrange and empty edges. Either form on
        // the other kind throws std::invalid_argument.
        Axes& set_heatmap_data(std::size_t i, const HeatmapData& data);

        // As heatmap() takes it: row-major rows x cols over xrange x yrange. Throws
        // for a heatmap_rec() (give it edges through HeatmapData).
        Axes& set_heatmap_data(std::size_t i, std::span<const double> data, int rows, int cols,
                               Range xrange, Range yrange);

        // A fill's points, validated as fill(). The exclude mask
        // follows FillData::exclude.
        Axes& set_fill_data(std::size_t i, const FillData& data);

        Axes& set_fill_data(std::size_t i, std::span<const double> x, std::span<const double> y1,
                            std::span<const double> y2);

        // A shape's numbers, validated as its plotting call.
        Axes& set_shape_poly_data(std::size_t i, const PolyData& data);

        Axes& set_shape_poly_data(std::size_t i, std::span<const double> x, std::span<const double> y);

        Axes& set_shape_circle_data(std::size_t i, const CircleData& data);

        Axes& set_shape_wedge_data(std::size_t i, const WedgeData& data);

        // A heatmap_tri()'s vertices, values and triangles, checked as the explicit
        // form of heatmap_tri() checks them (`tri` is required; nothing re-triangulates).
        // The span form without `tri` keeps the triangles, so the vertex count must
        // stay. hint_labels are kept while the vertex count is unchanged.
        Axes& set_heatmap_tri_data(std::size_t i, const HeatmapTriData& data);

        Axes& set_heatmap_tri_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                   std::span<const double> values, std::span<const std::uint32_t> tri);

        Axes& set_heatmap_tri_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                   std::span<const double> values);

        // A contour's grid, checked as contour() checks it. ContourData's levels
        // and `filled` replace the options' (empty levels: resolved again from
        // `nlevels`); the span form keeps both.
        Axes& set_contour_data(std::size_t i, const ContourData& data);

        Axes& set_contour_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                               std::span<const double> z);

        // A quiver's arrows, validated as quiver(). The amp stays; `c` follows
        // QuiverData::c (the span overload keeps it while the count holds).
        Axes& set_quiver_data(std::size_t i, const QuiverData& data);

        Axes& set_quiver_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                              std::span<const double> u, std::span<const double> v);

        // Move a reference line or span (validated as axline()/axspan(); a span's
        // ends are stored swapped when lo > hi).
        Axes& set_axline_data(std::size_t i, const AxLineData& data);

        Axes& set_axspan_data(std::size_t i, const AxSpanData& data);

        // A text's string and position, and whether it has an arrow and to
        // where (setting `arrow` on a text() gives it default ArrowOptions).
        Axes& set_text_data(std::size_t i, const TextData& data);

        // The string and position only; an annotation keeps its arrow and point.
        Axes& set_text_data(std::size_t i, std::string_view s, Pos x, Pos y);

        // ----------------------------------------------------------------
        // Hiding plot objects
        // ----------------------------------------------------------------
        // Show or hide object i (its options' `visible`, see
        // LineOptions::visible), keeping its data, options and place; with
        // Figure::refresh() after. A hidden object leaves the auto limits, the
        // legend and the colorbars. Wins over a Data-panel Visible toggle made
        // before the call. Throws std::out_of_range for i >= `*_count()`.
        Axes& set_line_visible(std::size_t i, bool visible);
        bool line_visible(std::size_t i) const;

        Axes& set_scatter_visible(std::size_t i, bool visible);
        bool scatter_visible(std::size_t i) const;

        Axes& set_scatter_z_visible(std::size_t i, bool visible);
        bool scatter_z_visible(std::size_t i) const;

        Axes& set_bar_visible(std::size_t i, bool visible);
        bool bar_visible(std::size_t i) const;

        Axes& set_heatmap_visible(std::size_t i, bool visible);
        bool heatmap_visible(std::size_t i) const;

        // A stack()'s layers are fills of their own.
        Axes& set_fill_visible(std::size_t i, bool visible);
        bool fill_visible(std::size_t i) const;

        Axes& set_shape_poly_visible(std::size_t i, bool visible);
        bool shape_poly_visible(std::size_t i) const;

        Axes& set_shape_circle_visible(std::size_t i, bool visible);
        bool shape_circle_visible(std::size_t i) const;

        Axes& set_shape_wedge_visible(std::size_t i, bool visible);
        bool shape_wedge_visible(std::size_t i) const;
        Axes& set_heatmap_tri_visible(std::size_t i, bool visible);
        bool heatmap_tri_visible(std::size_t i) const;

        Axes& set_contour_visible(std::size_t i, bool visible);
        bool contour_visible(std::size_t i) const;

        Axes& set_quiver_visible(std::size_t i, bool visible);
        bool quiver_visible(std::size_t i) const;

        Axes& set_axline_visible(std::size_t i, bool visible);
        bool axline_visible(std::size_t i) const;

        Axes& set_axspan_visible(std::size_t i, bool visible);
        bool axspan_visible(std::size_t i) const;

        // An annotation's arrow goes with its text.
        Axes& set_text_visible(std::size_t i, bool visible);
        bool text_visible(std::size_t i) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
        friend class Figure;
        // A Plane2D holds an Axes::Impl rather than being an Axes.
        friend class Plane2D;

        explicit Axes();
    };
} // namespace sextant
