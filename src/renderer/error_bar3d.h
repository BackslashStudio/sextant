#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sextant {

// Error bars in a scene (v1.0 step 17). What this header owns is everything
// both outputs have to agree about -- where every whisker, cap arm, block face
// and block edge is, what colour and how wide -- for the reason bar3d.h and
// line3d.h own the same for their kinds.
//
// **Everything is resolved on the CPU, in box space, for one camera.** Two
// things about an error bar depend on the view and not only on the data: a
// whisker and its caps face the eye, and every pixel length (`linewidth`,
// `capsize`, `boxwidth`) is a pixel *at the box centre* converted to box
// units, which moves with the limits and the zoom. So the pieces are rebuilt
// when the view changes -- the raster path caches them per view, the SVG path
// builds them once -- and neither output re-derives any of it.

// Which kind of series a bar hangs off. Provenance only: the drawing is the
// same for both.
enum class ErrorBar3DOwner { Scatter, Line };

// One piece of one point's error bar, in box space.
//
//   - a **stroke** (`face == false`): `p[0]`-`p[1]`, drawn as a ribbon
//     `2 * half_width` box units wide, expanded across cross(p1 - p0, facing).
//     Every stroke of one point carries that point's own `facing`, so a
//     whisker, its caps and its block's edges are rigid rather than twisting
//     toward the eye along their length -- the difference is only visible
//     under perspective, and it is the difference between a drawn object and
//     a smear.
//   - a **face** (`face == true`): the ring `p[0..3]` of one side of the block.
struct ErrorBar3DPiece {
    bool   face = false;
    Vec3   p[4]{};
    Vec3   facing{};          // strokes: unit direction to the eye at the point
    double half_width = 0.0;  // strokes: box units
    Color  color{};           // alpha resolved, depth shade not yet applied
    float  depthshade = 0.0f; // the owning series' own
    ErrorBar3DOwner owner = ErrorBar3DOwner::Scatter;
    std::size_t plot = 0, point = 0;
};

// The colour a series' error bars take: `errorbar.color`, or the series' flat
// colour, or black for a series with a `colors` vector (2D scatter_z's rule --
// a colormapped series has no one colour of its own).
inline Color errorbar3d_color(const ErrorBar3DOptions& style, Color fallback) {
    return style.color.value_or(fallback);
}
inline Color errorbar3d_color(const Scatter3DPlot& s) {
    return errorbar3d_color(s.opts.errorbar, s.colormapped() ? Color::Black : s.opts.color);
}
inline Color errorbar3d_color(const Line3DPlot& l) {
    return errorbar3d_color(l.opts.errorbar, l.colormapped() ? Color::Black : l.opts.color);
}

// True when a series draws any error bar at all. `linewidth <= 0` drops the
// whole bar, block included -- the 2D rule.
inline bool errorbar3d_drawn(const ErrorBar3DData& err, const ErrorBar3DOptions& style) {
    return !err.empty() && style.linewidth > 0.0f;
}

// True when some of a series' error-bar pieces have to be composited rather
// than left to the depth buffer. Decided from the style alone, without
// building anything, which is what the scene's "is there a translucent pass"
// question needs -- and conservative in the one direction that is safe: a
// block that turns out to have zero extent everywhere still says yes.
inline bool errorbar3d_translucent(const ErrorBar3DData& err, const ErrorBar3DOptions& style,
                                   Color c) {
    if (!errorbar3d_drawn(err, style)) return false;
    if (c.a < 1.0f) return true;
    if (!err.any_box()) return false;
    const auto part = [](float a) { return a > 0.0f && a < 1.0f; };
    return part(c.a * style.box_alpha) || part(c.a * style.edge_alpha);
}
inline bool errorbar3d_translucent(const Scatter3DPlot& s) {
    return errorbar3d_translucent(s.err, s.opts.errorbar, errorbar3d_color(s));
}
inline bool errorbar3d_translucent(const Line3DPlot& l) {
    return errorbar3d_translucent(l.err, l.opts.errorbar, errorbar3d_color(l));
}

// Every piece of one series' error bars, appended to `out`, in point order:
// each point's whiskers (x, y, z, each followed by its caps), then its block's
// six faces, then its twelve edges.
//
// Nothing is culled against a perspective eye here; the consumers do that,
// each in the way its output needs.
void errorbar3d_pieces(const Projector3D& proj, const Scatter3DPlot& s, std::size_t plot,
                       std::vector<ErrorBar3DPiece>& out);
void errorbar3d_pieces(const Projector3D& proj, const Line3DPlot& l, std::size_t plot,
                       std::vector<ErrorBar3DPiece>& out);

// A stroke's ribbon: its four corners in ring order, a - side, a + side,
// b + side, b - side. False when the stroke has no width or points straight
// at the eye, in which case it covers no pixel and is skipped.
bool errorbar3d_ribbon(const ErrorBar3DPiece& stroke, Vec3 corners[4]);

// One piece, projected and ready to emit: the SVG writer's view, and the
// painter's.
//
// **A stroke stays a two-point stroke here, where the raster path draws the
// ribbon** -- Line3DSegment's bargain, for its reason: a two-point ring has no
// plane, so the painter orders it exactly by stroke_vs_polygon() and never
// cuts anything along it. A block face is a real four-point polygon and takes
// part in the painter like any bar face.
struct ErrorBar3DPolygon {
    std::vector<float> xy;    // pixels: the ring for a face, the two ends for a stroke
    std::vector<Vec3>  box;   // the same, in box space
    bool  filled = false;     // a face
    Color color{};            // finished: alpha resolved, depth shade applied
    float width = 0.0f;       // strokes: pixels at the midpoint
    float depth = 0.0f;       // Px3::depth at the centroid
    ErrorBar3DOwner owner = ErrorBar3DOwner::Scatter;
    std::size_t plot = 0, point = 0;
};

// Every piece of every series' error bars, far to near by centroid depth.
// Pieces entirely behind a perspective eye are dropped.
std::vector<ErrorBar3DPolygon> plan_errorbars3d(const Projector3D& proj,
                                                const std::vector<Scatter3DPlot>& points,
                                                const std::vector<Line3DPlot>& lines);

} // namespace sextant
