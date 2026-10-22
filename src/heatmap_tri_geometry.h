#pragma once
// A heatmap_tri()'s triangles on screen, shared by the GL draw, the SVG raster,
// a plane's raster and the hit test, so all of them leave out the same
// triangles and interpolate the same way. Edges are straight in pixels and a
// value varies linearly across a triangle in pixels (matplotlib's tripcolor
// with shading="gouraud"), on any axis scale. Under a non-linear norm the
// value is interpolated and the norm applied per pixel.
#include "color_norm.h"
#include "colormaps.h"
#include "coord_transform.h"
#include "plot_objects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace sextant {

// A value on the colormap's 0..1 under a linear norm (not yet clamped or
// wrapped); vmin == vmax puts everything at 0, as a heatmap does.
inline double heatmap_tri_t(const HeatmapTriOptions& o, double v) {
    const double span = static_cast<double>(o.vmax) - static_cast<double>(o.vmin);
    return span != 0.0 ? (v - o.vmin) / span : 0.0;
}

// The norm a heatmap_tri draws with (linear when its own doesn't fit).
inline ColorNorm::Kind heatmap_tri_norm(const HeatmapTriOptions& o) {
    return norm_in_force(o.norm, o.vmin, o.vmax);
}

// What a vertex carries for interpolation: its colormap position under a
// linear norm (interpolating it is interpolating the value), else the value,
// which heatmap_tri_pos() normalises per pixel.
inline double heatmap_tri_attr(const HeatmapTriOptions& o, ColorNorm::Kind kind, double v) {
    return kind == ColorNorm::Kind::Linear ? heatmap_tri_t(o, v) : v;
}

// An interpolated heatmap_tri_attr() as a colormap position; a contourf's
// fill takes its own band colours (HeatmapTriPlot::band_t).
inline double heatmap_tri_pos(const HeatmapTriPlot& p, ColorNorm::Kind kind, double a) {
    if (kind == ColorNorm::Kind::Linear) return a;
    if (kind == ColorNorm::Kind::Boundaries && !p.band_t.empty()) {
        const std::ptrdiff_t b = norm_band(p.opts.norm.bounds, a);
        return b < 0 || static_cast<std::size_t>(b) >= p.band_t.size() ? std::numeric_limits<double>::quiet_NaN()
                                                                       : p.band_t[static_cast<std::size_t>(b)];
    }
    return norm_t(kind, p.opts.norm, p.opts.vmin, p.opts.vmax, a);
}

// Bands for the GL fragment's lookup under a boundary norm, as (boundary,
// band position) pairs, one per boundary (the last pair's position unused).
// Each position is the colormap entry the CPU picks for it (index / 255,
// already wrapped or clamped), so the GPU samples the same entry; a NaN
// position (a band with no colour) is -1, which the fragment discards.
inline std::vector<float> heatmap_band_texels(const std::vector<double>& bounds, const std::vector<double>& band_t,
                                              bool cyclic) {
    std::vector<float> out(bounds.size() * 2, 0.0f);
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        out[2 * i] = static_cast<float>(bounds[i]);
        if (i < band_t.size())
            out[2 * i + 1] = std::isnan(band_t[i]) ? -1.0f
                                                   : static_cast<float>(colormaps::index(band_t[i], cyclic)) / 255.0f;
    }
    return out;
}

// A boundary norm's own band positions (spread over the whole map).
inline std::vector<double> norm_band_positions(const ColorNorm& n) {
    std::vector<double> t(n.bounds.size() > 1 ? n.bounds.size() - 1 : 0);
    for (std::size_t i = 0; i < t.size(); ++i) t[i] = norm_band_t(i, t.size());
    return t;
}

// One drawn triangle: its vertices (in pixels, or in scale space for the hit
// test), their vertex indices, and which triangle of `tri` it is.
struct TriPx {
    double x[3], y[3];
    std::uint32_t v[3];
    std::uint32_t index;
};

