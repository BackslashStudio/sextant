// The boundary norm (ColorNorm::boundaries): the band arithmetic, what it
// leaves out, heatmap cells, heatmap_tri's per-pixel bands on the GPU against
// the SVG raster, scatter_z and quiver, the stepped colorbar (requests,
// labels and their thinning, GL blocks, SVG rects), the refusals and a plane.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "color_norm.h"
#include "heatmap_pixels.h"
#include "heatmap_tri_geometry.h"
#include "quiver_geometry.h"

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 400, H = 300;

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
            CellLayout cell;
            const std::uint8_t* at(double x, double y) const {
                static const std::uint8_t none[4] = {0, 0, 0, 0};
                const int ix = static_cast<int>(x), iy = static_cast<int>(y);
                if (ix < 0 || iy < 0 || ix >= im.width || iy >= im.height) return none;
                return &im.pixels[(static_cast<std::size_t>(iy) * im.width + ix) * 4];
            }
            const std::uint8_t* data(double x, double y) const { return at(cell.tr.to_px(x), cell.tr.to_py(y)); }
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
        HeatmapPlot row4(std::vector<float> v, ColorNorm n) {
            HeatmapOptions o;
            o.norm = std::move(n);
            return make_heatmap(1, 4, [v](int, int c) { return v[static_cast<std::size_t>(c)]; }, o,
                                Range{0, 4}, Range{0, 1});
        }

        // The unit square as two triangles, value `a` on the left edge and `b` on the right.
        HeatmapTriPlot square(double a, double b, ColorNorm n) {
            HeatmapTriPlot p;
            p.x = std::vector<double>{0, 1, 1, 0};
            p.y = std::vector<double>{0, 0, 1, 1};
            p.values = std::vector<double>{a, b, b, a};
            p.tri = std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3};
            p.opts.norm = std::move(n);
            return p;
        }

        std::vector<double> ramp(int n) {
            std::vector<double> b(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) b[static_cast<std::size_t>(i)] = i;
            return b;
        }
    } // namespace

    void test_boundary_norm() {
        std::printf("\n[boundary norm; stepped colorbar]\n");
        using K = ColorNorm::Kind;
        const ColorNorm b4 = ColorNorm::boundaries({0, 1, 2, 5});

        // ---- The arithmetic.
        {
            auto t = [&](double v) { return norm_t(K::Boundaries, b4, 0.0, 0.0, v); };
            check(t(0.0) == 0.0 && t(0.99) == 0.0 && t(1.0) == 0.5 && t(4.9) == 1.0,
                  "bounds: band i of n at i / (n - 1), each band [b_i, b_i+1)");
            check(t(5.0) == 1.0 && std::isnan(t(-0.1)) && std::isnan(t(5.1)) && std::isnan(t(std::nan(""))),
                  "bounds: the last band closed; outside the boundaries (and NaN) no colour");
            check(norm_t(K::Boundaries, ColorNorm::boundaries({0, 1}), 0.0, 0.0, 0.3) == 0.5,
                  "bounds: one band takes the middle of the map");
            check(norm_masks(K::Boundaries, b4, 7.0) && norm_masks(K::Boundaries, b4, -1.0)
                      && !norm_masks(K::Boundaries, b4, 3.0) && !norm_masks(K::Boundaries, b4, std::nan("")),
                  "bounds: what is left out (NaN is NaN's own case)");
            check(norm_fits(b4, 0.0, 0.0) && norm_fits(b4, 3.0, -7.0), "bounds: fit whatever vmin/vmax are");
            check(!norm_fits(ColorNorm::boundaries({0}), 0, 1) && !norm_fits(ColorNorm::boundaries({0, 0, 1}), 0, 1)
                      && !norm_fits(ColorNorm::boundaries({1, 0}), 0, 1)
                      && !norm_fits(ColorNorm::boundaries({0, std::nan("")}), 0, 1)
                      && !norm_fits(ColorNorm::boundaries(ramp(16385)), 0, 1) && norm_fits(ColorNorm::boundaries(ramp(16384)), 0, 1),
                  "bounds: 2 to 16384, finite, strictly increasing");
            check(norm_in_force(ColorNorm::boundaries({2, 1}), 0, 1) == K::Linear, "bounds: an unusable list draws linear");
            check(b4 == ColorNorm{K::Boundaries, 0.0f, {0, 1, 2, 5}} && !(b4 == ColorNorm::boundaries({0, 1, 2, 6})),
                  "bounds: the factory builds the plain struct; == compares the boundaries");
        }

        // ---- Heatmap cells.
        {
            const HeatmapPlot hp = row4({-1.0f, 0.5f, 1.5f, 5.0f}, b4);
            const HeatmapPixels px(hp);
            std::uint8_t c[4][4];
            for (int k = 0; k < 4; ++k) px.rgba(static_cast<std::size_t>(k), c[k]);
            check(c[0][3] == 0 && near_rgb(c[1], lut_at(0.0), 0) && near_rgb(c[2], lut_at(0.5), 0)
                      && near_rgb(c[3], lut_at(1.0), 0),
                  "cells: one colour per band; a cell outside the boundaries is transparent");
        }

        // ---- GL.
        RenderSnapshot heat = limits(0, 4, 0, 1);
        heat.heatmaps.push_back(row4({-1.0f, 0.5f, 1.5f, 5.0f}, b4));
        heat.heatmaps[0].opts.colorbar = true;
        RenderSnapshot heat2 = heat;
        heat2.heatmaps[0].opts.norm = ColorNorm::boundaries({0, 1, 100});
        // Uneven bands across the square: v = 3x, boundaries at x = 1/6 and 2/3.
        RenderSnapshot tri = limits(-0.5, 1.5, -0.5, 1.5);
        tri.heatmap_tris.push_back(square(0.0, 3.0, ColorNorm::boundaries({0, 0.5, 2, 3})));
        // A triangle partly outside: v = 4x - 1 below 0 left of x = 0.25.
        RenderSnapshot part = limits(-0.5, 1.5, -0.5, 1.5);
        part.heatmap_tris.push_back(square(-1.0, 3.0, ColorNorm::boundaries({0, 1, 2, 3})));
        RenderSnapshot pts = limits(0, 4, 0, 1);
        {
            ScatterZPlot p;
            p.x = std::vector<double>{0.5, 1.5, 2.5, 3.5};
            p.y = std::vector<double>{0.5, 0.5, 0.5, 0.5};
            p.z = std::vector<double>{-1.0, 0.5, 1.5, 6.0};
            p.opts.norm = ColorNorm::boundaries({0, 1, 2});
            p.opts.alpha = 1.0f;
            p.opts.size = 12.0f;
            pts.scatter_z.push_back(std::move(p));
        }
        std::vector<Probe> probes;
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            unsigned long long gen = 0;
            // heat2 reuses heat's data generation: only the norm in the cache key tells them apart.
            for (const RenderSnapshot* s : {&heat, &heat2, &tri, &part, &pts}) {
                FigureSnapshot fs = one_axes(*s);
                fs.generation = ++gen;
                fs.data_generation = s == &heat2 ? gen - 1 : gen;
                probes.push_back({render_figure_rgba(dev, data, fs, W, H), compute_figure_layout(fs, W, H).cells[0]});
            }
            FigureSnapshot fs = one_axes(tri);
            fs.generation = fs.data_generation = ++gen;
            fs.axes[0].snap = [&] {
                RenderSnapshot s = tri;
                s.heatmap_tris[0].opts.colorbar = true;
                return s;
            }();
            export_figure_png(dev, data, fs, "boundary_norm_tri.png", W, H, 1);
            export_figure_svg(fs, "boundary_norm_tri.svg", W, H);
        }
        {
            const Probe& p = probes[0];
            check(near_rgb(p.data(0.5, 0.5), kGrey, 2) && near_rgb(p.data(1.5, 0.5), lut_at(0.0))
                      && near_rgb(p.data(2.5, 0.5), lut_at(0.5)) && near_rgb(p.data(3.5, 0.5), lut_at(1.0)),
                  "GL heatmap: one colour per band, the cell outside shows the plot area");
            check(near_rgb(probes[1].data(2.5, 0.5), lut_at(1.0)) && near_rgb(probes[1].data(3.5, 0.5), lut_at(1.0))
                      && near_rgb(probes[1].data(1.5, 0.5), lut_at(0.0)),
                  "GL heatmap: new boundaries with the same data rebuild the cached texture");
        }
        {
            const Probe& p = probes[2];
            check(near_rgb(p.data(0.1, 0.5), lut_at(0.0)) && near_rgb(p.data(0.4, 0.5), lut_at(0.5))
                      && near_rgb(p.data(0.8, 0.5), lut_at(1.0)),
                  "GL heatmap_tri: the band found per pixel (uneven bands inside one triangle)");
            const Probe& q = probes[3];
            check(near_rgb(q.data(0.1, 0.5), kGrey, 2) && near_rgb(q.data(0.4, 0.5), lut_at(0.0))
                      && near_rgb(q.data(0.6, 0.5), lut_at(0.5)) && near_rgb(q.data(0.9, 0.5), lut_at(1.0)),
                  "GL heatmap_tri: a vertex outside leaves out only the pixels outside");
            const auto hit = heatmap_tri_at(part.heatmap_tris[0], AxisScale{}, AxisScale{}, 0.1, 0.5);
            const auto hit2 = heatmap_tri_at(part.heatmap_tris[0], AxisScale{}, AxisScale{}, 0.6, 0.5);
            check(!hit && hit2 && std::abs(hit2->value - 1.4) < 1e-9,
                  "heatmap_tri: hover finds nothing where nothing is drawn, the value where it is");
        }
        {
            const Probe& p = probes[4];
            check(near_rgb(p.data(0.5, 0.5), kGrey, 2) && near_rgb(p.data(3.5, 0.5), kGrey, 2)
                      && near_rgb(p.data(1.5, 0.5), lut_at(0.0)) && near_rgb(p.data(2.5, 0.5), lut_at(1.0)),
                  "GL scatter_z: points outside the boundaries are not drawn");
            check(marker_hidden(pts.scatter_z[0], 3) && !marker_hidden(pts.scatter_z[0], 2),
                  "scatter_z: and hidden (not hovered, not written)");
        }

        // ---- SVG raster against the GPU, and the bands' edges.
        {
            const Probe& p = probes[2];
            const CoordTransform& tr = p.cell.tr;
            const int X0 = static_cast<int>(std::floor(tr.to_px(0.0))), X1 = static_cast<int>(std::ceil(tr.to_px(1.0)));
            const int Y0 = static_cast<int>(std::floor(tr.to_py(1.0))), Y1 = static_cast<int>(std::ceil(tr.to_py(0.0)));
            const auto t = rasterize_heatmap_tri(tri.heatmap_tris[0], tr, X0, Y0, X1 - X0, Y1 - Y0);
            int n = 0, off = 0;
            for (int j = 2; j < Y1 - Y0 - 2; j += 3)
                for (int i = 2; i < X1 - X0 - 2; i += 3) {
                    const float v = t[static_cast<std::size_t>(j) * (X1 - X0) + i];
                    if (std::isnan(v)) continue;
                    ++n;
                    if (!near_rgb(p.at(X0 + i + 0.5, Y0 + j + 0.5), lut_at(v), 2)) ++off;
                }
            std::printf("  SVG band raster vs GL: %d samples, %d differ\n", n, off);
            check(n > 500 && off * 100 <= n, "SVG heatmap_tri: the band raster matches GL (at most 1% at band edges)");
            // Band edge at x = 1/6: the colour changes within a pixel of it.
            const double xe = tr.to_px(1.0 / 6.0), ym = tr.to_py(0.5);
            check(near_rgb(p.at(xe - 1.5, ym), lut_at(0.0), 2) && near_rgb(p.at(xe + 1.5, ym), lut_at(0.5), 2),
                  "GL heatmap_tri: a band edge where the value crosses its boundary");

            const std::string svg = render_figure_svg(one_axes(part), W, H);
            check(count(svg, "<image") == 1, "SVG heatmap_tri: still one <image>");
        }

        // ---- Quiver.
        {
            QuiverPlot q;
            q.x = std::vector<double>{0, 1, 2};
            q.y = std::vector<double>{0, 0, 0};
            q.u = std::vector<double>{1, 1, 1};
            q.v = std::vector<double>{0, 0, 0};
            q.c = std::vector<double>{-1, 1.5, 9};
            q.opts.norm = ColorNorm::boundaries({0, 1, 2});
            std::vector<std::size_t> drawn;
            for_each_quiver_arrow(q, {}, {}, [&](std::size_t i, double, double, double, double) { drawn.push_back(i); });
            const Color c = quiver_arrow_color(q, 1);
            const std::uint8_t* top = lut_at(1.0);
            check(drawn == std::vector<std::size_t>{1} && std::abs(c.r * 255.0f - top[0]) < 0.6f
                      && std::abs(c.b * 255.0f - top[2]) < 0.6f,
                  "quiver: arrows outside the boundaries are left out; the rest take their band's colour");
        }

        // ---- The stepped colorbar.
        {
            const auto reqs = find_colorbar_requests(heat);
            check(reqs.size() == 1 && reqs[0].bounds == b4.bounds && reqs[0].band_t == std::vector<double>{0.0, 0.5, 1.0}
                      && reqs[0].vmin == 0.0f && reqs[0].vmax == 5.0f,
                  "colorbar: a boundary norm's request carries the boundaries and band positions");
            RenderSnapshot lin = heat;
            lin.heatmaps[0].opts.norm = ColorNorm::linear();
            check(find_colorbar_requests(lin)[0].bounds.empty(), "colorbar: other norms ask for the gradient bar");

            const ColorbarBox& cb = probes[0].cell.colorbars.at(0);
            bool texts = cb.labels.size() == 4;
            const char* want[4] = {"0", "1", "2", "5"};
            for (std::size_t k = 0; texts && k < 4; ++k) texts = cb.labels[k].text == want[k];
            check(cb.stepped() && texts, "colorbar: stepped, a label at every boundary");
            const float bottom = cb.rect.y + cb.rect.h;
            check(std::abs(cb.labels[0].y - bottom) < 1e-3f && std::abs(cb.labels[3].y - cb.rect.y) < 1e-3f
                      && std::abs(cb.labels[1].y - (bottom - cb.rect.h / 3.0f)) < 1e-3f && cb.labels[0].x == cb.vmin_x,
                  "colorbar: equal bands, boundary k at k/n from vmin's end, at the numbers' anchor");

            // Blocks on the GPU, one colour each, in the band's position.
            const Probe& p = probes[0];
            bool blocks = true;
            for (int i = 0; i < 3; ++i) {
                const double y = bottom - (i + 0.5) / 3.0 * cb.rect.h;
                blocks = blocks && near_rgb(p.at(cb.rect.x + cb.rect.w * 0.5, y), lut_at(0.5 * i), 0);
            }
            check(blocks, "colorbar: GL draws one flat block per band");

            const std::string svg = render_figure_svg(one_axes(heat), W, H);
            check(count(svg, "colorbarGrad") == 0 && svg.find(">5</text>") != std::string::npos
                      && svg.find(">2</text>") != std::string::npos,
                  "colorbar: SVG writes rects, no gradient, and the boundary labels");
            check(count(render_figure_svg(one_axes(lin), W, H), "colorbarGrad") == 2,
                  "colorbar: a linear norm's bar is still the gradient");

            // 101 boundaries crowd the bar: thinned to every k-th, first and last kept.
            RenderSnapshot many = heat;
            many.heatmaps[0].opts.norm = ColorNorm::boundaries(ramp(101));
            const CellLayout c = compute_figure_layout(one_axes(many), W, H).cells[0];
            const auto& ml = c.colorbars.at(0).labels;
            const float lh = font_vmetrics(many.colorbar_opts.font_path, many.colorbar_opts.fontsize).line_height;
            bool spaced = ml.size() > 3 && ml.size() < 101 && ml.front().text == "0" && ml.back().text == "100";
            for (std::size_t k = 1; spaced && k < ml.size(); ++k) spaced = std::abs(ml[k].y - ml[k - 1].y) >= lh - 1e-3f;
            std::printf("  101 boundaries on a %.0f px bar: %zu labels\n", c.colorbars[0].rect.h, ml.size());
            check(spaced, "colorbar: crowded labels thinned, a line height apart, first and last kept");

            RenderSnapshot across = many;
            across.colorbar_opts.anchor = ColorbarAnchor::Bottom;
            const CellLayout h = compute_figure_layout(one_axes(across), W, H).cells[0];
            const auto& hl = h.colorbars.at(0).labels;
            bool apart = hl.size() > 2 && hl.size() < 101 && hl.back().text == "100";
            for (std::size_t k = 1; apart && k < hl.size(); ++k) {
                const float wa = text_width(across.colorbar_opts.font_path, across.colorbar_opts.fontsize, hl[k - 1].text);
                const float wb = text_width(across.colorbar_opts.font_path, across.colorbar_opts.fontsize, hl[k].text);
                apart = hl[k].x - hl[k - 1].x >= 0.5f * (wa + wb) + kColorbarLabelGap - 1e-3f;
            }
            check(apart, "colorbar: along a horizontal bar, labels a gap apart");

            // The widest boundary label gets its room.
            RenderSnapshot wide = heat;
            wide.heatmaps[0].opts.norm = ColorNorm::boundaries({0, 1, 2, 123456});
            const float fw = compute_figure_layout(one_axes(wide), W, H).cells[0].frame.w;
            check(fw < probes[0].cell.frame.w - 10.0f, "colorbar: the layout measures the widest boundary label");
        }

        // ---- The plotting calls check the boundaries.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const std::vector<double> z4{1, 2, 3, 4};
            check(throws([&] { ax->heatmap(z4, 2, 2, {0, 1}, {0, 1}, {.norm = ColorNorm::boundaries({1, 0})}); })
                      && throws([&] { ax->scatter_z(z4, z4, z4, {.norm = ColorNorm::boundaries({1})}); })
                      && ax->heatmap_count() == 0,
                  "api: unusable boundaries throw and add nothing");
            ax->heatmap(z4, 2, 2, {0, 1}, {0, 1}, {.vmin = 3.0f, .vmax = 3.0f, .norm = ColorNorm::boundaries({0, 2, 4})});
            check(ax->heatmap_count() == 1, "api: usable boundaries are taken whatever vmin/vmax are");
            auto fig3 = Figure::create();
            auto pl = fig3->add_subplot3d(1, 1, 1)->plane(PlaneOrientation::XY, 0.5);
            check(throws([&] { pl->heatmap(z4, 2, 2, {0, 1}, {0, 1}, {.norm = ColorNorm::boundaries({0, 0})}); }),
                  "api: Plane2D checks the same");
        }

        // ---- Plane: the heatmap_tri resampling holds band positions.
        {
            const HeatmapPlot g = heatmap_tri_grid(square(0.0, 3.0, ColorNorm::boundaries({0, 1, 2, 3})), 6);
            bool ok = g.opts.vmin == 0.0f && g.opts.vmax == 1.0f && g.opts.norm == ColorNorm::linear();
            for (float v : g.data) ok = ok && (std::isnan(v) || v == 0.0f || v == 0.5f || v == 1.0f);
            check(ok && g.data[0] == 0.0f && g.data[5] == 1.0f,
                  "plane: the resampled grid holds band positions over a linear 0..1");
        }
    }
} // namespace lt
