#pragma once
// Axes::shape_poly()/shape_circle()/shape_wedge() as pixel geometry, shared by
// the GL path, the SVG writer, plane geometry, the hit test, the limits and the
// tests, so every output fills the same area (the fill_geometry.h pattern).
//
// Raw values in, pixels out (y down): the plot structs only carry them. A
// shape comes back as filled triangles plus its closed outline rings.
//
// Data-space points go through the axis transform, so a circle given in data
// units is an ellipse on screen when the axes' pixels per unit differ, and
// bends on a log or symlog axis. A value at or below 0 on a log axis lands far
// past the frame's low edge (AxisScale::fwd()'s clip, as a fill's does), so
// the shape runs off the frame there instead of breaking.
#include "coord_transform.h"
#include "shape_limits.h"
#include "sextant/style.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

namespace sextant {

struct ShapePt {
    float x, y;
};

struct ShapeGeom {
    std::vector<ShapePt> tris;                // three per triangle
    std::vector<std::vector<ShapePt>> rings;  // closed outlines (the last point joins the first)

    bool empty() const { return tris.empty(); }
};

namespace shape_detail {
    inline float px_of(const Pos& p, const CoordTransform& tr) { return pos_to_px(p, tr); }
    inline float py_of(const Pos& p, const CoordTransform& tr) { return pos_to_py(p, tr); }

    // A radius that is a length on screen (Pixels, or a Fraction of the
    // frame's shorter side).
    inline double length_px(const Pos& r, const CoordTransform& tr) {
        return r.space == Coords::Pixels ? r.v : r.v * std::min(tr.pw, tr.ph);
    }

    // Segments of an arc of `sweep` degrees at about `radius_px`: a chord at
    // most ~0.1 px off the curve, never fewer than one per 45 degrees.
    inline int arc_segments(double sweep, double radius_px) {
        const double r = std::clamp(std::isfinite(radius_px) ? radius_px : 1.0, 1.0, 1e5);
        const double step = 2.0 * std::acos(1.0 - std::min(0.1 / r, 1.0));
        const double n = std::ceil(sweep * kPi / 180.0 / std::max(step, 1e-6));
        return static_cast<int>(std::clamp(n, std::ceil(sweep / 45.0), 1440.0));
    }

    inline Pos pos_in(double v, Coords space) {
        Pos p(v);
        p.space = space;
        return p;
    }

    inline bool finite_pt(const ShapePt& p) { return std::isfinite(p.x) && std::isfinite(p.y); }

    inline double cross(const ShapePt& a, const ShapePt& b, const ShapePt& c) {
        return (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y)
               - (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
    }

    // Strictly inside triangle abc (a, b, c counter-clockwise by cross() > 0).
    inline bool strictly_inside(const ShapePt& p, const ShapePt& a, const ShapePt& b, const ShapePt& c) {
        return cross(a, b, p) > 0.0 && cross(b, c, p) > 0.0 && cross(c, a, p) > 0.0;
    }

    // Ear clipping into `out`, three vertex indices of `p` per triangle. O(n^3)
    // worst case, simple polygons only (a self-intersecting one still
    // terminates, its fill is unspecified); poly_geometry() caches the result
    // for as long as the view leaves it valid.
    // shortcut: no sweep triangulation, so the first clip of a polygon of
    // tens of thousands of vertices is slow; upgrade when those matter.
    inline void ear_clip(const std::vector<ShapePt>& p, std::vector<std::uint32_t>& out) {
        std::vector<std::size_t> idx(p.size());
        std::iota(idx.begin(), idx.end(), std::size_t{0});
        double area2 = 0.0;
        for (std::size_t i = 0; i < p.size(); ++i) {
            const ShapePt &a = p[i], &b = p[(i + 1) % p.size()];
            area2 += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
        }
        // The loop wants the winding in which a convex corner has cross() > 0,
        // which is the sign of this shoelace sum.
        if (area2 < 0.0) std::reverse(idx.begin(), idx.end());

        const auto emit = [&](std::size_t a, std::size_t b, std::size_t c) {
            out.push_back(static_cast<std::uint32_t>(a));
            out.push_back(static_cast<std::uint32_t>(b));
            out.push_back(static_cast<std::uint32_t>(c));
        };
        while (idx.size() > 3) {
            const std::size_t m = idx.size();
            bool clipped = false;
            for (std::size_t k = 0; k < m && !clipped; ++k) {
                const std::size_t i0 = idx[(k + m - 1) % m], i1 = idx[k], i2 = idx[(k + 1) % m];
                if (cross(p[i0], p[i1], p[i2]) <= 0.0) continue;   // reflex or flat
                bool ear = true;
                for (std::size_t q : idx)
                    if (q != i0 && q != i1 && q != i2 && strictly_inside(p[q], p[i0], p[i1], p[i2])) {
                        ear = false;
                        break;
                    }
                if (!ear) continue;
                emit(i0, i1, i2);
                idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(k));
                clipped = true;
            }
            if (clipped) continue;
            // No ear: drop a flat vertex, else (self-intersecting) fan what is left.
            bool dropped = false;
            for (std::size_t k = 0; k < m; ++k)
                if (cross(p[idx[(k + m - 1) % m]], p[idx[k]], p[idx[(k + 1) % m]]) == 0.0) {
                    idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(k));
                    dropped = true;
                    break;
                }
            if (dropped) continue;
            for (std::size_t k = 1; k + 1 < m; ++k) emit(idx[0], idx[k], idx[k + 1]);
            return;
        }
        if (idx.size() == 3 && cross(p[idx[0]], p[idx[1]], p[idx[2]]) != 0.0) emit(idx[0], idx[1], idx[2]);
    }

} // namespace shape_detail

