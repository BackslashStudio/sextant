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
#include <optional>

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
