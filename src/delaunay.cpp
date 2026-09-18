#include "delaunay.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sextant {

namespace {

struct Pt { double x = 0.0, y = 0.0; };

struct Tri {
    std::uint32_t a = 0, b = 0, c = 0;   // into the working point array
    bool alive = true;
};

double cross2(Pt o, Pt p, Pt q) {
    return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x);
}

// Whether `d` lies strictly inside the circle through a, b, c, which must be
// wound counter-clockwise. The standard lifted determinant: each point is
// raised onto the paraboloid z = x^2 + y^2, where "inside the circumcircle"
// becomes "below the plane through the three lifted points".
//
// A *strict* test, so a point exactly on the circle is not inside it. That is
// what makes a regular grid -- four cocircular corners, which is the most
// ordinary input this will ever see -- produce a triangulation rather than an
// oscillation: with a non-strict test both diagonals want the same quad and
// the hole boundary stops being a simple polygon.
bool in_circumcircle(Pt a, Pt b, Pt c, Pt d) {
    const double ax = a.x - d.x, ay = a.y - d.y;
    const double bx = b.x - d.x, by = b.y - d.y;
    const double cx = c.x - d.x, cy = c.y - d.y;
    const double det = (ax * ax + ay * ay) * (bx * cy - cx * by)
                     - (bx * bx + by * by) * (ax * cy - cx * ay)
                     + (cx * cx + cy * cy) * (ax * by - bx * ay);
    return det > 0.0;
}

} // namespace

bool delaunay_triangulate(std::span<const double> u, std::span<const double> v,
                          std::vector<std::uint32_t>& tri) {
    tri.clear();
    const std::size_t n = std::min(u.size(), v.size());
    if (n < 3) return false;

    // ---- Deduplicate, keeping the caller's indices -------------------------
    //
    // `keep` is the list of distinct points, each remembering which of the
    // caller's vertices it stands for -- the *first* one, so a duplicate's own
    // index simply never appears in a triangle. Sorting is what makes this
    // O(n log n) rather than quadratic; the tolerance is relative to the
    // point set's own extent, because "the same point" is a statement about
    // the data's scale and not about doubles.
    double lo_x =  std::numeric_limits<double>::max();
    double hi_x = -std::numeric_limits<double>::max();
    double lo_y =  std::numeric_limits<double>::max();
    double hi_y = -std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < n; ++i) {
        lo_x = std::min(lo_x, u[i]); hi_x = std::max(hi_x, u[i]);
        lo_y = std::min(lo_y, v[i]); hi_y = std::max(hi_y, v[i]);
    }
    const double span = std::max(hi_x - lo_x, hi_y - lo_y);
    if (!(span > 0.0)) return false;           // every point in one place
    const double eps = span * 1e-12;

    std::vector<std::uint32_t> order(n);
    for (std::size_t i = 0; i < n; ++i) order[i] = static_cast<std::uint32_t>(i);
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        if (u[a] != u[b]) return u[a] < u[b];
        if (v[a] != v[b]) return v[a] < v[b];
        return a < b;
    });

    std::vector<Pt> pts;                       // the distinct points
    std::vector<std::uint32_t> src;            // pts[k] is the caller's src[k]
    for (const std::uint32_t i : order) {
        if (!pts.empty() && std::fabs(pts.back().x - u[i]) <= eps
                         && std::fabs(pts.back().y - v[i]) <= eps)
            continue;
        pts.push_back({ u[i], v[i] });
        src.push_back(i);
    }
    if (pts.size() < 3) return false;

    // ---- The super-triangle ------------------------------------------------
    //
    // Big enough that no input point can be outside it and no *circumcircle*
    // of a real triangle can reach one of its corners -- which is what keeps
    // the super-triangle from perturbing the answer rather than merely
    // containing it. The factor is generous for that reason; it costs three
    // points.
    const double cx = (lo_x + hi_x) * 0.5;
    const double cy = (lo_y + hi_y) * 0.5;
    const double r  = span * 20.0;
    const std::uint32_t s0 = static_cast<std::uint32_t>(pts.size());
    pts.push_back({ cx - 2.0 * r, cy - r });
    pts.push_back({ cx + 2.0 * r, cy - r });
    pts.push_back({ cx,           cy + 2.0 * r });

    std::vector<Tri> tris;
    tris.push_back({ s0, s0 + 1, s0 + 2, true });

    // ---- Bowyer-Watson -----------------------------------------------------
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
    std::vector<char> shared;
    for (std::uint32_t p = 0; p < s0; ++p) {
        const Pt d = pts[p];
        edges.clear();
        for (Tri& t : tris) {
            if (!t.alive) continue;
            if (!in_circumcircle(pts[t.a], pts[t.b], pts[t.c], d)) continue;
            t.alive = false;
            edges.push_back({ t.a, t.b });
            edges.push_back({ t.b, t.c });
            edges.push_back({ t.c, t.a });
        }
        // The hole's boundary is the edges that belonged to exactly one of the
        // deleted triangles; an edge shared by two of them is interior and
        // disappears with them. Compared undirected, since the two triangles
        // that share an edge traverse it in opposite directions.
        shared.assign(edges.size(), 0);
        for (std::size_t i = 0; i < edges.size(); ++i)
            for (std::size_t j = i + 1; j < edges.size(); ++j)
                if ((edges[i].first == edges[j].second && edges[i].second == edges[j].first) ||
                    (edges[i].first == edges[j].first  && edges[i].second == edges[j].second)) {
                    shared[i] = 1;
                    shared[j] = 1;
                }
        for (std::size_t i = 0; i < edges.size(); ++i) {
            if (shared[i]) continue;
            std::uint32_t a = edges[i].first, b = edges[i].second;
            // Wound counter-clockwise, which in_circumcircle() requires of
            // every triangle it is handed. The boundary edges come out of a
            // CCW triangle already oriented, but a swap costs nothing and
            // removes the need to trust that.
            if (cross2(pts[a], pts[b], d) < 0.0) std::swap(a, b);
            tris.push_back({ a, b, p, true });
        }
        // Compacting here rather than at the end keeps the scan above over
        // live triangles only, which is most of what this loop costs.
        tris.erase(std::remove_if(tris.begin(), tris.end(),
                                  [](const Tri& t) { return !t.alive; }),
                   tris.end());
    }

    // ---- Back to the caller's indices --------------------------------------
    for (const Tri& t : tris) {
        if (!t.alive) continue;
        // Anything still touching the super-triangle is outside the hull.
        if (t.a >= s0 || t.b >= s0 || t.c >= s0) continue;
        // A zero-area triangle here is not the caller's degenerate input --
        // ingest keeps those and guards the normal -- but an artefact of
        // three nearly collinear points, which contributes nothing to the
        // picture and would give the painter a ring with no plane.
        if (cross2(pts[t.a], pts[t.b], pts[t.c]) == 0.0) continue;
        tri.push_back(src[t.a]);
        tri.push_back(src[t.b]);
        tri.push_back(src[t.c]);
    }
    // Collinear input: the hull is a segment, every triangle touched the
    // super-triangle, and nothing is left. The caller turns this into a throw
    // rather than an empty box.
    return !tri.empty();
}

} // namespace sextant
