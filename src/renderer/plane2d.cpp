#include "plane2d.h"
#include "../colormaps.h"
#include "error_bar_shape.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sextant {

namespace {

// The colormap lookup both the texture and the per-cell polygons use, so the
// two forms of the same plane come out in the same colours.
Color cell_color(const std::uint8_t* lut, float value, float vmin, float vrange) {
    float t = (vrange != 0.0f) ? (value - vmin) / vrange : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const std::uint8_t* c = &lut[static_cast<int>(t * 255.0f) * 4];
    return { c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f };
}

Color with_alpha(Color c, float a) { c.a *= std::clamp(a, 0.0f, 1.0f); return c; }

} // namespace


PlaneQuad plane_heatmap_quad(const HeatmapPlot& hp, PlaneOrientation orient,
                             double offset) {
    PlaneQuad q;
    q.normal_axis = axis_map(orient).h;
    q.p[0] = plane_point(orient, hp.xrange.lo, hp.yrange.lo, offset);
    q.p[1] = plane_point(orient, hp.xrange.hi, hp.yrange.lo, offset);
    q.p[2] = plane_point(orient, hp.xrange.hi, hp.yrange.hi, offset);
    q.p[3] = plane_point(orient, hp.xrange.lo, hp.yrange.hi, offset);
    // Exactly draw_heatmap()'s assignment: uv v = 1 at the yrange.lo edge, so
    // the last uploaded row lands there and the first lands at yrange.hi.
    const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
    std::memcpy(q.uv, uv, sizeof(uv));
    return q;
}

int plane_raster_cap(const PlotRect& frame) {
    const double d = std::ceil(std::max(frame.w, frame.h));
    return std::max(1, static_cast<int>(d));
}

PlaneRaster plane_raster(const Projector3D& proj, PlaneOrientation orient,
                         double offset) {
    return plane_raster(proj.transform(), orient, offset,
                        plane_raster_cap(proj.frame()));
}

PlaneRaster plane_raster(const Transform3D& tf, PlaneOrientation orient,
                         double offset, int max_dim) {
    const Axis3Map m = axis_map(orient);
    const double lo[3]   = { tf.xmin, tf.ymin, tf.zmin };
    const double hi[3]   = { tf.xmax, tf.ymax, tf.zmax };
    const double side[3] = { tf.aspect.x, tf.aspect.y, tf.aspect.z };

    PlaneRaster r;
    r.normal_axis = m.h;

    // The raster's *shape* comes from the plane's extent in **box** space, not
    // from the frame's aspect. Box space because that is what the projector
    // consumes and what BoxAspect has already had its say in; and shaping it to
    // the frame instead would make a texel non-square in plane units whenever
    // the two aspects differed, so a circular marker would come out elliptical
    // before the projection ever touched it.
    const double su = std::fabs(side[m.u]), sv = std::fabs(side[m.v]);
    const double big = std::max(su, sv);
    const int cap = std::max(1, max_dim);
    auto scale = [&](double s) {
        if (big <= 0.0) return cap;
        return std::clamp(static_cast<int>(std::lround(cap * (s / big))), 1, cap);
    };
    r.w = scale(su);
    r.h = scale(sv);
    // One raster pixel, as a length in the box. Taken from the *longer* side
    // over the cap rather than from either side over its own rounded count, so
    // the rounding above cannot make the two directions disagree and turn a
    // circle into an ellipse.
    r.box_per_px = big > 0.0 ? big / static_cast<double>(cap) : 0.0;

    // The plane's own 2D transform. The limits go in as declared rather than
    // ordered, so a reversed axis mirrors the raster exactly as it mirrors a 2D
    // axes -- and the quad below is keyed on the same two values, which is what
    // keeps the texture on the right way round without a second sign rule.
    r.tr.xmin = lo[m.u]; r.tr.xmax = hi[m.u];
    r.tr.ymin = lo[m.v]; r.tr.ymax = hi[m.v];
    r.tr.px = 0.0f;                          r.tr.py = 0.0f;
    r.tr.pw = static_cast<float>(r.w);       r.tr.ph = static_cast<float>(r.h);
    r.tr.win_w = r.tr.pw;                    r.tr.win_h = r.tr.ph;

    const double uu[4] = { r.tr.xmin, r.tr.xmax, r.tr.xmax, r.tr.xmin };
    const double vv[4] = { r.tr.ymin, r.tr.ymin, r.tr.ymax, r.tr.ymax };
    for (int i = 0; i < 4; ++i) {
        r.p[i] = plane_point(orient, uu[i], vv[i], offset);
        r.uv[i][0] = (i == 1 || i == 2) ? 1.0f : 0.0f;
        // **The opposite of plane_heatmap_quad()'s v, and for a real reason.**
        // That quad samples an *uploaded image*, whose row 0 is the top. This
        // one samples a *rendered framebuffer*, whose row 0 is the bottom --
        // and the plane's own transform puts v_min at the bottom, since to_py()
        // flips. So v_min is t = 0 here where it is t = 1 there.
        r.uv[i][1] = (i >= 2) ? 1.0f : 0.0f;
    }
    return r;
}

