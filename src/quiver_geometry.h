#pragma once
// Axes::quiver() as arrows, shared by the GL path (whose vertex shader repeats
// arrow_outline() in GLSL), the SVG writer, plane geometry, the limits and the
// tests, so every output draws the same arrows.
//
// An arrow is a vector in data units (amp * (u, v)) put down at (x, y) by the
// pivot; its two ends are placed on the axis scales, so it points the way the
// data does. Across, it is pixels: a shaft `width` wide and a triangular head.
#include "axis_scale.h"
#include "color_norm.h"
#include "colormaps.h"
#include "plot_objects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace sextant {

// QuiverOptions::amp when unset: the mean length of the finite, nonzero
// vectors becomes extent / (1.8 * max(10, sqrt(N))), N the arrows whose four
// numbers are finite and `extent` their anchors' x extent (else their y extent,
// else 1). 1 when no vector qualifies.
inline double quiver_auto_amp(std::span<const double> x, std::span<const double> y,
                                std::span<const double> u, std::span<const double> v) {
    const std::size_t n = std::min({x.size(), y.size(), u.size(), v.size()});
    double xlo = 0.0, xhi = 0.0, ylo = 0.0, yhi = 0.0, sum = 0.0;
    std::size_t finite = 0, nonzero = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]) || !std::isfinite(u[i])
            || !std::isfinite(v[i]))
            continue;
        if (finite == 0) {
            xlo = xhi = x[i];
            ylo = yhi = y[i];
        } else {
            xlo = std::min(xlo, x[i]);
            xhi = std::max(xhi, x[i]);
            ylo = std::min(ylo, y[i]);
            yhi = std::max(yhi, y[i]);
        }
        ++finite;
        const double len = std::hypot(u[i], v[i]);
        if (len > 0.0 && std::isfinite(len)) {
            sum += len;
            ++nonzero;
        }
    }
    if (nonzero == 0) return 1.0;
    double extent = xhi - xlo;
    if (!(extent > 0.0)) extent = yhi - ylo;
    if (!(extent > 0.0) || !std::isfinite(extent)) extent = 1.0;
    const double mean = sum / static_cast<double>(nonzero);
    const double sn = std::max(10.0, std::sqrt(static_cast<double>(finite)));
    const double s = extent / (1.8 * sn * mean);
    return std::isfinite(s) && s > 0.0 ? s : 1.0;
}

// Calls fn(i, tx, ty, hx, hy) for each arrow drawn, with its tail (t) and tip
// (h) in scale space through xs/ys (identities on a plane). Left out: a
// non-finite number (c included, when colormapped), a c the norm leaves
// without a colour (<= 0 under log), a zero vector, an end with no place on
// its scale.
template <class F>
void for_each_quiver_arrow(const QuiverPlot& p, const AxisScale& xs, const AxisScale& ys, F&& fn) {
    const std::size_t n = p.count();
    const double s = p.amp();
    const bool mapped = p.colormapped();
    const ColorNorm::Kind kind = mapped ? norm_in_force(p.opts.norm, p.opts.vmin, p.opts.vmax)
                                        : ColorNorm::Kind::Linear;
    const double back = p.opts.pivot == QuiverPivot::Tip      ? 1.0
                        : p.opts.pivot == QuiverPivot::Middle ? 0.5
                                                              : 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double du = s * p.u[i], dv = s * p.v[i];
        if (!std::isfinite(p.x[i]) || !std::isfinite(p.y[i]) || !std::isfinite(du)
            || !std::isfinite(dv) || (du == 0.0 && dv == 0.0)
            || (mapped && !(i < p.c.size() && std::isfinite(p.c[i]) && !norm_masks(kind, p.c[i]))))
            continue;
        const double x0 = p.x[i] - back * du, y0 = p.y[i] - back * dv;
        const double x1 = x0 + du, y1 = y0 + dv;
        if (!xs.valid(x0) || !xs.valid(x1) || !ys.valid(y0) || !ys.valid(y1)) continue;
        const double tx = xs.fwd(x0), ty = ys.fwd(y0), hx = xs.fwd(x1), hy = ys.fwd(y1);
        if (!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(hx) || !std::isfinite(hy))
            continue;
        fn(i, tx, ty, hx, hy);
    }
}

