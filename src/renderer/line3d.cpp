#include "line3d.h"
#include "../colormaps.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sextant {

Color line3d_point_color(const Line3DPlot& l, std::size_t i, double vmin, double vmax) {
    Color base = l.opts.color;
    if (l.colormapped()) {
        // The identical lookup scatter3d_point_color() and surface_cell_color()
        // do, down to the rounding -- a shared rule that three kinds spell
        // differently is not shared.
        const double span = vmax - vmin;
        const double t = span != 0.0 ? (l.color_at(i) - vmin) / span : 0.0;
        const int idx = static_cast<int>(std::clamp(t, 0.0, 1.0) * 255.0);
        const uint8_t* lut = colormaps::get(l.opts.cmap) + idx * 4;
        base = { lut[0] / 255.0f, lut[1] / 255.0f, lut[2] / 255.0f, 1.0f };
    }
    return { base.r, base.g, base.b, line3d_alpha(l) };
}

double line3d_plot_distance(const Line3DPlot& l, const Projector3D& proj) {
    if (l.count() == 0) return 0.0;
    const Transform3D& tf = proj.transform();
    // The centre of the path's own bounding box, not the mean of its points --
    // scatter3d_plot_distance()'s choice, for the same reason: where the
    // samples happen to bunch must not move the "centre".
    double lo[3] = {  std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max() };
    double hi[3] = { -std::numeric_limits<double>::max(),
                     -std::numeric_limits<double>::max(),
                     -std::numeric_limits<double>::max() };
    for (std::size_t i = 0; i < l.count(); ++i) {
        const double p[3] = { l.x[i], l.y[i], l.z[i] };
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], p[a]);
            hi[a] = std::max(hi[a], p[a]);
        }
    }
    const Vec3 c{ (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5 };
    return length(tf.to_box(c.x, c.y, c.z) - eye_coord(proj));
}

void line3d_draw_order(const Line3DPlot& l, const Projector3D& proj,
                       std::vector<std::size_t>& out) {
    const std::size_t n = l.segment_count();
    out.clear();
    out.reserve(n);
    for (std::size_t s = 0; s < n; ++s) out.push_back(s);
    if (n < 2) return;

    const Transform3D& tf = proj.transform();
    std::vector<float> depth(n, 0.0f);
    for (std::size_t s = 0; s < n; ++s) {
        std::size_t a = 0, b = 0;
        l.segment_ends(s, a, b);
        // The midpoint's depth, not the nearer or farther end's. A segment has
        // a depth *range*, and no single number describes it -- which is what
        // makes this order a heuristic (see the header). The midpoint is the
        // one choice of the three that is symmetric in the segment's two ends,
        // so reversing a path's point order cannot change its drawn order.
        const Vec3 pa = tf.to_box(l.x[a], l.y[a], l.z[a]);
        const Vec3 pb = tf.to_box(l.x[b], l.y[b], l.z[b]);
        const Vec3 mid{ (pa.x + pb.x) * 0.5, (pa.y + pb.y) * 0.5, (pa.z + pb.z) * 0.5 };
        depth[s] = proj.project_box(mid).depth;
    }

    // Far to near, stably, so a frame is the same frame as the last and two
    // segments at one depth keep their path order.
    std::stable_sort(out.begin(), out.end(),
                     [&](std::size_t a, std::size_t b) { return depth[a] > depth[b]; });
}

