#pragma once
#include <sextant/style.h>
#include <algorithm>

namespace sextant {

// One marker, reduced to what any 2D renderer can draw.
//
// **Three consumers, one definition**, for the reason plan_box3d() and
// bar3d_bounds() are one function each: the SVG writer draws data markers, and
// *both* renderers draw a legend key that is supposed to be the same shape.
// Before this existed there was no definition at all -- the legend drew a
// hardcoded circle in NanoVG and another hardcoded circle in SVG, so a
// diamond series was keyed by a circle in both outputs and the two agreed with
// each other while disagreeing with the picture. Agreement between the outputs
// is worth nothing when neither of them is looking at the data.
//
// The GPU is the fourth consumer and cannot use this: it tests a fragment
// against the shape in a shader rather than assembling one (see uMarker in
// data_renderer.cpp). The forms below are written to match those tests --
// `Cross` and `Plus` are strokes there too, which is why they are not polygons
// here.
struct MarkerShape {
    enum class Form {
        None,      // nothing is drawn
        Disc,      // centre + radius
        Rect,      // centred square, half-side = radius
        Polygon,   // filled ring of `count` points
        Strokes    // `count`/2 line segments, `width` wide
    };

    Form  form   = Form::None;
    float cx     = 0.0f, cy = 0.0f;
    float radius = 0.0f;   // Disc and Rect
    float width  = 0.0f;   // Strokes only
    // Polygon: the ring. Strokes: consecutive pairs of endpoints.
    float pts[8][2]{};
    int   count  = 0;
};

// `r` is the marker's half-extent in pixels -- the same number
// ScatterOptions::size names as a diameter, halved by the caller.
inline MarkerShape marker_shape(MarkerStyle m, float cx, float cy, float r) {
    MarkerShape s;
    s.cx = cx; s.cy = cy; s.radius = r;
    auto put = [&](float x, float y) { s.pts[s.count][0] = x; s.pts[s.count][1] = y; ++s.count; };

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
            put(cx, cy - r); put(cx + r, cy + r); put(cx - r, cy + r);
            break;
        case MarkerStyle::Diamond:
            s.form = MarkerShape::Form::Polygon;
            put(cx, cy - r); put(cx + r, cy); put(cx, cy + r); put(cx - r, cy);
            break;
        case MarkerStyle::Cross: {
            const float d = r * 0.707f;   // half-length along each diagonal
            s.form  = MarkerShape::Form::Strokes;
            s.width = std::max(1.0f, r * 0.35f);
            put(cx - d, cy - d); put(cx + d, cy + d);
            put(cx + d, cy - d); put(cx - d, cy + d);
            break;
        }
        case MarkerStyle::Plus:
            s.form  = MarkerShape::Form::Strokes;
            s.width = std::max(1.0f, r * 0.35f);
            put(cx, cy - r); put(cx, cy + r);
            put(cx - r, cy); put(cx + r, cy);
            break;
        case MarkerStyle::None:
        default:
            break;
    }
    return s;
}

} // namespace sextant
