#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include <cstddef>
#include <algorithm>
#include <vector>

namespace sextant {

// One bar face: corners in data space (the GPU buffer stays valid across camera
// and limit changes), normal in box space (where the light is). Shared by the
// GPU buffer builder and the SVG painter.
struct Bar3DFace {
    Vec3  p[4];              // corners, in ring order
    Vec3  normal;            // outward unit normal, box space
    float shade = 1.0f;      // multiplies the bar's colour
};

// Light fixed in box space, so shading can be baked into the vertex buffer.
// Chosen so the default view's three visible faces differ clearly.
inline constexpr Vec3 kBar3DLight{ 0.30, -0.55, 0.78 };

// 0 leaves faces flat; 1 takes unlit faces to black.
inline float bar3d_shade(Vec3 normal_box, float shading) {
    const double ndotl = std::max(0.0, dot(normal_box, normalize(kBar3DLight)));
    const double s = 1.0 - static_cast<double>(std::clamp(shading, 0.0f, 1.0f)) * (1.0 - ndotl);
    return static_cast<float>(std::clamp(s, 0.0, 1.0));
}

// Bar `k` as one data-space interval per box axis (footprint and base-to-tip).
// Shared by face building and hover ray casting.
void bar3d_bounds(const Bar3DPlot& b, std::size_t k, double lo[3], double hi[3]);

// Ray/bar intersection depth (Px3::depth, comparable with plane_ray_hit()),
// via slab tests. False on a miss or behind a perspective eye.
bool bar3d_ray_hit(const Bar3DPlot& b, std::size_t k, const Projector3D& proj,
                   float px, float py, float& depth);

// The six faces of bar `k` (k = i * |v| + j), ordered -x, +x, -y, +y, -z, +z in
// box axes. `tf` only supplies each axis's sign (reversed limits flip faces).
void bar3d_faces(const Bar3DPlot& b, std::size_t k, const Transform3D& tf,
                 Bar3DFace out[6]);

// True when the plot must be drawn back to front (alpha < 1).
inline bool bar3d_translucent(const Bar3DPlot& b) {
    return b.opts.color.a * b.opts.alpha < 1.0f;
}
inline float bar3d_alpha(const Bar3DPlot& b) {
    return std::clamp(b.opts.color.a * b.opts.alpha, 0.0f, 1.0f);
}
// Outline opacity, independent of the faces'.
inline float bar3d_edge_alpha(const Bar3DPlot& b) {
    return std::clamp(b.opts.edgecolor.a * b.opts.edge_alpha, 0.0f, 1.0f);
}

// The twelve edges of one bar as data-space endpoint pairs, each once, in a
// fixed order shared by the GPU ribbons and the SVG outline.
void bar3d_edges(const Bar3DFace f[6], Vec3 out[12][2]);

// Back-to-front bar order (flat indices), shared by the SVG painter and the
// GPU's translucent draw. Exact: grid cells are separated by axis-aligned
// planes, so ordering along u then v is a BSP traversal.
void bar3d_draw_order(const Bar3DPlot& b, const Projector3D& proj,
                      std::vector<std::size_t>& out);

// Distance of the plot's centre from the eye; a heuristic for ordering whole
// plots, shared by both paths.
double bar3d_plot_distance(const Bar3DPlot& b, const Projector3D& proj);

// Draw order of one bar's faces: the three facing away, then (from `front`)
// the three facing the camera. Opaque bars draw [front, 6). Exact for a convex
// solid.
struct Bar3DFaceOrder {
    int index[6] = { 0, 1, 2, 3, 4, 5 };
    int front = 0;
};
Bar3DFaceOrder bar3d_face_order(const Bar3DFace f[6], const Transform3D& tf,
                                const Projector3D& proj);

// One projected face for the SVG path.
struct Bar3DPolygon {
    std::vector<float> xy;              // pixel ring, x,y pairs
    // The ring in box space, for Newell's algorithm; two points for a bare edge
    // (ordered, never split).
    std::vector<Vec3> box;
    Color fill{ 0, 0, 0, 1 };           // the bar's colour, already shaded
    Color stroke{ 0, 0, 0, 1 };
    float stroke_width = 0.0f;          // 0 = no outline
    // False for a translucent bar's bare edges (open polylines).
    bool  filled = true;
    // The face's centroid depth (not the emission order; see plan_bars3d()).
    float depth = 0.0f;
    // The owning plot's distance, so the SVG writer can merge plans.
    float plot_depth = 0.0f;
    // Provenance (plot, flat bar index), so the order can be checked.
    std::size_t plot = 0, bar = 0;
};

// Every face to draw, projected and ordered back to front (SVG has no depth
// buffer): the three camera-facing faces for opaque bars, all six (hidden
// first) for translucent ones. Anything behind the eye is clipped. Edge width
// is pixels at the box centre, one width per bar.
std::vector<Bar3DPolygon> plan_bars3d(const Projector3D& proj,
                                      const std::vector<Bar3DPlot>& bars);

} // namespace sextant
