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
        float px, py, pw, ph; // plot rect (top-left + size)
        float win_w = 0, win_h = 0; // full framebuffer size

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
    // Pan/zoom: pixel deltas -> new limits (window-free, testable)
    // -------------------------------------------------------------------------
    struct AxisLimits {
        double xmin, xmax, ymin, ymax;
    };

    // Shift the view by a drag of (ddx_px, ddy_px) screen pixels — content
    // follows the cursor, like dragging a map.
    inline AxisLimits pan_limits(const CoordTransform& tr, float ddx_px, float ddy_px) {
        const double dx = -static_cast<double>(ddx_px / tr.pw) * (tr.xmax - tr.xmin);
        // Screen y grows downward, data y upward, hence +.
        const double dy = static_cast<double>(ddy_px / tr.ph) * (tr.ymax - tr.ymin);
        return {tr.xmin + dx, tr.xmax + dx, tr.ymin + dy, tr.ymax + dy};
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
    struct DataBounds {
        double xmin, xmax, ymin, ymax;
    };

    // Padding around automatic limits, as a fraction of the range. resolve_limits()
    // pads by hand with the same value.
    inline constexpr double kAutoScalePad = 0.05;

    inline DataBounds auto_scale(const AllPlotData& all, double pad = kAutoScalePad) {
        double xlo = std::numeric_limits<double>::max();
        double xhi = -std::numeric_limits<double>::max();
        double ylo = std::numeric_limits<double>::max();
        double yhi = -std::numeric_limits<double>::max();

        // Error bars widen the auto limits so the outermost bars aren't clipped.
        // For bars, `ys` is the heights (the tip).
        auto grow_err = [&](const CowVec<double>& xs, const CowVec<double>& ys,
                            const ErrorBarData& err) {
            const std::size_t n = std::min(xs.size(), ys.size());
            auto grow = [&](const CowVec<double>& ps, ErrOffsets (ErrorBarData::*at)(std::size_t) const,
                            double& lo, double& hi) {
                for (std::size_t i = 0; i < n; ++i) {
                    const ErrOffsets e = (err.*at)(i);
                    lo = std::min(lo, ps[i] - e.lo);
                    hi = std::max(hi, ps[i] + e.hi);
                }
            };
            if (err.has_x_cap()) grow(xs, &ErrorBarData::x_cap, xlo, xhi);
            if (err.has_x_box()) grow(xs, &ErrorBarData::x_box, xlo, xhi);
            if (err.has_y_cap()) grow(ys, &ErrorBarData::y_cap, ylo, yhi);
            if (err.has_y_box()) grow(ys, &ErrorBarData::y_box, ylo, yhi);
        };

        for (const auto& lp: all.lines) {
            for (double v: lp.x) {
                xlo = std::min(xlo, v);
                xhi = std::max(xhi, v);
            }
            for (double v: lp.y) {
                ylo = std::min(ylo, v);
                yhi = std::max(yhi, v);
            }
            grow_err(lp.x, lp.y, lp.err);
        }
        for (const auto& sp: all.scatters) {
            for (double v: sp.x) {
                xlo = std::min(xlo, v);
                xhi = std::max(xhi, v);
            }
            for (double v: sp.y) {
                ylo = std::min(ylo, v);
                yhi = std::max(yhi, v);
            }
            grow_err(sp.x, sp.y, sp.err);
        }
        for (const auto& sp: all.scatter_z) {
            for (double v: sp.x) {
                xlo = std::min(xlo, v);
                xhi = std::max(xhi, v);
            }
            for (double v: sp.y) {
                ylo = std::min(ylo, v);
                yhi = std::max(yhi, v);
            }
            grow_err(sp.x, sp.y, sp.err);
        }
        for (const auto& bp: all.bars) {
            for (std::size_t i = 0; i < bp.centers.size(); ++i) {
                const double half = bp.bar_width * 0.5;
                xlo = std::min(xlo, bp.centers[i] - half);
                xhi = std::max(xhi, bp.centers[i] + half);
                // Y spans 0 to height (negative heights go below 0)
                ylo = std::min(ylo, std::min(0.0, bp.heights[i]));
                yhi = std::max(yhi, std::max(0.0, bp.heights[i]));
            }
            // Error bars hang off the tip (heights), not the baseline.
            grow_err(bp.centers, bp.heights, bp.err);
        }
        for (const auto& hp: all.heatmaps) {
            // The heatmap's extent; either range may be reversed.
            xlo = std::min({xlo, hp.xrange.lo, hp.xrange.hi});
            xhi = std::max({xhi, hp.xrange.lo, hp.xrange.hi});
            ylo = std::min({ylo, hp.yrange.lo, hp.yrange.hi});
            yhi = std::max({yhi, hp.yrange.lo, hp.yrange.hi});
        }

        if (xlo > xhi) {
            xlo = 0;
            xhi = 1;
        }
        if (ylo > yhi) {
            ylo = 0;
            yhi = 1;
        }
        if (xlo == xhi) {
            xlo -= 0.5;
            xhi += 0.5;
        }
        if (ylo == yhi) {
            ylo -= 0.5;
            yhi += 0.5;
        }

        const double dx = (xhi - xlo) * pad;
        const double dy = (yhi - ylo) * pad;
        return {xlo - dx, xhi + dx, ylo - dy, yhi + dy};
    }

    // -------------------------------------------------------------------------
    // Tick generation
    // -------------------------------------------------------------------------
    inline double nice_step(double raw) {
        const double exp = std::floor(std::log10(raw));
        const double f = raw / std::pow(10.0, exp);
        // Thresholds are geometric midpoints: sqrt(1*2)≈1.41, sqrt(2*2.5)≈2.24, sqrt(2.5*5)≈3.54, sqrt(5*10)≈7.07
        double nice = (f < 1.5) ? 1.0 : (f < 2.25) ? 2.0 : (f < 3.5) ? 2.5 : (f < 7.0) ? 5.0 : 10.0;
        return nice * std::pow(10.0, exp);
    }

    inline std::vector<Tick> generate_ticks(double lo, double hi, int target = 7) {
        if (lo >= hi) return {};
        const double step = nice_step((hi - lo) / target);
        if (!(step > 0.0)) return {};
        const double first = std::ceil(lo / step) * step;
        std::vector<Tick> ticks;
        // `first + k * step`, not `v += step`: accumulation drifts and prints e.g.
        // "-2.77556e-17" instead of "0".
        for (int k = 0; ; ++k) {
            double v = first + k * step;
            if (v > hi + step * 1e-6) break;
            if (v < lo - step * 1e-6) continue;
            // Snap near-zero (within 1e-6 steps) to exactly 0, which also avoids
            // printing "-0".
            if (std::fabs(v) < step * 1e-6) v = 0.0;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", v);
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
                                         float win_w, float win_h) {
        if (xlim_auto || ylim_auto) {
            const auto b = auto_scale(all);
            if (xlim_auto) {
                xmin = b.xmin;
                xmax = b.xmax;
            }
            if (ylim_auto) {
                ymin = b.ymin;
                ymax = b.ymax;
            }
        }
        return {xmin, xmax, ymin, ymax, pr.x, pr.y, pr.w, pr.h, win_w, win_h};
    }
} // namespace sextant