std::vector<std::uint8_t> plane_heatmap_rgba(const HeatmapPlot& hp) {
    std::vector<std::uint8_t> out;
    if (hp.rows <= 0 || hp.cols <= 0) return out;

    const std::uint8_t* lut = colormaps::get(hp.opts.cmap);
    const float vmin = hp.opts.vmin, vrange = hp.opts.vmax - hp.opts.vmin;
    // Row 0 of the output is the yrange.hi edge. With origin "lower", data row
    // 0 is at yrange.lo, so the rows come out reversed; with "upper" they do
    // not. Same `flip` the 2D texture upload applies, for the same reason.
    const bool flip = (hp.opts.origin == "lower");

    out.resize(static_cast<std::size_t>(hp.rows) * hp.cols * 4);
    for (int r = 0; r < hp.rows; ++r) {
        const int src = flip ? (hp.rows - 1 - r) : r;
        for (int c = 0; c < hp.cols; ++c) {
            float t = (vrange != 0.0f)
                    ? (hp.data[static_cast<std::size_t>(src) * hp.cols + c] - vmin) / vrange
                    : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            std::memcpy(&out[(static_cast<std::size_t>(r) * hp.cols + c) * 4],
                        &lut[static_cast<int>(t * 255.0f) * 4], 4);
        }
    }
    return out;
}

bool plane_translucent(const PlaneSnapshot& p) { return p.opts.alpha < 1.0f; }

double plane_distance(const PlaneSnapshot& p, const Projector3D& proj) {
    // The centre of everything on the plane, taken through the 2D bounds the
    // plane's own sheet reports -- so a small slice near one corner of the box
    // orders by where it actually is, not by where its plane's face is.
    const DataBounds b = auto_scale(p.sheet.all(), 0.0);
    const Vec3 mid = plane_point(p.orient, (b.xmin + b.xmax) * 0.5,
                                           (b.ymin + b.ymax) * 0.5, p.offset);
    // Distance from eye_coord(), not Px3::depth -- the same quantity
    // bar3d_plot_distance() reports, because the SVG writer interleaves the
    // two plans by comparing them (see eye_coord()'s comment).
    const Vec3 box = proj.transform().to_box(mid.x, mid.y, mid.z);
    return length(box - eye_coord(proj));
}

// ---------------------------------------------------------------------------
// plane_geometry: the 2D kinds as primitives in the plane
// ---------------------------------------------------------------------------
namespace {

// Accumulates primitives while keeping the batch list in step, so a caller
// cannot append to a list and forget to say where the batch ends. `open`
// extends the batch already at the back when it is of the same kind and still
// current, so consecutive same-kind groups cost one batch rather than several.
struct GeometryBuilder {
    PlaneGeometry    g;
    PlaneOrientation orient = PlaneOrientation::XY;
    double           offset = 0.0;

    Vec3 at(double u, double v) const { return plane_point(orient, u, v, offset); }

