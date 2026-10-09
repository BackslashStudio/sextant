#pragma once
#include <sextant/style.h>
#include <algorithm>
#include <cmath>

namespace sextant {
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
        // Polygon: the ring (a Plus or Cross has twelve corners).
        float pts[12][2]{};
        int count = 0;
    };

    // `r` is the half-extent in pixels (ScatterOptions::size / 2). `inset` moves
    // every edge that far inward (a stroke of width 2 x inset centred on the
    // result then fills the outer `inset`-wide band of the original shape, which is
    // how an outline is drawn).
    inline MarkerShape marker_shape(MarkerStyle m, float cx, float cy, float r,
                                    float inset = 0.0f) {
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
            case MarkerStyle::Diamond: {
                const float d = std::max(0.01f, r - h * 1.4142136f);
                s.form = MarkerShape::Form::Polygon;
                put(cx, cy - d);
                put(cx + d, cy);
                put(cx, cy + d);
                put(cx - d, cy);
                break;
            }
            case MarkerStyle::Cross:
                bars(std::max(0.01f, 0.2121320f * r - h), arm, true);
                break;
            case MarkerStyle::Plus:
                bars(std::max(0.01f, 0.3f * r - h), arm, false);
                break;
            case MarkerStyle::None:
            default:
                break;
        }
        return s;
    }
} // namespace sextant
