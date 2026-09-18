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

// Identifies which per-kind plot vector an object lives in. Both the
// data-panel view layer and the cross-thread edit payload need to name a plot
// object as a (kind, index) pair -- plot objects carry no identity of their
// own.
//
// The first five are the 2D vectors, and are what a plane of a 3D scene holds
// too (spec_3d.md §6). The five after them are the kinds native to a 3D
// axes: each indexes its own vector on RenderSnapshot3D rather than any
// RenderSnapshot, so each is always addressed at the *axes* (plane index -1)
// and never reaches a sheet. A new kind is appended rather than slotted in
// beside its relatives, so that every existing serialized/positional use of
// the enum keeps its value.
enum class PlotKind { Line, Scatter, Bar, Heatmap, ScatterZ, Bar3D, Surface, Scatter3D, Line3D,
                      SurfaceTri };

// The bulk data of every plot object below is a CowVec, not a plain vector:
// Axes::Impl and RenderSnapshot declare these same structs, and a snapshot is
// copied on hot paths (refresh(), and every frame of a pan). See cow_vec.h —
// in particular its threading note, which is what keeps a shared buffer safe.

// One end pair of an error bar at one point, resolved: both distances from the
// point, each >= 0, and 0 meaning "nothing on this side".
struct ErrOffsets {
    double lo = 0.0, hi = 0.0;
    bool any() const { return lo > 0.0 || hi > 0.0; }
};

// The caller's ErrorBar, copied at ingest. It lives in the plot object rather
// than the options struct for the reason CowVec exists: `opts` is deep-copied
// into every snapshot, so per-point vectors parked there would put a memcpy of
// the whole series back on the pan path.
//
// Stored exactly as given, one end or both, and resolved by the accessors: a
// one-end-given pair reads as symmetric, a negative as its magnitude and a
// non-finite entry as 0. Keeping "symmetric" a fact of the stored data rather
// than a copy made at ingest means an edit to one end can never quietly break
// it -- and a CowVec copy would share a buffer anyway, so nothing is saved.
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

// The caller's ErrorBar3D, copied at ingest (v1.0 step 17): ErrorBarData's
// twelve-span sibling, stored as given and resolved by the accessors on
// exactly its terms. Indexed by axis (0 = x, 1 = y, 2 = z) rather than named
// per direction, because every consumer -- the geometry, the limits, the hover
// text -- does the same thing on all three axes and would otherwise write it
// three times.
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

    std::size_t count() const { return x.size(); }

    // What `loop` means, in one place -- Line3DPlot::segment_count() for a 2D
    // line, and for the same reason: the stroke shader's instance count, the
    // SVG element and a plane's sheet strokes must agree about whether there
    // is a closing segment and which points it joins. A path of fewer than
    // two points has no segment at all, looped or not.
    std::size_t segment_count() const {
        if (count() < 2) return 0;
        return opts.loop ? count() : count() - 1;
    }

    // The two point indices segment `s` joins; the closing segment is the one
    // that wraps.
    void segment_ends(std::size_t s, std::size_t& a, std::size_t& b) const {
        a = s;
        b = (s + 1 == count()) ? 0 : s + 1;
    }
};

struct ScatterPlot {
    CowVec<double> x, y;
    ErrorBarData   err;
    ScatterOptions opts;
};

// bar_width is always in data-space units, already resolved: inter-bar
// spacing x BarOptions::width for bar(), bin width x the same fraction for
// hist(). This struct does not know which method produced it.
struct BarPlot {
    CowVec<double> centers;
    CowVec<double> heights;
    double         bar_width = 1.0;
    ErrorBarData   err;
    BarOptions     opts;
};

// Data is row-major, rows×cols float values in [vmin,vmax] (before colormap).
//
// The grid occupies xrange × yrange in data space, uniformly: a cell is
// cell_w() × cell_h() and the ranges are its outer edges, not centres (see
// Range). Everything that needs a cell's position -- the textured quad, the
// SVG <image> box, contour tracing, hover hit-testing -- goes through the
// four accessors below rather than assuming the old index-space footprint.
// imshow() is exactly the case xrange = [0,cols], yrange = [0,rows].
//
// Row index counts along yrange from `lo`, which for the default
// origin=="lower" is also storage order; the origin flip is applied where the
// data is read, not here.
struct HeatmapPlot {
    CowVec<float>  data;
    int            rows = 0, cols = 0;
    Range          xrange{ 0.0, 1.0 }, yrange{ 0.0, 1.0 };
    HeatmapOptions opts;

