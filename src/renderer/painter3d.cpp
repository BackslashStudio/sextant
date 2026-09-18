#include "painter3d.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>

namespace sextant {
namespace {

// A length in box units below which two points are the same point. The box is
// the unit-ish cube every scene is mapped into (see spec_3d.md §2), so this is
// an absolute tolerance rather than a relative one on purpose: every scene
// arrives at the same scale here, which is the whole reason the chain
// normalizes before the camera sees anything.
constexpr double kEps = 1e-9;

// How far off a plane a vertex may be and still count as *on* it. Larger than
// kEps because it is compared against a dot product of box-space coordinates
// rather than against a coordinate, and because the alternative to being
// generous here is splitting a polygon into a sliver and its complement.
constexpr double kPlaneEps = 1e-7;

double signed_dist(Vec3 p, Vec3 p0, Vec3 n) { return dot(p - p0, n); }

Vec3 mix(Vec3 a, Vec3 b, double t) { return a + (b - a) * t; }

// The ring's area, from the same summed cross terms `ring_plane()` builds its
// normal out of -- the magnitude that formula throws away is twice the area of
// the polygon, planar or not.
//
// It exists to answer one question: did a cut actually cut anything. See the
// split site below.
double ring_area(const std::vector<Vec3>& ring) {
    if (ring.size() < 3) return 0.0;
    Vec3 sum{ 0.0, 0.0, 0.0 };
    const std::size_t m = ring.size();
    for (std::size_t i = 0; i < m; ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) % m];
        sum.x += (a.y - b.y) * (a.z + b.z);
        sum.y += (a.z - b.z) * (a.x + b.x);
        sum.z += (a.x - b.x) * (a.y + b.y);
    }
    return 0.5 * std::sqrt(dot(sum, sum));
}

} // namespace

bool ring_plane(const std::vector<Vec3>& ring, Vec3& p0, Vec3& n) {
    if (ring.size() < 3) return false;
    // Newell's normal: the summed cross terms over every edge. Stable where a
    // single cross product of two adjacent edges is not, which matters because
    // a surface cell at a fold and a bar face seen edge-on both produce very
    // nearly collinear neighbours.
    Vec3 sum{ 0.0, 0.0, 0.0 };
    Vec3 c{ 0.0, 0.0, 0.0 };
    const std::size_t m = ring.size();
    for (std::size_t i = 0; i < m; ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) % m];
        sum.x += (a.y - b.y) * (a.z + b.z);
        sum.y += (a.z - b.z) * (a.x + b.x);
        sum.z += (a.x - b.x) * (a.y + b.y);
        c = c + a;
    }
    const double len = std::sqrt(dot(sum, sum));
    if (len < kEps) return false;
    n = sum * (1.0 / len);
    p0 = c * (1.0 / static_cast<double>(m));

    // **And the ring has to actually lie on it.** Newell's normal is defined
    // for any closed ring, planar or not, and for a warped one it comes back a
    // plausible-looking average that contains none of the vertices. Every test
    // below then asks "which side of this plane is that on" about a plane that
    // is not a surface of anything, gets an answer that is true of neither
    // half of the polygon, and the pair never resolves -- so the algorithm
    // splits it, and splits the pieces, and does not stop. Three cells of
    // `test_translucent3d` were wrong for exactly this reason, and the
    // symptom at the top was 1,225 splits out of 145 polygons.
    //
    // So a polygon that is not planar is refused a plane. It can still be
    // *ordered* -- it just cannot be a blade, exactly like the two-point bar
    // edge. The tolerance is relative to the ring's own size because box
    // coordinates are order 1 and a cell of a fine grid is not.
    double extent = 0.0, dev = 0.0;
    for (const Vec3& v : ring) {
        extent = std::max(extent, length(v - p0));
        dev    = std::max(dev, std::fabs(dot(v - p0, n)));
    }
    if (extent > kEps && dev > 1e-3 * extent) return false;
    return true;
}

void split_ring_by_plane(const std::vector<Vec3>& ring, Vec3 p0, Vec3 n,
                         std::vector<Vec3>& front, std::vector<Vec3>& back) {
    front.clear();
    back.clear();
    const std::size_t m = ring.size();
    // A two-point ring is a stroke, and cutting one is cutting a line: each
    // side keeps its endpoint and the crossing. Not the loop below, which
    // walks a closed ring and would visit the one edge twice.
    if (m == 2) {
        const double da = signed_dist(ring[0], p0, n);
        const double db = signed_dist(ring[1], p0, n);
        if ((da > kPlaneEps && db < -kPlaneEps) || (da < -kPlaneEps && db > kPlaneEps)) {
            const Vec3 x = mix(ring[0], ring[1], da / (da - db));
            std::vector<Vec3>* a, * b;
            if (da > 0.0) {a = &front; b = &back;}
            else {b = &front; a = &back;}
            *a = std::vector<Vec3>{ring[0], x};
            *b = std::vector<Vec3>{x, ring[1]};
        } else {
            // Not straddling. Whichever side it is on gets all of it, the
            // polygon case's convention.
            if (da >= -kPlaneEps && db >= -kPlaneEps) front = ring;
            if (da <=  kPlaneEps && db <=  kPlaneEps) back  = ring;
        }
        return;
    }
    if (m < 3) return;

    auto clip = [&](double side, std::vector<Vec3>& out) {
        for (std::size_t i = 0; i < m; ++i) {
            const Vec3& a = ring[i];
            const Vec3& b = ring[(i + 1) % m];
            const double da = signed_dist(a, p0, n) * side;
            const double db = signed_dist(b, p0, n) * side;
            const bool ina = da >= -kPlaneEps;
            const bool inb = db >= -kPlaneEps;
            if (ina) out.push_back(a);
            // Only a genuine sign change makes a crossing point. A vertex
            // sitting on the plane is already in `out` from the branch above,
            // and emitting it twice would leave a zero-length edge that the
            // area test below then has to reason about.
            if (ina != inb && std::fabs(da - db) > kEps)
                out.push_back(mix(a, b, da / (da - db)));
        }
        if (out.size() < 3) out.clear();
    };
    clip(+1.0, front);
    clip(-1.0, back);
}

