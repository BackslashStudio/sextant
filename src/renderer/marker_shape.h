#pragma once
#include <sextant/style.h>
#include <algorithm>
#include <cmath>

namespace sextant {
    // A line marker's stroke width when its `edge_linewidth` is 0, in pixels.
    inline constexpr float kLineMarkerWidth = 1.5f;

    // ThinCross, ThinPlus, VLine and HLine: drawn as a stroke in the fill's color
    // (a filled ring `line_marker_width()` thick), never outlined.
    inline bool is_line_marker(MarkerStyle m) {
        return m == MarkerStyle::ThinCross || m == MarkerStyle::ThinPlus ||
               m == MarkerStyle::VLine || m == MarkerStyle::HLine;
    }

    inline float line_marker_width(float edge_linewidth) {
        return edge_linewidth > 0.0f ? edge_linewidth : kLineMarkerWidth;
    }

    // One marker as 2D primitives, shared by the SVG data markers and both legends
    // so keys match the picture. The GPU tests shapes in the shader instead (see
    // marker_sd in data_renderer.cpp); every shape is a filled region, so an
    // outline follows its boundary.
    struct MarkerShape {
        enum class Form {
            None, // nothing is drawn
            Disc, // centre + radius
            Rect, // centred square, half-side = radius
            Polygon // filled ring of `count` points
        };

        Form form = Form::None;
        float cx = 0.0f, cy = 0.0f;
        float radius = 0.0f; // Disc and Rect
        // Polygon: the ring (a Plus or Cross has twelve corners, a Star ten).
        float pts[12][2]{};
        int count = 0;
    };

    namespace marker_shape_detail {
        // Moves every edge of a ring (around the origin) `h` inward, each corner
        // along its mitre, as the shader's signed distance does. `h` stops short
        // of the nearest edge line, where the ring would collapse.
        inline void inset_ring(float (&p)[12][2], int n, float h) {
            if (h <= 0.0f || n < 3) return;
            float area = 0.0f;
            for (int i = 0; i < n; ++i) {
                const int j = (i + 1) % n;
                area += p[i][0] * p[j][1] - p[j][0] * p[i][1];
            }
            const float sgn = area > 0.0f ? 1.0f : -1.0f;
            float nx[12], ny[12], dmin = 1e30f;
            for (int i = 0; i < n; ++i) {
                const int j = (i + 1) % n;
                const float dx = p[j][0] - p[i][0], dy = p[j][1] - p[i][1];
                const float len = std::max(1e-12f, std::hypot(dx, dy));
                nx[i] = -dy / len * sgn;    // inward normal of edge i -> i+1
                ny[i] = dx / len * sgn;
                dmin = std::min(dmin, std::fabs(p[i][0] * nx[i] + p[i][1] * ny[i]));
            }
            h = std::min(h, 0.99f * dmin);
            float out[12][2];
            for (int i = 0; i < n; ++i) {
                const int e0 = (i + n - 1) % n, e1 = i;
                const float sx = nx[e0] + nx[e1], sy = ny[e0] + ny[e1];
                const float k = h / std::max(1e-6f, 1.0f + nx[e0] * nx[e1] + ny[e0] * ny[e1]);
                out[i][0] = p[i][0] + sx * k;
                out[i][1] = p[i][1] + sy * k;
            }
            for (int i = 0; i < n; ++i) {
                p[i][0] = out[i][0];
                p[i][1] = out[i][1];
            }
        }
    } // namespace marker_shape_detail