// The triangles that draw, in `tri` order: none with a NaN value, a value the
// norm leaves without a colour (<= 0 under log) or a vertex the scale cannot
// place (<= 0 on Log). `map(x, y, ox, oy)` places a vertex. A boundary norm
// leaves out pixels, not triangles: a vertex outside the boundaries still
// draws the part of its triangles inside them.
template <class Map>
std::vector<TriPx> heatmap_tri_mapped(const HeatmapTriPlot& p, const AxisScale& xs, const AxisScale& ys,
                                      Map&& map) {
    std::vector<TriPx> out;
    const std::size_t n = p.x.size();
    if (p.y.size() != n || p.values.size() != n) return out;
    std::vector<double> px(n), py(n);
    std::vector<char> ok(n);
    const ColorNorm::Kind kind = heatmap_tri_norm(p.opts);
    for (std::size_t i = 0; i < n; ++i) {
        ok[i] = !std::isnan(p.values[i])
                && !(kind == ColorNorm::Kind::Log && norm_masks(kind, p.opts.norm, p.values[i]))
                && xs.valid(p.x[i]) && ys.valid(p.y[i]);
        if (ok[i]) map(p.x[i], p.y[i], px[i], py[i]);
    }
    out.reserve(p.tri.size() / 3);
    for (std::size_t t = 0; t + 2 < p.tri.size(); t += 3) {
        TriPx q;
        bool drawn = true;
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t v = p.tri[t + static_cast<std::size_t>(k)];
            if (v >= n || !ok[v]) {
                drawn = false;
                break;
            }
            q.v[k] = v;
            q.x[k] = px[v];
            q.y[k] = py[v];
        }
        if (!drawn) continue;
        q.index = static_cast<std::uint32_t>(t / 3);
        out.push_back(q);
    }
    return out;
}

// In pixels, through the transform.
inline std::vector<TriPx> heatmap_tri_pixels(const HeatmapTriPlot& p, const CoordTransform& tr) {
    return heatmap_tri_mapped(p, tr.xs, tr.ys, [&tr](double x, double y, double& ox, double& oy) {
        ox = tr.to_px(x);
        oy = tr.to_py(y);
    });
}

// Barycentric weights of (x, y) in `q`, or nothing outside it (edges count as
// inside) or for a degenerate triangle.
inline std::optional<std::array<double, 3>> tri_weights(const TriPx& q, double x, double y) {
    const double x0 = q.x[0], y0 = q.y[0], x1 = q.x[1], y1 = q.y[1], x2 = q.x[2], y2 = q.y[2];
    const double det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (det == 0.0) return std::nullopt;
    const double a = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / det;
    const double b = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / det;
    const double c = 1.0 - a - b;
    constexpr double eps = -1e-9;
    if (a < eps || b < eps || c < eps) return std::nullopt;
    return std::array<double, 3>{ a, b, c };
}

// The colormap position under each pixel centre of a W x H block whose top
// left is pixel (x0, y0): NaN where no triangle covers it, the later triangle
// where two do (it draws on top).
inline std::vector<float> rasterize_heatmap_tri(const HeatmapTriPlot& p, const CoordTransform& tr,
                                                int x0, int y0, int w, int h) {
    std::vector<float> out(static_cast<std::size_t>(std::max(w, 0)) * static_cast<std::size_t>(std::max(h, 0)),
                           std::numeric_limits<float>::quiet_NaN());
    if (w <= 0 || h <= 0) return out;
    const ColorNorm::Kind kind = heatmap_tri_norm(p.opts);
    for (const TriPx& q : heatmap_tri_pixels(p, tr)) {
        // The pixel box, clamped before the cast (a vertex can lie far off).
        const auto clampd = [](double v) { return std::clamp(v, -1e7, 1e7); };
        const double lx = clampd(std::min({ q.x[0], q.x[1], q.x[2] })), hx = clampd(std::max({ q.x[0], q.x[1], q.x[2] }));
        const double ly = clampd(std::min({ q.y[0], q.y[1], q.y[2] })), hy = clampd(std::max({ q.y[0], q.y[1], q.y[2] }));
        const int i0 = std::max(0, static_cast<int>(std::floor(lx - 0.5)) - x0);
        const int i1 = std::min(w - 1, static_cast<int>(std::ceil(hx - 0.5)) - x0);
        const int j0 = std::max(0, static_cast<int>(std::floor(ly - 0.5)) - y0);
        const int j1 = std::min(h - 1, static_cast<int>(std::ceil(hy - 0.5)) - y0);
        const double t[3] = { heatmap_tri_attr(p.opts, kind, p.values[q.v[0]]),
                              heatmap_tri_attr(p.opts, kind, p.values[q.v[1]]),
                              heatmap_tri_attr(p.opts, kind, p.values[q.v[2]]) };
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                const auto wts = tri_weights(q, x0 + i + 0.5, y0 + j + 0.5);
                if (!wts) continue;
                out[static_cast<std::size_t>(j) * static_cast<std::size_t>(w) + static_cast<std::size_t>(i)] =
                    static_cast<float>(heatmap_tri_pos(p, kind,
                                                       (*wts)[0] * t[0] + (*wts)[1] * t[1] + (*wts)[2] * t[2]));
            }
    }
    return out;
}

