#pragma once
// Internal header — defines Axes3D::Impl. The 3D counterpart of axes_impl.h,
// and deliberately not a specialization of it: see plot_objects.h's
// FigureAxesSnapshot for why the two kinds meet only at the snapshot boundary.
#include "sextant/axes3d.h"
#include "plane2d_impl.h"
#include "plot_objects.h"
#include "tick.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sextant {

struct Axes3D::Impl {
    // The plot data, held in the same CowVec form Axes::Impl holds its five
    // vectors in, so a snapshot shares the buffers instead of copying them.
    std::vector<Bar3DPlot> bars3d;
    std::vector<SurfacePlot> surfaces;
    std::vector<Scatter3DPlot> scatter3d;
    std::vector<Line3DPlot> lines3d;
    std::vector<SurfaceTriPlot> surface_tri;

    // The 2D kinds, one plane each. shared_ptr because plane() hands one back
    // and the caller draws on it afterwards -- unlike bar3d(), which takes its
    // data and is done.
    std::vector<std::shared_ptr<Plane2D>> planes;

    // Decoration. Font sizes live in axes_style, as they do for Axes::Impl,
    // which is what lets the widget panel drive them through one channel.
    std::string title, xtitle, ytitle, ztitle;

    bool        grid_enabled = true;
    GridOptions grid_opts;
    bool            legend_enabled = false;
    LegendOptions   legend_opts;
    ColorbarOptions colorbar_opts;
    AxesStyle   axes_style;
    Box3DStyle  box_style;
    BoxAspect   aspect;

    Camera3D camera;
    // What a double-click restores. Held separately from `camera` so that
    // navigating, or a scripted set_camera(), cannot redefine "reset".
    Camera3D default_camera;

    double xmin = 0, xmax = 1, ymin = 0, ymax = 1, zmin = 0, zmax = 1;
    bool   xlim_auto = true, ylim_auto = true, zlim_auto = true;

    std::optional<std::vector<Tick>> xticks_override, yticks_override, zticks_override;

    // The caller-thread half of RenderSnapshot3D's plane accessors -- see the
    // note there. Plane2D::Impl and PlaneSnapshot deliberately name their
    // orient/offset/opts/sheet identically, so one edit body writes both.
    std::size_t   plane_count() const     { return planes.size(); }
    Plane2D::Impl& plane_at(std::size_t i) { return *planes[i]->d; }

    RenderSnapshot3D build_snapshot() const {
        RenderSnapshot3D s;
        s.bars3d = bars3d;
        s.surfaces = surfaces;
        s.scatter3d = scatter3d;
        s.lines3d = lines3d;
        s.surface_tri = surface_tri;
        s.planes.reserve(planes.size());
        for (const auto& p : planes) s.planes.push_back(p->d->build_snapshot());
        s.title = title; s.xtitle = xtitle; s.ytitle = ytitle; s.ztitle = ztitle;
        s.grid_enabled = grid_enabled; s.grid_opts = grid_opts;
        s.legend_enabled = legend_enabled; s.legend_opts = legend_opts;
        s.colorbar_opts  = colorbar_opts;
        s.axes_style = axes_style;
        s.box_style  = box_style;
        s.aspect     = aspect;
        s.camera     = camera;
        s.default_camera = default_camera;
        s.xmin = xmin; s.xmax = xmax;
        s.ymin = ymin; s.ymax = ymax;
        s.zmin = zmin; s.zmax = zmax;
        s.xlim_auto = xlim_auto; s.ylim_auto = ylim_auto; s.zlim_auto = zlim_auto;
        s.xticks_override = xticks_override;
        s.yticks_override = yticks_override;
        s.zticks_override = zticks_override;
        return s;
    }
};

} // namespace sextant
