#include "painter3d.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>

namespace sextant {
namespace {

// Box-unit distance below which two points coincide. Absolute, since every
// scene is normalized to the unit-ish box.
constexpr double kEps = 1e-9;

// How far off a plane a vertex may be and still count as on it (generous, to
// avoid sliver splits).
constexpr double kPlaneEps = 1e-7;

double signed_dist(Vec3 p, Vec3 p0, Vec3 n) { return dot(p - p0, n); }

Vec3 mix(Vec3 a, Vec3 b, double t) { return a + (b - a) * t; }

// Ring area from the same summed cross terms as ring_plane(); used to check
// whether a cut actually divided anything.
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
    // Newell's normal (summed cross terms): stable for nearly collinear edges.
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

    // The ring must actually lie on that plane: a warped ring gets an average
    // plane containing none of its vertices, and splitting against it never
    // terminates. Non-planar rings can be ordered but not used as blades.
    // Tolerance is relative to the ring's size.
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
    // A two-point ring is a stroke: cut it as a segment.
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
            // Not straddling: all of it goes to its side.
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
            // Only a real sign change makes a crossing (on-plane vertices are
            // already in `out`).
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
    // Two points = a stroke (bar edge, wireframe edge): no plane, never a blade,
    // but ordered and cut like any primitive. One point = a scatter3d marker:
    // never a blade or victim; its screen footprint is a disc of `radius`
    // pixels. Behind a perspective eye it isn't drawn (empty `px`).
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
    // Half a pixel of slack: polygons that merely touch have nothing to resolve.
    constexpr float kSlack = 0.5f;
    return a.bb[2] < b.bb[0] + kSlack || b.bb[2] < a.bb[0] + kSlack
        || a.bb[3] < b.bb[1] + kSlack || b.bb[3] < a.bb[1] + kSlack;
}

// True when all of `poly` is on the far side of the plane (p0, n), with `n`
// facing the eye (Newell's tests 3 and 4).
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

// Separating-axis test on two convex projected rings; true when they overlap
// (the last test before a split).
bool projections_overlap(const std::vector<float>& a, const std::vector<float>& b) {
    // Tolerance for shared edges, tuned between two tests: below ~0.10 a lone
    // smooth sheet starts splitting; at 0.25 real overlaps are missed.
    // test_scene3d_svg_order() defends both bounds.
    //
    // A stroke against a polygon uses the polygon's and the line's normals;
    // two strokes are never compared (nothing to occlude or cut).
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

// A marker vs a polygon: +1 in front, -1 behind, 2 = no screen overlap. Never
// needs a split. Compares depths along the marker's pixel ray; the whole
// symbol goes on one side, as in the raster path.
int point_vs_polygon(const PaintPoly& pt, const PaintPoly& poly, const Projector3D& proj) {
    // Inside the polygon's projection (convex half-plane walk).
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

    // Inside: the side of the polygon's plane (normal facing the eye) decides.
    Vec3 p0, n;
    if (!ring_plane(poly.ring, p0, n)) return 2;
    if (!proj.faces_camera(p0, n)) n = n * -1.0;
    const double d = signed_dist(pt.ring[0], p0, n);
    // A marker on a surface is drawn after it.
    return d >= -kPlaneEps ? +1 : -1;
}

// A stroke vs a polygon it overlaps on screen: +1 in front, -1 behind, 0 =
// passes through inside the overlap (needs a cut), 2 = no overlap. Exact: the
// line crosses the plane at one point X, so clip the line to the polygon's
// projection and check whether X falls inside. A line on the plane counts as
// in front (so wireframes draw over their cells).
int stroke_vs_polygon(const PaintPoly& line, const PaintPoly& poly, const Projector3D& proj) {
    Vec3 p0, n;
    if (!ring_plane(poly.ring, p0, n)) return 0;
    if (!proj.faces_camera(p0, n)) n = n * -1.0;
    const Vec3& a = line.ring[0];
    const Vec3& b = line.ring[1];
    const double da = signed_dist(a, p0, n), db = signed_dist(b, p0, n);
    if (da >= -kPlaneEps && db >= -kPlaneEps) return +1;
    if (da <=  kPlaneEps && db <=  kPlaneEps) return -1;

    // The crossing as a parameter along the projected line (a crossing behind a
    // near-clipped end is left to the cut).
    const Vec3 x = mix(a, b, da / (da - db));
    Px3 xa, xb;
    if (!proj.project_segment(x, x, xa, xb)) return 0;
    const float lx = line.px[0], ly = line.px[1];
    const float dx = line.px[2] - lx, dy = line.px[3] - ly;
    const float len2 = dx * dx + dy * dy;
    if (len2 < 1e-12f) return 0;
    const float sx = ((xa.x - lx) * dx + (xa.y - ly) * dy) / len2;

    // Cyrus-Beck: the line's parameter interval inside the polygon.
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
    // Work bound (tests + splits), so an unanticipated scene gives a slightly
    // wrong picture rather than a hung export. 5M leaves ~5x headroom over the
    // heaviest test scene.
    if (max_work == 0) max_work = 20000000;
    // Split bound, proportional to the input. This is the one that binds in
    // practice, hence SvgExportOptions::max_splits.
    if (max_splits == 0) max_splits = 8 * polys.size() + 64;

    // Drop degenerate rings and anything clipped away by the near plane.
    std::vector<PaintPoly> list;
    list.reserve(polys.size());
    for (PaintPoly& p : polys) {
        prepare_paint_poly(p, proj);
        // Two floats is a marker (one projected point).
        if (p.px.size() >= 2) list.push_back(std::move(p));
    }

    // Newell's first step: sort farthest first by each polygon's farthest
    // point. Stable, since ties keep each object's own (exact) order.
    // Sorts (depth, index) keys and applies the permutation once, which is far
    // cheaper than moving polygons (the split path re-sorts per split). Ties
    // break on index, so the result equals a stable_sort.
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
        // Each slot is emptied before being refilled; scratch buffers are reused.
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

    // Entries already promoted once; a second conflict means a cycle (split).
    std::vector<char> promoted(list.size(), 0);

    // Upper bound on `dmax` over each suffix, which keeps Newell's early stop
    // sound after promotions unsort the tail. Only ever an over-estimate, so
    // stopping on it is safe.
    std::vector<float> smax(list.size(), 0.0f);
    auto rebuild_smax = [&](std::size_t from) {
        smax.resize(list.size());
        for (std::size_t i = list.size(); i-- > from;)
            smax[i] = (i + 1 < list.size()) ? std::max(list[i].dmax, smax[i + 1])
                                            : list[i].dmax;
    };
    rebuild_smax(0);

    // ---- The screen-space bucket grid ------------------------------------
    // Only polygons sharing a grid cell with the head can overlap it, which
    // avoids the quadratic scan. Keyed on PaintPoly::id (positions change on
    // promotion and split; `pos_of` maps id -> position). Append-only: a
    // split's first piece keeps the parent's id and entries, only the second
    // is inserted; stale entries are dropped lazily once behind `head`.
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
    // About two polygons per cell, capped at both ends.
    const int grid = std::clamp(
        static_cast<int>(std::lround(std::sqrt(static_cast<double>(list.size()) / 2.0))),
        1, 96);
    const float cw = std::max(1e-3f, (gx1 - gx0) / static_cast<float>(grid));
    const float chh = std::max(1e-3f, (gy1 - gy0) / static_cast<float>(grid));
    std::vector<std::vector<std::uint32_t>> cells(
        static_cast<std::size_t>(grid) * static_cast<std::size_t>(grid));
    // Polygons covering more cells than this (e.g. a plane's quad) go in one
    // list every query reads.
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

    // Candidate gathering; `seen` is stamped by generation, so it needs no clear.
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
                // Strictly behind head: the polygon at head may still be pushed
                // back by a promotion.
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
        // Position order: deterministic, and lets the depth bound stop the scan.
        std::sort(out.begin(), out.end());
    };

    while (head < list.size()) {
        bool restart = false;
        gather(cand);

        for (const std::size_t q : cand) {
            const PaintPoly& P = list[head];
            const PaintPoly& Q = list[q];

            // Test 1: depth extents don't overlap. `continue`, not `break`:
            // promotions break the sorted order the early stop would rely on.
            if (smax[q] <= P.dmin) break;
            if (Q.dmax <= P.dmin) continue;

            ++st.tests;
            if (++work > max_work) { st.bailed = true; break; }   // work, not splits

            // Test 2: screen bounding boxes are disjoint.
            if (bb_disjoint(P, Q)) continue;

            // Tests 3 and 4: P behind Q's plane, or Q in front of P's (planes
            // facing the eye).
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

            // Test 5a, markers: exact, and before the separating-axis test (a
            // one-point projection has no edges). Marker-marker and
            // marker-stroke pairs keep the sort's answer, which is exact.
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
                    // The marker is in front: promotion alone resolves it.
                }
            }

            // Test 5: the projections themselves (skipped for markers,
            // already decided above).
            if (!marker_pair && !projections_overlap(P.px, Q.px)) continue;

            // Test 6: a stroke against a polygon (see stroke_vs_polygon()).
            // Otherwise promote, or cut a line passing through.
            if (!marker_pair) {
                const bool p_line = P.ring.size() == 2, q_line = Q.ring.size() == 2;
                if (p_line != q_line) {
                    const int side = p_line ? stroke_vs_polygon(P, Q, proj)
                                            : stroke_vs_polygon(Q, P, proj);
                    if (side == 2) continue;
                    if ((p_line && side < 0) || (q_line && side > 0)) continue;
                }
            }

            // Unresolved. First time: assume the sort was wrong and promote Q.
            if (!promoted[q]) {
                promoted[q] = 1;
                const std::size_t j = q;
                if (j > head) {
                    // `promoted` is indexed by position and must rotate with
                    // the list, or conflicting pairs promote each other forever.
                    std::rotate(list.begin() + static_cast<std::ptrdiff_t>(head),
                                list.begin() + static_cast<std::ptrdiff_t>(j),
                                list.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                    std::rotate(promoted.begin() + static_cast<std::ptrdiff_t>(head),
                                promoted.begin() + static_cast<std::ptrdiff_t>(j),
                                promoted.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                    // Flatten the bound over the rotated span (still an
                    // over-estimate).
                    std::fill(smax.begin() + static_cast<std::ptrdiff_t>(head),
                              smax.begin() + static_cast<std::ptrdiff_t>(j) + 1,
                              smax[head]);
                    for (std::size_t i = head; i <= j; ++i) pos_of[list[i].id] = i;
                }
                restart = true;
                break;
            }

            // Q was already promoted: a cycle. Cut one by the other's plane so
            // each piece is separable by test 3.
            ++st.cycles;

            // Either cut resolves the pair; prefer the cheaper victim (a plane's
            // payload is a whole image). But it stays a preference: if the
            // preferred victim doesn't straddle, cut the other one.
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
                // Only flat polygons or lines may be cut; a warped ring would
                // be duplicated rather than cut, forever.
                if (!stroke) {
                    Vec3 vp0, vn;
                    if (!ring_plane(vring, vp0, vn)) continue;
                }
                split_ring_by_plane(vring, sp0, sn, piece_front, piece_back);
                const std::size_t min_pts = stroke ? 2 : 3;
                if (piece_front.size() < min_pts || piece_back.size() < min_pts) continue;
                // A cut must divide the victim: vertices within kPlaneEps count
                // on both sides, so an edge lying on the blade yields a
                // zero-area piece. Treating that as a cut loops forever on
                // coplanar split pieces. Relative to the victim's size (strokes:
                // length).
                constexpr double kMinPieceFrac = 1e-6;
                auto measure = [stroke](const std::vector<Vec3>& r) {
                    return stroke ? length(r[1] - r[0]) : ring_area(r);
                };
                if (std::min(measure(piece_front), measure(piece_back))
                    <= kMinPieceFrac * measure(vring)) continue;
                cut = true;
            }
            if (!cut) {
                // Nothing to cut (coplanar, touching, or sub-pixel crossing);
                // either order draws the same. Counted.
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
            // `a` keeps the parent's id (its grid entries still cover it); `b`
            // is new and inserted.
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
            // Re-sort the tail and clear promotion marks. Required: skipping it
            // is faster but changes split order and leaves pixel errors. Done
            // via the index permutation (see sort_by_depth()).
            sort_by_depth(head);
            std::fill(promoted.begin() + static_cast<std::ptrdiff_t>(head),
                      promoted.end(), 0);
            // The tail is sorted again: reset the suffix bound and rebuild
            // `pos_of` over the range.
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

    // Whatever is left after the work bound comes out in depth order.
    for (; head < list.size(); ++head) out.push_back(std::move(list[head]));

    st.output = out.size();
    if (stats) *stats = st;
    return out;
}

// -------------------------------------------------------------------------
// The scene, ordered for a writer that has no camera
// -------------------------------------------------------------------------
namespace {

// The whole-object order Newell replaced (SEXTANT_NEWELL=0), kept as a
// baseline to measure and test against.
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
    // Markers merge as a stream (already far to near), not appended at the end.
    auto flush_before = [&](float depth) {
        for (;;) {
            const bool p = np < plane_ids.size() && plane_depth[np] > depth;
            const bool s = ns < surfaces.size() && surfaces[ns].plot_depth > depth;
            const bool m = nm < markers.size() && markers[nm].depth > depth;
            const bool g = nl < segments.size() && segments[nl].depth > depth;
            const bool t = nt < meshes.size() && meshes[nt].plot_depth > depth;
            // Error-bar pieces merge per piece (already far to near).
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

    // `rank` is the polygon's position in its plan (the object's exact order);
    // it keeps siblings from being rearranged by the stable sort.
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
        // Two points is a wireframe edge.
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

    // A mesh face: an ordinary blade and victim; two-point rings are its
    // wireframe edges.
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

    // One point per marker; `rank` keeps a cloud's exact order.
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

    // One two-point stroke per segment (never a blade). See Line3DSegment.
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

    // Error bars: whiskers, caps and block edges are strokes; block faces are
    // ordinary four-point rings.
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

    // One polygon per plane (its forms are coplanar layers). The plan is sorted
    // by plane distance; each plane's first item fixes the order.
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
        // Only split pieces carry pixels; unsplit polygons are emitted as
        // planned (so unsplit scenes match the whole-object output).
        if (p.split) s.xy = p.px;
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace sextant
