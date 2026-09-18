#pragma once
#include "../coord_transform.h"
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include <cstdint>
#include <string>
#include <vector>

namespace sextant {
    // A 2D plane in the 3D scene, reduced to what each output path draws. Shared by
    // DataRenderer and the SVG writer so both place a plane's contents identically.

    // A heatmap's four corners on a plane, in parent data space (the GPU buffer
    // survives limit changes and orbits), with texture coordinates. Ring order and
    // uv match DataRenderer::draw_heatmap(): corner 0 is (xrange.lo, yrange.lo) at
    // uv (0,1); v = 0 is the first uploaded row.
    struct PlaneQuad {
        Vec3 p[4];
        float uv[4][2];
        // The plane's normal axis index (0/1/2), where the offset was applied.
        int normal_axis = 2;
    };

    PlaneQuad plane_heatmap_quad(const HeatmapPlot& hp, PlaneOrientation orient,
                                 double offset);

    // ---------------------------------------------------------------------------
    // The plane's raster
    // ---------------------------------------------------------------------------
    // A plane's contents are rendered in 2D painter order into their own
    // framebuffer and enter the scene as one textured quad. The raster spans the
    // parent's limits (the box face), so content outside them is clipped. No GL
    // here, so it is testable without a context.
    struct PlaneRaster {
        // Raster size in logical pixels: the size the plane would have filling the
        // subplot frame. Pixel sizes on a plane (linewidth, marker size) are pixels
        // of this raster. Camera-independent.
        int w = 1, h = 1;

        // The plane's 2D transform (parent limits on the in-plane axes onto
        // [0,w] x [0,h]), used by the ordinary 2D draw calls.
        CoordTransform tr;

        // The quad in parent data space; corner 0 is (u_min, v_min), running u
        // then v.
        Vec3 p[4];
        float uv[4][2];
        int normal_axis = 2;

        // Box units per raster pixel (square). Shared with the SVG writer so stroke
        // widths on a plane match in both outputs.
        double box_per_px = 1.0;
    };

    // The raster's longer side in logical pixels, from the frame.
    int plane_raster_cap(const PlotRect& frame);

    PlaneRaster plane_raster(const Transform3D& tf, PlaneOrientation orient,
                             double offset, int max_dim);

    // Preferred overload: the cap comes from the projector's frame.
    PlaneRaster plane_raster(const Projector3D& proj, PlaneOrientation orient,
                             double offset);

    // Colormapped RGBA, rows top-down with the origin flip applied (row 0 is the
    // yrange.hi edge), for both the texture upload and the SVG <image>.
    std::vector<std::uint8_t> plane_heatmap_rgba(const HeatmapPlot& hp);

    // ---------------------------------------------------------------------------
    // The other 2D kinds as primitives in parent data space
    // ---------------------------------------------------------------------------
    //   Tri     a filled area in the plane      -- bar bodies, error-bar boxes
    //   Seg     a stroke in the scene           -- lines, bar edges, whiskers
    //   Marker  a symbol                        -- scatter and scatter_z points
    //
    // A Seg's width is pixels at the box centre, converted to a box length.
    struct PlaneGeometry {
        struct Tri {
            Vec3 p[3];
            Color fill;
        };

        struct Seg {
            Vec3 a, b;
            Color color;
            float width_px = 1.0f;
        };

        struct Marker {
            Vec3 p;
            Color color;
            float size_px = 1.0f;
            MarkerStyle marker = MarkerStyle::Circle;
        };

        std::vector<Tri> tris;
        std::vector<Seg> segs;
        std::vector<Marker> markers;

        // The 2D painter order as consecutive ranges into the three lists (fills
        // and strokes alternate, and coplanar primitives can't be depth-sorted).
        struct Batch {
            enum class Kind { Tri, Seg, Marker } kind = Kind::Tri;

            std::size_t begin = 0, end = 0;
        };

        std::vector<Batch> batches;

        bool empty() const { return batches.empty(); }
    };

    // Everything on `p` except its heatmaps, in 2D painter order. Used by the SVG
    // writer only; the raster path draws into the plane's framebuffer instead.
    // Markers are drawn into the plane, so they foreshorten with it; `size` is in
    // raster pixels.
    PlaneGeometry plane_geometry(const PlaneSnapshot& p);

    // Distance of the plane's centre from the eye; the whole-object heuristic,
    // comparable with bar3d_plot_distance().
    double plane_distance(const PlaneSnapshot& p, const Projector3D& proj);

    // True when the plane must be drawn back to front.
    bool plane_translucent(const PlaneSnapshot& p);

    // ---------------------------------------------------------------------------
    // SVG: the warp problem
    // ---------------------------------------------------------------------------
    // SVG transforms are affine, so:
    //   Orthographic: <image> with matrix(...) is exact.
    //   Perspective:  one <polygon> per cell (exact), up to kPlane3DCellCap; above
    //                 that, an affine <image> from three corners, with `warning`.
    // Fills, strokes and markers project to ordinary pixel elements either way.
    struct PlanePlanItem {
        // Which form this item took; exactly one payload is populated. One ordered
        // list because the order runs across forms (layers of one picture).
        enum class Form { Image, Polys, Strokes, Markers };

        Form form = Form::Image;

        // Form::Image -- colormapped pixels, plus the matrix(a b c d e f) mapping
        // the unit image square (0,0 top-left) onto figure pixels.
        std::vector<std::uint8_t> rgba;
        int rows = 0, cols = 0;
        float matrix[6] = {1, 0, 0, 1, 0, 0};

        // Form::Polys -- filled pixel rings, near-plane clipped (perspective
        // heatmap cells and the plane's filled areas).
        struct Poly {
            std::vector<float> xy;
            Color fill;
        };

        std::vector<Poly> polys;

        // Form::Strokes -- open pixel polylines, one width each.
        struct Stroke {
            std::vector<float> xy;
            Color color;
            float width = 1.0f;
        };

        std::vector<Stroke> strokes;

        // Form::Markers -- a symbol at a pixel, at its pixel size.
        struct Mark {
            float x = 0, y = 0, size = 0;
            Color color;
            MarkerStyle marker = MarkerStyle::Circle;
        };

        std::vector<Mark> marks;

        // Whole-plane opacity; per-primitive colors carry only their own alpha.
        float alpha = 1.0f;

        // The plane's distance from the eye (larger is further); the same for all
        // of a plane's items, so a stable sort keeps them together.
        float depth = 0.0f;

        // Provenance (plane, plot object), so the order can be checked.
        std::size_t plane = 0, plot = 0;

        // The plane's quad in box space, which the painter orders and splits the
        // plane by; split pieces are drawn through the piece's outline.
        Vec3 quad[4]{};

        // Set when the cell cap forced the affine fallback; written as an XML
        // comment.
        std::string warning;
    };

    // Perspective planes with more cells than this fall back to an affine <image>
    // (128x128 stays exact, 512x512 doesn't).
    inline constexpr std::size_t kPlane3DCellCap = 20000;

    // Every plane's items, ordered back to front by plane distance, 2D painter
    // order within a plane. Empty planes produce nothing.
    std::vector<PlanePlanItem> plan_planes3d(const Projector3D& proj,
                                             const std::vector<PlaneSnapshot>& planes);
} // namespace sextant
