#pragma once
// Axes::fill() as regions, shared by the GL path, the SVG writer, plane
// geometry, the limits and the tests, so every output fills the same area.
//
// A region is a run: a maximal range of points neither excluded nor holding a
// NaN (nor, on a log run axis, at or below 0). On a log value axis a curve at or
// below 0 does not break the run: AxisScale::fwd() puts it far below any view,
// so the fill reaches past the frame's low edge, as matplotlib's clip does.
// Each run is worked out in scale space, where the outputs draw straight
// segments: the steps (in data units, as a line's), then, with `interpolate`,
// the crossing of the curves towards an excluded neighbour.
#include "axis_scale.h"
#include "line_steps.h"
#include "plot_objects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sextant {

// One region in scale space: along it `r` (the run coordinate), across it the
// curves `a` (y1) and `b` (y2), point by point.
struct FillRun {
    std::vector<double> r, a, b;

    std::size_t size() const { return r.size(); }
};

namespace fill_detail {
    // Point i has a place: finite numbers, its run coordinate valid on `rs`.
    inline bool placed(const FillPlot& p, std::size_t i, const AxisScale& rs) {
        return std::isfinite(p.x[i]) && std::isfinite(p.y1[i]) && std::isfinite(p.y2[i])
               && rs.valid(p.x[i]);
    }

    inline std::size_t point_count(const FillPlot& p) {
        return std::min({p.x.size(), p.y1.size(), p.y2.size()});
    }
} // namespace fill_detail

// Calls fn(begin, end) for each run of points [begin, end), lone points
// included (they fill nothing but count towards limits).
template <class F>
void for_each_fill_run(const FillPlot& p, const AxisScale& rs, F&& fn) {
    const std::size_t n = fill_detail::point_count(p);
    std::size_t i = 0;
    while (i < n) {
        if (p.excluded(i) || !fill_detail::placed(p, i, rs)) {
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < n && !p.excluded(j) && fill_detail::placed(p, j, rs)) ++j;
        fn(i, j);
        i = j;
    }
}

// Where the curves cross between run end `i` and its neighbour `j`, in scale
// space through rs/vs: only towards an excluded neighbour that has a place, and
// only when y1 - y2 changes sign across the pair. Writes (r, v) and returns
// true when there is such a point.
inline bool fill_crossing(const FillPlot& p, std::size_t i, std::size_t j, const AxisScale& rs,
                          const AxisScale& vs, double& r, double& v) {
    if (!p.excluded(j) || !fill_detail::placed(p, j, rs)) return false;
    const double ri = rs.fwd(p.x[i]), rj = rs.fwd(p.x[j]);
    const double ai = vs.fwd(p.y1[i]), aj = vs.fwd(p.y1[j]);
    const double di = ai - vs.fwd(p.y2[i]), dj = aj - vs.fwd(p.y2[j]);
    if (!(di * dj < 0.0)) return false;
    const double t = di / (di - dj);
    r = ri + t * (rj - ri);
    v = ai + t * (aj - ai);
    return std::isfinite(r) && std::isfinite(v);
}

// The regions of `p` in scale space: `rs` maps the run coordinate, `vs` the
// curves (the y axis' and x axis' scales for a Horizontal fill, swapped for a
// Vertical one; identities on a plane). Runs of fewer than two points are left
// out: they enclose nothing.
inline std::vector<FillRun> fill_runs(const FillPlot& p, const AxisScale& rs, const AxisScale& vs) {
    std::vector<FillRun> out;
    const std::size_t n = fill_detail::point_count(p);
    std::vector<double> sx, s1, sx2, s2;
    for_each_fill_run(p, rs, [&](std::size_t b, std::size_t e) {
        if (e - b < 2) return;
        FillRun run;
        double cr = 0.0, cv = 0.0;
        const bool lead = p.opts.interpolate && b > 0 && fill_crossing(p, b, b - 1, rs, vs, cr, cv);
        if (lead) {
            run.r.push_back(cr);
            run.a.push_back(cv);
            run.b.push_back(cv);
        }
        // Both curves through the same expansion: the x it emits depends on x
        // alone, so sx and sx2 agree.
        step_vertices(p.x.data() + b, p.y1.data() + b, e - b, p.opts.drawstyle, false, sx, s1);
        step_vertices(p.x.data() + b, p.y2.data() + b, e - b, p.opts.drawstyle, false, sx2, s2);
        for (std::size_t k = 0; k < sx.size(); ++k) {
            run.r.push_back(rs.fwd(sx[k]));
            run.a.push_back(vs.fwd(s1[k]));
            run.b.push_back(vs.fwd(s2[k]));
        }
        if (p.opts.interpolate && e < n && fill_crossing(p, e - 1, e, rs, vs, cr, cv)) {
            run.r.push_back(cr);
            run.a.push_back(cv);
            run.b.push_back(cv);
        }
        out.push_back(std::move(run));
    });
    return out;
}