    double cell_w() const { return cols > 0 ? (xrange.hi - xrange.lo) / cols : 0.0; }
    double cell_h() const { return rows > 0 ? (yrange.hi - yrange.lo) / rows : 0.0; }

    // Index space -> data space. `col`/`row` are in cell units from the
    // range's `lo` edge and may be fractional -- a cell centre is j + 0.5.
    double x_at(double col) const { return xrange.lo + col * cell_w(); }
    double y_at(double row) const { return yrange.lo + row * cell_h(); }

    // The inverse, for hit-testing: data space -> fractional cell index. A
    // degenerate range (only reachable by building this struct directly;
    // Axes::heatmap() rejects one) reports 0 rather than dividing by zero.
    double col_at(double x) const {
        const double w = xrange.hi - xrange.lo;
        return w != 0.0 ? (x - xrange.lo) / w * cols : 0.0;
    }
    double row_at(double y) const {
        const double h = yrange.hi - yrange.lo;
        return h != 0.0 ? (y - yrange.lo) / h * rows : 0.0;
    }
};

// Continuous-color scatter — see ScatterZOptions. z is per-point data mapped
// through opts.cmap/vmin/vmax, independent of the x/y position.
struct ScatterZPlot {
    CowVec<double>  x, y, z;
    ErrorBarData    err;
    ScatterZOptions opts;
};

// Which box axis each of a Bar3DPlot's three directions is, as indices into
// (x, y, z). One place rather than a switch at every consumer: the raster
// path, the SVG path and auto_scale3d() all have to agree about what "u" is,
// and three copies of a three-case switch is three chances to disagree.
struct Axis3Map { int u = 0, v = 1, h = 2; };

inline Axis3Map axis_map(PlaneOrientation o) {
    switch (o) {
        case PlaneOrientation::YZ: return { 1, 2, 0 };
        case PlaneOrientation::ZX: return { 2, 0, 1 };
        case PlaneOrientation::XY: break;
    }
    return { 0, 1, 2 };
}

// Bars standing on a u x v grid. `heights` is row-major with u as the major
// index, so bar (i, j) is heights[i * v.size() + j].
//
// u_width/v_width are data-space footprints, already resolved from
// Bar3DOptions::width/depth times the grid spacing -- BarPlot::bar_width's
// precedent, and for the same reason: nothing downstream should have to know
// which fraction produced them. `bottoms` is empty unless the caller gave one
// per bar, in which case it is indexed like `heights`.
struct Bar3DPlot {
    CowVec<double> u, v;
    CowVec<double> heights;
    CowVec<double> bottoms;
    double         u_width = 1.0, v_width = 1.0;
    PlaneOrientation orient = PlaneOrientation::XY;
    Bar3DOptions   opts;

    std::size_t count() const { return u.size() * v.size(); }
    std::size_t index_of(std::size_t i, std::size_t j) const { return i * v.size() + j; }

    double height_at(std::size_t k) const { return k < heights.size() ? heights[k] : 0.0; }
    double bottom_at(std::size_t k) const {
        return k < bottoms.size() ? bottoms[k] : opts.bottom;
    }

    // The bar's extent along the standing axis, low end first, so a negative
    // height reads as a bar hanging below its base rather than as an inverted
    // box every consumer has to normalize for itself.
    double h_lo(std::size_t k) const { return std::min(bottom_at(k), bottom_at(k) + height_at(k)); }
    double h_hi(std::size_t k) const { return std::max(bottom_at(k), bottom_at(k) + height_at(k)); }
};

// A surface over the same u x v grid Bar3DPlot stands its bars on, and stored
// the same way: `heights` is row-major with u as the major index, so sample
// (i, j) is heights[i * v.size() + j].
//
// The vertices are the samples; what is *drawn* is the |u|-1 by |v|-1 grid of
// cells between them, which is why the ingest requires at least 2 x 2. A cell
// is addressed by its low corner, so cell (i, j) spans samples (i, j) through
// (i+1, j+1) -- the same indexing arithmetic as the bar grid, one row and one
// column shorter.
struct SurfacePlot {
    CowVec<double> u, v;
    CowVec<double> heights;
    PlaneOrientation orient = PlaneOrientation::XY;
    SurfaceOptions opts;

