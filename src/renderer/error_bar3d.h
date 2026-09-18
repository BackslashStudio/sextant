#pragma once
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sextant {

// 3D error bars, resolved on the CPU in box space for one camera: whiskers face
// the eye and pixel lengths are converted at the box centre, so pieces are
// rebuilt when the view changes (cached per view by the raster path).

// Which kind of series the bar hangs off (provenance only).
enum class ErrorBar3DOwner { Scatter, Line };

// One piece of one point's error bar, in box space:
//   - a stroke (`face == false`): `p[0]`-`p[1]` as a ribbon `2 * half_width`
//     wide across cross(p1 - p0, facing). All strokes of a point share its
//     `facing`, so the bar stays rigid.
//   - a face (`face == true`): the ring `p[0..3]` of one side of the block.
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

// Error-bar color: `errorbar.color`, else the series' flat color, else black
// for a `colors` series.
inline Color errorbar3d_color(const ErrorBar3DOptions& style, Color fallback) {
    return style.color.value_or(fallback);
}
inline Color errorbar3d_color(const Scatter3DPlot& s) {
    return errorbar3d_color(s.opts.errorbar, s.colormapped() ? Color::Black : s.opts.color);
}
inline Color errorbar3d_color(const Line3DPlot& l) {
    return errorbar3d_color(l.opts.errorbar, l.colormapped() ? Color::Black : l.opts.color);
}

// True when a series draws any error bar; `linewidth <= 0` draws none.
inline bool errorbar3d_drawn(const ErrorBar3DData& err, const ErrorBar3DOptions& style) {
    return !err.empty() && style.linewidth > 0.0f;
}

// True when some pieces need compositing, from the style alone (conservative).
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

// All pieces of one series, appended in point order: whiskers (x, y, z, each
// with caps), then block faces, then block edges. Not culled here.
void errorbar3d_pieces(const Projector3D& proj, const Scatter3DPlot& s, std::size_t plot,
                       std::vector<ErrorBar3DPiece>& out);
void errorbar3d_pieces(const Projector3D& proj, const Line3DPlot& l, std::size_t plot,
                       std::vector<ErrorBar3DPiece>& out);

// A stroke's ribbon corners in ring order. False when it has no width or
// points at the eye.
bool errorbar3d_ribbon(const ErrorBar3DPiece& stroke, Vec3 corners[4]);

// One projected piece for the SVG path. Strokes stay two-point strokes (never
// split); block faces are polygons.
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

// All pieces of all series, far to near by centroid depth; pieces entirely
// behind a perspective eye are dropped.
std::vector<ErrorBar3DPolygon> plan_errorbars3d(const Projector3D& proj,
                                                const std::vector<Scatter3DPlot>& points,
                                                const std::vector<Line3DPlot>& lines);

} // namespace sextant
