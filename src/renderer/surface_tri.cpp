#include "surface_tri.h"
#include "../colormaps.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sextant {

void surface_tri_face(const SurfaceTriPlot& s, std::size_t f,
                      const Transform3D& tf, SurfaceTriFace& out) {
    std::size_t a = 0, b = 0, c = 0;
    s.face_verts(f, a, b, c);
    out.vert[0] = a; out.vert[1] = b; out.vert[2] = c;
    out.p[0] = s.vertex(a);
    out.p[1] = s.vertex(b);
    out.p[2] = s.vertex(c);

    // The normal in box space -- so it already accounts for a reversed limit
    // mirroring an axis and for a non-cubic BoxAspect stretching one, both of
    // which change what a face is tilted like and so how it should be lit.
    // surface_cell() takes the same care for the same reason.
    const Vec3 b0 = tf.to_box(out.p[0].x, out.p[0].y, out.p[0].z);
    const Vec3 b1 = tf.to_box(out.p[1].x, out.p[1].y, out.p[1].z);
    const Vec3 b2 = tf.to_box(out.p[2].x, out.p[2].y, out.p[2].z);
    // A cross product of two edges, where a cell uses its diagonals: three
    // points are always coplanar, so there is nothing for Newell's summed form
    // to stabilize here and the edges are the direct answer.
    const Vec3 n = cross(b1 - b0, b2 - b0);
    const double len = length(n);
    out.normal = len > 0.0 ? n * (1.0 / len) : Vec3{ 0.0, 0.0, 0.0 };

    // |n.l|, not max(0, n.l) -- see the header. A degenerate face takes the
    // light head-on rather than going black.
    const double ndotl = len > 0.0
        ? std::fabs(dot(out.normal, normalize(kBar3DLight)))
        : 1.0;
    const double sh = std::clamp(s.opts.shading, 0.0f, 1.0f);
    out.shade = static_cast<float>(std::clamp(1.0 - sh * (1.0 - ndotl), 0.0, 1.0));
}

Color surface_tri_vertex_color(const SurfaceTriPlot& s, std::size_t i,
                               double vmin, double vmax) {
    Color base = s.opts.color;
    if (s.colormapped()) {
        // The identical lookup surface_cell_color(), scatter3d_point_color()
        // and line3d_point_color() do, down to the rounding -- a shared rule
        // that four kinds spell differently is not shared.
        const double t = surface_tri_vertex_t(s, i, vmin, vmax);
        const int idx = static_cast<int>(std::clamp(t, 0.0, 1.0) * 255.0);
        const uint8_t* lut = colormaps::get(s.opts.cmap) + idx * 4;
        base = { lut[0] / 255.0f, lut[1] / 255.0f, lut[2] / 255.0f, 1.0f };
    }
    return { base.r, base.g, base.b, surface_tri_alpha(s) };
}

Color surface_tri_shaded_color(const SurfaceTriPlot& s, const SurfaceTriFace& f,
                               double t) {
    Color base = s.opts.color;
    if (s.colormapped()) {
        const int idx = static_cast<int>(std::clamp(t, 0.0, 1.0) * 255.0);
        const uint8_t* lut = colormaps::get(s.opts.cmap) + idx * 4;
        base = { lut[0] / 255.0f, lut[1] / 255.0f, lut[2] / 255.0f, 1.0f };
    }
    return { base.r * f.shade, base.g * f.shade, base.b * f.shade,
             surface_tri_alpha(s) };
}

void surface_tri_face_edges(const SurfaceTriFace& f, Vec3 out[3][2]) {
    for (int i = 0; i < 3; ++i) {
        out[i][0] = f.p[i];
        out[i][1] = f.p[(i + 1) % 3];
    }
}

