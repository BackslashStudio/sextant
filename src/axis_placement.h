#pragma once
#include "sextant/style.h"
#include <algorithm>
#include <optional>

namespace sextant {

// Where one axis line sits along one coordinate that is not its own (v1.0
// step 19; step 20 reuses this for the 3D box, which needs two of them per
// axis). Data space, not pixels: whether an axis is interior has to be known
// before the frame is sized, because an interior axis reserves no room for
// its labels, and the frame depends on what was reserved.
struct AxisPlacement {
    // The value on the other coordinate. For the 2D x axis that is a y.
    double pos = 0.0;

    // Strictly between the limits, so the axis is its own line rather than
    // one of the frame's edges, and its ticks and labels fall inside the
    // frame instead of in a reserved band outside it.
    bool interior = false;

    // On the high end rather than the low one. Its ticks and labels then go
    // on the far side of the line -- up for x, right for y -- so they point
    // away from the data rather than over it.
    bool high = false;
};

// `lo`/`hi` are the resolved limits of the coordinate `pos` is measured
// along, after resolve_limits() has already folded `pin` into them if that
// coordinate is on automatic limits. A pin outside them clamps, which is what
// keeps an axis on the frame's edge rather than losing it when the caller
// fixes limits that exclude the crossing point or zooms away from it.
inline AxisPlacement place_axis(AxisPosition p, const std::optional<double>& pin,
                                double lo, double hi)
{
    AxisPlacement a;
    if (pin) {
        a.pos = std::clamp(*pin, lo, hi);
    } else {
        switch (p) {
            // Mid reads the resolved range, so it needs no help from the
            // caller to find the middle of automatic limits. It assumes a
            // linear scale, which is safe while the library has none other.
            case AxisPosition::Mid:  a.pos = lo + (hi - lo) * 0.5; break;
            case AxisPosition::High: a.pos = hi; break;
            // Auto and Low are the same placement in 2D. They differ only in
            // 3D, where Auto is the camera's choice among four parallel edges.
            default:                 a.pos = lo; break;
        }
    }
    // A degenerate range (lo == hi) leaves both flags false, i.e. Low, which
    // is the only placement that means anything when there is nowhere to be
    // in between.
    a.interior = a.pos > lo && a.pos < hi;
    a.high     = hi > lo && !a.interior && a.pos >= hi;
    return a;
}

// One coordinate of a 3D axis line's position (v1.0 step 20). Two of these
// place one axis: an x axis needs a y *and* a z, and its four parallel box
// edges are the four combinations of their extremes.
struct AxisCoord3D {
    // Where the line sits along this coordinate, in data space. Meaningless
    // when `camera` is true -- there is no data value to report, because the
    // choice is being deferred to something that works in box space.
    AxisPlacement at;

    // Auto with no pin: keep the silhouette edge plan_box3d() picks from the
    // eye direction. This is the one thing Auto can mean here and cannot mean
    // in 2D, where there is no camera to ask -- which is why AxisPosition
    // needs Auto as a distinct enumerator rather than a synonym for Low.
    bool camera = false;
};

// `lo`/`hi` are the coordinate's resolved limits, as place_axis() wants them.
// Low and High are *absolute* here -- that coordinate's data minimum and
// maximum, a fixed face of the box -- so an axis pinned to one stops migrating
// as the camera orbits. That is the want Auto cannot serve.
//
// No box-space conversion happens here on purpose: the caller has the
// Transform3D and converts with it, so there is exactly one copy of the
// data->box arithmetic.
inline AxisCoord3D place_axis3d(AxisPosition p, const std::optional<double>& pin,
                                double lo, double hi)
{
    AxisCoord3D c;
    // A pin outclasses the enum, as in 2D, and clamps per coordinate. Two
    // clamped coordinates land the axis on a box edge, one on a face -- so
    // "which of twelve edges is nearest" never has to be answered.
    if (p == AxisPosition::Auto && !pin) { c.camera = true; return c; }
    c.at = place_axis(p, pin, lo, hi);
    return c;
}

// Fold an origin component into automatic bounds, so pinning an axis to a
// value outside the data widens the view to show it rather than clamping it
// onto the frame's edge. Called on the *unpadded* bounds: the pinned value
// then gets the same 5% breathing room as a data point, instead of landing
// exactly on the frame edge and reading as Low.
inline void widen_for_origin(const std::optional<double>& pin, bool limits_auto,
                             double& lo, double& hi)
{
    if (!limits_auto || !pin) return;
    lo = std::min(lo, *pin);
    hi = std::max(hi, *pin);
}

} // namespace sextant
