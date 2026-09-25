#pragma once
#include "sextant/style.h"
#include "sextant/axes3d.h"
#include "cow_vec.h"
#include "tick.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

namespace sextant {

// Names a plot object's per-kind vector; objects are addressed as (kind, index).
// The first five are 2D (also held by planes); the rest are native 3D kinds,
// always addressed at the axes (plane -1). Append new kinds to keep values stable.
enum class PlotKind { Line, Scatter, Bar, Heatmap, ScatterZ, Bar3D, Surface, Scatter3D, Line3D,
                      SurfaceTri };

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

struct LinePlot {
    CowVec<double> x, y;
    ErrorBarData   err;
    LineOptions    opts;
    unsigned long long data_stamp = 0;

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
};

struct ScatterPlot {
    CowVec<double> x, y;
    ErrorBarData   err;
    ScatterOptions opts;
    unsigned long long data_stamp = 0;
};

// bar_width is in data units, already resolved (spacing x width fraction for
// bar(), bin width x fraction for hist()).
struct BarPlot {
    CowVec<double> centers;
    CowVec<double> heights;
    double         bar_width = 1.0;
    ErrorBarData   err;
    BarOptions     opts;
    unsigned long long data_stamp = 0;
};

// Row-major rows x cols values, mapped through vmin/vmax and the colormap. The
// grid spans xrange x yrange uniformly (ranges are outer edges); use the
// accessors below for cell positions. Rows count from yrange.lo; the origin
// flip is applied where data is read.
struct HeatmapPlot {
    CowVec<float>  data;
    int            rows = 0, cols = 0;
    Range          xrange{ 0.0, 1.0 }, yrange{ 0.0, 1.0 };
    HeatmapOptions opts;
    unsigned long long data_stamp = 0;

    double cell_w() const { return cols > 0 ? (xrange.hi - xrange.lo) / cols : 0.0; }
    double cell_h() const { return rows > 0 ? (yrange.hi - yrange.lo) / rows : 0.0; }

    // Cell index (fractional; a centre is j + 0.5) -> data space.
    double x_at(double col) const { return xrange.lo + col * cell_w(); }
    double y_at(double row) const { return yrange.lo + row * cell_h(); }

    // Data space -> fractional cell index. A degenerate range returns 0.
    double col_at(double x) const {
        const double w = xrange.hi - xrange.lo;
        return w != 0.0 ? (x - xrange.lo) / w * cols : 0.0;
    }
    double row_at(double y) const {
        const double h = yrange.hi - yrange.lo;
        return h != 0.0 ? (y - yrange.lo) / h * rows : 0.0;
    }
};

// Continuous-color scatter: z is mapped through opts.cmap/vmin/vmax.
struct ScatterZPlot {
    CowVec<double>  x, y, z;
    ErrorBarData    err;
    ScatterZOptions opts;
    unsigned long long data_stamp = 0;
};

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
    Bar3DOptions   opts;
    unsigned long long data_stamp = 0;

    std::size_t count() const { return u.size() * v.size(); }
    std::size_t index_of(std::size_t i, std::size_t j) const { return i * v.size() + j; }

    double height_at(std::size_t k) const { return k < heights.size() ? heights[k] : 0.0; }
    double bottom_at(std::size_t k) const {
        return k < bottoms.size() ? bottoms[k] : opts.bottom;
    }

    // Extent along the standing axis, low end first (negative heights hang down).
    double h_lo(std::size_t k) const { return std::min(bottom_at(k), bottom_at(k) + height_at(k)); }
    double h_hi(std::size_t k) const { return std::max(bottom_at(k), bottom_at(k) + height_at(k)); }
};

// A surface over a u x v grid, laid out like Bar3DPlot. Draws the
// (|u|-1) x (|v|-1) cells between samples; cell (i, j) spans samples (i, j)
// to (i+1, j+1).
struct SurfacePlot {
    CowVec<double> u, v;
    CowVec<double> heights;
    PlaneOrientation orient = PlaneOrientation::XY;
    SurfaceOptions opts;
    unsigned long long data_stamp = 0;