    // `r` is the half-extent in pixels (ScatterOptions::size / 2). `inset` moves
    // every edge that far inward (a stroke of width 2 x inset centred on the
    // result then fills the outer `inset`-wide band of the original shape, which is
    // how an outline is drawn). A line marker ignores `inset` and is `line_w`
    // thick (see line_marker_width()).
    inline MarkerShape marker_shape(MarkerStyle m, float cx, float cy, float r,
                                    float inset = 0.0f, float line_w = kLineMarkerWidth) {
        MarkerShape s;
        const float h = std::max(0.0f, inset);
        const float arm = std::max(0.01f, r - h);   // half-extent of the inset shape
        s.cx = cx;
        s.cy = cy;
        s.radius = arm;
        auto put = [&](float x, float y) {
            s.pts[s.count][0] = x;
            s.pts[s.count][1] = y;
            ++s.count;
        };

        // Two crossing bars `w` half-wide and `len` half-long, as one twelve-corner
        // ring; turned 45 degrees for a Cross. The GPU shape is the same bars cut
        // by the unit circle, so its arm ends are very slightly rounder.
        auto bars = [&](float w, float len, bool turned) {
            static const float k[12][2] = {
                {-1, -2}, {1, -2}, {1, -1}, {2, -1}, {2, 1}, {1, 1},
                {1, 2}, {-1, 2}, {-1, 1}, {-2, 1}, {-2, -1}, {-1, -1}
            };
            s.form = MarkerShape::Form::Polygon;
            for (const auto& q: k) {
                // -2/2 stand for the arm end (len), -1/1 for the bar edge (w).
                const float px = (std::abs(q[0]) > 1.5f ? len : w) * (q[0] < 0 ? -1.0f : 1.0f);
                const float py = (std::abs(q[1]) > 1.5f ? len : w) * (q[1] < 0 ? -1.0f : 1.0f);
                if (turned) put(cx + (px - py) * 0.70710678f, cy + (px + py) * 0.70710678f);
                else put(cx + px, cy + py);
            }
        };

        // A ring built about the origin, inset, then moved to the centre.
        float ring[12][2]{};
        int n = 0;
        auto ring_put = [&](float x, float y) {
            ring[n][0] = x;
            ring[n][1] = y;
            ++n;
        };
        auto place_ring = [&] {
            marker_shape_detail::inset_ring(ring, n, h);
            s.form = MarkerShape::Form::Polygon;
            for (int i = 0; i < n; ++i) put(cx + ring[i][0], cy + ring[i][1]);
        };
        // `count` corners on radius `rad`, the first straight up (y grows down),
        // then every `step` radians clockwise on screen.
        auto spokes = [&](int count, float rad, float first, float step) {
            for (int i = 0; i < count; ++i) {
                const float a = first + step * static_cast<float>(i);
                ring_put(rad * std::cos(a), rad * std::sin(a));
            }
        };
        constexpr float kUp = -1.5707963f;
        constexpr float kPi = 3.14159265f;

        switch (m) {
            case MarkerStyle::Circle:
                s.form = MarkerShape::Form::Disc;
                break;
            case MarkerStyle::Square:
                s.form = MarkerShape::Form::Rect;
                break;
            case MarkerStyle::Triangle: {
                // Upward-pointing, apex on the centre line. The inset shape is the
                // original scaled about its incentre (inradius 0.618 r).
                constexpr float kIn = 0.6180340f;
                const float ic = cy + r * (1.0f - kIn);
                const float k = std::max(0.01f, (r * kIn - h) / (r * kIn));
                s.form = MarkerShape::Form::Polygon;
                put(cx, ic - (2.0f - kIn) * r * k);      // apex, (2 - inradius) r above the incentre
                put(cx + r * k, ic + kIn * r * k);       // base corners, one inradius below it
                put(cx - r * k, ic + kIn * r * k);
                break;
            }
            case MarkerStyle::TriangleDown:
            case MarkerStyle::TriangleLeft:
            case MarkerStyle::TriangleRight: {
                // The upward triangle, mirrored or turned (as the shader's p.yx).
                const MarkerShape up = marker_shape(MarkerStyle::Triangle, 0.0f, 0.0f, r, inset);
                s.form = MarkerShape::Form::Polygon;
                for (int i = 0; i < up.count; ++i) {
                    const float dx = up.pts[i][0], dy = up.pts[i][1];
                    if (m == MarkerStyle::TriangleDown) put(cx + dx, cy - dy);
                    else if (m == MarkerStyle::TriangleLeft) put(cx + dy, cy + dx);
                    else put(cx - dy, cy + dx);
                }
                break;
            }
            case MarkerStyle::Diamond: {
                const float d = std::max(0.01f, r - h * 1.4142136f);
                s.form = MarkerShape::Form::Polygon;
                put(cx, cy - d);
                put(cx + d, cy);
                put(cx, cy + d);
                put(cx - d, cy);
                break;
            }
            case MarkerStyle::Star: {
                // Five points; the inner corners at 0.382 r make a regular star
                // (matplotlib's).
                constexpr float kInner = 0.3819660f;
                for (int i = 0; i < 5; ++i) {
                    const float a = kUp + 0.4f * kPi * static_cast<float>(i);
                    ring_put(r * std::cos(a), r * std::sin(a));
                    ring_put(kInner * r * std::cos(a + 0.2f * kPi), kInner * r * std::sin(a + 0.2f * kPi));
                }
                place_ring();
                break;
            }
            case MarkerStyle::Pentagon:
                spokes(5, r, kUp, 0.4f * kPi);
                place_ring();
                break;
            case MarkerStyle::Hexagon:
                spokes(6, r, kUp, kPi / 3.0f);
                place_ring();
                break;
            case MarkerStyle::Cross:
                bars(std::max(0.01f, 0.2121320f * r - h), arm, true);
                break;
            case MarkerStyle::Plus:
                bars(std::max(0.01f, 0.3f * r - h), arm, false);
                break;
            case MarkerStyle::ThinCross:
            case MarkerStyle::ThinPlus: {
                const float w = std::clamp(line_w * 0.5f, 0.01f, r);
                bars(w, r, m == MarkerStyle::ThinCross);
                break;
            }
            case MarkerStyle::VLine:
            case MarkerStyle::HLine: {
                const float w = std::clamp(line_w * 0.5f, 0.01f, r);
                const float hx = m == MarkerStyle::VLine ? w : r, hy = m == MarkerStyle::VLine ? r : w;
                s.form = MarkerShape::Form::Polygon;
                put(cx - hx, cy - hy);
                put(cx + hx, cy - hy);
                put(cx + hx, cy + hy);
                put(cx - hx, cy + hy);
                break;
            }
            case MarkerStyle::None:
            default:
                break;
        }
        return s;
    }
} // namespace sextant