void prepare_paint_poly(PaintPoly& p, const Projector3D& proj) {
    p.px.clear();
    p.dmin = std::numeric_limits<float>::max();
    p.dmax = -std::numeric_limits<float>::max();
    p.bb[0] = p.bb[1] = std::numeric_limits<float>::max();
    p.bb[2] = p.bb[3] = -std::numeric_limits<float>::max();
    // Two points is a legal ring here: a *stroke*, the bare box edge a
    // translucent bar emits and one edge of a surface's wireframe. It has no
    // plane, so it can never be the blade -- but it is ordered against every
    // polygon like any other primitive, and cut by one where it passes
    // through it.
    //
    // It used to be carried by its depth extent alone, which was harmless
    // while the only strokes were a translucent bar's own edges and stopped
    // being so once a surface's wireframe could run straight through an
    // opaque bar: the depth sort draws a face whose far corner is deeper than
    // the line *before* the line, and the line then shows through the face.
    //
    // One point is a legal ring too: a scatter3d marker (v1.0 step 12.5). It
    // is a symbol rather than geometry -- it has no plane and no extent in the
    // scene -- so it is never a blade and never a victim, and what it covers
    // on screen is a disc of `radius` pixels about where the point landed.
    // Behind a perspective eye it is simply not drawn, which is what the empty
    // `px` says.
    if (p.ring.size() == 1) {
        if (!proj.in_front(p.ring[0])) return;
        const Px3 q = proj.project_box(p.ring[0]);
        p.px = { q.x, q.y };
        p.dmin = p.dmax = q.depth;
        p.bb[0] = q.x - p.radius;  p.bb[1] = q.y - p.radius;
        p.bb[2] = q.x + p.radius;  p.bb[3] = q.y + p.radius;
        return;
    }

    if (p.ring.size() < 2) return;

    std::vector<Px3> proj_ring;
    if (p.ring.size() == 2) {
        Px3 a, b;
        if (!proj.project_segment(p.ring[0], p.ring[1], a, b)) return;
        proj_ring = { a, b };
    } else {
        proj.project_polygon(p.ring, proj_ring);
        if (proj_ring.size() < 3) { p.px.clear(); return; }
    }

    p.px.reserve(proj_ring.size() * 2);
    for (const Px3& q : proj_ring) {
        p.px.push_back(q.x);
        p.px.push_back(q.y);
        p.dmin = std::min(p.dmin, q.depth);
        p.dmax = std::max(p.dmax, q.depth);
        p.bb[0] = std::min(p.bb[0], q.x);
        p.bb[1] = std::min(p.bb[1], q.y);
        p.bb[2] = std::max(p.bb[2], q.x);
        p.bb[3] = std::max(p.bb[3], q.y);
    }
}

namespace {

bool bb_disjoint(const PaintPoly& a, const PaintPoly& b) {
    // A shared edge is not an overlap. Half a pixel of slack, because two
    // polygons that merely touch on screen have nothing to resolve and
    // resolving them anyway is how a scene acquires splits it does not need.
    constexpr float kSlack = 0.5f;
    return a.bb[2] < b.bb[0] + kSlack || b.bb[2] < a.bb[0] + kSlack
        || a.bb[3] < b.bb[1] + kSlack || b.bb[3] < a.bb[1] + kSlack;
}

// True when every vertex of `poly` lies on the far side of the plane through
// `p0` with normal `n`, where `n` has already been turned to face the eye. The
// question Newell's third and fourth tests both ask, from opposite ends.
bool wholly_behind(const std::vector<Vec3>& ring, Vec3 p0, Vec3 n) {
    for (const Vec3& v : ring)
        if (signed_dist(v, p0, n) > kPlaneEps) return false;
    return true;
}

bool wholly_in_front(const std::vector<Vec3>& ring, Vec3 p0, Vec3 n) {
    for (const Vec3& v : ring)
        if (signed_dist(v, p0, n) < -kPlaneEps) return false;
    return true;
}

// Separating-axis test on two projected rings. Both are convex, so a single
// axis on which their shadows do not meet proves they do not overlap; the
// candidate axes are the edge normals of both.
//
// Returns true when the projections *do* overlap, which is the last of
// Newell's tests to fail and the one that commits the pair to a split.
bool projections_overlap(const std::vector<float>& a, const std::vector<float>& b) {
    // A shared edge is not an overlap, and this is how much of one is allowed
    // to be. It is a *tolerance*, not a derived quantity, and it is squeezed
    // from both sides by two scenes that must both come out right:
    //
    //  - Too small and geometry that merely passes near itself starts
    //    conflicting. A smooth 50x50 sheet on its own occludes itself nowhere
    //    and must never split; it picks up cuts at 0.10 and below.
    //  - Too large and a genuine overlap narrower than the slack is called
    //    disjoint, the pair is never ordered, and the emission is wrong there.
    //    The gallery's sixth cell has two such pairs at 0.25, overlapping by
    //    about a fifth of a pixel each.
    //
    // 0.15 is the window between them, and both bounds are defended by tests:
    // `test_scene3d_svg_order()`'s lone-sheet control for the lower one, and
    // its four interleaving cells -- judged pixel by pixel against a ray cast,
    // at zero tolerance -- for the upper.
    //
    // A stroke (two points, four floats) takes part against a polygon: the
    // separating axes are then the polygon's edge normals and the line's own,
    // which is still complete for a segment against a convex polygon. Two
    // strokes never do. Their axes would need the lines' own directions as
    // well, and there is nothing to gain -- two crossing lines have no area
    // to occlude with, and neither could be cut by the other.
    constexpr float kSlack = 0.15f;
    auto axes_separate = [&](const std::vector<float>& src,
                             const std::vector<float>& other) {
        const std::size_t n = src.size() / 2;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t j = (i + 1) % n;
            const float ex = src[j * 2] - src[i * 2];
            const float ey = src[j * 2 + 1] - src[i * 2 + 1];
            const float len = std::sqrt(ex * ex + ey * ey);
            if (len < 1e-6f) continue;
            const float nx = -ey / len, ny = ex / len;
            float a0 = std::numeric_limits<float>::max(), a1 = -a0;
            for (std::size_t k = 0; k < n; ++k) {
                const float d = nx * src[k * 2] + ny * src[k * 2 + 1];
                a0 = std::min(a0, d); a1 = std::max(a1, d);
            }
            float b0 = std::numeric_limits<float>::max(), b1 = -b0;
            for (std::size_t k = 0; k * 2 + 1 < other.size(); ++k) {
                const float d = nx * other[k * 2] + ny * other[k * 2 + 1];
                b0 = std::min(b0, d); b1 = std::max(b1, d);
            }
            if (a1 < b0 + kSlack || b1 < a0 + kSlack) return true;
        }
        return false;
    };
    if (a.size() < 4 || b.size() < 4) return false;
    if (a.size() < 6 && b.size() < 6) return false;
    return !axes_separate(a, b) && !axes_separate(b, a);
}

