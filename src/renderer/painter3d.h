#pragma once
#include "../coord_transform3d.h"
#include "bar3d.h"
#include "surface.h"
#include "surface_tri.h"
#include "plane2d.h"
#include "scatter3d.h"
#include "line3d.h"
#include "error_bar3d.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sextant {

// Newell's algorithm: an emission order for a 3D scene that has no depth
// buffer (v1.0 step 9).
//
// **This is not a sort, and that is the whole point.** Depth order between two
// primitives is not a function of a point on either of them -- a large tilted
// quad and a small one near its far edge put the large one's centroid nearer
// while the small one is in front where they actually overlap -- and it is not
// even a total order, since three quads in a pinwheel give a cycle. So no key
// exists to sort by, at any granularity. Newell's answer is a ladder of
// *pairwise* tests and, when every one of them fails, a **split**: the pair
// that could not be separated is made separable by cutting one of them along
// the other's plane. The splitting is not an optimisation. It is what turns a
// relation that is not a total order into one, and it is why the pinwheel has
// an answer here at all.
//
// The raster path does not need this -- a depth buffer resolves the same
// question per fragment for nothing -- so its one consumer is the SVG writer,
// where memory/spec_3d.md §10's known flaw lives.

// One convex polygon of the scene, in box space, with where it came from.
//
// Box space and not data space: `Projector3D` works there, `Px3::depth` is
// defined there, and a plane's normal means something there. Data space would
// need the axis scales folded into every dot product.
struct PaintPoly {
    // Convex and flat, at least three points, in box space -- or exactly two,
    // which is a stroke: ordered and cut like a polygon, never a blade -- or
    // exactly one, which is a scatter3d marker: a *symbol*, with no extent in
    // the scene at all, so it can be neither a blade nor a victim and its
    // screen footprint comes from `radius` below. The source of truth: a
    // split rewrites this and everything else about the polygon is derived.
    std::vector<Vec3> ring;

    // Provenance, as (kind, object, element). Carried for two reasons. It is
    // what lets a consumer find the payload a piece belongs to after a split,
    // and it is what makes the emission order checkable from outside -- an
    // order whose output cannot say what it came from can only be checked
    // against the thing that produced it.
    enum class Kind { Plane, Bar, Surface, Scatter, Line, Mesh, ErrorBar };
    Kind        kind    = Kind::Bar;
    std::size_t object  = 0;    // index within its kind
    std::size_t element = 0;    // bar face, surface cell, mesh face; unused for a plane

    // Where this polygon sits in its own object's internal order, which is the
    // order `plan_bars3d()` and `plan_surfaces3d()` emitted it in. Only a tie
    // break for the initial sort, so that two polygons at equal depth come out
    // in a stable order and one file is the same file as the last.
    //
    // **It is deliberately not more than that.** The step's plan proposed
    // keeping each object as a *leaf* -- never comparing two polygons of one
    // bar grid, since `bar3d_draw_order()` and `surface_draw_order()` already
    // answer for them exactly -- and that turned out to be an optimisation
    // this algorithm cannot safely take. Newell's first step sorts the whole
    // list by depth, which does not preserve any object's internal order, so
    // the shortcut has to be propped up by a rule about which promotions are
    // allowed; and that rule misfires constantly, marks polygons as promoted
    // that were not, and drives the run straight into splits it does not need.
    // A five-by-five grid and one plane produced 673 of them.
    //
    // Plain Newell handles those pairs perfectly well and cheaply: two faces
    // of one convex bar are separated by test 3, two cells of a grid by the
    // axis-aligned plane between them, and two adjacent cells never overlap in
    // projection at all. The grid orders remain, for the raster path and as
    // the input order here; they are simply not a special case.
    std::size_t rank    = 0;

    // Index into the caller's original list, so a payload survives a split.
    // Both halves of a split polygon carry the parent's value.
    std::size_t source  = 0;

    // True on both halves of a split, and on nothing else. A consumer needs
    // it because the two cases are emitted differently: an unsplit polygon is
    // the plan's own item, byte for byte, while a piece has to be drawn as the
    // new ring the split produced.
    bool split = false;

    // **Point rings only: the marker's half-extent in pixels.** A one-point
    // ring has no shape of its own in the scene -- it is a symbol drawn at a
    // pixel -- so this is the only thing that says how much of the screen it
    // covers, and the screen bounding box below is built from it. Zero for
    // every other kind, which have their footprint in the ring itself.
    float radius = 0.0f;

    // Derived, and refreshed whenever `ring` changes. `dmin`/`dmax` are the
    // Px3::depth extent (box units from the camera, larger is further); `px`
    // is the projected ring as x,y pairs, near-plane clipped; `bb` is its
    // screen bounding box as x0,y0,x1,y1.
    float dmin = 0.0f, dmax = 0.0f;
    std::vector<float> px;
    float bb[4] = { 0, 0, 0, 0 };

    // Assigned by paint_order() and meaningless before it: a name that
    // survives the permutations the algorithm makes. The screen-space bucket
    // grid holds these rather than list positions, because a promotion rotates
    // a span of the list and a split re-sorts its tail, and an index of
    // positions would be stale after either.
    std::uint32_t id = 0;
};

// What one ordering run did. Reported rather than logged because the step's
// viability is a number: `splits` is the geometry this mechanism adds, and
// `bailed` is the admission that a scene was too big for it.
struct PaintOrderStats {
    std::size_t input  = 0;     // polygons in
    std::size_t output = 0;     // polygons out, pieces included
    std::size_t splits = 0;     // splits performed
    std::size_t tests  = 0;     // pairwise comparisons that got past the depth extent
    std::size_t cycles = 0;     // conflicts that could only be resolved by splitting
    // Conflicts where neither polygon straddles the other, so there was
    // nothing to cut. The pair keeps the depth sort's answer. A handful is
    // ordinary -- coplanar or touching geometry, where either order draws the
    // same picture -- and a lot of them means the scene is not being answered.
    std::size_t unresolved = 0;