    std::size_t count() const { return u.size() * v.size(); }
    std::size_t index_of(std::size_t i, std::size_t j) const { return i * v.size() + j; }
    double height_at(std::size_t k) const { return k < heights.size() ? heights[k] : 0.0; }

    std::size_t cell_rows() const { return u.size() > 1 ? u.size() - 1 : 0; }
    std::size_t cell_cols() const { return v.size() > 1 ? v.size() - 1 : 0; }
    std::size_t cell_count() const { return cell_rows() * cell_cols(); }
};

// The range a surface's colormap is normalized over. `SurfaceOptions::vmin ==
// vmax` means "the surface's own range" -- see the option's own comment for why
// that default differs from HeatmapOptions'. One definition, because the raster
// path, the vector path and the colorbar the surface asks for must all sample
// and label the colormap at the same place.
//
// Here rather than in renderer/surface.h, where it lived until v1.0 step 11.3:
// it is arithmetic over the plot's own data and nothing else, and the colorbar
// lookup below -- which has to resolve the range to measure the bar's numbers
// -- sits above that header and could not reach it there.
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

// A sheet on a triangulated mesh (v1.0 step 14) -- SurfacePlot's sibling for
// data that has no `u x v` grid. The vertices are three independent coordinate
// vectors, as a cloud's and a path's are; what makes it a *sheet* is `tri`,
// three vertex indices per triangle, row-major.
//
// The indices are stored rather than consumed because they *are* the mesh:
// the two `orient` overloads of Axes3D::surface_tri() run their Delaunay
// triangulation at ingest and store the result here, so downstream there is
// exactly one kind of mesh and nothing has to ask which overload was called.
// They are deliberately never uploaded to the GPU -- a per-face normal and
// shade cannot be shared between the faces meeting at a vertex, so the mesh
// expands to 3M independent vertices exactly as a SurfacePlot's cells already
// do -- which is what leaves the index type free to be `std::uint32_t` for
// clarity rather than chosen for `glDrawElements`.
//
// `colors` is the fourth dimension and is empty for a flat mesh, exactly as on
// a Scatter3DPlot: nothing downstream asks which overload was called, only
// whether this vector has anything in it. A value belongs to a *vertex*, and a
// triangle interpolates between its three.
struct SurfaceTriPlot {
    CowVec<double> x, y, z;
    CowVec<std::uint32_t> tri;
    CowVec<double> colors;
    SurfaceTriOptions opts;

    std::size_t count() const { return x.size(); }               // vertices
    std::size_t face_count() const { return tri.size() / 3; }
    bool colormapped() const { return !colors.empty(); }
    double color_at(std::size_t i) const { return i < colors.size() ? colors[i] : 0.0; }

    // The three vertex indices of face `f`. Every consumer asks this rather
    // than re-deriving `3f + k`, for the reason Line3DPlot::segment_ends()
    // exists: the raster buffer, the SVG plan, the wireframe and the ray cast
    // must not be able to disagree about which vertices a face joins.
    //
    // Out of range comes back as 0, which is a guard for a hand-built
    // snapshot; ingest rejects every index the public API could produce.
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

    // The data-space position of vertex `i`. Three independent coordinates and
    // no Axis3Map -- a mesh stands on no pair of axes, exactly as a cloud and
    // a path do not.
    Vec3 vertex(std::size_t i) const {
        return { i < x.size() ? x[i] : 0.0,
                 i < y.size() ? y[i] : 0.0,
                 i < z.size() ? z[i] : 0.0 };
    }
};

// The range a mesh's `colors` are normalized over -- scatter3d_value_range()
// for a mesh, and a third copy rather than a template over the three for the
// reason line3d_value_range() records: these are siblings, not instances of
// one thing, and one of them will grow a range rule the others do not the
// moment either needs to exclude a vertex from its own scale.
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

// Markers at |x| points in the scene -- see Scatter3DOptions.
//
// `colors` is the fourth dimension and is empty for a flat series, which is
// the one thing that distinguishes the two overloads of Axes3D::scatter3d()
// once the data is in: nothing downstream asks which was called, only whether
// this vector has anything in it.
//
// Unlike every other 3D kind here there is no `orient` and no Axis3Map: a
// grid stands on two axes and rises along a third, while a scatter simply has
// three coordinates, none of them privileged.
struct Scatter3DPlot {
    CowVec<double> x, y, z;
    CowVec<double> colors;
    ErrorBar3DData err;
    Scatter3DOptions opts;

