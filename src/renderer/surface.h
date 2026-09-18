#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "bar3d.h"
#include <cstddef>
#include <limits>
#include <vector>

namespace sextant {
    // One surface cell: corners in data space, normal in box space (as for bar
    // faces). A sheet is two-sided, so shading uses the normal's absolute value.
    struct SurfaceCell {
        Vec3 p[4]; // corners, in ring order: (i,j) (i+1,j) (i+1,j+1) (i,j+1)
        Vec3 normal; // unit, box space, sign arbitrary
        float shade = 1.0f; // multiplies the cell's colour
        double value = 0.0; // the height the colormap is sampled at
    };

    // Moller-Trumbore ray/triangle test shared by grid and mesh surfaces;
    // returns the hit parameter or kTriRayMiss. Hits may be negative (under
    // orthographic the ray origin is inside the scene).
    inline constexpr double kTriRayMiss = -std::numeric_limits<double>::max();

    double tri_ray_t(const Projector3D::Ray3& r, Vec3 a, Vec3 b, Vec3 c);

    // Cell (i, j), addressed by its low corner. `tf` only puts the normal in box
    // space. `value` is the mean of the four corner heights.
    void surface_cell(const SurfacePlot& s, std::size_t i, std::size_t j,
                      const Transform3D& tf, SurfaceCell& out);

    // Ray/cell intersection depth (Px3::depth, comparable with the other hit
    // tests). `sample` is the nearest of the cell's four samples. Split on the same
    // 0-2 diagonal as the raster path. False on a miss or behind the eye.
    bool surface_ray_hit(const SurfacePlot& s, std::size_t k, const Projector3D& proj,
                         float px, float py, float& depth, std::size_t& sample);

    // A cell's final color (flat or colormapped, shaded, with alpha). Shared by
    // PNG and SVG.
    Color surface_cell_color(const SurfacePlot& s, const SurfaceCell& c,
                             double vmin, double vmax);

    // True when the surface must be drawn back to front (alpha < 1).
    inline bool surface_translucent(const SurfacePlot& s) {
        return s.opts.color.a * s.opts.alpha < 1.0f || s.opts.alpha < 1.0f;
    }

    inline float surface_alpha(const SurfacePlot& s) {
        return std::clamp(s.opts.alpha, 0.0f, 1.0f);
    }

    inline float surface_edge_alpha(const SurfacePlot& s) {
        return std::clamp(s.opts.edgecolor.a * s.opts.edge_alpha, 0.0f, 1.0f);
    }

    // The four edges of one cell as data-space endpoint pairs. The GPU draws shared
    // edges twice (depth test keeps one); plan_surfaces3d() emits each once.
    void surface_cell_edges(const SurfaceCell& c, Vec3 out[4][2]);

    // Back-to-front cell order (k = i * cell_cols() + j). Exact for the same reason
    // as bar3d_draw_order(); a cell's two triangles need no ordering.
    void surface_draw_order(const SurfacePlot& s, const Projector3D& proj,
                            std::vector<std::size_t>& out);

    // Distance of the surface's centre from the eye; the whole-object heuristic,
    // comparable with bar3d_plot_distance().
    double surface_plot_distance(const SurfacePlot& s, const Projector3D& proj);

    // One projected cell polygon (or wireframe edge) for the SVG path.
    struct Surface3DPolygon {
        std::vector<float> xy; // pixel ring, x,y pairs
        // The ring in box space, for Newell's algorithm.
        std::vector<Vec3> box;
        Color fill{0, 0, 0, 1}; // already shaded and alpha'd; unused when !filled
        Color stroke{0, 0, 0, 1};
        float stroke_width = 0.0f; // 0 = no wireframe
        // False for a wireframe edge: two points, no fill.
        bool filled = true;
        float depth = 0.0f; // the cell's own centroid depth
        // The owning surface's distance, so the SVG writer can merge plans.
        float plot_depth = 0.0f;
        // Provenance (plot, flat cell index), so the order can be checked.
        std::size_t plot = 0, cell = 0;
    };

    // Every cell, projected and ordered back to front: two triangles on the raster
    // diagonal, then (with the wireframe) the cell's owned edges, each grid edge
    // once. Anything behind the eye is clipped.
    std::vector<Surface3DPolygon> plan_surfaces3d(const Projector3D& proj,
                                                  const std::vector<SurfacePlot>& surfaces);
} // namespace sextant
