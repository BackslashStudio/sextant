#pragma once
// A value's place on the colormap through a ColorNorm, shared by every output
// of the colormapped kinds (heatmap, heatmap_rec, heatmap_tri, scatter_z,
// quiver) so they all agree. A linear norm is left to each caller's own
// arithmetic, so its bytes stay what they were.
#include "sextant/style.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace sextant {

// The most boundaries a boundary norm takes (GL 4.1's least texture width:
// heatmap_tri's shader reads them from a one-row texture).
inline constexpr std::size_t kMaxNormBounds = 16384;

// Whether a boundary list is usable: 2 to kMaxNormBounds, finite, strictly increasing.
inline bool norm_bounds_ok(const std::vector<double>& b) {
    if (b.size() < 2 || b.size() > kMaxNormBounds) return false;
    for (std::size_t i = 0; i < b.size(); ++i)
        if (!std::isfinite(b[i]) || (i > 0 && !(b[i] > b[i - 1]))) return false;
    return true;
}

// Whether `n` fits vmin/vmax: log needs both above 0, two_slope its centre
// strictly between them (either order), boundaries a usable list (vmin/vmax
// unused). Linear always fits.
inline bool norm_fits(const ColorNorm& n, double vmin, double vmax) {
    switch (n.kind) {
        case ColorNorm::Kind::Log:
            return vmin > 0.0 && vmax > 0.0 && std::isfinite(vmin) && std::isfinite(vmax);
        case ColorNorm::Kind::TwoSlope: {
            const double c = n.center;
            return std::isfinite(c) && (c - vmin) * (vmax - c) > 0.0;
        }
        case ColorNorm::Kind::Boundaries: return norm_bounds_ok(n.bounds);
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
    if (n.kind == ColorNorm::Kind::Boundaries)
        throw std::invalid_argument(w + ": a boundary norm needs 2 to 16384 finite, strictly increasing boundaries");
    throw std::invalid_argument(w + ": a two-slope norm needs its centre strictly between vmin and vmax");
}

// Band i's colormap position under a boundary norm of `bands` bands:
// spread over the whole map (BoundaryNorm's), one band in the middle.
inline double norm_band_t(std::size_t i, std::size_t bands) {
    return bands > 1 ? static_cast<double>(i) / static_cast<double>(bands - 1) : 0.5;
}

// The band of `v` among boundaries `b` (a usable list): [b[i], b[i+1]), the
// last band closed; -1 outside [b.front(), b.back()] or NaN.
inline std::ptrdiff_t norm_band(const std::vector<double>& b, double v) {
    if (!(v >= b.front() && v <= b.back())) return -1;
    const std::ptrdiff_t i = std::upper_bound(b.begin(), b.end(), v) - b.begin() - 1;
    return std::min<std::ptrdiff_t>(i, static_cast<std::ptrdiff_t>(b.size()) - 2);
}

// A value's colormap position under a norm in force (`kind` from
// norm_in_force(); `n` supplies its centre or boundaries), not yet clamped or
// wrapped. NaN when the value has no colour: NaN, <= 0 under log, outside the
// boundaries. vmin == vmax puts everything at 0.
inline double norm_t(ColorNorm::Kind kind, const ColorNorm& n, double vmin, double vmax, double v) {
    if (std::isnan(v)) return v;
    switch (kind) {
        case ColorNorm::Kind::Log: {
            if (!(v > 0.0)) return std::numeric_limits<double>::quiet_NaN();
            const double lo = std::log(vmin), span = std::log(vmax) - lo;
            return span != 0.0 ? (std::log(v) - lo) / span : 0.0;
        }
        case ColorNorm::Kind::Boundaries: {
            const std::ptrdiff_t i = norm_band(n.bounds, v);
            if (i < 0) return std::numeric_limits<double>::quiet_NaN();
            return norm_band_t(static_cast<std::size_t>(i), n.bounds.size() - 1);
        }
        case ColorNorm::Kind::TwoSlope: {
            const double c = n.center;
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
// (<= 0 under log, outside the boundaries), so its cell, triangle, point or
// arrow is not drawn.
inline bool norm_masks(ColorNorm::Kind kind, const ColorNorm& n, double v) {
    if (kind == ColorNorm::Kind::Log) return !(v > 0.0);
    if (kind == ColorNorm::Kind::Boundaries) return !std::isnan(v) && norm_band(n.bounds, v) < 0;
    return false;
}

} // namespace sextant
