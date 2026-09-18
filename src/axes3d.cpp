#include "axes3d_impl.h"
#include "plane2d_impl.h"
#include "coord_transform3d.h"   // clamp_camera
#include "delaunay.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace sextant {
    Axes3D::Axes3D() : d(std::make_unique<Impl>()) {
    }

    Axes3D::~Axes3D() = default;

    namespace {
        // Rejects a limit pair that can't be divided by.
        void check_limits(const char* who, double lo, double hi) {
            if (!std::isfinite(lo) || !std::isfinite(hi))
                throw std::invalid_argument(std::string(who) + ": limits must be finite");
            if (lo == hi)
                throw std::invalid_argument(std::string(who) + ": limits must span a non-zero interval");
        }

        // Length-checks and copies the caller's ErrorBar3D (same messages as 2D).
        ErrorBar3DData take_error_bars3d(const ErrorBar3D& eb, std::size_t n, const char* who) {
            auto take = [&](std::span<const double> s, const char* field) {
                if (!s.empty() && s.size() != n)
                    throw std::invalid_argument(
                        std::string(who) + ": err." + field + " has " +
                        std::to_string(s.size()) + " entries, need one per point (" +
                        std::to_string(n) + ")");
                return CowVec<double>(std::vector<double>(s.begin(), s.end()));
            };
            ErrorBar3DData d;
            d.cap_lo[0] = take(eb.x_cap_lo, "x_cap_lo");
            d.cap_hi[0] = take(eb.x_cap_hi, "x_cap_hi");
            d.box_lo[0] = take(eb.x_box_lo, "x_box_lo");
            d.box_hi[0] = take(eb.x_box_hi, "x_box_hi");
            d.cap_lo[1] = take(eb.y_cap_lo, "y_cap_lo");
            d.cap_hi[1] = take(eb.y_cap_hi, "y_cap_hi");
            d.box_lo[1] = take(eb.y_box_lo, "y_box_lo");
            d.box_hi[1] = take(eb.y_box_hi, "y_box_hi");
            d.cap_lo[2] = take(eb.z_cap_lo, "z_cap_lo");
            d.cap_hi[2] = take(eb.z_cap_hi, "z_cap_hi");
            d.box_lo[2] = take(eb.z_box_lo, "z_box_lo");
            d.box_hi[2] = take(eb.z_box_hi, "z_box_hi");
            return d;
        }

        // Bar footprint from the smallest gap in a grid vector (so bars never
        // overlap); 1 for a single entry.
        double grid_spacing(std::span<const double> c) {
            double best = 0.0;
            for (std::size_t i = 1; i < c.size(); ++i) {
                const double d = std::fabs(c[i] - c[i - 1]);
                if (d > 0.0 && (best == 0.0 || d < best)) best = d;
            }
            return best > 0.0 ? best : 1.0;
        }

        void check_grid(const char* who, std::span<const double> u, std::span<const double> v,
                        std::span<const double> heights) {
            if (u.empty() || v.empty())
                throw std::invalid_argument(std::string(who) + ": u and v must be non-empty");
            if (heights.size() != u.size() * v.size())
                throw std::invalid_argument(std::string(who) +
                                            ": heights must be |u| * |v| values, row-major with u as the major index");
            for (std::span<const double> s: {u, v, heights})
                for (double d: s)
                    if (!std::isfinite(d))
                        throw std::invalid_argument(std::string(who) + ": values must be finite");
        }
    } // namespace

    Axes3D& Axes3D::bar3d(PlaneOrientation orient,
                          std::span<const double> u, std::span<const double> v,
                          std::span<const double> heights, Bar3DOptions opts) {
        return bar3d(orient, u, v, heights, {}, opts);
    }

    Axes3D& Axes3D::bar3d(PlaneOrientation orient,
                          std::span<const double> u, std::span<const double> v,
                          std::span<const double> heights,
                          std::span<const double> bottoms, Bar3DOptions opts) {
        check_grid("Axes3D::bar3d", u, v, heights);
        if (!bottoms.empty() && bottoms.size() != heights.size())
            throw std::invalid_argument(
                "Axes3D::bar3d: bottoms must be empty or the same length as heights");

        Bar3DPlot b;
        b.orient = orient;
        b.opts = std::move(opts);
        b.u = std::vector<double>(u.begin(), u.end());
        b.v = std::vector<double>(v.begin(), v.end());
        b.heights = std::vector<double>(heights.begin(), heights.end());
        if (!bottoms.empty()) b.bottoms = std::vector<double>(bottoms.begin(), bottoms.end());
        // Resolve the footprint to data units once.
        b.u_width = grid_spacing(u) * std::max(0.0f, b.opts.width);
        b.v_width = grid_spacing(v) * std::max(0.0f, b.opts.depth);
        d->bars3d.push_back(std::move(b));
        return *this;
    }

    Axes3D& Axes3D::surface(PlaneOrientation orient,
                            std::span<const double> u, std::span<const double> v,
                            std::span<const double> heights, SurfaceOptions opts) {
        check_grid("Axes3D::surface", u, v, heights);
        // A surface needs cells, so at least 2 x 2 samples.
        if (u.size() < 2 || v.size() < 2)
            throw std::invalid_argument(
                "Axes3D::surface: u and v must each have at least 2 values -- a surface is "
                "made of cells between neighbouring samples");
        if (!std::isfinite(opts.vmin) || !std::isfinite(opts.vmax))
            throw std::invalid_argument("Axes3D::surface: vmin and vmax must be finite");

        SurfacePlot s;
        s.orient = orient;
        s.opts = std::move(opts);
        s.u = std::vector<double>(u.begin(), u.end());
        s.v = std::vector<double>(v.begin(), v.end());
        s.heights = std::vector<double>(heights.begin(), heights.end());
        d->surfaces.push_back(std::move(s));
        return *this;
    }

    // Vertex validation shared by every surface_tri() overload.
    namespace {
        void check_mesh_vertices(std::span<const double> x, std::span<const double> y,
                                 std::span<const double> z, std::span<const double> colors,
                                 const SurfaceTriOptions& opts) {
            // At least three vertices for a triangle.
            if (x.size() < 3)
                throw std::invalid_argument(
                    "Axes3D::surface_tri: a mesh needs at least three vertices");
            if (y.size() != x.size() || z.size() != x.size())
                throw std::invalid_argument(
                    "Axes3D::surface_tri: x, y and z must be the same length");
            if (!colors.empty() && colors.size() != x.size())
                throw std::invalid_argument(
                    "Axes3D::surface_tri: colors must be empty or the same length as x -- a "
                    "value belongs to a vertex, and a triangle interpolates between its three");
            for (std::span<const double> s: {x, y, z, colors})
                for (double v: s)
                    if (!std::isfinite(v))
                        throw std::invalid_argument("Axes3D::surface_tri: values must be finite");
            if (!std::isfinite(opts.vmin) || !std::isfinite(opts.vmax))
                throw std::invalid_argument("Axes3D::surface_tri: vmin and vmax must be finite");
        }

        // Builds the plot object once the topology is settled; `tri` is validated.
        SurfaceTriPlot make_mesh(std::span<const double> x, std::span<const double> y,
                                 std::span<const double> z,
                                 std::vector<std::uint32_t> tri,
                                 std::span<const double> colors, SurfaceTriOptions opts) {
            SurfaceTriPlot s;
            s.opts = std::move(opts);
            s.x = std::vector<double>(x.begin(), x.end());
            s.y = std::vector<double>(y.begin(), y.end());
            s.z = std::vector<double>(z.begin(), z.end());
            s.tri = std::move(tri);
            if (!colors.empty()) s.colors = std::vector<double>(colors.begin(), colors.end());
            return s;
        }
    } // namespace

    Axes3D& Axes3D::surface_tri(std::span<const double> x, std::span<const double> y,
                                std::span<const double> z,
                                std::span<const std::uint32_t> tri, SurfaceTriOptions opts) {
        return surface_tri(x, y, z, tri, std::span<const double>{}, std::move(opts));
    }

    Axes3D& Axes3D::surface_tri(std::span<const double> x, std::span<const double> y,
                                std::span<const double> z,
                                std::span<const std::uint32_t> tri,
                                std::span<const double> colors, SurfaceTriOptions opts) {
        check_mesh_vertices(x, y, z, colors, opts);
        // Empty topology is rejected: it would draw nothing.
        if (tri.empty())
            throw std::invalid_argument("Axes3D::surface_tri: tri must name at least one triangle");
        if (tri.size() % 3 != 0)
            throw std::invalid_argument(
                "Axes3D::surface_tri: tri must be a multiple of three -- three vertex "
                "indices per triangle, row-major");
        for (const std::uint32_t i: tri)
            if (static_cast<std::size_t>(i) >= x.size())
                throw std::invalid_argument(
                    "Axes3D::surface_tri: every index in tri must be less than the vertex count");

        // Degenerate triangles are kept so face counts match the caller's; the
        // normal is guarded in surface_tri_face().
        d->surface_tri.push_back(make_mesh(x, y, z,
                                           std::vector<std::uint32_t>(tri.begin(), tri.end()),
                                           colors, std::move(opts)));
        return *this;
    }

    Axes3D& Axes3D::surface_tri(std::span<const double> x, std::span<const double> y,
                                std::span<const double> z, PlaneOrientation orient,
                                SurfaceTriOptions opts) {
        return surface_tri(x, y, z, orient, std::span<const double>{}, std::move(opts));
    }

    Axes3D& Axes3D::surface_tri(std::span<const double> x, std::span<const double> y,
                                std::span<const double> z, PlaneOrientation orient,
                                std::span<const double> colors, SurfaceTriOptions opts) {
        check_mesh_vertices(x, y, z, colors, opts);

        // Project onto the two axes `orient` names. Only the triangulation uses
        // the orientation; afterwards only indices are stored.
        const Axis3Map m = axis_map(orient);
        std::span<const double> axis[3] = {x, y, z};
        std::vector<std::uint32_t> tri;
        if (!delaunay_triangulate(axis[m.u], axis[m.v], tri))
            throw std::invalid_argument(
                "Axes3D::surface_tri: these vertices have no triangulation in the plane "
                "given -- they are collinear there, or fewer than three of them are "
                "distinct. Rendering a blank box would be the worse answer; pick a "
                "different orientation, or pass the topology explicitly");

        // A concave domain gets its convex hull filled (no masking yet).
        d->surface_tri.push_back(make_mesh(x, y, z, std::move(tri), colors,
                                           std::move(opts)));
        return *this;
    }

    Axes3D& Axes3D::scatter3d(std::span<const double> x, std::span<const double> y,
                              std::span<const double> z, Scatter3DOptions opts) {
        return scatter3d(x, y, z, std::span<const double>{}, ErrorBar3D{}, std::move(opts));
    }

    Axes3D& Axes3D::scatter3d(std::span<const double> x, std::span<const double> y,
                              std::span<const double> z, const ErrorBar3D& err,
                              Scatter3DOptions opts) {
        return scatter3d(x, y, z, std::span<const double>{}, err, std::move(opts));
    }

    Axes3D& Axes3D::scatter3d(std::span<const double> x, std::span<const double> y,
                              std::span<const double> z, std::span<const double> colors,
                              Scatter3DOptions opts) {
        return scatter3d(x, y, z, colors, ErrorBar3D{}, std::move(opts));
    }

    Axes3D& Axes3D::scatter3d(std::span<const double> x, std::span<const double> y,
                              std::span<const double> z, std::span<const double> colors,
                              const ErrorBar3D& err, Scatter3DOptions opts) {
        if (x.empty())
            throw std::invalid_argument("Axes3D::scatter3d: x, y and z must be non-empty");
        if (y.size() != x.size() || z.size() != x.size())
            throw std::invalid_argument("Axes3D::scatter3d: x, y and z must be the same length");
        if (!colors.empty() && colors.size() != x.size())
            throw std::invalid_argument(
                "Axes3D::scatter3d: colors must be empty or the same length as x");
        for (std::span<const double> s: {x, y, z, colors})
            for (double d: s)
                if (!std::isfinite(d))
                    throw std::invalid_argument("Axes3D::scatter3d: values must be finite");
        if (!std::isfinite(opts.vmin) || !std::isfinite(opts.vmax))
            throw std::invalid_argument("Axes3D::scatter3d: vmin and vmax must be finite");
        ErrorBar3DData eb = take_error_bars3d(err, x.size(), "Axes3D::scatter3d");

        Scatter3DPlot s;
        s.err = std::move(eb);
        s.opts = std::move(opts);
        s.x = std::vector<double>(x.begin(), x.end());
        s.y = std::vector<double>(y.begin(), y.end());
        s.z = std::vector<double>(z.begin(), z.end());
        if (!colors.empty()) s.colors = std::vector<double>(colors.begin(), colors.end());
        d->scatter3d.push_back(std::move(s));
        return *this;
    }

    Axes3D& Axes3D::line3d(std::span<const double> x, std::span<const double> y,
                           std::span<const double> z, Line3DOptions opts) {
        return line3d(x, y, z, std::span<const double>{}, ErrorBar3D{}, std::move(opts));
    }

    Axes3D& Axes3D::line3d(std::span<const double> x, std::span<const double> y,
                           std::span<const double> z, const ErrorBar3D& err,
                           Line3DOptions opts) {
        return line3d(x, y, z, std::span<const double>{}, err, std::move(opts));
    }

    Axes3D& Axes3D::line3d(std::span<const double> x, std::span<const double> y,
                           std::span<const double> z, std::span<const double> colors,
                           Line3DOptions opts) {
        return line3d(x, y, z, colors, ErrorBar3D{}, std::move(opts));
    }

    Axes3D& Axes3D::line3d(std::span<const double> x, std::span<const double> y,
                           std::span<const double> z, std::span<const double> colors,
                           const ErrorBar3D& err, Line3DOptions opts) {
        // A path needs two points; one would draw nothing.
        if (x.size() < 2)
            throw std::invalid_argument("Axes3D::line3d: a path needs at least two points");
        if (y.size() != x.size() || z.size() != x.size())
            throw std::invalid_argument("Axes3D::line3d: x, y and z must be the same length");
        if (!colors.empty() && colors.size() != x.size())
            throw std::invalid_argument(
                "Axes3D::line3d: colors must be empty or the same length as x -- a value "
                "belongs to a point, and a segment ramps between the two it joins");
        for (std::span<const double> s: {x, y, z, colors})
            for (double v: s)
                if (!std::isfinite(v))
                    throw std::invalid_argument("Axes3D::line3d: values must be finite");
        if (!std::isfinite(opts.vmin) || !std::isfinite(opts.vmax))
            throw std::invalid_argument("Axes3D::line3d: vmin and vmax must be finite");
        ErrorBar3DData eb = take_error_bars3d(err, x.size(), "Axes3D::line3d");

        Line3DPlot l;
        l.err = std::move(eb);
        l.opts = std::move(opts);
        l.x = std::vector<double>(x.begin(), x.end());
        l.y = std::vector<double>(y.begin(), y.end());
        l.z = std::vector<double>(z.begin(), z.end());
        if (!colors.empty()) l.colors = std::vector<double>(colors.begin(), colors.end());
        d->lines3d.push_back(std::move(l));
        return *this;
    }

    std::shared_ptr<Plane2D> Axes3D::plane(PlaneOrientation orient, double offset,
                                           Plane2DOptions opts) {
        if (!std::isfinite(offset))
            throw std::invalid_argument("Axes3D::plane: offset must be finite");
        opts.alpha = std::clamp(opts.alpha, 0.0f, 1.0f);
        auto p = std::shared_ptr<Plane2D>(new Plane2D(orient, offset, opts));
        d->planes.push_back(p);
        return p;
    }

    Axes3D& Axes3D::set_title(std::string_view text, float fontsize) {
        d->title = text;
        d->axes_style.title_fontsize = fontsize;
        return *this;
    }

    Axes3D& Axes3D::set_xtitle(std::string_view text, float fontsize) {
        d->xtitle = text;
        d->axes_style.xtitle_fontsize = fontsize;
        return *this;
    }

    Axes3D& Axes3D::set_ytitle(std::string_view text, float fontsize) {
        d->ytitle = text;
        d->axes_style.ytitle_fontsize = fontsize;
        return *this;
    }

    Axes3D& Axes3D::set_ztitle(std::string_view text, float fontsize) {
        d->ztitle = text;
        d->axes_style.ztitle_fontsize = fontsize;
        return *this;
    }

    Axes3D& Axes3D::set_xlim(double lo, double hi) {
        check_limits("Axes3D::set_xlim", lo, hi);
        d->xmin = lo;
        d->xmax = hi;
        d->xlim_auto = false;
        return *this;
    }

    Axes3D& Axes3D::set_ylim(double lo, double hi) {
        check_limits("Axes3D::set_ylim", lo, hi);
        d->ymin = lo;
        d->ymax = hi;
        d->ylim_auto = false;
        return *this;
    }

    Axes3D& Axes3D::set_zlim(double lo, double hi) {
        check_limits("Axes3D::set_zlim", lo, hi);
        d->zmin = lo;
        d->zmax = hi;
        d->zlim_auto = false;
        return *this;
    }

    Axes3D& Axes3D::set_xticks(std::span<const double> pos, std::vector<std::string> labels) {
        if (pos.empty()) d->xticks_override.reset();
        else d->xticks_override = make_tick_override(pos, labels);
        return *this;
    }

    Axes3D& Axes3D::set_yticks(std::span<const double> pos, std::vector<std::string> labels) {
        if (pos.empty()) d->yticks_override.reset();
        else d->yticks_override = make_tick_override(pos, labels);
        return *this;
    }

    Axes3D& Axes3D::set_zticks(std::span<const double> pos, std::vector<std::string> labels) {
        if (pos.empty()) d->zticks_override.reset();
        else d->zticks_override = make_tick_override(pos, labels);
        return *this;
    }

    Axes3D& Axes3D::grid(bool enable, GridOptions opts) {
        d->grid_enabled = enable;
        d->grid_opts = opts;
        return *this;
    }

    Axes3D& Axes3D::set_axes_style(AxesStyle opts) {
        d->axes_style = opts;
        return *this;
    }

    Axes3D& Axes3D::set_box_style(Box3DStyle opts) {
        d->box_style = opts;
        return *this;
    }

    Axes3D& Axes3D::legend(LegendOptions opts) {
        d->legend_enabled = true;
        d->legend_opts = opts;
        return *this;
    }

    Axes3D& Axes3D::set_colorbar_style(ColorbarOptions opts) {
        d->colorbar_opts = opts;
        return *this;
    }

    Axes3D& Axes3D::set_box_aspect(BoxAspect a) {
        if (!(a.x > 0.0) || !(a.y > 0.0) || !(a.z > 0.0) ||
            !std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z))
            throw std::invalid_argument("Axes3D::set_box_aspect: sides must be finite and positive");
        d->aspect = a;
        return *this;
    }

    // Every camera setter goes through clamp_camera(), as navigation does.
    Axes3D& Axes3D::set_view(double azimuth_deg, double elevation_deg) {
        Camera3D cam = d->camera;
        cam.azimuth = azimuth_deg;
        cam.elevation = elevation_deg;
        d->camera = clamp_camera(cam);
        return *this;
    }

    Axes3D& Axes3D::set_camera(Camera3D cam) {
        d->camera = clamp_camera(cam);
        return *this;
    }

    Camera3D Axes3D::camera() const { return d->camera; }

    Axes3D& Axes3D::set_default_camera(Camera3D cam) {
        d->default_camera = clamp_camera(cam);
        return *this;
    }

    Axes3D& Axes3D::set_projection(Projection mode) {
        d->camera.projection = mode;
        return *this;
    }

    Axes3D& Axes3D::set_fov(double degrees) {
        Camera3D cam = d->camera;
        cam.fov = degrees;
        d->camera = clamp_camera(cam);
        return *this;
    }

    Axes3D& Axes3D::cla() {
        *d = Impl{}; // reset to defaults
        return *this;
    }
} // namespace sextant