// What the cursor is over: the topmost triangle containing it, the value
// interpolated there, and the triangle's nearest vertex. Found in scale space
// from the cursor's data point (x, y): pixels are affine in it on a 2D axes,
// and a plane's sheet is linear, so the weights are the drawn ones.
struct TriHit {
    std::uint32_t triangle = 0;
    double value = 0.0;
    std::uint32_t nearest = 0;
};

inline std::optional<TriHit> heatmap_tri_at(const HeatmapTriPlot& p, const AxisScale& xs, const AxisScale& ys,
                                            double x, double y) {
    if (!xs.valid(x) || !ys.valid(y)) return std::nullopt;
    const std::vector<TriPx> tris = heatmap_tri_mapped(p, xs, ys, [&](double u, double v, double& ou, double& ov) {
        ou = xs.fwd(u);
        ov = ys.fwd(v);
    });
    const double sx = xs.fwd(x), sy = ys.fwd(y);
    for (auto it = tris.rbegin(); it != tris.rend(); ++it) {
        const auto wts = tri_weights(*it, sx, sy);
        if (!wts) continue;
        TriHit h;
        h.triangle = it->index;
        int best = 0;
        for (int k = 0; k < 3; ++k) {
            h.value += (*wts)[static_cast<std::size_t>(k)] * p.values[it->v[k]];
            if ((*wts)[static_cast<std::size_t>(k)] > (*wts)[static_cast<std::size_t>(best)]) best = k;
        }
        h.nearest = it->v[best];
        // Under a boundary norm, a point outside the boundaries is not drawn.
        if (norm_masks(heatmap_tri_norm(p.opts), p.opts.norm, h.value)) return std::nullopt;
        return h;
    }
    return std::nullopt;
}

// The triangles resampled onto an even n x n heatmap over their vertices' box
// (linear axes, as a plane's sheet): each cell the value interpolated at its
// centre, NaN (transparent) where no triangle covers it, with the same
// colormap and range (under a non-linear norm, the colormap positions over a
// linear 0..1). What a plane's SVG writes, as a heatmap's image or cells.
inline HeatmapPlot heatmap_tri_grid(const HeatmapTriPlot& p, int n) {
    HeatmapPlot g;
    double x0 = std::numeric_limits<double>::max(), x1 = -x0, y0 = x0, y1 = -x0;
    for (std::size_t i = 0; i < p.x.size() && i < p.y.size(); ++i) {
        x0 = std::min(x0, p.x[i]);
        x1 = std::max(x1, p.x[i]);
        y0 = std::min(y0, p.y[i]);
        y1 = std::max(y1, p.y[i]);
    }
    if (!(x1 > x0) || !(y1 > y0) || n < 1) return g;
    CoordTransform tr{ x0, x1, y0, y1, 0.0f, 0.0f, static_cast<float>(n), static_cast<float>(n),
                       static_cast<float>(n), static_cast<float>(n) };
    const std::vector<float> t = rasterize_heatmap_tri(p, tr, 0, 0, n, n);
    g.opts.cmap = p.opts.cmap;
    if (heatmap_tri_norm(p.opts) == ColorNorm::Kind::Linear) {
        const double span = static_cast<double>(p.opts.vmax) - static_cast<double>(p.opts.vmin);
        std::vector<float> v(t.size());
        for (std::size_t i = 0; i < t.size(); ++i)
            v[i] = std::isnan(t[i]) ? t[i] : static_cast<float>(p.opts.vmin + t[i] * span);
        g.data = std::move(v);
        g.opts.vmin = p.opts.vmin;
        g.opts.vmax = p.opts.vmax;
    } else {
        g.data = t;
        g.opts.vmin = 0.0f;
        g.opts.vmax = 1.0f;
    }
    g.rows = g.cols = n;
    g.xrange = { x0, x1 };
    g.yrange = { y0, y1 };
    g.opts.origin = "upper";  // raster row 0 is the top
    return g;
}

} // namespace sextant