    // Which of the two bounds stopped the run, when one did. The painter is
    // the only thing that knows -- `splits >= max_splits` cannot be inferred
    // afterwards, because the automatic split bound is derived from the input
    // size and a caller who set neither has no number to compare against. A
    // warning that names the wrong knob is worse than one that names none.
    bool bailed_on_splits = false;

    // True when the run hit its work limit and gave up. The result is still a
    // complete emission order -- the remaining polygons come out in depth
    // order, which is what the whole-object path would have produced anyway --
    // but it is no longer exact, and a caller that cares must say so. An
    // export that takes a minute is a worse answer than a slightly wrong
    // picture, and an export that never finishes is not an answer at all.
    bool bailed = false;
};

// The emission order, back to front: draw `out[0]` first.
//
// `polys` is taken by value and consumed; the result holds its entries and the
// pieces splitting produced. Anything with fewer than two points, or nothing
// in front of a perspective eye, is dropped.
//
// `max_work` bounds the pairwise tests plus splits before the run bails and
// `max_splits` bounds the cuts on their own, each 0 taking its default. They
// are separate because they fail differently: `max_work` is a wall-clock bound
// and `max_splits` is a bound on how much geometry this is allowed to invent,
// and it is the one that binds on a scene where two objects interleave
// everywhere. See PaintOrderStats::bailed for what bailing means.
std::vector<PaintPoly> paint_order(std::vector<PaintPoly> polys,
                                   const Projector3D& proj,
                                   PaintOrderStats* stats = nullptr,
                                   std::size_t max_work = 0,
                                   std::size_t max_splits = 0);

// Cuts `ring` by the plane through `p0` with normal `n`, into the part on the
// normal's side and the part behind it. Either may come back empty, which is
// what "the polygon did not actually straddle the plane" looks like. A
// two-point ring is cut as a line segment.
//
// Sutherland-Hodgman, run twice with the plane reversed rather than once with
// a two-sided walk: two passes of the same tested routine are worth more than
// one clever one, and the polygon is convex so both halves are convex too.
void split_ring_by_plane(const std::vector<Vec3>& ring, Vec3 p0, Vec3 n,
                         std::vector<Vec3>& front, std::vector<Vec3>& back);

// The ring's plane, as a point on it and a unit normal. False when the ring is
// degenerate -- fewer than three points, or all of them collinear -- in which
// case it can still be *ordered* but can never *split* anything.
//
// Newell's own normal formula rather than a cross product of two edges: a
// nearly-collinear pair of edges makes the cross product meaningless, while
// the summed form uses every vertex and is stable for any planar ring.
bool ring_plane(const std::vector<Vec3>& ring, Vec3& p0, Vec3& n);

// Fills the derived fields of `p` from its ring. Public because the tests
// build polygons directly and want the same preparation the algorithm uses.
void prepare_paint_poly(PaintPoly& p, const Projector3D& proj);

// -------------------------------------------------------------------------
// The scene, ordered for a writer that has no camera
// -------------------------------------------------------------------------
// One thing to emit, naming an item of one of the three existing plans rather
// than carrying a copy of it.
//
// That indirection is the whole shape of this. The SVG writer deliberately
// never sees a `Projector3D` -- it is handed plans of pixels and emits them,
// which is what keeps one projection in the library instead of two. Splitting
// needs a projector, so the split happens *here*, in the plan layer where
// `plan_bars3d()` already does its own sorting, and what reaches the writer is
// an order plus, for the pieces, the pixels they came out as.
struct ScenePaint {
    enum class Kind { Bar, Surface, Plane, Scatter, Line, Mesh, ErrorBar };
    Kind kind = Kind::Bar;

    // Bar, Surface and Scatter: an index into that plan's vector. Plane: the
    // plane's own index -- every `PlanePlanItem` whose `plane` matches, in plan order,
    // because a plane's four forms are layers of one coplanar picture and have
    // one position in the scene between them.
    std::size_t index = 0;

    // Empty unless the painter split this polygon. For a bar or a surface it
    // is the piece's own pixel ring, to be drawn instead of the plan's; for a
    // plane it is the outline the plane's items must be *clipped* to, since an
    // <image> cannot be cut but can be clipped.
    std::vector<float> xy;
};

// The whole scene in one emission order, back to front, splitting where no
// order exists otherwise.
//
// The three plans arrive already ordered internally -- and that internal order
// is exact, which is why this treats each object as a leaf (see
// PaintPoly::rank). What it adds is the order *between* objects, which nothing
// before it had: memory/spec_3d.md §10's known flaw is exactly the absence of
// one.
// `max_work` and `max_splits` are SvgExportOptions' two bounds, 0 meaning the
// automatic one. They arrive here rather than being decided here because a
// scene that needs more than the default is the caller's scene, not this
// function's -- see PaintOrderStats::bailed.
std::vector<ScenePaint> plan_scene3d(const Projector3D& proj,
                                     const std::vector<Bar3DPolygon>& bars,
                                     const std::vector<Surface3DPolygon>& surfaces,
                                     const std::vector<PlanePlanItem>& planes,
                                     const std::vector<Scatter3DMarker>& markers,
                                     const std::vector<Line3DSegment>& segments,
                                     const std::vector<SurfaceTriPolygon>& meshes,
                                     const std::vector<ErrorBar3DPolygon>& errbars,
                                     PaintOrderStats* stats = nullptr,
                                     std::size_t max_work = 0,
                                     std::size_t max_splits = 0);

} // namespace sextant
