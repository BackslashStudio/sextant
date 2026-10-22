#pragma once
#include "sextant/style.h"
#include "sextant/axes3d.h"
#include "sextant/object_id.h"
#include "axis_scale.h"
#include "color_norm.h"
#include "cow_vec.h"
#include "tick_style.h"
#include "tick.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace sextant {

// Names a plot object's per-kind vector; objects are addressed as (kind, index).
// The first five are 2D (also held by planes); then the native 3D kinds,
// always addressed at the axes (plane -1). Text is held by both 2D and 3D
// axes (never by a plane); AxLine and AxSpan by 2D axes only (a plane's sheet
// holds none); Fill, Quiver, Poly, Circle, Wedge, HeatmapTri and Contour by 2D
// axes and planes. Append new kinds to keep values stable.
enum class PlotKind { Line, Scatter, Bar, Heatmap, ScatterZ, Bar3D, Surface, Scatter3D, Line3D,
                      SurfaceTri, Text, AxLine, AxSpan, Fill, Quiver, Poly, Circle, Wedge, HeatmapTri,
                      Contour };

// A plot object's (or a plane's) identity, the public ObjectId
// (sextant/object_id.h): kept by set_*_data(), unchanged by
// removing or reordering its siblings, never reused in a process. Edits carry
// it beside their index (figure_edits.h), so an edit follows its object rather
// than its position. 0 = none: objects of hand-built snapshots, and edits made
// without a drawn snapshot, which are addressed by index alone.

// What an edit carries when the snapshot it was made over had nothing at its
// index: it matches no object, so the edit drops.
inline constexpr ObjectId kNoObject = ~ObjectId{0};

