#pragma once
// Delaunay triangulation of a planar point set -- the topology the two
// `orient` overloads of Axes3D::surface_tri() derive when the caller gives
// none (v1.0 step 14.5).
//
// Here rather than inside axes3d.cpp for one reason: the oracle. This is
// unusually well served by checks that do not consult our own output at all --
// the **empty-circumcircle property** over every triangle/vertex pair, Euler's
// formula tying the triangle count to the hull size, and "the union of the
// triangles is the convex hull" -- and all three need to call the
// triangulation directly, on a point set the check chose.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sextant {

// Triangulates the points (u[i], v[i]) and writes 3M indices into `tri`, three
// per triangle and wound counter-clockwise in the u-v plane.
//
// **Indices are into the caller's own array**, duplicates included. Exactly
// coincident points are deduplicated for the triangulation -- Bowyer-Watson
// has no answer for two points at one position -- but they keep their slots,
// and a duplicate simply names no triangle. Renumbering instead would have
// been cheaper here and wrong everywhere else: `colors` and `hint_labels` are
// index-aligned with the vertices the caller passed.
//
// False when there is no triangulation to give: fewer than three distinct
// points, or all of them collinear. A caller that renders the result anyway
// would draw an empty box, so `surface_tri()` throws on it instead.
//
// Bowyer-Watson, which is O(n^2) in the worst case and close to O(n log n) on
// the scattered data this is for. That is the right trade here because it runs
// **once, at ingest** -- what is stored is the indices, so a pan, an orbit and
// a frame cost nothing, and the alternative (a divide-and-conquer or sweep-
// line construction) buys an asymptote at several times the code.
bool delaunay_triangulate(std::span<const double> u, std::span<const double> v,
                          std::vector<std::uint32_t>& tri);

} // namespace sextant
