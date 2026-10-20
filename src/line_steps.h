#pragma once
// The one step expansion (LineOptions::drawstyle), shared by the GL line cache,
// the SVG writer and plane geometry, so every output strokes the same path.
#include "sextant/style.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace sextant {

// Appends the stepped path through points (x[i], y[i]), i < n, to ox/oy.
// Between neighbours a and b it emits a, then the corner(s): StepsPre
// (a.x, b.y); StepsPost (b.x, a.y); StepsMid (m, a.y) (m, b.y) with m the mean
// of the two x values in data units. An open path ends with the last point;
// a looped one steps the closing pair too and ends on its corner, so the
// caller's closing segment runs from there to the first point. A corner of a
// pair with a non-finite end is NaN, so the pair draws nothing (as a straight
// segment with a NaN end does). Default copies the points.
inline void step_vertices(const double* x, const double* y, std::size_t n,
                          DrawStyle ds, bool loop,
                          std::vector<double>& ox, std::vector<double>& oy) {
    ox.clear();
    oy.clear();
    if (ds == DrawStyle::Default || n < 2) {
        ox.assign(x, x + n);
        oy.assign(y, y + n);
        return;
    }
    const std::size_t per = ds == DrawStyle::StepsMid ? 3 : 2;
    ox.reserve(n * per);
    oy.reserve(n * per);
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    auto put = [&](double px, double py) {
        ox.push_back(px);
        oy.push_back(py);
    };
    const std::size_t pairs = loop ? n : n - 1;
    for (std::size_t a = 0; a < pairs; ++a) {
        const std::size_t b = a + 1 == n ? 0 : a + 1;
        put(x[a], y[a]);
        const bool ok = std::isfinite(x[a]) && std::isfinite(y[a])
                        && std::isfinite(x[b]) && std::isfinite(y[b]);
        switch (ds) {
            case DrawStyle::StepsPre:
                put(ok ? x[a] : nan, ok ? y[b] : nan);
                break;
            case DrawStyle::StepsPost:
                put(ok ? x[b] : nan, ok ? y[a] : nan);
                break;
            default: {
                const double m = ok ? 0.5 * (x[a] + x[b]) : nan;
                put(m, ok ? y[a] : nan);
                put(m, ok ? y[b] : nan);
                break;
            }
        }
    }
    if (!loop) put(x[n - 1], y[n - 1]);
}

// The path a 2D line strokes: its own points for Default (no copy), else the
// expansion held in `sx`/`sy`. Segments join neighbours; `loop` adds the
// closing one from the last vertex back to the first.
struct StrokePath {
    std::vector<double> sx, sy;
    const double* x = nullptr;
    const double* y = nullptr;
    std::size_t n = 0;
    bool loop = false;

    void build(const double* px, const double* py, std::size_t count, DrawStyle ds, bool closed) {
        loop = closed;
        if (ds == DrawStyle::Default || count < 2) {
            x = px;
            y = py;
            n = count;
            return;
        }
        step_vertices(px, py, count, ds, closed, sx, sy);
        x = sx.data();
        y = sy.data();
        n = sx.size();
    }

    std::size_t segment_count() const {
        if (n < 2) return 0;
        return loop ? n : n - 1;
    }
};

// The stroke shader's miter limit, which is SVG's default stroke-miterlimit.
inline constexpr float k_stroke_miter_limit = 4.0f;

// True where the stroke shader bevels the joint at j of segments a->j and j->b
// (pixels) although the turn is within the miter limit: the inner corner would
// slide past half of the shorter segment and fold its quad over. An SVG viewer
// miters there, so svg_writer bevels such joints itself. Mirrors the shader's
// miter_joint() (data_renderer.cpp); keep the two in step.
inline bool stroke_short_bevel(float ax, float ay, float jx, float jy, float bx, float by,
                               float half_width) {
    const float dax = jx - ax, day = jy - ay, dbx = bx - jx, dby = by - jy;
    const float la = std::sqrt(dax * dax + day * day), lb = std::sqrt(dbx * dbx + dby * dby);
    if (!(la > 1e-6f) || !(lb > 1e-6f)) return false;
    const float nbx = -dby / lb, nby = dbx / lb;
    float mx = -day / la + nbx, my = dax / la + nby;
    const float ml = std::sqrt(mx * mx + my * my);
    if (ml < 1e-3f) return false;
    mx /= ml;
    my /= ml;
    const float den = mx * nbx + my * nby;
    if (den <= 1.0f / k_stroke_miter_limit) return false;
    return half_width * std::sqrt(std::max(1.0f - den * den, 0.0f)) > 0.5f * std::min(la, lb) * den;
}

} // namespace sextant