inline ObjectId next_object_id() {
    static std::atomic<ObjectId> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

// Bulk data below is CowVec so snapshot copies share buffers (see cow_vec.h).
// Every plot object carries a `data_stamp`, as TitleStamps does for titles:
// plotting it and each set_*_data() take a fresh next_snapshot_generation(), so
// a Data-panel op recorded over an older copy is dropped rather than replayed
// onto the caller's newer data.

// Resolved error-bar distances at one point, each >= 0; 0 = nothing that side.
struct ErrOffsets {
    double lo = 0.0, hi = 0.0;
    bool any() const { return lo > 0.0 || hi > 0.0; }
};

// The caller's ErrorBar, stored as given. The accessors resolve it: one end
// given reads as symmetric, negatives as magnitudes, non-finite as 0.
struct ErrorBarData {
    CowVec<double> x_cap_lo, x_cap_hi, x_box_lo, x_box_hi;
    CowVec<double> y_cap_lo, y_cap_hi, y_box_lo, y_box_hi;

    bool has_x_cap() const { return !x_cap_lo.empty() || !x_cap_hi.empty(); }
    bool has_y_cap() const { return !y_cap_lo.empty() || !y_cap_hi.empty(); }
    bool has_x_box() const { return !x_box_lo.empty() || !x_box_hi.empty(); }
    bool has_y_box() const { return !y_box_lo.empty() || !y_box_hi.empty(); }
    bool empty() const {
        return !has_x_cap() && !has_y_cap() && !has_x_box() && !has_y_box();
    }

    ErrOffsets x_cap(std::size_t i) const { return pair(x_cap_lo, x_cap_hi, i); }
    ErrOffsets y_cap(std::size_t i) const { return pair(y_cap_lo, y_cap_hi, i); }
    ErrOffsets x_box(std::size_t i) const { return pair(x_box_lo, x_box_hi, i); }
    ErrOffsets y_box(std::size_t i) const { return pair(y_box_lo, y_box_hi, i); }

private:
    static ErrOffsets pair(const CowVec<double>& lo, const CowVec<double>& hi,
                           std::size_t i) {
        return { mag(lo.empty() ? hi : lo, i), mag(hi.empty() ? lo : hi, i) };
    }
    static double mag(const CowVec<double>& v, std::size_t i) {
        return i < v.size() && std::isfinite(v[i]) ? std::fabs(v[i]) : 0.0;
    }
};

// The caller's ErrorBar3D, stored as given and resolved like ErrorBarData.
// Indexed by axis (0 = x, 1 = y, 2 = z).
struct ErrorBar3DData {
    CowVec<double> cap_lo[3], cap_hi[3], box_lo[3], box_hi[3];

    bool has_cap(int a) const { return !cap_lo[a].empty() || !cap_hi[a].empty(); }
    bool has_box(int a) const { return !box_lo[a].empty() || !box_hi[a].empty(); }
    bool any_box() const { return has_box(0) || has_box(1) || has_box(2); }
    bool empty() const {
        for (int a = 0; a < 3; ++a)
            if (has_cap(a) || has_box(a)) return false;
        return true;
    }

    ErrOffsets cap(int a, std::size_t i) const { return pair(cap_lo[a], cap_hi[a], i); }
    ErrOffsets box(int a, std::size_t i) const { return pair(box_lo[a], box_hi[a], i); }

private:
    static ErrOffsets pair(const CowVec<double>& lo, const CowVec<double>& hi,
                           std::size_t i) {
        return { mag(lo.empty() ? hi : lo, i), mag(hi.empty() ? lo : hi, i) };
    }
    static double mag(const CowVec<double>& v, std::size_t i) {
        return i < v.size() && std::isfinite(v[i]) ? std::fabs(v[i]) : 0.0;
    }
};

// Every plot object below with a `color` option holds it set, so drawing and the
// panels dereference it: a plotting call fills it from the color cycle, and an
// object built here (tests, internal copies) starts with the old fixed default.
struct LinePlot {
    CowVec<double> x, y;
    ErrorBarData   err;
    LineOptions    opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    std::size_t count() const { return x.size(); }

    // Drawn segments: `loop` adds a closing one; fewer than two points = none.
    std::size_t segment_count() const {
        if (count() < 2) return 0;
        return opts.loop ? count() : count() - 1;
    }

    // The point indices segment `s` joins; the closing segment wraps to 0.
    void segment_ends(std::size_t s, std::size_t& a, std::size_t& b) const {
        a = s;
        b = (s + 1 == count()) ? 0 : s + 1;
    }

    // set_<kind>_visible()'s next_snapshot_generation(), 0 = never called; a
    // panel style edit made over an older one keeps `opts.visible` (see
    // assign_plot_opts()). Last, so the plotting calls' positional braces
    // leave it 0.
    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;
};

struct ScatterPlot {
    CowVec<double> x, y;
    ErrorBarData   err;
    ScatterOptions opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;
    // Per-point diameters (ScatterOptions::sizes, taken at the call); empty =
    // opts.size for all. See marker_size_at().
    CowVec<double> sizes;

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;
};

// bar_width is in data units, already resolved (spacing x width fraction for
// bar(), bin width x fraction for hist()).
struct BarPlot {
    CowVec<double> centers;
    CowVec<double> heights;
    double         bar_width = 1.0;
    ErrorBarData   err;
    BarOptions     opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;
    // Each bar's base (BarOptions::bottom, taken at the call and spread to one
    // per bar); empty = 0 for all. See base_at().
    CowVec<double> bottom;

    bool horizontal() const { return opts.orientation == Orientation::Horizontal; }
    double base_at(std::size_t i) const { return i < bottom.size() ? bottom[i] : 0.0; }
    double tip_at(std::size_t i) const { return base_at(i) + heights[i]; }

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;
};

// The bars' tips (base + height), as a column: `heights` itself without bases.
inline CowVec<double> bar_tips(const BarPlot& p) {
    if (p.bottom.empty()) return p.heights;
    std::vector<double> t(p.heights.size());
    for (std::size_t i = 0; i < t.size(); ++i) t[i] = p.tip_at(i);
    return t;
}

// Two columns of point coordinates (shared, not copied).
struct XYColumns {
    CowVec<double> x, y;
};

// Where a bar's error bars hang and its hover point sits: its tip, at (center,
// tip) standing or (tip, center) lying.
inline XYColumns bar_anchors(const BarPlot& p) {
    CowVec<double> tips = bar_tips(p);
    if (p.horizontal()) return {std::move(tips), p.centers};
    return {p.centers, std::move(tips)};
}

// A bar's outline color: `edgecolor` faded by `alpha`.
inline Color bar_edge(const BarPlot& p) {
    Color c = p.opts.edgecolor;
    c.a *= std::clamp(p.opts.alpha, 0.0f, 1.0f);
    return c;
}

// Row-major rows x cols values, mapped through vmin/vmax and the colormap. The
// grid spans xrange x yrange uniformly (ranges are outer edges), or, for a
// heatmap_rec(), runs through x_edges / y_edges; use the accessors below for
// cell positions. Rows count from yrange.lo; the origin flip is applied where
// data is read (a mesh is always "lower").
struct HeatmapPlot {
    CowVec<float>  data;
    int            rows = 0, cols = 0;
    Range          xrange{ 0.0, 1.0 }, yrange{ 0.0, 1.0 };
    HeatmapOptions opts;
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    // An even grid's cell size; a mesh's average.
    double cell_w() const { return cols > 0 ? (xrange.hi - xrange.lo) / cols : 0.0; }
    double cell_h() const { return rows > 0 ? (yrange.hi - yrange.lo) / rows : 0.0; }

    // Cell index (fractional; a centre is j + 0.5) -> data space. A mesh is
    // linear within each cell.
    double x_at(double col) const {
        return x_edges.empty() ? xrange.lo + col * cell_w() : edge_at(x_edges, col);
    }
    double y_at(double row) const {
        return y_edges.empty() ? yrange.lo + row * cell_h() : edge_at(y_edges, row);
    }

    // Edge k exactly (0..cols, 0..rows): the mesh's own, or the range's ends.
    double x_edge(int k) const {
        if (!x_edges.empty()) return x_edges[static_cast<std::size_t>(k)];
        return k == cols ? xrange.hi : xrange.lo + k * cell_w();
    }
    double y_edge(int k) const {
        if (!y_edges.empty()) return y_edges[static_cast<std::size_t>(k)];
        return k == rows ? yrange.hi : yrange.lo + k * cell_h();
    }

    // Data space -> fractional cell index. A degenerate range returns 0.
    double col_at(double x) const {
        if (!x_edges.empty()) return index_at(x_edges, x);
        const double w = xrange.hi - xrange.lo;
        return w != 0.0 ? (x - xrange.lo) / w * cols : 0.0;
    }
    double row_at(double y) const {
        if (!y_edges.empty()) return index_at(y_edges, y);
        const double h = yrange.hi - yrange.lo;
        return h != 0.0 ? (y - yrange.lo) / h * rows : 0.0;
    }

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;

    // 1: `data` is rows x cols values through opts.cmap. 3 or 4: an
    // imshow_rgb() image, rows x cols x channels colours in [0, 1]; the
    // colormap, colorbar and contours of opts are unused.
    int channels = 1;
    bool colormapped() const { return channels == 1; }

    // A heatmap_rec(): cols + 1 and rows + 1 edges, strictly monotone; empty
    // for an even grid. xrange / yrange are their first and last.
    CowVec<double> x_edges, y_edges;
    bool mesh() const { return !x_edges.empty(); }

private:
    // Cell f's edge position, extrapolating past either end.
    static double edge_at(const CowVec<double>& e, double f) {
        const std::size_t n = e.size() - 1;
        const double i = std::clamp(std::floor(f), 0.0, static_cast<double>(n - 1));
        const std::size_t k = static_cast<std::size_t>(i);
        return e[k] + (f - i) * (e[k + 1] - e[k]);
    }
    // edge_at()'s inverse, for either direction of the edges.
    static double index_at(const CowVec<double>& e, double v) {
        const std::size_t n = e.size() - 1;
        const auto it = e[n] > e[0] ? std::upper_bound(e.begin(), e.end(), v)
                                    : std::upper_bound(e.begin(), e.end(), v, std::greater<double>());
        const std::size_t k = std::min<std::size_t>(
            static_cast<std::size_t>(std::max<std::ptrdiff_t>(it - e.begin() - 1, 0)), n - 1);
        return static_cast<double>(k) + (v - e[k]) / (e[k + 1] - e[k]);
    }
};

// Continuous-color scatter: z is mapped through opts.cmap/vmin/vmax.
struct ScatterZPlot {
    CowVec<double>  x, y, z;
    ErrorBarData    err;
    ScatterZOptions opts;
    unsigned long long data_stamp = 0;
    ObjectId id = 0;
    CowVec<double>  sizes;   // as ScatterPlot::sizes

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;
};

// True when scatter_z point i has no colour under its norm (z <= 0 under
// log, outside the boundaries): hidden as a point with no size is. Other
// kinds have no norm.
template <class P>
bool marker_norm_masked(const P& p, std::size_t i) {
    if constexpr (requires { p.z; p.opts.norm; }) {
        const ColorNorm::Kind kind = norm_in_force(p.opts.norm, p.opts.vmin, p.opts.vmax);
        return kind != ColorNorm::Kind::Linear && i < p.z.size() && norm_masks(kind, p.opts.norm, p.z[i]);
    } else {
        return false;
    }
}

// A marker's pixel diameter: its own from `sizes` when the series has them,
// else the series' `size`. A per-point size that is NaN or <= 0 hides that
// point (not drawn, not hovered): 0 is returned, which the GPU draws as nothing.
// So does a scatter_z point its norm leaves without a colour.
template <class P>
float marker_size_at(const P& p, std::size_t i) {
    if (marker_norm_masked(p, i)) return 0.0f;
    if (p.sizes.empty()) return p.opts.size;
    const float s = i < p.sizes.size() ? static_cast<float>(p.sizes[i]) : 0.0f;
    return s > 0.0f ? s : 0.0f;
}

// True when point i is hidden by its own size (see marker_size_at()) or its
// norm. A series without `sizes` hides nothing by size, whatever its `size`.
template <class P>
bool marker_hidden(const P& p, std::size_t i) {
    return (!p.sizes.empty() && !(i < p.sizes.size() && static_cast<float>(p.sizes[i]) > 0.0f))
           || marker_norm_masked(p, i);
}

// Box axis index (into x, y, z) of each grid direction, for every consumer to
// agree on.
struct Axis3Map { int u = 0, v = 1, h = 2; };

inline Axis3Map axis_map(PlaneOrientation o) {
    switch (o) {
        case PlaneOrientation::YZ: return { 1, 2, 0 };
        case PlaneOrientation::ZX: return { 2, 0, 1 };
        case PlaneOrientation::XY: break;
    }
    return { 0, 1, 2 };
}

// Bars on a u x v grid; bar (i, j) is heights[i * v.size() + j]. u_width/v_width
// are resolved data-space footprints. `bottoms` is empty or indexed like
// `heights`.
struct Bar3DPlot {
    CowVec<double> u, v;
    CowVec<double> heights;
    CowVec<double> bottoms;
    double         u_width = 1.0, v_width = 1.0;
    PlaneOrientation orient = PlaneOrientation::XY;
    Bar3DOptions   opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    std::size_t count() const { return u.size() * v.size(); }
    std::size_t index_of(std::size_t i, std::size_t j) const { return i * v.size() + j; }

    double height_at(std::size_t k) const { return k < heights.size() ? heights[k] : 0.0; }
    double bottom_at(std::size_t k) const {
        return k < bottoms.size() ? bottoms[k] : opts.bottom;
    }

    // Extent along the standing axis, low end first (negative heights hang down).
    double h_lo(std::size_t k) const { return std::min(bottom_at(k), bottom_at(k) + height_at(k)); }
    double h_hi(std::size_t k) const { return std::max(bottom_at(k), bottom_at(k) + height_at(k)); }

    unsigned long long visible_stamp = 0;
};

// A surface over a u x v grid, laid out like Bar3DPlot. Draws the
// (|u|-1) x (|v|-1) cells between samples; cell (i, j) spans samples (i, j)
// to (i+1, j+1).
struct SurfacePlot {
    CowVec<double> u, v;
    CowVec<double> heights;
    PlaneOrientation orient = PlaneOrientation::XY;
    SurfaceOptions opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    std::size_t count() const { return u.size() * v.size(); }
    std::size_t index_of(std::size_t i, std::size_t j) const { return i * v.size() + j; }
    double height_at(std::size_t k) const { return k < heights.size() ? heights[k] : 0.0; }

    std::size_t cell_rows() const { return u.size() > 1 ? u.size() - 1 : 0; }
    std::size_t cell_cols() const { return v.size() > 1 ? v.size() - 1 : 0; }
    std::size_t cell_count() const { return cell_rows() * cell_cols(); }

    unsigned long long visible_stamp = 0;
};

// Colormap range for a surface: opts.vmin/vmax, or the data range when equal.
// Shared by the raster path, SVG path and colorbar.
inline void surface_value_range(const SurfacePlot& s, double& vmin, double& vmax) {
    if (s.opts.vmin != s.opts.vmax) {
        vmin = s.opts.vmin;
        vmax = s.opts.vmax;
        return;
    }
    double lo =  std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();
    for (std::size_t k = 0; k < s.count() && k < s.heights.size(); ++k) {
        lo = std::min(lo, s.heights[k]);
        hi = std::max(hi, s.heights[k]);
    }
    if (lo > hi) { lo = 0.0; hi = 1.0; }   // nothing to take a range from
    vmin = lo;
    vmax = hi;
}

// A sheet on a triangulated mesh: vertices plus `tri` (three indices per
// triangle, row-major). Delaunay output from ingest is stored here too. Indices
// are never uploaded (the mesh is expanded per face). `colors` is per vertex
// and empty for a flat mesh.
struct SurfaceTriPlot {
    CowVec<double> x, y, z;
    CowVec<std::uint32_t> tri;
    CowVec<double> colors;
    SurfaceTriOptions opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    std::size_t count() const { return x.size(); }               // vertices
    std::size_t face_count() const { return tri.size() / 3; }
    bool colormapped() const { return !colors.empty(); }
    double color_at(std::size_t i) const { return i < colors.size() ? colors[i] : 0.0; }

    // The vertex indices of face `f`; all consumers use this. Out-of-range
    // indices read as 0 (guard for hand-built snapshots).
    void face_verts(std::size_t f, std::size_t& a, std::size_t& b,
                    std::size_t& c) const {
        const std::size_t k = f * 3;
        const std::size_t n = count();
        auto at = [&](std::size_t i) -> std::size_t {
            if (i >= tri.size()) return 0;
            const std::size_t v = tri[i];
            return v < n ? v : 0;
        };
        a = at(k); b = at(k + 1); c = at(k + 2);
    }

    // Data-space position of vertex `i`.
    Vec3 vertex(std::size_t i) const {
        return { i < x.size() ? x[i] : 0.0,
                 i < y.size() ? y[i] : 0.0,
                 i < z.size() ? z[i] : 0.0 };
    }

    unsigned long long visible_stamp = 0;
};

// Colormap range for a mesh's `colors`, as surface_value_range().
inline void surface_tri_value_range(const SurfaceTriPlot& s, double& vmin, double& vmax) {
    if (s.opts.vmin != s.opts.vmax) {
        vmin = s.opts.vmin;
        vmax = s.opts.vmax;
        return;
    }
    double lo =  std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < s.colors.size(); ++i) {
        lo = std::min(lo, s.colors[i]);
        hi = std::max(hi, s.colors[i]);
    }
    if (lo > hi) { lo = 0.0; hi = 1.0; }   // nothing to take a range from
    vmin = lo;
    vmax = hi;
}