    std::size_t count() const { return u.size() * v.size(); }
    std::size_t index_of(std::size_t i, std::size_t j) const { return i * v.size() + j; }
    double height_at(std::size_t k) const { return k < heights.size() ? heights[k] : 0.0; }

    std::size_t cell_rows() const { return u.size() > 1 ? u.size() - 1 : 0; }
    std::size_t cell_cols() const { return v.size() > 1 ? v.size() - 1 : 0; }
    std::size_t cell_count() const { return cell_rows() * cell_cols(); }
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
    SurfaceTriOptions opts;
    unsigned long long data_stamp = 0;

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
    Scatter3DOptions opts;
    unsigned long long data_stamp = 0;

    std::size_t count() const { return x.size(); }
    bool colormapped() const { return !colors.empty(); }
    double color_at(std::size_t i) const { return i < colors.size() ? colors[i] : 0.0; }
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
    Line3DOptions opts;
    unsigned long long data_stamp = 0;

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

struct AllPlotData {
    const std::vector<LinePlot>&     lines;
    const std::vector<ScatterPlot>&  scatters;
    const std::vector<BarPlot>&      bars;
    const std::vector<HeatmapPlot>&  heatmaps;
    const std::vector<ScatterZPlot>& scatter_z;
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
// resets them. `cleared` is the exception: cla() gives it a fresh stamp, and an
// edit addressed to a plot object by index (its appearance) records it, so
// after a cla() and re-plot it cannot land on whatever took that index.
struct StyleStamps {
    unsigned long long style = 0, grid = 0, legend = 0, colorbar = 0;
    unsigned long long xticks = 0, yticks = 0, zticks = 0;
    unsigned long long box = 0, aspect = 0;   // 3D only
    unsigned long long cleared = 0;

    static constexpr StyleStamps any() {
        constexpr auto m = ~0ull;
        return { m, m, m, m, m, m, m, m, m, m };
    }
};

// Figure-level: suptitle() -> suptitle (its text), set_suptitle_style() and
// suptitle()'s font size -> suptitle_style, set_margins() -> margins.
struct FigureStamps {
    unsigned long long suptitle = 0, suptitle_style = 0, margins = 0;

    static constexpr FigureStamps any() {
        constexpr auto m = ~0ull;
        return { m, m, m };
    }
};

// Everything the render thread needs from Axes::Impl: decoration and limits
// copied, plot data shared via CowVec. Built by Axes::Impl::build_snapshot().
struct RenderSnapshot {
    std::vector<LinePlot>     lines;
    std::vector<ScatterPlot>  scatters;
    std::vector<BarPlot>      bars;
    std::vector<HeatmapPlot>  heatmaps;
    std::vector<ScatterZPlot> scatter_z;

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

    // Explicit ticks; absent = generate_ticks().
    std::optional<std::vector<Tick>> xticks_override, yticks_override;

    AllPlotData all() const { return { lines, scatters, bars, heatmaps, scatter_z }; }
};

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
    // What a double-click restores (the reset runs on the render thread).
    Camera3D    default_camera;

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

// One grid cell, holding a 2D or 3D axes snapshot. A variant (not a base class)
// so the compiler enumerates every consumer. The accessors are for code that
// already knows the kind; otherwise use std::visit.
struct FigureAxesSnapshot {
    AxesSlot slot;
    std::variant<RenderSnapshot, RenderSnapshot3D> snap;