    void open(PlaneGeometry::Batch::Kind k) {
        const std::size_t n = size_of(k);
        if (!g.batches.empty() && g.batches.back().kind == k && g.batches.back().end == n)
            return;
        g.batches.push_back({ k, n, n });
    }
    void close() {
        if (g.batches.empty()) return;
        auto& b = g.batches.back();
        b.end = size_of(b.kind);
        if (b.end == b.begin) g.batches.pop_back();   // nothing was added
    }

    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color fill) {
        g.tris.push_back({ { a, b, c }, fill });
        g.tris.push_back({ { a, c, d }, fill });
    }
    // An axis-aligned rectangle in the plane, from two opposite in-plane
    // corners. A degenerate one is dropped rather than emitted as a zero-area
    // triangle -- a bar of height 0 draws nothing, as it does in 2D.
    void rect(double u0, double v0, double u1, double v1, Color fill) {
        if (u0 == u1 || v0 == v1) return;
        quad(at(u0, v0), at(u1, v0), at(u1, v1), at(u0, v1), fill);
    }
    void rect_outline(double u0, double v0, double u1, double v1,
                      Color color, float width_px) {
        if (width_px <= 0.0f || u0 == u1 || v0 == v1) return;
        seg_uv(u0, v0, u1, v0, color, width_px);
        seg_uv(u1, v0, u1, v1, color, width_px);
        seg_uv(u1, v1, u0, v1, color, width_px);
        seg_uv(u0, v1, u0, v0, color, width_px);
    }
    void seg_uv(double u0, double v0, double u1, double v1, Color c, float w) {
        g.segs.push_back({ at(u0, v0), at(u1, v1), c, w });
    }
    void marker(double u, double v, Color c, float size_px, MarkerStyle m) {
        g.markers.push_back({ at(u, v), c, size_px, m });
    }

private:
    std::size_t size_of(PlaneGeometry::Batch::Kind k) const {
        switch (k) {
            case PlaneGeometry::Batch::Kind::Tri:    return g.tris.size();
            case PlaneGeometry::Batch::Kind::Seg:    return g.segs.size();
            case PlaneGeometry::Batch::Kind::Marker: return g.markers.size();
        }
        return 0;
    }
};

// Frame units per pixel on each in-plane axis -- the plane's own (u, v), which
// generally have different scales -- for the two pixel lengths an error bar
// has: its caps (through whisker_segments()) and the box's fallback width.
struct ErrScale { double px_u, px_v; };

// The error bars of one series: the box first (a fill), then the whiskers
// over it -- the same shapes and the same order the 2D path draws, and the
// same whisker_segments() definition of a whisker.
void error_bars(GeometryBuilder& fills, GeometryBuilder& strokes,
                const CowVec<double>& xs, const CowVec<double>& ys,
                const ErrorBarData& err, const ErrorBarOptions& style,
                const Color& fallback, const ErrScale& sc) {
    if (err.empty() || style.linewidth <= 0.0f) return;
    const Color c = style.color.value_or(fallback);
    const std::size_t n = std::min(xs.size(), ys.size());
    const double half_box = 0.5 * style.boxwidth;
    auto seg = [&](double u0, double v0, double u1, double v1) {
        strokes.seg_uv(u0, v0, u1, v1, c, style.linewidth);
    };

    for (std::size_t i = 0; i < n; ++i) {
        if (err.has_y_box() || err.has_x_box()) {
            // Box data where the series carries it, and the pixel-derived
            // fallback width in the direction it does not -- exactly the 2D rule.
            const ErrOffsets ex = err.x_box(i), ey = err.y_box(i);
            const double bx0 = err.has_x_box() ? xs[i] - ex.lo : xs[i] - sc.px_u * half_box;
            const double bx1 = err.has_x_box() ? xs[i] + ex.hi : xs[i] + sc.px_u * half_box;
            const double by0 = err.has_y_box() ? ys[i] - ey.lo : ys[i] - sc.px_v * half_box;
            const double by1 = err.has_y_box() ? ys[i] + ey.hi : ys[i] + sc.px_v * half_box;
            if (bx0 != bx1 && by0 != by1) {
                const double u0 = std::min(bx0, bx1), u1 = std::max(bx0, bx1);
                const double v0 = std::min(by0, by1), v1 = std::max(by0, by1);
                Color fill = c;
                fill.a *= std::clamp(style.box_alpha, 0.0f, 1.0f);
                fills.rect(u0, v0, u1, v1, fill);
                strokes.rect_outline(u0, v0, u1, v1, c, style.linewidth);
            }
        }
        if (err.has_y_cap()) {
            const ErrOffsets e = err.y_cap(i);
            whisker_segments(xs[i], ys[i], ys[i] - e.lo, ys[i] + e.hi, true,
                             sc.px_v, sc.px_u, style.capsize, style.capstyle, seg);
        }
        if (err.has_x_cap()) {
            const ErrOffsets e = err.x_cap(i);
            whisker_segments(xs[i], ys[i], xs[i] - e.lo, xs[i] + e.hi, false,
                             sc.px_u, sc.px_v, style.capsize, style.capstyle, seg);
        }
    }
}

} // namespace