// Markers at points in the scene. `colors` is empty for a flat series.
struct Scatter3DPlot {
    CowVec<double> x, y, z;
    CowVec<double> colors;
    ErrorBar3DData err;
    Scatter3DOptions opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;
    CowVec<double> sizes;    // as ScatterPlot::sizes

    std::size_t count() const { return x.size(); }
    bool colormapped() const { return !colors.empty(); }
    double color_at(std::size_t i) const { return i < colors.size() ? colors[i] : 0.0; }

    unsigned long long visible_stamp = 0;
};

// Colormap range for a scatter3d's `colors`, as surface_value_range().
inline void scatter3d_value_range(const Scatter3DPlot& s, double& vmin, double& vmax) {
    if (s.opts.vmin != s.opts.vmax) {
        vmin = s.opts.vmin;
        vmax = s.opts.vmax;
        return;
    }
    double lo =  std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < s.colors.size(); ++i) {
        lo = std::min(lo, s.colors[i]);
        hi = std::max(hi, s.colors[i]);
    }
    if (lo > hi) { lo = 0.0; hi = 1.0; }   // nothing to take a range from
    vmin = lo;
    vmax = hi;
}

// A path through the scene: data like Scatter3DPlot, drawn as the segments
// between points. `colors` belongs to points; a segment ramps between its ends.
struct Line3DPlot {
    CowVec<double> x, y, z;
    CowVec<double> colors;
    ErrorBar3DData err;
    Line3DOptions opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    std::size_t count() const { return x.size(); }
    bool colormapped() const { return !colors.empty(); }
    double color_at(std::size_t i) const { return i < colors.size() ? colors[i] : 0.0; }

    // Drawn segments: `loop` adds a closing one; fewer than two points = none.
    std::size_t segment_count() const {
        if (count() < 2) return 0;
        return opts.loop ? count() : count() - 1;
    }

    // The point indices segment `s` joins; the closing segment wraps to 0.
    // All consumers use this.
    void segment_ends(std::size_t s, std::size_t& a, std::size_t& b) const {
        a = s;
        b = (s + 1 == count()) ? 0 : s + 1;
    }

    unsigned long long visible_stamp = 0;
};

// Colormap range for a line3d's `colors`, as surface_value_range().
inline void line3d_value_range(const Line3DPlot& l, double& vmin, double& vmax) {
    if (l.opts.vmin != l.opts.vmax) {
        vmin = l.opts.vmin;
        vmax = l.opts.vmax;
        return;
    }
    double lo =  std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < l.colors.size(); ++i) {
        lo = std::min(lo, l.colors[i]);
        hi = std::max(hi, l.colors[i]);
    }
    if (lo > hi) { lo = 0.0; hi = 1.0; }
    vmin = lo;
    vmax = hi;
}

// A text's string and placement, the part a Data-panel op replaces.
//   2D:  (x, y) the text; with `arrow`, (px, py) the point it points at (data).
//   3D:  (x, y, z) the data point it is drawn at (x and y in Coords::Data), or
//        with x and y frame fractions a text2d() (z unused); with `arrow`,
//        (x, y, z) is the point and the text sits (dx, dy) pixels from it, y up.
struct TextContent {
    std::string text;
    Pos x, y;
    double z = 0.0;
    bool arrow = false;
    double px = 0.0, py = 0.0;
    double dx = 0.0, dy = 0.0;
};

// A text's appearance, the part a style edit replaces (ArrowOptions is read
// only with `arrow`).
struct TextStyle {
    TextOptions  text;
    ArrowOptions arrow;
};

struct TextPlot {
    TextContent content;
    TextStyle   opts;
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    unsigned long long visible_stamp = 0;
};

// Axes::axline(): `value` on the axis the orientation names (y for Horizontal).
struct AxLinePlot {
    double        value = 0.0;
    AxLineOptions opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    // Whether `value` is an x value (a Vertical line).
    bool on_x() const { return opts.orientation == Orientation::Vertical; }

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;

    // A sloped line (mode != Value): `value`, `orientation`, `from` and `to` are
    // unused. PointSlope reads x, y and slope; TwoPoints x, y, x2 and y2.
    AxLineMode mode = AxLineMode::Value;
    Pos x, y, x2, y2;
    double slope = 0.0;
    bool sloped() const { return mode != AxLineMode::Value; }
};

// Axes::axspan(): lo..hi on the axis the orientation names, as AxLinePlot.
struct AxSpanPlot {
    double        lo = 0.0, hi = 1.0;
    AxSpanOptions opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    bool on_x() const { return opts.orientation == Orientation::Vertical; }

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;
};

// Axes::fill(): the area between y1 and y2 along x, one value of each
// per point. `x` is the run coordinate, on the y axis when the fill is
// Vertical (and y1/y2 then on x). fill_geometry.h turns it into regions.
struct FillPlot {
    CowVec<double> x, y1, y2;
    FillOptions    opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;
    // FillOptions::exclude, taken at the call: one per point, nonzero = left
    // out (a column of numbers, so the Data panel edits it as the others);
    // empty = none.
    CowVec<double> exclude;

    bool vertical() const { return opts.orientation == Orientation::Vertical; }
    bool excluded(std::size_t i) const { return i < exclude.size() && exclude[i] != 0.0; }

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;
};

// Axes::quiver(): one arrow per point, from (x, y) along (u, v). `opts.amp`
// is always set (resolved at the call). quiver_geometry.h places the arrows.
struct QuiverPlot {
    CowVec<double> x, y, u, v;
    QuiverOptions  opts{.color = Color::Blue, .amp = 1.0};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;
    // The values the arrows' colors map (quiver(x, y, u, v, c)); empty = flat.
    CowVec<double> c;

    std::size_t count() const { return std::min({x.size(), y.size(), u.size(), v.size()}); }
    double amp() const { return opts.amp.value_or(1.0); }
    bool colormapped() const { return !c.empty(); }

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;
};

// Axes::shape_poly(): the vertices in `opts.space`; shape_geometry.h turns it
// into triangles.
// The last ear clipping of a polygon (shape_geometry.h poly_geometry()), shared
// by every copy of the plot (snapshots, hover, export). It is trusted only when
// the vertices and `key` match the call exactly, so it never needs invalidating.
struct PolyTriCache {
    std::mutex m;
    std::vector<double> key;           // the space and view the clipping depends on
    std::vector<double> raw;           // x then y it was cut from
    std::vector<std::uint32_t> kept;   // raw index of each vertex after dropping repeats
    std::vector<std::uint32_t> tris;   // three kept-vertex indices per triangle
};