    bool is_3d() const { return std::holds_alternative<RenderSnapshot3D>(snap); }
    const RenderSnapshot*   snap2d() const { return std::get_if<RenderSnapshot>(&snap); }
    const RenderSnapshot3D* snap3d() const { return std::get_if<RenderSnapshot3D>(&snap); }
    RenderSnapshot*   snap2d() { return std::get_if<RenderSnapshot>(&snap); }
    RenderSnapshot3D* snap3d() { return std::get_if<RenderSnapshot3D>(&snap); }
};

// Monotonic stamp for FigureSnapshot; the render caches' invalidation key
// (addresses could be recycled).
inline unsigned long long next_snapshot_generation() {
    static std::atomic<unsigned long long> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

// What SnapshotBox holds: every axes with its grid slot, captured together.
struct FigureSnapshot {
    std::vector<FigureAxesSnapshot> axes;

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

    // Grid weights; empty = equal. Read through grid_weights().
    std::vector<float> col_ratios, row_ratios;

    // Figure-level title.
    std::string suptitle;
    SuptitleOptions suptitle_opts;
    FigureStamps stamps;
};

// One plot object's colorbar: the scale it explains and its name.
struct ColorbarRequest {
    Colormap    cmap = Colormap::Viridis;
    float       vmin = 0.0f, vmax = 1.0f;
    std::string name;
};

// Every colorbar an axes draws: one per requesting object, in kind order then
// index (outward from the frame). Shared by raster and SVG paths.
inline std::vector<ColorbarRequest> find_colorbar_requests(const RenderSnapshot& snap) {
    std::vector<ColorbarRequest> reqs;
    for (const auto& hp : snap.heatmaps)
        if (hp.opts.colorbar) reqs.push_back({ hp.opts.cmap, hp.opts.vmin, hp.opts.vmax,
                                               hp.opts.name });
    for (const auto& sp : snap.scatter_z)
        if (sp.opts.colorbar) reqs.push_back({ sp.opts.cmap, sp.opts.vmin, sp.opts.vmax,
                                               sp.opts.name });
    return reqs;
}

// 3D: colorbars are drawn beside the cell, not in the scene. The axes' own
// kinds come first, then each plane's. bar3d has no colormap.
inline std::vector<ColorbarRequest> find_colorbar_requests(const RenderSnapshot3D& snap) {
    std::vector<ColorbarRequest> reqs;
    for (const auto& sp : snap.surfaces) {
        // Only with `colormap` on; the range is the resolved one.
        if (!sp.opts.colorbar || !sp.opts.colormap) continue;
        double lo = 0.0, hi = 1.0;
        surface_value_range(sp, lo, hi);
        reqs.push_back({ sp.opts.cmap, static_cast<float>(lo), static_cast<float>(hi),
                         sp.opts.name });
    }
        // Only with a `colors` vector; the range is the resolved one.
    for (const auto& sc : snap.scatter3d) {
        if (!sc.opts.colorbar || !sc.colormapped()) continue;
        double lo = 0.0, hi = 1.0;
        scatter3d_value_range(sc, lo, hi);
        reqs.push_back({ sc.opts.cmap, static_cast<float>(lo), static_cast<float>(hi),
                         sc.opts.name });
    }
        // As scatter3d.
    for (const auto& lp : snap.lines3d) {
        if (!lp.opts.colorbar || !lp.colormapped()) continue;
        double lo = 0.0, hi = 1.0;
        line3d_value_range(lp, lo, hi);
        reqs.push_back({ lp.opts.cmap, static_cast<float>(lo), static_cast<float>(hi),
                         lp.opts.name });
    }
        // As scatter3d.
    for (const auto& sm : snap.surface_tri) {
        if (!sm.opts.colorbar || !sm.colormapped()) continue;
        double lo = 0.0, hi = 1.0;
        surface_tri_value_range(sm, lo, hi);
        reqs.push_back({ sm.opts.cmap, static_cast<float>(lo), static_cast<float>(hi),
                         sm.opts.name });
    }
    for (const auto& pl : snap.planes) {
        auto one = find_colorbar_requests(pl.sheet);
        reqs.insert(reqs.end(), one.begin(), one.end());
    }
    return reqs;
}

} // namespace sextant