// A polygon whose vertices are (x[i], y[i]) in `space`, closed implicitly.
// Consecutive duplicates (and a repeated first point at the end) are dropped;
// fewer than three distinct vertices give nothing.
//
// `cache` (the plot's, may be null) keeps the ear clipping between calls. Where
// the map to pixels is affine (a Fraction or Pixels polygon, or Data on two
// linear axes) the clipping holds through pan and zoom; on a bent axis it holds
// for the same view only. It is reused only when the vertices match exactly.
inline ShapeGeom poly_geometry(std::span<const double> x, std::span<const double> y, Coords space,
                               const CoordTransform& tr, PolyTriCache* cache = nullptr) {
    using namespace shape_detail;
    ShapeGeom g;
    std::vector<ShapePt> p;
    std::vector<std::uint32_t> kept;
    const std::size_t n = std::min(x.size(), y.size());
    for (std::size_t i = 0; i < n; ++i) {
        const Pos X = pos_in(x[i], space), Y = pos_in(y[i], space);
        const ShapePt q{px_of(X, tr), py_of(Y, tr)};
        if (!finite_pt(q)) return {};
        if (!p.empty() && p.back().x == q.x && p.back().y == q.y) continue;
        p.push_back(q);
        kept.push_back(static_cast<std::uint32_t>(i));
    }
    while (p.size() > 1 && p.front().x == p.back().x && p.front().y == p.back().y) {
        p.pop_back();
        kept.pop_back();
    }
    if (p.size() < 3) return {};

    std::vector<double> key{static_cast<double>(space)};
    if (space == Coords::Data && !(tr.xs.linear() && tr.ys.linear()))
        key.insert(key.end(), {static_cast<double>(tr.xs.kind), tr.xs.base, tr.xs.linthresh, tr.xs.linscale,
                               static_cast<double>(tr.ys.kind), tr.ys.base, tr.ys.linthresh, tr.ys.linscale,
                               tr.xmin, tr.xmax, tr.ymin, tr.ymax, tr.px, tr.py, tr.pw, tr.ph});
    const auto raw_same = [&](const std::vector<double>& raw) {
        return raw.size() == 2 * n && std::equal(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(n), raw.begin())
               && std::equal(y.begin(), y.begin() + static_cast<std::ptrdiff_t>(n), raw.begin() + static_cast<std::ptrdiff_t>(n));
    };
    std::vector<std::uint32_t> tris;
    bool hit = false;
    if (cache) {
        std::lock_guard lock(cache->m);
        if (cache->key == key && cache->kept == kept && raw_same(cache->raw)) {
            tris = cache->tris;
            hit = true;
        }
    }
    if (!hit) {
        ear_clip(p, tris);
        if (cache) {
            std::lock_guard lock(cache->m);
            cache->key = std::move(key);
            cache->kept = kept;
            cache->raw.assign(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(n));
            cache->raw.insert(cache->raw.end(), y.begin(), y.begin() + static_cast<std::ptrdiff_t>(n));
            cache->tris = tris;
        }
    }
    if (tris.empty()) return {};
    g.tris.reserve(tris.size());
    for (const std::uint32_t i : tris) g.tris.push_back(p[i]);
    g.rings.push_back(std::move(p));
    return g;
}

