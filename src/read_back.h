#pragma once
// Plot object <-> public data struct, shared by Axes, Plane2D and Axes3D: the
// read-back conversions here, the set_*_data() bodies in each source file.
#include "sextant/axes3d.h"
#include "object_remove.h"
#include "plot_objects.h"
#include "contour_plot.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace sextant::read_back {
    // A set_*_data() span, owned by the plot object from here on.
    template <typename T>
    std::vector<T> own(std::span<const T> s) { return std::vector<T>(s.begin(), s.end()); }

    // Whether a stored axis still holds exactly these values.
    template <typename T>
    bool same(const std::vector<T>& a, std::span<const T> b) { return std::ranges::equal(a, b); }

    // The i-th object of a kind; `who` names the getter or setter in the error.
    template <typename V>
    auto at(V& v, std::size_t i, const char* who) -> decltype(v[i]) {
        if (i >= v.size())
            throw std::out_of_range(std::string(who) + ": index " + std::to_string(i)
                                    + " out of range, count is " + std::to_string(v.size()));
        return v[i];
    }

    // set_<kind>_visible(): the flag, with a fresh visible_stamp so a Data-panel
    // style edit made before it keeps it (assign_plot_opts()).
    template <typename V>
    void set_visible(V& v, std::size_t i, bool visible, const char* who) {
        auto& p = at(v, i, who);
        visible_flag(p.opts) = visible;
        p.visible_stamp = next_snapshot_generation();
    }

    template <typename V>
    bool visible(const V& v, std::size_t i, const char* who) { return object_shown(at(v, i, who)); }

    // opts.zorder at a plotting call: finite, or it could not order anything.
    inline void check_zorder(double z, const char* who) {
        if (!std::isfinite(z))
            throw std::invalid_argument(std::string(who) + ": zorder must be finite");
    }

    // The per-kind work of zorder()/set_zorder(), outside any lambda (Clang 18 crashes on
    // `if constexpr (requires ...)` inside a capturing generic lambda). A kind without a
    // zorder (a text) gives nothing / false.
    template <class V>
    std::optional<double> zorder_at(const V& v, std::size_t i) {
        if constexpr (requires { v[0].opts.zorder; }) return v[i].opts.zorder;
        else return std::nullopt;
    }

    template <class V>
    bool store_zorder(V& v, std::size_t i, double z) {
        if constexpr (requires { v[0].opts.zorder; }) {
            v[i].opts.zorder = z;
            v[i].zorder_stamp = next_snapshot_generation();
            return true;
        } else {
            return false;
        }
    }

    // Axes::set_zorder() / Plane2D::set_zorder(): the object by id, with a fresh
    // zorder_stamp so a Data-panel style edit made before it keeps it
    // (assign_plot_opts()). A text, an unknown id or a stale one warns and gives
    // false. `H` is Axes::Impl or a plane's sheet.
    template <class H>
    bool set_zorder(H& holder, ObjectId id, double z, const char* who) {
        check_zorder(z, who);
        const auto at = holder.find_object(id);
        const bool done = at && with_kind(holder, at->kind, [index = at->index, z](auto& v) {
            return store_zorder(v, index, z);
        }, false);
        if (!done) object_remove::warn_missing(who, "object", id);
        return done;
    }

    template <class H>
    std::optional<double> zorder(const H& holder, ObjectId id, const char* who) {
        const auto at = holder.find_object(id);
        std::optional<double> z;
        if (at)
            z = with_kind(holder, at->kind, [index = at->index](const auto& v) {
                return zorder_at(v, index);
            }, std::optional<double>{});
        if (!z) object_remove::warn_missing(who, "object", id);
        return z;
    }

    // For set_*_data(): error bars, hint_labels, marker sizes and bar bases stay while the
    // point count (a grid's shape) is unchanged and are dropped otherwise, as
    // they would no longer line up. Then takes a fresh data_stamp.
    template <typename P>
    void keep_aligned(P& p, bool same_shape) {
        if (!same_shape) {
            if constexpr (requires { p.err; }) p.err = {};
            p.opts.hint_labels.clear();
            if constexpr (requires { p.sizes; }) p.sizes = std::vector<double>{};
            if constexpr (requires { p.bottom; }) p.bottom = std::vector<double>{};
        }
        p.data_stamp = next_snapshot_generation();
    }

    // Per-point marker sizes for `n` points (ScatterOptions::sizes): empty, or
    // exactly one per point. Values are not checked: NaN or <= 0 hides a point.
    inline void check_sizes(const std::vector<double>& sizes, std::size_t n, const char* who) {
        if (!sizes.empty() && sizes.size() != n)
            throw std::invalid_argument(std::string(who) + ": sizes must be empty or one per point (got "
                                        + std::to_string(sizes.size()) + " for "
                                        + std::to_string(n) + " points)");
    }

    // Bar bases for `n` bars (BarOptions::bottom): empty, one value for every
    // bar, or one per bar; returned one per bar (or empty).
    inline std::vector<double> take_bottom(std::vector<double> bottom, std::size_t n, const char* who) {
        if (bottom.size() == 1 && n != 1) return std::vector<double>(n, bottom[0]);
        if (!bottom.empty() && bottom.size() != n)
            throw std::invalid_argument(std::string(who) + ": bottom must be empty, one value or one per bar (got "
                                        + std::to_string(bottom.size()) + " for "
                                        + std::to_string(n) + " bars)");
        return bottom;
    }

    inline LineData     to_data(const LinePlot& p)     { return { p.x.get(), p.y.get() }; }
    inline ScatterData  to_data(const ScatterPlot& p)  {
        return { p.x.get(), p.y.get(), p.sizes.get() };
    }
    inline ScatterZData to_data(const ScatterZPlot& p) {
        return { p.x.get(), p.y.get(), p.z.get(), p.sizes.get() };
    }
    inline BarData      to_data(const BarPlot& p)      {
        return { p.centers.get(), p.heights.get(), p.bottom.get() };
    }

    inline HeatmapData to_data(const HeatmapPlot& p) {
        return { std::vector<double>(p.data.begin(), p.data.end()),
                 p.rows, p.cols, p.xrange, p.yrange, p.channels, p.x_edges.get(), p.y_edges.get() };
    }

    inline Bar3DData to_data(const Bar3DPlot& p) {
        return { p.orient, p.u.get(), p.v.get(), p.heights.get(), p.bottoms.get() };
    }

    inline SurfaceData to_data(const SurfacePlot& p) {
        return { p.orient, p.u.get(), p.v.get(), p.heights.get() };
    }

    inline SurfaceTriData to_data(const SurfaceTriPlot& p) {
        return { p.x.get(), p.y.get(), p.z.get(), p.tri.get(), p.colors.get() };
    }

    inline Scatter3DData to_data(const Scatter3DPlot& p) {
        return { p.x.get(), p.y.get(), p.z.get(), p.colors.get(), p.sizes.get() };
    }

    inline Line3DData to_data(const Line3DPlot& p) {
        return { p.x.get(), p.y.get(), p.z.get(), p.colors.get() };
    }

    // A fill's three columns, one value each per point.
    inline void check_fill(std::size_t nx, std::size_t n1, std::size_t n2, const char* who) {
        if (n1 != nx || n2 != nx)
            throw std::invalid_argument(std::string(who) + ": x, y1 and y2 must have the same length");
    }

    // FillOptions::exclude for `n` points, as the column the plot keeps (1 =
    // excluded): empty, or one per point.
    inline std::vector<double> take_exclude(const std::vector<bool>& exclude, std::size_t n,
                                            const char* who) {
        if (!exclude.empty() && exclude.size() != n)
            throw std::invalid_argument(std::string(who) + ": exclude must be empty or one per point (got "
                                        + std::to_string(exclude.size()) + " for "
                                        + std::to_string(n) + " points)");
        std::vector<double> out(exclude.size());
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = exclude[i] ? 1.0 : 0.0;
        return out;
    }

    inline FillData to_data(const FillPlot& p) {
        std::vector<bool> exclude(p.exclude.size());
        for (std::size_t i = 0; i < exclude.size(); ++i) exclude[i] = p.excluded(i);
        return { p.x.get(), p.y1.get(), p.y2.get(), std::move(exclude) };
    }

    // A quiver's anchors in one of two forms: x, y, u and v one value each per
    // arrow; or the grid, matplotlib's 1D X and Y against 2D U and V: x one value
    // per column, y one per row, u and v row-major, `u[i * cols + j]` at
    // `(x[j], y[i])`. The grid is expanded into one anchor per arrow (`x`, `y`);
    // the point form leaves them empty (`grid` false). The two forms agree only
    // on a 1x1 grid, which is the same arrow either way. Other lengths throw.
    struct QuiverAnchors {
        bool grid = false;
        std::vector<double> x, y;
    };

    inline QuiverAnchors quiver_anchors(std::span<const double> x, std::span<const double> y,
                                        std::size_t nu, std::size_t nv, const char* who) {
        QuiverAnchors a;
        if (y.size() == x.size() && nu == x.size() && nv == x.size()) return a;
        if (nv == nu && !x.empty() && !y.empty() && nu / x.size() == y.size() && nu % x.size() == 0) {
            a.grid = true;
            a.x.reserve(nu);
            a.y.reserve(nu);
            for (const double yi: y)
                for (const double xj: x) {
                    a.x.push_back(xj);
                    a.y.push_back(yi);
                }
            return a;
        }
        throw std::invalid_argument(std::string(who) + ": x, y, u and v must have the same length, or u and v "
                                    "one value per grid point (x.size() * y.size()) for x per column and y per "
                                    "row (got " + std::to_string(x.size()) + ", " + std::to_string(y.size()) + ", "
                                    + std::to_string(nu) + ", " + std::to_string(nv) + ")");
    }

    // A quiver's colour values: none, or one per arrow.
    inline void check_quiver_c(std::size_t nc, std::size_t n, const char* who) {
        if (nc != 0 && nc != n)
            throw std::invalid_argument(std::string(who) + ": c must be empty or one per arrow (got "
                                        + std::to_string(nc) + " for " + std::to_string(n) + " arrows)");
    }

    inline Quiver3DData to_data(const Quiver3DPlot& p) {
        return { p.x.get(), p.y.get(), p.z.get(), p.u.get(), p.v.get(), p.w.get(), p.c.get() };
    }

    inline QuiverData to_data(const QuiverPlot& p) {
        return { p.x.get(), p.y.get(), p.u.get(), p.v.get(), p.c.get() };
    }

    inline AxLineData to_data(const AxLinePlot& p) {
        return { p.value, p.mode, p.x, p.y, p.x2, p.y2, p.slope };
    }

    // A sloped line's numbers: finite, and for two points not the same point.
    inline void check_axline_sloped(const AxLineData& d, const char* who) {
        const bool two = d.mode == AxLineMode::TwoPoints;
        for (double v : {d.x.v, d.y.v, d.slope}) {
            if (!std::isfinite(v))
                throw std::invalid_argument(std::string(who) + ": the point and slope must be finite");
        }
        if (two) {
            if (!std::isfinite(d.x2.v) || !std::isfinite(d.y2.v))
                throw std::invalid_argument(std::string(who) + ": the points must be finite");
            if (d.x.space == d.x2.space && d.y.space == d.y2.space && d.x.v == d.x2.v && d.y.v == d.y2.v)
                throw std::invalid_argument(std::string(who) + ": the two points must differ");
        }
    }
    inline AxSpanData to_data(const AxSpanPlot& p) { return { p.lo, p.hi }; }

    // A polygon: one y per x, at least three vertices, all finite.
    inline void check_poly(std::span<const double> x, std::span<const double> y, const char* who) {
        if (x.size() != y.size())
            throw std::invalid_argument(std::string(who) + ": x and y must have the same length");
        if (x.size() < 3)
            throw std::invalid_argument(std::string(who) + ": a polygon needs at least 3 vertices");
        for (std::size_t i = 0; i < x.size(); ++i)
            if (!std::isfinite(x[i]) || !std::isfinite(y[i]))
                throw std::invalid_argument(std::string(who) + ": vertices must be finite");
    }

    // A heatmap_tri()'s vertices: x and y finite, values one per vertex (NaN
    // allowed: it leaves out the vertex's triangles), at least three.
    inline void check_tri_vertices(std::span<const double> x, std::span<const double> y,
                                   std::span<const double> values, const char* who) {
        const std::string w = who;
        if (y.size() != x.size() || values.size() != x.size())
            throw std::invalid_argument(w + ": x, y and values must be the same length");
        if (x.size() < 3)
            throw std::invalid_argument(w + ": a triangle mesh needs at least three vertices");
        for (std::size_t i = 0; i < x.size(); ++i)
            if (!std::isfinite(x[i]) || !std::isfinite(y[i]))
                throw std::invalid_argument(w + ": x and y must be finite");
    }

    // Explicit topology over `n` vertices, as Axes3D::surface_tri() checks it.
    inline void check_tri_indices(std::span<const std::uint32_t> tri, std::size_t n, const char* who) {
        const std::string w = who;
        if (tri.empty())
            throw std::invalid_argument(w + ": tri must name at least one triangle");
        if (tri.size() % 3 != 0)
            throw std::invalid_argument(w + ": tri must be a multiple of three -- three vertex "
                                            "indices per triangle, row-major");
        for (const std::uint32_t i : tri)
            if (static_cast<std::size_t>(i) >= n)
                throw std::invalid_argument(w + ": every index in tri must be less than the vertex count");
    }

    // A circle's or wedge's centre and radius: finite, r > 0, and a Data
    // radius (measured along the axes) needs a Data centre.
    inline void check_round(const Pos& cx, const Pos& cy, const Pos& r, const char* who) {
        if (!std::isfinite(cx.v) || !std::isfinite(cy.v) || !std::isfinite(r.v))
            throw std::invalid_argument(std::string(who) + ": centre and radius must be finite");
        if (!(r.v > 0.0))
            throw std::invalid_argument(std::string(who) + ": the radius must be positive");
        if (r.space == Coords::Data && (cx.space != Coords::Data || cy.space != Coords::Data))
            throw std::invalid_argument(std::string(who) + ": a radius in data units needs a centre in data units");
    }

    // A wedge: a round shape, finite angles, and a hole in [0, r) (in r's space).
    inline void check_wedge(const Pos& cx, const Pos& cy, const Pos& r, double theta1, double theta2,
                            double inner, const char* who) {
        check_round(cx, cy, r, who);
        if (!std::isfinite(theta1) || !std::isfinite(theta2) || !std::isfinite(inner))
            throw std::invalid_argument(std::string(who) + ": angles and the inner radius must be finite");
        if (inner < 0.0 || inner >= r.v)
            throw std::invalid_argument(std::string(who) + ": the inner radius must be in [0, r)");
    }

    inline PolyData   to_data(const PolyPlot& p)   { return { p.x.get(), p.y.get() }; }
    inline HeatmapTriData to_data(const HeatmapTriPlot& p) {
        return { p.x.get(), p.y.get(), p.values.get(), p.tri.get() };
    }
    inline ContourData to_data(const ContourPlot& p) {
        return { p.x.get(), p.y.get(), p.z.get(), p.geom ? p.geom->levels : std::vector<double>{}, p.opts.filled };
    }
    inline CircleData to_data(const CirclePlot& p) { return { p.cx, p.cy, p.r }; }
    inline WedgeData  to_data(const WedgePlot& p)  {
        return { p.cx, p.cy, p.r, p.theta1, p.theta2, p.inner };
    }

    // A reference line's or span's numbers (value or ends, `from`, `to`) must
    // be finite.
    inline void check_reference(std::initializer_list<double> vs, const char* who) {
        for (double v : vs)
            if (!std::isfinite(v))
                throw std::invalid_argument(std::string(who) + ": values, from and to must be finite");
    }

    // A text, as Axes and Axes3D read it.
    inline TextData to_data(const TextPlot& p) {
        const TextContent& c = p.content;
        return { c.text, c.x, c.y, c.arrow, c.px, c.py };
    }

    inline Text3DData to_data3d(const TextPlot& p) {
        const TextContent& c = p.content;
        return { c.text, c.x.v, c.y.v, c.z, c.x.space == Coords::Fraction, c.arrow, c.dx, c.dy };
    }

    inline TextContent content_of(const TextData& d) {
        TextContent c;
        c.text = d.text;
        c.x = d.x;
        c.y = d.y;
        c.arrow = d.arrow;
        c.px = d.px;
        c.py = d.py;
        return c;
    }

    // `who` names the setter in the error.
    inline TextContent content_of(const Text3DData& d, const char* who) {
        if (d.arrow && d.in_frame)
            throw std::invalid_argument(std::string(who) + ": a text with an arrow is placed in "
                                        "the scene, not in the frame (arrow and in_frame both set)");
        TextContent c;
        c.text = d.text;
        c.x = d.in_frame ? Pos::fraction(d.x) : Pos(d.x);
        c.y = d.in_frame ? Pos::fraction(d.y) : Pos(d.y);
        c.z = d.z;
        c.arrow = d.arrow;
        c.dx = d.dx;
        c.dy = d.dy;
        return c;
    }

    // Every number a text is placed by must be finite.
    inline void check_text(const TextContent& c, const char* who) {
        for (double v : { c.x.v, c.y.v, c.z, c.px, c.py, c.dx, c.dy })
            if (!std::isfinite(v))
                throw std::invalid_argument(std::string(who) + ": text coordinates must be finite");
    }
} // namespace sextant::read_back
