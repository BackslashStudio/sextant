#pragma once
#include "plot_objects.h"
#include "coord_transform.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace sextant {

// Contour lines over a heatmap, in three stages:
//
//   trace_contours()  data  -> iso-lines in *data* space   (expensive, cached)
//   plan_contours()   those -> pixel runs + label anchors  (per frame, cheap)
//   ContourCache      holds the first result across frames
//
// Both render paths call the same two functions, so neither derives contour
// geometry of its own. See spec_architecture.md's "Contours" for the design.

// One traced iso-line, as a polyline inside the heatmap's [0,cols] x [0,rows]
// footprint. View-independent, hence cacheable across pan/zoom.
struct ContourLine {
    double             level = 0.0;
    std::vector<float> x, y;
    bool               closed = false;   // last vertex coincides with the first
};

using ContourSet = std::vector<ContourLine>;

// Marching squares over the heatmap's cell *centres*: sample (i, j) sits at
// data-space (j + 0.5, i + 0.5) with i counted from the bottom. Storage is
// row-major with row 0 first regardless of origin, so the row a sample reads
// is `i` for origin=="lower" and `rows-1-i` otherwise. A heatmap narrower than
// two cells in either direction traces nothing -- a square needs four samples.
// Saddle cells are resolved by the average of the four corners.
//
// Levels come from hp.opts.contours, already sorted and de-duplicated by
// Axes::heatmap(). O(rows * cols) per level, which is why nothing calls this
// per frame -- see ContourCache.
ContourSet trace_contours(const HeatmapPlot& hp);

// "%g", the same formatting generate_ticks() gives a tick label.
std::string format_contour_level(double level);

// A stretch of one contour line, projected to pixels and ready to stroke. A
// labelled line yields two of these, either side of the gap cut for its text.
struct ContourRun {
    std::vector<float> px, py;
};

// One inline level label. `angle` is in radians in *pixel* space (y down),
// which is the sign convention both nvgRotate() and SVG's rotate() use.
struct ContourLabel {
    float       x = 0.0f, y = 0.0f;
    float       angle = 0.0f;
    std::string text;
};

struct ContourDraw {
    std::vector<ContourRun>   runs;
    std::vector<ContourLabel> labels;
};

// Projects `set` through `tr` and, when opts.contour_labels is set, breaks
// each line around one label at its arc-length midpoint. `font_path` is the
// AxesStyle font the label will be drawn in: the gap has to be as wide as the
// text, so this measures it through text_metrics.h, the only measuring stick
// available to the headless SVG path. A line too short to break keeps its
// label off and is drawn whole.
ContourDraw plan_contours(const ContourSet& set, const CoordTransform& tr,
                          const HeatmapOptions& opts,
                          const std::string& font_path);

// Traced geometry held across frames by whoever draws it (NvgRenderer, i.e.
// one per window thread). Keyed on the snapshot's *data* generation, so a pan
// republishing the snapshot every frame does not throw the trace away; the
// level list, origin and grid size are part of the key too, since all four
// change what a trace produces. A data_generation of 0 never matches, so an
// unstamped snapshot re-traces rather than showing a stale contour.
class ContourCache {
public:
    const ContourSet& get(int axes_index, int plot_index,
                          unsigned long long data_generation,
                          const HeatmapPlot& hp);

private:
    struct Entry {
        unsigned long long  generation = 0;
        std::vector<double> levels;
        std::string         origin;
        int                 rows = 0, cols = 0;
        ContourSet          set;
    };
    std::unordered_map<long long, Entry> entries_;
};

} // namespace sextant
