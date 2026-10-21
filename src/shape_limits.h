#pragma once
// What autoscale reads from Axes::shape_poly()/shape_circle()/shape_wedge():
// the data-space points a shape adds, and the angle arithmetic the geometry
// shares. Only a shape measured in data units counts; a Fraction or Pixels one
// never moves the limits.
#include "plot_objects.h"

#include <algorithm>
#include <cmath>
#include <optional>

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
// The same over every shown shape of the three kinds in `all` (both bounds scans).
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
