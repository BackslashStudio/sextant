#pragma once
// Text objects (Axes::text()/annotate(), the Axes3D ones) laid out in figure
// pixels, for NanoVG and the SVG writer alike. Everything is a pixel size, so
// in 3D a text keeps its size at any distance (the annotation-invariance rule):
// only the anchor and the arrow's point are projected. Measured with
// text_metrics.h, so no GL context is needed.
#include "../plot_objects.h"
#include "../coord_transform.h"
#include "../coord_transform3d.h"
#include "plot_rect.h"
#include <string>
#include <vector>

namespace sextant {

struct TextDraw {
    // The anchor in figure pixels (y down), TextOptions::dx/dy applied. The
    // lines and the box are relative to it, rotated by `angle` about it.
    float ax = 0.0f, ay = 0.0f;
    // Radians, clockwise on screen (NanoVG's and SVG's sense): -rotation.
    float angle = 0.0f;

    // One line: `x` is the edge (or centre) `ha` aligns, `y` its baseline.
    struct Line {
        std::string text;
        float x = 0.0f, y = 0.0f;
    };
    std::vector<Line> lines;
    HAlign ha = HAlign::Left;
    float fontsize = 12.0f;
    std::string font_path;
    Color color;   // alpha applied

    // The box around the lines (pad included), relative to the anchor; drawn
    // when `fill` or the edge shows.
    PlotRect box{};
    bool has_box = false;
    Color fill{};
    Color edge{};
    float edge_width = 0.0f;   // 0 = no outline

    // The arrow in figure pixels: the shaft as a polyline (x, y pairs; empty =
    // no arrow), then its heads.
    std::vector<float> shaft;
    LineStyle linestyle = LineStyle::Solid;
    float linewidth = 1.0f;
    Color arrow_color{};
    struct Head {
        ArrowHead kind = ArrowHead::None;
        // Filled: a triangle (3 points). Open: a chevron (3 points, stroked).
        // Bar: a segment (the first 2 points).
        float xy[6] = {};
    };
    std::vector<Head> heads;

    // Scissor to the frame (TextOptions::clip_to_frame).
    bool clip = false;
};

// A 2D axes' texts, in plot order. Hidden ones are left out.
std::vector<TextDraw> plan_texts(const std::vector<TextPlot>& texts, const CoordTransform& tr,
                                 const PlotRect& frame);

// A 3D axes' texts: anchors projected through `proj`, frame fractions against
// `frame`.
std::vector<TextDraw> plan_texts3d(const std::vector<TextPlot>& texts, const Projector3D& proj,
                                   const PlotRect& frame);

} // namespace sextant
