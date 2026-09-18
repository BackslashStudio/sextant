#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "bar3d.h"
#include "surface.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sextant {
    // Rules both outputs share for a triangulated mesh: face corners, normal and
    // shade, vertex color, and order. Shares tri_ray_t() and the two-sided shading
    // rule with surface.h, nothing else.

    // True when the mesh must be composited (alpha < 1); `colors` has no alpha.
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

    // One face: corners in data space, normal in box space (as SurfaceCell), with
    // a value per vertex since mesh colors interpolate.
    struct SurfaceTriFace {
        Vec3 p[3]; // corners, in the winding `tri` gave
        std::size_t vert[3] = {0, 0, 0}; // which vertices they are
        Vec3 normal; // unit, box space, sign arbitrary
        float shade = 1.0f; // multiplies the face's colour, flat
        double value[3] = {0.0, 0.0, 0.0}; // each vertex's own colormap value
    };

    // Face `f`. Shade is two-sided |n.l| (mesh winding is often inconsistent); a
    // degenerate face takes the light head-on rather than going black.
    void surface_tri_face(const SurfaceTriPlot& s, std::size_t f,
                          const Transform3D& tf, SurfaceTriFace& out);

    // One vertex's color before the shade (flat or colormapped). Triangles
    // interpolate between vertex values.
    Color surface_tri_vertex_color(const SurfaceTriPlot& s, std::size_t i,
                                   double vmin, double vmax);

    // The vertex's normalized colormap position, unclamped (the SVG gradient fit
    // needs it affine); clamped at lookup.
    inline double surface_tri_vertex_t(const SurfaceTriPlot& s, std::size_t i,
                                       double vmin, double vmax) {
        const double span = vmax - vmin;
        return span != 0.0 ? (s.color_at(i) - vmin) / span : 0.0;
    }

    // A face's final color at a given value: colormap lookup, then shade, then
    // alpha (the shader's order).
    Color surface_tri_shaded_color(const SurfaceTriPlot& s, const SurfaceTriFace& f,
                                   double t);

    // The three edges of one face as data-space endpoint pairs. Shared edges are
    // drawn twice (no edge map); the depth test keeps one.
    void surface_tri_face_edges(const SurfaceTriFace& f, Vec3 out[3][2]);

    // Back-to-front face order by centroid depth. A heuristic (meshes can fold
    // over themselves); the raster path uses depth peeling instead.
    void surface_tri_draw_order(const SurfaceTriPlot& s, const Projector3D& proj,
                                std::vector<std::size_t>& out);

    // The mesh's bounding-box centre distance, comparable with the other kinds'.
    double surface_tri_plot_distance(const SurfaceTriPlot& s, const Projector3D& proj);

    // Ray/face intersection depth (Px3::depth, comparable with the other hit
    // tests); `vertex` is the nearest of the three. False on a miss or behind the
    // eye.
    bool surface_tri_ray_hit(const SurfaceTriPlot& s, std::size_t f,
                             const Projector3D& proj, float px, float py,
                             float& depth, std::size_t& vertex);

    // One projected face for the SVG path.
    struct SurfaceTriPolygon {
        std::vector<float> xy; // pixel ring, x,y pairs
        std::vector<Vec3> box; // the same ring in box space, for Newell
        Color fill{0, 0, 0, 1}; // already shaded and alpha'd
        Color stroke{0, 0, 0, 1};
        float stroke_width = 0.0f; // 0 = no wireframe
        bool filled = true; // false = one two-point wireframe edge

        // ---- The gradient, for a colormapped mesh -------------------------------
        // The interpolated value is affine in the triangle's plane, so under
        // orthographic one <linearGradient> per triangle is exact. Under
        // perspective it is a ratio of affine functions of pixel position:
        //
        //     t(x, y) = (na + nx*x + ny*y) / (wa + wx*x + wy*y)
        //
        // (pixels, unclamped t; all w are 1 under ortho). Stops sampled through it
        // are exact along the gradient axis. Split pieces re-derive the axis from
        // their own ring.
        bool colormapped = false;
        Colormap cmap = Colormap::Viridis;
        float na = 0.0f, nx = 0.0f, ny = 0.0f;
        float wa = 1.0f, wx = 0.0f, wy = 0.0f;
        // Gradient axis at the centroid (unit pixel direction); zero when there is
        // no screen-space gradient, meaning use the flat `fill`.
        float gx = 0.0f, gy = 0.0f;
        // Shade and alpha applied after each stop's colormap lookup.
        float shade = 1.0f;
        float alpha = 1.0f;

        float depth = 0.0f; // the face's own centroid depth
        // The owning mesh's distance, so the SVG writer can merge plans.
        float plot_depth = 0.0f;
        std::size_t plot = 0, face = 0;
    };

    // Every face, projected and ordered back to front, each followed (with the
    // wireframe) by its three edges; shared edges appear twice. Anything behind
    // the eye is clipped.
    std::vector<SurfaceTriPolygon> plan_surface_tri3d(const Projector3D& proj,
                                                      const std::vector<SurfaceTriPlot>& meshes);
} // namespace sextant