struct PolyPlot {
    CowVec<double> x, y;
    PolyOptions    opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;
    std::shared_ptr<PolyTriCache> tri = std::make_shared<PolyTriCache>();
};

// Axes::shape_circle(): centre and radius, each with its own space.
struct CirclePlot {
    Pos cx, cy, r;
    CircleOptions opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;
};

// Axes::shape_wedge(): a circle's numbers, the sweep (degrees, counter-clockwise
// from +x) and the hole's radius in r's space.
struct WedgePlot {
    Pos cx, cy, r;
    double theta1 = 0.0, theta2 = 90.0, inner = 0.0;
    WedgeOptions opts{.color = Color::Blue};
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;
};

// Axes::heatmap_tri(): one value per vertex over triangles (three vertex
// indices each into x / y / values), coloured per pixel through opts.cmap.
struct HeatmapTriPlot {
    CowVec<double> x, y, values;
    CowVec<std::uint32_t> tri;
    HeatmapTriOptions opts;
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;

    // Under a boundary norm, each band's colormap position instead of the
    // norm's own spread (NaN: the band is not drawn). Set only on a contourf's
    // fill (ContourGeometry); empty for a heatmap_tri.
    std::vector<double> band_t;
};

// A contour's derived drawing data (contour_plot.h): its triangles, resolved
// levels and colours, and traced lines.
struct ContourGeometry;

// Axes::contour() / contourf(): z over a grid of points, x one per column and
// y one per row. `geom` is rebuilt by contour_rebuild() whenever the data or
// the options change (ingest, set_contour_data(), a style edit).
struct ContourPlot {
    CowVec<double> x, y, z;
    ContourOptions opts;
    unsigned long long data_stamp = 0;
    ObjectId id = 0;

    unsigned long long visible_stamp = 0;
    unsigned long long zorder_stamp = 0;

    std::shared_ptr<const ContourGeometry> geom;

    int rows() const { return static_cast<int>(y.size()); }
    int cols() const { return static_cast<int>(x.size()); }
};

// Derives `p.geom` from its data and options (contour_plot.cpp).
void contour_rebuild(ContourPlot& p);

// The reference kinds, fills and quivers are optional so a view built over the
// first five alone (tests) still reads as before.
struct AllPlotData {
    const std::vector<LinePlot>&     lines;
    const std::vector<ScatterPlot>&  scatters;
    const std::vector<BarPlot>&      bars;
    const std::vector<HeatmapPlot>&  heatmaps;
    const std::vector<ScatterZPlot>& scatter_z;
    const std::vector<AxLinePlot>*   axlines = nullptr;
    const std::vector<AxSpanPlot>*   axspans = nullptr;
    const std::vector<FillPlot>*     fills = nullptr;
    const std::vector<QuiverPlot>*   quivers = nullptr;
    const std::vector<PolyPlot>*     polys = nullptr;
    const std::vector<CirclePlot>*   circles = nullptr;
    const std::vector<WedgePlot>*    wedges = nullptr;
    const std::vector<HeatmapTriPlot>* heatmap_tris = nullptr;
    const std::vector<ContourPlot>*    contours = nullptr;
};

// Caller-side stamps of an axes' titles: each set_*title() takes a fresh
// next_snapshot_generation(), 0 = never set (or cleared by cla()). A panel
// title edit carries the stamps of the snapshot it was typed over and is
// applied only to a title not set since, so a stale edit never overwrites a
// newer set_*title().
struct TitleStamps {
    unsigned long long title = 0, xtitle = 0, ytitle = 0, ztitle = 0;

    // For edits built without a snapshot (tests): applies over anything.
    static constexpr TitleStamps any() {
        constexpr auto m = ~0ull;
        return { m, m, m, m };
    }
};

// The same, for the axis limits: set_xlim()/set_ylim()/set_zlim() stamp their
// axis, cla() resets. Pan/zoom and the Limits fields record the stamps of the
// snapshot they worked over, so the view survives refresh() until the program
// sets that axis itself.
struct LimitStamps {
    unsigned long long x = 0, y = 0, z = 0;

    static constexpr LimitStamps any() {
        constexpr auto m = ~0ull;
        return { m, m, m };
    }
};

// The same, for the setter groups that have no stamp of their own: each setter
// stamps its group (grid() -> grid, set_axes_style() -> style, ...), cla()
// resets them. An edit addressed to a plot object needs none: it carries the
// object's ObjectId, which a cla() and re-plot never hands to another object.
struct StyleStamps {
    unsigned long long style = 0, grid = 0, legend = 0, colorbar = 0;
    unsigned long long xticks = 0, yticks = 0, zticks = 0;
    unsigned long long box = 0;     // 3D only
    unsigned long long aspect = 0;  // 3D set_box_aspect(), 2D set_aspect()

    static constexpr StyleStamps any() {
        constexpr auto m = ~0ull;
        return { m, m, m, m, m, m, m, m, m };
    }
};

// Figure-level: suptitle() -> suptitle (its text), set_suptitle_style() and
// suptitle()'s font size -> suptitle_style, set_margins() -> margins,
// set_background() -> background.
struct FigureStamps {
    unsigned long long suptitle = 0, suptitle_style = 0, margins = 0, background = 0;

    static constexpr FigureStamps any() {
        constexpr auto m = ~0ull;
        return { m, m, m, m };
    }
};

// Axes::set_aspect(): y units drawn `ratio` times as long as x units, 0 = auto.
struct AxesAspect {
    double ratio = 0.0;
    Adjustable adjustable = Adjustable::Box;

    // Whether layout widens limits for it (Adjustable::Datalim with a ratio).
    bool datalim() const { return ratio > 0.0 && adjustable == Adjustable::Datalim; }
    bool operator==(const AxesAspect&) const = default;
};

// Everything the render thread needs from Axes::Impl: decoration and limits
// copied, plot data shared via CowVec. Built by Axes::Impl::build_snapshot().
struct RenderSnapshot {
    std::vector<LinePlot>     lines;
    std::vector<ScatterPlot>  scatters;
    std::vector<BarPlot>      bars;
    std::vector<HeatmapPlot>  heatmaps;
    std::vector<ScatterZPlot> scatter_z;
    // A plane's sheet holds none (planes have no text API).
    std::vector<TextPlot>     texts;
    // Nor reference lines and spans (Axes::axline()/axspan()).
    std::vector<AxLinePlot>   axlines;
    std::vector<AxSpanPlot>   axspans;
    // Axes::fill(), on axes and planes alike.
    std::vector<FillPlot>     fills;
    // Axes::quiver(), likewise.
    std::vector<QuiverPlot>   quivers;
    // Axes::shape_poly(), shape_circle(), shape_wedge(), likewise.
    std::vector<PolyPlot>     polys;
    std::vector<CirclePlot>   circles;
    std::vector<WedgePlot>    wedges;
    // Axes::heatmap_tri(), likewise.
    std::vector<HeatmapTriPlot> heatmap_tris;
    // Axes::contour() / contourf(), likewise.
    std::vector<ContourPlot> contours;

    // Font sizes live in axes_style.
    std::string title, xtitle, ytitle;
    TitleStamps title_stamps;

    bool          grid_enabled   = false;
    GridOptions   grid_opts;
    bool          legend_enabled = false;
    LegendOptions   legend_opts;
    ColorbarOptions colorbar_opts;
    AxesStyle     axes_style;

    double xmin = 0, xmax = 1, ymin = 0, ymax = 1;
    bool   xlim_auto = true, ylim_auto = true;
    LimitStamps limit_stamps;
    StyleStamps style_stamps;

    // Axes::set_xscale()/set_yscale(); a plane's sheet keeps Linear.
    AxisScale xscale, yscale;
    // Axes::sharex()/sharey(): the share group's id, 0 = not shared. Members
    // hold the same limits, limit stamp and scale (copy_shared_x/y()).
    unsigned long long xshare = 0, yshare = 0;
    // Axes::set_xtick_format()/set_ytick_format(); empty = the scale's default.
    std::string xtick_format, ytick_format;
    // Axes::set_aspect(); a twin's is never set (layout reads the cell's own).
    AxesAspect aspect;

    // Explicit ticks; absent = generate_ticks().
    std::optional<std::vector<Tick>> xticks_override, yticks_override;

    AllPlotData all() const {
        return { lines, scatters, bars, heatmaps, scatter_z, &axlines, &axspans, &fills, &quivers, &polys, &circles, &wedges, &heatmap_tris, &contours };
    }
};

