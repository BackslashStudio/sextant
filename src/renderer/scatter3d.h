#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "marker_shape.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sextant {

// Rules both outputs share for a 3D cloud: point color, depth shade and order.
// Marker shapes are marker_shape()'s; the billboard is in the vertex shader.

// True when the cloud must be composited (alpha < 1); `colors` has no alpha.
inline bool scatter3d_translucent(const Scatter3DPlot& s) {
    return s.opts.alpha < 1.0f || (!s.colormapped() && s.opts.color.a < 1.0f);
}

inline float scatter3d_alpha(const Scatter3DPlot& s) {
    const float base = s.colormapped() ? 1.0f : s.opts.color.a;
    return std::clamp(base * s.opts.alpha, 0.0f, 1.0f);
}

// One point's color before the depth shade (flat or colormapped).
Color scatter3d_point_color(const Scatter3DPlot& s, std::size_t i,
                            double vmin, double vmax);

// The depth cue: see depth_shade(). The GPU computes it in the vertex shader
// from box_depth_range(), so an orbit re-uploads nothing.
inline Color scatter3d_depth_shade(Color c, float depthshade, float t) {
    return depth_shade(c, depthshade, t);
}

// One projected marker for the SVG path.
struct Scatter3DMarker {
    float cx = 0.0f, cy = 0.0f;   // pixel centre, in the figure's coordinates
    float radius = 0.0f;          // half-extent in pixels; `size` is a diameter
    MarkerStyle marker = MarkerStyle::Circle;
    // Final color (colormap and depth shade applied).
    Color color{};
    // Px3::depth (for the whole-object merge) and box position (for pairwise
    // tests).
    float depth = 0.0f;
    Vec3  box{};
    std::size_t plot = 0, index = 0;
};

// Every drawn marker, far to near; points behind a perspective eye are
// dropped. The sort is exact for markers against each other.
std::vector<Scatter3DMarker> plan_scatter3d(const Projector3D& proj,
                                            const std::vector<Scatter3DPlot>& points);

// The cloud's bounding-box centre distance, comparable with the other kinds'.
double scatter3d_plot_distance(const Scatter3DPlot& s, const Projector3D& proj);

// Back-to-front point order. Exact: flat billboards can't interpenetrate.
void scatter3d_draw_order(const Scatter3DPlot& s, const Projector3D& proj,
                          std::vector<std::size_t>& out);

} // namespace sextant
