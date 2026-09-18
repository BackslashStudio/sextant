#pragma once
#include "plot_objects.h"
#include "coord_transform3d.h"
#include "coord_transform.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace sextant {
    // Contour lines over a heatmap, shared by both render paths:
    //
    //   trace_contours()  data  -> iso-lines in data space   (expensive, cached)
    //   plan_contours()   those -> pixel runs + label anchors  (per frame)
    //   ContourCache      holds traces across frames

    // One iso-line as a data-space polyline; view-independent, so cacheable.
    struct ContourLine {
        double level = 0.0;
        std::vector<float> x, y;
        bool closed = false; // last vertex coincides with the first
    };

    using ContourSet = std::vector<ContourLine>;

    // Marching squares over cell centres: sample (i, j) sits at cell (j + 0.5,
    // i + 0.5), i counted from the yrange.lo end (storage row `i` for
    // origin=="lower", else `rows-1-i`). Needs at least 2x2 cells. Saddles are
    // resolved by the corner average. Levels come pre-sorted from ingest.
    ContourSet trace_contours(const HeatmapPlot& hp);

    // "%g", as tick labels.
    std::string format_contour_level(double level);

    // A projected stretch of a contour line; a labelled line yields two.
    struct ContourRun {
        std::vector<float> px, py;
    };

    // An inline level label. `angle` is radians in pixel space (y down).
    struct ContourLabel {
        float x = 0.0f, y = 0.0f;
        float angle = 0.0f;
        std::string text;
    };

    struct ContourDraw {
        std::vector<ContourRun> runs;
        std::vector<ContourLabel> labels;
    };

    // Maps contour data points to pixels, for a 2D axes or a plane in a 3D scene.
    // Contours are annotation: width and labels stay fixed on screen.
    class ContourProjector {
    public:
        // Implicit so 2D call sites can pass a CoordTransform.
        ContourProjector(const CoordTransform& tr)
            : tr_(&tr), frame_{tr.px, tr.py, tr.pw, tr.ph} {
        }

        ContourProjector(const Projector3D& proj, PlaneOrientation orient, double offset)
            : proj_(&proj), orient_(orient), offset_(offset), frame_(proj.frame()) {
        }

        struct Pt {
            float x = 0.0f, y = 0.0f;
            bool in_front = true;
        };

        Pt at(double u, double v) const {
            if (tr_) return {tr_->to_px(u), tr_->to_py(v), true};
            const Vec3 p = plane_point(orient_, u, v, offset_);
            const Vec3 b = proj_->transform().to_box(p.x, p.y, p.z);
            return {
                proj_->project_box(b).x, proj_->project_box(b).y,
                proj_->in_front(b)
            };
        }

        const PlotRect& frame() const { return frame_; }

    private:
        const CoordTransform* tr_ = nullptr;
        const Projector3D* proj_ = nullptr;
        PlaneOrientation orient_ = PlaneOrientation::XY;
        double offset_ = 0.0;
        PlotRect frame_{};
    };

    // Projects `set` and, with opts.contour_labels, breaks each line around a label
    // at its arc-length midpoint (gap measured via text_metrics.h in `font_path`).
    // A line too short to break is drawn whole without a label. A line with any
    // vertex behind the eye is dropped.
    ContourDraw plan_contours(const ContourSet& set, const ContourProjector& proj,
                              const HeatmapOptions& opts,
                              const std::string& font_path);

    class ContourCache;

    // Planned contours for one plane, with their style.
    struct PlaneContourDraw {
        ContourDraw draw;
        Color color{0, 0, 0, 1};
        float linewidth = 1.0f;
        float fontsize = 10.0f;
        std::size_t plane = 0, plot = 0;
    };

    // Every contour of every heatmap on every plane, planned to pixels, for both
    // NvgRenderer and the SVG writer. `cache` is optional (nullptr for export).
    std::vector<PlaneContourDraw> plan_plane_contours(
        const Projector3D& proj, const std::vector<PlaneSnapshot>& planes,
        const std::string& font_path, ContourCache* cache,
        unsigned long long data_generation, int axes_index);

    // Traced geometry kept across frames (one per window thread). Keyed on data
    // generation plus levels, origin, grid size and extent; generation 0 never
    // matches.
    class ContourCache {
    public:
        // `plane_index` is -1 for the axes' own heatmaps, else the plane's index.
        const ContourSet& get(int axes_index, int plane_index, int plot_index,
                              unsigned long long data_generation,
                              const HeatmapPlot& hp);

    private:
        struct Entry {
            unsigned long long generation = 0;
            std::vector<double> levels;
            std::string origin;
            int rows = 0, cols = 0;
            Range xrange, yrange;
            ContourSet set;
        };

        std::unordered_map<long long, Entry> entries_;
    };
} // namespace sextant
