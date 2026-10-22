#pragma once
// Axes::hist2d()'s binning, as numpy's histogram2d (which matplotlib's hist2d
// calls): bins are half-open [lo, hi) but the last also takes its upper edge,
// and a point outside the edges, or with a NaN coordinate, is not counted.
#include "color_norm.h"
#include "sextant/style.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace sextant {

struct Hist2dBins {
    std::vector<double> x_edges, y_edges; // cols + 1, rows + 1
    bool even = true;                      // neither axis had edges given
    std::vector<double> values;            // rows x cols, row 0 at the lowest y
    float vmin = 0.0f, vmax = 1.0f;        // resolved (see Hist2dOptions::vmin)

    int rows() const { return static_cast<int>(y_edges.size()) - 1; }
    int cols() const { return static_cast<int>(x_edges.size()) - 1; }
};

namespace hist2d_impl {

// One axis's edges: the given ones, checked, or `bins` even ones over the range
// (given, or the extent of `v`'s finite values `with` a finite partner).
inline std::vector<double> edges(std::span<const double> v, std::span<const double> partner,
                                 int bins, const std::optional<Range>& range,
                                 const std::vector<double>& given, const std::string& w, const char* axis) {
    if (!given.empty()) {
        if (given.size() < 2)
            throw std::invalid_argument(w + ": " + axis + "_edges needs at least two edges");
        for (std::size_t i = 0; i < given.size(); ++i) {
            if (!std::isfinite(given[i]))
                throw std::invalid_argument(w + ": " + axis + "_edges must be finite");
            if (i > 0 && !(given[i] > given[i - 1]))
                throw std::invalid_argument(w + ": " + axis + "_edges must be strictly increasing");
        }
        return given;
    }
    if (bins < 1) throw std::invalid_argument(w + ": " + axis + "bins must be at least 1");
    double lo = 0.0, hi = 1.0;
    if (range) {
        lo = range->lo;
        hi = range->hi;
        if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo))
            throw std::invalid_argument(w + ": " + axis + "range must be finite with lo < hi");
    } else {
        bool any = false;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (!std::isfinite(v[i]) || !std::isfinite(partner[i])) continue;
            lo = any ? std::min(lo, v[i]) : v[i];
            hi = any ? std::max(hi, v[i]) : v[i];
            any = true;
        }
        if (!any) {
            lo = 0.0;
            hi = 1.0;
        } else if (lo == hi) {
            lo -= 0.5;
            hi += 0.5;
        }
    }
    std::vector<double> e(static_cast<std::size_t>(bins) + 1);
    for (int i = 0; i <= bins; ++i) e[static_cast<std::size_t>(i)] = lo + (hi - lo) * i / bins;
    e.back() = hi;
    return e;
}

// The bin of `v` among `e`, or -1 outside.
inline int bin_of(const std::vector<double>& e, double v) {
    if (!(v >= e.front() && v <= e.back())) return -1;
    if (v == e.back()) return static_cast<int>(e.size()) - 2;
    return static_cast<int>(std::upper_bound(e.begin(), e.end(), v) - e.begin()) - 1;
}

} // namespace hist2d_impl

// Counts (or densities) per bin, cmin/cmax applied, and the colour limits.
// Throws std::invalid_argument for x and y of different lengths, bins < 1,
// a bad range or bad edges.
inline Hist2dBins hist2d_bins(std::span<const double> x, std::span<const double> y,
                              const Hist2dOptions& o, const char* who) {
    const std::string w = who;
    if (x.size() != y.size()) throw std::invalid_argument(w + ": x and y must be the same length");
    Hist2dBins b;
    b.x_edges = hist2d_impl::edges(x, y, o.xbins, o.xrange, o.x_edges, w, "x");
    b.y_edges = hist2d_impl::edges(y, x, o.ybins, o.yrange, o.y_edges, w, "y");
    b.even = o.x_edges.empty() && o.y_edges.empty();
    const std::size_t cols = b.x_edges.size() - 1, rows = b.y_edges.size() - 1;
    b.values.assign(rows * cols, 0.0);
    double total = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const int c = hist2d_impl::bin_of(b.x_edges, x[i]);
        const int r = hist2d_impl::bin_of(b.y_edges, y[i]);
        if (c < 0 || r < 0) continue;
        b.values[static_cast<std::size_t>(r) * cols + static_cast<std::size_t>(c)] += 1.0;
        total += 1.0;
    }
    if (o.density && total > 0.0)
        for (std::size_t r = 0; r < rows; ++r)
            for (std::size_t c = 0; c < cols; ++c)
                b.values[r * cols + c] /= total * (b.x_edges[c + 1] - b.x_edges[c])
                                          * (b.y_edges[r + 1] - b.y_edges[r]);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (double& v : b.values)
        if ((o.cmin && v < *o.cmin) || (o.cmax && v > *o.cmax)) v = nan;

    // Colour limits: the values' extent unless given; log takes the smallest
    // value above 0, and two-slope widens the far side to keep the centre inside
    // (matplotlib's TwoSlopeNorm.autoscale_None).
    const bool log = o.norm.kind == ColorNorm::Kind::Log;
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const double v : b.values) {
        if (std::isnan(v) || (log && !(v > 0.0))) continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (!(lo <= hi)) {
        lo = log ? 1.0 : 0.0;
        hi = log ? 10.0 : 1.0;
    }
    if (o.norm.kind == ColorNorm::Kind::TwoSlope && !o.vmin && !o.vmax) {
        const double c = o.norm.center;
        if (lo >= c) lo = 2.0 * c - hi;
        if (hi <= c) hi = 2.0 * c - lo;
        if (!(lo < c && c < hi)) {
            lo = c - 1.0;
            hi = c + 1.0;
        }
    }
    b.vmin = o.vmin ? *o.vmin : static_cast<float>(lo);
    b.vmax = o.vmax ? *o.vmax : static_cast<float>(hi);
    return b;
}

} // namespace sextant