// What a share group holds in common, copied from one member to another;
// written once for Axes::Impl and RenderSnapshot (the same field names).
template <class D, class S>
void copy_shared_x(D& d, const S& s) {
    d.xmin = s.xmin; d.xmax = s.xmax; d.xlim_auto = s.xlim_auto;
    d.limit_stamps.x = s.limit_stamps.x;
    d.xscale = s.xscale;
}
template <class D, class S>
void copy_shared_y(D& d, const S& s) {
    d.ymin = s.ymin; d.ymax = s.ymax; d.ylim_auto = s.ylim_auto;
    d.limit_stamps.y = s.limit_stamps.y;
    d.yscale = s.yscale;
}

// One 2D plane in a 3D scene. `sheet` is the Plane2D's Axes::Impl snapshot;
// only its plot vectors and colorbar/legend options are read (limits, ticks and
// title are the parent's).
struct PlaneSnapshot {
    PlaneOrientation orient = PlaneOrientation::XY;
    // Position along the plane's normal axis, in data units.
    double           offset = 0.0;
    Plane2DOptions   opts;
    // Set when the plane is made and by set_offset()/set_alpha(); a plane edit
    // records it (as StyleStamps).
    unsigned long long placement_stamp = 0;
    // The plane's identity (see ObjectId); plane-addressed edits carry it.
    ObjectId id = 0;
    // Bumped when a Data-panel style edit patches `sheet` on the render thread
    // (which leaves data_generation alone); the plane's raster keys on it.
    unsigned long long style_generation = 0;
    RenderSnapshot   sheet;
};

// Everything the render thread needs from Axes3D::Impl. 2D kinds appear only
// via `planes`.
struct RenderSnapshot3D {
    std::vector<Bar3DPlot> bars3d;
    std::vector<SurfacePlot> surfaces;
    std::vector<Scatter3DPlot> scatter3d;
    std::vector<Line3DPlot> lines3d;
    std::vector<SurfaceTriPlot> surface_tri;
    std::vector<TextPlot> texts;
    // In insertion order; draw order is resolved per frame, this only breaks ties.
    std::vector<PlaneSnapshot> planes;

    // Font sizes live in axes_style.
    std::string title, xtitle, ytitle, ztitle;
    TitleStamps title_stamps;

    // On by default, unlike 2D.
    bool        grid_enabled = true;
    GridOptions grid_opts;

    // Legend and colorbar belong to the cell (drawn beside the frame, not in
    // the scene). The legend is enabled per axes; colorbars are requested by
    // individual plot objects, including those on planes. Styling is the axes'.
    bool            legend_enabled = false;
    LegendOptions   legend_opts;
    ColorbarOptions colorbar_opts;

    AxesStyle   axes_style;
    Box3DStyle  box_style;
    BoxAspect   aspect;
    Camera3D    camera;
    // As TitleStamps, for the camera: set_camera()/set_view()/set_projection()/
    // set_fov() take a fresh stamp, cla() resets it to 0.
    unsigned long long camera_stamp = 0;
    // set_default_camera()'s camera; when `default_camera_set`, Home's camera (the
    // double-click and Edit > Home), else Home is the program's camera.
    Camera3D    default_camera;
    bool        default_camera_set = false;

    double xmin = 0, xmax = 1, ymin = 0, ymax = 1, zmin = 0, zmax = 1;
    bool   xlim_auto = true, ylim_auto = true, zlim_auto = true;
    LimitStamps limit_stamps;
    StyleStamps style_stamps;

    std::optional<std::vector<Tick>> xticks_override, yticks_override, zticks_override;

    // Plane access matching Axes3D::Impl's, so one apply_axes3d_edit() and
    // apply_plot_data_ops() serve both sides of the thread boundary.
    std::size_t     plane_count() const              { return planes.size(); }
    PlaneSnapshot&  plane_at(std::size_t i)          { return planes[i]; }
    const PlaneSnapshot& plane_at(std::size_t i) const { return planes[i]; }
};

// Where an object is: its kind and its index within that kind's vector.
struct ObjectRef {
    PlotKind    kind  = PlotKind::Line;
    std::size_t index = 0;
};

// The public name of a location: PickKind has no Text, AxLine or AxSpan, so
// those give nothing.
inline std::optional<ObjectLocation> to_location(const std::optional<ObjectRef>& r) {
    if (!r) return std::nullopt;
    PickKind k = PickKind::None;
    switch (r->kind) {
        case PlotKind::Line:      k = PickKind::Line;      break;
        case PlotKind::Scatter:   k = PickKind::Scatter;   break;
        case PlotKind::Bar:       k = PickKind::Bar;       break;
        case PlotKind::Heatmap:   k = PickKind::Heatmap;   break;
        case PlotKind::ScatterZ:  k = PickKind::ScatterZ;  break;
        case PlotKind::Bar3D:     k = PickKind::Bar3D;     break;
        case PlotKind::Surface:   k = PickKind::Surface;   break;
        case PlotKind::Scatter3D: k = PickKind::Scatter3D; break;
        case PlotKind::Line3D:    k = PickKind::Line3D;    break;
        case PlotKind::SurfaceTri:k = PickKind::SurfaceTri;break;
        case PlotKind::Fill:      k = PickKind::Fill;      break;
        case PlotKind::Quiver:    k = PickKind::Quiver;    break;
        case PlotKind::Poly:      k = PickKind::Poly;      break;
        case PlotKind::Circle:    k = PickKind::Circle;    break;
        case PlotKind::Wedge:     k = PickKind::Wedge;     break;
        case PlotKind::HeatmapTri:k = PickKind::HeatmapTri;break;
        case PlotKind::Contour:   k = PickKind::Contour;   break;
        case PlotKind::Text: case PlotKind::AxLine: case PlotKind::AxSpan: return std::nullopt;
    }
    return ObjectLocation{ k, r->index };
}

inline constexpr PlotKind kPlotKinds2D[] = { PlotKind::Line, PlotKind::Scatter, PlotKind::Bar,
                                             PlotKind::Heatmap, PlotKind::ScatterZ, PlotKind::Text,
                                             PlotKind::AxLine, PlotKind::AxSpan, PlotKind::Fill,
                                             PlotKind::Quiver, PlotKind::Poly, PlotKind::Circle,
                                             PlotKind::Wedge, PlotKind::HeatmapTri, PlotKind::Contour };
inline constexpr PlotKind kPlotKinds3D[] = { PlotKind::Bar3D, PlotKind::Surface,
                                             PlotKind::Scatter3D, PlotKind::Line3D,
                                             PlotKind::SurfaceTri, PlotKind::Text };

// Calls `f` with the vector holding `kind` in a 2D holder (RenderSnapshot,
// Axes::Impl, a plane's sheet) and returns its result, or `none` for a kind
// the holder has not. `f` returns one type for every vector.
template <class S, class F, class R>
    requires requires(S& s) { s.lines; s.scatter_z; }
R with_kind(S& s, PlotKind kind, F&& f, R none) {
    switch (kind) {
        case PlotKind::Line:     return f(s.lines);
        case PlotKind::Scatter:  return f(s.scatters);
        case PlotKind::Bar:      return f(s.bars);
        case PlotKind::Heatmap:  return f(s.heatmaps);
        case PlotKind::ScatterZ: return f(s.scatter_z);
        case PlotKind::Text:     return f(s.texts);
        case PlotKind::AxLine:   return f(s.axlines);
        case PlotKind::AxSpan:   return f(s.axspans);
        case PlotKind::Fill:     return f(s.fills);
        case PlotKind::Quiver:   return f(s.quivers);
        case PlotKind::Poly:     return f(s.polys);
        case PlotKind::Circle:   return f(s.circles);
        case PlotKind::Wedge:    return f(s.wedges);
        case PlotKind::HeatmapTri: return f(s.heatmap_tris);
        case PlotKind::Contour:  return f(s.contours);
        default:                 return none;
    }
}

// The same for a 3D holder (RenderSnapshot3D, Axes3D::Impl): its own kinds.
template <class S, class F, class R>
    requires requires(S& s) { s.bars3d; s.surface_tri; }
R with_kind(S& s, PlotKind kind, F&& f, R none) {
    switch (kind) {
        case PlotKind::Bar3D:      return f(s.bars3d);
        case PlotKind::Surface:    return f(s.surfaces);
        case PlotKind::Scatter3D:  return f(s.scatter3d);
        case PlotKind::Line3D:     return f(s.lines3d);
        case PlotKind::SurfaceTri: return f(s.surface_tri);
        case PlotKind::Text:       return f(s.texts);
        default:                   return none;
    }
}

namespace objects_detail {
template <class V>
std::optional<std::size_t> index_of(const V& v, ObjectId id) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].id == id) return i;
    return std::nullopt;
}