void surface_tri_draw_order(const SurfaceTriPlot& s, const Projector3D& proj,
                            std::vector<std::size_t>& out) {
    const std::size_t n = s.face_count();
    out.clear();
    out.reserve(n);
    for (std::size_t f = 0; f < n; ++f) out.push_back(f);
    if (n < 2) return;

    const Transform3D& tf = proj.transform();
    std::vector<float> depth(n, 0.0f);
    for (std::size_t f = 0; f < n; ++f) {
        std::size_t a = 0, b = 0, c = 0;
        s.face_verts(f, a, b, c);
        const Vec3 pa = s.vertex(a), pb = s.vertex(b), pc = s.vertex(c);
        const Vec3 cen{ (pa.x + pb.x + pc.x) / 3.0,
                        (pa.y + pb.y + pc.y) / 3.0,
                        (pa.z + pb.z + pc.z) / 3.0 };
        depth[f] = proj.project_box(tf.to_box(cen.x, cen.y, cen.z)).depth;
    }

    // Far to near, stably, so a frame is the same frame as the last and two
    // faces at one depth keep their topology order.
    std::stable_sort(out.begin(), out.end(),
                     [&](std::size_t a, std::size_t b) { return depth[a] > depth[b]; });
}

double surface_tri_plot_distance(const SurfaceTriPlot& s, const Projector3D& proj) {
    if (s.count() == 0) return 0.0;
    const Transform3D& tf = proj.transform();
    // The centre of the mesh's own bounding box, not the mean of its vertices
    // -- line3d_plot_distance()'s and scatter3d_plot_distance()'s choice, for
    // the same reason: where the samples happen to bunch must not move the
    // "centre", and a mesh is exactly the kind of data that is dense in one
    // region and sparse elsewhere.
    double lo[3] = {  std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max() };
    double hi[3] = { -std::numeric_limits<double>::max(),
                     -std::numeric_limits<double>::max(),
                     -std::numeric_limits<double>::max() };
    for (std::size_t i = 0; i < s.count(); ++i) {
        const Vec3 p = s.vertex(i);
        const double c[3] = { p.x, p.y, p.z };
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], c[a]);
            hi[a] = std::max(hi[a], c[a]);
        }
    }
    const Vec3 c{ (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5 };
    return length(tf.to_box(c.x, c.y, c.z) - eye_coord(proj));
}

bool surface_tri_ray_hit(const SurfaceTriPlot& s, std::size_t f,
                         const Projector3D& proj, float px, float py,
                         float& depth, std::size_t& vertex) {
    if (f >= s.face_count()) return false;
    const Transform3D& tf = proj.transform();

    std::size_t vi[3] = { 0, 0, 0 };
    s.face_verts(f, vi[0], vi[1], vi[2]);
    Vec3 b[3];
    for (int c = 0; c < 3; ++c) {
        const Vec3 p = s.vertex(vi[c]);
        b[c] = tf.to_box(p.x, p.y, p.z);
    }

    const Projector3D::Ray3 r = proj.ray_from_pixel(px, py);
    const double t = tri_ray_t(r, b[0], b[1], b[2]);
    if (t == kTriRayMiss) return false;

    const Vec3 hit = r.origin + r.dir * t;
    if (!proj.in_front(hit)) return false;

    // Which of the three vertices the hit is nearest, in box space -- the
    // index a caller's hint_labels entry is written against.
    int best = 0;
    double best_d2 = -1.0;
    for (int c = 0; c < 3; ++c) {
        const Vec3 d = hit - b[c];
        const double d2 = dot(d, d);
        if (best_d2 < 0.0 || d2 < best_d2) { best_d2 = d2; best = c; }
    }
    vertex = vi[best];
    depth = proj.project_box(hit).depth;
    return true;
}

namespace {

// Fits the affine function through three (x, y) -> value samples, i.e. solves
// the 3x3 system whose matrix is [[x0,y0,1],[x1,y1,1],[x2,y2,1]]. False when
// the projected triangle has no area, which is a face seen exactly edge-on and
// is the writer's cue to fall back to a flat fill.
//
// One helper for both halves of the projective form -- the numerator (t/w) and
// the denominator (1/w) share this very matrix, so fitting them separately
// with two copies of the arithmetic would be two chances to transpose a term.
bool fit_affine(const float x[3], const float y[3], const double v[3],
                double det, float& a, float& bx, float& by) {
    if (det == 0.0) return false;
    // Cramer's rule on the same determinant, which the caller computed once.
    const double d0 = v[0] * (y[1] - y[2]) + v[1] * (y[2] - y[0]) + v[2] * (y[0] - y[1]);
    const double d1 = v[0] * (x[2] - x[1]) + v[1] * (x[0] - x[2]) + v[2] * (x[1] - x[0]);
    const double d2 = v[0] * (static_cast<double>(x[1]) * y[2] - static_cast<double>(x[2]) * y[1])
                    + v[1] * (static_cast<double>(x[2]) * y[0] - static_cast<double>(x[0]) * y[2])
                    + v[2] * (static_cast<double>(x[0]) * y[1] - static_cast<double>(x[1]) * y[0]);
    bx = static_cast<float>(d0 / det);
    by = static_cast<float>(d1 / det);
    a  = static_cast<float>(d2 / det);
    return true;
}

} // namespace

