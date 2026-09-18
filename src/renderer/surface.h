#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "bar3d.h"
#include <cstddef>
#include <limits>
#include <vector>

namespace sextant {

// A surface reduced to cells, in *data* space with a *box*-space normal.
//
// The same bargain bar3d.h strikes and for the same reason: one definition,
// two consumers -- the GPU buffer builder in DataRenderer and the SVG
// painter -- so the two outputs cannot disagree about where a cell is or what
// colour it came out. Data space for the corners because the GPU buffer is
// held in data space (the camera and the limits are uniforms, so neither
// invalidates it); box space for the normal because that is where the light
// is fixed.
//
// Deliberately *not* built on Bar3DFace even though the fields line up. A bar
// face is one of six on a closed solid, with an outward normal that says which
// side is outside; a surface cell is a sheet with two sides and no outside, so
// its normal is signed by nothing and the shading takes its absolute value.
// Sharing the struct would have hidden that difference rather than removed it.
struct SurfaceCell {
    Vec3  p[4];              // corners, in ring order: (i,j) (i+1,j) (i+1,j+1) (i,j+1)
    Vec3  normal;            // unit, box space, sign arbitrary
    float shade = 1.0f;      // multiplies the cell's colour
    double value = 0.0;      // the height the colormap is sampled at
};

// The ray/triangle test both sheet kinds run, and the sentinel it misses with.
//
// Published rather than kept private to surface.cpp because a mesh face is the
// same question about the same shape (v1.0 step 14.4): surface_ray_hit() calls
// it twice, on the two halves of a cell, and surface_tri_ray_hit() calls it
// once, on the face itself. A second transcription of Moller-Trumbore is a
// second set of epsilons for the tooltip to land differently by.
//
// The caller only ever compares hits, so one return value is enough and there
// is no "missed" out-parameter. A real hit can be negative: under an
// orthographic camera the ray origin sits on a plane through the target, and
// half the scene is behind it.
inline constexpr double kTriRayMiss = -std::numeric_limits<double>::max();
double tri_ray_t(const Projector3D::Ray3& r, Vec3 a, Vec3 b, Vec3 c);

// Cell (i, j), i in [0, cell_rows()), j in [0, cell_cols()), addressed by its
// low corner. `tf` is consulted only to put the normal in box space, which is
// what keeps the corners in data space.
//
// `value` is the mean of the four corner heights rather than the low corner's:
// a cell is coloured for what it *is*, and taking one of its corners would
// shift the whole colour field by half a cell against the geometry, which
// looks like a colormap that is subtly off rather than like an indexing choice.
void surface_cell(const SurfacePlot& s, std::size_t i, std::size_t j,
                  const Transform3D& tf, SurfaceCell& out);

// Where a pixel's ray meets cell `k` of a surface, as `depth` in Px3's own
// quantity so the answer compares directly against plane_ray_hit()'s and
// bar3d_ray_hit()'s -- which is what lets one hover hint order a plane, a bar
// and a surface in a single list. `sample` comes back as the flat index of the
// nearest of the cell's four samples, because that is what a caller's
// `hint_labels` entry is written about.
//
// Two triangle tests, split on the same 0-2 diagonal the raster path uses, so
// the tooltip cannot land on geometry that is not where the picture put it --
// the property bar3d_bounds() exists to give the bars.
//
// False when the ray misses the cell or the hit is behind a perspective eye.
bool surface_ray_hit(const SurfacePlot& s, std::size_t k, const Projector3D& proj,
                     float px, float py, float& depth, std::size_t& sample);

// The finished colour of one cell -- flat or colormapped, shaded, with the
// plot's alpha already in it. The single place a cell's colour is decided, so
// PNG and SVG cannot differ by a rounding rule in the colormap lookup.
Color surface_cell_color(const SurfacePlot& s, const SurfaceCell& c,
                         double vmin, double vmax);

// True when the surface has to be drawn back to front rather than left to the
// depth buffer. bar3d_translucent()'s counterpart, and one predicate for the
// same reason: three readings of `alpha < 1` is three chances to disagree
// about a boundary case.
inline bool surface_translucent(const SurfacePlot& s) {
    return s.opts.color.a * s.opts.alpha < 1.0f || s.opts.alpha < 1.0f;
}
inline float surface_alpha(const SurfacePlot& s) {
    return std::clamp(s.opts.alpha, 0.0f, 1.0f);
}
inline float surface_edge_alpha(const SurfacePlot& s) {
    return std::clamp(s.opts.edgecolor.a * s.opts.edge_alpha, 0.0f, 1.0f);
}

// The four edges of one cell, as pairs of data-space endpoints. Every cell
// emits all four, so an interior edge is drawn twice -- which costs a
// duplicate line and buys the property that the wireframe is exactly the cell
// grid with no boundary special case. The GPU builds its ribbon instances from
// these; the duplicate lands at the same depth as the original, so GL_LESS or a
// peel pass keeps only one of the two. plan_surfaces3d() uses the same edge numbering but emits each shared
// edge once, since an SVG has no depth test to drop the second copy.
void surface_cell_edges(const SurfaceCell& c, Vec3 out[4][2]);

// The order one surface's cells must be drawn in, back to front, as flat cell
// indices (k = i * cell_cols() + j).
//
// Exact, and it is the grid that makes it so -- the identical argument
// bar3d_draw_order() rests on. Every cell lies between two consecutive u lines
// and two consecutive v lines, so any two cells are separated by an
// axis-aligned plane, and whatever lies on the far side of that plane from the
// eye cannot occlude what lies on the near side. The heights play no part,
// exactly as a bar's height plays none: a sample cannot move a cell across a
// grid line.
//
// The two triangles *within* one cell are the one thing this does not order,
// and they need no ordering: they meet along a diagonal and nowhere else, so
// they cannot interpenetrate, and two non-interpenetrating convex polygons
// have no cyclic overlap to resolve.
void surface_draw_order(const SurfacePlot& s, const Projector3D& proj,
                        std::vector<std::size_t>& out);

// How far one surface's centre is from the eye. Two surfaces in an axes have
// no separating plane between them, so there is no exact order for the pair --
// this is the admitted heuristic both paths order whole objects by, and it is
// one place rather than two so they at least agree on which heuristic.
// bar3d_plot_distance()'s counterpart, and measured the same way (a box-space
// distance from eye_coord()) so a surface and a bar grid can be interleaved.
double surface_plot_distance(const SurfacePlot& s, const Projector3D& proj);

// One cell, projected, ready to emit. Bar3DPolygon's sibling rather than the
// same type: they carry the same fields because both are "a projected polygon
// with a fill, a stroke, an object depth and a provenance", and they are kept
// apart because `cell` and `bar` index different things and a merged type
// would have to be read twice to know which. If a third kind ever wants it,
// that is the moment to extract one.
struct Surface3DPolygon {
    std::vector<float> xy;              // pixel ring, x,y pairs
    // The same ring in *box* space, for the same reason Bar3DPolygon carries
    // one: Newell's algorithm (step 9) works there.
    std::vector<Vec3> box;
    Color fill{ 0, 0, 0, 1 };           // already shaded and alpha'd; unused when !filled
    Color stroke{ 0, 0, 0, 1 };
    float stroke_width = 0.0f;          // 0 = no wireframe
    // false = one wireframe edge: `box` and `xy` are two points, an open line
    // with no fill. Never a closed ring -- see plan_surfaces3d().
    bool  filled = true;
    float depth = 0.0f;                 // the cell's own centroid depth
    // The owning surface's distance from the eye, repeated on every polygon so
    // the SVG writer can merge this plan against the bar and plane plans
    // without re-deriving it. Whole objects interleave; cells within one do not.
    float plot_depth = 0.0f;
    // Which cell this came from, as (plot object, flat cell index). The plan is
    // the only account of the scene its consumer gets, and without this the
    // order it is put in cannot be checked from outside -- the lesson step 4's
    // painter sort taught at some cost.
    std::size_t plot = 0, cell = 0;
};

// Every cell of every surface, projected to pixels and ordered back to front --
// the painter's algorithm SVG needs because it has no depth buffer
// (memory/spec_3d.md §10). A cell is two triangles on the diagonal the raster
// path splits on, followed -- when the wireframe is on -- by the cell edges it
// owns as two-point segments: each edge of the grid exactly once, with the
// later-drawn of the two cells that share it.
//
// Anything behind the eye is dropped by the projector's polygon clip.
std::vector<Surface3DPolygon> plan_surfaces3d(const Projector3D& proj,
                                              const std::vector<SurfacePlot>& surfaces);

} // namespace sextant