    std::size_t count() const { return x.size(); }
    bool colormapped() const { return !colors.empty(); }
    double color_at(std::size_t i) const { return i < colors.size() ? colors[i] : 0.0; }
};

// The range a scatter3d's `colors` are normalized over, on exactly the terms
// surface_value_range() states: `vmin == vmax` means the series' own range,
// because a `colors` vector is a measured quantity in the caller's own units
// and a 0..1 default would clip almost every real series to one end of the
// colormap. One definition, because the raster path, the SVG path and the
// colorbar the series asks for must sample and label the map at one place.
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

// A path through the scene (v1.0 step 13): the fourth native 3D kind, and the
// first that is a *path* rather than a grid or a cloud. Its data is shaped
// exactly like a Scatter3DPlot's -- three independent coordinates and an
// optional fourth carried as colour -- and what differs is entirely in the
// drawing: the points are joined, so what is stroked is the |x| - 1 segments
// between them (|x| with `loop`), and a `colors` value belongs to a *point*
// while a colour belongs to a *segment*, which is why a segment ramps between
// the two it joins.
struct Line3DPlot {
    CowVec<double> x, y, z;
    CowVec<double> colors;
    ErrorBar3DData err;
    Line3DOptions opts;

    std::size_t count() const { return x.size(); }
    bool colormapped() const { return !colors.empty(); }
    double color_at(std::size_t i) const { return i < colors.size() ? colors[i] : 0.0; }

    // How many segments are actually drawn. `loop` closes the path with one
    // more, and a path of fewer than two points has none at all -- which
    // ingest rejects, so this is a guard for a hand-built snapshot rather than
    // for anything the public API can produce.
    std::size_t segment_count() const {
        if (count() < 2) return 0;
        return opts.loop ? count() : count() - 1;
    }

    // The two point indices segment `s` joins. The closing segment is the one
    // that wraps, which is the whole of what `loop` means downstream -- every
    // consumer asks this rather than re-deriving the wrap, so the ribbon, the
    // SVG painter, the hover search and the gradient cannot disagree about
    // whether there is a closing segment or which points it joins.
    void segment_ends(std::size_t s, std::size_t& a, std::size_t& b) const {
        a = s;
        b = (s + 1 == count()) ? 0 : s + 1;
    }
};

// The range a line3d's `colors` are normalized over -- scatter3d_value_range()
// for a path, and deliberately a second function rather than a template over
// the two: the two structs are siblings, not instances of one thing, and one
// of them will grow a range rule the other does not the moment either needs to
// exclude a point from its own scale.
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

// Owns everything the render path needs from Axes::Impl, so a background
// render thread never dereferences live Axes state. Decoration and limits are
// copies; the bulk plot data is an immutable *share* of the live buffers
// (CowVec), which gives the same isolation. Built by
// Axes::Impl::build_snapshot() and handed off through SnapshotBox.
struct RenderSnapshot {
    std::vector<LinePlot>     lines;
    std::vector<ScatterPlot>  scatters;
    std::vector<BarPlot>      bars;
    std::vector<HeatmapPlot>  heatmaps;
    std::vector<ScatterZPlot> scatter_z;

    // Their font sizes live in axes_style (see Axes::Impl).
    std::string title, xtitle, ytitle;

    bool          grid_enabled   = false;
    GridOptions   grid_opts;
    bool          legend_enabled = false;
    LegendOptions   legend_opts;
    ColorbarOptions colorbar_opts;
    AxesStyle     axes_style;

    double xmin = 0, xmax = 1, ymin = 0, ymax = 1;
    bool   xlim_auto = true, ylim_auto = true;

    // Explicit tick override set via Axes::set_xticks/set_yticks (or the
    // widget panel's tick table). Absent = fall back to generate_ticks().
    std::optional<std::vector<Tick>> xticks_override, yticks_override;

    AllPlotData all() const { return { lines, scatters, bars, heatmaps, scatter_z }; }
};

