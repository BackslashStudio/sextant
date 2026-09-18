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
    // Newell's algorithm: an emission order for a 3D scene without a depth buffer
    // (the SVG writer). Depth order isn't a function of one point per primitive and
    // can be cyclic, so it uses pairwise tests and, when they all fail, splits one
    // polygon along the other's plane.

    // One convex polygon of the scene in box space, with its provenance.
    struct PaintPoly {
        // Convex and flat with 3+ points; exactly two = a stroke (ordered and cut,
        // never a blade); exactly one = a scatter3d marker (footprint from
        // `radius`). The source of truth: a split rewrites it.
        std::vector<Vec3> ring;

        // Provenance (kind, object, element): finds the payload after a split and
        // makes the order checkable.
        enum class Kind { Plane, Bar, Surface, Scatter, Line, Mesh, ErrorBar };

        Kind kind = Kind::Bar;
        std::size_t object = 0; // index within its kind
        std::size_t element = 0; // bar face, surface cell, mesh face; unused for a plane

        // Position in the object's own plan order; only a tie-break for the initial
        // sort, for deterministic output. Objects are not treated as leaves: plain
        // Newell handles intra-object pairs cheaply, and the leaf shortcut caused
        // needless splits.
        std::size_t rank = 0;

        // Index into the caller's list; both halves of a split keep it.
        std::size_t source = 0;

        // True on both halves of a split: emit the new ring, not the plan's item.
        bool split = false;

        // Point rings only: the marker's half-extent in pixels. Zero otherwise.
        float radius = 0.0f;

        // Derived from `ring`: `dmin`/`dmax` = Px3::depth extent; `px` = projected,
        // near-clipped ring as x,y pairs; `bb` = screen bounding box x0,y0,x1,y1.
        float dmin = 0.0f, dmax = 0.0f;
        std::vector<float> px;
        float bb[4] = {0, 0, 0, 0};

        // Stable name assigned by paint_order(), used by the screen-space bucket
        // grid (list positions change on promotion and split).
        std::uint32_t id = 0;
    };

    // What one ordering run did.
    struct PaintOrderStats {
        std::size_t input = 0; // polygons in
        std::size_t output = 0; // polygons out, pieces included
        std::size_t splits = 0; // splits performed
        std::size_t tests = 0; // pairwise comparisons that got past the depth extent
        std::size_t cycles = 0; // conflicts that could only be resolved by splitting
        // Conflicts with nothing to cut (neither straddles the other); keeps the
        // depth sort's answer. A few are normal (coplanar or touching geometry).
        std::size_t unresolved = 0;

        // Which bound stopped the run, so the warning names the right option.
        bool bailed_on_splits = false;

        // The run hit its work limit: the rest is in plain depth order, so the
        // result is complete but not exact.
        bool bailed = false;
    };

    // The emission order, back to front. `polys` is consumed; the result includes
    // split pieces. Rings with fewer than two points, or nothing in front of a
    // perspective eye, are dropped. `max_work` bounds tests plus splits (wall
    // clock); `max_splits` bounds splits alone. 0 = default for each.
    std::vector<PaintPoly> paint_order(std::vector<PaintPoly> polys,
                                       const Projector3D& proj,
                                       PaintOrderStats* stats = nullptr,
                                       std::size_t max_work = 0,
                                       std::size_t max_splits = 0);

    // Cuts `ring` by the plane (p0, n) into the front and back parts (either may be
    // empty); a two-point ring is cut as a segment. Sutherland-Hodgman run twice.
    void split_ring_by_plane(const std::vector<Vec3>& ring, Vec3 p0, Vec3 n,
                             std::vector<Vec3>& front, std::vector<Vec3>& back);

    // The ring's plane (point and unit normal) via Newell's normal formula (stable
    // for nearly collinear edges). False when degenerate: orderable, can't split.
    bool ring_plane(const std::vector<Vec3>& ring, Vec3& p0, Vec3& n);

    // Fills `p`'s derived fields. Public for tests.
    void prepare_paint_poly(PaintPoly& p, const Projector3D& proj);

    // -------------------------------------------------------------------------
    // The scene, ordered for a writer that has no camera
    // -------------------------------------------------------------------------
    // One thing to emit, referring to an item of one of the plans. Splitting needs
    // a projector, so it happens here in the plan layer; the SVG writer receives an
    // order plus the pixels of split pieces.
    struct ScenePaint {
        enum class Kind { Bar, Surface, Plane, Scatter, Line, Mesh, ErrorBar };

        Kind kind = Kind::Bar;

        // Index into that plan's vector; for Plane, the plane index (all of its
        // PlanePlanItems, in plan order).
        std::size_t index = 0;

        // Set only for split pieces: the pixel ring to draw (bars, surfaces), or
        // the clip outline (planes, since an <image> can't be cut).
        std::vector<float> xy;
    };

    // The whole scene in one back-to-front emission order, splitting where needed.
    // `max_work`/`max_splits` are SvgExportOptions' bounds (0 = automatic).
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
