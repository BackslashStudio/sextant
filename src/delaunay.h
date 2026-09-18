#pragma once
// Delaunay triangulation of a planar point set, used by the `orient` overloads
// of Axes3D::surface_tri(). Standalone so tests can check it directly.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sextant {
    // Bowyer-Watson triangulation of (u[i], v[i]): writes three indices per
    // triangle into `tri`, counter-clockwise in the u-v plane. Indices refer to the
    // caller's array; coincident points are skipped but keep their slots (so
    // `colors`/`hint_labels` stay aligned). Returns false for fewer than three
    // distinct points or all collinear. Runs once at ingest.
    bool delaunay_triangulate(std::span<const double> u, std::span<const double> v,
                              std::vector<std::uint32_t>& tri);
} // namespace sextant