PlaneGeometry plane_geometry(const PlaneSnapshot& p) {
    const RenderSnapshot& s = p.sheet;
    using Kind = PlaneGeometry::Batch::Kind;

    GeometryBuilder b;
    b.orient = p.orient;
    b.offset = p.offset;

    // Error-bar caps and the box's fallback width are *pixel* lengths
    // with no data-space form at all -- unlike a stroke width, which has one
    // meaning in the scene. They are resolved against the plane's own extent
    // instead: a cap is a fraction of what the data covers, on each in-plane
    // axis separately, since the two generally have different scales.
    // Approximate by construction, and one place rather than one per call site.
    const DataBounds ext = auto_scale(s.all(), 0.0);
    constexpr double kPixelsAcross = 400.0;   // a plane, in round numbers
    const double px_u = std::max(1e-300, ext.xmax - ext.xmin) / kPixelsAcross;
    const double px_v = std::max(1e-300, ext.ymax - ext.ymin) / kPixelsAcross;

    // --- bar bodies, then bar outlines and lines. Same order draw_bars() and
    // draw_lines() run in, so a line sits over a bar's edge as it does in 2D.
    b.open(Kind::Tri);
    for (const auto& bp : s.bars) {
        const double half = bp.bar_width * 0.5;
        const Color c = with_alpha(bp.opts.color, bp.opts.alpha);
        for (std::size_t i = 0; i < bp.centers.size() && i < bp.heights.size(); ++i)
            b.rect(bp.centers[i] - half, 0.0, bp.centers[i] + half, bp.heights[i], c);
    }
    b.close();

    b.open(Kind::Seg);
    for (const auto& bp : s.bars) {
        if (bp.opts.linewidth <= 0.0f) continue;
        const double half = bp.bar_width * 0.5;
        for (std::size_t i = 0; i < bp.centers.size() && i < bp.heights.size(); ++i)
            b.rect_outline(bp.centers[i] - half, 0.0, bp.centers[i] + half,
                           bp.heights[i], bp.opts.edgecolor, bp.opts.linewidth);
    }
    // Dashing does not come into the scene, and that is recorded rather than
    // quietly done (see memory/spec_3d.md §4): a dash pattern is an arc length
    // in *pixels*, and a world-space ribbon has no pixel arc length that
    // survives the projection. A non-solid line on a plane draws solid.
    for (const auto& lp : s.lines) {
        if (lp.opts.linestyle == LineStyle::None || lp.opts.linewidth <= 0.0f) continue;
        const Color c = with_alpha(lp.opts.color, lp.opts.alpha);
        // segment_count()/segment_ends() rather than the wrap written again,
        // so a plane's strokes and the flat axes' instances cannot disagree
        // about whether the path closes. The length guard stays what it was:
        // a y vector shorter than x (only reachable from a hand-built
        // snapshot) drops the segments that would read past its end.
        const std::size_t n = std::min(lp.x.size(), lp.y.size());
        for (std::size_t s = 0; s < lp.segment_count(); ++s) {
            std::size_t i, j;
            lp.segment_ends(s, i, j);
            if (i >= n || j >= n) continue;
            b.seg_uv(lp.x[i], lp.y[i], lp.x[j], lp.y[j], c, lp.opts.linewidth);
        }
    }
    b.close();

    // --- error bars, for every kind that carries them, in the 2D order: after
    // the fills so a bar's error bar sits over its own bar, and before the
    // markers so those stay on top. A box is a fill and a whisker is a stroke,
    // so the two have to interleave with what came before -- which is what the
    // batch list exists for.
    {
        GeometryBuilder fills, strokes;
        fills.orient = strokes.orient = p.orient;
        fills.offset = strokes.offset = p.offset;

        auto run = [&](const CowVec<double>& xs, const CowVec<double>& ys,
                       const ErrorBarData& err, const ErrorBarOptions& style,
                       const Color& fallback) {
            error_bars(fills, strokes, xs, ys, err, style, fallback, ErrScale{ px_u, px_v });
        };
        for (const auto& lp : s.lines)
            run(lp.x, lp.y, lp.err, lp.opts.errorbar, lp.opts.color);
        // Heights, not the zero baseline: an error bar measures the bar's tip.
        for (const auto& bp : s.bars)
            run(bp.centers, bp.heights, bp.err, bp.opts.errorbar, bp.opts.edgecolor);
        for (const auto& sp : s.scatters)
            run(sp.x, sp.y, sp.err, sp.opts.errorbar, sp.opts.color);
        // Scatter_z has no single colour of its own, so black rather than a
        // per-point colormap value the bar cannot have -- same rule as 2D.
        for (const auto& sp : s.scatter_z)
            run(sp.x, sp.y, sp.err, sp.opts.errorbar, Color::Black);

        if (!fills.g.tris.empty()) {
            b.open(Kind::Tri);
            b.g.tris.insert(b.g.tris.end(), fills.g.tris.begin(), fills.g.tris.end());
            b.close();
        }
        if (!strokes.g.segs.empty()) {
            b.open(Kind::Seg);
            b.g.segs.insert(b.g.segs.end(), strokes.g.segs.begin(), strokes.g.segs.end());
            b.close();
        }
    }

    // --- markers, last, so they stay on top of what they annotate.
    b.open(Kind::Marker);
    for (const auto& sp : s.scatters) {
        const Color c = with_alpha(sp.opts.color, sp.opts.alpha);
        const std::size_t n = std::min(sp.x.size(), sp.y.size());
        for (std::size_t i = 0; i < n; ++i)
            b.marker(sp.x[i], sp.y[i], c, sp.opts.size, sp.opts.marker);
    }
    for (const auto& sp : s.scatter_z) {
        const std::uint8_t* lut = colormaps::get(sp.opts.cmap);
        const float vmin = sp.opts.vmin, vrange = sp.opts.vmax - sp.opts.vmin;
        const std::size_t n = std::min({ sp.x.size(), sp.y.size(), sp.z.size() });
        for (std::size_t i = 0; i < n; ++i)
            b.marker(sp.x[i], sp.y[i],
                     with_alpha(cell_color(lut, static_cast<float>(sp.z[i]), vmin, vrange),
                                sp.opts.alpha),
                     sp.opts.size, sp.opts.marker);
    }
    b.close();

    return std::move(b.g);
}

