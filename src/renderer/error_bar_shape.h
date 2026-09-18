#pragma once
#include <sextant/style.h>
#include <cmath>

namespace sextant {

// An arrow cap's length over its width: an equilateral head, sqrt(3)/2.
inline constexpr double kArrowLengthRatio = 0.8660254037844386;

// One whisker, reduced to line segments -- the definition of what a whisker
// and its caps *are*, shared by the raster path, the SVG writer and a plane's
// sheet, for marker_shape()'s reason: three consumers agreeing on a
// definition beats three copies agreeing with each other.
//
// Works in any 2D frame whose axes are the data axes: pixels for the raster
// and SVG paths, and a plane's own (u, v) for a sheet. `vertical` says which
// axis the whisker runs along. (cx, cy) is the point, and `lo_end`/`hi_end`
// are the two ends' coordinates along the whisker -- each equal to the
// point's own when that side has nothing, which draws neither stem nor cap on
// that side. Only sign and distance from the point are read, so a frame with
// a flipped axis (screen y) needs nothing special.
//
// `unit_along`/`unit_across` are frame units per pixel on the two axes -- 1 in
// a pixel frame, and different from each other on a plane whose axes have
// different scales -- since `capsize` is a pixel length.
//
// `seg(x0, y0, x1, y1)` receives each segment: the stem once, from end to end,
// then each present end's cap. A flat cap is one crossbar `capsize` long. An
// arrow cap is an open chevron with its tip on the end, `capsize` wide and
// kArrowLengthRatio x `capsize` long, scaled down whole when the stem on that
// side is shorter than the head, so it never reaches back past the point.
template <class Seg>
void whisker_segments(double cx, double cy, double lo_end, double hi_end,
                      bool vertical, double unit_along, double unit_across,
                      float capsize, CapStyle style, Seg&& seg)
{
    const double c      = vertical ? cy : cx;
    const double across = vertical ? cx : cy;
    auto emit = [&](double a0, double b0, double a1, double b1) {   // (along, across)
        if (vertical) seg(b0, a0, b1, a1);
        else          seg(a0, b0, a1, b1);
    };
    if (lo_end == c && hi_end == c) return;
    emit(lo_end, across, hi_end, across);

    if (!(capsize > 0.0f)) return;
    const double half = 0.5 * capsize * unit_across;
    auto cap = [&](double end) {
        if (end == c) return;
        if (style == CapStyle::Flat) {
            emit(end, across - half, end, across + half);
            return;
        }
        const double len   = kArrowLengthRatio * capsize * std::fabs(unit_along);
        const double avail = std::fabs(end - c);
        const double k     = len > avail ? avail / len : 1.0;
        const double base  = end - (end > c ? 1.0 : -1.0) * len * k;
        emit(base, across - half * k, end, across);
        emit(base, across + half * k, end, across);
    };
    cap(lo_end);
    cap(hi_end);
}

} // namespace sextant
