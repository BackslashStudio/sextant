#pragma once
// Plot object <-> public data struct, shared by Axes, Plane2D and Axes3D: the
// read-back conversions here, the set_*_data() bodies in each source file.
#include "sextant/axes3d.h"
#include "plot_objects.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
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
                 p.rows, p.cols, p.xrange, p.yrange };
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

    // A quiver's four columns, one value each per arrow.
    inline void check_quiver(std::size_t nx, std::size_t ny, std::size_t nu, std::size_t nv,
                             const char* who) {
        if (ny != nx || nu != nx || nv != nx)
            throw std::invalid_argument(std::string(who) + ": x, y, u and v must have the same length");
    }

    // A quiver's colour values: none, or one per arrow.
    inline void check_quiver_c(std::size_t nc, std::size_t n, const char* who) {
        if (nc != 0 && nc != n)
            throw std::invalid_argument(std::string(who) + ": c must be empty or one per arrow (got "
                                        + std::to_string(nc) + " for " + std::to_string(n) + " arrows)");
    }

    inline QuiverData to_data(const QuiverPlot& p) {
        return { p.x.get(), p.y.get(), p.u.get(), p.v.get(), p.c.get() };
    }

    inline AxLineData to_data(const AxLinePlot& p) { return { p.value }; }
    inline AxSpanData to_data(const AxSpanPlot& p) { return { p.lo, p.hi }; }

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
