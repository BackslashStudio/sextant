#include "surface.h"
#include "../colormaps.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sextant {
    namespace {
        double comp(Vec3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

        // Data-space point of sample (i, j) via the plot's Axis3Map.
        Vec3 sample_point(const SurfacePlot& s, const Axis3Map& m,
                          std::size_t i, std::size_t j) {
            double c[3] = {0.0, 0.0, 0.0};
            c[m.u] = i < s.u.size() ? s.u[i] : 0.0;
            c[m.v] = j < s.v.size() ? s.v[j] : 0.0;
            c[m.h] = s.height_at(s.index_of(i, j));
            return {c[0], c[1], c[2]};
        }
    } // namespace

    void surface_cell(const SurfacePlot& s, std::size_t i, std::size_t j,
                      const Transform3D& tf, SurfaceCell& out) {
        const Axis3Map m = axis_map(s.orient);
        out.p[0] = sample_point(s, m, i, j);
        out.p[1] = sample_point(s, m, i + 1, j);
        out.p[2] = sample_point(s, m, i + 1, j + 1);
        out.p[3] = sample_point(s, m, i, j + 1);

        out.value = 0.25 * (comp(out.p[0], m.h) + comp(out.p[1], m.h)
                            + comp(out.p[2], m.h) + comp(out.p[3], m.h));

        // Normal from the diagonals, in box space (accounts for reversed limits and
        // BoxAspect). A degenerate cell takes the light head-on rather than black.
        const Vec3 b0 = tf.to_box(out.p[0].x, out.p[0].y, out.p[0].z);
        const Vec3 b1 = tf.to_box(out.p[1].x, out.p[1].y, out.p[1].z);
        const Vec3 b2 = tf.to_box(out.p[2].x, out.p[2].y, out.p[2].z);
        const Vec3 b3 = tf.to_box(out.p[3].x, out.p[3].y, out.p[3].z);
        const Vec3 n = cross(b2 - b0, b3 - b1);
        const double len = length(n);
        out.normal = len > 0.0 ? n * (1.0 / len) : Vec3{0.0, 0.0, 0.0};

        // |n.l|, not max(0, n.l): a sheet has two sides, so the normal's sign is
        // just winding. (Bars keep the sign; hence a separate function.)
        const double ndotl = len > 0.0
                                 ? std::fabs(dot(out.normal, normalize(kBar3DLight)))
                                 : 1.0;
        const double sh = std::clamp(s.opts.shading, 0.0f, 1.0f);
        out.shade = static_cast<float>(std::clamp(1.0 - sh * (1.0 - ndotl), 0.0, 1.0));
    }

    namespace {
        // Barycentric slack, so a ray at a cell centre (on the shared diagonal) or at
        // a mesh vertex isn't rejected by float error in the pixel-derived ray. A
        // millionth of the triangle's size: sub-pixel at any drawable size.
        constexpr double kEdgeEps = 1e-6;

        constexpr double kMiss = kTriRayMiss;
    } // namespace

    // Moller-Trumbore in box space.
    double tri_ray_t(const Projector3D::Ray3& r, Vec3 a, Vec3 b, Vec3 c) {
        const Vec3 e1 = b - a, e2 = c - a;
        const Vec3 p = cross(r.dir, e2);
        const double det = dot(e1, p);
        if (std::fabs(det) < 1e-15) return kMiss; // edge-on
        const double inv = 1.0 / det;
        const Vec3 tv = r.origin - a;
        const double u = dot(tv, p) * inv;
        if (u < -kEdgeEps || u > 1.0 + kEdgeEps) return kMiss;
        const Vec3 q = cross(tv, e1);
        const double v = dot(r.dir, q) * inv;
        if (v < -kEdgeEps || u + v > 1.0 + kEdgeEps) return kMiss;
        return dot(e2, q) * inv;
    }

    bool surface_ray_hit(const SurfacePlot& s, std::size_t k, const Projector3D& proj,
                         float px, float py, float& depth, std::size_t& sample) {
        if (k >= s.cell_count()) return false;
        const std::size_t nc = s.cell_cols();
        const std::size_t i = k / nc, j = k % nc;

        SurfaceCell cell;
        surface_cell(s, i, j, proj.transform(), cell);

        const Transform3D& tf = proj.transform();
        Vec3 b[4];
        for (int c = 0; c < 4; ++c)
            b[c] = tf.to_box(cell.p[c].x, cell.p[c].y, cell.p[c].z);

        const Projector3D::Ray3 r = proj.ray_from_pixel(px, py);
        // The same 0-2 diagonal as the vertex buffer.
        const double t0 = tri_ray_t(r, b[0], b[1], b[2]);
        const double t1 = tri_ray_t(r, b[0], b[2], b[3]);
        double t = kMiss;
        if (t0 != kMiss && t1 != kMiss) t = std::min(t0, t1);
        else if (t0 != kMiss) t = t0;
        else if (t1 != kMiss) t = t1;
        else return false;

        const Vec3 hit = r.origin + r.dir * t;
        if (!proj.in_front(hit)) return false;

        // Nearest of the four samples, in box space (the hint_labels index).
        int best = 0;
        double best_d2 = -1.0;
        for (int c = 0; c < 4; ++c) {
            const Vec3 d = hit - b[c];
            const double d2 = dot(d, d);
            if (best_d2 < 0.0 || d2 < best_d2) {
                best_d2 = d2;
                best = c;
            }
        }
        const std::size_t si[4] = {
            s.index_of(i, j), s.index_of(i + 1, j),
            s.index_of(i + 1, j + 1), s.index_of(i, j + 1)
        };
        sample = si[best];
        depth = proj.project_box(hit).depth;
        return true;
    }

    Color surface_cell_color(const SurfacePlot& s, const SurfaceCell& c,
                             double vmin, double vmax) {
        Color base = s.opts.color;
        if (s.opts.colormap) {
            const double span = vmax - vmin;
            const double t = span != 0.0 ? (c.value - vmin) / span : 0.0;
            const int idx = static_cast<int>(std::clamp(t, 0.0, 1.0) * 255.0);
            const uint8_t* lut = colormaps::get(s.opts.cmap) + idx * 4;
            base = {lut[0] / 255.0f, lut[1] / 255.0f, lut[2] / 255.0f, 1.0f};
        }
        return {
            base.r * c.shade, base.g * c.shade, base.b * c.shade,
            std::clamp(base.a * surface_alpha(s), 0.0f, 1.0f)
        };
    }

    void surface_cell_edges(const SurfaceCell& c, Vec3 out[4][2]) {
        for (int i = 0; i < 4; ++i) {
            out[i][0] = c.p[i];
            out[i][1] = c.p[(i + 1) & 3];
        }
    }

    void surface_draw_order(const SurfacePlot& s, const Projector3D& proj,
                            std::vector<std::size_t>& out) {
        const std::size_t n = s.cell_count();
        out.clear();
        out.reserve(n);
        for (std::size_t k = 0; k < n; ++k) out.push_back(k);
        if (n < 2) return;

        const Transform3D& tf = proj.transform();
        const Axis3Map m = axis_map(s.orient);
        const Vec3 eye = eye_coord(proj);
        const std::size_t nc = s.cell_cols();

        // The cell's low corner in box space; heights don't matter (grid lines are
        // the separating planes).
        auto grid_key = [&](std::size_t k, int which) {
            const std::size_t i = nc ? k / nc : 0, j = nc ? k % nc : 0;
            double c[3] = {0.0, 0.0, 0.0};
            c[m.u] = i < s.u.size() ? s.u[i] : 0.0;
            c[m.v] = j < s.v.size() ? s.v[j] : 0.0;
            const Vec3 box = tf.to_box(c[0], c[1], c[2]);
            return std::fabs(comp(box, which) - comp(eye, which));
        };

        std::stable_sort(out.begin(), out.end(),
                         [&](std::size_t a, std::size_t b) {
                             const double au = grid_key(a, m.u), bu = grid_key(b, m.u);
                             if (au != bu) return au > bu;
                             return grid_key(a, m.v) > grid_key(b, m.v);
                         });
    }

    double surface_plot_distance(const SurfacePlot& s, const Projector3D& proj) {
        if (s.count() == 0) return 0.0;
        const Transform3D& tf = proj.transform();
        const Axis3Map m = axis_map(s.orient);
        // Bounding-box centre (mean of two opposite corners), not the sample mean.
        const Vec3 a = sample_point(s, m, 0, 0);
        const Vec3 b = sample_point(s, m, s.u.size() - 1, s.v.size() - 1);
        const Vec3 c = (a + b) * 0.5;
        return length(tf.to_box(c.x, c.y, c.z) - eye_coord(proj));
    }

    std::vector<Surface3DPolygon> plan_surfaces3d(const Projector3D& proj,
                                                  const std::vector<SurfacePlot>& surfaces) {
        std::vector<Surface3DPolygon> out;
        const Transform3D& tf = proj.transform();
        // Box units per pixel at the box centre; wireframe widths scale from it.
        const double ref = proj.box_units_per_pixel(Vec3{0.0, 0.0, 0.0});

        std::vector<std::size_t> plot_order;
        for (std::size_t i = 0; i < surfaces.size(); ++i)
            if (surfaces[i].cell_count() > 0
                && surfaces[i].heights.size() >= surfaces[i].count())
                plot_order.push_back(i);
        std::stable_sort(plot_order.begin(), plot_order.end(),
                         [&](std::size_t a, std::size_t b) {
                             return surface_plot_distance(surfaces[a], proj) >
                                    surface_plot_distance(surfaces[b], proj);
                         });

        std::vector<Px3> ring;
        std::vector<Vec3> box_ring;
        std::vector<std::size_t> order, rank;
        for (const std::size_t si: plot_order) {
            const SurfacePlot& s = surfaces[si];
            const float plot_depth = static_cast<float>(surface_plot_distance(s, proj));
            const std::size_t nr = s.cell_rows();
            const std::size_t nc = s.cell_cols();
            double vmin = 0.0, vmax = 1.0;
            surface_value_range(s, vmin, vmax);
            Color edge = s.opts.edgecolor;
            edge.a = surface_edge_alpha(s);
            surface_draw_order(s, proj, order);
            const bool wire = s.opts.edges && s.opts.edge_linewidth > 0.0f;
            // Each cell's position in `order`, to give a shared edge to the later one.
            if (wire) {
                rank.assign(order.size(), 0);
                for (std::size_t r = 0; r < order.size(); ++r) rank[order[r]] = r;
            }

            SurfaceCell cell;
            for (const std::size_t k: order) {
                const std::size_t ci = k / nc, cj = k % nc;
                surface_cell(s, ci, cj, tf, cell);

                box_ring.clear();
                for (const Vec3& p: cell.p) box_ring.push_back(tf.to_box(p.x, p.y, p.z));

                // Two triangles on the raster path's diagonal, not one quad: a
                // cell's corners are generally not coplanar, and Newell's tests
                // need a plane.
                const int tri[2][3] = {{0, 1, 2}, {0, 2, 3}};
                const Color fill = surface_cell_color(s, cell, vmin, vmax);
                for (const auto& t: tri) {
                    const std::vector<Vec3> tb{box_ring[t[0]], box_ring[t[1]], box_ring[t[2]]};
                    proj.project_polygon(tb, ring);
                    if (ring.size() < 3) continue;
                    Surface3DPolygon poly;
                    poly.box = tb;
                    poly.xy.reserve(ring.size() * 2);
                    for (const Px3& q: ring) {
                        poly.xy.push_back(q.x);
                        poly.xy.push_back(q.y);
                    }
                    poly.fill = fill;
                    // The wireframe is the cell grid (emitted below), not the
                    // triangulation.
                    poly.stroke_width = 0.0f;
                    poly.depth = proj.project_box((tb[0] + tb[1] + tb[2])
                                                  * (1.0 / 3.0)).depth;
                    poly.plot_depth = plot_depth;
                    poly.plot = si;
                    poly.cell = k;
                    out.push_back(std::move(poly));
                }

                if (!wire) continue;

                // The wireframe as two-point segments (a non-planar ring can't be
                // ordered by the painter). Each edge once, given to the later-drawn
                // of its two cells so it goes over both fills.
                //
                // Edges are (0,1) (1,2) (2,3) (3,0) -- surface_cell_edges()'s
                // order -- and across them lie cells (i,j-1) (i+1,j) (i,j+1) (i-1,j).
                const bool has_nb[4] = {cj > 0, ci + 1 < nr, cj + 1 < nc, ci > 0};
                const std::size_t nb[4] = {k - 1, k + nc, k + 1, k - nc};
                for (int e = 0; e < 4; ++e) {
                    if (has_nb[e] && rank[nb[e]] > rank[k]) continue;
                    const Vec3 a = box_ring[e], b = box_ring[(e + 1) & 3];
                    Px3 pa, pb;
                    if (!proj.project_segment(a, b, pa, pb)) continue;
                    // Width measured at the segment's midpoint.
                    const double here = proj.box_units_per_pixel((a + b) * 0.5);
                    Surface3DPolygon seg;
                    seg.box = {a, b};
                    seg.xy = {pa.x, pa.y, pb.x, pb.y};
                    seg.filled = false;
                    seg.stroke = edge;
                    seg.stroke_width = here > 0.0
                                           ? static_cast<float>(s.opts.edge_linewidth * ref / here)
                                           : s.opts.edge_linewidth;
                    seg.depth = (pa.depth + pb.depth) * 0.5f;
                    seg.plot_depth = plot_depth;
                    seg.plot = si;
                    seg.cell = k;
                    out.push_back(std::move(seg));
                }
            }
        }
        return out;
    }
} // namespace sextant
