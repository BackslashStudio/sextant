#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "../tick.h"
#include "sextant/style.h"
#include <string>
#include <vector>

namespace sextant {

// The 3D box, reduced to pixels: filled panes, grid lines, axis lines, tick
// marks, and the anchors of every piece of text on it.
//
// One function, two consumers -- NvgRenderer and svg_writer -- for the same
// reason compute_figure_layout() exists for the 2D figure. It matters more
// here: which panes are drawn, which edge carries the ticks and which way a
// label is pushed all depend on the camera, so two transcriptions would not
// merely drift, they would drift *per frame*.
//
// Everything below is in figure pixel coordinates, already projected. That is
// what makes the annotation-invariance rule (memory/spec_3d.md §4) automatic
// rather than a special case: neither renderer ever sees the camera, so
// nothing they draw can pick up a perspective divide. The corollary is a
// constraint -- the box frame must never become geometry in the scene.
struct Box3DPlan {
    // A polyline or polygon as flat x,y pairs.
    struct Poly { std::vector<float> xy; };

    // Text is always centred on its anchor, in both outputs: the offset rule
    // below pushes the anchor far enough out that no other alignment is
    // needed, and one alignment is one fewer thing the two paths can disagree
    // about.
    struct Label {
        float       x = 0.0f, y = 0.0f;
        std::string text;
        float       fontsize = 11.0f;
        Color       color{ 0.2f, 0.2f, 0.2f, 1.0f };
        // The font this label is measured and drawn with; "" is the
        // renderer's default. Carried per label because the axis titles and
        // the tick labels can be sized differently but share a font path.
        std::string font_path;
        // For a tick label, the index in `tick_marks` of the tick it names.
        // tick_labels is a *subset* of tick_marks -- a foreshortened edge has
        // room for fewer numbers than subdivisions -- so the plan says which,
        // rather than leaving the two to be matched up by position. -1 on an
        // axis title, which names no single tick.
        int         tick = -1;
    };

    // Drawn under the data (pass 1).
    std::vector<Poly> panes;        // filled
    std::vector<Poly> pane_edges;   // outlined
    std::vector<Poly> grid;

    // Drawn over it (pass 3).
    std::vector<Poly>  axis_lines;
    std::vector<Poly>  tick_marks;
    std::vector<Label> tick_labels;
    std::vector<Label> axis_titles;
};

// `proj` must already be built from `snap` and the cell's frame. The three
// tick lists are the ones layout resolved, so an override reaches here the
// same way it reaches a 2D axis.
Box3DPlan plan_box3d(const Projector3D& proj, const RenderSnapshot3D& snap,
                     const std::vector<Tick>& xticks,
                     const std::vector<Tick>& yticks,
                     const std::vector<Tick>& zticks);

} // namespace sextant
