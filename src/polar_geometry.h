#pragma once
// A polar axes' geometry (PolarAxes), shared by layout, both drawers, hover
// and navigation. Its lines are drawn by the 2D line code: polar_sheet()
// turns them into "unit" space -- the disc of radius 1 about the origin, x
// east and y north -- and the cell's CoordTransform maps the frame's box in
// that space onto the frame, so the frame is round.
#include "coord_transform.h"
#include "line_steps.h"
#include "plot_objects.h"
#include "tick.h"
#include "ticker.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace sextant {
    inline constexpr double kPolarDeg = std::numbers::pi / 180.0;
    // A curve is cut into chords at most this much θ apart (1°).
    inline constexpr double kPolarChord = kPolarDeg;
    // The r axis aims for this many ticks.
    inline constexpr int kPolarRTicks = 5;

    // One polar frame: θ (radians, data) to the screen angle, r to the radius.
    struct PolarMap {
        double offset = 0.0;            // radians
        double dir = 1.0;
        double rmin = 0.0, rmax = 1.0;
        double tmin = 0.0, tmax = 2.0 * std::numbers::pi;   // radians

        bool full() const { return tmax - tmin >= 2.0 * std::numbers::pi * (1.0 - 1e-12); }
        // The screen angle (radians, counterclockwise from east) of θ.
        double angle(double theta) const { return offset + dir * theta; }
        // r as a fraction of the frame's radius.
        double rho(double r) const { return (r - rmin) / (rmax - rmin); }
        double unit_x(double theta, double r) const { return rho(r) * std::cos(angle(theta)); }
        double unit_y(double theta, double r) const { return rho(r) * std::sin(angle(theta)); }

        // θ in [tmin, tmin + 2π) and r at a unit point; the centre reads θ = tmin.
        void from_unit(double ux, double uy, double& theta, double& r) const {
            const double rh = std::hypot(ux, uy);
            r = rmin + rh * (rmax - rmin);
            const double a = rh > 0.0 ? std::atan2(uy, ux) : angle(tmin);
            double t = (a - offset) * dir;
            const double two_pi = 2.0 * std::numbers::pi;
            t = tmin + std::fmod(std::fmod(t - tmin, two_pi) + two_pi, two_pi);
            theta = t;
        }

        // Whether a unit point lies inside the frame (the disc, or the sector).
        bool inside(double ux, double uy) const {
            if (ux * ux + uy * uy > 1.0 + 1e-12) return false;
            if (full()) return true;
            double t, r;
            from_unit(ux, uy, t, r);
            return t <= tmax + 1e-12;
        }
    };

    // A box in unit space.
    struct PolarBox {
        double xmin, xmax, ymin, ymax;
    };

    // r's limits as drawn: set ones, or automatic (see PolarAxes::rlim()).
    // Only shown lines count; non-finite points are skipped.
    inline Range polar_auto_rlim(const RenderSnapshot& s) {
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const LinePlot& l: s.lines) {
            if (!l.opts.visible) continue;
            for (std::size_t i = 0; i < l.y.size(); ++i) {
                const double r = l.y[i];
                if (!std::isfinite(r) || !std::isfinite(l.x[i])) continue;
                lo = std::min(lo, r);
                hi = std::max(hi, r);
            }
        }
        if (!(lo <= hi)) return {0.0, 1.0};
        lo = std::min(lo, 0.0);
        if (hi <= lo) hi = lo + 1.0;
        const double step = nice_step((hi - lo) / kPolarRTicks);
        if (!(step > 0.0)) return {lo, hi};
        if (lo < 0.0) lo = std::floor(lo / step) * step;
        hi = std::ceil(hi / step - 1e-9) * step;
        if (hi <= lo) hi = lo + step;
        return {lo, hi};
    }

    // r limits: rlim set (ylim_auto false) or automatic.
    inline Range polar_rlim(const RenderSnapshot& s) {
        if (!s.ylim_auto && std::isfinite(s.ymin) && std::isfinite(s.ymax) && s.ymin != s.ymax)
            return {s.ymin, s.ymax};
        return polar_auto_rlim(s);
    }

    inline PolarMap polar_map(const PolarState& p, Range rlim) {
        PolarMap m;
        m.offset = p.theta_offset * kPolarDeg;
        m.dir = p.direction < 0 ? -1.0 : 1.0;
        m.rmin = rlim.lo;
        m.rmax = rlim.hi;
        m.tmin = p.thetamin * kPolarDeg;
        m.tmax = p.thetamax * kPolarDeg;
        return m;
    }

    inline PolarMap polar_map(const RenderSnapshot& s) { return polar_map(*s.polar, polar_rlim(s)); }

    // The frame's box in unit space: the disc's, or the sector's (its arc's
    // ends, the compass points it passes and the centre).
    inline PolarBox polar_box(const PolarMap& m) {
        if (m.full()) return {-1.0, 1.0, -1.0, 1.0};
        PolarBox b{0.0, 0.0, 0.0, 0.0};
        const auto add = [&](double a) {
            b.xmin = std::min(b.xmin, std::cos(a));
            b.xmax = std::max(b.xmax, std::cos(a));
            b.ymin = std::min(b.ymin, std::sin(a));
            b.ymax = std::max(b.ymax, std::sin(a));
        };
        const double a0 = std::min(m.angle(m.tmin), m.angle(m.tmax));
        const double a1 = std::max(m.angle(m.tmin), m.angle(m.tmax));
        add(a0);
        add(a1);
        const double q = std::numbers::pi / 2.0;
        for (double k = std::ceil(a0 / q); k * q <= a1; k += 1.0) add(k * q);
        // Exact compass values, not cos(π/2)'s 6e-17.
        const auto snap = [](double& v) { if (std::fabs(v) < 1e-12) v = 0.0; };
        snap(b.xmin); snap(b.xmax); snap(b.ymin); snap(b.ymax);
        return b;
    }

    // The frame outline in unit space: the circle, or the sector's two radii
    // and its arc, as a closed polygon (chords kPolarChord apart).
    inline void polar_outline(const PolarMap& m, std::vector<double>& ux, std::vector<double>& uy) {
        ux.clear();
        uy.clear();
        const double t0 = m.full() ? 0.0 : m.tmin;
        const double t1 = m.full() ? 2.0 * std::numbers::pi : m.tmax;
        const int n = std::max(1, static_cast<int>(std::ceil((t1 - t0) / kPolarChord - 1e-9)));
        if (!m.full()) {
            ux.push_back(0.0);
            uy.push_back(0.0);
        }
        for (int k = 0; k <= n; ++k) {
            if (m.full() && k == n) break;
            const double a = m.angle(t0 + (t1 - t0) * k / n);
            ux.push_back(std::cos(a));
            uy.push_back(std::sin(a));
        }
    }

    // One ring (r = const) across the frame's θ span, open for a sector.
    inline void polar_ring(const PolarMap& m, double r, std::vector<double>& ux, std::vector<double>& uy) {
        ux.clear();
        uy.clear();
        const double rh = m.rho(r);
        const double t0 = m.full() ? 0.0 : m.tmin;
        const double t1 = m.full() ? 2.0 * std::numbers::pi : m.tmax;
        const int n = std::max(1, static_cast<int>(std::ceil((t1 - t0) / kPolarChord - 1e-9)));
        for (int k = 0; k <= n; ++k) {
            const double a = m.angle(t0 + (t1 - t0) * k / n);
            ux.push_back(rh * std::cos(a));
            uy.push_back(rh * std::sin(a));
        }
    }

    // One line's points (θ, r) as drawn, in unit space: steps applied in
    // (θ, r), the closing segment added for `loop`, each segment clipped to r
    // in [rmin, rmax] (and θ to the sector) and cut into chords at most
    // kPolarChord apart. NaN separates the runs.
    inline void polar_polyline(const PolarMap& m, const double* th, const double* r, std::size_t n,
                               DrawStyle ds, bool loop, std::vector<double>& ox, std::vector<double>& oy) {
        ox.clear();
        oy.clear();
        StrokePath sp;
        sp.build(th, r, n, ds, loop);
        const std::size_t segs = sp.segment_count();
        constexpr double nan = std::numeric_limits<double>::quiet_NaN();
        const double rlo = std::min(m.rmin, m.rmax), rhi = std::max(m.rmin, m.rmax);
        const bool sector = !m.full();
        bool open = false;   // the last point put ends a run still going
        const auto put = [&](double t, double rr) {
            ox.push_back(m.unit_x(t, rr));
            oy.push_back(m.unit_y(t, rr));
        };
        const auto brk = [&] {
            if (open) {
                ox.push_back(nan);
                oy.push_back(nan);
            }
            open = false;
        };
        // [lo, hi] narrowed to where a + (b - a) t lies in [vlo, vhi].
        const auto narrow = [](double a, double b, double vlo, double vhi, double& lo, double& hi) {
            const double d = b - a;
            if (d == 0.0) {
                if (a < vlo || a > vhi) hi = -1.0;
                return;
            }
            double t0 = (vlo - a) / d, t1 = (vhi - a) / d;
            if (t0 > t1) std::swap(t0, t1);
            lo = std::max(lo, t0);
            hi = std::min(hi, t1);
        };
        for (std::size_t s = 0; s < segs; ++s) {
            const std::size_t i = s, j = (s + 1 == sp.n) ? 0 : s + 1;
            const double t0 = sp.x[i], r0 = sp.y[i], t1 = sp.x[j], r1 = sp.y[j];
            if (!std::isfinite(t0) || !std::isfinite(r0) || !std::isfinite(t1) || !std::isfinite(r1)) {
                brk();
                continue;
            }
            double lo = 0.0, hi = 1.0;
            narrow(r0, r1, rlo, rhi, lo, hi);
            if (sector) narrow(t0, t1, m.tmin, m.tmax, lo, hi);
            if (!(lo <= hi)) {
                brk();
                continue;
            }
            // A run picks up where the last segment ended only if it ended unclipped.
            if (lo > 0.0) brk();
            const int k = std::max(1, static_cast<int>(std::ceil(std::fabs(t1 - t0) * (hi - lo) / kPolarChord - 1e-9)));
            for (int q = open ? 1 : 0; q <= k; ++q) {
                const double u = lo + (hi - lo) * q / k;
                put(t0 + (t1 - t0) * u, r0 + (r1 - r0) * u);
            }
            open = true;
            if (hi < 1.0) brk();
        }
        // No trailing break.
        if (!ox.empty() && std::isnan(ox.back())) {
            ox.pop_back();
            oy.pop_back();
        }
    }

    // The sheet as the 2D line code draws it: every line in unit space
    // (polar_polyline(), plain style, no loop), the limits the frame's box.
    // Polar axes hold lines only.
    inline RenderSnapshot polar_sheet(const RenderSnapshot& s, const PolarMap& m) {
        RenderSnapshot out = s;
        const PolarBox b = polar_box(m);
        out.xmin = b.xmin; out.xmax = b.xmax; out.ymin = b.ymin; out.ymax = b.ymax;
        out.xlim_auto = out.ylim_auto = false;
        out.xscale = AxisScale{};
        out.yscale = AxisScale{};
        out.xticks_override.reset();
        out.yticks_override.reset();
        out.polar.reset();
        std::vector<double> ox, oy;
        for (LinePlot& l: out.lines) {
            polar_polyline(m, l.x.data(), l.y.data(), l.x.size(), l.opts.drawstyle, l.opts.loop, ox, oy);
            l.x = CowVec<double>(ox);
            l.y = CowVec<double>(oy);
            l.opts.drawstyle = DrawStyle::Default;
            l.opts.loop = false;
        }
        return out;
    }

    // The spokes' θ (degrees) and labels: set_thetagrids() ones, or every 45°
    // in the span (360 dropped on a full circle, where 0 is).
    inline std::vector<Tick> polar_theta_ticks(const RenderSnapshot& s) {
        const PolarState& p = *s.polar;
        std::vector<Tick> out;
        const auto in_span = [&](double deg) {
            return deg >= p.thetamin - 1e-9 && deg <= p.thetamax + 1e-9;
        };
        const bool full = p.thetamax - p.thetamin >= 360.0 - 1e-9;
        if (s.xticks_override) {
            for (const Tick& t: *s.xticks_override)
                if (std::isfinite(t.value) && in_span(t.value)) out.push_back(t);
            return out;
        }
        for (double deg = std::ceil(p.thetamin / 45.0 - 1e-9) * 45.0; deg <= p.thetamax + 1e-9; deg += 45.0) {
            if (full && deg >= p.thetamin + 360.0 - 1e-9) break;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g\xC2\xB0", std::fabs(deg) < 1e-9 ? 0.0 : deg);
            out.push_back({deg, buf, false});
        }
        return out;
    }

    // The rings' r and labels: set_rticks() ones or ~kPolarRTicks generated
    // (relabelled by the r tick format), only those past rmin and up to rmax
    // (the centre has none).
    inline std::vector<Tick> polar_r_ticks(const RenderSnapshot& s, Range rlim) {
        std::vector<Tick> all = s.yticks_override
                                    ? relabel_ticks(*s.yticks_override, s.ytick_format)
                                    : ticker_detail::linear_majors(rlim.lo, rlim.hi, kPolarRTicks, s.ytick_format);
        std::vector<Tick> out;
        const double lo = std::min(rlim.lo, rlim.hi), hi = std::max(rlim.lo, rlim.hi);
        const double eps = (hi - lo) * 1e-9;
        for (Tick& t: all) {
            if (!std::isfinite(t.value)) continue;
            if (std::fabs(t.value - rlim.lo) <= eps) continue;
            if (t.value < lo - eps || t.value > hi + eps) continue;
            out.push_back(std::move(t));
        }
        return out;
    }
} // namespace sextant