// ---------------------------------------------------------------------------
// plan_planes3d
// ---------------------------------------------------------------------------

namespace {

// The affine <image> form: three projected corners are enough, since an
// orthographic projection of a rectangle is a parallelogram and matrix(...)
// maps the unit square onto exactly that. Under perspective the same three
// corners give the documented fallback, which is a best effort rather than a
// second exact answer -- the fourth corner will not land where it belongs.
void fill_image_form(PlanePlanItem& item, const HeatmapPlot& hp,
                     const PlaneQuad& q, const Projector3D& proj) {
    item.form = PlanePlanItem::Form::Image;
    item.rgba = plane_heatmap_rgba(hp);
    item.rows = hp.rows;
    item.cols = hp.cols;

    // Image (0,0) is the top-left texel, which the quad's uv puts on corner 3;
    // (1,0) is corner 2 and (0,1) is corner 0.
    const Px3 origin  = proj.project(q.p[3].x, q.p[3].y, q.p[3].z);
    const Px3 along_u = proj.project(q.p[2].x, q.p[2].y, q.p[2].z);
    const Px3 along_v = proj.project(q.p[0].x, q.p[0].y, q.p[0].z);

    item.matrix[0] = along_u.x - origin.x;   // a
    item.matrix[1] = along_u.y - origin.y;   // b
    item.matrix[2] = along_v.x - origin.x;   // c
    item.matrix[3] = along_v.y - origin.y;   // d
    item.matrix[4] = origin.x;               // e
    item.matrix[5] = origin.y;               // f
}

// The exact perspective form: every cell projected on its own, clipped at the
// near plane. Cells are emitted in storage order rather than sorted -- a plane
// is flat, so no two of its own cells can occlude each other whatever the
// camera does, and an order that cannot matter is one fewer thing to get wrong.
void fill_cell_form(PlanePlanItem& item, const HeatmapPlot& hp,
                    PlaneOrientation orient, double offset,
                    const Projector3D& proj) {
    item.form = PlanePlanItem::Form::Polys;
    const std::uint8_t* lut = colormaps::get(hp.opts.cmap);
    const float vmin = hp.opts.vmin, vrange = hp.opts.vmax - hp.opts.vmin;
    const bool flip = (hp.opts.origin == "lower");

    std::vector<Vec3> ring(4);
    std::vector<Px3>  out;
    item.polys.reserve(static_cast<std::size_t>(hp.rows) * hp.cols);

    for (int r = 0; r < hp.rows; ++r) {
        // Data row r occupies one cell of yrange, at which end depending on
        // `origin` -- the same rule plane_heatmap_rgba() applies to the pixels.
        const int band = flip ? r : (hp.rows - 1 - r);
        const double v0 = hp.y_at(band), v1 = hp.y_at(band + 1);
        for (int c = 0; c < hp.cols; ++c) {
            const double u0 = hp.x_at(c), u1 = hp.x_at(c + 1);
            const Vec3 pts[4] = { plane_point(orient, u0, v0, offset),
                                  plane_point(orient, u1, v0, offset),
                                  plane_point(orient, u1, v1, offset),
                                  plane_point(orient, u0, v1, offset) };
            for (int i = 0; i < 4; ++i)
                ring[static_cast<std::size_t>(i)] =
                    proj.transform().to_box(pts[i].x, pts[i].y, pts[i].z);

            proj.project_polygon(ring, out);
            if (out.size() < 3) continue;

            PlanePlanItem::Poly poly;
            poly.xy.reserve(out.size() * 2);
            for (const Px3& px : out) { poly.xy.push_back(px.x); poly.xy.push_back(px.y); }
            poly.fill = cell_color(lut, hp.data[static_cast<std::size_t>(r) * hp.cols + c],
                                   vmin, vrange);
            item.polys.push_back(std::move(poly));
        }
    }
}

// The plane's non-heatmap primitives, projected. One item per batch, so the
// order they were built in is the order they come out in.
void append_geometry_items(std::vector<PlanePlanItem>& out, const PlaneSnapshot& pl,
                           std::size_t pi, float depth, const Projector3D& proj) {
    const PlaneGeometry g = plane_geometry(pl);
    if (g.empty()) return;

    const Transform3D& tf = proj.transform();
    auto to_box = [&](const Vec3& p) { return tf.to_box(p.x, p.y, p.z); };

    // **A pixel on a plane is a pixel of the plane's own raster** (step 7a).
    // Not a pixel at the box centre, which is what this converted through
    // before the plane became a little screen: the raster path now rasterizes
    // strokes and markers into that raster, so a length of `n` pixels there is
    // `n * box_per_px` box units, and the vector path has to say the same or
    // the two outputs disagree about how wide a line on a plane is.
    //
    // From plane_raster() rather than recomputed, so there is one definition
    // of how big a raster pixel is -- see PlaneRaster::box_per_px.
    const double ref = plane_raster(proj, pl.orient, pl.offset).box_per_px;

    // The plane's normal in box space: the data -> box map is an axis-aligned
    // diagonal, so the axis the orientation is normal to stays the normal
    // after it.
    Vec3 normal{ 0.0, 0.0, 0.0 };
    (&normal.x)[axis_map(pl.orient).h] = 1.0;

    // The apparent width is *measured*, not derived from the distance alone,
    // because a plane's stroke is expanded in the plane rather than
    // billboarded (see k_plane_stroke_vert). So it foreshortens with the
    // plane's tilt as well as thinning with distance, and the honest way to
    // get one number for an SVG stroke-width is to project the ribbon's own
    // two edges and measure between them -- which also keeps this exact
    // against the raster path instead of approximately right.
    auto stroke_width_at = [&](Vec3 a_box, Vec3 b_box, float width_px) {
        const Vec3 d = b_box - a_box;
        const Vec3 side = cross(d, normal);
        const double sl = length(side);
        if (sl <= 1e-12) return 0.0f;
        const Vec3 half = side * (0.5 * width_px * ref / sl);
        const Vec3 mid  = (a_box + b_box) * 0.5;
        const Px3 lo = proj.project_box(mid - half);
        const Px3 hi = proj.project_box(mid + half);
        if (!lo.in_front() || !hi.in_front()) return 0.0f;
        return static_cast<float>(std::hypot(hi.x - lo.x, hi.y - lo.y));
    };

    std::vector<Vec3> ring;
    std::vector<Px3>  clipped;

    for (const auto& batch : g.batches) {
        PlanePlanItem item;
        item.alpha = pl.opts.alpha;
        item.depth = depth;
        item.plane = pi;

        switch (batch.kind) {
            case PlaneGeometry::Batch::Kind::Tri:
                item.form = PlanePlanItem::Form::Polys;
                for (std::size_t i = batch.begin; i < batch.end && i < g.tris.size(); ++i) {
                    const auto& t = g.tris[i];
                    ring.assign({ to_box(t.p[0]), to_box(t.p[1]), to_box(t.p[2]) });
                    proj.project_polygon(ring, clipped);
                    if (clipped.size() < 3) continue;
                    PlanePlanItem::Poly poly;
                    poly.xy.reserve(clipped.size() * 2);
                    for (const Px3& q : clipped) {
                        poly.xy.push_back(q.x);
                        poly.xy.push_back(q.y);
                    }
                    poly.fill = t.fill;
                    item.polys.push_back(std::move(poly));
                }
                break;

            case PlaneGeometry::Batch::Kind::Seg:
                item.form = PlanePlanItem::Form::Strokes;
                for (std::size_t i = batch.begin; i < batch.end && i < g.segs.size(); ++i) {
                    const auto& s = g.segs[i];
                    const Vec3 ba = to_box(s.a), bb = to_box(s.b);
                    Px3 pa, pb;
                    if (!proj.project_segment(ba, bb, pa, pb)) continue;
                    PlanePlanItem::Stroke st;
                    st.xy    = { pa.x, pa.y, pb.x, pb.y };
                    st.color = s.color;
                    st.width = stroke_width_at(ba, bb, s.width_px);
                    item.strokes.push_back(std::move(st));
                }
                break;

            case PlaneGeometry::Batch::Kind::Marker:
                item.form = PlanePlanItem::Form::Markers;
                for (std::size_t i = batch.begin; i < batch.end && i < g.markers.size(); ++i) {
                    const auto& m = g.markers[i];
                    const Vec3 box = to_box(m.p);
                    if (!proj.in_front(box)) continue;
                    const Px3 q = proj.project_box(box);
                    // **A marker on a plane is now on the plane**, and step 7a
                    // reversed the rule that used to stand here: its size was a
                    // pixel size wherever it landed, because it was billboarded
                    // toward the eye. Under the screen model it is rasterized
                    // into the plane, so it foreshortens and recedes with it,
                    // and `size` names a pixel of the plane's raster.
                    //
                    // Measured the same way a stroke's width is, and for the
                    // same reason: project the marker's own extent rather than
                    // scaling by a distance, so this stays exact against the
                    // raster instead of approximately right. A tilted plane
                    // makes a circle an ellipse and one number cannot say so --
                    // §4's admitted vector concession, spent here on the mean
                    // of the two in-plane directions.
                    const Axis3Map am = axis_map(pl.orient);
                    Vec3 du{ 0.0, 0.0, 0.0 }, dv{ 0.0, 0.0, 0.0 };
                    (&du.x)[am.u] = 0.5 * m.size_px * ref;
                    (&dv.x)[am.v] = 0.5 * m.size_px * ref;
                    const Px3 qu = proj.project_box(box + du);
                    const Px3 qv = proj.project_box(box + dv);
                    const double ru = std::hypot(qu.x - q.x, qu.y - q.y);
                    const double rv = std::hypot(qv.x - q.x, qv.y - q.y);
                    const float size_px = static_cast<float>(ru + rv);
                    item.marks.push_back({ q.x, q.y, size_px, m.color, m.marker });
                }
                break;
        }

        if (!item.polys.empty() || !item.strokes.empty() || !item.marks.empty())
            out.push_back(std::move(item));
    }
}

} // namespace