// Moves v[from] to position `to` (clamped to the last), keeping the others' order.
template <class V>
void move_to(V& v, std::size_t from, std::size_t to) {
    if (from >= v.size()) return;
    const std::size_t dst = std::min(to, v.size() - 1);
    const auto at = [&v](std::size_t i) { return v.begin() + static_cast<std::ptrdiff_t>(i); };
    if (from < dst)      std::rotate(at(from), at(from + 1), at(dst + 1));
    else if (dst < from) std::rotate(at(dst), at(from), at(from + 1));
}
} // namespace objects_detail

// The id of object `index` of `kind`, kNoObject when there is none.
template <class S>
ObjectId object_id_at(const S& s, PlotKind kind, int index) {
    return with_kind(s, kind, [index](const auto& v) {
        return index >= 0 && static_cast<std::size_t>(index) < v.size()
                   ? v[static_cast<std::size_t>(index)].id : kNoObject;
    }, kNoObject);
}

// The visible_stamp of the object at (kind, index); ~0 (older than nothing) if
// there is none.
template <class S>
unsigned long long visible_stamp_at(const S& s, PlotKind kind, int index) {
    return with_kind(s, kind, [index](const auto& v) {
        return index >= 0 && static_cast<std::size_t>(index) < v.size()
                   ? v[static_cast<std::size_t>(index)].visible_stamp : ~0ull;
    }, ~0ull);
}

namespace objects_detail {
// Outside the lambda below: Clang 18 crashes on `if constexpr (requires ...)` inside a
// capturing generic lambda.
template <class V>
unsigned long long zorder_stamp_of(const V& v, int index) {
    if constexpr (requires { v[0].zorder_stamp; })
        return index >= 0 && static_cast<std::size_t>(index) < v.size()
                   ? v[static_cast<std::size_t>(index)].zorder_stamp : ~0ull;
    else
        return ~0ull;
}
} // namespace objects_detail

// The zorder_stamp of the object at (kind, index); ~0 if there is none, or the
// kind has no zorder (a text).
template <class S>
unsigned long long zorder_stamp_at(const S& s, PlotKind kind, int index) {
    return with_kind(s, kind, [index](const auto& v) {
        return objects_detail::zorder_stamp_of(v, index);
    }, ~0ull);
}

// Where object `id` is among `kinds` of a holder, if anywhere.
template <class S, std::size_t N>
std::optional<ObjectRef> find_object_in(const S& s, const PlotKind (&kinds)[N], ObjectId id) {
    if (id == 0 || id == kNoObject) return std::nullopt;
    for (PlotKind k : kinds) {
        const auto i = with_kind(s, k, [id](const auto& v) {
            return objects_detail::index_of(v, id);
        }, std::optional<std::size_t>{});
        if (i) return ObjectRef{ k, *i };
    }
    return std::nullopt;
}

// A plot object taken out by remove_object(), for putting back:
// where it was among its kind, and the whole object, its id included.
using PlotObject2D = std::variant<LinePlot, ScatterPlot, BarPlot, HeatmapPlot, ScatterZPlot,
                                  TextPlot, AxLinePlot, AxSpanPlot, FillPlot, QuiverPlot,
                                  PolyPlot, CirclePlot, WedgePlot, HeatmapTriPlot, ContourPlot>;
using PlotObject3D = std::variant<Bar3DPlot, SurfacePlot, Scatter3DPlot, Line3DPlot,
                                  SurfaceTriPlot, TextPlot>;
template <class Object>
struct RemovedObject {
    std::size_t index = 0;
    Object      object;
};

// Removes object `id` from its kind's vector and returns it; nothing if it is
// not there.
template <class Object, class S, std::size_t N>
std::optional<RemovedObject<Object>> remove_object_in(S& s, const PlotKind (&kinds)[N],
                                                      ObjectId id) {
    using R = std::optional<RemovedObject<Object>>;
    const auto at = find_object_in(s, kinds, id);
    if (!at) return std::nullopt;
    return with_kind(s, at->kind, [&at](auto& v) -> R {
        const auto it = v.begin() + static_cast<std::ptrdiff_t>(at->index);
        RemovedObject<Object> r{ at->index, Object(std::move(*it)) };
        v.erase(it);
        return r;
    }, R{});
}

// Puts a removed object back at its index (clamped to the end of its kind),
// with its id; false if an object with that id is there already.
template <class S, std::size_t N, class Object>
bool restore_object_in(S& s, const PlotKind (&kinds)[N], RemovedObject<Object> r) {
    const ObjectId id = std::visit([](const auto& p) { return p.id; }, r.object);
    if (find_object_in(s, kinds, id)) return false;
    return std::visit([&](auto& p) {
        using P = std::decay_t<decltype(p)>;
        bool done = false;
        for (PlotKind k : kinds)
            done = done || with_kind(s, k, [&](auto& v) {
                if constexpr (std::is_same_v<typename std::decay_t<decltype(v)>::value_type, P>) {
                    const std::size_t at = std::min(r.index, v.size());
                    v.insert(v.begin() + static_cast<std::ptrdiff_t>(at), std::move(p));
                    return true;
                } else {
                    return false;
                }
            }, false);
        return done;
    }, r.object);
}

// Moves object `id` to position `to` among its kind (clamped to the last),
// keeping the others' order; returns where it was (a move back undoes it), or
// nothing if it is not there.
template <class S, std::size_t N>
std::optional<std::size_t> move_object_in(S& s, const PlotKind (&kinds)[N], ObjectId id,
                                          std::size_t to) {
    const auto at = find_object_in(s, kinds, id);
    if (!at) return std::nullopt;
    with_kind(s, at->kind, [&at, to](auto& v) {
        objects_detail::move_to(v, at->index, to);
        return true;
    }, false);
    return at->index;
}

// The order the plotting calls made a holder's plot objects in, texts left out:
// by ObjectId, which next_object_id() hands out at the call, so a later call
// draws over an earlier one whatever their kinds. What drawing, export, the
// legend, the colorbars and the hover hint all follow. Equal ids (objects built
// by hand with id 0, in tests) keep the listed kind order, then the index.
// Hidden objects (object_shown()) are left out, as all of those skip them.

// Whether an object is drawn at all: its options' `visible`. Ink, export, legend
// keys, colorbars, hover, pick and auto limits ask it; read-back, edits and the
// Data panel do not. A plane's own `visible` is separate (plane_drawn()).
template <class P> bool object_shown(const P& p) { return p.opts.visible; }
inline bool object_shown(const TextPlot& t) { return t.opts.text.visible; }

// The paint-order key of an object: its options' `zorder` (3D kinds and texts have
// none, so 0; only the 2D order functions read it).
template <class P> double object_zorder(const P& p) {
    if constexpr (requires { p.opts.zorder; }) return p.opts.zorder;
    else return 0.0;
}

// The `visible` field of an object's options (a text's is in its TextOptions).
template <class O> bool& visible_flag(O& o) {
    if constexpr (requires { o.text.visible; }) return o.text.visible;
    else return o.visible;
}
template <class V> bool any_shown(const V& v) {
    for (const auto& p : v)
        if (object_shown(p)) return true;
    return false;
}

namespace objects_detail {
struct OrderKey {
    double    z;
    ObjectId  id;
    ObjectRef ref;
};

template <class V>
void add_order_keys(std::vector<OrderKey>& keys, PlotKind kind, const V& v) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (object_shown(v[i])) keys.push_back({ object_zorder(v[i]), v[i].id, { kind, i } });
}

// By zorder first when `by_z`, then by ObjectId; the sort is stable, so equal
// keys keep the order they were added in.
inline std::vector<ObjectRef> sorted_refs(std::vector<OrderKey>& keys, bool by_z = false) {
    std::stable_sort(keys.begin(), keys.end(), [by_z](const OrderKey& a, const OrderKey& b) {
        if (by_z && a.z != b.z) return a.z < b.z;
        return a.id < b.id;
    });
    std::vector<ObjectRef> out;
    out.reserve(keys.size());
    for (const OrderKey& k : keys) out.push_back(k.ref);
    return out;
}

