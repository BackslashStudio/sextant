#pragma once
#include "../plot_objects.h"
#include "../renderer/box3d.h"
#include "../renderer/bar3d.h"
#include "../renderer/plane2d.h"
#include "../renderer/surface.h"
#include "../renderer/scatter3d.h"
#include "../renderer/painter3d.h"
#include "../contour.h"
#include "../renderer/figure_layout.h"
#include "../renderer/plot_rect.h"
#include "../coord_transform.h"
#include "sextant/style.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sextant {
    // Everything needed to write one axes to SVG.
    struct SvgAxesData {
        int width = 800;
        int height = 600;
        // All positions come from compute_figure_layout(), as in the raster path.
        CellLayout layout;
        std::vector<LinePlot> lines;
        std::vector<ScatterPlot> scatters;
        std::vector<BarPlot> bars;
        std::vector<ScatterZPlot> scatter_z;
        std::vector<HeatmapPlot> heatmaps;
        // Font sizes come from axes_style.
        std::string title, xtitle, ytitle;
        bool grid_enabled = false;
        GridOptions grid_opts;
        AxesStyle axes_style;
        // Presence and boxes are in `layout`; only the cosmetics are here.
        LegendOptions legend_opts;
        ColorbarOptions colorbar_opts;

        // 3D cells only (plot vectors above are then empty): the box, already in
        // pixels via plan_box3d(). The writer never sees a camera.
        std::optional<Box3DPlan> box3d;
        Box3DStyle box3d_style;
        GridOptions box3d_grid_opts;
        // Projected, painter-sorted scene geometry from the plan layer:
        // bars (plan_bars3d()),
        std::vector<Bar3DPolygon> bars3d;
        // surfaces (plan_surfaces3d()),
        std::vector<Surface3DPolygon> surfaces3d;
        // meshes (plan_surface_tri3d(); colormapped faces use a <linearGradient>),
        std::vector<SurfaceTriPolygon> meshes3d;
        // planes (an affine <image> under orthographic, per-cell polygons under
        // perspective),
        std::vector<PlanePlanItem> planes3d;
        // scatter3d markers (plan_scatter3d()),
        std::vector<Scatter3DMarker> markers3d;
        // line3d segments as strokes (plan_lines3d(); see Line3DSegment),
        std::vector<Line3DSegment> lines3d;
        // and error bars (plan_errorbars3d()).
        std::vector<ErrorBar3DPolygon> errbars3d;
        // Emission order from Newell's algorithm, plus the pixels of split
        // polygons. Empty = fall back to the whole-object depth interleave.
        std::vector<ScenePaint> scene3d;
        // Set when the painter hit its bound (the tail is plain depth order);
        // written into the file as an XML comment.
        std::string scene3d_warning;
        // Plane contours in pixels (annotation: fixed width and label size).
        std::vector<PlaneContourDraw> contours3d;
    };

    // A whole figure. width/height are the canvas size; each SvgAxesData's own
    // width/height are unused here.
    struct SvgFigureData {
        int width = 800;
        int height = 600;
        std::string suptitle;
        SuptitleOptions suptitle_opts;
        std::vector<SvgAxesData> axes;
    };

    // Write a single-axes figure to SVG. No GL context required.
    void write_svg(std::string_view path, const SvgAxesData& d);

    // Write a multi-axes figure (one clipped <g> per axes) to SVG. No GL needed.
    void write_svg(std::string_view path, const SvgFigureData& d);
} // namespace sextant
