#include "scatter3d.h"
#include "../colormaps.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sextant {
    Color scatter3d_point_color(const Scatter3DPlot& s, std::size_t i,
                                double vmin, double vmax) {
        Color base = s.opts.color;
        if (s.colormapped()) {
            // Same lookup and rounding as surface_cell_color().
            const double span = vmax - vmin;
            const double t = span != 0.0 ? (s.color_at(i) - vmin) / span : 0.0;
            const int idx = static_cast<int>(std::clamp(t, 0.0, 1.0) * 255.0);
            const uint8_t* lut = colormaps::get(s.opts.cmap) + idx * 4;
            base = {lut[0] / 255.0f, lut[1] / 255.0f, lut[2] / 255.0f, 1.0f};
        }
        return {base.r, base.g, base.b, scatter3d_alpha(s)};
    }

    double scatter3d_plot_distance(const Scatter3DPlot& s, const Projector3D& proj) {
        if (s.count() == 0) return 0.0;
        const Transform3D& tf = proj.transform();
        // Bounding-box centre (not the point mean), as the other kinds.
        double lo[3] = {
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()
        };
        double hi[3] = {
            -std::numeric_limits<double>::max(),
            -std::numeric_limits<double>::max(),
            -std::numeric_limits<double>::max()
        };
        for (std::size_t i = 0; i < s.count(); ++i) {
            const double p[3] = {s.x[i], s.y[i], s.z[i]};
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], p[a]);
                hi[a] = std::max(hi[a], p[a]);
            }
        }
        const Vec3 c{(lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5};
        return length(tf.to_box(c.x, c.y, c.z) - eye_coord(proj));
    }

    void scatter3d_draw_order(const Scatter3DPlot& s, const Projector3D& proj,
                              std::vector<std::size_t>& out) {
        const std::size_t n = s.count();
        out.clear();
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) out.push_back(i);

        const Transform3D& tf = proj.transform();
        std::vector<float> depth(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i)
            depth[i] = proj.project_box(tf.to_box(s.x[i], s.y[i], s.z[i])).depth;

        // Far to near, stable, by Px3::depth (what the depth buffer and painter
        // use).
        std::stable_sort(out.begin(), out.end(),
                         [&](std::size_t a, std::size_t b) { return depth[a] > depth[b]; });
    }

    std::vector<Scatter3DMarker> plan_scatter3d(const Projector3D& proj,
                                                const std::vector<Scatter3DPlot>& points) {
        std::vector<Scatter3DMarker> out;
        const Transform3D& tf = proj.transform();

        // The depth ramp: the same two numbers the shader gets.
        float dmin = 0.0f, dmax = 1.0f;
        box_depth_range(proj, dmin, dmax);
        const float dspan = (dmax - dmin) != 0.0f ? 1.0f / (dmax - dmin) : 0.0f;

        for (std::size_t pi = 0; pi < points.size(); ++pi) {
            const Scatter3DPlot& s = points[pi];
            if (s.opts.marker == MarkerStyle::None) continue;
            double vmin = 0.0, vmax = 1.0;
            scatter3d_value_range(s, vmin, vmax);
            for (std::size_t i = 0; i < s.count(); ++i) {
                const Vec3 b = tf.to_box(s.x[i], s.y[i], s.z[i]);
                // Drop points behind a perspective eye.
                if (!proj.in_front(b)) continue;
                const Px3 q = proj.project_box(b);
                Scatter3DMarker m;
                m.cx = q.x;
                m.cy = q.y;
                m.radius = std::max(0.0f, s.opts.size * 0.5f);
                m.marker = s.opts.marker;
                m.color = scatter3d_depth_shade(scatter3d_point_color(s, i, vmin, vmax),
                                                s.opts.depthshade,
                                                (q.depth - dmin) * dspan);
                m.depth = q.depth;
                m.box = b;
                m.plot = pi;
                m.index = i;
                out.push_back(m);
            }
        }

        std::stable_sort(out.begin(), out.end(),
                         [](const Scatter3DMarker& a, const Scatter3DMarker& b) {
                             return a.depth > b.depth;
                         });
        return out;
    }
} // namespace sextant
