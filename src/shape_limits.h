#pragma once
// What autoscale reads from Axes::shape_poly()/shape_circle()/shape_wedge()
// and pie(): the data-space points a shape adds, and the angle arithmetic the
// geometry shares (a pie's slices too, for pie_geometry.h). Only a shape measured in data units counts; a Fraction or Pixels one
// never moves the limits.
#include "plot_objects.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

namespace sextant {

// The data-space box of a circle or wedge: where autoscale looks. The arc's
// extreme points only, so a quarter wedge adds a quarter's box.
struct ShapeBox {
    double xmin, xmax, ymin, ymax;
};

namespace shape_detail {
    constexpr double kPi = 3.14159265358979323846;

    // The sweep from theta1 to theta2 counter-clockwise, in degrees: 360 for a
    // full turn (>= 360 apart), 0 for none.
    inline double sweep_of(double theta1, double theta2) {
        const double d = theta2 - theta1;
        if (!(d == d)) return 0.0;
        if (d >= 360.0) return 360.0;
        if (d >= 0.0) return d;
        const double w = std::fmod(d, 360.0) + 360.0;
        return w >= 360.0 ? 0.0 : w;
    }
} // namespace shape_detail

// A circle or wedge in data units: the box of the arc's end points, any axis
// crossing it passes, and the centre when the wedge has one. Nothing for a
// degenerate shape.
inline std::optional<ShapeBox> wedge_data_box(double cx, double cy, double r, double theta1, double theta2,
                                              double inner) {
    using namespace shape_detail;
    const double sweep = sweep_of(theta1, theta2);
    if (!(sweep > 0.0) || !(r > 0.0) || !std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(r))
        return std::nullopt;
    ShapeBox b{cx, cx, cy, cy};
    bool any = false;
    const auto add = [&](double rad, double deg) {
        const double a = deg * kPi / 180.0;
        const double x = cx + rad * std::cos(a), y = cy + rad * std::sin(a);
        b = any ? ShapeBox{std::min(b.xmin, x), std::max(b.xmax, x), std::min(b.ymin, y), std::max(b.ymax, y)}
                : ShapeBox{x, x, y, y};
        any = true;
    };
    for (const double rad : {r, inner > 0.0 ? inner : -1.0}) {
        if (rad < 0.0) continue;
        add(rad, theta1);
        add(rad, theta1 + sweep);
        // Quarter turns inside the sweep.
        for (double a = std::ceil(theta1 / 90.0) * 90.0; a < theta1 + sweep; a += 90.0) add(rad, a);
    }
    if (inner <= 0.0 && sweep < 360.0)
        b = {std::min(b.xmin, cx), std::max(b.xmax, cx), std::min(b.ymin, cy), std::max(b.ymax, cy)};
    return b;
}

// Axes::pie()'s slices, as matplotlib's pie(): slice k runs from where slice
// k - 1 ended, starting at `startangle`, by its fraction of a full turn,
// counter-clockwise or clockwise; explode moves it out along its middle.
struct PieSlice {
    std::size_t k = 0;                  // its index in the pie's values
    double value = 0.0;
    double frac = 0.0;                  // of a full turn
    double theta1 = 0.0, theta2 = 0.0;  // degrees, counter-clockwise from +x, theta1 <= theta2
    double mid = 0.0;                   // radians, its middle
    double cx = 0.0, cy = 0.0;          // its centre, explode applied
};

// The slices of `values` (checked: finite, >= 0, a positive sum), with
// `explode` empty or one per value.
inline std::vector<PieSlice> pie_slices(std::span<const double> values, std::span<const double> explode,
                                        const PieOptions& o) {
    double sum = 0.0;
    for (const double v : values) sum += v;
    const double scale = o.normalize ? sum : 1.0;
    std::vector<PieSlice> out;
    if (!(scale > 0.0)) return out;
    out.reserve(values.size());
    double t = o.startangle / 360.0;   // in turns, as matplotlib's
    for (std::size_t k = 0; k < values.size(); ++k) {
        PieSlice s;
        s.k = k;
        s.value = values[k];
        s.frac = values[k] / scale;
        const double t2 = o.counterclock ? t + s.frac : t - s.frac;
        s.theta1 = 360.0 * std::min(t, t2);
        s.theta2 = 360.0 * std::max(t, t2);
        s.mid = shape_detail::kPi * (t + t2);
        const double e = k < explode.size() ? explode[k] * o.radius : 0.0;
        s.cx = o.cx + e * std::cos(s.mid);
        s.cy = o.cy + e * std::sin(s.mid);
        out.push_back(s);
        t = t2;
    }
    return out;
}

inline std::vector<PieSlice> pie_slices(const PiePlot& p) {
    return pie_slices(p.values.get(), p.explode, p.opts);
}

// Calls fn(x, y) for each data point a pie adds to the limits: each slice's
// box, explode included, and each drawn label's anchor.
template <class F>
void pie_limit_points(const PiePlot& p, F&& fn) {
    const PieOptions& o = p.opts;
    const bool labels = o.labeldistance.has_value() && !p.labels.empty();
    for (const PieSlice& s : pie_slices(p)) {
        if (const auto b = wedge_data_box(s.cx, s.cy, o.radius, s.theta1, s.theta2, o.inner)) {
            fn(b->xmin, b->ymin);
            fn(b->xmax, b->ymax);
        }
        if (labels && !p.labels[s.k].empty()) {
            const double d = *o.labeldistance * o.radius;
            fn(s.cx + d * std::cos(s.mid), s.cy + d * std::sin(s.mid));
        }
    }
}

// Calls fn(x, y) for each data-space point a shape adds to the limits: a
// polygon's vertices (only in data units); a circle's or wedge's box corners
// (only when centre and radius are all Data).
template <class F>
void shape_limit_points(const PolyPlot& p, F&& fn) {
    if (p.opts.space != Coords::Data) return;
    const std::size_t n = std::min(p.x.size(), p.y.size());
    for (std::size_t i = 0; i < n; ++i) fn(p.x[i], p.y[i]);
}

template <class F>
void shape_limit_points(const CirclePlot& p, F&& fn) {
    if (p.r.space != Coords::Data || p.cx.space != Coords::Data || p.cy.space != Coords::Data) return;
    if (const auto b = wedge_data_box(p.cx.v, p.cy.v, p.r.v, 0.0, 360.0, 0.0)) {
        fn(b->xmin, b->ymin);
        fn(b->xmax, b->ymax);
    }
}

template <class F>
void shape_limit_points(const WedgePlot& p, F&& fn) {
    if (p.r.space != Coords::Data || p.cx.space != Coords::Data || p.cy.space != Coords::Data) return;
    if (const auto b = wedge_data_box(p.cx.v, p.cy.v, p.r.v, p.theta1, p.theta2, p.inner)) {
        fn(b->xmin, b->ymin);
        fn(b->xmax, b->ymax);
    }
}
// The same over every shown shape of the three kinds and every shown pie in
// `all` (both bounds scans).
template <class F>
void all_shape_limit_points(const AllPlotData& all, F&& fn) {
    if (all.polys)
        for (const auto& p : *all.polys)
            if (object_shown(p)) shape_limit_points(p, fn);
    if (all.circles)
        for (const auto& p : *all.circles)
            if (object_shown(p)) shape_limit_points(p, fn);
    if (all.wedges)
        for (const auto& p : *all.wedges)
            if (object_shown(p)) shape_limit_points(p, fn);
    if (all.pies)
        for (const auto& p : *all.pies)
            if (object_shown(p)) pie_limit_points(p, fn);
}

// Calls fn(x, y) for each data point a sloped axline() adds to the limits: its
// point (or two), only when all are in data units. A reference line is a line,
// not a mark, so only its anchors count.
template <class F>
void axline_limit_points(const AxLinePlot& p, F&& fn) {
    const bool two = p.mode == AxLineMode::TwoPoints;
    if (p.x.space != Coords::Data || p.y.space != Coords::Data) return;
    if (two && (p.x2.space != Coords::Data || p.y2.space != Coords::Data)) return;
    fn(p.x.v, p.y.v);
    if (two) fn(p.x2.v, p.y2.v);
}

} // namespace sextant