// One 2D plane in a 3D scene: where it sits, and everything on it.
//
// `sheet` is a whole RenderSnapshot rather than a bare list of plot vectors,
// because a Plane2D holds an Axes::Impl and this is what that Impl builds --
// which is the point of the arrangement (spec_3d.md §6): ingest, validation,
// CowVec sharing and build_snapshot() are the 2D code unchanged. The fields of
// it that mean nothing on a plane (limits, ticks, the axes title) are simply
// never read: a plane's in-plane coordinates *are* the parent's data
// coordinates, so there are no limits of its own to resolve. What is read is
// the five plot vectors, plus colorbar_opts/legend_opts, which the parent cell
// hoists (see compute_cell_decorations()).
struct PlaneSnapshot {
    PlaneOrientation orient = PlaneOrientation::XY;
    // Along the axis `orient` is normal to, in that axis's data units.
    double           offset = 0.0;
    Plane2DOptions   opts;
    RenderSnapshot   sheet;
};

// The 3D counterpart of RenderSnapshot: everything the render path needs from
// Axes3D::Impl. It is a separate type rather than extra fields on
// RenderSnapshot precisely because almost none of that sibling's vocabulary
// (one CoordTransform, two limits, five plot vectors) means anything here --
// the 2D kinds reach a 3D scene through `planes` below, each of which carries
// its own RenderSnapshot, rather than by this type growing them.
struct RenderSnapshot3D {
    // bar3d is the first native 3D kind (step 4).
    std::vector<Bar3DPlot> bars3d;
    // The second (step 7c). Beside the bars rather than under them: the two
    // share a grid layout and a light, and nothing else -- a bar is a solid
    // with six faces and a surface is a sheet with two sides.
    std::vector<SurfacePlot> surfaces;
    // The third (v1.0 step 12), and the first that is not a grid at all: a
    // cloud of points with three independent coordinates, so it has no
    // `orient` and feeds all three limits from one vector each.
    std::vector<Scatter3DPlot> scatter3d;
    // The fourth (v1.0 step 13): the cloud's points joined in order. Shares
    // the cloud's data shape and none of its drawing -- what is stroked is the
    // segments between the points, not the points.
    std::vector<Line3DPlot> lines3d;
    // The fifth (v1.0 step 14): a sheet on a triangulated mesh. Beside
    // `surfaces` rather than merged into it for the reason `surfaces` sits
    // beside `bars3d`: the two answer the same question about differently
    // shaped data, and a grid carries a vocabulary -- u, v, an orient, a cell
    // addressed by its low corner -- that a mesh has none of.
    std::vector<SurfaceTriPlot> surface_tri;
    // The 2D kinds, each on its own plane (step 5). Order is the order they
    // were added; the depth order they are actually drawn in is resolved per
    // frame from the camera, so this only decides ties.
    std::vector<PlaneSnapshot> planes;

    // Font sizes live in axes_style, as they do in 2D.
    std::string title, xtitle, ytitle, ztitle;

    // On by default, unlike 2D: without grid lines a back pane is a blank
    // wall and there is nothing in the picture to read a depth off.
    bool        grid_enabled = true;
    GridOptions grid_opts;

    // Hoisted decoration (spec_3d.md §6). Both belong to the *cell*: they are
    // measured in pixels and drawn beside the frame, so putting either in the
    // scene would foreshorten it, turn it with the camera and let the geometry
    // it explains occlude it. The legend is switched on here rather than per
    // plane because it keys the cell -- two planes asking for two boxes is not
    // something a figure can lay out -- while a *colorbar* is requested by an
    // individual plot object, exactly as in 2D, and found across the planes.
    //
    // Their *styling* is the axes' either way (v1.0 step 11.2). A colorbar's
    // used to be hoisted off whichever plane had asked, which was always the
    // default -- a Plane2D has no set_colorbar_style() -- and had no answer
    // for a bar asked for by something that is not on a plane at all.
    bool            legend_enabled = false;
    LegendOptions   legend_opts;
    ColorbarOptions colorbar_opts;

    AxesStyle   axes_style;
    Box3DStyle  box_style;
    BoxAspect   aspect;
    Camera3D    camera;
    // What a double-click in the plot restores. Held in the snapshot because
    // the reset happens on the render thread, which never sees Axes3D::Impl.
    Camera3D    default_camera;

    double xmin = 0, xmax = 1, ymin = 0, ymax = 1, zmin = 0, zmax = 1;
    bool   xlim_auto = true, ylim_auto = true, zlim_auto = true;

    std::optional<std::vector<Tick>> xticks_override, yticks_override, zticks_override;

