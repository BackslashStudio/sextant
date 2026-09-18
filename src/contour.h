#pragma once
#include "plot_objects.h"
#include "coord_transform3d.h"
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

// One traced iso-line, as a polyline in *data* space, inside the heatmap's
// xrange x yrange footprint. View-independent, hence cacheable across
// pan/zoom.
struct ContourLine {
    double             level = 0.0;
    std::vector<float> x, y;
    bool               closed = false;   // last vertex coincides with the first
};

using ContourSet = std::vector<ContourLine>;

// Marching squares over the heatmap's cell *centres*: sample (i, j) sits at
// cell index (j + 0.5, i + 0.5) with i counted from the yrange.lo end, and is
// emitted at the data-space point hp.x_at()/y_at() maps that to (for
// imshow(), the two are the same number). Storage is row-major with row 0
// first regardless of origin, so the row a sample reads is `i` for
// origin=="lower" and `rows-1-i` otherwise. A heatmap narrower than two cells
// in either direction traces nothing -- a square needs four samples. Saddle
// cells are resolved by the average of the four corners.
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

// Where a contour's data-space points land in pixels: either a 2D axes'
// CoordTransform, or a plane in a 3D scene. One type rather than two planners,
// because everything a contour plan does after the projection -- the arc
// length, the gap measured against the text, the label angle -- is pixel-space
// work that does not care which projection produced the pixels. A branch per
// point costs nothing next to the arc-length pass.
//
// **A contour is annotation, not geometry** (spec_3d.md §4): its width and its
// label size stay fixed on screen, which is why this hands back pixels and the
// stroke happens in pixel space in both outputs. That is the same category the
// box's own tick marks are in, and the opposite of a data line on a plane.
class ContourProjector {
public:
    // Implicit on purpose: every existing 2D call site passes a CoordTransform
    // and should keep reading as though this class were not here.
    ContourProjector(const CoordTransform& tr)
        : tr_(&tr), frame_{ tr.px, tr.py, tr.pw, tr.ph } {}
    ContourProjector(const Projector3D& proj, PlaneOrientation orient, double offset)
        : proj_(&proj), orient_(orient), offset_(offset), frame_(proj.frame()) {}

    struct Pt { float x = 0.0f, y = 0.0f; bool in_front = true; };

    Pt at(double u, double v) const {
        if (tr_) return { tr_->to_px(u), tr_->to_py(v), true };
        const Vec3 p = plane_point(orient_, u, v, offset_);
        const Vec3 b = proj_->transform().to_box(p.x, p.y, p.z);
        return { proj_->project_box(b).x, proj_->project_box(b).y,
                 proj_->in_front(b) };
    }
    const PlotRect& frame() const { return frame_; }

private:
    const CoordTransform* tr_   = nullptr;
    const Projector3D*    proj_ = nullptr;
    PlaneOrientation      orient_ = PlaneOrientation::XY;
    double                offset_ = 0.0;
    PlotRect              frame_{};
};

// Projects `set` through `proj` and, when opts.contour_labels is set, breaks
// each line around one label at its arc-length midpoint. `font_path` is the
// AxesStyle font the label will be drawn in: the gap has to be as wide as the
// text, so this measures it through text_metrics.h, the only measuring stick
// available to the headless SVG path. A line too short to break keeps its
// label off and is drawn whole.
//
// A line with any vertex behind the eye is dropped whole rather than clipped.
// Clipping it would be easy enough, but a broken contour reads as data and a
// missing one reads as missing -- and the case only arises when the camera has
// been flown into the plane, where the picture is not being read anyway.
ContourDraw plan_contours(const ContourSet& set, const ContourProjector& proj,
                          const HeatmapOptions& opts,
                          const std::string& font_path);

class ContourCache;

// One plane's worth of planned contours, with the styling that goes with them
// -- ContourDraw carries geometry only, and the two consumers below would
// otherwise each have to go back to the heatmap for the colour.
struct PlaneContourDraw {
    ContourDraw draw;
    Color       color{ 0, 0, 0, 1 };
    float       linewidth = 1.0f;
    float       fontsize  = 10.0f;
    std::size_t plane = 0, plot = 0;
};

// Every contour of every heatmap on every plane, planned to pixels. One
// function, two consumers -- NvgRenderer and the SVG writer -- for the reason
// plan_box3d() is one function: neither output may derive contour geometry of
// its own.
//
// `cache` is optional: the window path passes its ContourCache so a pan or an
// orbit does not re-run marching squares, and the export path passes nullptr
// because it traces once and exits.
std::vector<PlaneContourDraw> plan_plane_contours(
    const Projector3D& proj, const std::vector<PlaneSnapshot>& planes,
    const std::string& font_path, ContourCache* cache,
    unsigned long long data_generation, int axes_index);

// Traced geometry held across frames by whoever draws it (NvgRenderer, i.e.
// one per window thread). Keyed on the snapshot's *data* generation, so a pan
// republishing the snapshot every frame does not throw the trace away; the
// level list, origin, grid size and extent are part of the key too, since all
// of them change what a trace produces -- the extent because the traced
// points are in data space, so moving the image moves them. A data_generation
// of 0 never matches, so an unstamped snapshot re-traces rather than showing a
// stale contour.
class ContourCache {
public:
    // `plane_index` is -1 for a heatmap owned by the axes itself -- the only
    // form a 2D slot ever produces -- and the plane's position otherwise, so a
    // plane's heatmap 0 and the axes' heatmap 0 do not share one entry. The
    // same extra field DataRenderer's CacheKey takes on, for the same reason.
    const ContourSet& get(int axes_index, int plane_index, int plot_index,
                          unsigned long long data_generation,
                          const HeatmapPlot& hp);

private:
    struct Entry {
        unsigned long long  generation = 0;
        std::vector<double> levels;
        std::string         origin;
        int                 rows = 0, cols = 0;
        Range               xrange, yrange;
        ContourSet          set;
    };
    std::unordered_map<long long, Entry> entries_;
};

} // namespace sextant
