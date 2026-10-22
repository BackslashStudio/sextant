#pragma once
// A value's place on the colormap through a ColorNorm, shared by every output
// of the colormapped kinds (heatmap, heatmap_rec, heatmap_tri, scatter_z,
// quiver) so they all agree. A linear norm is left to each caller's own
// arithmetic, so its bytes stay what they were.
#include "sextant/style.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace sextant {

// Whether `n` fits vmin/vmax: log needs both above 0, two_slope its centre
// strictly between them (either order). Linear always fits.
inline bool norm_fits(const ColorNorm& n, double vmin, double vmax) {
    switch (n.kind) {
        case ColorNorm::Kind::Log:
            return vmin > 0.0 && vmax > 0.0 && std::isfinite(vmin) && std::isfinite(vmax);
        case ColorNorm::Kind::TwoSlope: {
            const double c = n.center;
            return std::isfinite(c) && (c - vmin) * (vmax - c) > 0.0;
        }
        case ColorNorm::Kind::Linear: break;
    }
    return true;
}

// The norm drawing uses: `n`, or linear when it doesn't fit.
inline ColorNorm::Kind norm_in_force(const ColorNorm& n, double vmin, double vmax) {
    return norm_fits(n, vmin, vmax) ? n.kind : ColorNorm::Kind::Linear;
}

// A plotting call's check: throws std::invalid_argument when `n` doesn't fit.
inline void check_norm(const ColorNorm& n, double vmin, double vmax, const char* who) {
    if (norm_fits(n, vmin, vmax)) return;
    const std::string w = who;
    if (n.kind == ColorNorm::Kind::Log)
        throw std::invalid_argument(w + ": a log norm needs vmin > 0 and vmax > 0");
    throw std::invalid_argument(w + ": a two-slope norm needs its centre strictly between vmin and vmax");
}

// A value's colormap position under a norm in force (`kind` from
// norm_in_force()), not yet clamped or wrapped. NaN when the value has no
// colour: NaN, or <= 0 under log. vmin == vmax puts everything at 0.
inline double norm_t(ColorNorm::Kind kind, float center, double vmin, double vmax, double v) {
    if (std::isnan(v)) return v;
    switch (kind) {
        case ColorNorm::Kind::Log: {
            if (!(v > 0.0)) return std::numeric_limits<double>::quiet_NaN();
            const double lo = std::log(vmin), span = std::log(vmax) - lo;
            return span != 0.0 ? (std::log(v) - lo) / span : 0.0;
        }
        case ColorNorm::Kind::TwoSlope: {
            const double c = center;
            // On vmin's side of the centre: vmin..c onto 0..0.5; else c..vmax onto 0.5..1.
            if ((v - c) * (vmin - c) > 0.0) return 0.5 * (v - vmin) / (c - vmin);
            return 0.5 + 0.5 * (v - c) / (vmax - c);
        }
        case ColorNorm::Kind::Linear: break;
    }
    const double span = vmax - vmin;
    return span != 0.0 ? (v - vmin) / span : 0.0;
}

// Whether a value has no colour under the norm in force beyond being NaN
// (<= 0 under log), so its cell, triangle, point or arrow is not drawn.
inline bool norm_masks(ColorNorm::Kind kind, double v) {
    return kind == ColorNorm::Kind::Log && !(v > 0.0);
}

} // namespace sextant
