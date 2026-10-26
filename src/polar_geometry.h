#pragma once
// A polar axes' geometry (PolarAxes), shared by layout, both drawers, hover
// and navigation. Its objects are drawn by the 2D code: polar_sheet() turns
// lines, markers and bars into "unit" space -- the disc of radius 1 about the
// origin, x east and y north -- and the cell's CoordTransform maps the frame's
// box in that space onto the frame, so the frame is round.
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
    // Shown lines' and markers' r count, and bars' bases and tips; non-finite
    // points are skipped.
    inline Range polar_auto_rlim(const RenderSnapshot& s) {
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        const auto add = [&](double t, double r) {
            if (!std::isfinite(r) || !std::isfinite(t)) return;
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        };
        const auto points = [&](const auto& v) {
            for (const auto& p: v) {
                if (!object_shown(p)) continue;
                for (std::size_t i = 0; i < p.y.size(); ++i) add(p.x[i], p.y[i]);
            }
        };
        points(s.lines);
        points(s.scatters);
        points(s.scatter_z);
        for (const BarPlot& b: s.bars) {
            if (!object_shown(b)) continue;
            for (std::size_t i = 0; i < b.centers.size(); ++i) {
                add(b.centers[i], b.base_at(i));
                add(b.centers[i], b.tip_at(i));
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

    // Whether the point (θ, r) is drawn: r within the limits and the point,
    // where it lands, inside the frame (a sector's θ is taken modulo 2π, as
    // hover reads it back).
    inline bool polar_point_shown(const PolarMap& m, double theta, double r) {
        if (!std::isfinite(theta) || !std::isfinite(r)) return false;
        if (r < std::min(m.rmin, m.rmax) || r > std::max(m.rmin, m.rmax)) return false;
        return m.inside(m.unit_x(theta, r), m.unit_y(theta, r));
    }

    // One polar bar as drawn: an annulus sector about the unit origin, its
    // radii (unit) and screen angles (degrees counter-clockwise from +x) as
    // WedgePlot takes them.
    struct PolarBarSector {
        double inner = 0.0, outer = 0.0;
        double theta1 = 0.0, theta2 = 0.0;
    };

    // The bar centred on θ `centre`, `width` radians wide, r from `base` to
    // `tip`, as drawn: r clipped to the limits, θ to a sector (modulo 2π, so a
    // bar across a sector's start may give two pieces; a full circle never
    // cuts). Appends nothing when nothing shows. Width >= 2π is the whole ring.
    inline void polar_bar_sectors(const PolarMap& m, double centre, double width, double base, double tip,
                                  std::vector<PolarBarSector>& out) {
        if (!std::isfinite(centre) || !std::isfinite(width) || !std::isfinite(base) || !std::isfinite(tip)) return;
        const double two_pi = 2.0 * std::numbers::pi;
        const double rlo = std::min(m.rmin, m.rmax), rhi = std::max(m.rmin, m.rmax);
        const double r0 = std::clamp(std::min(base, tip), rlo, rhi);
        const double r1 = std::clamp(std::max(base, tip), rlo, rhi);
        if (!(r1 > r0) || !(width > 0.0)) return;
        const double ra = m.rho(r0), rb = m.rho(r1);
        const double inner = std::min(ra, rb), outer = std::max(ra, rb);
        const auto put = [&](double t0, double t1) {
            const double a0 = m.angle(t0) / kPolarDeg, a1 = m.angle(t1) / kPolarDeg;
            out.push_back({inner, outer, std::min(a0, a1), std::max(a0, a1)});
        };
        const double w = std::min(width, two_pi);
        if (m.full()) {
            put(centre - w / 2.0, centre + w / 2.0);
            return;
        }
        // The bar's start, moved by whole turns into [tmin, tmin + 2π).
        const double s0 = m.tmin + std::fmod(std::fmod(centre - w / 2.0 - m.tmin, two_pi) + two_pi, two_pi);
        for (const double st: {s0 - two_pi, s0}) {
            const double t0 = std::max(st, m.tmin), t1 = std::min(st + w, m.tmax);
            if (t1 > t0) put(t0, t1);
        }
    }

    // The sheet as the 2D code draws it, the limits the frame's box: every
    // line in unit space (polar_polyline(), plain style, no loop), the
    // markers that show (polar_point_shown()) at their unit points, and each
    // bar as wedges (polar_bar_sectors()) keeping its id, zorder and colours.
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
        // Points in unit space, their sizes (and z) kept in step.
        const auto points = [&](auto& p) {
            std::vector<double> px, py, pz, psz;
            for (std::size_t i = 0; i < p.x.size(); ++i) {
                if (!polar_point_shown(m, p.x[i], p.y[i])) continue;
                px.push_back(m.unit_x(p.x[i], p.y[i]));
                py.push_back(m.unit_y(p.x[i], p.y[i]));
                if (i < p.sizes.size()) psz.push_back(p.sizes[i]);
                if constexpr (requires { p.z; })
                    if (i < p.z.size()) pz.push_back(p.z[i]);
            }
            p.x = CowVec<double>(std::move(px));
            p.y = CowVec<double>(std::move(py));
            if (!p.sizes.empty()) p.sizes = CowVec<double>(std::move(psz));
            if constexpr (requires { p.z; }) p.z = CowVec<double>(std::move(pz));
        };
        for (ScatterPlot& p: out.scatters) points(p);
        for (ScatterZPlot& p: out.scatter_z) points(p);
        std::vector<PolarBarSector> sec;
        for (const BarPlot& b: out.bars) {
            for (std::size_t i = 0; i < b.centers.size(); ++i) {
                sec.clear();
                polar_bar_sectors(m, b.centers[i], b.bar_width, b.base_at(i), b.tip_at(i), sec);
                for (const PolarBarSector& q: sec) {
                    WedgePlot w;
                    w.cx = Pos(0.0);
                    w.cy = Pos(0.0);
                    w.r = Pos(q.outer);
                    w.theta1 = q.theta1;
                    w.theta2 = q.theta2;
                    w.inner = q.inner;
                    w.opts.color = b.opts.color;
                    w.opts.alpha = b.opts.alpha;
                    w.opts.edgecolor = b.opts.edgecolor;
                    w.opts.linewidth = b.opts.linewidth;
                    w.opts.visible = b.opts.visible;
                    w.opts.zorder = b.opts.zorder;
                    w.opts.show_legend = false;
                    w.data_stamp = b.data_stamp;
                    w.id = b.id;
                    out.wedges.push_back(std::move(w));
                }
            }
        }
        out.bars.clear();
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