// Where a marker is, relative to a polygon: +1 in front of it, -1 behind it,
// 2 when the two do not overlap on screen after all. Unlike every test above
// it there is no third answer -- a point cannot pass *through* a polygon --
// which is what makes this the one rung of the ladder that can never end in a
// split (v1.0 step 12.5).
//
// It is also the cheapest and the most exact: a marker is a symbol drawn at
// one projected pixel, so "is it in front" is decided by comparing its own
// depth with the polygon's depth along that one ray. No separating axis, no
// clipping, no tolerance beyond the plane epsilon.
//
// **What it approximates, deliberately, is the disc and not the point.** The
// marker is drawn whole at a size in pixels, so a polygon crossing its
// footprint puts the whole symbol on one side of itself -- and which side can
// change as the camera turns. Clipping a symbol to geometry is not
// better-defined, and the raster path does exactly the same thing for nothing
// (one depth for the whole sprite), so the two outputs agree about it.
int point_vs_polygon(const PaintPoly& pt, const PaintPoly& poly, const Projector3D& proj) {
    // Inside the polygon's *projection*, by the same winding-aware half-plane
    // walk stroke_vs_polygon() clips with -- a convex ring, so a point outside
    // any edge is outside.
    const std::vector<float>& q = poly.px;
    const std::size_t m = q.size() / 2;
    if (m < 3) return 2;
    float area = 0.0f;
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t j = (i + 1) % m;
        area += q[i * 2] * q[j * 2 + 1] - q[j * 2] * q[i * 2 + 1];
    }
    const float wind = area >= 0.0f ? 1.0f : -1.0f;
    const float px = pt.px[0], py = pt.px[1];
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t j = (i + 1) % m;
        const float ex = q[j * 2] - q[i * 2], ey = q[j * 2 + 1] - q[i * 2 + 1];
        if (ex * ex + ey * ey < 1e-12f) continue;
        const float nx = -ey * wind, ny = ex * wind;
        if (nx * (px - q[i * 2]) + ny * (py - q[i * 2 + 1]) < 0.0f) return 2;
    }

    // Inside. Which side of the polygon's own plane the point is on then
    // settles it, with the normal turned to face the eye so that positive is
    // "toward the camera" -- the convention tests 3 and 4 use.
    Vec3 p0, n;
    if (!ring_plane(poly.ring, p0, n)) return 2;
    if (!proj.faces_camera(p0, n)) n = n * -1.0;
    const double d = signed_dist(pt.ring[0], p0, n);
    // A marker lying *on* a surface is drawn after it, which is what puts a
    // point plotted on a sheet on top of the sheet rather than inside it --
    // the same rule stroke_vs_polygon() applies to a wireframe edge.
    return d >= -kPlaneEps ? +1 : -1;
}

// Where a stroke is, relative to a polygon it overlaps on screen: +1 in front
// of it, -1 behind it, 0 passing *through* it inside the overlap -- the one
// case only a cut can order -- and 2 when the two do not overlap after all.
//
// Newell's tests 3 and 4 each need one of the pair to have a plane, and a line
// has none, so against a line only one direction of the ladder exists. That
// leaves every line that straddles a polygon's *infinite* plane unresolved --
// a bar's edge beside the next bar's face, which never touches it -- and
// every one of them was cut for nothing. This is the missing rung, and it is
// exact rather than conservative: the line crosses the plane at one point X,
// so over any stretch of the line that does not contain X it is on one side.
// The stretch that matters is the part of its projection inside the polygon's
// projection. Clip for it; if X projects outside, the pair has an order.
//
// A line lying *on* the plane counts as in front, so it is drawn after the
// polygon it lies on -- the raster path's polygon offset, and the reason a
// wireframe edge is not buried under the neighbouring cell.
int stroke_vs_polygon(const PaintPoly& line, const PaintPoly& poly, const Projector3D& proj) {
    Vec3 p0, n;
    if (!ring_plane(poly.ring, p0, n)) return 0;
    if (!proj.faces_camera(p0, n)) n = n * -1.0;
    const Vec3& a = line.ring[0];
    const Vec3& b = line.ring[1];
    const double da = signed_dist(a, p0, n), db = signed_dist(b, p0, n);
    if (da >= -kPlaneEps && db >= -kPlaneEps) return +1;
    if (da <=  kPlaneEps && db <=  kPlaneEps) return -1;

    // The crossing, in pixels, as a parameter along the projected line.
    // `px` runs ring[0] -> ring[1] even when a near plane clipped one end off,
    // since the clip keeps the direction; a crossing behind that plane is not
    // on the drawn line at all, and is left to the cut.
    const Vec3 x = mix(a, b, da / (da - db));
    Px3 xa, xb;
    if (!proj.project_segment(x, x, xa, xb)) return 0;
    const float lx = line.px[0], ly = line.px[1];
    const float dx = line.px[2] - lx, dy = line.px[3] - ly;
    const float len2 = dx * dx + dy * dy;
    if (len2 < 1e-12f) return 0;
    const float sx = ((xa.x - lx) * dx + (xa.y - ly) * dy) / len2;

    // Cyrus-Beck: the parameter interval of the line inside the polygon.
    const std::vector<float>& q = poly.px;
    const std::size_t m = q.size() / 2;
    float area = 0.0f;
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t j = (i + 1) % m;
        area += q[i * 2] * q[j * 2 + 1] - q[j * 2] * q[i * 2 + 1];
    }
    const float wind = area >= 0.0f ? 1.0f : -1.0f;
    float s0 = 0.0f, s1 = 1.0f;
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t j = (i + 1) % m;
        const float ex = q[j * 2] - q[i * 2], ey = q[j * 2 + 1] - q[i * 2 + 1];
        if (ex * ex + ey * ey < 1e-12f) continue;
        // Inward normal of this edge.
        const float nx = -ey * wind, ny = ex * wind;
        const float f0 = nx * (lx - q[i * 2]) + ny * (ly - q[i * 2 + 1]);
        const float fd = nx * dx + ny * dy;
        if (std::fabs(fd) < 1e-12f) {
            if (f0 < 0.0f) return 2;
            continue;
        }
        const float t = -f0 / fd;
        if (fd > 0.0f) s0 = std::max(s0, t); else s1 = std::min(s1, t);
        if (s0 >= s1) return 2;
    }
    if (sx <= s0) return db > 0.0 ? +1 : -1;   // the overlap is on ring[1]'s side of X
    if (sx >= s1) return da > 0.0 ? +1 : -1;   // ...or on ring[0]'s
    return 0;
}

} // namespace

