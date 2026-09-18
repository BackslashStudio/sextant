#include "surface.h"
#include "../colormaps.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sextant {

namespace {

double comp(Vec3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

// The data-space point of sample (i, j), through the plot's own Axis3Map --
// the one place a surface's u/v/h become x/y/z, exactly as bar3d_bounds() is
// for a bar.
Vec3 sample_point(const SurfacePlot& s, const Axis3Map& m,
                  std::size_t i, std::size_t j) {
    double c[3] = { 0.0, 0.0, 0.0 };
    c[m.u] = i < s.u.size() ? s.u[i] : 0.0;
    c[m.v] = j < s.v.size() ? s.v[j] : 0.0;
    c[m.h] = s.height_at(s.index_of(i, j));
    return { c[0], c[1], c[2] };
}

} // namespace

void surface_cell(const SurfacePlot& s, std::size_t i, std::size_t j,
                  const Transform3D& tf, SurfaceCell& out) {
    const Axis3Map m = axis_map(s.orient);
    out.p[0] = sample_point(s, m, i,     j);
    out.p[1] = sample_point(s, m, i + 1, j);
    out.p[2] = sample_point(s, m, i + 1, j + 1);
    out.p[3] = sample_point(s, m, i,     j + 1);

    out.value = 0.25 * (comp(out.p[0], m.h) + comp(out.p[1], m.h)
                      + comp(out.p[2], m.h) + comp(out.p[3], m.h));

    // The normal from the diagonals, in box space -- so it already accounts
    // for a reversed limit mirroring an axis and for a non-cubic BoxAspect
    // stretching one, both of which change what a cell is tilted like and so
    // how it should be lit. A degenerate cell (two coincident corners, or a
    // perfectly edge-on sliver) has no normal; it takes the light head-on
    // rather than going black, since black would read as a hole in the sheet.
    const Vec3 b0 = tf.to_box(out.p[0].x, out.p[0].y, out.p[0].z);
    const Vec3 b1 = tf.to_box(out.p[1].x, out.p[1].y, out.p[1].z);
    const Vec3 b2 = tf.to_box(out.p[2].x, out.p[2].y, out.p[2].z);
    const Vec3 b3 = tf.to_box(out.p[3].x, out.p[3].y, out.p[3].z);
    const Vec3 n = cross(b2 - b0, b3 - b1);
    const double len = length(n);
    out.normal = len > 0.0 ? n * (1.0 / len) : Vec3{ 0.0, 0.0, 0.0 };

    // **The absolute value of n.l, not max(0, n.l).** A surface is a sheet
    // with two sides and no outside, so the sign of its normal is an artefact
    // of the winding rather than a fact about the geometry: with max(0, .) the
    // far side of a fold would go to black and the picture would grow a shadow
    // that is not there. bar3d_shade() cannot do this -- a bar's normal points
    // out of a solid and its sign means something -- which is the whole reason
    // the two are separate functions rather than one with a flag.
    const double ndotl = len > 0.0
        ? std::fabs(dot(out.normal, normalize(kBar3DLight)))
        : 1.0;
    const double sh = std::clamp(s.opts.shading, 0.0f, 1.0f);
    out.shade = static_cast<float>(std::clamp(1.0 - sh * (1.0 - ndotl), 0.0, 1.0));
}

namespace {

// The barycentric bounds are relaxed by this much, which is the whole reason
// it is named. A cell is split into two triangles along a diagonal, and the
// centre of a cell lies *exactly* on that diagonal -- so a ray aimed at the
// middle of a cell, which is precisely where a reader points, lands on the
// shared edge and can be rejected by both triangles at once for want of a
// last bit. The edge has to belong to both rather than to neither; taking the
// nearer of two identical hits costs nothing.
//
// **Sized against the pixel, not against the double**, which v1.0 step 14.4
// is what made load-bearing. The ray comes from a pixel whose coordinates
// are floats, so the barycentric coordinate of a point aimed at an exact
// corner carries roughly 1e-7 of relative error -- fifty times the 1e-9 this
// used to be. For a grid surface that only ever showed up at a cell centre,
// where two triangles both claim the point and either will do; for a mesh it
// is the *vertex*, which is the primary thing a reader points at and the
// index every hint_labels entry is written against. The slack is a millionth
// of a triangle's own size, so it is sub-pixel at any size worth drawing.
constexpr double kEdgeEps = 1e-6;

constexpr double kMiss = kTriRayMiss;

} // namespace

// Moller-Trumbore, in box space -- see the header.
double tri_ray_t(const Projector3D::Ray3& r, Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = cross(r.dir, e2);
    const double det = dot(e1, p);
    if (std::fabs(det) < 1e-15) return kMiss;   // edge-on
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
    // The same 0-2 diagonal the vertex buffer is built on. A different one
    // here would put the tooltip on a sliver of geometry that is not drawn
    // wherever the quad is warped.
    const double t0 = tri_ray_t(r, b[0], b[1], b[2]);
    const double t1 = tri_ray_t(r, b[0], b[2], b[3]);
    double t = kMiss;
    if (t0 != kMiss && t1 != kMiss) t = std::min(t0, t1);
    else if (t0 != kMiss)           t = t0;
    else if (t1 != kMiss)           t = t1;
    else                            return false;

    const Vec3 hit = r.origin + r.dir * t;
    if (!proj.in_front(hit)) return false;

    // Which of the four samples the hit is nearest, in box space -- the index
    // a caller's hint_labels entry is written against.
    int best = 0;
    double best_d2 = -1.0;
    for (int c = 0; c < 4; ++c) {
        const Vec3 d = hit - b[c];
        const double d2 = dot(d, d);
        if (best_d2 < 0.0 || d2 < best_d2) { best_d2 = d2; best = c; }
    }
    const std::size_t si[4] = { s.index_of(i, j),         s.index_of(i + 1, j),
                                s.index_of(i + 1, j + 1), s.index_of(i, j + 1) };
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
        base = { lut[0] / 255.0f, lut[1] / 255.0f, lut[2] / 255.0f, 1.0f };
    }
    return { base.r * c.shade, base.g * c.shade, base.b * c.shade,
             std::clamp(base.a * surface_alpha(s), 0.0f, 1.0f) };
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

