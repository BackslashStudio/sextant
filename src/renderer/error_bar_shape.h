#pragma once
#include <sextant/style.h>
#include <cmath>

namespace sextant {

// An arrow cap's length over its width: an equilateral head, sqrt(3)/2.
inline constexpr double kArrowLengthRatio = 0.8660254037844386;

// One whisker and its caps as line segments, shared by the raster path, the
// SVG writer and plane sheets.
//
// Works in any 2D frame aligned with the data axes (pixels, or a plane's
// (u, v)). `vertical` picks the whisker's axis; (cx, cy) is the point;
// `lo_end`/`hi_end` are the end coordinates (equal to the point's = nothing on
// that side). `unit_along`/`unit_across` are frame units per pixel, since
// `capsize` is in pixels.
//
// `seg(x0, y0, x1, y1)` receives the stem, then each present cap: a flat
// crossbar `capsize` long, or a chevron `capsize` wide and kArrowLengthRatio x
// `capsize` long, shrunk if the stem is shorter.
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
