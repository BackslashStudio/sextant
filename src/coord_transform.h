#pragma once
#include "axis_scale.h"
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
    // Limits are data values; each axis' scale (see AxisScale) sits between
    // them and the pixels, so a linear map in scale space is all a drawer needs:
    // sx()/sy() take data there, sxmin()...syspan() give the view there.
    struct CoordTransform {
        double xmin, xmax, ymin, ymax;
        float px, py, pw, ph; // plot rect (top-left + size)
        float win_w = 0, win_h = 0; // full framebuffer size
        AxisScale xs{}, ys{};

        double sx(double x) const { return xs.fwd(x); }
        double sy(double y) const { return ys.fwd(y); }
        double sxmin() const { return xs.fwd(xmin); }
        double sxmax() const { return xs.fwd(xmax); }
        double symin() const { return ys.fwd(ymin); }
        double symax() const { return ys.fwd(ymax); }
        double sxspan() const { return sxmax() - sxmin(); }
        double syspan() const { return symax() - symin(); }

        float to_px(double x) const {
            return px + static_cast<float>((sx(x) - sxmin()) / sxspan()) * pw;
        }

        // Pixel y=0 is at the top; data y increases upward
        float to_py(double y) const {
            return py + ph - static_cast<float>((sy(y) - symin()) / syspan()) * ph;
        }

        // Inverse of to_px/to_py — screen pixel back to data space (pan/zoom).
        double to_data_x(float screen_x) const {
            return xs.inv(sxmin() + static_cast<double>((screen_x - px) / pw) * sxspan());
        }

        double to_data_y(float screen_y) const {
            return ys.inv(symin() + static_cast<double>((py + ph - screen_y) / ph) * syspan());
        }
    };

    // -------------------------------------------------------------------------
    // Pan/zoom: pixel deltas -> new limits (window-free, testable)
    // -------------------------------------------------------------------------
    struct AxisLimits {
        double xmin, xmax, ymin, ymax;
    };

    // Shift the view by a drag of (ddx_px, ddy_px) screen pixels — content
    // follows the cursor, like dragging a map. In scale space, so on a log axis
    // a drag moves the same number of decades anywhere.
    inline AxisLimits pan_limits(const CoordTransform& tr, float ddx_px, float ddy_px) {
        const double dx = -static_cast<double>(ddx_px / tr.pw) * tr.sxspan();
        // Screen y grows downward, data y upward, hence +.
        const double dy = static_cast<double>(ddy_px / tr.ph) * tr.syspan();
        return {tr.xs.inv(tr.sxmin() + dx), tr.xs.inv(tr.sxmax() + dx),
                tr.ys.inv(tr.symin() + dy), tr.ys.inv(tr.symax() + dy)};
    }

    // Scale the view around the data point under (cursor_x_px, cursor_y_px) by
    // factor (< 1 zooms in, > 1 zooms out) — that point stays fixed on screen.
    // In scale space, as pan_limits().
    inline AxisLimits zoom_limits(const CoordTransform& tr, float cursor_x_px, float cursor_y_px, float factor) {
        const double x0 = tr.sxmin(), x1 = tr.sxmax(), y0 = tr.symin(), y1 = tr.symax();
        // to_data_x()/to_data_y() short of their inverse map.
        const double cx = x0 + static_cast<double>((cursor_x_px - tr.px) / tr.pw) * (x1 - x0);
        const double cy = y0 + static_cast<double>((tr.py + tr.ph - cursor_y_px) / tr.ph) * (y1 - y0);
        return {
            tr.xs.inv(cx - (cx - x0) * factor), tr.xs.inv(cx + (x1 - cx) * factor),
            tr.ys.inv(cy - (cy - y0) * factor), tr.ys.inv(cy + (y1 - cy) * factor)
        };
    }

    // -------------------------------------------------------------------------
    // Auto-scale: walk all plot objects, return padded bounds
    // -------------------------------------------------------------------------
    struct DataBounds {
        double xmin, xmax, ymin, ymax;
    };

    // Padding around automatic limits, as a fraction of the range.
    inline constexpr double kAutoScalePad = 0.05;

    // An unpadded interval on one axis; empty until something is added.
    struct AxisSpan {
        double lo = std::numeric_limits<double>::max();
        double hi = -std::numeric_limits<double>::max();

        bool empty() const { return lo > hi; }

        void add(double v) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }

        void add(const AxisSpan& s) {
            lo = std::min(lo, s.lo);
            hi = std::max(hi, s.hi);
        }
    };

    // Automatic bounds of one axis, in two parts. `loose` (points, lines, bars,
    // error bars, an origin pin) is padded; `tight` (heatmap extents) is not,
    // so an image meets the frame edge, as matplotlib's imshow does.
    struct AutoAxis {
        AxisSpan loose, tight;

        bool empty() const { return loose.empty() && tight.empty(); }
    };

    // Give an axis a usable range: nothing at all reads as 0..1 and a single
    // value as +-0.5 around it, both as loose (padded) data. Idempotent.
    inline void settle_axis(AutoAxis& a) {
        AxisSpan whole = a.loose;
        whole.add(a.tight);
        if (whole.empty()) a.loose = {0.0, 1.0};
        else if (whole.lo == whole.hi) a.loose = {whole.lo - 0.5, whole.hi + 0.5};
    }

    // The padded interval, after settle_axis(). The pad distance is `pad` times
    // the whole range (both parts), applied to `loose` only, then `tight` is
    // added as it is -- so without a heatmap this is the plain padded range, and
    // with only heatmaps it is their exact extent.
    inline void pad_axis(AutoAxis a, double pad, double& lo, double& hi) {
        settle_axis(a);
        AxisSpan whole = a.loose;
        whole.add(a.tight);
        const double d = (whole.hi - whole.lo) * pad;
        AxisSpan out;
        if (!a.loose.empty()) out = {a.loose.lo - d, a.loose.hi + d};
        out.add(a.tight);
        lo = out.lo;
        hi = out.hi;
    }

    struct AutoBounds {
        AutoAxis x, y;
    };

    namespace detail {
        // auto_bounds() with each value passed through fx/fy first (scale
        // space); a NaN they return widens nothing.
        template <class FX, class FY>
        AutoBounds auto_bounds_in(const AllPlotData& all, FX fx, FY fy) {
            double xlo = std::numeric_limits<double>::max();
            double xhi = -std::numeric_limits<double>::max();
            double ylo = std::numeric_limits<double>::max();
            double yhi = -std::numeric_limits<double>::max();

            auto add = [](double v, double& lo, double& hi) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            };

            // Error bars widen the auto limits so the outermost bars aren't clipped.
            // For bars, `ys` is the heights (the tip).
            auto grow_err = [&](const CowVec<double>& xs, const CowVec<double>& ys,
                                const ErrorBarData& err) {
                const std::size_t n = std::min(xs.size(), ys.size());
                auto grow = [&](const CowVec<double>& ps, ErrOffsets (ErrorBarData::*at)(std::size_t) const,
                                auto f, double& lo, double& hi) {
                    for (std::size_t i = 0; i < n; ++i) {
                        const ErrOffsets e = (err.*at)(i);
                        add(f(ps[i] - e.lo), lo, hi);
                        add(f(ps[i] + e.hi), lo, hi);
                    }
                };
                if (err.has_x_cap()) grow(xs, &ErrorBarData::x_cap, fx, xlo, xhi);
                if (err.has_x_box()) grow(xs, &ErrorBarData::x_box, fx, xlo, xhi);
                if (err.has_y_cap()) grow(ys, &ErrorBarData::y_cap, fy, ylo, yhi);
                if (err.has_y_box()) grow(ys, &ErrorBarData::y_box, fy, ylo, yhi);
            };

            auto add_points = [&](const CowVec<double>& xs, const CowVec<double>& ys) {
                for (double v: xs) add(fx(v), xlo, xhi);
                for (double v: ys) add(fy(v), ylo, yhi);
            };

            for (const auto& lp: all.lines) {
                add_points(lp.x, lp.y);
                grow_err(lp.x, lp.y, lp.err);
            }
            for (const auto& sp: all.scatters) {
                add_points(sp.x, sp.y);
                grow_err(sp.x, sp.y, sp.err);
            }
            for (const auto& sp: all.scatter_z) {
                add_points(sp.x, sp.y);
                grow_err(sp.x, sp.y, sp.err);
            }
            for (const auto& bp: all.bars) {
                for (std::size_t i = 0; i < bp.centers.size(); ++i) {
                    const double half = bp.bar_width * 0.5;
                    add(fx(bp.centers[i] - half), xlo, xhi);
                    add(fx(bp.centers[i] + half), xlo, xhi);
                    // Y spans 0 to height (negative heights go below 0); on a log
                    // axis the baseline has no place and counts for nothing.
                    add(fy(0.0), ylo, yhi);
                    add(fy(bp.heights[i]), ylo, yhi);
                }
                // Error bars hang off the tip (heights), not the baseline.
                grow_err(bp.centers, bp.heights, bp.err);
            }
            AutoBounds b;
            b.x.loose = {xlo, xhi};
            b.y.loose = {ylo, yhi};
            for (const auto& hp: all.heatmaps) {
                // The heatmap's extent, unpadded; either range may be reversed.
                b.x.tight.add(fx(hp.xrange.lo));
                b.x.tight.add(fx(hp.xrange.hi));
                b.y.tight.add(fy(hp.yrange.lo));
                b.y.tight.add(fy(hp.yrange.hi));
            }
            return b;
        }
    } // namespace detail

    // Unpadded bounds of every plot object, split as AutoAxis describes.
    inline AutoBounds auto_bounds(const AllPlotData& all) {
        const auto id = [](double v) { return v; };
        return detail::auto_bounds_in(all, id, id);
    }

    // The same in each axis' scale space: a value the scale has no place for
    // (<= 0 on log) is skipped.
    inline AutoBounds auto_bounds(const AllPlotData& all, const AxisScale& xs, const AxisScale& ys) {
        if (xs.linear() && ys.linear()) return auto_bounds(all);
        return detail::auto_bounds_in(all, [&xs](double v) { return xs.fwd_strict(v); },
                                      [&ys](double v) { return ys.fwd_strict(v); });
    }

    // Padded automatic bounds: auto_bounds() through pad_axis().
    inline DataBounds auto_scale(const AllPlotData& all, double pad = kAutoScalePad) {
        const AutoBounds b = auto_bounds(all);
        DataBounds r{};
        pad_axis(b.x, pad, r.xmin, r.xmax);
        pad_axis(b.y, pad, r.ymin, r.ymax);
        return r;
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

    // `lo` is the axis end drawn first (left, bottom). A reversed axis (lo > hi,
    // set_ylim(hi, lo)) gets the same ticks in reverse, so ticks[0] stays the
    // one at the `lo` end, as the layout measures the end labels.
    inline std::vector<Tick> generate_ticks(double lo, double hi, int target = 7) {
        if (lo > hi) {
            std::vector<Tick> ticks = generate_ticks(hi, lo, target);
            std::reverse(ticks.begin(), ticks.end());
            return ticks;
        }
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
