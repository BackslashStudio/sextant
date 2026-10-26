#pragma once
// A polar axes' geometry (PolarAxes), shared by layout, both drawers, hover
// and navigation. Its objects are drawn by the 2D code: polar_sheet() turns
// lines, markers, bars, areas and texts into "unit" space -- the disc of
// radius 1 about the origin, x east and y north -- and the cell's
// CoordTransform maps the frame's box in that space onto the frame, so the
// frame is round.
#include "coord_transform.h"
#include "fill_geometry.h"
#include "line_steps.h"
#include "plot_objects.h"
#include "shape_geometry.h"
#include "tick.h"
#include "ticker.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
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
    // Shown lines', markers', fills' and polygons' r count, and bars' bases
    // and tips; non-finite points are skipped.
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
        // Every region point on both curves (r across them, or along them
        // for a Vertical fill), and every polygon vertex.
        for (const FillPlot& f: s.fills) {
            if (!object_shown(f)) continue;
            const bool vert = f.vertical();
            for (const FillRun& run: fill_runs(f, AxisScale{}, AxisScale{}))
                for (std::size_t k = 0; k < run.size(); ++k) {
                    if (vert) {
                        add(run.a[k], run.r[k]);
                    } else {
                        add(run.r[k], run.a[k]);
                        add(run.r[k], run.b[k]);
                    }
                }
        }
        points(s.polys);
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

    // r limits: rlim set (rauto false) or automatic.
    inline Range polar_rlim(const RenderSnapshotPolar& s) {
        const PolarView& v = s.view;
        if (!v.rauto && std::isfinite(v.rmin) && std::isfinite(v.rmax) && v.rmin != v.rmax)
            return {v.rmin, v.rmax};
        return polar_auto_rlim(s.objects);
    }

    inline PolarMap polar_map(const PolarView& p, Range rlim) {
        PolarMap m;
        m.offset = p.theta_offset * kPolarDeg;
        m.dir = p.direction < 0 ? -1.0 : 1.0;
        m.rmin = rlim.lo;
        m.rmax = rlim.hi;
        m.tmin = p.thetamin * kPolarDeg;
        m.tmax = p.thetamax * kPolarDeg;
        return m;
    }

    inline PolarMap polar_map(const RenderSnapshotPolar& s) { return polar_map(s.view, polar_rlim(s)); }

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

    namespace polar_detail {
        struct ThetaR {
            double t, r;
        };

        // `poly` cut to where sign * (coordinate k - v) >= 0, k 0 for θ and 1
        // for r (Sutherland-Hodgman: a concave ring keeps the winding number
        // of every point inside, joined by zero-area bridges along the cut).
        inline void clip_half(std::vector<ThetaR>& poly, int k, double v, double sign,
                              std::vector<ThetaR>& tmp) {
            tmp.clear();
            const std::size_t n = poly.size();
            const auto f = [&](const ThetaR& p) { return sign * ((k == 0 ? p.t : p.r) - v); };
            for (std::size_t i = 0; i < n; ++i) {
                const ThetaR& a = poly[i];
                const ThetaR& b = poly[(i + 1) % n];
                const double fa = f(a), fb = f(b);
                if (fa >= 0.0) tmp.push_back(a);
                if ((fa >= 0.0) != (fb >= 0.0)) {
                    const double u = fa / (fa - fb);
                    ThetaR q{a.t + (b.t - a.t) * u, a.r + (b.r - a.r) * u};
                    (k == 0 ? q.t : q.r) = v;
                    tmp.push_back(q);
                }
            }
            poly.swap(tmp);
        }

        // To the r limits and, on a sector, raw θ in [tmin, tmax] (as lines
        // clip); fewer than three points left is nothing.
        inline void clip_frame(const PolarMap& m, std::vector<ThetaR>& poly, std::vector<ThetaR>& tmp) {
            clip_half(poly, 1, std::min(m.rmin, m.rmax), 1.0, tmp);
            clip_half(poly, 1, std::max(m.rmin, m.rmax), -1.0, tmp);
            if (!m.full()) {
                clip_half(poly, 0, m.tmin, 1.0, tmp);
                clip_half(poly, 0, m.tmax, -1.0, tmp);
            }
            if (poly.size() < 3) poly.clear();
        }

        inline bool finite_all(const std::vector<ThetaR>& p) {
            for (const ThetaR& q: p)
                if (!std::isfinite(q.t) || !std::isfinite(q.r)) return false;
            return true;
        }
    } // namespace polar_detail

    // Triangles in (θ, r), six numbers each (θ, r per corner), as drawn: each
    // clipped to the frame (polar_detail::clip_frame()), cut into strips at
    // most kPolarChord wide in θ, each strip fanned, appended to `out` as
    // three unit (x, y) points per triangle. Pieces never overlap, as the
    // source triangles don't.
    inline void polar_area_tris(const PolarMap& m, const std::vector<double>& tris, std::vector<float>& out) {
        using namespace polar_detail;
        std::vector<ThetaR> poly, strip, tmp;
        const auto put = [&](const ThetaR& q) {
            out.push_back(static_cast<float>(m.unit_x(q.t, q.r)));
            out.push_back(static_cast<float>(m.unit_y(q.t, q.r)));
        };
        for (std::size_t i = 0; i + 6 <= tris.size(); i += 6) {
            poly = {{tris[i], tris[i + 1]}, {tris[i + 2], tris[i + 3]}, {tris[i + 4], tris[i + 5]}};
            if (!finite_all(poly)) continue;
            clip_frame(m, poly, tmp);
            if (poly.empty()) continue;
            double tlo = poly[0].t, thi = tlo;
            for (const ThetaR& q: poly) {
                tlo = std::min(tlo, q.t);
                thi = std::max(thi, q.t);
            }
            const int k = std::max(1, static_cast<int>(std::ceil((thi - tlo) / kPolarChord - 1e-9)));
            for (int s = 0; s < k; ++s) {
                strip = poly;
                if (k > 1) {
                    if (s > 0) clip_half(strip, 0, tlo + (thi - tlo) * s / k, 1.0, tmp);
                    if (s + 1 < k) clip_half(strip, 0, tlo + (thi - tlo) * (s + 1) / k, -1.0, tmp);
                }
                for (std::size_t j = 1; j + 1 < strip.size(); ++j) {
                    put(strip[0]);
                    put(strip[j]);
                    put(strip[j + 1]);
                }
            }
        }
    }

    // A closed outline (θ[i], r[i]) as drawn, its fill the nonzero rule's:
    // clipped as polar_area_tris() clips, each edge cut into chords at most
    // kPolarChord apart, in unit space. Empty when nothing shows.
    inline PolarArea::Path polar_area_ring(const PolarMap& m, const std::vector<double>& th,
                                           const std::vector<double>& r) {
        using namespace polar_detail;
        PolarArea::Path out;
        out.closed = true;
        std::vector<ThetaR> poly, tmp;
        for (std::size_t i = 0; i < th.size() && i < r.size(); ++i) poly.push_back({th[i], r[i]});
        if (poly.size() < 3 || !finite_all(poly)) return out;
        clip_frame(m, poly, tmp);
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            const ThetaR& a = poly[i];
            const ThetaR& b = poly[(i + 1) % n];
            const int k = std::max(1, static_cast<int>(std::ceil(std::fabs(b.t - a.t) / kPolarChord - 1e-9)));
            for (int q = 0; q < k; ++q) {
                const double u = static_cast<double>(q) / k;
                const double t = a.t + (b.t - a.t) * u, rr = a.r + (b.r - a.r) * u;
                out.x.push_back(m.unit_x(t, rr));
                out.y.push_back(m.unit_y(t, rr));
            }
        }
        return out;
    }

    // A closed outline's edge as stroked: polar_polyline() looped, so it
    // breaks where the frame cuts the area (no stroke along the rim); never
    // cut, it is closed (its repeated last point dropped).
    inline PolarArea::Path polar_area_stroke(const PolarMap& m, const std::vector<double>& th,
                                             const std::vector<double>& r) {
        PolarArea::Path out;
        polar_polyline(m, th.data(), r.data(), std::min(th.size(), r.size()), DrawStyle::Default, true,
                       out.x, out.y);
        const std::size_t n = out.x.size();
        if (n < 3) return out;
        for (std::size_t i = 0; i < n; ++i)
            if (std::isnan(out.x[i])) return out;
        if (std::fabs(out.x[0] - out.x[n - 1]) < 1e-12 && std::fabs(out.y[0] - out.y[n - 1]) < 1e-12) {
            out.x.pop_back();
            out.y.pop_back();
            out.closed = true;
        }
        return out;
    }

    // A fill's regions in (θ, r) (identity scales: θ along it and r across,
    // swapped for a Vertical one): `part(tri, th, r)` per region, its
    // fill_triangles() (six numbers each) and its fill_outline().
    template <class F>
    void polar_fill_parts(const FillPlot& f, F&& part) {
        const bool vert = f.vertical();
        std::vector<double> tri, th, rr;
        for (const FillRun& run: fill_runs(f, AxisScale{}, AxisScale{})) {
            tri.clear();
            const auto corner = [&](double rc, double v) {
                tri.push_back(vert ? v : rc);
                tri.push_back(vert ? rc : v);
            };
            fill_triangles(run, [&](double r0, double v0, double r1, double v1, double r2, double v2) {
                corner(r0, v0);
                corner(r1, v1);
                corner(r2, v2);
            });
            th.clear();
            rr.clear();
            fill_outline(run, [&](double rc, double v) {
                th.push_back(vert ? v : rc);
                rr.push_back(vert ? rc : v);
            });
            part(tri, th, rr);
        }
    }

    // A fill as drawn: each region's polar_fill_parts() through the
    // functions above.
    inline PolarArea polar_fill_area(const PolarMap& m, const FillPlot& f) {
        PolarArea a;
        const bool edged = f.opts.linewidth > 0.0f && f.opts.linestyle != LineStyle::None;
        polar_fill_parts(f, [&](const std::vector<double>& tri, const std::vector<double>& th,
                                const std::vector<double>& rr) {
            polar_area_tris(m, tri, a.tris);
            PolarArea::Path ring = polar_area_ring(m, th, rr);
            if (!ring.x.empty()) a.rings.push_back(std::move(ring));
            if (edged) {
                PolarArea::Path st = polar_area_stroke(m, th, rr);
                if (!st.x.empty()) a.strokes.push_back(std::move(st));
            }
        });
        return a;
    }

    // A polygon's outline (θ, r) and its triangles (six numbers each),
    // ear-clipped in (θ, r): consecutive repeats and a closing repeat
    // dropped, as poly_geometry() drops them. False (nothing) for a
    // non-finite vertex or fewer than three left.
    inline bool polar_poly_parts(const PolyPlot& p, std::vector<double>& th, std::vector<double>& rr,
                                 std::vector<double>& tri) {
        th.clear();
        rr.clear();
        tri.clear();
        const std::size_t n = std::min(p.x.size(), p.y.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isfinite(p.x[i]) || !std::isfinite(p.y[i])) return false;
            if (!th.empty() && th.back() == p.x[i] && rr.back() == p.y[i]) continue;
            th.push_back(p.x[i]);
            rr.push_back(p.y[i]);
        }
        while (th.size() > 1 && th.front() == th.back() && rr.front() == rr.back()) {
            th.pop_back();
            rr.pop_back();
        }
        if (th.size() < 3) return false;
        std::vector<ShapePt> pts;
        for (std::size_t i = 0; i < th.size(); ++i)
            pts.push_back({static_cast<float>(th[i]), static_cast<float>(rr[i])});
        std::vector<std::uint32_t> idx;
        shape_detail::ear_clip(pts, idx);
        for (std::uint32_t k: idx) {
            tri.push_back(th[k]);
            tri.push_back(rr[k]);
        }
        return true;
    }

    // A polygon in (θ, r) as drawn: polar_poly_parts(), where its edges are
    // straight, through the functions above.
    inline PolarArea polar_poly_area(const PolarMap& m, const PolyPlot& p) {
        PolarArea a;
        std::vector<double> th, rr, tri;
        if (!polar_poly_parts(p, th, rr, tri)) return a;
        polar_area_tris(m, tri, a.tris);
        PolarArea::Path ring = polar_area_ring(m, th, rr);
        if (!ring.x.empty()) a.rings.push_back(std::move(ring));
        if (p.opts.linewidth > 0.0f && p.opts.linestyle != LineStyle::None) {
            PolarArea::Path st = polar_area_stroke(m, th, rr);
            if (!st.x.empty()) a.strokes.push_back(std::move(st));
        }
        return a;
    }

    // -------------------------------------------------------------------------
    // Data-space geometry (PolarRenderer): no view in it, so it is built once
    // per data change; the GPU maps (θ, r) to pixels and clips per pixel.
    // -------------------------------------------------------------------------

    // One unbroken stroke in (θ, r): `closed` joins its last point to its first.
    struct PolarStroke {
        std::vector<double> t, r;
        bool closed = false;
    };

    // Most chords one segment or strip is cut into (a θ span of ~1000 turns).
    inline constexpr int kPolarMaxChords = 1 << 18;

    inline int polar_chords(double span) {
        const double k = std::ceil(std::fabs(span) / kPolarChord - 1e-9);
        return k < 1.0 ? 1 : k > kPolarMaxChords ? kPolarMaxChords : static_cast<int>(k);
    }

    // A line's points (θ, r) as strokes: steps applied in (θ, r), the closing
    // segment added for `loop`, each segment cut into chords at most
    // kPolarChord apart in θ, a non-finite point breaking it (as
    // polar_polyline() without the frame). Exact repeats are dropped. A loop
    // never broken is one closed stroke; a broken one's last stroke runs on
    // into its first.
    inline void polar_line_strokes(const double* th, const double* r, std::size_t n, DrawStyle ds, bool loop,
                                   std::vector<PolarStroke>& out) {
        out.clear();
        StrokePath sp;
        sp.build(th, r, n, ds, loop);
        const std::size_t segs = sp.segment_count();
        PolarStroke cur;
        bool broken = false, first_ok = false, last_ok = false;
        const auto put = [&](double t, double rr) {
            if (!cur.t.empty() && cur.t.back() == t && cur.r.back() == rr) return;
            cur.t.push_back(t);
            cur.r.push_back(rr);
        };
        const auto flush = [&] {
            if (cur.t.size() >= 2) out.push_back(std::move(cur));
            cur = PolarStroke{};
        };
        for (std::size_t s = 0; s < segs; ++s) {
            const std::size_t i = s, j = (s + 1 == sp.n) ? 0 : s + 1;
            const double t0 = sp.x[i], r0 = sp.y[i], t1 = sp.x[j], r1 = sp.y[j];
            const bool ok = std::isfinite(t0) && std::isfinite(r0) && std::isfinite(t1) && std::isfinite(r1);
            if (s == 0) first_ok = ok;
            if (s + 1 == segs) last_ok = ok;
            if (!ok) {
                broken = true;
                flush();
                continue;
            }
            const int k = polar_chords(t1 - t0);
            for (int q = cur.t.empty() ? 0 : 1; q <= k; ++q)
                put(t0 + (t1 - t0) * q / k, r0 + (r1 - r0) * q / k);
            // A segment ending where it starts (k == 1, a repeat) adds nothing.
        }
        flush();
        if (!loop || out.empty()) return;
        if (!broken) {
            PolarStroke& c = out.front();
            if (c.t.size() >= 3 && c.t.back() == c.t.front() && c.r.back() == c.r.front()) {
                c.t.pop_back();
                c.r.pop_back();
                c.closed = true;
            }
        } else if (first_ok && last_ok && out.size() >= 2) {
            // The last stroke ends at point 0, where the first begins.
            PolarStroke& a = out.back();
            const PolarStroke& b = out.front();
            for (std::size_t q = 1; q < b.t.size(); ++q) {
                a.t.push_back(b.t[q]);
                a.r.push_back(b.r[q]);
            }
            out.front() = std::move(out.back());
            out.pop_back();
        }
    }

    // Triangles in (θ, r), six numbers each, cut into strips at most
    // kPolarChord wide in θ, each strip fanned, appended to `out` (θ, r per
    // corner): polar_area_tris() without the frame.
    inline void polar_area_strips(const std::vector<double>& tris, std::vector<double>& out) {
        using namespace polar_detail;
        std::vector<ThetaR> poly, strip, tmp;
        const auto put = [&](const ThetaR& q) {
            out.push_back(q.t);
            out.push_back(q.r);
        };
        for (std::size_t i = 0; i + 6 <= tris.size(); i += 6) {
            poly = {{tris[i], tris[i + 1]}, {tris[i + 2], tris[i + 3]}, {tris[i + 4], tris[i + 5]}};
            if (!finite_all(poly)) continue;
            double tlo = poly[0].t, thi = tlo;
            for (const ThetaR& q: poly) {
                tlo = std::min(tlo, q.t);
                thi = std::max(thi, q.t);
            }
            const int k = polar_chords(thi - tlo);
            for (int s = 0; s < k; ++s) {
                strip = poly;
                if (k > 1) {
                    if (s > 0) clip_half(strip, 0, tlo + (thi - tlo) * s / k, 1.0, tmp);
                    if (s + 1 < k) clip_half(strip, 0, tlo + (thi - tlo) * (s + 1) / k, -1.0, tmp);
                }
                for (std::size_t j = 1; j + 1 < strip.size(); ++j) {
                    put(strip[0]);
                    put(strip[j]);
                    put(strip[j + 1]);
                }
            }
        }
    }

    // One bar, unclipped: the ring sector from `base` to `tip` across
    // min(width, 2π) about `centre`, as strips at most kPolarChord wide
    // appended to `tris` ((θ, r) per corner), and its outline appended to
    // `edges` (two circles for a full turn).
    inline void polar_bar_parts(double centre, double width, double base, double tip, std::vector<double>& tris,
                                std::vector<PolarStroke>& edges) {
        if (!std::isfinite(centre) || !std::isfinite(width) || !std::isfinite(base) || !std::isfinite(tip)) return;
        const double w = std::min(std::fabs(width), 2.0 * std::numbers::pi);
        if (!(w > 0.0) || base == tip) return;
        const double t0 = centre - w / 2;
        const int k = polar_chords(w);
        const auto at = [&](int q) { return t0 + w * q / k; };
        for (int q = 0; q < k; ++q) {
            const double a = at(q), b = at(q + 1);
            for (const double v: {a, base, b, base, b, tip, a, base, b, tip, a, tip}) tris.push_back(v);
        }
        const bool full = w >= 2.0 * std::numbers::pi * (1.0 - 1e-12);
        if (full) {
            for (const double rr: {base, tip}) {
                PolarStroke c;
                c.closed = true;
                for (int q = 0; q < k; ++q) {
                    c.t.push_back(at(q));
                    c.r.push_back(rr);
                }
                edges.push_back(std::move(c));
            }
            return;
        }
        PolarStroke c;
        c.closed = true;
        for (int q = 0; q <= k; ++q) {
            c.t.push_back(at(q));
            c.r.push_back(base);
        }
        for (int q = k; q >= 0; --q) {
            c.t.push_back(at(q));
            c.r.push_back(tip);
        }
        edges.push_back(std::move(c));
    }

    // The texts as the 2D text pass places them: a Data text's (θ, r) and an
    // arrow's point at their unit points. A Data text whose point doesn't
    // show (polar_point_shown()) is dropped, unless clip_to_frame keeps one
    // on the far side of the r limits or the θ span; an arrow whose point
    // doesn't show is dropped. Frame-fraction and pixel texts pass as they
    // are; a text with mixed spaces (only an edit makes one) is dropped.
    inline std::vector<TextPlot> polar_texts(const RenderSnapshot& s, const PolarMap& m) {
        std::vector<TextPlot> out;
        for (const TextPlot& t: s.texts) {
            TextPlot q = t;
            TextContent& c = q.content;
            if (c.x.space != c.y.space) continue;
            if (c.x.space == Coords::Data) {
                const bool kept = polar_point_shown(m, c.x.v, c.y.v)
                                  || (t.opts.text.clip_to_frame && std::isfinite(c.x.v) && std::isfinite(c.y.v)
                                      && m.rho(c.y.v) >= 0.0);
                if (!kept) continue;
                const double ux = m.unit_x(c.x.v, c.y.v), uy = m.unit_y(c.x.v, c.y.v);
                c.x = Pos(ux);
                c.y = Pos(uy);
            }
            if (c.arrow) {
                if (!polar_point_shown(m, c.px, c.py)) continue;
                const double ux = m.unit_x(c.px, c.py), uy = m.unit_y(c.px, c.py);
                c.px = ux;
                c.py = uy;
            }
            out.push_back(std::move(q));
        }
        return out;
    }

    // The sheet as the 2D code draws it, the limits the frame's box: every
    // line in unit space (polar_polyline(), plain style, no loop), the
    // markers that show (polar_point_shown()) at their unit points, and each
    // bar as wedges (polar_bar_sectors()) keeping its id, zorder and colours,
    // each fill and polygon as a fill carrying its PolarArea, and the texts
    // placed (polar_texts()).
    inline RenderSnapshot polar_sheet(const RenderSnapshot& s, const PolarMap& m) {
        RenderSnapshot out = s;
        const PolarBox b = polar_box(m);
        out.xmin = b.xmin; out.xmax = b.xmax; out.ymin = b.ymin; out.ymax = b.ymax;
        out.xlim_auto = out.ylim_auto = false;
        out.xscale = AxisScale{};
        out.yscale = AxisScale{};
        out.xticks_override.reset();
        out.yticks_override.reset();
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
        // Fills and polygons as areas; a polygon becomes a fill keeping its
        // id, zorder and look, so one drawer takes both.
        for (FillPlot& f: out.fills) f.polar_area = std::make_shared<const PolarArea>(polar_fill_area(m, f));
        for (const PolyPlot& p: out.polys) {
            FillPlot f;
            f.opts.color = p.opts.color;
            f.opts.alpha = p.opts.alpha;
            f.opts.edgecolor = p.opts.edgecolor;
            f.opts.linewidth = p.opts.linewidth;
            f.opts.linestyle = p.opts.linestyle;
            f.opts.name = p.opts.name;
            f.opts.show_legend = false;
            f.opts.visible = p.opts.visible;
            f.opts.zorder = p.opts.zorder;
            f.data_stamp = p.data_stamp;
            f.id = p.id;
            f.polar_area = std::make_shared<const PolarArea>(polar_poly_area(m, p));
            out.fills.push_back(std::move(f));
        }
        out.polys.clear();
        out.texts = polar_texts(s, m);
        return out;
    }

    // The spokes' θ (degrees) and labels: set_thetagrids() ones, or every 45°
    // in the span (360 dropped on a full circle, where 0 is).
    inline std::vector<Tick> polar_theta_ticks(const RenderSnapshotPolar& s) {
        const PolarView& p = s.view;
        std::vector<Tick> out;
        const auto in_span = [&](double deg) {
            return deg >= p.thetamin - 1e-9 && deg <= p.thetamax + 1e-9;
        };
        const bool full = p.thetamax - p.thetamin >= 360.0 - 1e-9;
        if (s.style.thetagrids_override) {
            for (const Tick& t: *s.style.thetagrids_override)
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
    inline std::vector<Tick> polar_r_ticks(const RenderSnapshotPolar& s, Range rlim) {
        const PolarStyle& p = s.style;
        std::vector<Tick> all = p.rticks_override
                                    ? relabel_ticks(*p.rticks_override, p.rtick_format)
                                    : ticker_detail::linear_majors(rlim.lo, rlim.hi, kPolarRTicks, p.rtick_format);
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
