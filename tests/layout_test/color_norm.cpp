// Colour norms (ColorNorm: linear, log, two-slope) on heatmap, heatmap_rec,
// heatmap_tri, scatter_z and quiver, and hist2d(): the shared arithmetic, the
// cells, triangles, points and arrows a norm leaves out, GL pixels, SVG parity,
// the render caches keyed on the norm, the refusals, a plane, and the binning.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "color_norm.h"
#include "heatmap_pixels.h"
#include "heatmap_tri_geometry.h"
#include "hist2d.h"
#include "quiver_geometry.h"

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 400, H = 300;
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

        FigureSnapshot one_axes(RenderSnapshot s) {
            FigureSnapshot fs;
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            fa.snap = std::move(s);
            fs.axes.push_back(std::move(fa));
            return fs;
        }

        RenderSnapshot limits(double x0, double x1, double y0, double y1) {
            RenderSnapshot s;
            s.xmin = x0;
            s.xmax = x1;
            s.ymin = y0;
            s.ymax = y1;
            s.xlim_auto = s.ylim_auto = false;
            s.axes_style.background = {0.5f, 0.5f, 0.5f, 1.0f};
            return s;
        }

        const std::uint8_t* lut_at(double t) {
            return &colormaps::get(Colormap::Viridis)[colormaps::index(t, false) * 4];
        }

        bool near_rgb(const std::uint8_t* p, const std::uint8_t* c, int tol = 6) {
            return std::abs(p[0] - c[0]) <= tol && std::abs(p[1] - c[1]) <= tol && std::abs(p[2] - c[2]) <= tol;
        }

        const std::uint8_t kGrey[3] = {128, 128, 128};

        struct Probe {
            RgbaImage im;
            CoordTransform tr;
            const std::uint8_t* at(double x, double y) const {
                static const std::uint8_t none[4] = {0, 0, 0, 0};
                const int ix = static_cast<int>(x), iy = static_cast<int>(y);
                if (ix < 0 || iy < 0 || ix >= im.width || iy >= im.height) return none;
                return &im.pixels[(static_cast<std::size_t>(iy) * im.width + ix) * 4];
            }
            const std::uint8_t* data(double x, double y) const { return at(tr.to_px(x), tr.to_py(y)); }
        };

        bool throws(const auto& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        std::size_t count(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
            return n;
        }

        // One row of four cells over x in [0, 4].
        HeatmapPlot row4(std::vector<float> v, ColorNorm n, float vmin, float vmax) {
            HeatmapOptions o;
            o.norm = n;
            o.vmin = vmin;
            o.vmax = vmax;
            return make_heatmap(1, 4, [v](int, int c) { return v[static_cast<std::size_t>(c)]; }, o,
                                Range{0, 4}, Range{0, 1});
        }

        // The unit square as two triangles, value `a` on the left edge and `b` on the right.
        HeatmapTriPlot square(double a, double b, ColorNorm n, float vmin, float vmax) {
            HeatmapTriPlot p;
            p.x = std::vector<double>{0, 1, 1, 0};
            p.y = std::vector<double>{0, 0, 1, 1};
            p.values = std::vector<double>{a, b, b, a};
            p.tri = std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3};
            p.opts.norm = n;
            p.opts.vmin = vmin;
            p.opts.vmax = vmax;
            return p;
        }

        ScatterZPlot points(std::vector<double> z, ColorNorm n, float vmin, float vmax) {
            ScatterZPlot p;
            const std::size_t k = z.size();
            std::vector<double> x(k), y(k, 0.5);
            for (std::size_t i = 0; i < k; ++i) x[i] = static_cast<double>(i) + 0.5;
            p.x = std::move(x);
            p.y = std::move(y);
            p.z = std::move(z);
            p.opts.norm = n;
            p.opts.vmin = vmin;
            p.opts.vmax = vmax;
            p.opts.alpha = 1.0f;
            p.opts.size = 12.0f;
            return p;
        }
    } // namespace

    void test_color_norm() {
        std::printf("\n[colour norms and hist2d]\n");
        using K = ColorNorm::Kind;

        // ---- The shared arithmetic.
        {
            check(std::abs(norm_t(K::Log, ColorNorm{}, 1.0, 100.0, 10.0) - 0.5) < 1e-12
                      && std::abs(norm_t(K::Log, ColorNorm{}, 1.0, 100.0, 1000.0) - 1.5) < 1e-12,
                  "norm: log puts each decade evenly (10 halfway from 1 to 100)");
            check(std::isnan(norm_t(K::Log, ColorNorm{}, 1.0, 100.0, 0.0)) && norm_masks(K::Log, ColorNorm{}, -2.0)
                      && !norm_masks(K::Linear, ColorNorm{}, -2.0),
                  "norm: log leaves values <= 0 without a colour; linear keeps them");
            check(norm_t(K::TwoSlope, ColorNorm{}, -1.0, 3.0, 0.0) == 0.5 && norm_t(K::TwoSlope, ColorNorm{}, -1.0, 3.0, -0.5) == 0.25
                      && norm_t(K::TwoSlope, ColorNorm{}, -1.0, 3.0, 1.5) == 0.75,
                  "norm: two-slope maps vmin..centre to 0..0.5 and centre..vmax to 0.5..1");
            check(norm_t(K::TwoSlope, ColorNorm{}, 3.0, -1.0, 1.5) == 0.25, "norm: two-slope with vmin > vmax mirrors");
            check(norm_fits(ColorNorm::log(), 0.1, 5.0) && !norm_fits(ColorNorm::log(), 0.0, 5.0)
                      && !norm_fits(ColorNorm::two_slope(5.0f), 0.0, 5.0) && norm_fits(ColorNorm::two_slope(1.0f), 0.0, 5.0),
                  "norm: log fits above 0, two-slope with its centre strictly inside");
            check(norm_in_force(ColorNorm::log(), -1.0, 5.0) == K::Linear,
                  "norm: one that doesn't fit draws as linear");
            check(ColorNorm::two_slope(2.0f) == ColorNorm{K::TwoSlope, 2.0f} && ColorNorm{} == ColorNorm::linear(),
                  "norm: the factories build the plain struct");
        }

        // ---- Heatmap cells (shared by GL, SVG and planes).
        {
            const HeatmapPlot hp = row4({-1.0f, 1.0f, 10.0f, 100.0f}, ColorNorm::log(), 1.0f, 100.0f);
            const HeatmapPixels px(hp);
            std::uint8_t c[4][4];
            for (int k = 0; k < 4; ++k) px.rgba(static_cast<std::size_t>(k), c[k]);
            check(c[0][3] == 0 && near_rgb(c[1], lut_at(0.0), 0) && near_rgb(c[2], lut_at(0.5), 0)
                      && near_rgb(c[3], lut_at(1.0), 0),
                  "cells: log colours 1, 10, 100 at 0, 0.5, 1; a cell <= 0 is transparent");
            const HeatmapPlot ts = row4({-1.0f, 0.0f, 1.5f, 3.0f}, ColorNorm::two_slope(0.0f), -1.0f, 3.0f);
            const HeatmapPixels tp(ts);
            for (int k = 0; k < 4; ++k) tp.rgba(static_cast<std::size_t>(k), c[k]);
            check(near_rgb(c[0], lut_at(0.0), 0) && near_rgb(c[1], lut_at(0.5), 0) && near_rgb(c[2], lut_at(0.75), 0)
                      && near_rgb(c[3], lut_at(1.0), 0),
                  "cells: two-slope puts the centre at the middle of the map");
            const HeatmapPlot bad = row4({0.0f, 1.0f, 2.0f, 4.0f}, ColorNorm::log(), 0.0f, 4.0f);
            const HeatmapPixels bp(bad);
            bp.rgba(2, c[0]);
            check(near_rgb(c[0], lut_at(0.5), 0), "cells: a log norm with vmin 0 draws linear");
        }

        // ---- GL, and the caches keyed on the norm.
        RenderSnapshot heat = limits(0, 4, 0, 1);
        heat.heatmaps.push_back(row4({-1.0f, 1.0f, 10.0f, 100.0f}, ColorNorm::log(), 1.0f, 100.0f));
        RenderSnapshot heat_lin = heat;
        heat_lin.heatmaps[0].opts.norm = ColorNorm::linear();
        RenderSnapshot tri = limits(-0.5, 1.5, -0.5, 1.5);
        tri.heatmap_tris.push_back(square(1.0, 100.0, ColorNorm::log(), 1.0f, 100.0f));
        RenderSnapshot tri2 = limits(-0.5, 1.5, -0.5, 1.5);
        tri2.heatmap_tris.push_back(square(-1.0, 3.0, ColorNorm::two_slope(0.0f), -1.0f, 3.0f));
        RenderSnapshot pts = limits(0, 4, 0, 1);
        pts.scatter_z.push_back(points({-1.0, 1.0, 10.0, 100.0}, ColorNorm::log(), 1.0f, 100.0f));
        RenderSnapshot pts_lin = pts;
        pts_lin.scatter_z[0].opts.norm = ColorNorm::linear();
        std::vector<Probe> probes;
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            unsigned long long gen = 0;
            // heat_lin and pts_lin reuse their predecessor's data generation, so
            // only the norm in the cache key can tell them apart.
            for (const RenderSnapshot* s : {&heat, &heat_lin, &tri, &tri2, &pts, &pts_lin}) {
                FigureSnapshot fs = one_axes(*s);
                const bool same_data = s == &heat_lin || s == &pts_lin;
                fs.generation = ++gen;
                fs.data_generation = same_data ? gen - 1 : gen;
                probes.push_back({render_figure_rgba(dev, data, fs, W, H), compute_figure_layout(fs, W, H).cells[0].tr});
            }
            FigureSnapshot fs = one_axes(tri);
            fs.generation = fs.data_generation = ++gen;
            export_figure_png(dev, data, fs, "color_norm_tri.png", W, H, 1);
            export_figure_svg(fs, "color_norm_tri.svg", W, H);
        }
        {
            const Probe& p = probes[0];
            check(near_rgb(p.data(0.5, 0.5), kGrey, 2) && near_rgb(p.data(1.5, 0.5), lut_at(0.0))
                      && near_rgb(p.data(2.5, 0.5), lut_at(0.5)) && near_rgb(p.data(3.5, 0.5), lut_at(1.0)),
                  "GL heatmap: log colours, the cell <= 0 shows the plot area");
            check(near_rgb(probes[1].data(2.5, 0.5), lut_at(9.0 / 99.0)) && near_rgb(probes[1].data(0.5, 0.5), lut_at(0.0)),
                  "GL heatmap: a norm change with the same data rebuilds the cached texture");
        }
        {
            const Probe& p = probes[2];
            // The value is interpolated on screen, then logged: 50.5 halfway, not 10.
            check(near_rgb(p.data(0.5, 0.5), lut_at(std::log(50.5) / std::log(100.0)))
                      && near_rgb(p.data(0.1, 0.5), lut_at(std::log(10.9) / std::log(100.0))),
                  "GL heatmap_tri: log applies to the interpolated value per pixel");
            const Probe& q = probes[3];
            check(near_rgb(q.data(0.25, 0.5), lut_at(0.5)) && near_rgb(q.data(0.625, 0.5), lut_at(0.75))
                      && near_rgb(q.data(0.125, 0.5), lut_at(0.25)),
                  "GL heatmap_tri: two-slope bends at the centre inside a triangle");
        }
        {
            const Probe& p = probes[4];
            check(near_rgb(p.data(0.5, 0.5), kGrey, 2) && near_rgb(p.data(2.5, 0.5), lut_at(0.5))
                      && near_rgb(p.data(3.5, 0.5), lut_at(1.0)),
                  "GL scatter_z: log colours, a point with z <= 0 is not drawn");
            check(near_rgb(probes[5].data(2.5, 0.5), lut_at(9.0 / 99.0)) && !near_rgb(probes[5].data(0.5, 0.5), kGrey, 2),
                  "GL scatter_z: a norm change with the same data rebuilds the cached instances");
        }

        // ---- SVG.
        {
            const Probe& p = probes[2];
            const std::string svg = render_figure_svg(one_axes(tri), W, H);
            check(count(svg, "<image") == 1, "SVG heatmap_tri: still one <image>");
            const int X0 = static_cast<int>(std::floor(p.tr.to_px(0.0))), X1 = static_cast<int>(std::ceil(p.tr.to_px(1.0)));
            const int Y0 = static_cast<int>(std::floor(p.tr.to_py(1.0))), Y1 = static_cast<int>(std::ceil(p.tr.to_py(0.0)));
            const auto t = rasterize_heatmap_tri(tri.heatmap_tris[0], p.tr, X0, Y0, X1 - X0, Y1 - Y0);
            int worst = 0, n = 0;
            for (int j = 2; j < Y1 - Y0 - 2; j += 7)
                for (int i = 2; i < X1 - X0 - 2; i += 7) {
                    const float v = t[static_cast<std::size_t>(j) * (X1 - X0) + i];
                    if (std::isnan(v)) continue;
                    const std::uint8_t* c = lut_at(v);
                    const std::uint8_t* g = p.at(X0 + i + 0.5, Y0 + j + 0.5);
                    for (int k = 0; k < 3; ++k) worst = std::max(worst, std::abs(c[k] - g[k]));
                    ++n;
                }
            std::printf("  SVG log raster vs GL: %d samples, worst channel difference %d\n", n, worst);
            check(n > 100 && worst <= 6, "SVG heatmap_tri: the log raster matches GL within one colormap step");

            const std::string zs = render_figure_svg(one_axes(pts), W, H);
            const std::string zl = render_figure_svg(one_axes(pts_lin), W, H);
            check(count(zs, "<circle") + 1 == count(zl, "<circle"), "SVG scatter_z: the point with z <= 0 is left out");
            char buf[32];
            const std::uint8_t* mid = lut_at(0.5);
            std::snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)", mid[0], mid[1], mid[2]);
            check(zs.find(buf) != std::string::npos, "SVG scatter_z: z = 10 takes the middle colour");

            RenderSnapshot hs = heat;
            const std::string hsvg = render_figure_svg(one_axes(hs), W, H);
            check(count(hsvg, "<image") == 1, "SVG heatmap: written as before (the cells carry the norm)");
        }

        // ---- What a norm leaves out: hover, quiver.
        {
            const ScatterZPlot& sp = pts.scatter_z[0];
            check(marker_hidden(sp, 0) && !marker_hidden(sp, 1) && marker_size_at(sp, 0) == 0.0f,
                  "scatter_z: z <= 0 under log is hidden as a point with no size");
            const CoordTransform tr{0, 4, 0, 1, 0.0f, 0.0f, 400.0f, 100.0f, 400.0f, 100.0f};
            const auto hit = find_pick(pts, tr, tr.to_px(0.5), tr.to_py(0.5));
            check(!hit || hit->kind != PickKind::ScatterZ || hit->element != 0, "scatter_z: and it is not hovered");

            QuiverPlot q;
            q.x = std::vector<double>{0, 1};
            q.y = std::vector<double>{0, 0};
            q.u = std::vector<double>{1, 1};
            q.v = std::vector<double>{0, 0};
            q.c = std::vector<double>{-1, 10};
            q.opts.norm = ColorNorm::log();
            q.opts.vmin = 1.0f;
            q.opts.vmax = 100.0f;
            int drawn = 0;
            for_each_quiver_arrow(q, {}, {}, [&](std::size_t i, double, double, double, double) {
                drawn += static_cast<int>(i) + 1;
            });
            const Color c = quiver_arrow_color(q, 1);
            const std::uint8_t* mid = lut_at(0.5);
            check(drawn == 2 && std::abs(c.r * 255.0f - mid[0]) < 0.6f && std::abs(c.g * 255.0f - mid[1]) < 0.6f,
                  "quiver: an arrow with c <= 0 under log is left out; c = 10 takes the middle colour");
        }

        // ---- The plotting calls check the norm.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const std::vector<double> z4{1, 2, 3, 4};
            const std::vector<double> e3{0, 1, 2}, e2{0, 1};
            check(throws([&] { ax->heatmap(z4, 2, 2, {0, 1}, {0, 1}, {.vmin = 0.0f, .vmax = 4.0f, .norm = ColorNorm::log()}); }),
                  "api: heatmap with a log norm and vmin 0 throws");
            check(throws([&] { ax->heatmap_rec(z4, 2, 2, e3, e3, {.vmin = -1.0f, .norm = ColorNorm::log()}); }),
                  "api: heatmap_rec too");
            const std::vector<double> tx{0, 1, 0}, ty{0, 0, 1};
            check(throws([&] { ax->heatmap_tri(tx, ty, tx, {.vmin = 1.0f, .vmax = 2.0f, .norm = ColorNorm::two_slope(3.0f)}); }),
                  "api: heatmap_tri with the centre outside vmin..vmax throws");
            check(throws([&] { ax->scatter_z(z4, z4, z4, {.vmax = 4.0f, .norm = ColorNorm::log()}); }),
                  "api: scatter_z with a log norm and vmin 0 throws");
            check(throws([&] { ax->quiver(z4, z4, z4, z4, z4, {.norm = ColorNorm::two_slope(1.0f)}); }),
                  "api: quiver with c and the centre on vmax throws");
            check(ax->heatmap_count() == 0 && ax->heatmap_tri_count() == 0, "api: and a refused call adds nothing");
            ax->quiver(z4, z4, z4, z4, {.norm = ColorNorm::log()});
            ax->heatmap(z4, 2, 2, {0, 1}, {0, 1}, {.vmin = 1.0f, .vmax = 4.0f, .norm = ColorNorm::log()});
            ax->scatter_z(z4, z4, z4, {.vmin = -1.0f, .vmax = 4.0f, .norm = ColorNorm::two_slope(0.0f)});
            check(ax->heatmap_count() == 1, "api: a quiver without c ignores its norm; fitting norms are taken");

            auto fig3 = Figure::create();
            auto ax3 = fig3->add_subplot3d(1, 1, 1);
            auto pl = ax3->plane(PlaneOrientation::XY, 0.5);
            check(throws([&] { pl->scatter_z(z4, z4, z4, {.vmax = 4.0f, .norm = ColorNorm::log()}); }),
                  "api: Plane2D checks the same");
        }

        // ---- Plane: the heatmap_tri resampling under a norm.
        {
            const HeatmapPlot g = heatmap_tri_grid(square(1.0, 100.0, ColorNorm::log(), 1.0f, 100.0f), 8);
            const double want = std::log(1.0 + 99.0 / 16.0) / std::log(100.0);
            check(g.opts.vmin == 0.0f && g.opts.vmax == 1.0f && g.opts.norm == ColorNorm::linear()
                      && std::abs(g.data[0] - want) < 1e-5,
                  "plane: under log the resampled grid holds colormap positions over a linear 0..1");
        }

        // ---- hist2d: the binning.
        {
            const std::vector<double> x{0, 0.5, 1, 1, kNaN, 2}, y{0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
            Hist2dOptions o;
            o.xbins = 2;
            o.ybins = 1;
            o.xrange = Range{0, 1};
            o.yrange = Range{0, 1};
            const Hist2dBins b = hist2d_bins(x, y, o, "t");
            check(b.even && b.rows() == 1 && b.cols() == 2 && b.values == std::vector<double>{1, 3},
                  "hist2d: half-open bins, the last takes its upper edge, NaN and outside points dropped");
            check(b.vmin == 1.0f && b.vmax == 3.0f, "hist2d: vmin/vmax default to the counts' extent");

            o.density = true;
            const Hist2dBins d = hist2d_bins(x, y, o, "t");
            check(std::abs(d.values[0] * 0.5 + d.values[1] * 0.5 - 1.0) < 1e-12, "hist2d: density integrates to 1");

            Hist2dOptions a;
            a.xbins = 4;
            a.ybins = 2;
            const std::vector<double> ax{1, 2, 3}, ay{5, 5, 5};
            const Hist2dBins e = hist2d_bins(ax, ay, a, "t");
            check(e.x_edges.front() == 1.0 && e.x_edges.back() == 3.0 && e.y_edges.front() == 4.5 && e.y_edges.back() == 5.5,
                  "hist2d: the default range is the points' extent, one value widened by 0.5");

            Hist2dOptions m;
            m.xbins = 3;
            m.ybins = 1;
            m.xrange = Range{0, 3};
            m.cmin = 1.0;
            const std::vector<double> mx{0.5, 2.5, 2.6}, my{0, 0, 0};
            const Hist2dBins c = hist2d_bins(mx, my, m, "t");
            check(c.values[0] == 1.0 && std::isnan(c.values[1]) && c.values[2] == 2.0, "hist2d: cmin = 1 empties the empty bins");
            m.cmin.reset();
            m.cmax = 1.5;
            check(std::isnan(hist2d_bins(mx, my, m, "t").values[2]), "hist2d: cmax empties the bins above it");

            m.cmax.reset();
            m.norm = ColorNorm::log();
            const Hist2dBins l = hist2d_bins(mx, my, m, "t");
            check(l.vmin == 1.0f && l.vmax == 2.0f, "hist2d: under log, vmin defaults to the smallest count above 0");
            m.norm = ColorNorm::two_slope(5.0f);
            const Hist2dBins t = hist2d_bins(mx, my, m, "t");
            check(t.vmin == 0.0f && t.vmax == 10.0f, "hist2d: under two-slope, the limits widen to keep the centre inside");

            Hist2dOptions g;
            g.x_edges = {1, 10, 100};
            g.ybins = 1;
            const std::vector<double> gx{2, 20, 50, 100}, gy{0, 0, 0, 0};
            const Hist2dBins r = hist2d_bins(gx, gy, g, "t");
            check(!r.even && r.cols() == 2 && r.values == std::vector<double>{1, 3}, "hist2d: given edges bin unevenly");
        }

        // ---- hist2d: the call.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const std::vector<double> x{0.1, 0.2, 0.9, 0.95}, y{0.1, 0.1, 0.9, 0.9};
            ax->hist2d(x, y, {.xbins = 2, .ybins = 2, .colorbar = true});
            const HeatmapData h = ax->heatmap_data(0);
            check(ax->heatmap_count() == 1 && h.rows == 2 && h.cols == 2 && h.x_edges.empty()
                      && h.data == std::vector<double>{2, 0, 0, 2} && h.xrange.lo == 0.1 && h.xrange.hi == 0.95,
                  "hist2d: even bins give a heatmap, row 0 the lowest y");
            ax->hist2d(x, y, {.ybins = 1, .x_edges = {0, 0.5, 1}});
            check(ax->heatmap_count() == 2 && ax->heatmap_data(1).x_edges.size() == 3
                      && ax->heatmap_data(1).data == std::vector<double>{2, 2},
                  "hist2d: edges give a heatmap_rec");
            const auto found = ax->find_object(ax->last_object());
            check(found && found->kind == PickKind::Heatmap, "hist2d: the result is an ordinary heatmap object");

            const std::vector<double> x3{1, 2, 3};
            check(throws([&] { ax->hist2d(x3, y); }), "hist2d: x and y of different lengths throw");
            check(throws([&] { ax->hist2d(x, y, {.xbins = 0}); }), "hist2d: bins < 1 throws");
            check(throws([&] { ax->hist2d(x, y, {.xrange = Range{1, 1}}); }), "hist2d: an empty range throws");
            check(throws([&] { ax->hist2d(x, y, {.y_edges = {0, 2, 1}}); }), "hist2d: unordered edges throw");
            check(throws([&] { ax->hist2d(x, y, {.norm = ColorNorm::log(), .vmin = 0.0f}); }),
                  "hist2d: a log norm with vmin 0 throws");
            check(ax->heatmap_count() == 2, "hist2d: and a refused call adds nothing");
        }
    }
} // namespace lt
