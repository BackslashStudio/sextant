#pragma once
#include "events.h"
#include "export.h"
#include "style.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sextant {
    // A point or direction in box coordinates.
    struct Vec3 {
        double x = 0.0, y = 0.0, z = 0.0;
    };

    // Side lengths of the box every 3D axes normalizes its data onto, centred on
    // the origin. Camera values are in box units, so they are independent of data
    // units. {1,1,1} is a cube; {2,1,1} draws x twice as long as y.
    struct BoxAspect {
        double x = 1.0, y = 1.0, z = 1.0;
    };

    // Orthographic (default) keeps parallel edges parallel, so lengths are
    // comparable anywhere in the box; perspective adds depth cues.
    enum class Projection {
        Orthographic,
        Perspective,
    };

    // Orbit camera in box coordinates. The box is fitted to the cell every frame,
    // so `zoom` scales that fit and there is no distance to set.
    struct Camera3D {
        // Degrees. Azimuth is about the box's z axis (0 looks along +x); elevation
        // is above the xy plane, clamped to +-89.
        double azimuth = -60.0;
        double elevation = 30.0;

        // Box-space point the camera orbits; defaults to the box centre.
        Vec3 target{0.0, 0.0, 0.0};

        // Magnifies the fitted picture (> 1 in, < 1 out). Does not change the
        // amount of perspective; that is `fov`.
        double zoom = 1.0;

        Projection projection = Projection::Orthographic;

        // Vertical field of view in degrees, perspective only; clamped to [5, 120].
        // The eye distance is derived from it: narrow = distant and nearly
        // parallel, wide = close and strongly foreshortened.
        double fov = 45.0;
    };

    // A pair of box axes, named by the two it spans; the third is its normal.
    // bar3d: the axis bars stand along. Plane2D: the plane's facing.
    //
    //   XY -> bars stand along z, on an x-y grid
    //   YZ -> along x, on a y-z grid
    //   ZX -> along y, on a z-x grid
    enum class PlaneOrientation { XY, YZ, ZX };

    struct Plane2DOptions {
        // Whole-plane opacity; multiplies the objects' own alpha. Below 1 the plane
        // must be depth-sorted per camera, so 1 is the cheap path.
        float alpha = 1.0f;

        // Hides the drawing only; limits, colorbars and legend keys stay, so
        // toggling a slice off never rescales the box.
        bool visible = true;
    };

    struct Bar3DOptions {
        // Unset = the axes' next color-cycle color, taken at the call (see
        // Axes3D::set_color_cycle()). The Data panel shows the color taken.
        std::optional<Color> color;

        // Multiplies the own alpha of `color` and `edgecolor`. Below 1 bars are
        // translucent and every face is drawn back to front. This needs
        // per-camera ordering every frame; exactly 1 keeps the cheap depth-buffer
        // path.
        float alpha = 1.0f;

        // Footprint as a fraction of the grid spacing in each direction; 1.0 makes
        // neighbours touch.
        float width = 0.8f; // along u
        float depth = 0.8f; // along v

        // Base of every bar, unless the `bottoms` overload is used.
        double bottom = 0.0;

        // Per-face flat shading from a light fixed in box space, so a face keeps
        // its brightness while orbiting. 0 = flat `color`, 1 = dimmest face black.
        float shading = 0.45f;

        // Bar outlines. Width is in pixels at the bar's depth (thinner when
        // distant under perspective). Translucent bars show all twelve edges;
        // for solid outlines on faint bars, lower `color`'s alpha, not `alpha`.
        bool edges = false;
        Color edgecolor = Color::Black;
        float edge_linewidth = 1.0f;

        // Legend key: a swatch of `color`. Empty draws no key.
        std::string name;
        bool show_legend = true;
        bool visible = true;     // see LineOptions::visible

        // Optional hover text per bar, below the default "x=.., y=.., height=.."
        // line. Indexed like `heights`. Empty or out of range = none.
        std::vector<std::string> hint_labels;
    };

    // A surface over the same `u x v` grid as bar3d, with `heights` laid out the
    // same way (row-major, u major).
    struct SurfaceOptions {
        // Flat color of every cell, unless `colormap` is on. Unset: see
        // Bar3DOptions::color; with `colormap` on it takes the next color without
        // advancing the cycle.
        std::optional<Color> color;

        // Color each cell by its height instead.
        bool colormap = false;
        Colormap cmap = Colormap::Viridis;

        // Colormap range in data units. Equal (default) means the surface's own
        // range -- unlike HeatmapOptions/ScatterZOptions, whose default is 0..1.
        float vmin = 0.0f, vmax = 0.0f;

        // Draw a colorbar (only with `colormap` on). It spans the resolved range.
        bool colorbar = false;

        // Names the colorbar scale when `colormap` is on; otherwise keys the
        // legend with a swatch of `color`. A colormapped surface has no legend key.
        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible

        // Multiplies the own alpha of `color` and `edgecolor`. Below 1 the surface
        // is translucent. Against other translucent objects the ordering is a
        // heuristic.
        float alpha = 1.0f;

        // Per-cell flat shading from bar3d's box-space light. 0 = flat, 1 = unlit
        // cells black.
        float shading = 0.45f;

        // Wireframe along the sampling grid. Width is in pixels at the box centre
        // and thins with distance.
        bool edges = false;
        Color edgecolor = Color::Black;
        float edge_linewidth = 1.0f;

        // Optional hover text per sample (not per cell), below the default
        // "x=.., y=.., z=.." line; indexed like `heights`. The tooltip reports the
        // nearest of a cell's four samples.
        std::vector<std::string> hint_labels;
    };

    // A sheet on a triangulated mesh, for data without a `u x v` grid.
    //
    // Color is per vertex and interpolates across a triangle; shading is flat per
    // face. The *value* is interpolated and then colormapped per fragment, so every
    // color on the mesh appears on the colorbar.
    struct SurfaceTriOptions {
        // Flat color of the whole mesh, unless a `colors` vector was given (which
        // is what makes a mesh colormapped; there is no `colormap` flag since a
        // mesh has no height axis). Unset: see SurfaceOptions::color, `colors`
        // standing for `colormap`.
        std::optional<Color> color;

        // Colormap for a `colors` vector; read only by the overloads that take one.
        Colormap cmap = Colormap::Viridis;

        // Colormap range. Equal (default) means the mesh's own range.
        float vmin = 0.0f, vmax = 0.0f;

        // Draw a colorbar (only with a `colors` vector).
        bool colorbar = false;

        // Names the colorbar scale when colormapped; otherwise keys the legend with
        // a swatch of `color`. A colormapped mesh has no legend key.
        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible

        // Multiplies the own alpha of `color` and `edgecolor`. Below 1 the mesh is
        // translucent. A mesh can occlude itself; depth peeling resolves that
        // exactly, the sorted fallback approximately.
        float alpha = 1.0f;

        // Per-face flat shading, as SurfaceOptions::shading. Two-sided (|n.l|) and
        // never backface-culled, since mesh winding is often inconsistent.
        float shading = 0.45f;

        // Wireframe: all three edges of every triangle (shared edges drawn twice).
        // Width is in pixels at the box centre and thins with distance.
        bool edges = false;
        Color edgecolor = Color::Black;
        float edge_linewidth = 1.0f;

        // Optional hover text per vertex, index-aligned with x/y/z, below the
        // default "x=.., y=.., z=.." line.
        std::vector<std::string> hint_labels;
    };

    // Error-bar data in a scene: per-point spans passed to scatter3d()/line3d().
    // A parameter rather than an options field because options are copied into
    // every snapshot. Write with designated initializers:
    //
    //     ax3->scatter3d(x, y, z, {.z_cap_lo = err}, opts);
    //
    // Same rules as the 2D ErrorBar: offsets, magnitude-valued; one end given means
    // symmetric; cap data draws the whisker, box data the box; zero draws nothing
    // on that side and non-finite reads as zero. A non-empty span must hold one
    // entry per point, or the method throws. The spans are copied during the call.
    //
    // In a scene, each whisker and its caps are a flat billboard facing the eye at
    // the point (a whisker pointing at the camera draws nothing). The box is a
    // block per point: `[p - box_lo, p + box_hi]` on axes with box data,
    // ErrorBar3DOptions::boxwidth wide on the others.
    struct ErrorBar3D {
        std::span<const double> x_cap_lo, x_cap_hi, x_box_lo, x_box_hi;
        std::span<const double> y_cap_lo, y_cap_hi, y_box_lo, y_box_hi;
        std::span<const double> z_cap_lo, z_cap_hi, z_box_lo, z_box_hi;

        // True when anything at all has been set.
        bool any() const {
            for (std::span<const double> s: {
                     x_cap_lo, x_cap_hi, x_box_lo, x_box_hi,
                     y_cap_lo, y_cap_hi, y_box_lo, y_box_hi,
                     z_cap_lo, z_cap_hi, z_box_lo, z_box_hi
                 })
                if (!s.empty()) return true;
            return false;
        }
    };

    // Error-bar style in a scene. Pixel lengths (`linewidth`, `capsize`,
    // `boxwidth`) are measured at the box centre and converted to box units, so a
    // distant error bar keeps its proportions under perspective.
    struct ErrorBar3DOptions {
        // Unset = the series' flat `color`, or black for a series with `colors`.
        std::optional<Color> color;

        // Whisker, caps and box edges. 0 draws no error bar at all.
        float linewidth = 1.0f;

        // Total cap length across each whisker end. 0 = no caps.
        float capsize = 6.0f;

        // Flat crossbar, or an open chevron `capsize` wide and 0.87 x `capsize`
        // long, shrunk when the whisker is shorter.
        CapStyle capstyle = CapStyle::Flat;

        // Block extent on each axis without box data.
        float boxwidth = 10.0f;

        // Opacity of the block's faces as a fraction of `color`'s alpha; 0 drops
        // them. The whole error bar also takes the series' `alpha`.
        float box_alpha = 0.25f;
    };

    // Markers at points in the scene: the 3D scatter() and scatter_z() in one. A
    // `colors` vector makes it colormapped. Note: in 3D `z` is a coordinate; the
    // color dimension is `colors` (unlike 2D scatter_z, where z is the color).
    struct Scatter3DOptions {
        // Flat color of every marker, unless a `colors` vector was given. Unset:
        // see SurfaceTriOptions::color.
        std::optional<Color> color;

        // Marker diameter in pixels, constant regardless of depth.
        float size = 20.0f;
        // Per-point diameters; see ScatterOptions::sizes (read back through
        // Scatter3DData::sizes).
        std::vector<double> sizes;
        MarkerStyle marker = MarkerStyle::Circle;

        // Multiplies the own alpha of every color (fill, outline, error bars), as
        // ScatterOptions::alpha. Below 1 markers are translucent (the expensive
        // path). Defaults to 1, unlike ScatterOptions::alpha's 0.8.
        float alpha = 1.0f;
        bool filled = true; // see ScatterOptions::filled

        // Marker outline; see ScatterOptions::edgecolor. Unset color = the
        // marker's own fill color. `depthshade` darkens the outline with the fill;
        // a marker with an outline below full alpha takes the translucent path.
        std::optional<Color> edgecolor;
        float edge_linewidth = 0.0f;

        // Darken markers toward black with distance: `depthshade * t`, t from 0 at
        // the box's near face to 1 at its far face (the box's extent, not the
        // series'). 0 = off. The legend key and colorbar are never darkened.
        float depthshade = 0.0f;

        // Colormap for a `colors` vector; read only by the overloads that take one.
        Colormap cmap = Colormap::Viridis;

        // Colormap range. Equal (default) means the series' own range.
        float vmin = 0.0f, vmax = 0.0f;

        // Draw a colorbar (only with a `colors` vector). It spans the resolved range.
        bool colorbar = false;

        // Legend key: the marker filled with `color`, or for a `colors` series
        // filled white with a black edge. With `colors` it also names the
        // colorbar scale.
        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible

        // Style of the error bars passed as an ErrorBar3D.
        ErrorBar3DOptions errorbar;

        // Optional hover text per point, index-aligned with x/y/z, below the
        // default "x=.., y=.., z=.." line (plus "c=.." with `colors`).
        std::vector<std::string> hint_labels;
    };

    // A path through the points in the order given: the 3D line().
    struct Line3DOptions {
        // Flat color of the path, unless a `colors` vector was given. Unset: see
        // SurfaceTriOptions::color.
        std::optional<Color> color;

        // Width in pixels at the box centre, converted once to a length in the box,
        // so under perspective a distant stretch is thinner.
        float linewidth = 1.5f;

        // No `linestyle`: a world-space ribbon has no pixel arc length to dash.

        // Multiplies `color`'s own alpha, and the error bars'. Below 1 the path
        // is translucent (the expensive path).
        float alpha = 1.0f;

        // Add a closing segment from the last point to the first. No point is
        // added; with `colors` it ramps from the last color back to the first.
        bool loop = false;

        // Darken toward black with distance, as Scatter3DOptions::depthshade.
        float depthshade = 0.0f;

        // Colormap for a `colors` vector; read only by the overloads that take one.
        Colormap cmap = Colormap::Viridis;

        // Colormap range. Equal (default) means the series' own range.
        float vmin = 0.0f, vmax = 0.0f;

        // Draw a colorbar (only with a `colors` vector).
        bool colorbar = false;

        // Legend key: a short segment in `color`, or for a `colors` series the
        // colormap swept along it.
        std::string name;
        bool show_legend = true; // see LineOptions::show_legend
        bool visible = true;     // see LineOptions::visible

        // Style of the error bars passed as an ErrorBar3D.
        ErrorBar3DOptions errorbar;

        // Optional hover text per vertex, index-aligned with x/y/z.
        std::vector<std::string> hint_labels;
    };

    // The box's own furniture; axis annotation is AxesStyle, shared with 2D.
    struct Box3DStyle {
        bool panes = true;
        Color pane_color = {0.94f, 0.94f, 0.96f, 1.0f};
        Color pane_edge_color = {0.75f, 0.75f, 0.78f, 1.0f};

        // Fraction of the frame left empty around the projected box for tick
        // labels and titles. Fixed, not measured from the text, because 3D label
        // positions depend on the camera fit, which depends on the frame.
        float margin = 0.12f;
    };

    // Read-back copies of the 3D kinds, as LineData is for 2D.
    struct Bar3DData {
        PlaneOrientation orient = PlaneOrientation::XY;
        std::vector<double> u, v, heights;
        // Empty = every bar stands on Bar3DOptions::bottom.
        std::vector<double> bottoms;
    };

    struct SurfaceData {
        PlaneOrientation orient = PlaneOrientation::XY;
        std::vector<double> u, v, heights;
    };

    struct SurfaceTriData {
        std::vector<double> x, y, z;
        // As passed, or the Delaunay triangulation derived from an orientation.
        std::vector<std::uint32_t> tri;
        // Empty for a flat mesh.
        std::vector<double> colors;
    };

    struct Scatter3DData {
        std::vector<double> x, y, z;
        // Empty for a flat series.
        std::vector<double> colors;
        // Per-point diameters; see ScatterData::sizes.
        std::vector<double> sizes;
    };

    struct Line3DData {
        std::vector<double> x, y, z;
        // Empty for a flat path.
        std::vector<double> colors;
    };

    // Axes3D::text(), text2d() and annotate(): the string and where it is.
    //   text()      (x, y, z) the data point it is drawn at.
    //   text2d()    `in_frame`: (x, y) fractions of the frame; z unused.
    //   annotate()  `arrow`: (x, y, z) the point, the text (dx, dy) pixels from
    //               it, y up.
    struct Text3DData {
        std::string text;
        double x = 0.0, y = 0.0, z = 0.0;
        bool in_frame = false;
        bool arrow = false;
        double dx = 0.0, dy = 0.0;
    };

    // A 2D plane in the 3D scene: one of the three orientations, at an offset along
    // its normal, carrying the 2D plot kinds. In-plane coordinates are the parent's
    // data coordinates (a plane at XY, offset 0.5 spans x and y at z = 0.5), and
    // its data feeds the parent's auto limits. Not an Axes: limits, ticks and
    // titles belong to the parent. Threads: as Axes (see Figure). A Plane2D keeps
    // no pointer to its parent, so one outliving it is safe, merely inert.
    class SEXTANT_API Plane2D {
    public:
        ~Plane2D();

        // The 2D plot kinds, in the parent's data coordinates along the plane's two
        // axes: XY -> (x, y), YZ -> (y, z), ZX -> (z, x). Options mean what they
        // mean in 2D, except that line widths, bar edges and error-bar strokes are
        // pixels at the box centre (they scale with depth under perspective), while
        // markers and contours keep a fixed screen size. An unset `color` comes
        // from the parent axes' color cycle (see Axes3D::set_color_cycle()).
        Plane2D& line(std::span<const double> x, std::span<const double> y,
                      LineOptions opts = {});

        Plane2D& line(std::span<const double> y, LineOptions opts = {});

        Plane2D& scatter(std::span<const double> x, std::span<const double> y,
                         ScatterOptions opts = {});

        Plane2D& scatter_z(std::span<const double> x, std::span<const double> y,
                           std::span<const double> z, ScatterZOptions opts = {});

        Plane2D& bar(std::span<const double> x, std::span<const double> height,
                     BarOptions opts = {});

        // With error bars, as on Axes. See ErrorBar.
        Plane2D& line(std::span<const double> x, std::span<const double> y,
                      const ErrorBar& err, LineOptions opts = {});

        Plane2D& line(std::span<const double> y, const ErrorBar& err, LineOptions opts = {});

        Plane2D& scatter(std::span<const double> x, std::span<const double> y,
                         const ErrorBar& err, ScatterOptions opts = {});

        Plane2D& scatter_z(std::span<const double> x, std::span<const double> y,
                           std::span<const double> z, const ErrorBar& err,
                           ScatterZOptions opts = {});

        Plane2D& bar(std::span<const double> x, std::span<const double> height,
                     const ErrorBar& err, BarOptions opts = {});

        // Ranges are the mesh's outer edges, as in 2D; reversed mirrors, degenerate
        // throws. Contours keep a fixed width on screen.
        Plane2D& heatmap(std::span<const double> data, int rows, int cols,
                         Range xrange, Range yrange, HeatmapOptions opts = {});

        // heatmap() over the index extent, as Axes::imshow().
        Plane2D& imshow(std::span<const double> data, int rows, int cols,
                        HeatmapOptions opts = {});

        // An RGB or RGBA image, as Axes::imshow_rgb().
        Plane2D& imshow_rgb(std::span<const double> data, int rows, int cols, int channels,
                            ImageOptions opts = {});

        Plane2D& imshow_rgb(std::span<const std::uint8_t> data, int rows, int cols, int channels,
                            ImageOptions opts = {});

        // Cells of any size, as Axes::heatmap_rec().
        Plane2D& heatmap_rec(std::span<const double> data, int rows, int cols,
                             std::span<const double> x_edges, std::span<const double> y_edges,
                             HeatmapRecOptions opts = {});

        // A heatmap over triangles, as Axes::heatmap_tri() (both forms).
        Plane2D& heatmap_tri(std::span<const double> x, std::span<const double> y,
                             std::span<const double> values, std::span<const std::uint32_t> tri,
                             HeatmapTriOptions opts = {});

        Plane2D& heatmap_tri(std::span<const double> x, std::span<const double> y,
                             std::span<const double> values, HeatmapTriOptions opts = {});

        // The area between two curves, as Axes::fill().
        Plane2D& fill(std::span<const double> x, std::span<const double> y1,
                              std::span<const double> y2, FillOptions opts = {});

        Plane2D& fill(std::span<const double> x, std::span<const double> y1,
                              double y2 = 0.0, FillOptions opts = {});

        // A band across the plane between two values: over the plane's v by default,
        // its u with Vertical, as Axes::axspan(); the frame it spans is the plane.
        // Axes::axline() has no counterpart on a plane.
        Plane2D& axspan(double lo, double hi, AxSpanOptions opts = {});

        // Filled shapes, as Axes::shape_poly(), shape_circle() and shape_wedge();
        // Pixels and Fraction positions are on the plane's own sheet.
        Plane2D& shape_poly(std::span<const double> x, std::span<const double> y, PolyOptions opts = {});

        Plane2D& shape_circle(Pos cx, Pos cy, Pos r, CircleOptions opts = {});

        Plane2D& shape_wedge(Pos cx, Pos cy, Pos r, double theta1, double theta2, WedgeOptions opts = {});

        // Stacked areas, as Axes::stack().
        Plane2D& stack(std::span<const double> x, std::span<const std::vector<double>> ys,
                       StackOptions opts = {});

        // An arrow field, as Axes::quiver().
        Plane2D& quiver(std::span<const double> x, std::span<const double> y,
                        std::span<const double> u, std::span<const double> v,
                        QuiverOptions opts = {});

        Plane2D& quiver(std::span<const double> x, std::span<const double> y,
                        std::span<const double> u, std::span<const double> v,
                        std::span<const double> c, QuiverOptions opts = {});

        // Position along the plane's normal axis, in data units.
        Plane2D& set_offset(double offset);

        // Whole-plane opacity; multiplies the objects' own alpha.
        Plane2D& set_alpha(float alpha);

        // Clears the plane's objects; the parent's color cycle carries on.
        Plane2D& cla();

        PlaneOrientation orientation() const;

        double offset() const;

        // Identity, as on Axes: id() is the plane's own (Axes3D::remove_plane() takes
        // it), last_object() the latest object made on it, remove() and
        // find_object() as Axes's, over the plane's objects.
        ObjectId id() const;
        ObjectId last_object() const;
        bool remove(ObjectId id);
        std::optional<ObjectLocation> find_object(ObjectId id) const;

        // Paint order within the plane, as Axes::set_zorder() and zorder().
        bool set_zorder(ObjectId id, double zorder);
        std::optional<double> zorder(ObjectId id) const;

        // Read-back of the plane's own objects, as on Axes (limits and titles
        // are the parent's).
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

        std::size_t axspan_count() const;
        AxSpanData axspan_data(std::size_t i) const;

        std::size_t shape_poly_count() const;
        PolyData shape_poly_data(std::size_t i) const;

        std::size_t shape_circle_count() const;
        CircleData shape_circle_data(std::size_t i) const;

        std::size_t shape_wedge_count() const;
        WedgeData shape_wedge_data(std::size_t i) const;

        std::size_t heatmap_tri_count() const;
        HeatmapTriData heatmap_tri_data(std::size_t i) const;

        std::size_t quiver_count() const;
        QuiverData quiver_data(std::size_t i) const;

        // Replace object i's data, as Axes::set_line_data() and its siblings.
        Plane2D& set_line_data(std::size_t i, const LineData& data);

        Plane2D& set_line_data(std::size_t i, std::span<const double> x, std::span<const double> y);

        Plane2D& set_scatter_data(std::size_t i, const ScatterData& data);

        Plane2D& set_scatter_data(std::size_t i, std::span<const double> x, std::span<const double> y);

        Plane2D& set_scatter_z_data(std::size_t i, const ScatterZData& data);

        Plane2D& set_scatter_z_data(std::size_t i, std::span<const double> x,
                                    std::span<const double> y, std::span<const double> z);

        Plane2D& set_bar_data(std::size_t i, const BarData& data);

        Plane2D& set_bar_data(std::size_t i, std::span<const double> x,
                              std::span<const double> height);

        Plane2D& set_heatmap_data(std::size_t i, const HeatmapData& data);

        Plane2D& set_heatmap_data(std::size_t i, std::span<const double> data, int rows, int cols,
                                  Range xrange, Range yrange);

        Plane2D& set_fill_data(std::size_t i, const FillData& data);

        Plane2D& set_fill_data(std::size_t i, std::span<const double> x, std::span<const double> y1,
                               std::span<const double> y2);

        Plane2D& set_axspan_data(std::size_t i, const AxSpanData& data);

        Plane2D& set_shape_poly_data(std::size_t i, const PolyData& data);

        Plane2D& set_shape_poly_data(std::size_t i, std::span<const double> x, std::span<const double> y);

        Plane2D& set_shape_circle_data(std::size_t i, const CircleData& data);

        Plane2D& set_shape_wedge_data(std::size_t i, const WedgeData& data);

        // A heatmap_tri()'s vertices, values and triangles, checked as the explicit
        // form of heatmap_tri() checks them (`tri` is required; nothing re-triangulates).
        // The span form without `tri` keeps the triangles, so the vertex count must
        // stay. hint_labels are kept while the vertex count is unchanged.
        Plane2D& set_heatmap_tri_data(std::size_t i, const HeatmapTriData& data);

        Plane2D& set_heatmap_tri_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                      std::span<const double> values, std::span<const std::uint32_t> tri);

        Plane2D& set_heatmap_tri_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                      std::span<const double> values);

        Plane2D& set_quiver_data(std::size_t i, const QuiverData& data);

        Plane2D& set_quiver_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                 std::span<const double> u, std::span<const double> v);

        // Show or hide object i, as Axes::set_line_visible() and its siblings:
        // a hidden object leaves the box's auto limits, the legend and the
        // colorbars (unlike the plane's own Plane2DOptions::visible), and a
        // plane whose objects are all hidden counts as empty.
        Plane2D& set_line_visible(std::size_t i, bool visible);
        bool line_visible(std::size_t i) const;

        Plane2D& set_scatter_visible(std::size_t i, bool visible);
        bool scatter_visible(std::size_t i) const;

        Plane2D& set_scatter_z_visible(std::size_t i, bool visible);
        bool scatter_z_visible(std::size_t i) const;

        Plane2D& set_bar_visible(std::size_t i, bool visible);
        bool bar_visible(std::size_t i) const;

        Plane2D& set_heatmap_visible(std::size_t i, bool visible);
        bool heatmap_visible(std::size_t i) const;

        Plane2D& set_fill_visible(std::size_t i, bool visible);
        bool fill_visible(std::size_t i) const;

        Plane2D& set_axspan_visible(std::size_t i, bool visible);
        bool axspan_visible(std::size_t i) const;

        Plane2D& set_shape_poly_visible(std::size_t i, bool visible);
        bool shape_poly_visible(std::size_t i) const;

        Plane2D& set_shape_circle_visible(std::size_t i, bool visible);
        bool shape_circle_visible(std::size_t i) const;

        Plane2D& set_shape_wedge_visible(std::size_t i, bool visible);
        bool shape_wedge_visible(std::size_t i) const;
        Plane2D& set_heatmap_tri_visible(std::size_t i, bool visible);
        bool heatmap_tri_visible(std::size_t i) const;

        Plane2D& set_quiver_visible(std::size_t i, bool visible);
        bool quiver_visible(std::size_t i) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
        friend class Axes3D;
        friend class Figure;

        Plane2D(PlaneOrientation orient, double offset, Plane2DOptions opts);
    };

    // Threads: as Axes (see Figure).
    class SEXTANT_API Axes3D {
    public:
        ~Axes3D();

        // ----------------------------------------------------------------
        // Plot types
        // ----------------------------------------------------------------
        // Bars on the grid `u` x `v`, standing along `orient`'s normal. `heights`
        // is |u| x |v| row-major, u major. Throws if a vector is empty or
        // non-finite, or `heights` is not |u| * |v| long. `bottoms`, indexed like
        // `heights`, gives each bar its own base (default Bar3DOptions::bottom).
        Axes3D& bar3d(PlaneOrientation orient,
                      std::span<const double> u, std::span<const double> v,
                      std::span<const double> heights, Bar3DOptions opts = {});

        Axes3D& bar3d(PlaneOrientation orient,
                      std::span<const double> u, std::span<const double> v,
                      std::span<const double> heights,
                      std::span<const double> bottoms, Bar3DOptions opts = {});

        // A surface over the grid `u` x `v`, rising along `orient`'s normal. Same
        // layout and throw conditions as bar3d; the grid must be at least 2 x 2.
        Axes3D& surface(PlaneOrientation orient,
                        std::span<const double> u, std::span<const double> v,
                        std::span<const double> heights, SurfaceOptions opts = {});

        // A sheet on a triangulated mesh: vertices (x[i], y[i], z[i]) and `tri`,
        // 3 indices per triangle (row-major, as matplotlib's `triangles`).
        // Throws if a vector is empty or non-finite, the coordinate lengths differ,
        // there are fewer than three vertices, `tri` is empty or not a multiple of
        // three, or an index is >= |x|. Degenerate triangles are kept, so the face
        // count matches the caller's.
        Axes3D& surface_tri(std::span<const double> x, std::span<const double> y,
                            std::span<const double> z,
                            std::span<const std::uint32_t> tri,
                            SurfaceTriOptions opts = {});

        // The same, colored per vertex by `colors` through opts.cmap/vmin/vmax
        // (overrides opts.color). `colors` is indexed like x/y/z; a triangle
        // interpolates between its vertices' values. Empty = the overload above.
        Axes3D& surface_tri(std::span<const double> x, std::span<const double> y,
                            std::span<const double> z,
                            std::span<const std::uint32_t> tri,
                            std::span<const double> colors,
                            SurfaceTriOptions opts = {});

        // The same two, with the topology derived: a Delaunay triangulation of the
        // vertices projected onto the plane `orient` names. It runs once at
        // ingest; editing a vertex later does not re-triangulate.
        //   - duplicate points keep their slots (so `colors`/`hint_labels` align);
        //   - collinear input throws;
        //   - a concave domain is filled to its convex hull -- pass `tri`
        //     explicitly for a non-convex domain.
        Axes3D& surface_tri(std::span<const double> x, std::span<const double> y,
                            std::span<const double> z, PlaneOrientation orient,
                            SurfaceTriOptions opts = {});

        Axes3D& surface_tri(std::span<const double> x, std::span<const double> y,
                            std::span<const double> z, PlaneOrientation orient,
                            std::span<const double> colors,
                            SurfaceTriOptions opts = {});

        // Markers at the points (x[i], y[i], z[i]). Throws if a vector is empty or
        // non-finite, the lengths differ, or a non-empty ErrorBar3D span does not
        // hold one entry per point. `scatter3d(x, y, z, {}, opts)` is ambiguous;
        // name a field (`{.z_cap_lo = e}`) or pass a real vector.
        Axes3D& scatter3d(std::span<const double> x, std::span<const double> y,
                          std::span<const double> z, Scatter3DOptions opts = {});

        // With error bars. See ErrorBar3D.
        Axes3D& scatter3d(std::span<const double> x, std::span<const double> y,
                          std::span<const double> z, const ErrorBar3D& err,
                          Scatter3DOptions opts = {});

        // With a fourth dimension coloring each marker through opts.cmap/vmin/vmax
        // (overrides opts.color). `colors` is indexed like x/y/z; empty = the
        // overload above.
        Axes3D& scatter3d(std::span<const double> x, std::span<const double> y,
                          std::span<const double> z, std::span<const double> colors,
                          Scatter3DOptions opts = {});

        // Both at once.
        Axes3D& scatter3d(std::span<const double> x, std::span<const double> y,
                          std::span<const double> z, std::span<const double> colors,
                          const ErrorBar3D& err, Scatter3DOptions opts = {});

        // A path through the points (x[i], y[i], z[i]) in the order given. Throws
        // as scatter3d does, and for fewer than two points.
        Axes3D& line3d(std::span<const double> x, std::span<const double> y,
                       std::span<const double> z, Line3DOptions opts = {});

        // With error bars. See ErrorBar3D.
        Axes3D& line3d(std::span<const double> x, std::span<const double> y,
                       std::span<const double> z, const ErrorBar3D& err,
                       Line3DOptions opts = {});

        // With a fourth dimension coloring the path through opts.cmap/vmin/vmax
        // (overrides opts.color). `colors` is indexed like x/y/z; a segment ramps
        // between its endpoints' colors. Empty = the overload above.
        Axes3D& line3d(std::span<const double> x, std::span<const double> y,
                       std::span<const double> z, std::span<const double> colors,
                       Line3DOptions opts = {});

        // Both at once.
        Axes3D& line3d(std::span<const double> x, std::span<const double> y,
                       std::span<const double> z, std::span<const double> colors,
                       const ErrorBar3D& err, Line3DOptions opts = {});

        // A 2D plane spanning the two axes `orient` names, at `offset` (data units)
        // along the third. Returns the plane for drawing on:
        //
        //     ax3->plane(PlaneOrientation::XY, 0.5)->heatmap(field, r, c, {400,700}, {-1,1});
        //
        // Any number of planes; insertion order only breaks depth ties. Throws on a
        // non-finite offset.
        std::shared_ptr<Plane2D> plane(PlaneOrientation orient, double offset,
                                       Plane2DOptions opts = {});

        // The planes made so far, in the order plane() made them. plane_at() returns
        // the same object plane() did; throws std::out_of_range for i >= plane_count().
        std::size_t plane_count() const;

        std::shared_ptr<Plane2D> plane_at(std::size_t i) const;

        // Planes by identity (Plane2D::id()): last_plane() is what the latest plane()
        // call made, 0 before any. remove_plane() takes a plane out of the box and
        // returns true; the Plane2D the caller holds keeps working but is no longer
        // drawn. An id that is not one of this axes' planes changes nothing, returns
        // false and sends a message. The later planes move down one in plane_at().
        // plane_index() is the `i` of plane_at(i) now, nothing for an unknown id --
        // what a Pick event's `pick_plane` needs.
        ObjectId last_plane() const;
        bool remove_plane(ObjectId id);
        std::optional<std::size_t> plane_index(ObjectId id) const;

        // The axes' own objects (not a plane's), as Axes: last_object(), remove(),
        // find_object().
        ObjectId last_object() const;
        bool remove(ObjectId id);
        std::optional<ObjectLocation> find_object(ObjectId id) const;

        // ----------------------------------------------------------------
        // Text -- as Axes::text(); drawn over the scene at a fixed pixel
        // size whatever the camera, and never hidden behind geometry
        // ----------------------------------------------------------------
        // `s` at the data point (x, y, z), projected. Hidden while the point is
        // outside the limits or behind the camera. Throws std::invalid_argument
        // for a non-finite coordinate.
        Axes3D& text(std::string_view s, double x, double y, double z, TextOptions opts = {});

        // `s` at (fx, fy), fractions of the frame from its bottom-left corner,
        // whatever the camera does.
        Axes3D& text2d(std::string_view s, double fx, double fy, TextOptions opts = {});

        // `s` dx, dy pixels (y up) from the projected point (x, y, z), with an
        // arrow to the point. Hidden as text() is.
        Axes3D& annotate(double x, double y, double z, std::string_view s, double dx, double dy,
                         TextOptions opts = {}, ArrowOptions arrow = {});

        // ----------------------------------------------------------------
        // Decoration -- all return *this for chaining, like Axes
        // ----------------------------------------------------------------
        Axes3D& set_title(std::string_view text, float fontsize = 18.0f);

        Axes3D& set_xtitle(std::string_view text, float fontsize = 16.5f);

        Axes3D& set_ytitle(std::string_view text, float fontsize = 16.5f);

        Axes3D& set_ztitle(std::string_view text, float fontsize = 16.5f);

        Axes3D& set_xlim(double lo, double hi);

        Axes3D& set_ylim(double lo, double hi);

        Axes3D& set_zlim(double lo, double hi);

        Axes3D& set_xticks(std::span<const double> positions,
                           std::vector<std::string> labels = {});

        Axes3D& set_yticks(std::span<const double> positions,
                           std::vector<std::string> labels = {});

        Axes3D& set_zticks(std::span<const double> positions,
                           std::vector<std::string> labels = {});

        // Grid lines on the three back panes at the tick positions. On by default.
        Axes3D& grid(bool enable = true, GridOptions opts = {});

        Axes3D& set_axes_style(AxesStyle opts = {});

        Axes3D& set_box_style(Box3DStyle opts = {});

        Axes3D& set_box_aspect(BoxAspect aspect);

        // Keys every named series of this axes, then of its planes in plane order.
        Axes3D& legend(LegendOptions opts = {});

        // Styling only, for every colorbar in the cell (including those requested by
        // plot objects on planes). A colorbar is requested by the plot object.
        Axes3D& set_colorbar_style(ColorbarOptions opts = {});

        // As Axes::set_color_cycle(), for bar3d(), surface(), surface_tri(),
        // scatter3d() and line3d() and the 2D calls on this axes' planes, which
        // share the cycle. A colormapped object takes the next color without
        // advancing the cycle.
        Axes3D& set_color_cycle(std::vector<Color> colors);

        // ----------------------------------------------------------------
        // Camera
        // ----------------------------------------------------------------
        // Elevation is clamped to +-89 degrees (see Camera3D).
        Axes3D& set_view(double azimuth_deg, double elevation_deg);

        Axes3D& set_camera(Camera3D cam);

        // Includes navigation in the window once Figure::refresh() has folded it
        // in, unless a camera setter was called after it.
        Camera3D camera() const;

        // Projection mode and perspective field of view (clamped to [5, 120]).
        // Neither changes the box's size on screen.
        Axes3D& set_projection(Projection mode);

        Axes3D& set_fov(double degrees);

        // The camera a double-click (and the window's Edit > Home) resets to.
        // Unaffected by set_camera() and drags. Never called: those go back to
        // the camera of the last set_camera()/set_view().
        Axes3D& set_default_camera(Camera3D cam);

        // Clears the objects and planes and resets the rest, the color cycle included.
        Axes3D& cla();

        // ----------------------------------------------------------------
        // Read-back
        // ----------------------------------------------------------------
        // As on Axes: the caller's calls plus panel edits to titles, limits and
        // plot data once Figure::refresh() has folded them in.
        std::string title() const;

        std::string xtitle() const;

        std::string ytitle() const;

        std::string ztitle() const;

        // The limits as drawn: set_xlim()'s, or the auto scale of the data.
        Range xlim() const;

        Range ylim() const;

        Range zlim() const;

        // The axes' own objects in the order they were added; a plane's are
        // read from the Plane2D. `*_data(i)` throws std::out_of_range for
        // i >= `*_count()`.
        std::size_t bar3d_count() const;
        Bar3DData bar3d_data(std::size_t i) const;

        std::size_t surface_count() const;
        SurfaceData surface_data(std::size_t i) const;

        std::size_t surface_tri_count() const;
        SurfaceTriData surface_tri_data(std::size_t i) const;

        std::size_t scatter3d_count() const;
        Scatter3DData scatter3d_data(std::size_t i) const;

        std::size_t line3d_count() const;
        Line3DData line3d_data(std::size_t i) const;

        // text(), text2d() and annotate() alike.
        std::size_t text_count() const;
        Text3DData text_data(std::size_t i) const;

        // ----------------------------------------------------------------
        // Updating plotted data
        // ----------------------------------------------------------------
        // Replace object i's data, as Axes::set_line_data() and its siblings:
        // validated as the plotting call would, hint_labels (and error bars)
        // kept while the point count or grid shape is unchanged. A bar
        // footprint is kept unless its u or v changes. set_surface_tri_data()
        // takes the topology as given and never re-triangulates.
        //
        // Each also takes its data as spans, in the plotting call's argument
        // order (see Axes::set_line_data()). As with the plotting calls, the
        // overload without `bottoms` stands every bar on Bar3DOptions::bottom,
        // and the one without `colors` makes the object flat-coloured.
        Axes3D& set_bar3d_data(std::size_t i, const Bar3DData& data);

        Axes3D& set_bar3d_data(std::size_t i, PlaneOrientation orient,
                               std::span<const double> u, std::span<const double> v,
                               std::span<const double> heights);

        Axes3D& set_bar3d_data(std::size_t i, PlaneOrientation orient,
                               std::span<const double> u, std::span<const double> v,
                               std::span<const double> heights, std::span<const double> bottoms);

        Axes3D& set_surface_data(std::size_t i, const SurfaceData& data);

        Axes3D& set_surface_data(std::size_t i, PlaneOrientation orient,
                                 std::span<const double> u, std::span<const double> v,
                                 std::span<const double> heights);

        Axes3D& set_surface_tri_data(std::size_t i, const SurfaceTriData& data);

        Axes3D& set_surface_tri_data(std::size_t i, std::span<const double> x,
                                     std::span<const double> y, std::span<const double> z,
                                     std::span<const std::uint32_t> tri);

        Axes3D& set_surface_tri_data(std::size_t i, std::span<const double> x,
                                     std::span<const double> y, std::span<const double> z,
                                     std::span<const std::uint32_t> tri,
                                     std::span<const double> colors);

        Axes3D& set_scatter3d_data(std::size_t i, const Scatter3DData& data);

        Axes3D& set_scatter3d_data(std::size_t i, std::span<const double> x,
                                   std::span<const double> y, std::span<const double> z);

        Axes3D& set_scatter3d_data(std::size_t i, std::span<const double> x,
                                   std::span<const double> y, std::span<const double> z,
                                   std::span<const double> colors);

        Axes3D& set_line3d_data(std::size_t i, const Line3DData& data);

        Axes3D& set_line3d_data(std::size_t i, std::span<const double> x,
                                std::span<const double> y, std::span<const double> z);

        Axes3D& set_line3d_data(std::size_t i, std::span<const double> x,
                                std::span<const double> y, std::span<const double> z,
                                std::span<const double> colors);

        // A text's string and placement, which may turn it into any of the
        // three (setting `arrow` on a text gives it default ArrowOptions).
        // Throws std::invalid_argument for a non-finite number, or `arrow` with
        // `in_frame`.
        Axes3D& set_text_data(std::size_t i, const Text3DData& data);

        // The string and (x, y, z) only, keeping the rest: a text2d() reads x, y
        // as fractions and ignores z; an annotate() keeps its offset.
        Axes3D& set_text_data(std::size_t i, std::string_view s, double x, double y, double z);

        // ----------------------------------------------------------------
        // Hiding plot objects
        // ----------------------------------------------------------------
        // Show or hide the axes' own object i, as Axes::set_line_visible() and
        // its siblings (a plane's objects through the Plane2D).
        Axes3D& set_bar3d_visible(std::size_t i, bool visible);
        bool bar3d_visible(std::size_t i) const;

        Axes3D& set_surface_visible(std::size_t i, bool visible);
        bool surface_visible(std::size_t i) const;

        Axes3D& set_surface_tri_visible(std::size_t i, bool visible);
        bool surface_tri_visible(std::size_t i) const;

        Axes3D& set_scatter3d_visible(std::size_t i, bool visible);
        bool scatter3d_visible(std::size_t i) const;

        Axes3D& set_line3d_visible(std::size_t i, bool visible);
        bool line3d_visible(std::size_t i) const;

        Axes3D& set_text_visible(std::size_t i, bool visible);
        bool text_visible(std::size_t i) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
        friend class Figure;

        explicit Axes3D();
    };
} // namespace sextant
