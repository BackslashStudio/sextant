#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "bar3d.h"
#include "surface.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sextant {

// A sheet on a triangulated mesh (v1.0 step 14). What this header owns is
// everything both outputs have to agree about -- a face's corners, its normal
// and shade, a vertex's colour, how the mesh is ordered -- for the reason
// bar3d.h, surface.h, scatter3d.h and line3d.h own the same for their kinds:
// two transcriptions of one rule is where the raster and the vector paths
// drift.
//
// Written against `surface.h` rather than beside it, and the difference in one
// line: a grid cell is four samples with a low corner, a mesh face is three
// vertex *indices*, and the second has no `orient`, no u/v and no Axis3Map. The
// two share the triangle ray test (tri_ray_t(), which surface.h publishes for
// exactly this) and the two-sided |n.l| shading rule, and nothing else.

// True when the mesh has to be composited rather than left to the depth
// buffer. surface_translucent()'s counterpart, and `colors` cannot make a mesh
// translucent for the same reason it cannot make a path translucent: a
// colormap has no alpha of its own.
inline bool surface_tri_translucent(const SurfaceTriPlot& s) {
    return s.opts.alpha < 1.0f || (!s.colormapped() && s.opts.color.a < 1.0f);
}

inline float surface_tri_alpha(const SurfaceTriPlot& s) {
    const float base = s.colormapped() ? 1.0f : s.opts.color.a;
    return std::clamp(base * s.opts.alpha, 0.0f, 1.0f);
}

inline float surface_tri_edge_alpha(const SurfaceTriPlot& s) {
    return std::clamp(s.opts.edgecolor.a * s.opts.edge_alpha, 0.0f, 1.0f);
}

// One face, in *data* space with a *box*-space normal -- SurfaceCell's shape
// with three corners instead of four, and the same split of spaces for the
// same reasons: the GPU buffer is held in data space (the camera and the
// limits are uniforms, so neither invalidates it) while the light is fixed in
// box space.
//
// Deliberately not SurfaceCell with a fourth corner dropped. A cell is
// addressed by its low corner on a grid and carries one `value`, a face is
// three independent vertices and carries three -- because a mesh's colour is
// per vertex and interpolates, where a cell's is flat.
struct SurfaceTriFace {
    Vec3        p[3];                // corners, in the winding `tri` gave
    std::size_t vert[3] = { 0, 0, 0 };  // which vertices they are
    Vec3        normal;              // unit, box space, sign arbitrary
    float       shade = 1.0f;        // multiplies the face's colour, flat
    double      value[3] = { 0.0, 0.0, 0.0 };  // each vertex's own colormap value
};

// Face `f`, f in [0, face_count()). `tf` is consulted only to put the normal
// in box space, which is what keeps the corners in data space.
//
// **The shade is |n.l|, not max(0, n.l)** -- surface_cell()'s rule, and it
// binds harder here. A grid surface at least has consistent winding by
// construction; a mesh read out of a file often does not, so a one-sided rule
// would black out whichever faces happened to be wound the other way. A
// degenerate (zero-area) face has no normal and takes the light head-on rather
// than going black, since black would read as a hole in the sheet -- which is
// what lets ingest keep degenerate triangles instead of dropping rows and
// disagreeing with the caller about the face count.
void surface_tri_face(const SurfaceTriPlot& s, std::size_t f,
                      const Transform3D& tf, SurfaceTriFace& out);

// One *vertex's* colour before the shade: the flat colour, or its own entry
// mapped through the colormap. The single place a mesh's colour is decided, so
// the PNG and the SVG cannot differ by a rounding rule.
//
// A vertex's, not a face's, and that is step 14's colour rule: a `colors`
// value is measured at a vertex, so a triangle interpolates between the three
// it joins rather than picking one or their mean. The raster path gets that
// from the rasterizer (a value per corner, looked up per fragment); the SVG
// path has to emit a gradient to say the same thing.
Color surface_tri_vertex_color(const SurfaceTriPlot& s, std::size_t i,
                               double vmin, double vmax);