    // Uniform plane access for the edit path (step 6b). Axes3D::Impl declares
    // the same two, over the shared_ptr<Plane2D> it holds instead of the
    // PlaneSnapshot here, and each yields something with the same four member
    // names -- orient, offset, opts, sheet. That is what lets one
    // apply_axes3d_edit() and one apply_plot_data_ops() serve both sides of
    // the thread boundary, which is the property that stops an edit from
    // working on the render thread and reverting on the next refresh().
    std::size_t     plane_count() const              { return planes.size(); }
    PlaneSnapshot&  plane_at(std::size_t i)          { return planes[i]; }
    const PlaneSnapshot& plane_at(std::size_t i) const { return planes[i]; }
};

// Grid position of one Axes within a Figure (1-indexed, row-major —
// matplotlib add_subplot semantics). rows=cols=index=1 for a Figure that
// never called add_subplot().
//
// A slot covers the rectangle of cells from `index` (top-left) to `last`
// (bottom-right); `last` = 0 means the one cell `index`. `index` stays the
// slot's identity -- the edit lanes, the selection and size_for_frame() are
// all keyed on it -- which is sound because Figure refuses any two slots
// that share a cell, so no two share a first cell either.
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

// One cell of the grid, holding whichever kind of axes occupies it.
//
// A variant rather than a common base with virtuals, and that is the whole
// mechanism rather than a cost (see memory/spec_3d.md §1): it turns "every
// place that consumes an axes snapshot" into a list the compiler produces,
// which is exactly the enumeration this feature needs. A base class would
// have hidden it, and would have had to invent a vocabulary the two kinds do
// not share -- one CoordTransform and two limits against three limits and a
// camera.
//
// The two accessors below are for consumers that have already established
// which kind they hold; anything that must handle both uses std::visit.
struct FigureAxesSnapshot {
    AxesSlot slot;
    std::variant<RenderSnapshot, RenderSnapshot3D> snap;

    bool is_3d() const { return std::holds_alternative<RenderSnapshot3D>(snap); }
    const RenderSnapshot*   snap2d() const { return std::get_if<RenderSnapshot>(&snap); }
    const RenderSnapshot3D* snap3d() const { return std::get_if<RenderSnapshot3D>(&snap); }
    RenderSnapshot*   snap2d() { return std::get_if<RenderSnapshot>(&snap); }
    RenderSnapshot3D* snap3d() { return std::get_if<RenderSnapshot3D>(&snap); }
};

// What SnapshotBox holds: every Axes in a Figure with its grid slot, captured
// at once so the render thread draws a consistent grid without touching live
// Axes::Impl.

