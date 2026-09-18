#include "error_bar3d.h"
#include "error_bar_shape.h"
#include <cmath>

namespace sextant {

namespace {

double comp(Vec3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
void   set_comp(Vec3& v, int a, double d) { (a == 0 ? v.x : a == 1 ? v.y : v.z) = d; }
Vec3   unit_axis(int a) { Vec3 e{}; set_comp(e, a, 1.0); return e; }

// Below this a direction is taken to be pointing at the eye: a whisker seen
// exactly end-on, or a block edge, which covers no pixel either way.
constexpr double kDegenerate = 1e-9;

void pieces(const Projector3D& proj, const CowVec<double>& xs, const CowVec<double>& ys,
            const CowVec<double>& zs, const ErrorBar3DData& err,
            const ErrorBar3DOptions& style, Color color, float depthshade,
            ErrorBar3DOwner owner, std::size_t plot, std::vector<ErrorBar3DPiece>& out) {
    if (!errorbar3d_drawn(err, style)) return;
    const Transform3D& tf = proj.transform();

    // Box units per pixel *at the box centre*, once for the whole series: the
    // conversion every pixel length here goes through, so a distant bar is
    // smaller as a whole rather than thinner in some parts and not others.
    const double k = proj.box_units_per_pixel(Vec3{ 0.0, 0.0, 0.0 });
    if (!(k > 0.0)) return;
    const double hw = 0.5 * style.linewidth * k;

    Color face_c = color; face_c.a = color.a * std::max(style.box_alpha, 0.0f);
    Color edge_c = color; edge_c.a = color.a * std::max(style.edge_alpha, 0.0f);

    const std::size_t n = std::min({ xs.size(), ys.size(), zs.size() });
    for (std::size_t i = 0; i < n; ++i) {
        const double d[3] = { xs[i], ys[i], zs[i] };
        const Vec3 P = tf.to_box(d[0], d[1], d[2]);
        const Vec3 facing = proj.has_eye_point() ? normalize(proj.eye_point() - P)
                                                 : normalize(proj.eye_dir());

        auto stroke = [&](Vec3 a, Vec3 b, Color c) {
            ErrorBar3DPiece s;
            s.p[0] = a;  s.p[1] = b;
            s.facing = facing;
            s.half_width = hw;
            s.color = c;
            s.depthshade = depthshade;
            s.owner = owner;  s.plot = plot;  s.point = i;
            out.push_back(s);
        };

        // The one coordinate along axis `a` that data value `v` lands on --
        // through to_box() with the other two held at the point's own, so a
        // side with no offset lands on exactly the point's coordinate and
        // whisker_segments() sees nothing there.
        auto along = [&](int a, double v) {
            double q[3] = { d[0], d[1], d[2] };
            q[a] = v;
            return comp(tf.to_box(q[0], q[1], q[2]), a);
        };

        // ---- Whiskers and caps: 2D's whisker_segments(), in a frame laid in
        // the plane of the axis and the eye. `along` is the box coordinate on
        // the axis itself; `across` is box units along the billboard direction.
        for (int a = 0; a < 3; ++a) {
            if (!err.has_cap(a)) continue;
            const ErrOffsets o = err.cap(a, i);
            if (!o.any()) continue;
            const Vec3 axis = unit_axis(a);
            const Vec3 x_dir = cross(axis, facing);
            if (length(x_dir) < kDegenerate) continue;
            const Vec3 across = normalize(x_dir);
            const double c = comp(P, a);
            whisker_segments(0.0, c, along(a, d[a] - o.lo), along(a, d[a] + o.hi),
                             true, k, k, style.capsize, style.capstyle,
                             [&](double x0, double y0, double x1, double y1) {
                                 stroke(P + across * x0 + axis * (y0 - c),
                                        P + across * x1 + axis * (y1 - c), color);
                             });
        }

        // ---- The block. An axis with box data spans its offsets; one without
        // spans `boxwidth` centred on the point. A direction whose data gives
        // it no extent at all draws no block, 2D's rule for a flat rectangle.
        if (!err.any_box()) continue;
        double lo[3], hi[3];
        bool empty = false;
        for (int a = 0; a < 3 && !empty; ++a) {
            if (err.has_box(a)) {
                const ErrOffsets o = err.box(a, i);
                const double e0 = along(a, d[a] - o.lo), e1 = along(a, d[a] + o.hi);
                lo[a] = std::min(e0, e1);
                hi[a] = std::max(e0, e1);
            } else {
                const double half = 0.5 * std::max(style.boxwidth, 0.0f) * k;
                lo[a] = comp(P, a) - half;
                hi[a] = comp(P, a) + half;
            }
            empty = !(hi[a] > lo[a]);
        }
        if (empty) continue;

        if (face_c.a > 0.0f) {
            // bar3d_faces()' ring layout: for each axis, the two axes it spans
            // in index order, so every face is the same shape of ring.
            for (int a = 0; a < 3; ++a) {
                const int p = (a + 1) % 3, q = (a + 2) % 3;
                for (int end = 0; end < 2; ++end) {
                    ErrorBar3DPiece f;
                    f.face = true;
                    const double ring[4][2] = { { lo[p], lo[q] }, { hi[p], lo[q] },
                                                { hi[p], hi[q] }, { lo[p], hi[q] } };
                    for (int r = 0; r < 4; ++r) {
                        Vec3 pt{};
                        set_comp(pt, a, end ? hi[a] : lo[a]);
                        set_comp(pt, p, ring[r][0]);
                        set_comp(pt, q, ring[r][1]);
                        f.p[r] = pt;
                    }
                    f.color = face_c;
                    f.depthshade = depthshade;
                    f.owner = owner;  f.plot = plot;  f.point = i;
                    out.push_back(f);
                }
            }
        }

        if (edge_c.a > 0.0f) {
            // Twelve edges: for each axis, the four parallel to it.
            for (int a = 0; a < 3; ++a) {
                const int p = (a + 1) % 3, q = (a + 2) % 3;
                for (int corner = 0; corner < 4; ++corner) {
                    Vec3 e0{}, e1{};
                    const double vp = (corner & 1) ? hi[p] : lo[p];
                    const double vq = (corner & 2) ? hi[q] : lo[q];
                    set_comp(e0, p, vp);  set_comp(e0, q, vq);  set_comp(e0, a, lo[a]);
                    set_comp(e1, p, vp);  set_comp(e1, q, vq);  set_comp(e1, a, hi[a]);
                    stroke(e0, e1, edge_c);
                }
            }
        }
    }
}

} // namespace

void errorbar3d_pieces(const Projector3D& proj, const Scatter3DPlot& s, std::size_t plot,
                       std::vector<ErrorBar3DPiece>& out) {
    pieces(proj, s.x, s.y, s.z, s.err, s.opts.errorbar, errorbar3d_color(s),
           s.opts.depthshade, ErrorBar3DOwner::Scatter, plot, out);
}

void errorbar3d_pieces(const Projector3D& proj, const Line3DPlot& l, std::size_t plot,
                       std::vector<ErrorBar3DPiece>& out) {
    pieces(proj, l.x, l.y, l.z, l.err, l.opts.errorbar, errorbar3d_color(l),
           l.opts.depthshade, ErrorBar3DOwner::Line, plot, out);
}

bool errorbar3d_ribbon(const ErrorBar3DPiece& s, Vec3 corners[4]) {
    if (s.face || !(s.half_width > 0.0)) return false;
    const Vec3 side = cross(s.p[1] - s.p[0], s.facing);
    const double len = length(side);
    if (len < kDegenerate) return false;
    const Vec3 off = side * (s.half_width / len);
    corners[0] = s.p[0] - off;
    corners[1] = s.p[0] + off;
    corners[2] = s.p[1] + off;
    corners[3] = s.p[1] - off;
    return true;
}

std::vector<ErrorBar3DPolygon> plan_errorbars3d(const Projector3D& proj,
                                                const std::vector<Scatter3DPlot>& points,
                                                const std::vector<Line3DPlot>& lines) {
    std::vector<ErrorBar3DPiece> all;
    for (std::size_t i = 0; i < points.size(); ++i) errorbar3d_pieces(proj, points[i], i, all);
    for (std::size_t i = 0; i < lines.size(); ++i)  errorbar3d_pieces(proj, lines[i], i, all);

    std::vector<ErrorBar3DPolygon> out;
    if (all.empty()) return out;
    out.reserve(all.size());

    // The depth ramp, once -- the same two numbers the shader gets.
    float dmin = 0.0f, dmax = 1.0f;
    box_depth_range(proj, dmin, dmax);
    const float dspan = (dmax - dmin) != 0.0f ? 1.0f / (dmax - dmin) : 0.0f;

    std::vector<Px3> ring;
    for (const ErrorBar3DPiece& s : all) {
        ErrorBar3DPolygon poly;
        poly.owner = s.owner;
        poly.plot  = s.plot;
        poly.point = s.point;
        Vec3 centre{};
        if (s.face) {
            poly.box.assign(s.p, s.p + 4);
            proj.project_polygon(poly.box, ring);
            if (ring.size() < 3) continue;
            poly.xy.reserve(ring.size() * 2);
            for (const Px3& q : ring) { poly.xy.push_back(q.x); poly.xy.push_back(q.y); }
            for (const Vec3& p : poly.box) centre = centre + p;
            centre = centre * 0.25;
            poly.filled = true;
        } else {
            // A stroke with no ribbon covers no pixel in the raster path, so
            // it is not emitted here either.
            Vec3 corners[4];
            if (!errorbar3d_ribbon(s, corners)) continue;
            Px3 pa, pb;
            if (!proj.project_segment(s.p[0], s.p[1], pa, pb)) continue;
            poly.box = { s.p[0], s.p[1] };
            poly.xy  = { pa.x, pa.y, pb.x, pb.y };
            centre = (s.p[0] + s.p[1]) * 0.5;
            // The ribbon's width at its midpoint, in pixels: a billboard lies
            // across the view, so its box-space width divided by the local
            // scale is its width on the screen.
            const double here = proj.box_units_per_pixel(centre);
            poly.width = here > 0.0 ? static_cast<float>(2.0 * s.half_width / here) : 0.0f;
            if (!(poly.width > 0.0f)) continue;
        }
        poly.depth = proj.project_box(centre).depth;
        poly.color = depth_shade(s.color, s.depthshade, (poly.depth - dmin) * dspan);
        out.push_back(std::move(poly));
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const ErrorBar3DPolygon& a, const ErrorBar3DPolygon& b) {
                         return a.depth > b.depth;
                     });
    return out;
}

} // namespace sextant