template <class S>
std::vector<ObjectRef> ordered_2d(const S& s, bool by_z) {
    std::vector<OrderKey> keys;
    keys.reserve(s.heatmaps.size() + s.bars.size() + s.lines.size() + s.scatters.size()
                 + s.scatter_z.size());
    // The fixed per-kind order that preceded call order, for the ties.
    add_order_keys(keys, PlotKind::Heatmap, s.heatmaps);
    add_order_keys(keys, PlotKind::Bar, s.bars);
    add_order_keys(keys, PlotKind::Line, s.lines);
    add_order_keys(keys, PlotKind::Scatter, s.scatters);
    add_order_keys(keys, PlotKind::ScatterZ, s.scatter_z);
    if constexpr (requires { s.axlines; s.axspans; }) {
        add_order_keys(keys, PlotKind::AxLine, s.axlines);
        add_order_keys(keys, PlotKind::AxSpan, s.axspans);
    }
    if constexpr (requires { s.fills; }) add_order_keys(keys, PlotKind::Fill, s.fills);
    if constexpr (requires { s.quivers; }) add_order_keys(keys, PlotKind::Quiver, s.quivers);
    if constexpr (requires { s.polys; }) {
        add_order_keys(keys, PlotKind::Poly, s.polys);
        add_order_keys(keys, PlotKind::Circle, s.circles);
        add_order_keys(keys, PlotKind::Wedge, s.wedges);
    }
    if constexpr (requires { s.heatmap_tris; }) add_order_keys(keys, PlotKind::HeatmapTri, s.heatmap_tris);
    if constexpr (requires { s.contours; }) add_order_keys(keys, PlotKind::Contour, s.contours);
    return sorted_refs(keys, by_z);
}
} // namespace objects_detail

// 2D: a RenderSnapshot, Axes::Impl, a plane's sheet or the SVG writer's data.
// call_order(): by ObjectId only (what the legend and colorbars list).
template <class S>
    requires requires(const S& s) { s.lines; s.scatters; s.bars; s.heatmaps; s.scatter_z; }
std::vector<ObjectRef> call_order(const S& s) {
    return objects_detail::ordered_2d(s, false);
}

// draw_order(): by (opts.zorder, ObjectId), what painting, export and hover follow.
template <class S>
    requires requires(const S& s) { s.lines; s.scatters; s.bars; s.heatmaps; s.scatter_z; }
std::vector<ObjectRef> draw_order(const S& s) {
    return objects_detail::ordered_2d(s, true);
}

// One object of a twinned cell (Axes::twinx()): which layer's holder (its
// index in the list given) and where in it.
struct LayerRef {
    std::size_t layer = 0;
    ObjectRef   ref;
};

namespace objects_detail {
template <class S>
std::vector<LayerRef> ordered_layers(std::span<const S* const> layers, bool by_z) {
    struct Key { double z; ObjectId id; LayerRef ref; };
    std::vector<Key> keys;
    for (std::size_t k = 0; k < layers.size(); ++k) {
        const S& s = *layers[k];
        const auto add = [&](PlotKind kind, const auto& v) {
            for (std::size_t i = 0; i < v.size(); ++i)
                if (object_shown(v[i])) keys.push_back({ object_zorder(v[i]), v[i].id, { k, { kind, i } } });
        };
        add(PlotKind::Heatmap, s.heatmaps);
        add(PlotKind::Bar, s.bars);
        add(PlotKind::Line, s.lines);
        add(PlotKind::Scatter, s.scatters);
        add(PlotKind::ScatterZ, s.scatter_z);
        if constexpr (requires { s.axlines; s.axspans; }) {
            add(PlotKind::AxLine, s.axlines);
            add(PlotKind::AxSpan, s.axspans);
        }
        if constexpr (requires { s.fills; }) add(PlotKind::Fill, s.fills);
        if constexpr (requires { s.quivers; }) add(PlotKind::Quiver, s.quivers);
        if constexpr (requires { s.polys; }) {
            add(PlotKind::Poly, s.polys);
            add(PlotKind::Circle, s.circles);
            add(PlotKind::Wedge, s.wedges);
        }
        if constexpr (requires { s.heatmap_tris; }) add(PlotKind::HeatmapTri, s.heatmap_tris);
        if constexpr (requires { s.contours; }) add(PlotKind::Contour, s.contours);
    }
    std::stable_sort(keys.begin(), keys.end(), [by_z](const Key& a, const Key& b) {
        if (by_z && a.z != b.z) return a.z < b.z;
        return a.id < b.id;
    });
    std::vector<LayerRef> out;
    out.reserve(keys.size());
    for (const Key& k : keys) out.push_back(k.ref);
    return out;
}
} // namespace objects_detail

// call_order() across a cell's layers (host first, then its twins): one order by
// ObjectId, so objects draw in call order whichever axes took them. Equal ids
// keep the layer order, then call_order()'s.
template <class S>
std::vector<LayerRef> layered_call_order(std::span<const S* const> layers) {
    return objects_detail::ordered_layers(layers, false);
}

// draw_order() across a cell's layers: by (zorder, ObjectId) over all of them.
template <class S>
std::vector<LayerRef> layered_draw_order(std::span<const S* const> layers) {
    return objects_detail::ordered_layers(layers, true);
}

// 3D: the axes' own kinds (not its planes, which are objects of their own). Only
// the legend and colorbars follow it: the scene is depth-tested.
inline std::vector<ObjectRef> call_order(const RenderSnapshot3D& s) {
    std::vector<objects_detail::OrderKey> keys;
    keys.reserve(s.bars3d.size() + s.surfaces.size() + s.scatter3d.size() + s.lines3d.size()
                 + s.surface_tri.size());
    objects_detail::add_order_keys(keys, PlotKind::Bar3D, s.bars3d);
    objects_detail::add_order_keys(keys, PlotKind::Surface, s.surfaces);
    objects_detail::add_order_keys(keys, PlotKind::Scatter3D, s.scatter3d);
    objects_detail::add_order_keys(keys, PlotKind::Line3D, s.lines3d);
    objects_detail::add_order_keys(keys, PlotKind::SurfaceTri, s.surface_tri);
    return objects_detail::sorted_refs(keys);
}

// Grid position of one axes (1-based, row-major, like matplotlib). Covers cells
// `index` (top-left) to `last` (bottom-right; 0 = just `index`). `index` is the
// slot's identity; Figure guarantees no two slots share a cell.
struct AxesSlot {
    int rows = 1, cols = 1, index = 1;
    int last = 0;

    int last_index() const { return last > 0 ? last : index; }
    // Zero-based bounds of the covered rectangle, both ends inclusive.
    int row0() const { return (index - 1) / cols; }
    int col0() const { return (index - 1) % cols; }
    int row1() const { return (last_index() - 1) / cols; }
    int col1() const { return (last_index() - 1) % cols; }
};

// A 2D axes' twins (Axes::twinx()/twiny()) are layers of its cell: layer 0 is
// the cell's own axes, kLayerTwinX its twinx, kLayerTwinY its twiny. An edit
// addresses a layer by axes_key(): the slot index alone for layer 0, so code
// that knows no twins addresses as before. add_subplot() keeps slot indices
// below 1 << kAxesKeyShift.
inline constexpr int kLayerTwinX = 1;
inline constexpr int kLayerTwinY = 2;
inline constexpr int kAxesKeyShift = 20;
constexpr int axes_key(int slot, int layer) { return slot + (layer << kAxesKeyShift); }
constexpr int key_slot(int key) { return key & ((1 << kAxesKeyShift) - 1); }
constexpr int key_layer(int key) { return key >> kAxesKeyShift; }

struct TwinSnapshot {
    int layer = kLayerTwinX;
    RenderSnapshot snap;
};

// One grid cell, holding a 2D or 3D axes snapshot. A variant (not a base class)
// so the compiler enumerates every consumer. The accessors are for code that
// already knows the kind; otherwise use std::visit.
struct FigureAxesSnapshot {
    AxesSlot slot;
    std::variant<RenderSnapshot, RenderSnapshot3D> snap;
    // 2D only: the axes' twins, twinx before twiny. Their own title, legend
    // and colorbar options are unused (the cell's are the host's).
    std::vector<TwinSnapshot> twins;

    // Layer `layer`'s snapshot (0 = the cell's own 2D axes); null if absent.
    const RenderSnapshot* layer2d(int layer) const {
        if (layer == 0) return snap2d();
        for (const TwinSnapshot& t : twins)
            if (t.layer == layer) return &t.snap;
        return nullptr;
    }
    RenderSnapshot* layer2d(int layer) {
        return const_cast<RenderSnapshot*>(std::as_const(*this).layer2d(layer));
    }

    bool is_3d() const { return std::holds_alternative<RenderSnapshot3D>(snap); }
    const RenderSnapshot*   snap2d() const { return std::get_if<RenderSnapshot>(&snap); }
    const RenderSnapshot3D* snap3d() const { return std::get_if<RenderSnapshot3D>(&snap); }
    RenderSnapshot*   snap2d() { return std::get_if<RenderSnapshot>(&snap); }
    RenderSnapshot3D* snap3d() { return std::get_if<RenderSnapshot3D>(&snap); }
};

