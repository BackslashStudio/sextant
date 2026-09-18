#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include <cstddef>
#include <algorithm>
#include <vector>

namespace sextant {

// One bar reduced to six faces, in *data* space with a *box*-space normal.
//
// One definition, two consumers -- the GPU buffer builder in DataRenderer and
// the SVG painter -- for exactly the reason plan_box3d() is one function: the
// two outputs must not be able to disagree about where a bar's faces are or
// what colour they came out. Data space rather than box space because the GPU
// buffer is held in data space (the camera and the limits are uniforms, so
// neither invalidates it); box space for the normal because that is where the
// light is fixed.
struct Bar3DFace {
    Vec3  p[4];              // corners, in ring order
    Vec3  normal;            // outward unit normal, box space
    float shade = 1.0f;      // multiplies the bar's colour
};

// The light bar3d faces are lit by, fixed in *box* space -- so a face keeps
// its brightness as the camera orbits, which is what lets the shade be baked
// into a vertex buffer with no camera in its key. Chosen so the three faces
// visible from the default camera (top, +x, -y) come out at three clearly
// different brightnesses rather than two of them nearly equal.
inline constexpr Vec3 kBar3DLight{ 0.30, -0.55, 0.78 };

// `shading` is Bar3DOptions::shading: 0 leaves every face flat, 1 takes the
// unlit ones to black.
inline float bar3d_shade(Vec3 normal_box, float shading) {
    const double ndotl = std::max(0.0, dot(normal_box, normalize(kBar3DLight)));
    const double s = 1.0 - static_cast<double>(std::clamp(shading, 0.0f, 1.0f)) * (1.0 - ndotl);
    return static_cast<float>(std::clamp(s, 0.0, 1.0));
}

// Bar `k` as one interval per *box* axis, in data space: its footprint on the
// two grid axes and base-to-tip on the third, through the plot's own Axis3Map.
//
// One definition, because the faces are built from it and the hover hint casts
// a ray at it -- and a footprint that differed between the two would put the
// tooltip on a bar that is not where the picture says it is.
void bar3d_bounds(const Bar3DPlot& b, std::size_t k, double lo[3], double hi[3]);

// Where a pixel's ray meets bar `k`, as `depth` in Px3's own quantity, so the
// answer compares directly against plane_ray_hit()'s. False when the ray
// misses the bar or the hit is behind a perspective eye.
//
// This is what §11 deferred: a bar is six faces rather than one plane, so
// inverting a pixel onto it is not a divide -- but it *is* an axis-aligned
// box, which is three slab tests and no less exact. See box_ray_hit().
bool bar3d_ray_hit(const Bar3DPlot& b, std::size_t k, const Projector3D& proj,
                   float px, float py, float& depth);

// The six faces of bar `k` (row-major, k = i * |v| + j), in a fixed order:
// -x, +x, -y, +y, -z, +z of the *box* axes the bar's orientation maps to.
//
// `tf` is consulted only for the sign of each axis's data->box scale: a
// reversed limit mirrors that axis, which flips which way a face points and so
// which faces are lit. Nothing else about the transform is used, which is what
// keeps the corners in data space.
void bar3d_faces(const Bar3DPlot& b, std::size_t k, const Transform3D& tf,
                 Bar3DFace out[6]);

// True when the plot has to be drawn back to front rather than left to the
// depth buffer. One predicate, because "is this the ordered path" is asked by
// the raster path, the vector path and the tests, and three readings of
// `alpha < 1` is three chances to disagree about a boundary case.
inline bool bar3d_translucent(const Bar3DPlot& b) {
    return b.opts.color.a * b.opts.alpha < 1.0f;
}
inline float bar3d_alpha(const Bar3DPlot& b) {
    return std::clamp(b.opts.color.a * b.opts.alpha, 0.0f, 1.0f);
}
// The outline's own opacity, independent of the face's: on a translucent bar
// the edges are the only thing saying where one box stops and the next starts,
// so fading them with the glass is what makes the picture unreadable.
inline float bar3d_edge_alpha(const Bar3DPlot& b) {
    return std::clamp(b.opts.edgecolor.a * b.opts.edge_alpha, 0.0f, 1.0f);
}

// The twelve edges of one bar, as pairs of data-space endpoints, taken off the
// two faces at the ends of the box's first axis: their four sides each, plus
// the four that connect corresponding corners. Every edge exactly once.
//
// Shared, because both output paths need the same twelve in the same order --
// the GPU builds its ribbon instances from them and the SVG path emits them as
// polylines when the bars are translucent (an opaque bar's outline is the
// stroke on its visible faces, which is the same set once the hidden ones are
// occluded).
void bar3d_edges(const Bar3DFace f[6], Vec3 out[12][2]);

// The order one plot's bars must be drawn in, back to front, as flat bar
// indices. Shared by the two paths that need it -- the SVG painter and the
// GPU's translucent draw -- because it is the same order for the same reason,
// and a second copy of it would be a second chance to get the sort that took
// two attempts wrong again.
//
// Exact, and it is the grid that makes it so: any two cells are separated by
// an axis-aligned plane (a grid line in u, or one in v), and whatever lies on
// the far side of that plane from the eye cannot occlude what lies on the
// near side. Ordering by distance from the eye along u, then along v within a
// column, is a BSP traversal of those planes -- correct for every pair
// whatever the heights or the bases. See memory/spec_3d.md §10 for the two
// cases that have no separating planes and so no exact order.
void bar3d_draw_order(const Bar3DPlot& b, const Projector3D& proj,
                      std::vector<std::size_t>& out);

// How far one plot's centre is from the eye. Two bar3d objects in an axes have
// no separating plane between them, so there is no exact order for the pair --
// this is the admitted heuristic both paths order plots by, and it is one
// place rather than two so they at least agree on which heuristic.
double bar3d_plot_distance(const Bar3DPlot& b, const Projector3D& proj);

// The six faces of one bar in the order they must be drawn, as indices into
// bar3d_faces()' output: the three turned away from the camera first, then the
// three turned toward it. `front` is where the second group starts, so an
// opaque bar draws `[front, 6)` and a translucent one all of it.
//
// Exact for the same reason the grid order is: neither group overlaps itself
// in projection, because the visible faces of a convex solid tile its
// silhouette and so do the hidden ones.
struct Bar3DFaceOrder {
    int index[6] = { 0, 1, 2, 3, 4, 5 };
    int front = 0;
};
Bar3DFaceOrder bar3d_face_order(const Bar3DFace f[6], const Transform3D& tf,
                                const Projector3D& proj);

// One face, projected, ready to emit. The SVG path's counterpart of what the
// depth buffer does for the raster one.
struct Bar3DPolygon {
    std::vector<float> xy;              // pixel ring, x,y pairs
    // The same ring in *box* space, which is where Newell's algorithm works
    // (step 9): a plane normal means something there and Px3::depth is
    // defined there. Two points rather than a ring for the bare edges below,
    // which can be ordered but never split anything.
    std::vector<Vec3> box;
    Color fill{ 0, 0, 0, 1 };           // the bar's colour, already shaded
    Color stroke{ 0, 0, 0, 1 };
    float stroke_width = 0.0f;          // 0 = no outline
    // False for the bare box edges a translucent bar emits: a stroked open
    // polyline rather than a filled face. Nothing else produces one.
    bool  filled = true;
    // The face's own centroid depth. *Not* what the emission order is decided
    // by -- see the sort in plan_bars3d() for why that would be wrong -- but
    // kept because it is the one number about a face that says where it is.
    float depth = 0.0f;
    // The owning *plot object's* distance from the eye -- what the plots were
    // sorted by, repeated on every polygon so the SVG writer can merge the
    // plane plan into this one without re-deriving it. Whole objects
    // interleave; faces within one do not.
    float plot_depth = 0.0f;
    // Which bar this face came from, as (plot object, flat bar index). The
    // plan is the only account of the scene its consumers get, and without
    // this the order it is so carefully put in cannot be checked from outside.
    std::size_t plot = 0, bar = 0;
};

// Every face of every bar that has to be drawn, projected to pixels and
// ordered back to front -- the painter's algorithm SVG needs because it has no
// depth buffer (memory/spec_3d.md §10).
//
// For an opaque plot that is the three faces of each bar turned toward the
// camera; for a translucent one it is all six, the hidden three first, since
// that is what showing through means. Anything behind the eye is dropped
// either way, by the projector's polygon clip.
//
// Outline width is the world-space half of the stroke fork: `edge_linewidth`
// is a pixel width at the *box centre*, so a bar further away gets a
// proportionally thinner stroke. One width per bar rather than a taper along
// each edge is the concession §4 records for SVG -- a polyline has one
// stroke-width -- and it is finer than the "one per line" that concession
// allows.
std::vector<Bar3DPolygon> plan_bars3d(const Projector3D& proj,
                                      const std::vector<Bar3DPlot>& bars);

} // namespace sextant
