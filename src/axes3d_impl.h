#pragma once
// Internal header — defines Axes3D::Impl, the 3D sibling of Axes::Impl.
#include "sextant/axes3d.h"
#include "color_cycle.h"
#include "plane2d_impl.h"
#include "plot_objects.h"
#include "tick.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sextant {
    struct Axes3D::Impl {
        // CowVec-backed plot data, so snapshots share buffers.
        std::vector<Bar3DPlot> bars3d;
        std::vector<SurfacePlot> surfaces;
        std::vector<Scatter3DPlot> scatter3d;
        std::vector<Line3DPlot> lines3d;
        std::vector<SurfaceTriPlot> surface_tri;
        std::vector<TextPlot> texts;
        std::vector<Quiver3DPlot> quivers3d;

        // The figure's FigureOptions::mathtext, set when the figure makes this
        // axes; only warn_math() reads it (drawing reads the snapshot's).
        bool mathtext = true;

        // Shared with every plane, whose sheet draws from it too (see Axes::Impl::cycle).
        std::shared_ptr<ColorCycle> cycle = std::make_shared<ColorCycle>();

        // shared_ptr: plane() returns the plane for the caller to draw on.
        std::vector<std::shared_ptr<Plane2D>> planes;

        // Font sizes live in axes_style, as in Axes::Impl.
        std::string title, xtitle, ytitle, ztitle;
        TitleStamps title_stamps;

        bool grid_enabled = true;
        GridOptions grid_opts;
        bool legend_enabled = false;
        LegendOptions legend_opts;
        ColorbarOptions colorbar_opts;
        AxesStyle axes_style;
        Box3DStyle box_style;
        BoxAspect aspect;

        Camera3D camera;
        unsigned long long camera_stamp = 0;
        // What a double-click restores; unaffected by navigation and set_camera().
        Camera3D default_camera;
        // Whether set_default_camera() was called: then it is Home's camera.
        bool default_camera_set = false;

        double xmin = 0, xmax = 1, ymin = 0, ymax = 1, zmin = 0, zmax = 1;
        bool xlim_auto = true, ylim_auto = true, zlim_auto = true;
        LimitStamps limit_stamps;
        StyleStamps style_stamps;

        std::optional<std::vector<Tick>> xticks_override, yticks_override, zticks_override;

        // Caller-thread side of RenderSnapshot3D's plane accessors. Plane2D::Impl
        // and PlaneSnapshot share member names so one edit body writes both.
        std::size_t plane_count() const { return planes.size(); }
        Plane2D::Impl& plane_at(std::size_t i) { return *planes[i]->d; }
        const Plane2D::Impl& plane_at(std::size_t i) const { return *planes[i]->d; }

        // As Axes::Impl::last_id; last_plane_id is the plane() call's.
        ObjectId last_id = 0, last_plane_id = 0;
        ObjectId new_id() { return last_id = next_object_id(); }

        // The axes' own objects and its planes by identity, as Axes::Impl's (a
        // plane's objects: through its sheet). A removed plane's Plane2D handle
        // keeps working, detached: nothing it holds is drawn; restore_plane()
        // attaches it again.
        std::optional<ObjectRef> find_object(ObjectId id) const {
            return find_object_in(*this, kPlotKinds3D, id);
        }
        std::optional<RemovedObject<PlotObject3D>> remove_object(ObjectId id) {
            return remove_object_in<PlotObject3D>(*this, kPlotKinds3D, id);
        }
        bool restore_object(RemovedObject<PlotObject3D> r) {
            return restore_object_in(*this, kPlotKinds3D, std::move(r));
        }
        std::optional<std::size_t> move_object(ObjectId id, std::size_t to) {
            return move_object_in(*this, kPlotKinds3D, id, to);
        }
        std::optional<std::size_t> find_plane(ObjectId id) const {
            for (std::size_t i = 0; i < planes.size(); ++i)
                if (plane_at(i).id == id) return i;
            return std::nullopt;
        }

        // A plane taken out by remove_plane(): where it was, and the plane.
        struct RemovedPlane {
            std::size_t              index = 0;
            std::shared_ptr<Plane2D> plane;
        };
        std::optional<RemovedPlane> remove_plane(ObjectId id) {
            const auto i = find_plane(id);
            if (!i) return std::nullopt;
            RemovedPlane r{ *i, planes[*i] };
            planes.erase(planes.begin() + static_cast<std::ptrdiff_t>(*i));
            return r;
        }
        // Back at its index (clamped to the end); false if it is there already.
        bool restore_plane(RemovedPlane r) {
            if (!r.plane || find_plane(r.plane->d->id)) return false;
            const std::size_t at = std::min(r.index, planes.size());
            planes.insert(planes.begin() + static_cast<std::ptrdiff_t>(at), std::move(r.plane));
            return true;
        }
        // Returns where it was, for a move back.
        std::optional<std::size_t> move_plane(ObjectId id, std::size_t to) {
            const auto i = find_plane(id);
            if (!i) return std::nullopt;
            objects_detail::move_to(planes, *i, to);
            return i;
        }

        // quiver3d(), both forms: `c` null = flat. Resolves an unset amp
        // (quiver3d_auto_amp()).
        void ingest_quiver3d(std::span<const double> x, std::span<const double> y,
                             std::span<const double> z, std::span<const double> u,
                             std::span<const double> v, std::span<const double> w,
                             const std::span<const double>* c, Quiver3DOptions opts, const char* who);

        // Text, as Axes::Impl's. Throw on a non-finite number.
        void ingest_text(TextContent c, TextStyle style, const char* who);
        void set_text_data(std::size_t i, TextContent c, const char* who);

        RenderSnapshot3D build_snapshot() const {
            RenderSnapshot3D s;
            s.bars3d = bars3d;
            s.surfaces = surfaces;
            s.scatter3d = scatter3d;
            s.lines3d = lines3d;
            s.surface_tri = surface_tri;
            s.texts = texts;
            s.quivers3d = quivers3d;
            s.planes.reserve(planes.size());
            for (const auto& p: planes) s.planes.push_back(p->d->build_snapshot());
            s.title = title;
            s.xtitle = xtitle;
            s.ytitle = ytitle;
            s.ztitle = ztitle;
            s.title_stamps = title_stamps;
            s.grid_enabled = grid_enabled;
            s.grid_opts = grid_opts;
            s.legend_enabled = legend_enabled;
            s.legend_opts = legend_opts;
            s.colorbar_opts = colorbar_opts;
            s.axes_style = axes_style;
            s.box_style = box_style;
            s.aspect = aspect;
            s.camera = camera;
            s.camera_stamp = camera_stamp;
            s.default_camera = default_camera;
            s.default_camera_set = default_camera_set;
            s.xmin = xmin;
            s.xmax = xmax;
            s.ymin = ymin;
            s.ymax = ymax;
            s.zmin = zmin;
            s.zmax = zmax;
            s.xlim_auto = xlim_auto;
            s.ylim_auto = ylim_auto;
            s.zlim_auto = zlim_auto;
            s.limit_stamps = limit_stamps;
            s.style_stamps = style_stamps;
            s.xticks_override = xticks_override;
            s.yticks_override = yticks_override;
            s.zticks_override = zticks_override;
            return s;
        }
    };
} // namespace sextant
