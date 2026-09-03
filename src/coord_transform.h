#pragma once
#include "plot_objects.h"
#include "renderer/plot_rect.h"
#include "tick.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <limits>

namespace sextant {

// -------------------------------------------------------------------------
// Coordinate transform: data space → pixel space
// -------------------------------------------------------------------------
struct CoordTransform {
    double xmin, xmax, ymin, ymax;
    float  px, py, pw, ph;           // plot rect (top-left + size)
    float  win_w = 0, win_h = 0;     // full framebuffer size

    float to_px(double x) const {
        return px + static_cast<float>((x - xmin) / (xmax - xmin)) * pw;
    }
    // Pixel y=0 is at the top; data y increases upward
    float to_py(double y) const {
        return py + ph - static_cast<float>((y - ymin) / (ymax - ymin)) * ph;
    }

    // Inverse of to_px/to_py — screen pixel back to data space (pan/zoom).
    double to_data_x(float screen_x) const {
        return xmin + static_cast<double>((screen_x - px) / pw) * (xmax - xmin);
    }
    double to_data_y(float screen_y) const {
        return ymin + static_cast<double>((py + ph - screen_y) / ph) * (ymax - ymin);
    }
};

// -------------------------------------------------------------------------
// Pan/zoom: pure pixel-delta -> new-limits math, kept independent of ImGui
// so it can be reasoned about (and eyeballed) without a window/render loop.
// -------------------------------------------------------------------------
struct AxisLimits { double xmin, xmax, ymin, ymax; };

// Shift the view by a drag of (ddx_px, ddy_px) screen pixels — content
// follows the cursor, like dragging a map.
inline AxisLimits pan_limits(const CoordTransform& tr, float ddx_px, float ddy_px) {
    const double dx = -static_cast<double>(ddx_px / tr.pw) * (tr.xmax - tr.xmin);
    // Screen y grows downward, data y grows upward (see to_py) — dragging
    // down (ddy_px > 0) should slide the view the same way, hence +.
    const double dy = static_cast<double>(ddy_px / tr.ph) * (tr.ymax - tr.ymin);
    return { tr.xmin + dx, tr.xmax + dx, tr.ymin + dy, tr.ymax + dy };
}

// Scale the view around the data point under (cursor_x_px, cursor_y_px) by
// factor (< 1 zooms in, > 1 zooms out) — that point stays fixed on screen.
inline AxisLimits zoom_limits(const CoordTransform& tr, float cursor_x_px, float cursor_y_px, float factor) {
    const double cx = tr.to_data_x(cursor_x_px);
    const double cy = tr.to_data_y(cursor_y_px);
    return {
        cx - (cx - tr.xmin) * factor, cx + (tr.xmax - cx) * factor,
        cy - (cy - tr.ymin) * factor, cy + (tr.ymax - cy) * factor
    };
}

// -------------------------------------------------------------------------
// Auto-scale: walk all plot objects, return padded bounds
// -------------------------------------------------------------------------
struct DataBounds { double xmin, xmax, ymin, ymax; };


inline DataBounds auto_scale(const AllPlotData& all, double pad = 0.05) {
    double xlo =  std::numeric_limits<double>::max();
    double xhi = -std::numeric_limits<double>::max();
    double ylo =  std::numeric_limits<double>::max();
    double yhi = -std::numeric_limits<double>::max();

    // An error bar extends a series past its own points, so it has to widen
    // the auto limits -- otherwise the bars on the extreme points fall outside
    // the frame and are scissored away, which reads as a plot that has lost
    // its uncertainty rather than as a limits problem.
    //
    // `xs`/`ys` are whatever the bars hang off: a line's or scatter's x/y, a
    // bar's centers/heights (its tip, not the baseline). Guarded per direction
    // so a series without error data costs nothing.
    auto grow_err = [&](const CowVec<double>& xs, const CowVec<double>& ys,
                        const ErrorBarData& err) {
        const std::size_t n = std::min(xs.size(), ys.size());
        if (err.has_x_span())
            for (std::size_t i = 0; i < n; ++i) {
                xlo = std::min(xlo, err.x_lo(i, xs[i]));
                xhi = std::max(xhi, err.x_hi(i, xs[i]));
            }
        if (err.has_x_box())
            for (std::size_t i = 0; i < n; ++i) {
                xlo = std::min(xlo, xs[i] - err.x_var(i));
                xhi = std::max(xhi, xs[i] + err.x_var(i));
            }
        if (err.has_y_span())
            for (std::size_t i = 0; i < n; ++i) {
                ylo = std::min(ylo, err.y_lo(i, ys[i]));
                yhi = std::max(yhi, err.y_hi(i, ys[i]));
            }
        if (err.has_y_box())
            for (std::size_t i = 0; i < n; ++i) {
                ylo = std::min(ylo, ys[i] - err.y_var(i));
                yhi = std::max(yhi, ys[i] + err.y_var(i));
            }
    };

    for (const auto& lp : all.lines) {
        for (double v : lp.x) { xlo = std::min(xlo, v); xhi = std::max(xhi, v); }
        for (double v : lp.y) { ylo = std::min(ylo, v); yhi = std::max(yhi, v); }
        grow_err(lp.x, lp.y, lp.err);
    }
    for (const auto& sp : all.scatters) {
        for (double v : sp.x) { xlo = std::min(xlo, v); xhi = std::max(xhi, v); }
        for (double v : sp.y) { ylo = std::min(ylo, v); yhi = std::max(yhi, v); }
        grow_err(sp.x, sp.y, sp.err);
    }
    for (const auto& sp : all.scatter_z) {
        for (double v : sp.x) { xlo = std::min(xlo, v); xhi = std::max(xhi, v); }
        for (double v : sp.y) { ylo = std::min(ylo, v); yhi = std::max(yhi, v); }
        grow_err(sp.x, sp.y, sp.err);
    }
    for (const auto& bp : all.bars) {
        for (std::size_t i = 0; i < bp.centers.size(); ++i) {
            const double half = bp.bar_width * 0.5;
            xlo = std::min(xlo, bp.centers[i] - half);
            xhi = std::max(xhi, bp.centers[i] + half);
            // Y always spans from 0 to height (negative heights go below 0)
            ylo = std::min(ylo, std::min(0.0, bp.heights[i]));
            yhi = std::max(yhi, std::max(0.0, bp.heights[i]));
        }
        // Heights, not the zero baseline the fill spans to: a bar's error bar
        // measures its tip, so grow_err applies unchanged once `heights` is
        // read as the y of a point.
        grow_err(bp.centers, bp.heights, bp.err);
    }
    for (const auto& hp : all.heatmaps) {
        // Heatmap fills [0, cols] × [0, rows] in data space
        xlo = std::min(xlo, 0.0); xhi = std::max(xhi, static_cast<double>(hp.cols));
        ylo = std::min(ylo, 0.0); yhi = std::max(yhi, static_cast<double>(hp.rows));
    }

    if (xlo > xhi) { xlo = 0; xhi = 1; }
    if (ylo > yhi) { ylo = 0; yhi = 1; }
    if (xlo == xhi) { xlo -= 0.5; xhi += 0.5; }
    if (ylo == yhi) { ylo -= 0.5; yhi += 0.5; }

    const double dx = (xhi - xlo) * pad;
    const double dy = (yhi - ylo) * pad;
    return { xlo - dx, xhi + dx, ylo - dy, yhi + dy };
}

// -------------------------------------------------------------------------
// Tick generation
// -------------------------------------------------------------------------
inline double nice_step(double raw) {
    const double exp  = std::floor(std::log10(raw));
    const double f    = raw / std::pow(10.0, exp);
    // Thresholds are geometric midpoints: sqrt(1*2)≈1.41, sqrt(2*2.5)≈2.24, sqrt(2.5*5)≈3.54, sqrt(5*10)≈7.07
    double nice = (f < 1.5) ? 1.0 : (f < 2.25) ? 2.0 : (f < 3.5) ? 2.5 : (f < 7.0) ? 5.0 : 10.0;
    return nice * std::pow(10.0, exp);
}

inline std::vector<Tick> generate_ticks(double lo, double hi, int target = 7) {
    if (lo >= hi) return {};
    const double step  = nice_step((hi - lo) / target);
    const double first = std::ceil(lo / step) * step;
    std::vector<Tick> ticks;
    for (double v = first; v <= hi + step * 1e-6; v += step) {
        if (v < lo - step * 1e-6) continue;
        // ceil(lo/step)*step yields a *negative* zero whenever the axis starts
        // below zero, and "%g" prints its sign -- so the zero tick came out as
        // "-0" on nearly every figure. Comparing equal to 0.0 catches both
        // signed zeros; assigning the literal replaces it with a positive one.
        const double label_v = (v == 0.0) ? 0.0 : v;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", label_v);
        ticks.push_back({v, buf});
    }
    return ticks;
}

// -------------------------------------------------------------------------
// Build CoordTransform from explicit limits + plot rect
// -------------------------------------------------------------------------
inline CoordTransform make_transform(const AllPlotData& all,
                                     double xmin, double xmax,
                                     double ymin, double ymax,
                                     bool xlim_auto, bool ylim_auto,
                                     const PlotRect& pr,
                                     float win_w, float win_h)
{
    if (xlim_auto || ylim_auto) {
        const auto b = auto_scale(all);
        if (xlim_auto) { xmin = b.xmin; xmax = b.xmax; }
        if (ylim_auto) { ymin = b.ymin; ymax = b.ymax; }
    }
    return { xmin, xmax, ymin, ymax, pr.x, pr.y, pr.w, pr.h, win_w, win_h };
}

} // namespace sextant