std::vector<PlanePlanItem> plan_planes3d(const Projector3D& proj,
                                         const std::vector<PlaneSnapshot>& planes) {
    std::vector<PlanePlanItem> out;

    for (std::size_t pi = 0; pi < planes.size(); ++pi) {
        const PlaneSnapshot& pl = planes[pi];
        if (!plane_drawn(pl)) continue;
        const float depth = static_cast<float>(plane_distance(pl, proj));

        // Heatmaps first, then everything else -- the 2D order, in which a
        // heatmap is the backdrop the rest is drawn on.
        for (std::size_t hi = 0; hi < pl.sheet.heatmaps.size(); ++hi) {
            const HeatmapPlot& hp = pl.sheet.heatmaps[hi];
            if (hp.rows <= 0 || hp.cols <= 0) continue;

            PlanePlanItem item;
            item.alpha = pl.opts.alpha;
            item.depth = depth;
            item.plane = pi;
            item.plot  = hi;

            const std::size_t cells = static_cast<std::size_t>(hp.rows) * hp.cols;
            const PlaneQuad q = plane_heatmap_quad(hp, pl.orient, pl.offset);

            if (!proj.is_perspective()) {
                fill_image_form(item, hp, q, proj);
            } else if (cells <= kPlane3DCellCap) {
                fill_cell_form(item, hp, pl.orient, pl.offset, proj);
            } else {
                fill_image_form(item, hp, q, proj);
                item.warning = "plane " + std::to_string(pi) + ": " +
                    std::to_string(cells) + " cells exceeds the " +
                    std::to_string(kPlane3DCellCap) +
                    "-cell cap for an exact perspective plane; "
                    "placed as an affine <image>, which a projective warp is not";
            }
            out.push_back(std::move(item));
        }

        append_geometry_items(out, pl, pi, depth, proj);
    }

    // Back to front, so emitting in order is the painter's algorithm. Stable,
    // because two planes at equal distance must come out in build order or the
    // file is only usually the same file -- and because one plane's own layers
    // all carry its distance and have to stay in the order they were built.
    std::stable_sort(out.begin(), out.end(),
                     [](const PlanePlanItem& a, const PlanePlanItem& b) {
                         return a.depth > b.depth;
                     });

    // Each plane's own quad, in box space, stamped on every item it produced.
    // Done here in one pass rather than threaded through the four form
    // builders: they differ in everything except which plane they belong to,
    // and this is a property of the plane rather than of the form. Cached per
    // plane because plane_raster() re-derives the quad from the projector.
    {
        const Transform3D& tf = proj.transform();
        std::vector<char> have(planes.size(), 0);
        std::vector<std::array<Vec3, 4>> quads(planes.size());
        for (PlanePlanItem& item : out) {
            if (item.plane >= planes.size()) continue;
            if (!have[item.plane]) {
                const PlaneSnapshot& pl = planes[item.plane];
                const PlaneRaster r = plane_raster(proj, pl.orient, pl.offset);
                for (int i = 0; i < 4; ++i)
                    quads[item.plane][static_cast<std::size_t>(i)] =
                        tf.to_box(r.p[i].x, r.p[i].y, r.p[i].z);
                have[item.plane] = 1;
            }
            for (int i = 0; i < 4; ++i)
                item.quad[i] = quads[item.plane][static_cast<std::size_t>(i)];
        }
    }
    return out;
}

} // namespace sextant