std::vector<SurfaceTriPolygon> plan_surface_tri3d(const Projector3D& proj,
                                                 const std::vector<SurfaceTriPlot>& meshes) {
    std::vector<SurfaceTriPolygon> out;
    const Transform3D& tf = proj.transform();
    // The reference depth the wireframe width is quoted at: one pixel at the
    // box centre is this many box units, so a face further away gets a
    // proportionally thinner stroke. The rule a bar outline and a grid
    // surface's wireframe both follow.
    const double ref = proj.box_units_per_pixel(Vec3{ 0.0, 0.0, 0.0 });

    std::vector<std::size_t> plot_order;
    for (std::size_t i = 0; i < meshes.size(); ++i)
        if (meshes[i].face_count() > 0) plot_order.push_back(i);
    std::stable_sort(plot_order.begin(), plot_order.end(),
                     [&](std::size_t a, std::size_t b) {
                         return surface_tri_plot_distance(meshes[a], proj) >
                                surface_tri_plot_distance(meshes[b], proj);
                     });

    std::vector<Px3> ring;
    std::vector<Vec3> box_ring;
    std::vector<std::size_t> order;
    for (const std::size_t mi : plot_order) {
        const SurfaceTriPlot& s = meshes[mi];
        const float plot_depth = static_cast<float>(surface_tri_plot_distance(s, proj));
        double vmin = 0.0, vmax = 1.0;
        surface_tri_value_range(s, vmin, vmax);
        Color edge = s.opts.edgecolor;
        edge.a = surface_tri_edge_alpha(s);
        const bool wire = s.opts.edges && s.opts.edge_linewidth > 0.0f;

        // Back to front by the face centroid -- a heuristic, unlike a grid's
        // exact cell order (see surface_tri_draw_order()). The painter
        // re-derives every pair exactly afterwards, so what this settles is
        // only the input order and the ties.
        surface_tri_draw_order(s, proj, order);

        SurfaceTriFace face;
        for (const std::size_t f : order) {
            surface_tri_face(s, f, tf, face);

            box_ring.clear();
            for (const Vec3& p : face.p) box_ring.push_back(tf.to_box(p.x, p.y, p.z));

            proj.project_polygon(box_ring, ring);
            if (ring.size() >= 3) {
                SurfaceTriPolygon poly;
                poly.box = box_ring;
                poly.xy.reserve(ring.size() * 2);
                for (const Px3& q : ring) { poly.xy.push_back(q.x); poly.xy.push_back(q.y); }

                // The centroid's value, which is what a flat fill shows and
                // what a degenerate gradient falls back to.
                double tv[3] = { 0.0, 0.0, 0.0 };
                for (int c = 0; c < 3; ++c)
                    tv[c] = s.colormapped()
                        ? surface_tri_vertex_t(s, face.vert[c], vmin, vmax) : 0.0;
                const double tc = (tv[0] + tv[1] + tv[2]) / 3.0;
                poly.fill        = surface_tri_shaded_color(s, face, tc);
                poly.shade       = face.shade;
                poly.alpha       = surface_tri_alpha(s);
                poly.colormapped = s.colormapped();
                poly.cmap        = s.opts.cmap;

                // The gradient, fitted through the *unclipped* corners rather
                // than through `ring`: the ring may have been cut by the near
                // plane, and the field is a property of the triangle's plane,
                // not of whichever part of it survived.
                if (poly.colormapped) {
                    float px[3], py[3];
                    double num[3], den[3];
                    bool ok = true;
                    for (int c = 0; c < 3 && ok; ++c) {
                        const Px3 q = proj.project_box(box_ring[c]);
                        if (!(q.w > 0.0f)) { ok = false; break; }
                        px[c] = q.x; py[c] = q.y;
                        // Perspective-correct interpolation in two halves: a
                        // quantity linear in the scene has t/w and 1/w exactly
                        // affine in screen space, so their ratio is exact
                        // everywhere -- under ortho every w is 1 and this
                        // collapses to the plain affine fit with no branch.
                        den[c] = 1.0 / q.w;
                        num[c] = tv[c] * den[c];
                    }
                    const double det = ok
                        ? (static_cast<double>(px[0]) * (py[1] - py[2])
                         + static_cast<double>(px[1]) * (py[2] - py[0])
                         + static_cast<double>(px[2]) * (py[0] - py[1]))
                        : 0.0;
                    if (ok && fit_affine(px, py, num, det, poly.na, poly.nx, poly.ny)
                           && fit_affine(px, py, den, det, poly.wa, poly.wx, poly.wy)) {
                        // The steepest-change direction at the centroid, which
                        // is where the fan (if any) is least wrong. d(n/w) has
                        // the numerator n'w - nw', and the positive w^2 below
                        // it only scales, so it drops out of a direction.
                        const float cx = (px[0] + px[1] + px[2]) / 3.0f;
                        const float cy = (py[0] + py[1] + py[2]) / 3.0f;
                        const float nc = poly.na + poly.nx * cx + poly.ny * cy;
                        const float wc = poly.wa + poly.wx * cx + poly.wy * cy;
                        float gx = poly.nx * wc - nc * poly.wx;
                        float gy = poly.ny * wc - nc * poly.wy;
                        const float gl = std::hypot(gx, gy);
                        // Three equal values give a zero gradient, which is a
                        // flat fill and not a ramp -- the same answer an
                        // edge-on face gets from `det == 0` above.
                        if (gl > 1e-12f) { poly.gx = gx / gl; poly.gy = gy / gl; }
                    }
                }

                poly.stroke_width = 0.0f;
                poly.depth = proj.project_box((box_ring[0] + box_ring[1] + box_ring[2])
                                              * (1.0 / 3.0)).depth;
                poly.plot_depth = plot_depth;
                poly.plot = mi;
                poly.face = f;
                out.push_back(std::move(poly));
            }

            if (!wire) continue;

            // All three edges, from every face -- see the header on why a mesh
            // cannot cheaply do what plan_surfaces3d() does and emit each
            // shared edge once.
            //
            // A two-point ring, not a closed triangle outline: `ring_plane()`
            // refuses two points, so the painter orders it with
            // stroke_vs_polygon() and can never take it for a blade. That is
            // post-step-10.1's finding, and it binds here for the same reason
            // -- a wired mesh is the scene that can contribute the most thin
            // cutting planes of anything in this library.
            Vec3 seg[3][2];
            surface_tri_face_edges(face, seg);
            for (const auto& e : seg) {
                const Vec3 a = tf.to_box(e[0].x, e[0].y, e[0].z);
                const Vec3 b = tf.to_box(e[1].x, e[1].y, e[1].z);
                Px3 pa, pb;
                if (!proj.project_segment(a, b, pa, pb)) continue;
                // Quoted at the segment's own midpoint, the rule a bar outline
                // follows: one pixel at the box centre, thinner with distance.
                const double here = proj.box_units_per_pixel((a + b) * 0.5);
                SurfaceTriPolygon s2;
                s2.box          = { a, b };
                s2.xy           = { pa.x, pa.y, pb.x, pb.y };
                s2.filled       = false;
                s2.stroke       = edge;
                s2.stroke_width = here > 0.0
                    ? static_cast<float>(s.opts.edge_linewidth * ref / here)
                    : s.opts.edge_linewidth;
                s2.depth        = (pa.depth + pb.depth) * 0.5f;
                s2.plot_depth   = plot_depth;
                s2.plot         = mi;
                s2.face         = f;
                out.push_back(std::move(s2));
            }
        }
    }
    return out;
}

} // namespace sextant