// Monotonic id stamped on every FigureSnapshot as it is built, and the
// invalidation key for the render thread's caches: a snapshot is immutable
// once published, so equal generations guarantee identical plot data. An
// explicit counter rather than the snapshot's address, because a freed
// snapshot's address can be recycled by the allocator.
inline unsigned long long next_snapshot_generation() {
    static std::atomic<unsigned long long> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

struct FigureSnapshot {
    std::vector<FigureAxesSnapshot> axes;

    // 0 means "never stamped" and never matches a cache entry, so a path that
    // forgets to stamp degrades to rebuilding every frame rather than showing
    // stale geometry.
    //
    // `generation` changes whenever *anything* in the snapshot changes;
    // `data_generation` only when the plot *data* does. The split exists
    // because pan and zoom push a limits edit on nearly every frame of a
    // drag: keying the render caches on `generation` would make them miss on
    // exactly the frames they are meant to help.
    unsigned long long generation      = 0;
    unsigned long long data_generation = 0;

    // Changes on every submitted change *except* navigation -- 2D pan, zoom,
    // reset, the 2D Limits fields and tick overrides, and the 3D camera -- and
    // is what the window's LayoutStore re-measures on (v1.0 step 15.2). The
    // only layout effect navigation has is the width of the tick labels, and
    // re-measuring for that moves the frame under a drag. The exclusion is
    // opt-out: anything not recognised as navigation bumps it, so a missed
    // case costs one extra refit rather than a stale layout.
    unsigned long long layout_generation = 0;

    // Copied from FigureOptions::subplot_col_gap/row_gap so the render
    // thread never has to read Figure::Impl::opts directly. These separate whole subplots,
    // decorations included, not bare plot frames.
    float col_gap = 20.0f;
    float row_gap = 20.0f;

    // Border between the figure's edge and the subplot grid; see
    // FigureMargins.
    FigureMargins margins;

    // Figure::set_col_ratios()/set_row_ratios(): each column's and row's
    // weight in sharing out the grid (v1.0 step 15.3). Empty means equal;
    // read through grid_weights(), never directly.
    std::vector<float> col_ratios, row_ratios;

    // Whole-figure decoration (Figure::suptitle), as opposed to the per-axes
    // title/xtitle/ytitle in RenderSnapshot.
    std::string suptitle;
    SuptitleOptions suptitle_opts;
};

// One object's ask for a bar: the scale it explains, and the name it goes
// under. The name is a copy rather than a pointer because a request outlives
// nothing in particular -- it is built per frame from a snapshot and read by
// the layout, which is the only thing that holds one.
struct ColorbarRequest {
    Colormap    cmap = Colormap::Viridis;
    float       vmin = 0.0f, vmax = 1.0f;
    std::string name;
};

// A colorbar is a decoration any plot kind can opt into, so requests are
// looked up across every kind that carries the flag rather than hardcoded to
// heatmaps. **Every** object that asks for one gets one (v1.0 step 11.1):
// two scatter_z series coloured on different scales are two scales, and one
// bar explaining both of them would be a lie about one of them. Shared by the
// raster and SVG paths so the two cannot disagree on which bars an axes gets
// or on the order they sit in.
//
// Order is kind order, then index within a kind -- the order the cell places
// them left to right, outward from the frame.
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

// The 3D overload: a colorbar is *hoisted* to the cell, whether the object
// that asked for it is on a plane or is the axes' own. It has to be -- a
// colorbar is a decoration measured and drawn in pixels beside the frame, and
// a bar drawn into the scene would be foreshortened, would turn with the
// camera and would be occluded by the geometry it explains. So requests are
// carved out of the cell exactly as a 2D axes' own would be; nothing about it
// reaches the projector, which never learns anything asked for one.
//
// The axes' own kinds come first and the planes' after, which is the order
// step 11.5 pins for the legend's keys and for its reason: a plane is
// decoration carried into someone else's scene.
//
// `bar3d` is absent because it has no colormap -- the colour of a bar chart is
// not a scale, exactly as it is not a legend key.
inline std::vector<ColorbarRequest> find_colorbar_requests(const RenderSnapshot3D& snap) {
    std::vector<ColorbarRequest> reqs;
    for (const auto& sp : snap.surfaces) {
        // Gated on `colormap`, since with it off the sheet is one flat colour
        // and the bar would key a mapping the picture does not use. The range
        // is the *resolved* one: vmin == vmax means the surface's own heights,
        // and a bar labelled "0" to "0" would explain nothing.
        if (!sp.opts.colorbar || !sp.opts.colormap) continue;
        double lo = 0.0, hi = 1.0;
        surface_value_range(sp, lo, hi);
        reqs.push_back({ sp.opts.cmap, static_cast<float>(lo), static_cast<float>(hi),
                         sp.opts.name });
    }
    // A cloud is keyed by whichever colouring it has, and only a colormapped
    // one asks for a bar -- gated on *having* a `colors` vector rather than on
    // a flag, since that vector is the whole of what makes the series
    // colormapped. The range is the resolved one, for the surface's reason:
    // vmin == vmax means the series' own values, and a bar labelled "0" to "0"
    // explains nothing.
    for (const auto& sc : snap.scatter3d) {
        if (!sc.opts.colorbar || !sc.colormapped()) continue;
        double lo = 0.0, hi = 1.0;
        scatter3d_value_range(sc, lo, hi);
        reqs.push_back({ sc.opts.cmap, static_cast<float>(lo), static_cast<float>(hi),
                         sc.opts.name });
    }
    // A path, on the cloud's terms exactly: gated on *having* a `colors`
    // vector rather than on a flag, and spanning the resolved range.
    for (const auto& lp : snap.lines3d) {
        if (!lp.opts.colorbar || !lp.colormapped()) continue;
        double lo = 0.0, hi = 1.0;
        line3d_value_range(lp, lo, hi);
        reqs.push_back({ lp.opts.cmap, static_cast<float>(lo), static_cast<float>(hi),
                         lp.opts.name });
    }
    // A mesh, on the cloud's and the path's terms exactly: gated on *having* a
    // `colors` vector rather than on a flag -- a mesh has no `colormap` flag,
    // having no height axis to colour by -- and spanning the resolved range.
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
