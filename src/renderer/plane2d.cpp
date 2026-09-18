#include "plane2d.h"
#include "../colormaps.h"
#include "error_bar_shape.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sextant {
    namespace {
        // Colormap lookup shared by the texture and the per-cell polygons.
        Color cell_color(const std::uint8_t* lut, float value, float vmin, float vrange) {
            float t = (vrange != 0.0f) ? (value - vmin) / vrange : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            const std::uint8_t* c = &lut[static_cast<int>(t * 255.0f) * 4];
            return {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f};
        }

        Color with_alpha(Color c, float a) {
            c.a *= std::clamp(a, 0.0f, 1.0f);
            return c;
        }
    } // namespace

    PlaneQuad plane_heatmap_quad(const HeatmapPlot& hp, PlaneOrientation orient,
                                 double offset) {
        PlaneQuad q;
        q.normal_axis = axis_map(orient).h;
        q.p[0] = plane_point(orient, hp.xrange.lo, hp.yrange.lo, offset);
        q.p[1] = plane_point(orient, hp.xrange.hi, hp.yrange.lo, offset);
        q.p[2] = plane_point(orient, hp.xrange.hi, hp.yrange.hi, offset);
        q.p[3] = plane_point(orient, hp.xrange.lo, hp.yrange.hi, offset);
        // As draw_heatmap(): uv v = 1 at yrange.lo (last uploaded row there).
        const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
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
        const double lo[3] = {tf.xmin, tf.ymin, tf.zmin};
        const double hi[3] = {tf.xmax, tf.ymax, tf.zmax};
        const double side[3] = {tf.aspect.x, tf.aspect.y, tf.aspect.z};

        PlaneRaster r;
        r.normal_axis = m.h;

        // Raster shape from the plane's box-space extent (not the frame's aspect),
        // so texels are square and markers stay circular.
        const double su = std::fabs(side[m.u]), sv = std::fabs(side[m.v]);
        const double big = std::max(su, sv);
        const int cap = std::max(1, max_dim);
        auto scale = [&](double s) {
            if (big <= 0.0) return cap;
            return std::clamp(static_cast<int>(std::lround(cap * (s / big))), 1, cap);
        };
        r.w = scale(su);
        r.h = scale(sv);
        // One raster pixel in box units, from the longer side, so both directions
        // agree despite rounding.
        r.box_per_px = big > 0.0 ? big / static_cast<double>(cap) : 0.0;

        // The plane's 2D transform, limits as declared, so a reversed axis mirrors
        // the raster (and the quad below uses the same values).
        r.tr.xmin = lo[m.u];
        r.tr.xmax = hi[m.u];
        r.tr.ymin = lo[m.v];
        r.tr.ymax = hi[m.v];
        r.tr.px = 0.0f;
        r.tr.py = 0.0f;
        r.tr.pw = static_cast<float>(r.w);
        r.tr.ph = static_cast<float>(r.h);
        r.tr.win_w = r.tr.pw;
        r.tr.win_h = r.tr.ph;

        const double uu[4] = {r.tr.xmin, r.tr.xmax, r.tr.xmax, r.tr.xmin};
        const double vv[4] = {r.tr.ymin, r.tr.ymin, r.tr.ymax, r.tr.ymax};
        for (int i = 0; i < 4; ++i) {
            r.p[i] = plane_point(orient, uu[i], vv[i], offset);
            r.uv[i][0] = (i == 1 || i == 2) ? 1.0f : 0.0f;
            // Opposite v to plane_heatmap_quad(): this samples a rendered
            // framebuffer (row 0 at the bottom), not an uploaded image.
            r.uv[i][1] = (i >= 2) ? 1.0f : 0.0f;
        }
        return r;
    }

    std::vector<std::uint8_t> plane_heatmap_rgba(const HeatmapPlot& hp) {
        std::vector<std::uint8_t> out;
        if (hp.rows <= 0 || hp.cols <= 0) return out;

        const std::uint8_t* lut = colormaps::get(hp.opts.cmap);
        const float vmin = hp.opts.vmin, vrange = hp.opts.vmax - hp.opts.vmin;
        // Row 0 of the output is the yrange.hi edge; flip for origin "lower".
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
        // Centre of the plane's contents (from the sheet's 2D bounds), not its face.
        const DataBounds b = auto_scale(p.sheet.all(), 0.0);
        const Vec3 mid = plane_point(p.orient, (b.xmin + b.xmax) * 0.5,
                                     (b.ymin + b.ymax) * 0.5, p.offset);
        // Distance from eye_coord(), comparable with bar3d_plot_distance().
        const Vec3 box = proj.transform().to_box(mid.x, mid.y, mid.z);
        return length(box - eye_coord(proj));
    }

    // ---------------------------------------------------------------------------
    // plane_geometry: the 2D kinds as primitives in the plane
    // ---------------------------------------------------------------------------
    namespace {
        // Accumulates primitives and keeps the batch list in step; `open` extends the
        // last batch when it is the same kind.
        struct GeometryBuilder {
            PlaneGeometry g;
            PlaneOrientation orient = PlaneOrientation::XY;
            double offset = 0.0;

            Vec3 at(double u, double v) const { return plane_point(orient, u, v, offset); }

            void open(PlaneGeometry::Batch::Kind k) {
                const std::size_t n = size_of(k);
                if (!g.batches.empty() && g.batches.back().kind == k && g.batches.back().end == n)
                    return;
                g.batches.push_back({k, n, n});
            }

            void close() {
                if (g.batches.empty()) return;
                auto& b = g.batches.back();
                b.end = size_of(b.kind);
                if (b.end == b.begin) g.batches.pop_back(); // nothing was added
            }

            void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Color fill) {
                g.tris.push_back({{a, b, c}, fill});
                g.tris.push_back({{a, c, d}, fill});
            }

            // An axis-aligned rectangle from two opposite corners; degenerate ones are
            // dropped.
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
                g.segs.push_back({at(u0, v0), at(u1, v1), c, w});
            }

            void marker(double u, double v, Color c, float size_px, MarkerStyle m) {
                g.markers.push_back({at(u, v), c, size_px, m});
            }

        private:
            std::size_t size_of(PlaneGeometry::Batch::Kind k) const {
                switch (k) {
                    case PlaneGeometry::Batch::Kind::Tri: return g.tris.size();
                    case PlaneGeometry::Batch::Kind::Seg: return g.segs.size();
                    case PlaneGeometry::Batch::Kind::Marker: return g.markers.size();
                }
                return 0;
            }
        };

        // Frame units per pixel on each in-plane axis, for error-bar caps and the
        // box's fallback width.
        struct ErrScale {
            double px_u, px_v;
        };

        // One series' error bars: box first, then whiskers (whisker_segments()), as in
        // 2D.
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
                    // Box data where given, else the pixel-derived fallback width (2D rule).
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

        // Error-bar pixel lengths are resolved against the plane's extent per
        // in-plane axis (approximate).
        const DataBounds ext = auto_scale(s.all(), 0.0);
        constexpr double kPixelsAcross = 400.0; // a plane, in round numbers
        const double px_u = std::max(1e-300, ext.xmax - ext.xmin) / kPixelsAcross;
        const double px_v = std::max(1e-300, ext.ymax - ext.ymin) / kPixelsAcross;

        // --- bar bodies, then bar outlines and lines (the 2D order).
        b.open(Kind::Tri);
        for (const auto& bp: s.bars) {
            const double half = bp.bar_width * 0.5;
            const Color c = with_alpha(bp.opts.color, bp.opts.alpha);
            for (std::size_t i = 0; i < bp.centers.size() && i < bp.heights.size(); ++i)
                b.rect(bp.centers[i] - half, 0.0, bp.centers[i] + half, bp.heights[i], c);
        }
        b.close();

        b.open(Kind::Seg);
        for (const auto& bp: s.bars) {
            if (bp.opts.linewidth <= 0.0f) continue;
            const double half = bp.bar_width * 0.5;
            for (std::size_t i = 0; i < bp.centers.size() && i < bp.heights.size(); ++i)
                b.rect_outline(bp.centers[i] - half, 0.0, bp.centers[i] + half,
                               bp.heights[i], bp.opts.edgecolor, bp.opts.linewidth);
        }
        // No dashing in the scene (a world-space ribbon has no pixel arc length);
        // non-solid lines on a plane draw solid.
        for (const auto& lp: s.lines) {
            if (lp.opts.linestyle == LineStyle::None || lp.opts.linewidth <= 0.0f) continue;
            const Color c = with_alpha(lp.opts.color, lp.opts.alpha);
            // segment_count()/segment_ends() decide closure; the length guard drops
            // segments past a shorter y vector.
            const std::size_t n = std::min(lp.x.size(), lp.y.size());
            for (std::size_t s = 0; s < lp.segment_count(); ++s) {
                std::size_t i, j;
                lp.segment_ends(s, i, j);
                if (i >= n || j >= n) continue;
                b.seg_uv(lp.x[i], lp.y[i], lp.x[j], lp.y[j], c, lp.opts.linewidth);
            }
        }
        b.close();

        // --- error bars, in the 2D order: after fills, before markers.
        {
            GeometryBuilder fills, strokes;
            fills.orient = strokes.orient = p.orient;
            fills.offset = strokes.offset = p.offset;

            auto run = [&](const CowVec<double>& xs, const CowVec<double>& ys,
                           const ErrorBarData& err, const ErrorBarOptions& style,
                           const Color& fallback) {
                error_bars(fills, strokes, xs, ys, err, style, fallback, ErrScale{px_u, px_v});
            };
            for (const auto& lp: s.lines)
                run(lp.x, lp.y, lp.err, lp.opts.errorbar, lp.opts.color);
            // Error bars hang off the tip (heights), not the baseline.
            for (const auto& bp: s.bars)
                run(bp.centers, bp.heights, bp.err, bp.opts.errorbar, bp.opts.edgecolor);
            for (const auto& sp: s.scatters)
                run(sp.x, sp.y, sp.err, sp.opts.errorbar, sp.opts.color);
            // scatter_z has no single color, so black (as in 2D).
            for (const auto& sp: s.scatter_z)
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

        // --- markers, last.
        b.open(Kind::Marker);
        for (const auto& sp: s.scatters) {
            const Color c = with_alpha(sp.opts.color, sp.opts.alpha);
            const std::size_t n = std::min(sp.x.size(), sp.y.size());
            for (std::size_t i = 0; i < n; ++i)
                b.marker(sp.x[i], sp.y[i], c, sp.opts.size, sp.opts.marker);
        }
        for (const auto& sp: s.scatter_z) {
            const std::uint8_t* lut = colormaps::get(sp.opts.cmap);
            const float vmin = sp.opts.vmin, vrange = sp.opts.vmax - sp.opts.vmin;
            const std::size_t n = std::min({sp.x.size(), sp.y.size(), sp.z.size()});
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
        // The affine <image> form from three projected corners: exact under
        // orthographic, a best-effort fallback under perspective.
        void fill_image_form(PlanePlanItem& item, const HeatmapPlot& hp,
                             const PlaneQuad& q, const Projector3D& proj) {
            item.form = PlanePlanItem::Form::Image;
            item.rgba = plane_heatmap_rgba(hp);
            item.rows = hp.rows;
            item.cols = hp.cols;

            // Image (0,0) = top-left texel = corner 3; (1,0) = corner 2; (0,1) = corner 0.
            const Px3 origin = proj.project(q.p[3].x, q.p[3].y, q.p[3].z);
            const Px3 along_u = proj.project(q.p[2].x, q.p[2].y, q.p[2].z);
            const Px3 along_v = proj.project(q.p[0].x, q.p[0].y, q.p[0].z);

            item.matrix[0] = along_u.x - origin.x; // a
            item.matrix[1] = along_u.y - origin.y; // b
            item.matrix[2] = along_v.x - origin.x; // c
            item.matrix[3] = along_v.y - origin.y; // d
            item.matrix[4] = origin.x; // e
            item.matrix[5] = origin.y; // f
        }

        // The exact perspective form: each cell projected and near-clipped, in storage
        // order (a flat plane can't occlude itself).
        void fill_cell_form(PlanePlanItem& item, const HeatmapPlot& hp,
                            PlaneOrientation orient, double offset,
                            const Projector3D& proj) {
            item.form = PlanePlanItem::Form::Polys;
            const std::uint8_t* lut = colormaps::get(hp.opts.cmap);
            const float vmin = hp.opts.vmin, vrange = hp.opts.vmax - hp.opts.vmin;
            const bool flip = (hp.opts.origin == "lower");

            std::vector<Vec3> ring(4);
            std::vector<Px3> out;
            item.polys.reserve(static_cast<std::size_t>(hp.rows) * hp.cols);

            for (int r = 0; r < hp.rows; ++r) {
                // Row r's position in yrange depends on `origin` (as plane_heatmap_rgba()).
                const int band = flip ? r : (hp.rows - 1 - r);
                const double v0 = hp.y_at(band), v1 = hp.y_at(band + 1);
                for (int c = 0; c < hp.cols; ++c) {
                    const double u0 = hp.x_at(c), u1 = hp.x_at(c + 1);
                    const Vec3 pts[4] = {
                        plane_point(orient, u0, v0, offset),
                        plane_point(orient, u1, v0, offset),
                        plane_point(orient, u1, v1, offset),
                        plane_point(orient, u0, v1, offset)
                    };
                    for (int i = 0; i < 4; ++i)
                        ring[static_cast<std::size_t>(i)] =
                                proj.transform().to_box(pts[i].x, pts[i].y, pts[i].z);

                    proj.project_polygon(ring, out);
                    if (out.size() < 3) continue;

                    PlanePlanItem::Poly poly;
                    poly.xy.reserve(out.size() * 2);
                    for (const Px3& px: out) {
                        poly.xy.push_back(px.x);
                        poly.xy.push_back(px.y);
                    }
                    poly.fill = cell_color(lut, hp.data[static_cast<std::size_t>(r) * hp.cols + c],
                                           vmin, vrange);
                    item.polys.push_back(std::move(poly));
                }
            }
        }

        // The plane's non-heatmap primitives, projected, one item per batch.
        void append_geometry_items(std::vector<PlanePlanItem>& out, const PlaneSnapshot& pl,
                                   std::size_t pi, float depth, const Projector3D& proj) {
            const PlaneGeometry g = plane_geometry(pl);
            if (g.empty()) return;

            const Transform3D& tf = proj.transform();
            auto to_box = [&](const Vec3& p) { return tf.to_box(p.x, p.y, p.z); };

            // A pixel on a plane is a pixel of its raster (`box_per_px`), matching
            // what the raster path draws.
            const double ref = plane_raster(proj, pl.orient, pl.offset).box_per_px;

            // The plane's normal in box space (axis-aligned, unchanged by data -> box).
            Vec3 normal{0.0, 0.0, 0.0};
            (&normal.x)[axis_map(pl.orient).h] = 1.0;

            // Stroke widths are measured by projecting the ribbon's two edges (strokes
            // lie in the plane), so they foreshorten exactly as in the raster.
            auto stroke_width_at = [&](Vec3 a_box, Vec3 b_box, float width_px) {
                const Vec3 d = b_box - a_box;
                const Vec3 side = cross(d, normal);
                const double sl = length(side);
                if (sl <= 1e-12) return 0.0f;
                const Vec3 half = side * (0.5 * width_px * ref / sl);
                const Vec3 mid = (a_box + b_box) * 0.5;
                const Px3 lo = proj.project_box(mid - half);
                const Px3 hi = proj.project_box(mid + half);
                if (!lo.in_front() || !hi.in_front()) return 0.0f;
                return static_cast<float>(std::hypot(hi.x - lo.x, hi.y - lo.y));
            };

            std::vector<Vec3> ring;
            std::vector<Px3> clipped;

            for (const auto& batch: g.batches) {
                PlanePlanItem item;
                item.alpha = pl.opts.alpha;
                item.depth = depth;
                item.plane = pi;

                switch (batch.kind) {
                    case PlaneGeometry::Batch::Kind::Tri:
                        item.form = PlanePlanItem::Form::Polys;
                        for (std::size_t i = batch.begin; i < batch.end && i < g.tris.size(); ++i) {
                            const auto& t = g.tris[i];
                            ring.assign({to_box(t.p[0]), to_box(t.p[1]), to_box(t.p[2])});
                            proj.project_polygon(ring, clipped);
                            if (clipped.size() < 3) continue;
                            PlanePlanItem::Poly poly;
                            poly.xy.reserve(clipped.size() * 2);
                            for (const Px3& q: clipped) {
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
                            st.xy = {pa.x, pa.y, pb.x, pb.y};
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
                            // Markers are drawn into the plane (they foreshorten);
                            // `size` is in raster pixels. Measured by projecting the
                            // marker's extent, averaged over the two in-plane directions.
                            const Axis3Map am = axis_map(pl.orient);
                            Vec3 du{0.0, 0.0, 0.0}, dv{0.0, 0.0, 0.0};
                            (&du.x)[am.u] = 0.5 * m.size_px * ref;
                            (&dv.x)[am.v] = 0.5 * m.size_px * ref;
                            const Px3 qu = proj.project_box(box + du);
                            const Px3 qv = proj.project_box(box + dv);
                            const double ru = std::hypot(qu.x - q.x, qu.y - q.y);
                            const double rv = std::hypot(qv.x - q.x, qv.y - q.y);
                            const float size_px = static_cast<float>(ru + rv);
                            item.marks.push_back({q.x, q.y, size_px, m.color, m.marker});
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

            // Heatmaps first (the backdrop), then everything else.
            for (std::size_t hi = 0; hi < pl.sheet.heatmaps.size(); ++hi) {
                const HeatmapPlot& hp = pl.sheet.heatmaps[hi];
                if (hp.rows <= 0 || hp.cols <= 0) continue;

                PlanePlanItem item;
                item.alpha = pl.opts.alpha;
                item.depth = depth;
                item.plane = pi;
                item.plot = hi;

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

        // Back to front, stable (deterministic output; a plane's layers keep build
        // order).
        std::stable_sort(out.begin(), out.end(),
                         [](const PlanePlanItem& a, const PlanePlanItem& b) {
                             return a.depth > b.depth;
                         });

        // Stamp each plane's box-space quad on its items (cached per plane).
        {
            const Transform3D& tf = proj.transform();
            std::vector<char> have(planes.size(), 0);
            std::vector<std::array<Vec3, 4>> quads(planes.size());
            for (PlanePlanItem& item: out) {
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