// The cell's frame edges in its twinned axes' colors (AxesStyle::xaxis_color);
// none for an untwinned or 3D cell.
inline EdgeColors twin_edge_colors(const FigureAxesSnapshot& fa) {
    const RenderSnapshot* s = fa.snap2d();
    if (!s || fa.twins.empty()) return {};
    const AxesStyle* tx = nullptr;
    const AxesStyle* ty = nullptr;
    for (const TwinSnapshot& t : fa.twins) (t.layer == kLayerTwinX ? tx : ty) = &t.snap.axes_style;
    return twin_edge_colors(s->axes_style, tx, ty);
}

// Monotonic stamp for FigureSnapshot; the render caches' invalidation key
// (addresses could be recycled).
inline unsigned long long next_snapshot_generation() {
    static std::atomic<unsigned long long> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

// What SnapshotBox holds: every axes with its grid slot, captured together.
struct FigureSnapshot {
    std::vector<FigureAxesSnapshot> axes;

    // The 2D axes an edit key names (axes_key()), twins included; null if none.
    const RenderSnapshot* find2d(int key) const {
        for (const FigureAxesSnapshot& fa : axes)
            if (fa.slot.index == key_slot(key)) return fa.layer2d(key_layer(key));
        return nullptr;
    }
    RenderSnapshot* find2d(int key) {
        return const_cast<RenderSnapshot*>(std::as_const(*this).find2d(key));
    }
    // The 3D axes at slot `key` (3D axes have no layers).
    const RenderSnapshot3D* find3d(int key) const {
        for (const FigureAxesSnapshot& fa : axes)
            if (fa.slot.index == key) return fa.snap3d();
        return nullptr;
    }
    RenderSnapshot3D* find3d(int key) {
        return const_cast<RenderSnapshot3D*>(std::as_const(*this).find3d(key));
    }
    // `f(key, snapshot)` for every 2D axes, each cell's own before its twins.
    template <class F> void for_each2d(F&& f) const {
        for (const FigureAxesSnapshot& fa : axes)
            if (const RenderSnapshot* s = fa.snap2d()) {
                f(fa.slot.index, *s);
                for (const TwinSnapshot& t : fa.twins) f(axes_key(fa.slot.index, t.layer), t.snap);
            }
    }
    template <class F> void for_each2d(F&& f) {
        for (FigureAxesSnapshot& fa : axes)
            if (RenderSnapshot* s = fa.snap2d()) {
                f(fa.slot.index, *s);
                for (TwinSnapshot& t : fa.twins) f(axes_key(fa.slot.index, t.layer), t.snap);
            }
    }

    // 0 = never stamped (never matches a cache, so it rebuilds). `generation`
    // changes on any change; `data_generation` only when plot data changes, so
    // pan/zoom doesn't invalidate the render caches.
    unsigned long long generation      = 0;
    unsigned long long data_generation = 0;

    // Changes on every change except navigation (2D pan/zoom/reset, limits,
    // tick overrides, 3D camera); LayoutStore re-measures on it. Unrecognised
    // changes bump it.
    unsigned long long layout_generation = 0;

    // From FigureOptions::subplot_col_gap/row_gap; gaps between whole subplots.
    float col_gap = 20.0f;
    float row_gap = 20.0f;

    // See FigureMargins.
    FigureMargins margins;

    // See FigureOptions::background.
    Color background = {0.93f, 0.93f, 0.93f, 1.0f};

    // FigureOptions::pick_radius: what the hover hint and a Pick search with.
    float pick_radius = 12.0f;

    // FigureOptions::mathtext; layout, render and export open a MathTextScope with it,
    // and the math font (FigureOptions::math_fontset resolved; "" = the text font).
    bool mathtext = true;
    std::string math_font;

    // Grid weights; empty = equal. Read through grid_weights().
    std::vector<float> col_ratios, row_ratios;

    // Figure-level title.
    std::string suptitle;
    SuptitleOptions suptitle_opts;
    FigureStamps stamps;
};

// One plot object's colorbar: the scale it explains and its name. A stepped
// bar (a boundary norm in force) also carries its boundaries and each band's
// colormap position, and vmin/vmax are its first and last boundary.
struct ColorbarRequest {
    Colormap    cmap = Colormap::Viridis;
    float       vmin = 0.0f, vmax = 1.0f;
    std::string name;
    std::vector<double> bounds; // empty: the gradient bar
    std::vector<double> band_t; // bounds.size() - 1 positions
};

// A colormapped object's request, stepped under a boundary norm in force.
template <class O>
ColorbarRequest colorbar_request(const O& opts) {
    ColorbarRequest q{ opts.cmap, opts.vmin, opts.vmax, opts.name, {}, {} };
    if (norm_in_force(opts.norm, opts.vmin, opts.vmax) == ColorNorm::Kind::Boundaries) {
        q.bounds = opts.norm.bounds;
        q.band_t.resize(q.bounds.size() - 1);
        for (std::size_t i = 0; i < q.band_t.size(); ++i) q.band_t[i] = norm_band_t(i, q.band_t.size());
        q.vmin = static_cast<float>(q.bounds.front());
        q.vmax = static_cast<float>(q.bounds.back());
    }
    return q;
}

// A contour's (contour_plot.cpp): a contourf's stepped at its levels in its
// bands' colours; a contour's the gradient over its resolved limits (stepped
// under a boundary norm).
ColorbarRequest contour_colorbar_request(const ContourPlot& p);

// Every colorbar an axes draws: one per requesting object, in call order
// (outward from the frame). Shared by raster and SVG paths.
inline std::vector<ColorbarRequest> find_colorbar_requests(const RenderSnapshot& snap) {
    std::vector<ColorbarRequest> reqs;
    auto add = [&reqs](const auto& opts) {
        if (opts.colorbar) reqs.push_back(colorbar_request(opts));
    };
    for (const ObjectRef& r : call_order(snap)) {
        if (r.kind == PlotKind::Heatmap && snap.heatmaps[r.index].colormapped())
            add(snap.heatmaps[r.index].opts);
        else if (r.kind == PlotKind::ScatterZ) add(snap.scatter_z[r.index].opts);
        else if (r.kind == PlotKind::Quiver && snap.quivers[r.index].colormapped())
            add(snap.quivers[r.index].opts);
        else if (r.kind == PlotKind::HeatmapTri) add(snap.heatmap_tris[r.index].opts);
        else if (r.kind == PlotKind::Contour && snap.contours[r.index].opts.colorbar && snap.contours[r.index].geom)
            reqs.push_back(contour_colorbar_request(snap.contours[r.index]));
    }
    return reqs;
}

// 3D: colorbars are drawn beside the cell, not in the scene. The axes' own
// kinds come first, in call order, then each plane's. bar3d has no colormap.
// Each range is the resolved one.
inline std::vector<ColorbarRequest> find_colorbar_requests(const RenderSnapshot3D& snap) {
    std::vector<ColorbarRequest> reqs;
    auto add = [&reqs](const auto& opts, double lo, double hi) {
        reqs.push_back({ opts.cmap, static_cast<float>(lo), static_cast<float>(hi), opts.name, {}, {} });
    };
    for (const ObjectRef& r : call_order(snap)) {
        double lo = 0.0, hi = 1.0;
        switch (r.kind) {
            case PlotKind::Surface: {
                // Only with `colormap` on.
                const auto& sp = snap.surfaces[r.index];
                if (!sp.opts.colorbar || !sp.opts.colormap) break;
                surface_value_range(sp, lo, hi);
                add(sp.opts, lo, hi);
                break;
            }
            case PlotKind::Scatter3D: {
                // Only with a `colors` vector.
                const auto& sc = snap.scatter3d[r.index];
                if (!sc.opts.colorbar || !sc.colormapped()) break;
                scatter3d_value_range(sc, lo, hi);
                add(sc.opts, lo, hi);
                break;
            }
            case PlotKind::Line3D: {
                // As scatter3d.
                const auto& lp = snap.lines3d[r.index];
                if (!lp.opts.colorbar || !lp.colormapped()) break;
                line3d_value_range(lp, lo, hi);
                add(lp.opts, lo, hi);
                break;
            }
            case PlotKind::SurfaceTri: {
                // As scatter3d.
                const auto& sm = snap.surface_tri[r.index];
                if (!sm.opts.colorbar || !sm.colormapped()) break;
                surface_tri_value_range(sm, lo, hi);
                add(sm.opts, lo, hi);
                break;
            }
            default: break;
        }
    }
    for (const auto& pl : snap.planes) {
        auto one = find_colorbar_requests(pl.sheet);
        reqs.insert(reqs.end(), one.begin(), one.end());
    }
    return reqs;
}

} // namespace sextant
