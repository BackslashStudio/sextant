#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sextant {

// A path through the scene (v1.0 step 13). What this header owns is everything
// both outputs have to agree about -- a point's colour, a segment's two ends,
// how wide the ribbon is and how the path is ordered -- for the reason
// bar3d.h, surface.h and scatter3d.h own the same for their kinds: two
// transcriptions of one rule is where the raster and the vector paths drift.
//
// **A path is stroked geometry in the scene**, which memory/spec_3d.md §4
// answers as the world-space billboarded ribbon -- the row written for
// `bar3d`'s edges, the only other thing here that is stroked at all. The
// consequences are stated where they bite: no dash pattern (a ribbon has no
// pixel arc length), and a width that thins with distance under perspective
// while the axis frame beside it does not.

// True when the path has to be composited rather than left to the depth
// buffer. scatter3d_translucent()'s counterpart, and `colors` cannot make a
// series translucent for the same reason: a colormap has no alpha of its own.
inline bool line3d_translucent(const Line3DPlot& l) {
    return l.opts.alpha < 1.0f || (!l.colormapped() && l.opts.color.a < 1.0f);
}

inline float line3d_alpha(const Line3DPlot& l) {
    const float base = l.colormapped() ? 1.0f : l.opts.color.a;
    return std::clamp(base * l.opts.alpha, 0.0f, 1.0f);
}

// One *point's* colour before the depth shade: the flat colour, or its own
// entry mapped through the colormap. The single place a path's colour is
// decided, so the PNG and the SVG cannot differ by a rounding rule.
//
// A point's, not a segment's, and that is the whole of step 13's colour rule:
// a `colors` value is measured at a point, so a segment ramps between the two
// it joins rather than picking one of them or their mean. The raster path gets
// that ramp from the rasterizer (a colour per ribbon end, interpolated across
// it); the SVG path has to emit a gradient to say the same thing.
Color line3d_point_color(const Line3DPlot& l, std::size_t i, double vmin, double vmax);

// The ribbon's half-width in *box* units, which is what makes it geometry.
//
// `linewidth` is a pixel width **at the box centre**, converted once here --
// Bar3DOptions::edge_linewidth's exact bargain, and the one that makes the
// number a caller writes readable (it is a pixel width somewhere) while
// keeping it a world length (it is the same length everywhere). Measured at
// the centre rather than per segment on purpose: a width that varied with the
// segment would make one path two thicknesses.
inline double line3d_half_width(const Line3DPlot& l, const Projector3D& proj) {
    return 0.5 * l.opts.linewidth * proj.box_units_per_pixel(Vec3{ 0.0, 0.0, 0.0 });
}

// Where the path sits, for the whole-object order the non-peeled fallback
// uses: the centre of its own bounding box, which is what a bar grid's, a
// surface's and a cloud's distance are, so the four compare directly.
double line3d_plot_distance(const Line3DPlot& l, const Projector3D& proj);

// The path's segments, back to front, by the depth of each segment's midpoint.
//
// **Approximate, unlike a cloud's order** -- and the difference is worth
// stating because the two kinds look so alike. A marker is a flat billboard
// that cannot interpenetrate another, so ordering markers by one number each
// is exact. Two ribbons of one path can cross, and a long ribbon seen nearly
// end-on spans a depth range its midpoint does not describe; so this is a
// heuristic on the same terms as bar3d_draw_order(), and the exact answer for
// the raster path is depth peeling, which does not consult it at all.
void line3d_draw_order(const Line3DPlot& l, const Projector3D& proj,
                       std::vector<std::size_t>& out);

// One segment, projected and ready to emit: the SVG writer's view of a path,
// and the painter's. Bar3DPolygon's and Surface3DPolygon's sibling and, like
// Scatter3DMarker, deliberately not either of them.
//
// **A segment is a two-point stroke here, where the raster path draws a
// quad**, and that is a cost decision rather than a difference of opinion
// about what a ribbon is. A four-point ring has a plane, so the painter would
// treat every one of them as a *blade* -- and a path is the one kind in this
// scene that can contribute thousands of primitives, each of them a thin
// cutting plane that other geometry merely straddles. post-step-10.1 measured
// what that costs on far fewer of them. A two-point ring is refused by
// `ring_plane()`, so it is ordered by `stroke_vs_polygon()` -- exact, and the
// one rung that can never end in a split -- and cuts nothing.
//
// Two consequences follow, and both are named divergences (memory/spec_3d.md
// §10) rather than defects:
//
//   - **one width per segment.** §4's SVG concession allows one per *line*;
//     this spends it per segment, as step 4 spent it per bar. `width` is the
//     ribbon's own two edges projected at the segment's midpoint, which is
//     `plane_geometry()`'s rule and is what keeps the two outputs agreeing to
//     the pixel away from the ends.
//   - **round joins, where the raster path miters.** A two-point stroke has no
//     polyline to join across, so the writer gives every segment a round cap:
//     a disc of exactly the half width at each end, which fills the wedge at
//     every bend precisely. The cost is that an *open* path's two ends extend
//     half a width beyond the first and last points, and that a corner sharper
//     than the miter limit is rounded rather than pointed.
struct Line3DSegment {
    float x0 = 0.0f, y0 = 0.0f;   // the two ends, in the figure's pixels
    float x1 = 0.0f, y1 = 0.0f;
    float width = 1.0f;           // stroke width in pixels at the midpoint

    // The two ends' colours, already through the colormap *and* the depth
    // shade, so the writer emits colours rather than deciding any. Equal for a
    // flat series, which is what lets the writer skip the gradient entirely.
    Color c0{}, c1{};

    // The two ends' normalized colormap values, and the map to look them up
    // in. Non-empty gradients are emitted as stops sampled from the map at
    // steps between these -- **not** as a two-stop ramp between `c0` and `c1`,
    // for the reason the raster path samples per fragment: the straight line
    // between two distant entries leaves the colormap, and every colour drawn
    // must be one the colorbar shows.
    bool     colormapped = false;
    Colormap cmap = Colormap::Viridis;
    float    v0 = 0.0f, v1 = 0.0f;

    // The depth cue as the factor it multiplies by -- `1 - depthshade * t` at
    // each end, which is what depth_shade() applies. Carried rather than left
    // to be recovered from `c0`/`c1`, because a gradient stop's colour comes
    // from the colormap and has to be darkened *after* the lookup; dividing a
    // shaded colour by its unshaded one to get the factor back fails exactly
    // where the colour is near black, which is where the cue matters most.
    float    k0 = 1.0f, k1 = 1.0f;

    // Px3::depth at the midpoint, and the two ends in box space -- the same
    // pair Scatter3DMarker carries, and for the same two consumers: the
    // whole-object fallback merges by the depth, the pairwise tests need the
    // positions.
    float depth = 0.0f;
    Vec3  a{}, b{};
    std::size_t plot = 0, index = 0;   // which path, which segment
};

// Every drawn segment of every path, far to near. Segments behind a
// perspective eye are dropped here rather than downstream, as plan_box3d()
// does with a tick behind the eye.
//
// Sorted by midpoint depth, which is `line3d_draw_order()`'s heuristic and
// says so: two ribbons of one path can cross. The painter re-derives the order
// for every pair that also involves geometry, exactly, so what this settles is
// only segments against each other.
std::vector<Line3DSegment> plan_lines3d(const Projector3D& proj,
                                        const std::vector<Line3DPlot>& lines);

} // namespace sextant
