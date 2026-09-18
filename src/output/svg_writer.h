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

// All data needed to write one axes to SVG, extracted from Axes::Impl by
// Figure::savefig_svg (a friend of Axes). The writer itself is a plain function.
struct SvgAxesData {
    int width  = 800;
    int height = 600;
    // Frame, transform, ticks, text anchors and the legend/colorbar boxes —
    // all of it computed by compute_figure_layout(), the same call the
    // window and PNG paths make. Nothing in this writer derives a position
    // of its own any more.
    CellLayout    layout;
    std::vector<LinePlot>     lines;
    std::vector<ScatterPlot>  scatters;
    std::vector<BarPlot>      bars;
    std::vector<ScatterZPlot> scatter_z;
    std::vector<HeatmapPlot>  heatmaps;
    // Their font sizes come from axes_style, like every other text size here.
    std::string title, xtitle, ytitle;
    bool  grid_enabled    = false;
    GridOptions grid_opts;
    AxesStyle axes_style;
    // Whether either decoration is present, and its box, both live in
    // `layout` (has_legend()/has_colorbar()). Only the cosmetics are here.
    LegendOptions   legend_opts;
    ColorbarOptions colorbar_opts;

    // Present exactly when this cell holds a 3D axes, in which case every
    // plot vector above is empty and `title` is the only text outside the
    // plan. The box arrives already reduced to pixels by the same
    // plan_box3d() call render_frame() makes -- this writer never sees a
    // camera, which is what keeps the two outputs from disagreeing about a
    // projection and is also why its text cannot pick up a perspective
    // divide (memory/spec_3d.md §4).
    std::optional<Box3DPlan> box3d;
    Box3DStyle               box3d_style;
    GridOptions              box3d_grid_opts;
    // The scene inside the box, already projected and painter-sorted by
    // plan_bars3d() -- same bargain as the plan above, and the reason this
    // writer needs no depth buffer and no camera.
    std::vector<Bar3DPolygon> bars3d;
    // The surfaces, likewise projected and painter-sorted, by plan_surfaces3d()
    // (step 7c). A separate list rather than more Bar3DPolygons because the
    // provenance field indexes a cell rather than a bar; the writer interleaves
    // the three lists by the whole-object depth all of them carry.
    std::vector<Surface3DPolygon> surfaces3d;
    // The triangulated meshes, by plan_surface_tri3d() (v1.0 step 14.2). A
    // face is one <polygon>, filled with a <linearGradient> when the mesh is
    // colormapped -- the exact reproduction of the raster picture under an
    // orthographic camera, since a barycentrically interpolated scalar is an
    // affine function of position in the plane and an affine projection keeps
    // its iso-lines straight and parallel. See SurfaceTriPolygon.
    std::vector<SurfaceTriPolygon> meshes3d;
    // The 2D planes in the scene, likewise already reduced -- to an affine
    // <image> under an orthographic camera, or one polygon per cell under a
    // perspective one, which is the SVG warp problem and its answer
    // (memory/spec_3d.md §10). Ordered back to front; the writer merges them
    // against `bars3d` by the object depth both plans carry.
    std::vector<PlanePlanItem> planes3d;
    // The scatter3d clouds, reduced to markers by plan_scatter3d() (v1.0 step
    // 12.5): a pixel, a size, a shape and a finished colour each. Not a
    // polygon list, because a marker is a symbol drawn at a point rather than
    // geometry with a ring -- which is also why `scene3d` can order one
    // exactly and can never split one.
    std::vector<Scatter3DMarker> markers3d;
    // The line3d paths, reduced to segments by plan_lines3d() (v1.0 step 13.3):
    // two pixels, a width, and a finished colour at each end. Strokes rather
    // than the ribbon quads the raster path draws, so that a long path cannot
    // fill the painter with thin cutting planes -- see Line3DSegment for that
    // decision and the two divergences it accepts.
    std::vector<Line3DSegment> lines3d;
    // The scatter3d and line3d error bars, by plan_errorbars3d() (v1.0 step
    // 17): strokes for the whiskers, caps and block edges, polygons for the
    // block faces, each with a finished colour. See ErrorBar3DPolygon.
    std::vector<ErrorBar3DPolygon> errbars3d;
    // The order the three lists above are actually emitted in (v1.0 step 9),
    // and the pixels of any polygon the painter had to split. Newell's
    // algorithm produced it, in the plan layer, because splitting needs a
    // projector and this writer deliberately never sees one. Empty means "fall
    // back to the whole-object interleave", which is what a 2D figure and a
    // scene with nothing in it both look like.
    std::vector<ScenePaint> scene3d;
    // Non-empty when the painter hit its work bound and gave up, in which case
    // the tail of the order is a plain depth sort and some of the scene is
    // wrong. Emitted into the file as an XML comment, the way a plane's
    // perspective-warp warning is: the file is where a warning about the file
    // belongs, and this library has no logging channel. Without it a bail is
    // invisible until somebody compares the SVG against the window by eye --
    // which is exactly how the bound being too low was found.
    std::string scene3d_warning;
    // A plane's contours, already planned to pixels by plan_plane_contours().
    // Annotation, not scene geometry: emitted with the box's furniture at a
    // fixed width and label size (memory/spec_3d.md §4).
    std::vector<PlaneContourDraw> contours3d;
};

// All data needed to write a whole figure (one or more axes, e.g. from
// Figure::add_subplot) to SVG. width/height are the figure-wide canvas size;
// each SvgAxesData's own width/height fields are unused in this path.
struct SvgFigureData {
    int width  = 800;
    int height = 600;
    std::string suptitle;
    SuptitleOptions suptitle_opts;
    std::vector<SvgAxesData> axes;
};

// Write a single-axes figure to an SVG file. No GL context required.
void write_svg(std::string_view path, const SvgAxesData& d);

// Write a multi-axes figure (one <g> per axes, each independently clipped
// to its own PlotRect) to a single SVG file. No GL context required.
void write_svg(std::string_view path, const SvgFigureData& d);

} // namespace sextant
