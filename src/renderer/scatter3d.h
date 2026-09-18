#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "marker_shape.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sextant {

// A cloud of markers in the scene (v1.0 step 12). What this header owns is
// everything both outputs have to agree about -- a point's colour, its depth
// shade, and how the cloud is ordered -- for the reason bar3d.h and surface.h
// own the same for their kinds: two transcriptions of one rule is where the
// raster and the vector paths drift.
//
// What is *not* here is the marker's shape, which marker_shape() already owns
// for three consumers, and the billboard that gives it a pixel size, which is
// four lines of the vertex shader and has no CPU counterpart -- the SVG writer
// draws a marker at a projected pixel, where a pixel size is simply the size.

// True when the cloud has to be composited rather than left to the depth
// buffer. bar3d_translucent()'s counterpart, and `colors` cannot make a series
// translucent: a colormap has no alpha of its own, so `alpha` and the flat
// colour's own are the whole of it.
inline bool scatter3d_translucent(const Scatter3DPlot& s) {
    return s.opts.alpha < 1.0f || (!s.colormapped() && s.opts.color.a < 1.0f);
}

inline float scatter3d_alpha(const Scatter3DPlot& s) {
    const float base = s.colormapped() ? 1.0f : s.opts.color.a;
    return std::clamp(base * s.opts.alpha, 0.0f, 1.0f);
}

// One point's colour before the depth shade: the flat colour, or its own entry
// mapped through the colormap. The single place a marker's colour is decided,
// so the PNG and the SVG cannot differ by a rounding rule in the lookup.
Color scatter3d_point_color(const Scatter3DPlot& s, std::size_t i,
                            double vmin, double vmax);

// The depth cue, and the one definition of it: `t` is 0 at the near face of
// the box and 1 at the far one, and the colour is mixed toward black by
// `depthshade * t` -- exactly what SurfaceOptions::shading does to a cell,
// keyed on distance rather than on a normal.
//
// The GPU computes this in the vertex shader from the same two numbers
// box_depth_range() hands the CPU, rather than baking it into the instance
// buffer, and that is the whole reason the cue is free: it is a uniform, so
// an orbit re-uploads nothing.
//
// Alpha is untouched. Fading it instead would be matplotlib's depthshade and
// would put every shaded cloud into the translucent pass -- a cost paid for a
// cue rather than for a picture (see memory/spec_impl.md step 12.4).
// The body moved to depth_shade() in coord_transform3d.h when line3d became
// the second kind to need it (v1.0 step 13); the name stays because a cloud's
// callers and checks read better for it, and because one body behind two names
// is the point -- two kinds in one scene must shade on one scale.
inline Color scatter3d_depth_shade(Color c, float depthshade, float t) {
    return depth_shade(c, depthshade, t);
}


// One marker, projected and ready to emit: the SVG writer's view of a cloud,
// and the painter's. Bar3DPolygon's and Surface3DPolygon's sibling, and
// deliberately not either of them -- those carry a ring, and a marker has no
// ring to carry. What it has is a pixel, a size and a colour.
struct Scatter3DMarker {
    float cx = 0.0f, cy = 0.0f;   // pixel centre, in the figure's coordinates
    float radius = 0.0f;          // half-extent in pixels; `size` is a diameter
    MarkerStyle marker = MarkerStyle::Circle;
    // Already through the colormap *and* the depth shade, so the writer emits
    // a colour rather than deciding one -- the property that keeps the two
    // outputs from disagreeing about a ramp.
    Color color{};
    // Px3::depth, and the box-space position the painter orders on. Both,
    // because the depth is what the whole-object fallback merges by and the
    // position is what the pairwise tests need.
    float depth = 0.0f;
    Vec3  box{};
    std::size_t plot = 0, index = 0;
};

// Every drawn marker of every cloud, far to near. Points behind a perspective
// eye are dropped here rather than downstream, which is the same thing
// plan_box3d() does with a tick behind the eye.
//
// Sorted, and that sort is exact: a marker is a flat symbol at one depth, so
// ordering them by it cannot be wrong the way ordering two surfaces by a
// single number can. The painter re-derives the order for the pairs that also
// involve geometry; this is what settles markers against each other.
std::vector<Scatter3DMarker> plan_scatter3d(const Projector3D& proj,
                                            const std::vector<Scatter3DPlot>& points);

// Where the cloud sits, for the whole-object order the non-peeled fallback
// uses: the centre of its own bounding box, which is what a bar grid's and a
// surface's distance are, so the three compare directly.
double scatter3d_plot_distance(const Scatter3DPlot& s, const Projector3D& proj);

// The cloud's points, back to front. **Exact, unlike every other translucent
// order in this scene** -- a marker is a flat billboard that cannot
// interpenetrate another one, so "which is in front" really is a function of
// one number per point, which is precisely what a sort needs and what a pair
// of surfaces never provides. So the fallback path is not approximating here;
// it is answering.
void scatter3d_draw_order(const Scatter3DPlot& s, const Projector3D& proj,
                          std::vector<std::size_t>& out);

} // namespace sextant
