#include "bar3d.h"
#include <algorithm>
#include <numeric>

namespace sextant {

namespace {

double axis_sign(double lo, double hi) { return hi < lo ? -1.0 : 1.0; }

double comp(Vec3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

void   set_component(Vec3& v, int a, double d) { (a == 0 ? v.x : a == 1 ? v.y : v.z) = d; }

} // namespace

void bar3d_bounds(const Bar3DPlot& b, std::size_t k, double lo[3], double hi[3]) {
    const Axis3Map m = axis_map(b.orient);
    const std::size_t nv = b.v.size();
    const std::size_t i = nv ? k / nv : 0;
    const std::size_t j = nv ? k % nv : 0;

    lo[0] = lo[1] = lo[2] = 0.0;
    hi[0] = hi[1] = hi[2] = 0.0;
    const double ui = i < b.u.size() ? b.u[i] : 0.0;
    const double vj = j < b.v.size() ? b.v[j] : 0.0;
    lo[m.u] = ui - b.u_width * 0.5;  hi[m.u] = ui + b.u_width * 0.5;
    lo[m.v] = vj - b.v_width * 0.5;  hi[m.v] = vj + b.v_width * 0.5;
    lo[m.h] = b.h_lo(k);             hi[m.h] = b.h_hi(k);
}

bool bar3d_ray_hit(const Bar3DPlot& b, std::size_t k, const Projector3D& proj,
                   float px, float py, float& depth) {
    if (k >= b.count()) return false;
    double lo[3], hi[3];
    bar3d_bounds(b, k, lo, hi);
    return box_ray_hit(proj, lo, hi, px, py, depth);
}

void bar3d_faces(const Bar3DPlot& b, std::size_t k, const Transform3D& tf,
                 Bar3DFace out[6]) {
    // The box, in data space, one interval per box axis.
    double lo[3], hi[3];
    bar3d_bounds(b, k, lo, hi);

    // A reversed limit mirrors its axis, so the data-space "+" end of a face
    // is the box-space "-" one. Cosmetic -- it decides which faces are lit --
    // but wrong in a way that would look like a lighting bug rather than a
    // limits one, so it is taken from the transform rather than assumed.
    const double sgn[3] = { axis_sign(tf.xmin, tf.xmax),
                            axis_sign(tf.ymin, tf.ymax),
                            axis_sign(tf.zmin, tf.zmax) };

    int n = 0;
    for (int a = 0; a < 3; ++a) {
        // The two axes the face spans, in index order, so the ring below is
        // the same shape for every face.
        const int p = (a + 1) % 3, q = (a + 2) % 3;
        for (int end = 0; end < 2; ++end) {
            const double at = end ? hi[a] : lo[a];
            Bar3DFace& f = out[n++];
            const double ring[4][2] = { { lo[p], lo[q] }, { hi[p], lo[q] },
                                        { hi[p], hi[q] }, { lo[p], hi[q] } };
            for (int c = 0; c < 4; ++c) {
                Vec3 pt{};
                set_component(pt, a, at);
                set_component(pt, p, ring[c][0]);
                set_component(pt, q, ring[c][1]);
                f.p[c] = pt;
            }
            Vec3 nrm{};
            set_component(nrm, a, (end ? 1.0 : -1.0) * sgn[a]);
            f.normal = nrm;
            f.shade  = bar3d_shade(nrm, b.opts.shading);
        }
    }
}


void bar3d_edges(const Bar3DFace f[6], Vec3 out[12][2]) {
    // f[0] and f[1] are the two faces at the ends of the box's first axis, so
    // their rings between them touch every edge: four sides each, plus the
    // four connecting corresponding corners.
    int n = 0;
    for (int e = 0; e < 4; ++e) {
        out[n][0] = f[0].p[e];       out[n][1] = f[0].p[(e + 1) % 4]; ++n;
        out[n][0] = f[1].p[e];       out[n][1] = f[1].p[(e + 1) % 4]; ++n;
        out[n][0] = f[0].p[e];       out[n][1] = f[1].p[e];           ++n;
    }
}

Bar3DFaceOrder bar3d_face_order(const Bar3DFace f[6], const Transform3D& tf,
                                const Projector3D& proj) {
    Bar3DFaceOrder o;
    int back[6], front[6], nb = 0, nf = 0;
    for (int i = 0; i < 6; ++i) {
        Vec3 c{};
        for (const Vec3& p : f[i].p) c = c + tf.to_box(p.x, p.y, p.z);
        c = c * 0.25;
        (proj.faces_camera(c, f[i].normal) ? front[nf++] : back[nb++]) = i;
    }
    for (int i = 0; i < nb; ++i) o.index[i] = back[i];
    for (int i = 0; i < nf; ++i) o.index[nb + i] = front[i];
    o.front = nb;
    return o;
}

double bar3d_plot_distance(const Bar3DPlot& b, const Projector3D& proj) {
    const std::size_t n = b.count();
    if (n == 0) return 0.0;
    const Transform3D& tf = proj.transform();
    Bar3DFace f0[6], f1[6];
    bar3d_faces(b, 0, tf, f0);
    bar3d_faces(b, n - 1, tf, f1);
    Vec3 c{};
    for (const Vec3& p : f0[0].p) c = c + p;
    for (const Vec3& p : f1[1].p) c = c + p;
    c = c * 0.125;
    return length(tf.to_box(c.x, c.y, c.z) - eye_coord(proj));
}

void bar3d_draw_order(const Bar3DPlot& b, const Projector3D& proj,
                      std::vector<std::size_t>& out) {
    const std::size_t n = b.count();
    out.clear();
    out.reserve(n);
    for (std::size_t k = 0; k < n; ++k) out.push_back(k);
    if (n < 2) return;

    const Transform3D& tf = proj.transform();
    const Axis3Map m = axis_map(b.orient);
    const Vec3 eye = eye_coord(proj);
    const std::size_t nv = b.v.size();

    // The cell's own coordinate on each grid axis, in box space. The bar's
    // height plays no part: the separating planes are the grid lines, and a
    // bar's height cannot move it across one.
    auto grid_key = [&](std::size_t k, int which) {
        const std::size_t i = nv ? k / nv : 0, j = nv ? k % nv : 0;
        double c[3] = { 0.0, 0.0, 0.0 };
        c[m.u] = i < b.u.size() ? b.u[i] : 0.0;
        c[m.v] = j < b.v.size() ? b.v[j] : 0.0;
        const Vec3 box = tf.to_box(c[0], c[1], c[2]);
        return std::fabs(comp(box, which) - comp(eye, which));
    };

    std::stable_sort(out.begin(), out.end(),
                     [&](std::size_t a, std::size_t c) {
                         const double au = grid_key(a, m.u), cu = grid_key(c, m.u);
                         if (au != cu) return au > cu;
                         return grid_key(a, m.v) > grid_key(c, m.v);
                     });
}

std::vector<Bar3DPolygon> plan_bars3d(const Projector3D& proj,
                                      const std::vector<Bar3DPlot>& bars) {
    std::vector<Bar3DPolygon> out;
    const Transform3D& tf = proj.transform();
    // The reference depth the edge width is quoted at: one pixel at the box
    // centre is this many box units, and a bar further away covers more of
    // them per pixel, so its stroke comes out narrower in exactly that ratio.
    const double ref = proj.box_units_per_pixel(Vec3{ 0.0, 0.0, 0.0 });

    // Two bar3d objects in one axes have no separating planes between them, so
    // there is no exact order for the pair -- only the grid inside each has
    // one. Their centres' distance from the eye is the heuristic, and it is
    // one because nothing better exists short of a BSP over the whole scene.
    std::vector<std::size_t> plot_order;
    for (std::size_t i = 0; i < bars.size(); ++i)
        if (bars[i].count() > 0 && bars[i].heights.size() >= bars[i].count())
            plot_order.push_back(i);
    std::stable_sort(plot_order.begin(), plot_order.end(),
                     [&](std::size_t a, std::size_t b) {
                         return bar3d_plot_distance(bars[a], proj) >
                                bar3d_plot_distance(bars[b], proj);
                     });

    std::vector<Px3> ring;
    std::vector<std::size_t> order;
    for (const std::size_t bi : plot_order) {
        const Bar3DPlot& b = bars[bi];
        // The key the writer merges the plane plan against, carried on every
        // polygon this plot produces: whole objects interleave with planes,
        // the faces within one do not.
        const float plot_depth = static_cast<float>(bar3d_plot_distance(b, proj));
        const bool translucent = bar3d_translucent(b);
        const float alpha = bar3d_alpha(b);
        Color edge = b.opts.edgecolor;
        edge.a = bar3d_edge_alpha(b);
        bar3d_draw_order(b, proj, order);

        // An outline is a stroke on the faces while a bar hides its own back:
        // the six edges nobody can see are occluded, so stroking the three
        // visible faces draws exactly the nine that show. A translucent bar
        // hides nothing, so all twelve have to be drawn -- once each, and
        // after the fills, which is the order the raster path uses too. That
        // is what `edge_pass` collects them for.
        std::vector<Bar3DPolygon> edge_pass;

        Bar3DFace faces[6];
        for (const std::size_t k : order) {
            bar3d_faces(b, k, tf, faces);

            // The bar's own centre, in box space: the depth its outline width
            // is quoted at.
            Vec3 bar_centre{};
            for (const Vec3& p : faces[0].p) bar_centre = bar_centre + p;
            for (const Vec3& p : faces[1].p) bar_centre = bar_centre + p;
            bar_centre = bar_centre * 0.125;
            const Vec3 centre_box = tf.to_box(bar_centre.x, bar_centre.y, bar_centre.z);

            float stroke_w = 0.0f;
            if (b.opts.edges && b.opts.edge_linewidth > 0.0f) {
                const double here = proj.box_units_per_pixel(centre_box);
                stroke_w = here > 0.0
                    ? static_cast<float>(b.opts.edge_linewidth * ref / here)
                    : b.opts.edge_linewidth;
            }

            // Hidden faces first and then the visible ones when the bar can be
            // seen through; only the visible ones when it cannot.
            const Bar3DFaceOrder fo = bar3d_face_order(faces, tf, proj);
            for (int fi = translucent ? 0 : fo.front; fi < 6; ++fi) {
                const Bar3DFace& f = faces[fo.index[fi]];
                Vec3 centre{};
                std::vector<Vec3> box_ring;
                box_ring.reserve(4);
                for (const Vec3& p : f.p) {
                    const Vec3 q = tf.to_box(p.x, p.y, p.z);
                    box_ring.push_back(q);
                    centre = centre + q;
                }
                centre = centre * 0.25;

                proj.project_polygon(box_ring, ring);
                if (ring.size() < 3) continue;

                Bar3DPolygon poly;
                poly.xy.reserve(ring.size() * 2);
                for (const Px3& q : ring) { poly.xy.push_back(q.x); poly.xy.push_back(q.y); }
                poly.fill = { b.opts.color.r * f.shade, b.opts.color.g * f.shade,
                              b.opts.color.b * f.shade, alpha };
                poly.stroke       = edge;
                poly.stroke_width = translucent ? 0.0f : stroke_w;
                poly.depth        = proj.project_box(centre).depth;
                poly.plot_depth   = plot_depth;
                poly.plot         = bi;
                poly.bar          = k;
                poly.box          = box_ring;
                out.push_back(std::move(poly));
            }

            if (translucent && stroke_w > 0.0f) {
                Vec3 seg[12][2];
                bar3d_edges(faces, seg);
                for (const auto& e : seg) {
                    Px3 pa, pb;
                    const Vec3 ea = tf.to_box(e[0].x, e[0].y, e[0].z);
                    const Vec3 eb = tf.to_box(e[1].x, e[1].y, e[1].z);
                    if (!proj.project_segment(ea, eb, pa, pb))
                        continue;
                    Bar3DPolygon line;
                    line.box = { ea, eb };
                    line.xy = { pa.x, pa.y, pb.x, pb.y };
                    line.filled       = false;
                    line.stroke       = edge;
                    line.stroke_width = stroke_w;
                    line.depth        = (pa.depth + pb.depth) * 0.5f;
                    line.plot_depth   = plot_depth;
                    line.plot         = bi;
                    line.bar          = k;
                    edge_pass.push_back(std::move(line));
                }
            }
        }
        for (Bar3DPolygon& e : edge_pass) out.push_back(std::move(e));
    }
    return out;
}
} // namespace sextant