// One arrow's outline in pixels, tail (tx, ty) to tip (hx, hy), into `out` as
// x, y pairs; returns the point count. 7 points (shaft corners at the tail,
// shaft and head corners at the head's base, the tip, then back down the other
// side), or 3 (a head alone) for an arrow shorter than its head, the head
// scaled to the arrow's length; 0 for an arrow under a hundredth of a pixel.
inline int arrow_outline(float tx, float ty, float hx, float hy, const QuiverOptions& o,
                         float (&out)[14]) {
    const float dx = hx - tx, dy = hy - ty;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 0.01f)) return 0;
    const float ux = dx / len, uy = dy / len;
    const float nx = -uy, ny = ux;
    float hl = std::max(o.head_length, 0.0f);
    float hw = 0.5f * std::max(o.head_width, 0.0f);
    if (len < hl) {
        hw *= len / hl;
        hl = len;
        const float bx = hx - ux * hl, by = hy - uy * hl;
        const float pts[6] = {bx + nx * hw, by + ny * hw, hx, hy, bx - nx * hw, by - ny * hw};
        std::copy(pts, pts + 6, out);
        return 3;
    }
    const float sw = 0.5f * std::max(o.width, 0.0f);
    const float bx = hx - ux * hl, by = hy - uy * hl;
    const float pts[14] = {
        tx + nx * sw, ty + ny * sw,
        bx + nx * sw, by + ny * sw,
        bx + nx * hw, by + ny * hw,
        hx, hy,
        bx - nx * hw, by - ny * hw,
        bx - nx * sw, by - ny * sw,
        tx - nx * sw, ty - ny * sw,
    };
    std::copy(pts, pts + 14, out);
    return 7;
}

// The triangles covering an outline of arrow_outline(), as emit(a, b, c) with
// point indices: the head, then the shaft's two. They never overlap, so a
// translucent arrow blends once.
template <class F>
void arrow_triangles(int points, F&& emit) {
    if (points == 3) {
        emit(0, 1, 2);
    } else if (points == 7) {
        emit(2, 3, 4);
        emit(0, 1, 5);
        emit(0, 5, 6);
    }
}

// Arrow i's color before the quiver's alpha: its c through cmap/vmin/vmax and
// the norm when colormapped (a cyclic map wraps), else the flat color.
inline Color quiver_arrow_color(const QuiverPlot& p, std::size_t i) {
    if (!p.colormapped()) return *p.opts.color;
    const uint8_t* lut = colormaps::get(p.opts.cmap);
    const ColorNorm::Kind kind = norm_in_force(p.opts.norm, p.opts.vmin, p.opts.vmax);
    double t = 0.0;
    if (kind != ColorNorm::Kind::Linear) {
        if (i < p.c.size()) t = norm_t(kind, p.opts.norm.center, p.opts.vmin, p.opts.vmax, p.c[i]);
    } else {
        const double range = static_cast<double>(p.opts.vmax) - p.opts.vmin;
        t = range != 0.0 && i < p.c.size() ? (p.c[i] - p.opts.vmin) / range : 0.0;
    }
    const uint8_t* c = &lut[colormaps::index(t, p.opts.cmap.is_cyclic()) * 4];
    return {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f};
}

// The points a quiver gives the automatic limits, as add(x, y) in scale space:
// both ends of every arrow drawn.
template <class Add>
void quiver_limit_points(const QuiverPlot& p, const AxisScale& xs, const AxisScale& ys, Add&& add) {
    for_each_quiver_arrow(p, xs, ys, [&](std::size_t, double tx, double ty, double hx, double hy) {
        add(tx, ty);
        add(hx, hy);
    });
}

// A quiver's legend key: an arrow of the default shape across the swatch,
// from x0 to x1 at height cy (pixels), as arrow_outline().
inline int legend_arrow_outline(float x0, float x1, float cy, float (&out)[14]) {
    return arrow_outline(x0, cy, x1, cy, QuiverOptions{}, out);
}

} // namespace sextant
