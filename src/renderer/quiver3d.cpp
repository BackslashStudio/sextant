#include "quiver3d.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sextant {
    namespace {
        // Below this the arrow's direction counts as pointing at the eye.
        constexpr double kDegenerate = 1e-9;
    }

    int quiver3d_outline(Vec3 t, Vec3 h, Vec3 facing, double k, const Quiver3DOptions& o, Vec3 (&out)[7]) {
        const Vec3 d = h - t;
        const double len = length(d);
        // Under a hundredth of a pixel, as arrow_outline().
        if (!(k > 0.0) || !(len > 0.01 * k)) return 0;
        const Vec3 u = d * (1.0 / len);
        const Vec3 across = cross(u, facing);
        if (length(across) < kDegenerate) return 0;
        const Vec3 n = normalize(across);
        double hl = std::max(static_cast<double>(o.head_length), 0.0) * k;
        double hw = 0.5 * std::max(static_cast<double>(o.head_width), 0.0) * k;
        if (len < hl) {
            hw *= len / hl;
            hl = len;
            const Vec3 b = h - u * hl;
            out[0] = b + n * hw;
            out[1] = h;
            out[2] = b - n * hw;
            return 3;
        }
        const double sw = 0.5 * std::max(static_cast<double>(o.width), 0.0) * k;
        const Vec3 b = h - u * hl;
        out[0] = t + n * sw;
        out[1] = b + n * sw;
        out[2] = b + n * hw;
        out[3] = h;
        out[4] = b - n * hw;
        out[5] = b - n * sw;
        out[6] = t - n * sw;
        return 7;
    }

    void quiver3d_arrows(const Projector3D& proj, const Quiver3DPlot& q, std::size_t plot,
                         std::vector<Quiver3DArrow>& out) {
        const Transform3D& tf = proj.transform();
        // Box units per pixel at the box centre, once per field.
        const double k = proj.box_units_per_pixel(Vec3{0.0, 0.0, 0.0});
        if (!(k > 0.0)) return;
        const Quiver3DColoring coloring = quiver3d_coloring(q);
        for_each_quiver3d_arrow(q, [&](std::size_t i, Vec3 td, Vec3 hd) {
            const Vec3 t = tf.to_box(td.x, td.y, td.z), h = tf.to_box(hd.x, hd.y, hd.z);
            const Vec3 mid = (t + h) * 0.5;
            const Vec3 facing = proj.has_eye_point() ? normalize(proj.eye_point() - mid)
                                                     : normalize(proj.eye_dir());
            Quiver3DArrow a;
            a.points = quiver3d_outline(t, h, facing, k, q.opts, a.p);
            if (a.points == 0) return;
            a.color = quiver3d_color(q, coloring, i);
            a.plot = plot;
            a.arrow = i;
            out.push_back(a);
        });
    }

    double quiver3d_plot_distance(const Quiver3DPlot& q, const Projector3D& proj) {
        // Bounding-box centre of the arrows drawn (both ends), as the other kinds.
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
        bool any = false;
        for_each_quiver3d_arrow(q, [&](std::size_t, Vec3 t, Vec3 h) {
            const double tv[3] = {t.x, t.y, t.z}, hv[3] = {h.x, h.y, h.z};
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min({lo[a], tv[a], hv[a]});
                hi[a] = std::max({hi[a], tv[a], hv[a]});
            }
            any = true;
        });
        if (!any) return 0.0;
        const Vec3 c{(lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5};
        return length(proj.transform().to_box(c.x, c.y, c.z) - eye_coord(proj));
    }

    std::vector<Quiver3DPolygon> plan_quivers3d(const Projector3D& proj, const std::vector<Quiver3DPlot>& quivers) {
        std::vector<Quiver3DArrow> all;
        for (std::size_t i = 0; i < quivers.size(); ++i)
            if (object_shown(quivers[i])) quiver3d_arrows(proj, quivers[i], i, all);

        std::vector<Quiver3DPolygon> out;
        if (all.empty()) return out;
        out.reserve(all.size());

        // The depth ramp: the same two numbers the shader gets.
        float dmin = 0.0f, dmax = 1.0f;
        box_depth_range(proj, dmin, dmax);
        const float dspan = (dmax - dmin) != 0.0f ? 1.0f / (dmax - dmin) : 0.0f;

        const double k = proj.box_units_per_pixel(Vec3{0.0, 0.0, 0.0});
        for (const Quiver3DArrow& a: all) {
            if (!(a.color.a > 0.0f)) continue;
            bool front = true;
            Vec3 centre{};
            for (int r = 0; r < a.points; ++r) {
                front = front && proj.in_front(a.p[r]);
                centre = centre + a.p[r];
            }
            if (!front) continue;
            centre = centre * (1.0 / static_cast<double>(a.points));
            const float depth = proj.project_box(centre).depth;
            const Color color = depth_shade(a.color, quivers[a.plot].opts.depthshade, (depth - dmin) * dspan);

            auto piece = [&](std::vector<Vec3> ring, bool head) {
                Quiver3DPolygon poly;
                poly.plot = a.plot;
                poly.arrow = a.arrow;
                poly.head = head;
                poly.depth = depth;
                poly.color = color;
                poly.xy.reserve(ring.size() * 2);
                for (const Vec3& p: ring) {
                    const Px3 q = proj.project_box(p);
                    poly.xy.push_back(q.x);
                    poly.xy.push_back(q.y);
                }
                poly.box = std::move(ring);
                out.push_back(std::move(poly));
            };
            if (a.points == 3) {
                piece({a.p[0], a.p[1], a.p[2]}, true);
                continue;
            }
            // The shaft's overlap into the head: half a pixel along the arrow,
            // while the head is still wider than the shaft there.
            Vec3 into{};
            if (color.a >= 1.0f) {
                const Vec3 base = (a.p[1] + a.p[5]) * 0.5;
                const Vec3 axis = a.p[3] - base;
                const double hl = length(axis);
                const double sw = 0.5 * length(a.p[1] - a.p[5]);
                const double hw = 0.5 * length(a.p[2] - a.p[4]);
                const double e = 0.5 * k;
                if (hl > e && hw * (1.0 - e / hl) >= sw) into = axis * (e / hl);
            }
            piece({a.p[0], a.p[1] + into, a.p[5] + into, a.p[6]}, false);
            piece({a.p[2], a.p[3], a.p[4]}, true);
        }

        // An arrow's pieces share its depth; the stable sort keeps the shaft first.
        std::stable_sort(out.begin(), out.end(),
                         [](const Quiver3DPolygon& a, const Quiver3DPolygon& b) { return a.depth > b.depth; });
        return out;
    }
} // namespace sextant