std::vector<Line3DSegment> plan_lines3d(const Projector3D& proj,
                                        const std::vector<Line3DPlot>& lines) {
    std::vector<Line3DSegment> out;
    const Transform3D& tf = proj.transform();

    // The ramp, once for the whole cell -- the same two numbers the shader
    // gets, so the SVG and the PNG darken a stretch of path by the same amount.
    float dmin = 0.0f, dmax = 1.0f;
    box_depth_range(proj, dmin, dmax);
    const float dspan = (dmax - dmin) != 0.0f ? 1.0f / (dmax - dmin) : 0.0f;

    for (std::size_t li = 0; li < lines.size(); ++li) {
        const Line3DPlot& l = lines[li];
        if (l.opts.linewidth <= 0.0f) continue;
        double vmin = 0.0, vmax = 1.0;
        line3d_value_range(l, vmin, vmax);
        const double span = vmax - vmin;
        const double half = line3d_half_width(l, proj);

        for (std::size_t s = 0; s < l.segment_count(); ++s) {
            std::size_t ia = 0, ib = 0;
            l.segment_ends(s, ia, ib);
            const Vec3 ba = tf.to_box(l.x[ia], l.y[ia], l.z[ia]);
            const Vec3 bb = tf.to_box(l.x[ib], l.y[ib], l.z[ib]);
            // Behind a perspective eye a point does not project off-screen, it
            // projects to the wrong side of the picture. A segment with either
            // end behind the eye is dropped whole rather than clipped: the
            // near-plane rule every CPU consumer of this projector obeys, and
            // clipping one would need a colour and a width at the new end,
            // which is a whole mechanism for a case the raster path already
            // handles on the GPU.
            if (!proj.in_front(ba) || !proj.in_front(bb)) continue;
            const Px3 qa = proj.project_box(ba);
            const Px3 qb = proj.project_box(bb);

            Line3DSegment g;
            g.x0 = qa.x; g.y0 = qa.y;
            g.x1 = qb.x; g.y1 = qb.y;

            const Vec3 mid{ (ba.x + bb.x) * 0.5, (ba.y + bb.y) * 0.5,
                            (ba.z + bb.z) * 0.5 };
            g.depth = proj.project_box(mid).depth;

            // The width: the ribbon's own two edges, projected. Measured at
            // the midpoint and in the direction the raster path expands --
            // perpendicular to the segment and to the eye -- so the number
            // here is the one the shader would have produced there, rather
            // than a second expression of "how wide is a line".
            const Vec3 eye = proj.has_eye_point()
                           ? Vec3{ proj.eye_point().x - mid.x,
                                   proj.eye_point().y - mid.y,
                                   proj.eye_point().z - mid.z }
                           : proj.eye_dir();
            Vec3 side = cross(bb - ba, eye);
            const double sl = length(side);
            if (sl > 1e-12) {
                side = side * (half / sl);
                const Px3 e0 = proj.project_box(mid - side);
                const Px3 e1 = proj.project_box(mid + side);
                g.width = static_cast<float>(std::hypot(e1.x - e0.x, e1.y - e0.y));
            } else {
                // A zero-length segment, or one pointing straight at the eye:
                // it has no visible extent to measure, so the ribbon is edge
                // on and a hairline is the honest answer.
                g.width = 0.0f;
            }

            const float ta = static_cast<float>((qa.depth - dmin) * dspan);
            const float tb = static_cast<float>((qb.depth - dmin) * dspan);
            g.c0 = depth_shade(line3d_point_color(l, ia, vmin, vmax),
                               l.opts.depthshade, ta);
            g.c1 = depth_shade(line3d_point_color(l, ib, vmin, vmax),
                               l.opts.depthshade, tb);
            // The same factor depth_shade() just applied, kept so a gradient
            // stop can be darkened after its own colormap lookup.
            const float sh = std::clamp(l.opts.depthshade, 0.0f, 1.0f);
            g.k0 = 1.0f - sh * std::clamp(ta, 0.0f, 1.0f);
            g.k1 = 1.0f - sh * std::clamp(tb, 0.0f, 1.0f);

            g.colormapped = l.colormapped();
            g.cmap = l.opts.cmap;
            if (g.colormapped && span != 0.0) {
                g.v0 = static_cast<float>(
                    std::clamp((l.color_at(ia) - vmin) / span, 0.0, 1.0));
                g.v1 = static_cast<float>(
                    std::clamp((l.color_at(ib) - vmin) / span, 0.0, 1.0));
            }

            g.a = ba; g.b = bb;
            g.plot = li;
            g.index = s;
            out.push_back(g);
        }
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const Line3DSegment& a, const Line3DSegment& b) {
                         return a.depth > b.depth;
                     });
    return out;
}

} // namespace sextant