// The vertex's normalized position in the colour scale -- what the colormap is
// actually indexed by, and deliberately *unclamped*: the SVG gradient fits an
// affine (or, under perspective, a rational) function through the three, and
// clamping first would make the thing being fitted non-affine. Clamped where
// it is looked up instead.
inline double surface_tri_vertex_t(const SurfaceTriPlot& s, std::size_t i,
                                   double vmin, double vmax) {
    const double span = vmax - vmin;
    return span != 0.0 ? (s.color_at(i) - vmin) / span : 0.0;
}

// The finished colour of one face at a given value -- the colormap lookup,
// then the face's flat shade, then the mesh's alpha. The order matters and is
// the shader's: the map is sampled first and the result darkened, never the
// other way round.
Color surface_tri_shaded_color(const SurfaceTriPlot& s, const SurfaceTriFace& f,
                               double t);

// The three edges of one face, as pairs of data-space endpoints.
//
// **All three, every face** -- so an interior edge shared by two faces is
// drawn twice. That is surface_cell_edges()' bargain, and here it is the only
// available one: a grid knows which of its four neighbours exist from the cell
// index alone, while finding a mesh's shared edges means building an edge map
// over the whole topology. The duplicate lands at the same depth as the
// original, so GL_LESS or a peel pass keeps one of the two; in the SVG the
// second is a stroke over an identical stroke.
void surface_tri_face_edges(const SurfaceTriFace& f, Vec3 out[3][2]);

// The order one mesh's faces must be drawn in, back to front, as face indices.
//
// **A heuristic, unlike surface_draw_order()** -- and the difference is the
// whole reason a mesh is a separate kind rather than a grid with holes. A
// grid's cells lie between consecutive u and v lines, so any two are separated
// by an axis-aligned plane and the order is *exact*. Mesh faces have no such
// plane, and a mesh can fold over and occlude itself, so ordering faces by one
// number each is an approximation on line3d_draw_order()'s terms. The exact
// answer for the raster path is depth peeling, which does not consult this at
// all; this is what the sorted fallback uses.
//
// The centroid's depth rather than the nearest or farthest corner's: it is the
// one choice of the three that is symmetric in the face's three vertices, so
// re-winding a triangle cannot change its drawn order.
void surface_tri_draw_order(const SurfaceTriPlot& s, const Projector3D& proj,
                            std::vector<std::size_t>& out);

// How far one mesh's centre is from the eye, measured exactly as a bar grid's,
// a surface's, a cloud's and a path's are -- the centre of its own bounding
// box, so all five compare directly and can be interleaved.
double surface_tri_plot_distance(const SurfaceTriPlot& s, const Projector3D& proj);

// Where a pixel's ray meets face `f`, as `depth` in Px3's own quantity so the
// answer compares directly against plane_ray_hit()'s, bar3d_ray_hit()'s and
// surface_ray_hit()'s -- which is what lets one hover hint order every kind in
// a single list. `vertex` comes back as the nearest of the face's three, since
// that is what a caller's `hint_labels` entry is written about.
//
// One triangle test where surface_ray_hit() does two, through the same
// tri_ray_t(): a mesh face *is* the triangle the raster path draws, so there
// is no diagonal to agree about.
//
// False when the ray misses the face or the hit is behind a perspective eye.
bool surface_tri_ray_hit(const SurfaceTriPlot& s, std::size_t f,
                         const Projector3D& proj, float px, float py,
                         float& depth, std::size_t& vertex);

// One face, projected, ready to emit -- Surface3DPolygon's sibling, and kept
// apart from it for the reason that type is kept apart from Bar3DPolygon:
// `face` and `cell` index different things, and the gradient fields below have
// no counterpart on a grid cell, whose colour is flat.
struct SurfaceTriPolygon {
    std::vector<float> xy;              // pixel ring, x,y pairs
    std::vector<Vec3>  box;             // the same ring in box space, for Newell
    Color fill{ 0, 0, 0, 1 };           // already shaded and alpha'd
    Color stroke{ 0, 0, 0, 1 };
    float stroke_width = 0.0f;          // 0 = no wireframe
    bool  filled = true;                // false = one two-point wireframe edge

