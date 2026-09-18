#pragma once
#include <sextant/style.h>
#include <algorithm>

namespace sextant {
    // One marker as 2D primitives, shared by the SVG data markers and both legends
    // so keys match the picture. The GPU tests shapes in the shader instead (see
    // uMarker in data_renderer.cpp); Cross and Plus are strokes in both.
    struct MarkerShape {
        enum class Form {
            None, // nothing is drawn
            Disc, // centre + radius
            Rect, // centred square, half-side = radius
            Polygon, // filled ring of `count` points
            Strokes // `count`/2 line segments, `width` wide
        };

        Form form = Form::None;
        float cx = 0.0f, cy = 0.0f;
        float radius = 0.0f; // Disc and Rect
        float width = 0.0f; // Strokes only
        // Polygon: the ring. Strokes: consecutive pairs of endpoints.
        float pts[8][2]{};
        int count = 0;
    };

    // `r` is the half-extent in pixels (ScatterOptions::size / 2).
    inline MarkerShape marker_shape(MarkerStyle m, float cx, float cy, float r) {
        MarkerShape s;
        s.cx = cx;
        s.cy = cy;
        s.radius = r;
        auto put = [&](float x, float y) {
            s.pts[s.count][0] = x;
            s.pts[s.count][1] = y;
            ++s.count;
        };

        switch (m) {
            case MarkerStyle::Circle:
                s.form = MarkerShape::Form::Disc;
                break;
            case MarkerStyle::Square:
                s.form = MarkerShape::Form::Rect;
                break;
            case MarkerStyle::Triangle:
                // Upward-pointing, apex on the centre line.
                s.form = MarkerShape::Form::Polygon;
                put(cx, cy - r);
                put(cx + r, cy + r);
                put(cx - r, cy + r);
                break;
            case MarkerStyle::Diamond:
                s.form = MarkerShape::Form::Polygon;
                put(cx, cy - r);
                put(cx + r, cy);
                put(cx, cy + r);
                put(cx - r, cy);
                break;
            case MarkerStyle::Cross: {
                const float d = r * 0.707f; // half-length along each diagonal
                s.form = MarkerShape::Form::Strokes;
                s.width = std::max(1.0f, r * 0.35f);
                put(cx - d, cy - d);
                put(cx + d, cy + d);
                put(cx + d, cy - d);
                put(cx - d, cy + d);
                break;
            }
            case MarkerStyle::Plus:
                s.form = MarkerShape::Form::Strokes;
                s.width = std::max(1.0f, r * 0.35f);
                put(cx, cy - r);
                put(cx, cy + r);
                put(cx - r, cy);
                put(cx + r, cy);
                break;
            case MarkerStyle::None:
            default:
                break;
        }
        return s;
    }
} // namespace sextant
