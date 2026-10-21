#pragma once
// Axes::axline()/axspan() in pixels, shared by the GL path, the SVG writer and
// the tests, so every output draws the same geometry. Nothing is cached: a
// line is two points and a span one rectangle, worked out per frame from the
// frame's transform (the data side through it, the other side from the
// options' frame fractions).
#include "coord_transform.h"
#include "plot_objects.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace sextant {

// A line's two ends in pixels (y down).
struct RefSegment {
    float x0, y0, x1, y1;
};

// A span's rectangle in pixels, x0 <= x1 and y0 <= y1.
struct RefRect {
    float x0, y0, x1, y1;
};

namespace reference_detail {
    // A frame fraction along x (0 = left) or y (0 = bottom), in pixels.
    inline float frame_x(const CoordTransform& tr, double f) {
        return tr.px + static_cast<float>(f) * tr.pw;
    }
    inline float frame_y(const CoordTransform& tr, double f) {
        return tr.py + tr.ph - static_cast<float>(f) * tr.ph;
    }
} // namespace reference_detail

// Where an axline() is drawn; nothing when its value has no place on its axis'
// scale (<= 0 on log) or lands at no finite pixel. Clipping is the caller's
// (the frame's scissor or clip path).
inline std::optional<RefSegment> axline_segment(const AxLinePlot& p, const CoordTransform& tr) {
    using namespace reference_detail;
    const AxisScale& s = p.on_x() ? tr.xs : tr.ys;
    if (!s.valid(p.value)) return std::nullopt;
    RefSegment g{};
    if (p.on_x()) {
        const float x = tr.to_px(p.value);
        g = {x, frame_y(tr, p.opts.from), x, frame_y(tr, p.opts.to)};
    } else {
        const float y = tr.to_py(p.value);
        g = {frame_x(tr, p.opts.from), y, frame_x(tr, p.opts.to), y};
    }
    if (!std::isfinite(g.x0) || !std::isfinite(g.y0) || !std::isfinite(g.x1) || !std::isfinite(g.y1))
        return std::nullopt;
    return g;
}

// A point in pixels (y down).
struct RefPt {
    float x, y;
};
using RefPath = std::vector<RefPt>;

namespace reference_detail {
    // The infinite line through `a` along (dx, dy), cut to the frame plus a few
    // pixels (caps and joins stay out of sight); nothing for no direction or a
    // line that misses it.
    inline std::optional<RefSegment> clip_line(RefPt a, double dx, double dy, const CoordTransform& tr) {
        constexpr double kM = 4.0;
        const double lo_x = tr.px - kM, hi_x = tr.px + tr.pw + kM;
        const double lo_y = tr.py - kM, hi_y = tr.py + tr.ph + kM;
        double t0 = -std::numeric_limits<double>::infinity(), t1 = std::numeric_limits<double>::infinity();
        const auto slab = [&](double p0, double d, double lo, double hi) {
            if (d == 0.0) return p0 >= lo && p0 <= hi;
            double ta = (lo - p0) / d, tb = (hi - p0) / d;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            return t0 <= t1;
        };
        if (!slab(a.x, dx, lo_x, hi_x) || !slab(a.y, dy, lo_y, hi_y)) return std::nullopt;
        if (!std::isfinite(t0) || !std::isfinite(t1)) return std::nullopt;
        return RefSegment{static_cast<float>(a.x + t0 * dx), static_cast<float>(a.y + t0 * dy),
                          static_cast<float>(a.x + t1 * dx), static_cast<float>(a.y + t1 * dy)};
    }