// The triangles covering one run, as emit(r0, v0, r1, v1, r2, v2). Each
// interval between neighbouring points gives two; where y1 - y2 changes sign
// inside it, the two meet at the crossing instead. They never overlap, so a
// translucent fill blends once everywhere (the shared edges are covered once).
template <class F>
void fill_triangles(const FillRun& run, F&& emit) {
    for (std::size_t k = 0; k + 1 < run.size(); ++k) {
        const double r0 = run.r[k], a0 = run.a[k], b0 = run.b[k];
        const double r1 = run.r[k + 1], a1 = run.a[k + 1], b1 = run.b[k + 1];
        const double d0 = a0 - b0, d1 = a1 - b1;
        if (d0 * d1 < 0.0) {
            const double t = d0 / (d0 - d1);
            const double rc = r0 + t * (r1 - r0), vc = a0 + t * (a1 - a0);
            emit(r0, a0, r0, b0, rc, vc);
            emit(rc, vc, r1, a1, r1, b1);
        } else {
            emit(r0, a0, r1, a1, r1, b1);
            emit(r0, a0, r1, b1, r0, b0);
        }
    }
}

// The closed outline of one run: y1 forward, then y2 back, as emit(r, v) per
// vertex; the caller closes it back to the first.
template <class F>
void fill_outline(const FillRun& run, F&& emit) {
    for (std::size_t k = 0; k < run.size(); ++k) emit(run.r[k], run.a[k]);
    for (std::size_t k = run.size(); k-- > 0;) emit(run.r[k], run.b[k]);
}

// The outline color: `edgecolor`, else the fill's.
inline Color fill_edge(const FillPlot& p) { return p.opts.edgecolor.value_or(*p.opts.color); }

// The points a fill gives the automatic limits, as add(r, v) in scale space
// through rs/vs: every point of every run (lone ones included) on both curves,
// and with `interpolate` the crossings the runs reach. A curve value `vs` has
// no place for (<= 0 on log) is passed as NaN, so it widens nothing; add()
// must skip NaN. A crossing counts only when all four of its values have a
// place (otherwise it lies among the far-below values, in no view).
template <class Add>
void fill_limit_points(const FillPlot& p, const AxisScale& rs, const AxisScale& vs, Add&& add) {
    const std::size_t n = fill_detail::point_count(p);
    auto values_placed = [&](std::size_t i) { return vs.valid(p.y1[i]) && vs.valid(p.y2[i]); };
    for_each_fill_run(p, rs, [&](std::size_t b, std::size_t e) {
        for (std::size_t i = b; i < e; ++i) {
            const double r = rs.fwd(p.x[i]);
            add(r, vs.fwd_strict(p.y1[i]));
            add(r, vs.fwd_strict(p.y2[i]));
        }
        if (!p.opts.interpolate || e - b < 2) return;
        double cr = 0.0, cv = 0.0;
        if (b > 0 && values_placed(b) && values_placed(b - 1)
            && fill_crossing(p, b, b - 1, rs, vs, cr, cv))
            add(cr, cv);
        if (e < n && values_placed(e - 1) && values_placed(e)
            && fill_crossing(p, e - 1, e, rs, vs, cr, cv))
            add(cr, cv);
    });
}

} // namespace sextant
