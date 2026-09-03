#pragma once
// The single definition of what each LineStyle looks like.
//
// Three renderers dash independently -- the data pass's stroke shader, NanoVG
// for grid lines and legend swatches, and the SVG writer's stroke-dasharray --
// so they read one table rather than three copies of the same numbers.
// svg_dasharray() is derived from the same array rather than written out
// separately, so the raster and vector paths cannot drift apart.
//
// Run lengths are absolute logical pixels, matching SVG's stroke-dasharray and
// deliberately unlike matplotlib, which scales its patterns by the line width.
#include "sextant/style.h"

#include <cstdio>
#include <string>

namespace sextant {

struct DashPattern {
    // Alternating on/off run lengths. Two-entry patterns leave [2] and [3] at
    // zero, which makes the third and fourth arms empty — so a consumer can
    // walk all four unconditionally and get the two-arm result.
    float seg[4] = { 0.f, 0.f, 0.f, 0.f };
    int   count  = 0;      // meaningful entries: 0 (solid), 2, or 4 — always even
    float period = 0.f;    // sum of seg[0..count); 0 means "not dashed"

    bool dashed() const { return period > 0.f && count > 0; }
};

inline DashPattern dash_pattern(LineStyle ls) {
    switch (ls) {
        case LineStyle::Dashed:  return { { 6.f, 4.f, 0.f, 0.f }, 2, 10.f };
        case LineStyle::Dotted:  return { { 2.f, 3.f, 0.f, 0.f }, 2,  5.f };
        case LineStyle::DashDot: return { { 6.f, 3.f, 2.f, 3.f }, 4, 14.f };
        default:                 return {};   // Solid and None: no pattern
    }
}

// The value for SVG's stroke-dasharray attribute ("6,4", "2,3", "6,3,2,3"),
// or an empty string for a style that needs no attribute at all. Built from
// dash_pattern() rather than from its own literals — %g renders these whole
// numbers without a decimal point, so the output is what it always was.
inline std::string svg_dasharray(LineStyle ls) {
    const DashPattern d = dash_pattern(ls);
    if (!d.dashed()) return {};
    std::string out;
    char buf[32];
    for (int i = 0; i < d.count; ++i) {
        std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(d.seg[i]));
        if (i) out += ',';
        out += buf;
    }
    return out;
}

} // namespace sextant