// A sector of the annulus between `inner` and `r` around (cx, cy), from
// theta1 to theta2 degrees counter-clockwise from +x; (0, 360) with inner 0 is
// a disc. `inner` is in the space of `r`, and a Data radius needs a Data centre
// (the callers check): then the arc is sampled in data units, x = cx + r cos t,
// y = cy + r sin t, so it is an ellipse on unequal axes. Otherwise the
// radius is a length in pixels and the arc is round.
inline ShapeGeom wedge_geometry(Pos cx, Pos cy, Pos r, double theta1, double theta2, Pos inner,
                                const CoordTransform& tr) {
    using namespace shape_detail;
    ShapeGeom g;
    const double sweep = sweep_of(theta1, theta2);
    if (!(sweep > 0.0) || !(r.v > 0.0) || !(inner.v >= 0.0) || !(inner.v < r.v)) return g;
    const bool full = sweep >= 360.0;
    const bool in_data = r.space == Coords::Data;

    // The point at angle `a` (degrees) on the circle of radius `rad` in r's space.
    const ShapePt c{px_of(cx, tr), py_of(cy, tr)};
    if (!finite_pt(c)) return g;
    double rpx = 0.0;   // for the segment count
    if (in_data) {
        rpx = std::max(std::fabs(tr.to_px(cx.v + r.v) - tr.to_px(cx.v)),
                       std::fabs(tr.to_py(cy.v + r.v) - tr.to_py(cy.v)));
    } else {
        rpx = length_px(r, tr);
    }
    const int n = arc_segments(sweep, rpx);
    const auto at = [&](double rad, int k) {
        const double a = (theta1 + sweep * k / n) * kPi / 180.0;
        if (in_data) return ShapePt{tr.to_px(cx.v + rad * std::cos(a)), tr.to_py(cy.v + rad * std::sin(a))};
        const double len = length_px(pos_in(rad, r.space), tr);
        return ShapePt{c.x + static_cast<float>(len * std::cos(a)), c.y - static_cast<float>(len * std::sin(a))};
    };

    std::vector<ShapePt> outer, in;
    const int pts = full ? n : n + 1;   // a full turn's last point is its first
    for (int k = 0; k < pts; ++k) {
        outer.push_back(at(r.v, k));
        if (inner.v > 0.0) in.push_back(at(inner.v, k));
    }
    for (const ShapePt& q : outer)
        if (!finite_pt(q)) return {};
    for (const ShapePt& q : in)
        if (!finite_pt(q)) return {};

    const std::size_t m = outer.size();
    if (inner.v <= 0.0) {
        // A fan from the centre.
        const std::size_t last = full ? m : m - 1;
        for (std::size_t k = 0; k < last; ++k) {
            g.tris.insert(g.tris.end(), {c, outer[k], outer[(k + 1) % m]});
        }
        std::vector<ShapePt> ring = outer;
        if (!full) ring.insert(ring.begin(), c);
        g.rings.push_back(std::move(ring));
    } else {
        // A strip between the two arcs.
        const std::size_t last = full ? m : m - 1;
        for (std::size_t k = 0; k < last; ++k) {
            const std::size_t j = (k + 1) % m;
            g.tris.insert(g.tris.end(), {outer[k], outer[j], in[k]});
            g.tris.insert(g.tris.end(), {outer[j], in[j], in[k]});
        }
        if (full) {
            g.rings.push_back(std::move(outer));
            g.rings.push_back(std::move(in));
        } else {
            std::vector<ShapePt> ring = std::move(outer);
            ring.insert(ring.end(), in.rbegin(), in.rend());
            g.rings.push_back(std::move(ring));
        }
    }
    return g;
}

inline ShapeGeom circle_geometry(Pos cx, Pos cy, Pos r, const CoordTransform& tr) {
    return wedge_geometry(cx, cy, r, 0.0, 360.0, shape_detail::pos_in(0.0, r.space), tr);
}

// The three plot kinds as geometry: what the GL path, the SVG writer, plane
// layout and the hit test all call.
inline ShapeGeom shape_geometry(const PolyPlot& p, const CoordTransform& tr) {
    return poly_geometry(p.x.get(), p.y.get(), p.opts.space, tr, p.tri.get());
}

inline ShapeGeom shape_geometry(const CirclePlot& p, const CoordTransform& tr) {
    return circle_geometry(p.cx, p.cy, p.r, tr);
}

inline ShapeGeom shape_geometry(const WedgePlot& p, const CoordTransform& tr) {
    return wedge_geometry(p.cx, p.cy, p.r, p.theta1, p.theta2, shape_detail::pos_in(p.inner, p.r.space), tr);
}

// Any of the three's outline color: `edgecolor`, else the fill's, faded by `alpha`.
template <class Shape>
inline Color shape_edge(const Shape& p) {
    Color c = p.opts.edgecolor.value_or(*p.opts.color);
    c.a *= std::clamp(p.opts.alpha, 0.0f, 1.0f);
    return c;
}

// Is the pixel inside the filled triangles (the hit test).
inline bool shape_contains(const ShapeGeom& g, float x, float y) {
    using namespace shape_detail;
    const ShapePt p{x, y};
    for (std::size_t i = 0; i + 2 < g.tris.size(); i += 3) {
        const ShapePt &a = g.tris[i], &b = g.tris[i + 1], &c = g.tris[i + 2];
        const double d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
        const bool neg = d1 < 0.0 || d2 < 0.0 || d3 < 0.0, pos = d1 > 0.0 || d2 > 0.0 || d3 > 0.0;
        if (!(neg && pos)) return true;
    }
    return false;
}

} // namespace sextant
