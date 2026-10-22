#pragma once
// contour() / contourf(): what a ContourPlot draws, derived once per data or
// options change (contour_rebuild()) and shared by GL, SVG, planes and hover.
//
// Each square of the grid with no NaN corner becomes four triangles around
// its centre (whose value is the corner mean, which also decides saddles).
// The fill is a heatmap_tri over them under a boundary norm of the levels,
// each band coloured at its midpoint; the lines are the levels' crossings of
// the same triangles, so a contour over a contourf of the same data follows
// the band edges.
#include "color_norm.h"
#include "contour.h"
#include "heatmap_tri_geometry.h"
#include "plot_objects.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sextant {

struct ContourGeometry {
    // The levels drawn: given (sorted, unique) or resolved from nlevels.
    std::vector<double> levels;
    // The colour limits (vmin/vmax, else the first and last level) and the
    // norm in force over them.
    float vmin = 0.0f, vmax = 1.0f;
    ColorNorm::Kind norm = ColorNorm::Kind::Linear;
    // Each level's colormap position (a line's colour) and each band's (its
    // midpoint's); NaN: the norm gives it no colour, so it is not drawn.
    std::vector<double> level_t, band_t;
    // The triangles: the grid's points (row-major) then the centres of the
    // squares drawn. Its norm is boundaries(levels), band_t its colours (no
    // fill with fewer than two levels).
    HeatmapTriPlot fill;
    // The traced lines (ContourLine::level), level by level; `line_level`
    // indexes `levels` for each.
    ContourSet lines;
    std::vector<std::size_t> line_level;
};

// The levels a contour draws: `o.levels` when given, else nlevels nice
// values over z's finite range (first <= min, last >= max; one value when
// the range is a point; none without a finite value).
std::vector<double> contour_levels(const ContourOptions& o, std::span<const double> z);

// A contour's colour limits for `levels`: vmin/vmax, else the first and last
// level (0..1 without any).
void contour_limits(const ContourOptions& o, const std::vector<double>& levels, float& vmin, float& vmax);

// Checks a contour's grid and options for the calls (std::invalid_argument,
// naming `who`): lengths, at least 2 x 2, finite and strictly monotone x and
// y, finite levels (at most kMaxNormBounds), nlevels >= 1, and a norm that
// fits the resolved limits. Sorts and de-duplicates o.levels.
void check_contour(std::span<const double> x, std::span<const double> y, std::span<const double> z,
                   ContourOptions& o, const char* who);

// A line colour: the single colour, else level k's place on the colormap
// (nothing when the norm leaves the level without a colour).
std::optional<Color> contour_line_color(const ContourPlot& p, std::size_t level);

// What a contourf has under the data point (x, y): the band and the value
// interpolated there (the drawn triangles, in scale space as heatmap_tri_at()).
struct ContourFillHit {
    std::size_t band = 0;
    double value = 0.0;
};
std::optional<ContourFillHit> contour_fill_at(const ContourPlot& p, const AxisScale& xs, const AxisScale& ys,
                                              double x, double y);

// The nearest point of a contour's lines to the cursor within `radius`
// pixels: its level and where it is. `proj.at(u, v)` gives a point's pixel
// (and whether it is in front); segments wholly outside the data box are
// skipped.
struct ContourLineHit {
    std::size_t level = 0;
    float x = 0.0f, y = 0.0f, d2 = 0.0f;
};
template <class Proj>
std::optional<ContourLineHit> contour_line_near(const ContourPlot& p, const Proj& proj, float cx, float cy,
                                                float radius, double x_lo, double x_hi, double y_lo, double y_hi) {
    if (!p.geom || p.opts.filled) return std::nullopt;
    std::optional<ContourLineHit> best;
    float best_d2 = radius * radius;
    for (std::size_t k = 0; k < p.geom->lines.size(); ++k) {
        const ContourLine& l = p.geom->lines[k];
        for (std::size_t i = 0; i + 1 < l.x.size(); ++i) {
            const double ax = l.x[i], ay = l.y[i], bx = l.x[i + 1], by = l.y[i + 1];
            if (std::max(ax, bx) < x_lo || std::min(ax, bx) > x_hi || std::max(ay, by) < y_lo
                || std::min(ay, by) > y_hi)
                continue;
            const auto a = proj.at(ax, ay), b = proj.at(bx, by);
            if (!a.in_front || !b.in_front) continue;
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len2 = dx * dx + dy * dy;
            float t = len2 > 0.0f ? ((cx - a.x) * dx + (cy - a.y) * dy) / len2 : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            const float px = a.x + t * dx, py = a.y + t * dy;
            const float d2 = (px - cx) * (px - cx) + (py - cy) * (py - cy);
            if (d2 <= best_d2) {
                best_d2 = d2;
                best = ContourLineHit{ p.geom->line_level[k], px, py, d2 };
            }
        }
    }
    return best;
}

// "%g" of a level, as the inline labels and the colorbar's.
inline std::string contour_band_text(const ContourGeometry& g, std::size_t band) {
    return format_contour_level(g.levels[band]) + ".." + format_contour_level(g.levels[band + 1]);
}

} // namespace sextant