    // ---- The gradient, for a colormapped mesh -------------------------------
    //
    // A scalar interpolated barycentrically is an *affine* function of position
    // in the triangle's plane, so its iso-lines are straight and parallel; an
    // orthographic projection is affine too, so they stay that way on screen.
    // That is exactly what a `<linearGradient>` paints, which is why one
    // gradient per triangle reproduces the raster picture *exactly* under
    // ortho -- and why `<meshgradient>` (dropped from the SVG 2 candidate
    // recommendation, rendered by no major browser) would be a Coons-patch
    // mechanism spent on a field that only ever varies along one axis.
    //
    // Under perspective the map is projective rather than affine: it still
    // takes lines to lines, so the iso-lines stay straight, but parallels
    // become concurrent and fan toward a vanishing point. The exact screen-
    // space form of a quantity that is linear in the scene is then a *ratio*
    // of two affine functions -- the familiar perspective-correct
    // interpolation -- so that is what is carried:
    //
    //     t(x, y) = (na + nx*x + ny*y) / (wa + wx*x + wy*y)
    //
    // with (x, y) in the figure's own pixels and `t` the unclamped normalized
    // colour value. Under ortho every w is 1, the denominator is constant, and
    // this degenerates to the affine case with no special path. Sampling the
    // stops through it makes the ramp exact *along* the gradient axis in both
    // projections, leaving only the cross-axis fan, which is second order and
    // bounded by the foreshortening across one triangle.
    //
    // **A split piece re-derives its axis from its own ring**, which is what
    // this form is for: the field is defined over the whole plane, so a piece
    // needs no sub-range recovery of the kind a cut Line3DSegment needs -- but
    // it does have to actually be re-derived, or every cut triangle carries its
    // parent's full ramp compressed into a fragment of itself.
    bool     colormapped = false;
    Colormap cmap = Colormap::Viridis;
    float    na = 0.0f, nx = 0.0f, ny = 0.0f;
    float    wa = 1.0f, wx = 0.0f, wy = 0.0f;
    // The gradient axis at the face's centroid, as a unit pixel direction.
    // Zero when the face has no screen-space gradient at all -- edge-on, or
    // three equal values -- which is the writer's signal to fall back to the
    // flat `fill`.
    float    gx = 0.0f, gy = 0.0f;
    // The flat shade and alpha a gradient stop needs *after* its own colormap
    // lookup. Carried rather than recovered from `fill`, for the reason
    // Line3DSegment carries `k0`/`k1`: dividing a shaded colour by its unshaded
    // one fails exactly where the colour is near black.
    float    shade = 1.0f;
    float    alpha = 1.0f;

    float depth = 0.0f;                 // the face's own centroid depth
    // The owning mesh's distance from the eye, repeated on every polygon so the
    // SVG writer can merge this plan against the others without re-deriving it.
    float plot_depth = 0.0f;
    std::size_t plot = 0, face = 0;
};

// Every face of every mesh, projected to pixels and ordered back to front --
// the painter's algorithm SVG needs because it has no depth buffer
// (memory/spec_3d.md §10). A face is one triangle, followed -- when the
// wireframe is on -- by its three edges as two-point segments.
//
// Unlike plan_surfaces3d()'s wireframe, every edge is emitted by every face
// that owns it, so an interior edge appears twice. A grid cell can name its
// four neighbours from its own index; a mesh face cannot name the faces across
// its edges without an edge map over the whole topology, and a stroke drawn
// twice over itself is the same picture.
//
// Anything behind the eye is dropped by the projector's polygon clip.
std::vector<SurfaceTriPolygon> plan_surface_tri3d(const Projector3D& proj,
                                                 const std::vector<SurfaceTriPlot>& meshes);

} // namespace sextant