    // A curve u -> pixel point (nothing where the data has no place), as runs of
    // points: sampled across [u0, u1], each gap subdivided until its midpoint is
    // within 0.2 px of the chord, and bisected to the edge where the curve
    // enters or leaves the scale's domain.
    template <class F>
    std::vector<RefPath> sample_runs(F&& f, double u0, double u1) {
        constexpr int kInitial = 64, kMaxDepth = 10, kMaxPoints = 4000;
        std::vector<RefPath> runs;
        RefPath cur;
        const auto end_run = [&] {
            if (cur.size() >= 2) runs.push_back(std::move(cur));
            cur.clear();
        };

        // Appends the points between samples a and b (both valid), b included.
        const auto refine = [&](auto&& self, double ua, const RefPt& pa, double ub, const RefPt& pb, int depth) -> void {
            if (depth < kMaxDepth && static_cast<int>(cur.size()) < kMaxPoints) {
                const double um = 0.5 * (ua + ub);
                if (const std::optional<RefPt> pm = f(um)) {
                    const double bx = pb.x - pa.x, by = pb.y - pa.y, len = std::hypot(bx, by);
                    const double dev = len > 0.0 ? std::fabs((pm->x - pa.x) * by - (pm->y - pa.y) * bx) / len
                                                 : std::hypot(pm->x - pa.x, pm->y - pa.y);
                    if (dev > 0.2) {
                        self(self, ua, pa, um, *pm, depth + 1);
                        self(self, um, *pm, ub, pb, depth + 1);
                        return;
                    }
                }
            }
            cur.push_back(pb);
        };
        // The last valid u between a valid one and an invalid one.
        const auto edge = [&](double good, double bad, std::optional<RefPt>& pg) {
            for (int k = 0; k < 14; ++k) {
                const double mid = 0.5 * (good + bad);
                if (const std::optional<RefPt> pm = f(mid)) { good = mid; pg = pm; }
                else bad = mid;
            }
            return good;
        };

        std::optional<RefPt> prev;
        double uprev = u0;
        for (int i = 0; i <= kInitial; ++i) {
            const double u = u0 + (u1 - u0) * i / kInitial;
            const std::optional<RefPt> p = f(u);
            if (p && prev) {
                refine(refine, uprev, *prev, u, *p, 0);
            } else if (p) {
                // Entered the domain after uprev (or starts in it).
                if (i == 0) {
                    cur.push_back(*p);
                } else {
                    std::optional<RefPt> pg = p;
                    const double good = edge(u, uprev, pg);
                    cur.push_back(*pg);
                    if (good != u) refine(refine, good, *pg, u, *p, 0);
                }
            } else if (prev) {
                // Left it after uprev.
                std::optional<RefPt> pg = prev;
                const double good = edge(uprev, u, pg);
                if (good != uprev) refine(refine, uprev, *prev, good, *pg, 0);
                end_run();
            }
            prev = p;
            uprev = u;
        }
        end_run();
        return runs;
    }
} // namespace reference_detail

// Where a sloped axline() is drawn: runs of pixel points, a straight line as
// one run of two. All the points in data units: the line of the data, straight
// on a linear pair of axes and sampled where a scale bends it, nothing where a
// log axis has no place for it. Any point a Fraction or Pixels position: a
// straight screen line through them (the slope the screen's, y up). Cut to the
// frame plus a few pixels (clipping to it is still the caller's).
inline std::vector<RefPath> axline_paths(const AxLinePlot& p, const CoordTransform& tr) {
    using namespace reference_detail;
    std::vector<RefPath> out;
    const bool two = p.mode == AxLineMode::TwoPoints;
    const auto push_seg = [&](const std::optional<RefSegment>& g) {
        if (g && std::isfinite(g->x0) && std::isfinite(g->y0) && std::isfinite(g->x1) && std::isfinite(g->y1))
            out.push_back({{g->x0, g->y0}, {g->x1, g->y1}});
    };
    const bool data = p.x.space == Coords::Data && p.y.space == Coords::Data
                      && (!two || (p.x2.space == Coords::Data && p.y2.space == Coords::Data));
    if (!data) {
        const RefPt a{pos_to_px(p.x, tr), pos_to_py(p.y, tr)};
        if (!std::isfinite(a.x) || !std::isfinite(a.y)) return out;
        if (two) {
            const double bx = pos_to_px(p.x2, tr), by = pos_to_py(p.y2, tr);
            push_seg(clip_line(a, bx - a.x, by - a.y, tr));
        } else {
            push_seg(clip_line(a, 1.0, -p.slope, tr));
        }
        return out;
    }

    const double x0 = p.x.v, y0 = p.y.v;
    const bool vertical = two && p.x2.v == x0;
    const double m = two ? (vertical ? 0.0 : (p.y2.v - y0) / (p.x2.v - x0)) : p.slope;
    if (vertical) {
        if (!tr.xs.valid(x0)) return out;
        push_seg(clip_line({tr.to_px(x0), tr.py}, 0.0, 1.0, tr));
        return out;
    }
    if (!std::isfinite(m)) return out;
    if (tr.xs.linear() && tr.ys.linear()) {
        // Affine: the anchor and the pixel direction, in double (a point one unit along
        // would overflow a float pixel for a huge slope).
        const RefPt a{tr.to_px(x0), tr.to_py(y0)};
        const double dx = tr.pw / (tr.xmax - tr.xmin), dy = -m * tr.ph / (tr.ymax - tr.ymin);
        if (std::isfinite(dx) && std::isfinite(dy)) push_seg(clip_line(a, dx, dy, tr));
        return out;
    }

    // Bent by a scale: sample along x, or along y where the line climbs more
    // than the frame is wide.
    constexpr double kM = 4.0;
    const auto at_x = [&](double u) -> std::optional<RefPt> {
        const double x = tr.to_data_x(static_cast<float>(u));
        const double y = y0 + m * (x - x0);
        if (!std::isfinite(x) || !std::isfinite(y) || !tr.xs.valid(x) || !tr.ys.valid(y)) return std::nullopt;
        const float py = tr.to_py(y);
        if (!std::isfinite(py)) return std::nullopt;
        return RefPt{static_cast<float>(u), std::clamp(py, -1e5f, 1e5f)};
    };
    const auto at_y = [&](double u) -> std::optional<RefPt> {
        const double y = tr.to_data_y(static_cast<float>(u));
        const double x = x0 + (y - y0) / m;
        if (!std::isfinite(x) || !std::isfinite(y) || !tr.xs.valid(x) || !tr.ys.valid(y)) return std::nullopt;
        const float px = tr.to_px(x);
        if (!std::isfinite(px)) return std::nullopt;
        return RefPt{std::clamp(px, -1e5f, 1e5f), static_cast<float>(u)};
    };
    const std::optional<RefPt> l = at_x(tr.px), r = at_x(tr.px + tr.pw);
    const bool steep = m != 0.0 && l && r && std::fabs(static_cast<double>(r->y) - l->y) > tr.pw;
    return steep ? sample_runs(at_y, tr.py - kM, tr.py + tr.ph + kM)
                 : sample_runs(at_x, tr.px - kM, tr.px + tr.pw + kM);
}

