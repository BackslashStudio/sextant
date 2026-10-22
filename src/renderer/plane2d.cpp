#include "plane2d.h"
#include "../colormaps.h"
#include "../fill_geometry.h"
#include "../heatmap_pixels.h"
#include "../heatmap_tri_geometry.h"
#include "../contour_plot.h"
#include "../quiver_geometry.h"
#include "../shape_geometry.h"
#include "../reference_lines.h"
#include "../line_steps.h"
#include "error_bar_shape.h"
#include "marker_shape.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace sextant {
    namespace {
        // A scatter_z point's colormap colour; a non-linear norm normalises its
        // z in double, as the 2D outputs do.
        Color point_color(const ScatterZPlot& sp, const std::uint8_t* lut, ColorNorm::Kind kind, std::size_t i) {
            const float vmin = sp.opts.vmin, vrange = sp.opts.vmax - sp.opts.vmin;
            const float value = static_cast<float>(sp.z[i]);
            const double t = kind == ColorNorm::Kind::Linear
                                 ? ((vrange != 0.0f) ? (value - vmin) / vrange : 0.0)
                                 : norm_t(kind, sp.opts.norm, sp.opts.vmin, sp.opts.vmax, sp.z[i]);
            const std::uint8_t* c = &lut[colormaps::index(t, sp.opts.cmap.is_cyclic()) * 4];
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

        const HeatmapPixels px(hp);
        // Row 0 of the output is the yrange.hi edge; flip for origin "lower".
        const bool flip = (hp.opts.origin == "lower");

        out.resize(static_cast<std::size_t>(hp.rows) * hp.cols * 4);
        for (int r = 0; r < hp.rows; ++r) {
            const int src = flip ? (hp.rows - 1 - r) : r;
            for (int c = 0; c < hp.cols; ++c)
                px.rgba(static_cast<std::size_t>(src) * hp.cols + c,
                        &out[(static_cast<std::size_t>(r) * hp.cols + c) * 4]);
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

            void marker(double u, double v, Color c, float size_px, MarkerStyle m,
                        Color edge = {}, float edge_px = 0.0f) {
                g.markers.push_back({at(u, v), c, size_px, m, edge, edge_px});
            }

            // A heatmap, by its index in the sheet: a batch of its own, never
            // extended.
            void heatmap(std::size_t index) {
                g.batches.push_back({PlaneGeometry::Batch::Kind::Heatmap, index, index + 1});
            }

            // A heatmap_tri, likewise.
            void heatmap_tri(std::size_t index) {
                g.batches.push_back({PlaneGeometry::Batch::Kind::HeatmapTri, index, index + 1});
            }

            // A contourf, likewise.
            void contour_fill(std::size_t index) {
                g.batches.push_back({PlaneGeometry::Batch::Kind::Contour, index, index + 1});
            }

        private:
            std::size_t size_of(PlaneGeometry::Batch::Kind k) const {
                switch (k) {
                    case PlaneGeometry::Batch::Kind::Tri: return g.tris.size();
                    case PlaneGeometry::Batch::Kind::Seg: return g.segs.size();
                    case PlaneGeometry::Batch::Kind::Marker: return g.markers.size();
                    case PlaneGeometry::Batch::Kind::Heatmap:
                    case PlaneGeometry::Batch::Kind::HeatmapTri:
                    case PlaneGeometry::Batch::Kind::Contour: return 0; // never opened
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
        // 2D. `alpha` is the series'.
        void error_bars(GeometryBuilder& fills, GeometryBuilder& strokes,
                        const CowVec<double>& xs, const CowVec<double>& ys,
                        const ErrorBarData& err, const ErrorBarOptions& style,
                        const Color& fallback, float alpha, const ErrScale& sc) {
            if (err.empty() || style.linewidth <= 0.0f) return;
            const Color c = with_alpha(style.color.value_or(fallback), alpha);
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

    PlaneGeometry plane_geometry(const PlaneSnapshot& p, const CoordTransform* raster_tr) {
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

        // The transform shapes and spans are laid out in, as the raster path does:
        // the plane's own, else (no projector) a 400 px stand-in over the sheet's
        // extent. Its pixels are taken back to plane coordinates.
        CoordTransform st{};
        if (raster_tr) {
            st = *raster_tr;
        } else {
            st.xmin = ext.xmin; st.xmax = ext.xmax; st.ymin = ext.ymin; st.ymax = ext.ymax;
            st.px = 0.0f; st.py = 0.0f; st.pw = kPixelsAcross; st.ph = kPixelsAcross;
        }

        // One object's error bars: boxes, then whiskers (error_bars()).
        auto errs = [&](const CowVec<double>& xs, const CowVec<double>& ys,
                        const ErrorBarData& err, const ErrorBarOptions& style,
                        const Color& fallback, float alpha) {
            if (err.empty()) return;
            GeometryBuilder fills, strokes;
            fills.orient = strokes.orient = p.orient;
            fills.offset = strokes.offset = p.offset;
            error_bars(fills, strokes, xs, ys, err, style, fallback, alpha, ErrScale{px_u, px_v});
            b.open(Kind::Tri);
            b.g.tris.insert(b.g.tris.end(), fills.g.tris.begin(), fills.g.tris.end());
            b.close();
            b.open(Kind::Seg);
            b.g.segs.insert(b.g.segs.end(), strokes.g.segs.begin(), strokes.g.segs.end());
            b.close();
        };

        // In draw order (draw_order()), as the raster (DataRenderer::draw_plots()): a bar or line
        // object's error bars over it, a scatter's under its markers.
        for (const ObjectRef& r: draw_order(s)) {
            switch (r.kind) {
                case PlotKind::Heatmap:
                    b.heatmap(r.index);
                    break;

                case PlotKind::HeatmapTri:
                    b.heatmap_tri(r.index);
                    break;

                case PlotKind::Contour: {
                    // A contourf as heatmap_tri's resampling; a contour's lines as
                    // strokes, whole (no labels on a plane), level by level.
                    const ContourPlot& cp = s.contours[r.index];
                    if (!cp.geom) break;
                    if (cp.opts.filled) {
                        if (cp.geom->levels.size() >= 2 && !cp.geom->fill.tri.empty()) b.contour_fill(r.index);
                        break;
                    }
                    if (!(cp.opts.linewidth > 0.0f)) break;
                    b.open(Kind::Seg);
                    for (std::size_t k = 0; k < cp.geom->lines.size(); ++k) {
                        const std::optional<Color> c = contour_line_color(cp, cp.geom->line_level[k]);
                        if (!c) continue;
                        const ContourLine& l = cp.geom->lines[k];
                        for (std::size_t i = 0; i + 1 < l.x.size(); ++i)
                            b.seg_uv(l.x[i], l.y[i], l.x[i + 1], l.y[i + 1], *c, cp.opts.linewidth);
                    }
                    b.close();
                    break;
                }

                case PlotKind::Bar: {
                    const auto& bp = s.bars[r.index];
                    const double half = bp.bar_width * 0.5;
                    const std::size_t n = std::min(bp.centers.size(), bp.heights.size());
                    const Color c = with_alpha(*bp.opts.color, bp.opts.alpha);
                    // Bar i's corners (u0, v0, u1, v1): category edges by base and
                    // tip, swapped when it lies along u.
                    auto corners = [&](std::size_t i) {
                        const double c0 = bp.centers[i] - half, c1 = bp.centers[i] + half;
                        const double v0 = bp.base_at(i), v1 = bp.tip_at(i);
                        return bp.horizontal() ? std::array{v0, c0, v1, c1}
                                               : std::array{c0, v0, c1, v1};
                    };
                    b.open(Kind::Tri);
                    for (std::size_t i = 0; i < n; ++i) {
                        const auto [u0, v0, u1, v1] = corners(i);
                        b.rect(u0, v0, u1, v1, c);
                    }
                    b.close();
                    if (bp.opts.linewidth > 0.0f) {
                        b.open(Kind::Seg);
                        for (std::size_t i = 0; i < n; ++i) {
                            const auto [u0, v0, u1, v1] = corners(i);
                            b.rect_outline(u0, v0, u1, v1, bar_edge(bp), bp.opts.linewidth);
                        }
                        b.close();
                    }
                    // Error bars hang off the tip, not the base.
                    if (!bp.err.empty()) {
                        const XYColumns a = bar_anchors(bp);
                        errs(a.x, a.y, bp.err, bp.opts.errorbar, bp.opts.edgecolor, bp.opts.alpha);
                    }
                    break;
                }

                case PlotKind::Line: {
                    // No dashing in the scene (a world-space ribbon has no pixel arc
                    // length); non-solid lines on a plane draw solid.
                    const auto& lp = s.lines[r.index];
                    if (lp.opts.linestyle != LineStyle::None && lp.opts.linewidth > 0.0f) {
                        const Color c = with_alpha(*lp.opts.color, lp.opts.alpha);
                        // The stroked path (steps, if any) over the points both
                        // vectors have; `loop` adds the closing segment.
                        const std::size_t n = std::min(lp.x.size(), lp.y.size());
                        StrokePath sp;
                        sp.build(lp.x.data(), lp.y.data(), n, lp.opts.drawstyle, lp.opts.loop);
                        b.open(Kind::Seg);
                        for (std::size_t k = 0; k < sp.segment_count(); ++k) {
                            const std::size_t j = k + 1 == sp.n ? 0 : k + 1;
                            b.seg_uv(sp.x[k], sp.y[k], sp.x[j], sp.y[j], c, lp.opts.linewidth);
                        }
                        b.close();
                    }
                    errs(lp.x, lp.y, lp.err, lp.opts.errorbar, *lp.opts.color, lp.opts.alpha);
                    break;
                }

                case PlotKind::Fill: {
                    // The regions' triangles, then their outlines (solid, as a
                    // plane's lines), in plane coordinates (no scales on a plane).
                    const auto& fp = s.fills[r.index];
                    const std::vector<FillRun> runs = fill_runs(fp, AxisScale{}, AxisScale{});
                    const bool vert = fp.vertical();
                    auto uv = [vert](double run, double v) {
                        return vert ? std::array{v, run} : std::array{run, v};
                    };
                    const Color c = with_alpha(*fp.opts.color, fp.opts.alpha);
                    b.open(Kind::Tri);
                    for (const FillRun& run : runs)
                        fill_triangles(run, [&](double r0, double v0, double r1, double v1,
                                                double r2, double v2) {
                            const auto [u0, w0] = uv(r0, v0);
                            const auto [u1, w1] = uv(r1, v1);
                            const auto [u2, w2] = uv(r2, v2);
                            b.g.tris.push_back({{b.at(u0, w0), b.at(u1, w1), b.at(u2, w2)}, c});
                        });
                    b.close();
                    if (fp.opts.linewidth > 0.0f && fp.opts.linestyle != LineStyle::None) {
                        const Color ec = fill_edge(fp);
                        b.open(Kind::Seg);
                        for (const FillRun& run : runs) {
                            std::vector<std::array<double, 2>> ring;
                            fill_outline(run, [&](double rr, double v) { ring.push_back(uv(rr, v)); });
                            for (std::size_t k = 0; k < ring.size(); ++k) {
                                const auto& p0 = ring[k];
                                const auto& p1 = ring[(k + 1) % ring.size()];
                                b.seg_uv(p0[0], p0[1], p1[0], p1[1], ec, fp.opts.linewidth);
                            }
                        }
                        b.close();
                    }
                    break;
                }

                case PlotKind::Poly:
                case PlotKind::Circle:
                case PlotKind::Wedge: {
                    ShapeGeom g;
                    const Color* col = nullptr;
                    float alpha = 1.0f, lw = 0.0f;
                    LineStyle ls = LineStyle::None;
                    Color ec;
                    const auto take = [&](const auto& sp) {
                        g = shape_geometry(sp, st);
                        col = &*sp.opts.color; alpha = sp.opts.alpha; lw = sp.opts.linewidth; ls = sp.opts.linestyle;
                        ec = shape_edge(sp);
                    };
                    if (r.kind == PlotKind::Poly) take(s.polys[r.index]);
                    else if (r.kind == PlotKind::Circle) take(s.circles[r.index]);
                    else take(s.wedges[r.index]);
                    if (g.empty()) break;
                    const auto uv = [&](const ShapePt& q) {
                        return std::array{st.to_data_x(q.x), st.to_data_y(q.y)};
                    };
                    const Color c = with_alpha(*col, alpha);
                    b.open(Kind::Tri);
                    for (std::size_t i = 0; i + 2 < g.tris.size(); i += 3) {
                        const auto a = uv(g.tris[i]), bb = uv(g.tris[i + 1]), cc = uv(g.tris[i + 2]);
                        b.g.tris.push_back({{b.at(a[0], a[1]), b.at(bb[0], bb[1]), b.at(cc[0], cc[1])}, c});
                    }
                    b.close();
                    if (lw > 0.0f && ls != LineStyle::None) {
                        b.open(Kind::Seg);
                        for (const std::vector<ShapePt>& ring : g.rings)
                            for (std::size_t k = 0; k < ring.size(); ++k) {
                                const auto p0 = uv(ring[k]), p1 = uv(ring[(k + 1) % ring.size()]);
                                b.seg_uv(p0[0], p0[1], p1[0], p1[1], ec, lw);
                            }
                        b.close();
                    }
                    break;
                }

                case PlotKind::AxSpan: {
                    // The band in the plane's own pixels (held to the plane), then its
                    // outline.
                    const auto& sp = s.axspans[r.index];
                    const std::optional<RefRect> rr = axspan_rect(sp, st, 0.0f);
                    if (!rr) break;
                    const double u0 = st.to_data_x(rr->x0), u1 = st.to_data_x(rr->x1);
                    const double v0 = st.to_data_y(rr->y1), v1 = st.to_data_y(rr->y0);
                    b.open(Kind::Tri);
                    b.rect(u0, v0, u1, v1, with_alpha(*sp.opts.color, sp.opts.alpha));
                    b.close();
                    if (sp.opts.linewidth > 0.0f) {
                        b.open(Kind::Seg);
                        b.rect_outline(u0, v0, u1, v1, axspan_edge(sp), sp.opts.linewidth);
                        b.close();
                    }
                    break;
                }

                case PlotKind::Quiver: {
                    // Each arrow's triangles in plane coordinates. Its pixel sizes
                    // (shaft, head) go through px_u/px_v, as error-bar caps do.
                    const auto& qp = s.quivers[r.index];
                    b.open(Kind::Tri);
                    for_each_quiver_arrow(qp, AxisScale{}, AxisScale{},
                                          [&](std::size_t i, double tx, double ty, double hx, double hy) {
                        const Color c = with_alpha(quiver_arrow_color(qp, i), qp.opts.alpha);
                        float pts[14];
                        const int n = arrow_outline(static_cast<float>(tx / px_u),
                                                    static_cast<float>(ty / px_v),
                                                    static_cast<float>(hx / px_u),
                                                    static_cast<float>(hy / px_v), qp.opts, pts);
                        auto at = [&](int k) { return b.at(pts[2 * k] * px_u, pts[2 * k + 1] * px_v); };
                        arrow_triangles(n, [&](int i, int j, int k) {
                            b.g.tris.push_back({{at(i), at(j), at(k)}, c});
                        });
                    });
                    b.close();
                    break;
                }

                case PlotKind::Scatter: {
                    const auto& sp = s.scatters[r.index];
                    errs(sp.x, sp.y, sp.err, sp.opts.errorbar, *sp.opts.color, sp.opts.alpha);
                    const Color c = with_alpha(*sp.opts.color, marker_face_alpha(sp.opts));
                    const std::size_t n = std::min(sp.x.size(), sp.y.size());
                    // Unset edgecolor = the fill's color; alpha is its own times `alpha`.
                    const Color ec = with_alpha(sp.opts.edgecolor ? *sp.opts.edgecolor : *sp.opts.color,
                                                sp.opts.alpha);
                    const float ew = std::max(0.0f, sp.opts.edge_linewidth);
                    b.open(Kind::Marker);
                    for (std::size_t i = 0; i < n; ++i)
                        if (!marker_hidden(sp, i))
                            b.marker(sp.x[i], sp.y[i], c, marker_size_at(sp, i), sp.opts.marker, ec, ew);
                    b.close();
                    break;
                }

                case PlotKind::ScatterZ: {
                    const auto& sp = s.scatter_z[r.index];
                    // scatter_z has no single color, so black (as in 2D).
                    errs(sp.x, sp.y, sp.err, sp.opts.errorbar, Color::Black, sp.opts.alpha);
                    const std::uint8_t* lut = colormaps::get(sp.opts.cmap);
                    const ColorNorm::Kind kind = norm_in_force(sp.opts.norm, sp.opts.vmin, sp.opts.vmax);
                    const std::size_t n = std::min({sp.x.size(), sp.y.size(), sp.z.size()});
                    const float ew = std::max(0.0f, sp.opts.edge_linewidth);
                    b.open(Kind::Marker);
                    for (std::size_t i = 0; i < n; ++i) {
                        if (marker_hidden(sp, i)) continue;
                        const Color face = point_color(sp, lut, kind, i);
                        Color ec = sp.opts.edgecolor ? *sp.opts.edgecolor : face;
                        ec.a = std::clamp((sp.opts.edgecolor ? ec.a : 1.0f) * sp.opts.alpha,
                                          0.0f, 1.0f);
                        b.marker(sp.x[i], sp.y[i], with_alpha(face, marker_face_alpha(sp.opts)),
                                 marker_size_at(sp, i), sp.opts.marker, ec, ew);
                    }
                    b.close();
                    break;
                }

                default: break;
            }
        }

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
            const HeatmapPixels px(hp);
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

                    // A transparent (NaN) cell draws nothing.
                    const Color fill = px.color(static_cast<std::size_t>(r) * hp.cols + c);
                    if (fill.a <= 0.0f) continue;
                    proj.project_polygon(ring, out);
                    if (out.size() < 3) continue;

                    PlanePlanItem::Poly poly;
                    poly.xy.reserve(out.size() * 2);
                    for (const Px3& px: out) {
                        poly.xy.push_back(px.x);
                        poly.xy.push_back(px.y);
                    }
                    poly.fill = fill;
                    item.polys.push_back(std::move(poly));
                }
            }
        }

        // A mesh over the cell cap as an even grid for the affine <image>: up to
        // four samples per cell along each axis (2048 at most), each the cell
        // under its centre.
        HeatmapPlot resampled_mesh(const HeatmapPlot& hp) {
            HeatmapPlot even = hp;
            even.x_edges = std::vector<double>{};
            even.y_edges = std::vector<double>{};
            even.cols = std::min(2048, 4 * hp.cols);
            even.rows = std::min(2048, 4 * hp.rows);
            const std::size_t n = static_cast<std::size_t>(hp.channels);
            std::vector<float> data(static_cast<std::size_t>(even.rows) * even.cols * n);
            std::vector<std::size_t> col_of(static_cast<std::size_t>(even.cols));
            for (int i = 0; i < even.cols; ++i)
                col_of[static_cast<std::size_t>(i)] = static_cast<std::size_t>(std::clamp(
                    static_cast<int>(hp.col_at(even.x_at(i + 0.5))), 0, hp.cols - 1));
            for (int j = 0; j < even.rows; ++j) {
                const auto r = static_cast<std::size_t>(std::clamp(
                    static_cast<int>(hp.row_at(even.y_at(j + 0.5))), 0, hp.rows - 1));
                for (int i = 0; i < even.cols; ++i) {
                    const std::size_t src = (r * static_cast<std::size_t>(hp.cols) + col_of[static_cast<std::size_t>(i)]) * n;
                    const std::size_t dst = (static_cast<std::size_t>(j) * even.cols + static_cast<std::size_t>(i)) * n;
                    for (std::size_t k = 0; k < n; ++k) data[dst + k] = hp.data[src + k];
                }
            }
            even.data = std::move(data);
            return even;
        }

        // One heatmap as an item: an affine <image> under orthographic, cell
        // polygons under perspective (up to kPlane3DCellCap). A mesh is cell
        // polygons in both projections; over the cap, an even resampling as an
        // <image>.
        // `hi` is the object's index in its kind, for the item's provenance.
        void append_heatmap_item(std::vector<PlanePlanItem>& out, const PlaneSnapshot& pl,
                                 std::size_t pi, const HeatmapPlot& hp, std::size_t hi, float depth,
                                 const Projector3D& proj) {
            if (hp.rows <= 0 || hp.cols <= 0) return;

            PlanePlanItem item;
            item.alpha = pl.opts.alpha;
            item.depth = depth;
            item.plane = pi;
            item.plot = hi;

            const std::size_t cells = static_cast<std::size_t>(hp.rows) * hp.cols;
            const PlaneQuad q = plane_heatmap_quad(hp, pl.orient, pl.offset);

            if (hp.mesh()) {
                if (cells <= kPlane3DCellCap) {
                    fill_cell_form(item, hp, pl.orient, pl.offset, proj);
                } else {
                    fill_image_form(item, resampled_mesh(hp), q, proj);
                    item.warning = "plane " + std::to_string(pi) + ": " +
                                   std::to_string(cells) + " cells of a heatmap_rec() exceed the " +
                                   std::to_string(kPlane3DCellCap) +
                                   "-cell cap for cell polygons; resampled to an even <image>";
                }
            } else if (!proj.is_perspective()) {
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

        // The plane's contents, projected, one item per batch.
        void append_geometry_items(std::vector<PlanePlanItem>& out, const PlaneSnapshot& pl,
                                   std::size_t pi, float depth, const Projector3D& proj) {
            const PlaneRaster raster = plane_raster(proj, pl.orient, pl.offset);
            const PlaneGeometry g = plane_geometry(pl, &raster.tr);
            if (g.empty()) return;

            const Transform3D& tf = proj.transform();
            auto to_box = [&](const Vec3& p) { return tf.to_box(p.x, p.y, p.z); };

            // A pixel on a plane is a pixel of its raster (`box_per_px`), matching
            // what the raster path draws.
            const double ref = raster.box_per_px;

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
                if (batch.kind == PlaneGeometry::Batch::Kind::Heatmap) {
                    if (batch.begin < pl.sheet.heatmaps.size())
                        append_heatmap_item(out, pl, pi, pl.sheet.heatmaps[batch.begin], batch.begin, depth, proj);
                    continue;
                }
                if (batch.kind == PlaneGeometry::Batch::Kind::Contour) {
                    // As heatmap_tri below: the contourf's triangles resampled.
                    if (batch.begin < pl.sheet.contours.size() && pl.sheet.contours[batch.begin].geom)
                        append_heatmap_item(out, pl, pi,
                                            heatmap_tri_grid(pl.sheet.contours[batch.begin].geom->fill,
                                                             proj.is_perspective() ? 128 : 512),
                                            batch.begin, depth, proj);
                    continue;
                }
                if (batch.kind == PlaneGeometry::Batch::Kind::HeatmapTri) {
                    // Resampled to an even grid: cell polygons under perspective (128 x 128
                    // stays under kPlane3DCellCap), the affine image under orthographic.
                    if (batch.begin < pl.sheet.heatmap_tris.size())
                        append_heatmap_item(out, pl, pi,
                                            heatmap_tri_grid(pl.sheet.heatmap_tris[batch.begin],
                                                             proj.is_perspective() ? 128 : 512),
                                            batch.begin, depth, proj);
                    continue;
                }
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

                    case PlaneGeometry::Batch::Kind::Heatmap:
                    case PlaneGeometry::Batch::Kind::HeatmapTri:
                    case PlaneGeometry::Batch::Kind::Contour: break; // handled above
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
                            // The outline foreshortens with the marker, and so does
                            // a line marker's stroke (its default width included).
                            const float k = m.size_px > 0.0f ? size_px / m.size_px : 0.0f;
                            const float ew = is_line_marker(m.marker) ? line_marker_width(m.edge_px)
                                                                      : m.edge_px;
                            item.marks.push_back({q.x, q.y, size_px, m.color, m.marker,
                                                  m.edge, ew * k});
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
