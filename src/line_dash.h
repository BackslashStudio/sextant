#pragma once
// The single LineStyle dash table, shared by the stroke shader, NanoVG and the
// SVG writer. Run lengths are absolute logical pixels (not scaled by width).
#include "sextant/style.h"

#include <cstdio>
#include <string>

namespace sextant {

struct DashPattern {
    // Alternating on/off run lengths; unused entries are zero, so consumers can
    // walk all four.
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

// SVG stroke-dasharray value ("6,4", ...), or empty for no dashing.
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