// Where an axspan() is drawn. Each data end goes through the transform; an end
// the scale has no place for (<= 0 on log) lies past the frame's low-value
// edge, and nothing is drawn when both do. Every edge is held within `reach`
// pixels outside the frame, so a span far beyond the view stays finite and its
// outline (give `reach` more than half its width) stays out of sight.
inline std::optional<RefRect> axspan_rect(const AxSpanPlot& p, const CoordTransform& tr, float reach) {
    using namespace reference_detail;
    const bool on_x = p.on_x();
    const AxisScale& s = on_x ? tr.xs : tr.ys;
    if (!s.valid(p.lo) && !s.valid(p.hi)) return std::nullopt;
    // The frame's extent along the data axis, and its low-value end.
    const float f_lo = on_x ? tr.px : tr.py;
    const float f_hi = on_x ? tr.px + tr.pw : tr.py + tr.ph;
    const bool rising = on_x ? tr.sxspan() > 0.0 : tr.syspan() < 0.0;   // pixels grow with the value
    const float low_side = rising ? f_lo - reach : f_hi + reach;
    const auto px = [&](double v) {
        const float q = !s.valid(v) ? low_side : on_x ? tr.to_px(v) : tr.to_py(v);
        // NaN (a degenerate view) falls to the low side too.
        return std::isnan(q) ? low_side : std::clamp(q, f_lo - reach, f_hi + reach);
    };
    const float a = px(p.lo), b = px(p.hi);
    const float c0 = on_x ? frame_y(tr, p.opts.from) : frame_x(tr, p.opts.from);
    const float c1 = on_x ? frame_y(tr, p.opts.to) : frame_x(tr, p.opts.to);
    RefRect r{};
    if (on_x) r = {std::min(a, b), std::min(c0, c1), std::max(a, b), std::max(c0, c1)};
    else      r = {std::min(c0, c1), std::min(a, b), std::max(c0, c1), std::max(a, b)};
    return r;
}

// The `reach` both outputs pass: past an outline's half width.
inline float axspan_reach(const AxSpanPlot& p) { return std::max(p.opts.linewidth, 0.0f) + 2.0f; }

// A span's outline color: `edgecolor`, else the fill's.
inline Color axspan_edge(const AxSpanPlot& p) { return p.opts.edgecolor.value_or(*p.opts.color); }

} // namespace sextant
