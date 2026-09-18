#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sextant {
    // Rules both outputs share for a 3D path: point color, segment ends, ribbon
    // width and order. A path is a world-space billboarded ribbon: no dashes, and
    // it thins with distance under perspective.

    // True when the path must be composited (alpha < 1); `colors` has no alpha.
    inline bool line3d_translucent(const Line3DPlot& l) {
        return l.opts.alpha < 1.0f || (!l.colormapped() && l.opts.color.a < 1.0f);
    }

    inline float line3d_alpha(const Line3DPlot& l) {
        const float base = l.colormapped() ? 1.0f : l.opts.color.a;
        return std::clamp(base * l.opts.alpha, 0.0f, 1.0f);
    }

    // One point's color before the depth shade (flat or colormapped). Segments ramp
    // between their two points' colors (the SVG path emits a gradient for this).
    Color line3d_point_color(const Line3DPlot& l, std::size_t i, double vmin, double vmax);

    // Ribbon half-width in box units: `linewidth` pixels measured at the box
    // centre, so the whole path has one width in the scene.
    inline double line3d_half_width(const Line3DPlot& l, const Projector3D& proj) {
        return 0.5 * l.opts.linewidth * proj.box_units_per_pixel(Vec3{0.0, 0.0, 0.0});
    }

    // The path's bounding-box centre distance, comparable with the other kinds'.
    double line3d_plot_distance(const Line3DPlot& l, const Projector3D& proj);

    // Back-to-front segment order by midpoint depth. A heuristic (ribbons can
    // cross); the raster path uses depth peeling instead.
    void line3d_draw_order(const Line3DPlot& l, const Projector3D& proj,
                           std::vector<std::size_t>& out);

    // One projected segment for the SVG path: a two-point stroke rather than the
    // raster path's quad, so the painter orders it exactly (stroke_vs_polygon())
    // and never splits on it. Consequences: one width per segment (the ribbon's
    // width at its midpoint), and round caps instead of miters, so open ends
    // extend half a width and sharp corners are rounded.
    struct Line3DSegment {
        float x0 = 0.0f, y0 = 0.0f; // the two ends, in the figure's pixels
        float x1 = 0.0f, y1 = 0.0f;
        float width = 1.0f; // stroke width in pixels at the midpoint

        // End colors after colormap and depth shade; equal for a flat series.
        Color c0{}, c1{};

        // Normalized colormap values at the ends; gradients sample the map between
        // them (a two-stop ramp would leave the colormap).
        bool colormapped = false;
        Colormap cmap = Colormap::Viridis;
        float v0 = 0.0f, v1 = 0.0f;

        // Depth-shade factor at each end, applied after each gradient stop's
        // lookup (can't be recovered from near-black colors).
        float k0 = 1.0f, k1 = 1.0f;

        // Midpoint Px3::depth (whole-object merge) and box-space ends (pairwise
        // tests).
        float depth = 0.0f;
        Vec3 a{}, b{};
        std::size_t plot = 0, index = 0; // which path, which segment
    };

    // Every drawn segment, far to near by midpoint depth; segments behind a
    // perspective eye are dropped. The painter re-orders against other geometry.
    std::vector<Line3DSegment> plan_lines3d(const Projector3D& proj,
                                            const std::vector<Line3DPlot>& lines);
} // namespace sextant
