#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "../tick.h"
#include "sextant/style.h"
#include <string>
#include <vector>

namespace sextant {

// The 3D box reduced to pixels: panes, grid lines, axis lines, tick marks and
// text anchors. Shared by NvgRenderer and the SVG writer; neither sees the
// camera, so annotation never picks up a perspective divide.
struct Box3DPlan {
    // A polyline or polygon as flat x,y pairs.
    struct Poly { std::vector<float> xy; };

    // Text is always centred on its anchor in both outputs.
    struct Label {
        float       x = 0.0f, y = 0.0f;
        std::string text;
        float       fontsize = 11.0f;
        Color       color{ 0.2f, 0.2f, 0.2f, 1.0f };
        // Font for this label ("" = default).
        std::string font_path;
        // For a tick label: its index in `tick_marks` (labels are a subset).
        // -1 for an axis title.
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

// `proj` must be built from `snap` and the cell's frame. Tick lists come from
// layout, so overrides apply as in 2D.
Box3DPlan plan_box3d(const Projector3D& proj, const RenderSnapshot3D& snap,
                     const std::vector<Tick>& xticks,
                     const std::vector<Tick>& yticks,
                     const std::vector<Tick>& zticks);

} // namespace sextant