std::vector<PaintPoly> paint_order(std::vector<PaintPoly> polys,
                                   const Projector3D& proj,
                                   PaintOrderStats* stats,
                                   std::size_t max_work,
                                   std::size_t max_splits) {
    PaintOrderStats st;
    st.input = polys.size();
    // The bound exists so that a scene nobody anticipated produces a slightly
    // wrong picture instead of an export that never returns. Chosen against
    // the largest thing this library draws -- a 200x200 surface is 39,601
    // cells -- so that a scene of that size still gets a few dozen tests per
    // polygon before the run gives up.
    // Sized against the scenes this library actually draws rather than
    // guessed. `test_translucent3d`'s bar-grid-plus-surface cell -- 1,014
    // translucent bar faces and 288 surface triangles -- settles at ~894,000
    // tests and 2,015 splits, and it was silently *bailing* at the 400,000
    // this used to be, which is how a cell nobody had measured came out wrong.
    // Five million leaves that scene a factor of five of headroom and still
    // bounds the pathological case at a few seconds.
    if (max_work == 0) max_work = 20000000;
    // A separate cap on splits, because they are the expensive unit: each one
    // grows the list, re-sorts its tail and re-emits a payload. Proportional
    // to the input rather than absolute -- a scene of a thousand polygons may
    // legitimately need more cuts than a scene of ten.
    //
    // **It is the bound that actually binds**, and the default is not generous
    // for every scene: `test_translucent3d`'s sixth cell -- a sheet threaded
    // through 144 translucent bars, so it runs *inside* every one of them --
    // reaches it at 10,481 cuts having used only 1.0M of its 20M test budget.
    // That is why it is a caller-settable option (SvgExportOptions::max_splits)
    // rather than a constant: the scene that needs more is the caller's, and
    // the report names the number it stopped at so the new one can be chosen
    // against a real figure.
    if (max_splits == 0) max_splits = 8 * polys.size() + 64;

    // Drop what cannot be drawn: degenerate rings, and anything a perspective
    // near plane clipped away to nothing.
    std::vector<PaintPoly> list;
    list.reserve(polys.size());
    for (PaintPoly& p : polys) {
        prepare_paint_poly(p, proj);
        // Two floats is a marker: one projected point, which is the whole of
        // what a symbol has. Four was the floor while every primitive here
        // had at least two corners.
        if (p.px.size() >= 2) list.push_back(std::move(p));
    }

    // Newell's first step, and the only sort in the algorithm: farthest first,
    // by the polygon's own farthest point. It is a *starting guess* rather than
    // an answer -- everything below exists because it is sometimes wrong.
    //
    // Stable, and that is load-bearing: within one object the sort must not
    // rearrange anything, because the object's own order is already exact and
    // this key is not. Ties therefore keep the order the caller merged them
    // in, which is each object's own sequence.
    // **A permutation is sorted here, not the polygons.** A `PaintPoly` carries
    // three vectors and costs about a hundred bytes to move, and a comparison
    // sort moves every element O(log n) times. That is affordable once; the
    // split path below re-sorts the whole tail *per split*, which on the
    // gallery's densest cell is two thousand times over three thousand
    // polygons. Sorting an eight-byte key per polygon and applying the
    // permutation once afterwards costs two moves per element instead.
    //
    // The order is *identical* rather than merely equivalent, which is the
    // only reason this is allowed to be a performance change: the permutation
    // starts in list order, so breaking ties on the index is precisely what
    // `stable_sort` guaranteed. It also makes the comparator a strict total
    // order, so plain `sort` -- which allocates no merge buffer -- suffices.
    // The key travels *with* the index rather than being fetched through it.
    // An index-only sort still reads `list[i].dmax` on every comparison, which
    // is a random access into a few hundred kilobytes of polygon and gives
    // back most of what sorting indices saved; eight bytes of (depth, index)
    // laid out contiguously is what the comparisons actually want.
    struct DepthKey { float dmax; std::uint32_t idx; };
    std::vector<DepthKey>  keys;
    std::vector<PaintPoly> perm_buf;
    auto sort_by_depth = [&](std::size_t from) {
        const std::size_t m = list.size() - from;
        if (m < 2) return;
        keys.resize(m);
        for (std::size_t i = 0; i < m; ++i)
            keys[i] = { list[from + i].dmax, static_cast<std::uint32_t>(i) };
        std::sort(keys.begin(), keys.end(), [](const DepthKey& a, const DepthKey& b) {
            return a.dmax != b.dmax ? a.dmax > b.dmax : a.idx < b.idx;
        });
        // Every element of the range is moved out exactly once, so each slot
        // is already empty when the second pass moves the new occupant in and
        // nothing is freed on the way. Both scratch buffers are reused across
        // the thousands of calls the split path makes.
        perm_buf.clear();
        perm_buf.reserve(m);
        for (std::size_t i = 0; i < m; ++i)
            perm_buf.push_back(std::move(list[from + keys[i].idx]));
        for (std::size_t i = 0; i < m; ++i)
            list[from + i] = std::move(perm_buf[i]);
    };
    sort_by_depth(0);

    std::vector<PaintPoly> out;
    out.reserve(list.size());

    // Which entries have already been moved to the front once. A polygon that
    // conflicts after having been promoted is in a cycle, and a cycle is what
    // splitting exists for.
    std::vector<char> promoted(list.size(), 0);

    // An upper bound on `dmax` over each suffix of the list, which is what
    // makes Newell's early stop sound once promotions have disturbed the sort.
    //
    // The stop wants "no polygon after this one reaches P's nearest point",
    // and the depth sort is what normally proves it. A promotion lifts one
    // polygon out of the tail and puts it at the head, so after the second one
    // the tail is no longer sorted and the stop starts skipping pairs that
    // really do overlap -- invisible in a small scene, and it left 25 pixels
    // of `test_translucent3d`'s plane-and-surface cell on the wrong side of
    // the plane. Scanning the whole tail instead is correct and far too slow:
    // the two-bar-grid cell did not finish an export in ninety seconds.
    //
    // This is the third answer. It is only ever an over-estimate -- a
    // promotion *removes* an element from the tail, which can only lower the
    // true maximum -- so stopping on it is always safe, and it stays tight
    // enough to keep the scan short.
    std::vector<float> smax(list.size(), 0.0f);
    auto rebuild_smax = [&](std::size_t from) {
        smax.resize(list.size());
        for (std::size_t i = list.size(); i-- > from;)
            smax[i] = (i + 1 < list.size()) ? std::max(list[i].dmax, smax[i + 1])
                                            : list[i].dmax;
    };
    rebuild_smax(0);

    // ---- The screen-space bucket grid ------------------------------------
    //
    // Nearly every pair the scan looks at dies at the bounding-box test, and
    // finding them is the whole cost: the tail is scanned per emission, so the
    // work is quadratic in a scene where everything overlaps in depth.
    // `test_translucent3d`'s sheet-through-a-bar-grid cell reached sixteen
    // million tests and its work bound. A uniform grid over the plot rect
    // never generates those pairs at all -- only polygons sharing a cell with
    // the head can possibly overlap it.
    //
    // Keyed on `PaintPoly::id` and not on list positions, because a promotion
    // rotates a span of the list and a split re-sorts its tail; `pos_of` is the
    // one place that mapping is maintained, and it is updated at exactly those
    // two sites.
    //
    // **The grid is append-only, and that is what keeps it cheap.** A split
    // makes two pieces inside the parent's box, so the parent's entries stay a
    // valid superset for the piece that keeps its id and only the new piece
    // needs inserting. Nothing is ever removed for being wrong -- entries are
    // dropped lazily, while a bucket is being read, once their polygon is
    // behind `head` and therefore emitted for good.
    for (std::size_t i = 0; i < list.size(); ++i)
        list[i].id = static_cast<std::uint32_t>(i);
    std::vector<std::size_t> pos_of(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) pos_of[list[i].id] = i;
    std::uint32_t next_id = static_cast<std::uint32_t>(list.size());

    float gx0 = std::numeric_limits<float>::max(), gy0 = gx0;
    float gx1 = -gx0, gy1 = -gx0;
    for (const PaintPoly& p : list) {
        gx0 = std::min(gx0, p.bb[0]); gy0 = std::min(gy0, p.bb[1]);
        gx1 = std::max(gx1, p.bb[2]); gy1 = std::max(gy1, p.bb[3]);
    }
    // About two polygons per cell if they were spread evenly, capped so a
    // scene of a few polygons does not allocate a grid, and so a huge one does
    // not spend more on cells than on tests.
    const int grid = std::clamp(
        static_cast<int>(std::lround(std::sqrt(static_cast<double>(list.size()) / 2.0))),
        1, 96);
    const float cw = std::max(1e-3f, (gx1 - gx0) / static_cast<float>(grid));
    const float chh = std::max(1e-3f, (gy1 - gy0) / static_cast<float>(grid));
    std::vector<std::vector<std::uint32_t>> cells(
        static_cast<std::size_t>(grid) * static_cast<std::size_t>(grid));
    // A polygon covering more cells than this is not worth bucketing -- a
    // plane's quad spans the whole box face and would fill the grid on its
    // own. Those go in one list that every query reads, which is right
    // because a query against one of them has to look at everything anyway.
    constexpr int kMaxCells = 48;
    std::vector<std::uint32_t> broad;

    auto cell_span = [&](const PaintPoly& p, int& i0, int& j0, int& i1, int& j1) {
        i0 = std::clamp(static_cast<int>((p.bb[0] - gx0) / cw), 0, grid - 1);
        i1 = std::clamp(static_cast<int>((p.bb[2] - gx0) / cw), 0, grid - 1);
        j0 = std::clamp(static_cast<int>((p.bb[1] - gy0) / chh), 0, grid - 1);
        j1 = std::clamp(static_cast<int>((p.bb[3] - gy0) / chh), 0, grid - 1);
        return (i1 - i0 + 1) * (j1 - j0 + 1);
    };
    auto bucket_insert = [&](const PaintPoly& p) {
        int i0, j0, i1, j1;
        if (cell_span(p, i0, j0, i1, j1) > kMaxCells) { broad.push_back(p.id); return; }
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
                cells[static_cast<std::size_t>(j) * grid + i].push_back(p.id);
    };
    for (const PaintPoly& p : list) bucket_insert(p);

    std::size_t head = 0;
    std::size_t work = 0;
    std::vector<Vec3> piece_front, piece_back;

    // Candidate gathering. `seen` de-duplicates a polygon that shares several
    // cells with the head, stamped by a generation counter so it costs no
    // clearing.
    std::vector<std::uint32_t> seen(next_id, 0);
    std::uint32_t gen = 0;
    std::vector<std::size_t> cand;
    auto gather = [&](std::vector<std::size_t>& out) {
        out.clear();
        ++gen;
        if (seen.size() < next_id) seen.resize(next_id, 0);
        auto take = [&](std::vector<std::uint32_t>& bucket) {
            for (std::size_t k = 0; k < bucket.size();) {
                const std::uint32_t id = bucket[k];
                const std::size_t pos = pos_of[id];
                // Strictly *behind* head, never equal to it: a polygon sitting
                // at the head has not been emitted and can be pushed back down
                // by the next promotion, and dropping it here would make it
                // invisible to every later query.
                if (pos < head) {
                    bucket[k] = bucket.back();
                    bucket.pop_back();
                    continue;
                }
                if (pos > head && seen[id] != gen) { seen[id] = gen; out.push_back(pos); }
                ++k;
            }
        };
        int i0, j0, i1, j1;
        if (cell_span(list[head], i0, j0, i1, j1) > kMaxCells) {
            for (std::size_t q = head + 1; q < list.size(); ++q) out.push_back(q);
            return;
        }
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
                take(cells[static_cast<std::size_t>(j) * grid + i]);
        take(broad);
        // Position order, so the run is deterministic and one export is the
        // same file as the last -- and so the depth bound below can still stop
        // the scan, since it is non-increasing in position.
        std::sort(out.begin(), out.end());
    };

    while (head < list.size()) {
        bool restart = false;
        gather(cand);

        for (const std::size_t q : cand) {
            const PaintPoly& P = list[head];
            const PaintPoly& Q = list[q];

            // Test 1: the depth extents do not overlap, so the pair is settled
            // whichever way round it is.
            //
            // **`continue`, not `break`, and that distinction cost 25 pixels.**
            // Newell's first step sorts by farthest point precisely so that
            // this test can stop the scan: once Q's farthest point is nearer
            // than P's nearest, every later Q is too. But a *promotion* lifts
            // one polygon out of the tail and puts it at the head, and the
            // second promotion therefore leaves a polygon sitting in the tail
            // out of order with everything after it. The sorted invariant the
            // early stop rests on is gone by then, and stopping skips pairs
            // that genuinely overlap -- which is invisible in a small scene
            // and produced exactly the residue this test was catching in a
            // dense one. Scanning the whole tail costs one float comparison
            // per rejected pair and is unconditionally right.
            if (smax[q] <= P.dmin) break;
            if (Q.dmax <= P.dmin) continue;

            ++st.tests;
            if (++work > max_work) { st.bailed = true; break; }   // work, not splits

            // Test 2: screen bounding boxes are disjoint.
            if (bb_disjoint(P, Q)) continue;

            // Tests 3 and 4, each asked with the other's plane turned to face
            // the eye: P entirely behind Q's plane, or Q entirely in front of
            // P's. Either settles the pair without touching the projections.
            Vec3 qp0, qn;
            if (ring_plane(Q.ring, qp0, qn)) {
                if (!proj.faces_camera(qp0, qn)) qn = qn * -1.0;
                if (wholly_behind(P.ring, qp0, qn)) continue;
            }
            Vec3 pp0, pn;
            const bool p_has_plane = ring_plane(P.ring, pp0, pn);
            if (p_has_plane) {
                if (!proj.faces_camera(pp0, pn)) pn = pn * -1.0;
                if (wholly_in_front(Q.ring, pp0, pn)) continue;
            }

            // Test 5a, for a marker: exact, and *before* the separating-axis
            // test, which a one-point projection cannot take part in -- it has
            // no edges to raise an axis from, so projections_overlap() would
            // call every marker disjoint from everything and leave the depth
            // sort to answer alone. See point_vs_polygon().
            //
            // Two markers, or a marker and a stroke, fall through to `continue`
            // and keep the sort's answer, which for a pair of points is not an
            // approximation: a marker's dmin and dmax are one number, so
            // sorting on it *is* ordering them, and two flat symbols cannot
            // interpenetrate to make that wrong.
            const bool marker_pair = P.ring.size() == 1 || Q.ring.size() == 1;
            {
                const bool p_pt = P.ring.size() == 1, q_pt = Q.ring.size() == 1;
                if (p_pt || q_pt) {
                    const bool p_poly = P.ring.size() >= 3, q_poly = Q.ring.size() >= 3;
                    if (p_pt && q_poly) {
                        const int side = point_vs_polygon(P, Q, proj);
                        if (side == 2 || side < 0) continue;
                    } else if (q_pt && p_poly) {
                        const int side = point_vs_polygon(Q, P, proj);
                        if (side == 2 || side > 0) continue;
                    } else {
                        continue;
                    }
                    // The marker is in front of the polygon that is currently
                    // ahead of it, so the pair really is out of order and the
                    // promotion below is the whole resolution: nothing here
                    // can ever be cut.
                }
            }

            // Test 5: the projections themselves. Everything above is a cheap
            // rejection; this is the one that decides.
            //
            // Skipped for a marker, which has already been decided exactly
            // above: a one-point projection raises no separating axis, so this
            // would call it disjoint and undo the answer.
            if (!marker_pair && !projections_overlap(P.px, Q.px)) continue;

            // Test 6, for a stroke against a polygon only: the exact answer
            // tests 3 and 4 cannot give when one of the pair has no plane.
            // See stroke_vs_polygon(). P may go first if P is a line behind
            // Q, or Q is a line in front of P; anything else falls through to
            // the promotion, and a line through the polygon on to the cut.
            if (!marker_pair) {
                const bool p_line = P.ring.size() == 2, q_line = Q.ring.size() == 2;
                if (p_line != q_line) {
                    const int side = p_line ? stroke_vs_polygon(P, Q, proj)
                                            : stroke_vs_polygon(Q, P, proj);
                    if (side == 2) continue;
                    if ((p_line && side < 0) || (q_line && side > 0)) continue;
                }
            }

            // The pair is genuinely unresolved. First time: assume the sort
            // simply had them the wrong way round and try Q as the back-most.
            if (!promoted[q]) {
                promoted[q] = 1;
                const std::size_t j = q;
                if (j > head) {
                    // `promoted` is indexed by *position*, so it has to move
                    // with the list. Rotating one without the other leaves
                    // every mark attached to the wrong polygon, and the
                    // consequence is not a wrong picture but a hang: the pair
                    // that conflicts keeps finding itself unmarked, promotes
                    // each other in turn and never reaches the split that
                    // would settle it.
                    std::rotate(list.begin() + static_cast<std::ptrdiff_t>(head),
                                list.begin() + static_cast<std::ptrdiff_t>(j),
                                list.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                    std::rotate(promoted.begin() + static_cast<std::ptrdiff_t>(head),
                                promoted.begin() + static_cast<std::ptrdiff_t>(j),
                                promoted.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                    // The rotated span now holds a permutation of what used to
                    // start at `head`, so the bound that was true there is
                    // true of all of it. Flattening it keeps `smax` an
                    // over-estimate, which is all the stop needs.
                    std::fill(smax.begin() + static_cast<std::ptrdiff_t>(head),
                              smax.begin() + static_cast<std::ptrdiff_t>(j) + 1,
                              smax[head]);
                    for (std::size_t i = head; i <= j; ++i) pos_of[list[i].id] = i;
                }
                restart = true;
                break;
            }

            // Q has been to the front already, so P and Q are in a cycle and
            // no ordering of them as whole polygons is right. Cut P by Q's
            // plane: the two pieces lie on opposite sides of it, so each is
            // separable from Q by test 3, and the relation becomes an order.
            ++st.cycles;

            // **Which of the two to cut is free, and is chosen on cost.**
            // Newell cuts P by Q's plane; cutting Q by P's plane resolves the
            // pair just as completely, because either way each piece ends up
            // wholly on one side of the other's plane and test 3 or 4 then
            // separates it. So the choice is a cost decision, and the costs
            // are wildly unequal: a piece re-emits its payload, and a plane's
            // payload is a whole raster image while a bar face's is nine
            // numbers. Cutting the plane where the bar would do multiplies a
            // heatmap by the number of pieces.
            // **The preference is about cost, and it has to stay a
            // preference.** Cutting the cheap one first is right; refusing
            // ever to cut the expensive one is not. A polygon can only be cut
            // by a plane it actually straddles, so when the preferred victim
            // does not straddle the other's plane -- which happens inside a
            // cycle of three, where the pair that conflicts is not the pair
            // that crosses -- the only way to resolve the pair is to cut the
            // other one instead. Refusing left 25 pixels of
            // `test_translucent3d`'s plane-and-surface cell on the wrong side
            // of the plane, by up to 0.27 box units.
            const bool cut_q = (P.kind == PaintPoly::Kind::Plane
                                && Q.kind != PaintPoly::Kind::Plane);
            std::size_t victim = q;
            bool cut = false;
            for (int attempt = 0; attempt < 2 && !cut; ++attempt) {
                const bool q_victim = (attempt == 0) ? cut_q : !cut_q;
                victim = q_victim ? q : head;
                const std::size_t blade = q_victim ? head : q;
                Vec3 sp0, sn;
                if (!ring_plane(list[blade].ring, sp0, sn)) continue;
                const std::vector<Vec3>& vring = list[victim].ring;
                const bool stroke = vring.size() == 2;
                // **Only a flat polygon or a line may be cut.** Clipping
                // assumes the victim lies on one plane; a warped ring -- which
                // is what ring_plane() refuses -- that has three corners on the
                // blade comes back as *itself* on one side and a copy of those
                // three corners on the other, and both pieces have area. That
                // is not a cut, it is a duplication, and it repeats on every
                // pass: a surface's closed wireframe ring did exactly this
                // until every export that had one ran to its split bound. The
                // plan no longer emits such rings; this makes the next one a
                // pair left unresolved rather than a loop.
                if (!stroke) {
                    Vec3 vp0, vn;
                    if (!ring_plane(vring, vp0, vn)) continue;
                }
                split_ring_by_plane(vring, sp0, sn, piece_front, piece_back);
                const std::size_t min_pts = stroke ? 2 : 3;
                if (piece_front.size() < min_pts || piece_back.size() < min_pts) continue;
                // **A cut that does not divide anything is not a cut**, and
                // counting one as a cut is how this loop used to fail to
                // terminate. `split_ring_by_plane()` counts a vertex within
                // kPlaneEps of the blade as being on *both* sides, which is
                // right -- it is what stops a polygon that merely touches the
                // plane from being shaved -- but it means a victim whose edge
                // lies *on* the blade comes back as itself plus a three-point
                // ring of zero area. The vertex count cannot tell that from a
                // real cut.
                //
                // It is not a corner case. Splitting a surface produces pieces
                // that are coplanar with their parent, so a bar face already
                // cut by one surface triangle has an edge lying exactly on the
                // plane of every piece of that triangle -- and each of them in
                // turn "cuts" it into itself and a null. The gallery's sixth
                // cell reached 44,850 splits that way, 44,653 of them on a
                // single bar face, of which 2,801 out of 2,803 pieces had zero
                // area: the face was genuinely halved once and then shaved
                // 2,800 times without changing.
                //
                // Relative to the victim rather than absolute, because a cell
                // of a fine grid is small in box units and a fixed floor would
                // refuse to cut it at all. A piece a millionth of its parent
                // is far below a pixel at any figure size this library draws.
                //
                // A stroke is held to the same rule in the one measure a line
                // has: its length.
                constexpr double kMinPieceFrac = 1e-6;
                auto measure = [stroke](const std::vector<Vec3>& r) {
                    return stroke ? length(r[1] - r[0]) : ring_area(r);
                };
                if (std::min(measure(piece_front), measure(piece_back))
                    <= kMinPieceFrac * measure(vring)) continue;
                cut = true;
            }
            if (!cut) {
                // Neither straddles the other -- or straddles it only by a
                // sliver -- so there is nothing to cut and no order to find:
                // the two are coplanar, touching, or crossing over a region
                // too small to be a pixel, and either sequence draws the same
                // picture. Counted rather than passed over in silence -- a
                // scene that reaches this often is a scene this algorithm is
                // not answering.
                ++st.unresolved;
                promoted[q] = 0;
                continue;
            }
            ++st.splits;
            if (st.splits > max_splits) { st.bailed = st.bailed_on_splits = true; break; }
            if (++work > max_work)      { st.bailed = true; break; }

            PaintPoly a = list[victim];
            PaintPoly b = a;
            a.ring = piece_front;
            b.ring = piece_back;
            a.split = b.split = true;
            // `a` keeps the parent's name, so the parent's bucket entries go on
            // covering it -- they describe a box that contains it. `b` is new
            // and has to be put in the grid itself.
            b.id = next_id++;
            prepare_paint_poly(a, proj);
            prepare_paint_poly(b, proj);
            if (seen.size() < next_id) seen.resize(next_id, 0);
            pos_of.resize(next_id, 0);
            bucket_insert(b);
            list[victim] = std::move(a);
            list.insert(list.begin() + static_cast<std::ptrdiff_t>(victim) + 1,
                        std::move(b));
            promoted.insert(promoted.begin() + static_cast<std::ptrdiff_t>(victim) + 1, 0);
            // The two pieces are somewhere else in the depth order now, and
            // every promotion mark was made about a list that no longer
            // exists.
            //
            // **This sort is the cost of the whole algorithm, and it is not
            // optional.** Newell needs no particular order to be *correct* --
            // every pair is still tested, and each of the five tests settles a
            // pair whichever way round it is -- so dropping it looks free, and
            // on the gallery's densest figure it takes an export from 125 s to
            // 27 s. It also puts one or two pixels of two different cells out
            // of order: without the depth order to work from, the splits fall
            // in a different sequence and produce slivers thin enough for the
            // quarter-pixel slack in the projection-overlap test to call two
            // polygons disjoint when they are not. Speed bought with pixels is
            // not a trade this step is allowed to make.
            //
            // So it stays, and a permutation is what it costs instead: this
            // sorts indices and moves each polygon exactly twice, rather than
            // moving a hundred-byte object O(n log n) times per split. Same
            // order, same splits, same file -- see sort_by_depth().
            sort_by_depth(head);
            std::fill(promoted.begin() + static_cast<std::ptrdiff_t>(head),
                      promoted.end(), 0);
            // The tail is sorted again, so the suffix bound can be exact again
            // -- and every position in it has moved, so the grid's one map
            // from name to position has to be rebuilt over the same range.
            rebuild_smax(head);
            for (std::size_t i = head; i < list.size(); ++i) pos_of[list[i].id] = i;
            restart = true;
            break;
        }

        if (st.bailed) break;
        if (restart) continue;

        out.push_back(std::move(list[head]));
        std::fill(promoted.begin() + static_cast<std::ptrdiff_t>(head), promoted.end(), 0);
        ++head;
    }

    // Whatever is left when the work bound ran out comes out in depth order,
    // which is exactly what the whole-object path would have produced.
    for (; head < list.size(); ++head) out.push_back(std::move(list[head]));

    st.output = out.size();
    if (stats) *stats = st;
    return out;
}

// -------------------------------------------------------------------------
// The scene, ordered for a writer that has no camera
// -------------------------------------------------------------------------
namespace {

// The order this step replaces, reachable in the same binary through
// SEXTANT_NEWELL=0 -- whole objects interleaved by their own distance from the
// eye, exactly as the SVG writer merged the three plans before step 9. It
// exists to be measured and asserted *against*: a check that only ever runs
// against the algorithm it is testing cannot tell a correct painter from a
// lenient oracle, and the same goes for a timing.
std::vector<ScenePaint> whole_object_order(const std::vector<Bar3DPolygon>& bars,
                                           const std::vector<Surface3DPolygon>& surfaces,
                                           const std::vector<PlanePlanItem>& planes,
                                           const std::vector<Scatter3DMarker>& markers,
                                           const std::vector<Line3DSegment>& segments,
                                           const std::vector<SurfaceTriPolygon>& meshes,
                                           const std::vector<ErrorBar3DPolygon>& errbars) {
    std::vector<std::size_t> plane_ids;
    std::vector<float> plane_depth;
    for (const PlanePlanItem& it : planes)
        if (std::find(plane_ids.begin(), plane_ids.end(), it.plane) == plane_ids.end()) {
            plane_ids.push_back(it.plane);
            plane_depth.push_back(it.depth);
        }

    std::vector<ScenePaint> out;
    std::size_t np = 0, ns = 0, nm = 0, nl = 0, nt = 0, ne = 0;
    // Markers merge here as a fourth stream rather than being appended after
    // the scene: they arrive already sorted far to near, and a cloud emitted
    // wholesale at the end would draw every point over the geometry in front
    // of it -- which is precisely the failure this control path exists to be
    // measured against, so it must not have a new one of its own.
    auto flush_before = [&](float depth) {
        for (;;) {
            const bool p = np < plane_ids.size() && plane_depth[np] > depth;
            const bool s = ns < surfaces.size() && surfaces[ns].plot_depth > depth;
            const bool m = nm < markers.size() && markers[nm].depth > depth;
            const bool g = nl < segments.size() && segments[nl].depth > depth;
            const bool t = nt < meshes.size() && meshes[nt].plot_depth > depth;
            // Error-bar pieces merge per piece, as markers and segments do:
            // the plan arrives far to near.
            const bool e = ne < errbars.size() && errbars[ne].depth > depth;
            if (!p && !s && !m && !g && !t && !e) break;
            const float pd = p ? plane_depth[np] : -std::numeric_limits<float>::max();
            const float sd = s ? surfaces[ns].plot_depth : -std::numeric_limits<float>::max();
            const float md = m ? markers[nm].depth : -std::numeric_limits<float>::max();
            const float gd = g ? segments[nl].depth : -std::numeric_limits<float>::max();
            const float td = t ? meshes[nt].plot_depth : -std::numeric_limits<float>::max();
            const float ed = e ? errbars[ne].depth : -std::numeric_limits<float>::max();
            if (p && pd >= sd && pd >= md && pd >= gd && pd >= td && pd >= ed)
                out.push_back({ ScenePaint::Kind::Plane, plane_ids[np++], {} });
            else if (s && sd >= md && sd >= gd && sd >= td && sd >= ed)
                out.push_back({ ScenePaint::Kind::Surface, ns++, {} });
            else if (t && td >= md && td >= gd && td >= ed)
                out.push_back({ ScenePaint::Kind::Mesh, nt++, {} });
            else if (m && md >= gd && md >= ed)
                out.push_back({ ScenePaint::Kind::Scatter, nm++, {} });
            else if (g && gd >= ed)
                out.push_back({ ScenePaint::Kind::Line, nl++, {} });
            else
                out.push_back({ ScenePaint::Kind::ErrorBar, ne++, {} });
        }
    };
    for (std::size_t i = 0; i < bars.size(); ++i) {
        flush_before(bars[i].plot_depth);
        out.push_back({ ScenePaint::Kind::Bar, i, {} });
    }
    flush_before(-std::numeric_limits<float>::max());
    return out;
}

bool newell_enabled() {
    static const bool on = [] {
        const char* s = std::getenv("SEXTANT_NEWELL");
        return !(s && std::atoi(s) == 0);
    }();
    return on;
}

} // namespace

std::vector<ScenePaint> plan_scene3d(const Projector3D& proj,
                                     const std::vector<Bar3DPolygon>& bars,
                                     const std::vector<Surface3DPolygon>& surfaces,
                                     const std::vector<PlanePlanItem>& planes,
                                     const std::vector<Scatter3DMarker>& markers,
                                     const std::vector<Line3DSegment>& segments,
                                     const std::vector<SurfaceTriPolygon>& meshes,
                                     const std::vector<ErrorBar3DPolygon>& errbars,
                                     PaintOrderStats* stats,
                                     std::size_t max_work,
                                     std::size_t max_splits) {
    if (!newell_enabled()) {
        if (stats) *stats = PaintOrderStats{};
        return whole_object_order(bars, surfaces, planes, markers, segments, meshes, errbars);
    }

    std::vector<PaintPoly> soup;
    soup.reserve(bars.size() + surfaces.size() + 8);

    // `rank` is the polygon's own position in its plan, which *is* its
    // object's exact internal order -- the plans are built back to front and
    // this is the only thing that reads them that way. Two polygons of one
    // object are never compared, so what rank does is keep them from being
    // rearranged: the initial sort is stable and a promotion never jumps a
    // lower-ranked sibling.
    for (std::size_t i = 0; i < bars.size(); ++i) {
        if (bars[i].box.size() < 2) continue;
        PaintPoly p;
        p.ring    = bars[i].box;
        p.kind    = PaintPoly::Kind::Bar;
        p.object  = bars[i].plot;
        p.element = bars[i].bar;
        p.rank    = i;
        p.source  = i;
        soup.push_back(std::move(p));
    }
    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        // Two points is a wireframe edge -- the bar loop's rule above.
        if (surfaces[i].box.size() < 2) continue;
        PaintPoly p;
        p.ring    = surfaces[i].box;
        p.kind    = PaintPoly::Kind::Surface;
        p.object  = surfaces[i].plot;
        p.element = surfaces[i].cell;
        p.rank    = i;
        p.source  = i;
        soup.push_back(std::move(p));
    }

    // A mesh face, on a grid cell's terms exactly: a triangle is a ring with a
    // plane, so it is an ordinary blade and an ordinary victim, and a two-point
    // ring is one of its wireframe edges, which `ring_plane()` refuses and
    // `stroke_vs_polygon()` orders. Nothing new is needed here -- which is what
    // PaintPoly::rank's note predicted, since plain Newell already runs over
    // every polygon and Surface3DPolygon had nothing grid-specific in it. A mesh
    // is not a new cost class either: a 50x50 `surface` already feeds the
    // painter 5000 triangles.
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        if (meshes[i].box.size() < 2) continue;
        PaintPoly p;
        p.ring    = meshes[i].box;
        p.kind    = PaintPoly::Kind::Mesh;
        p.object  = meshes[i].plot;
        p.element = meshes[i].face;
        p.rank    = i;
        p.source  = i;
        soup.push_back(std::move(p));
    }

    // One marker, one point. `rank` is its position in the plan, which is
    // already the exact far-to-near order among markers, so the stable sort
    // keeps a cloud internally right without ever comparing two of its points.
    for (std::size_t i = 0; i < markers.size(); ++i) {
        PaintPoly p;
        p.ring    = { markers[i].box };
        p.radius  = markers[i].radius;
        p.kind    = PaintPoly::Kind::Scatter;
        p.object  = markers[i].plot;
        p.element = markers[i].index;
        p.rank    = i;
        p.source  = i;
        soup.push_back(std::move(p));
    }

    // One segment, two points -- a stroke, which ring_plane() refuses, so it is
    // ordered by stroke_vs_polygon() and can never be a blade. That is the
    // whole reason a path is emitted as strokes rather than as the ribbon
    // quads the raster path draws: a quad has a plane, and a path can
    // contribute thousands of thin ones for other geometry to be cut along.
    // See Line3DSegment.
    for (std::size_t i = 0; i < segments.size(); ++i) {
        PaintPoly p;
        p.ring    = { segments[i].a, segments[i].b };
        p.kind    = PaintPoly::Kind::Line;
        p.object  = segments[i].plot;
        p.element = segments[i].index;
        p.rank    = i;
        p.source  = i;
        soup.push_back(std::move(p));
    }

    // Error bars (v1.0 step 17): a whisker, a cap arm or a block edge is a
    // two-point stroke, ordered like a path's segment and never a blade; a
    // block face is a four-point ring, an ordinary blade and victim like a
    // translucent bar's face.
    for (std::size_t i = 0; i < errbars.size(); ++i) {
        if (errbars[i].box.size() < 2) continue;
        PaintPoly p;
        p.ring    = errbars[i].box;
        p.kind    = PaintPoly::Kind::ErrorBar;
        p.object  = errbars[i].plot;
        p.element = errbars[i].point;
        p.rank    = i;
        p.source  = i;
        soup.push_back(std::move(p));
    }

    // One polygon per *plane*, not per item: since 7a a plane is one flat quad
    // and its four forms are layers of one coplanar picture, so they share a
    // position in the scene and are emitted together. The plan is sorted by
    // plane distance, so the first item of each plane fixes the order the
    // planes were built in, which is what the emission below walks.
    std::vector<std::size_t> plane_ids;
    for (const PlanePlanItem& it : planes) {
        if (std::find(plane_ids.begin(), plane_ids.end(), it.plane) != plane_ids.end())
            continue;
        plane_ids.push_back(it.plane);
        PaintPoly p;
        p.ring.assign(it.quad, it.quad + 4);
        p.kind    = PaintPoly::Kind::Plane;
        p.object  = it.plane;
        p.element = 0;
        p.rank    = 0;
        p.source  = it.plane;
        soup.push_back(std::move(p));
    }

    const std::vector<PaintPoly> ordered =
        paint_order(std::move(soup), proj, stats, max_work, max_splits);

    std::vector<ScenePaint> out;
    out.reserve(ordered.size());
    for (const PaintPoly& p : ordered) {
        ScenePaint s;
        switch (p.kind) {
            case PaintPoly::Kind::Bar:     s.kind = ScenePaint::Kind::Bar;     break;
            case PaintPoly::Kind::Surface: s.kind = ScenePaint::Kind::Surface; break;
            case PaintPoly::Kind::Plane:   s.kind = ScenePaint::Kind::Plane;   break;
            case PaintPoly::Kind::Scatter: s.kind = ScenePaint::Kind::Scatter; break;
            case PaintPoly::Kind::Line:    s.kind = ScenePaint::Kind::Line;    break;
            case PaintPoly::Kind::Mesh:    s.kind = ScenePaint::Kind::Mesh;    break;
            case PaintPoly::Kind::ErrorBar: s.kind = ScenePaint::Kind::ErrorBar; break;
        }
        s.index = p.source;
        // Only a piece carries pixels. An untouched polygon is emitted as the
        // plan built it, which is what keeps a scene with nothing to split
        // byte-identical to the file the whole-object path produced -- when
        // the order agrees, which for a scene with nothing interleaving it
        // does.
        if (p.split) s.xy = p.px;
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace sextant