    // The cell's own low corner on each grid axis, in box space. The heights
    // play no part: the separating planes are the grid lines, and a sample
    // cannot move a cell across one.
    auto grid_key = [&](std::size_t k, int which) {
        const std::size_t i = nc ? k / nc : 0, j = nc ? k % nc : 0;
        double c[3] = { 0.0, 0.0, 0.0 };
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
    // The centre of the grid's own bounding box, which is what a bar plot's
    // distance is too -- the mean of two opposite corners rather than of every
    // sample, so a densely sampled ripple does not pull the "centre" toward
    // wherever the samples happen to bunch.
    const Vec3 a = sample_point(s, m, 0, 0);
    const Vec3 b = sample_point(s, m, s.u.size() - 1, s.v.size() - 1);
    const Vec3 c = (a + b) * 0.5;
    return length(tf.to_box(c.x, c.y, c.z) - eye_coord(proj));
}

std::vector<Surface3DPolygon> plan_surfaces3d(const Projector3D& proj,
                                              const std::vector<SurfacePlot>& surfaces) {
    std::vector<Surface3DPolygon> out;
    const Transform3D& tf = proj.transform();
    // The reference depth the wireframe width is quoted at: one pixel at the
    // box centre is this many box units, so a cell further away gets a
    // proportionally thinner stroke. The same rule a bar outline follows.
    const double ref = proj.box_units_per_pixel(Vec3{ 0.0, 0.0, 0.0 });

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
    for (const std::size_t si : plot_order) {
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
        // Where each cell sits in `order`, so an edge shared by two cells can
        // be given to the one drawn later. See the wireframe below.
        if (wire) {
            rank.assign(order.size(), 0);
            for (std::size_t r = 0; r < order.size(); ++r) rank[order[r]] = r;
        }

        SurfaceCell cell;
        for (const std::size_t k : order) {
            const std::size_t ci = k / nc, cj = k % nc;
            surface_cell(s, ci, cj, tf, cell);

            box_ring.clear();
            for (const Vec3& p : cell.p) box_ring.push_back(tf.to_box(p.x, p.y, p.z));

            // **Two triangles, not one quad, and the diagonal is the same one
            // the raster path splits on.**
            //
            // A cell's four samples are not coplanar in general, and not
            // nearly: on the gallery's own ripple they miss a common plane by
            // 10-18% of the cell's own width. So a cell *has* no plane, and
            // that matters twice. It matters to the picture, because the
            // raster path has always drawn two triangles here while the SVG
            // drew one quad -- for a warped cell those are different shapes,
            // and the two outputs were quietly disagreeing about the geometry
            // rather than only about the order. And it matters to the painter
            // (step 9), because every one of Newell's tests asks which side of
            // a polygon's plane something lies on: a quad with no plane
            // answers that question with an average that contains none of its
            // own vertices, so pairs never resolve and the algorithm splits
            // them over and over. That is what made three of
            // `test_translucent3d`'s cells wrong.
            const int tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
            const Color fill = surface_cell_color(s, cell, vmin, vmax);
            for (const auto& t : tri) {
                const std::vector<Vec3> tb{ box_ring[t[0]], box_ring[t[1]], box_ring[t[2]] };
                proj.project_polygon(tb, ring);
                if (ring.size() < 3) continue;
                Surface3DPolygon poly;
                poly.box = tb;
                poly.xy.reserve(ring.size() * 2);
                for (const Px3& q : ring) { poly.xy.push_back(q.x); poly.xy.push_back(q.y); }
                poly.fill         = fill;
                // The wireframe is the *cell* grid, not the triangulation, so
                // it is emitted once below rather than stroked onto both
                // halves -- which would draw the diagonal and turn every
                // wireframe into a different picture.
                poly.stroke_width = 0.0f;
                poly.depth        = proj.project_box((tb[0] + tb[1] + tb[2])
                                                     * (1.0 / 3.0)).depth;
                poly.plot_depth   = plot_depth;
                poly.plot         = si;
                poly.cell         = k;
                out.push_back(std::move(poly));
            }

            if (!wire) continue;

            // **The wireframe as its cell edges, one two-point segment each --
            // not the cell's outline as one closed ring.** The ring is the
            // shape the fill path stopped using in post-step-9, and for the
            // same reason: a cell's four corners have no common plane, and
            // Newell's algorithm cannot order a polygon that has none. It
            // could not be a blade, and as a *victim* it was worse -- cut by a
            // plane that three of its corners lie on, it came back as itself
            // plus a copy of one of its own triangles, which the area test
            // takes for progress. So every wireframe export split until it hit
            // its bound, and every piece reached the writer as a filled polygon.
            //
            // A segment has no plane to be wrong about. It is the shape a
            // translucent bar's box edge already takes, and the painter orders
            // it against every polygon and cuts it where it has to.
            //
            // **Each edge once, and given to the later-drawn of its two
            // cells.** Once, because that is what the raster path draws: both
            // copies of a shared edge land at one depth, where GL_LESS rejects
            // the second and a peel pass takes only one. The later cell,
            // because the edge has to be drawn over *both* its cells' fills --
            // in the whole-object order that is simply emitting it after the
            // later cell, and in the painter's it is the tie-break: a segment is
            // never farther than a triangle it bounds, and where the two tie,
            // the one merged in later comes out later.
            //
            // Edges are (0,1) (1,2) (2,3) (3,0) -- surface_cell_edges()'s
            // order -- and across them lie cells (i,j-1) (i+1,j) (i,j+1) (i-1,j).
            const bool has_nb[4] = { cj > 0, ci + 1 < nr, cj + 1 < nc, ci > 0 };
            const std::size_t nb[4] = { k - 1, k + nc, k + 1, k - nc };
            for (int e = 0; e < 4; ++e) {
                if (has_nb[e] && rank[nb[e]] > rank[k]) continue;
                const Vec3 a = box_ring[e], b = box_ring[(e + 1) & 3];
                Px3 pa, pb;
                if (!proj.project_segment(a, b, pa, pb)) continue;
                // Quoted at the segment's own midpoint, the rule a bar outline
                // follows: one pixel at the box centre, thinner with distance.
                const double here = proj.box_units_per_pixel((a + b) * 0.5);
                Surface3DPolygon seg;
                seg.box          = { a, b };
                seg.xy           = { pa.x, pa.y, pb.x, pb.y };
                seg.filled       = false;
                seg.stroke       = edge;
                seg.stroke_width = here > 0.0
                    ? static_cast<float>(s.opts.edge_linewidth * ref / here)
                    : s.opts.edge_linewidth;
                seg.depth        = (pa.depth + pb.depth) * 0.5f;
                seg.plot_depth   = plot_depth;
                seg.plot         = si;
                seg.cell         = k;
                out.push_back(std::move(seg));
            }
        }
    }
    return out;
}

} // namespace sextant
