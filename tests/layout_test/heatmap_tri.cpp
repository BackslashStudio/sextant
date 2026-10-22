// heatmap_tri(): values per vertex over triangles, coloured per pixel. The
// shared geometry (dropped triangles, weights, the CPU raster), GL pixels
// against the layout's transform, screen-space interpolation on a log axis,
// SVG raster parity, limits, picks and hover, read-back, set data, the
// refusals, the edits and a plane. Part of sextant_layout_test; see
// layout_test.h.
#include "layout_test.h"
#include "heatmap_tri_geometry.h"
#include "plot_data_view.h"

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 400, H = 300;

        AxisScale log10s() { return make_axis_scale(Scale::Log, {}); }

        FigureSnapshot one_axes(RenderSnapshot s) {
            FigureSnapshot fs;
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            fa.snap = std::move(s);
            fs.axes.push_back(std::move(fa));
            return fs;
        }

        RenderSnapshot limits(double x0, double x1, double y0, double y1, AxisScale xs = {}) {
            RenderSnapshot s;
            s.xmin = x0;
            s.xmax = x1;
            s.ymin = y0;
            s.ymax = y1;
            s.xlim_auto = s.ylim_auto = false;
            s.xscale = xs;
            s.axes_style.background = {0.5f, 0.5f, 0.5f, 1.0f};
            return s;
        }

        HeatmapTriPlot tri_plot(std::vector<double> x, std::vector<double> y, std::vector<double> v,
                                std::vector<std::uint32_t> tri) {
            HeatmapTriPlot p;
            p.x = std::move(x);
            p.y = std::move(y);
            p.values = std::move(v);
            p.tri = std::move(tri);
            return p;
        }

        // The unit square as two triangles, value = x (0 at the left, 1 at the right).
        HeatmapTriPlot square() {
            return tri_plot({0, 1, 1, 0}, {0, 0, 1, 1}, {0, 1, 1, 0}, {0, 1, 2, 0, 2, 3});
        }

        const std::uint8_t* lut_at(double t) {
            return &colormaps::get(Colormap::Viridis)[colormaps::index(t, false) * 4];
        }

        bool near_rgb(const std::uint8_t* p, const std::uint8_t* c, int tol = 6) {
            return std::abs(p[0] - c[0]) <= tol && std::abs(p[1] - c[1]) <= tol && std::abs(p[2] - c[2]) <= tol;
        }

        struct Probe {
            RgbaImage im;
            CoordTransform tr;
            const std::uint8_t* at(double x, double y) const {
                static const std::uint8_t none[4] = {0, 0, 0, 0};
                const int ix = static_cast<int>(x), iy = static_cast<int>(y);
                if (ix < 0 || iy < 0 || ix >= im.width || iy >= im.height) return none;
                return &im.pixels[(static_cast<std::size_t>(iy) * im.width + ix) * 4];
            }
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
    } // namespace

    void test_heatmap_tri() {
        std::printf("\n[heatmap_tri: values per vertex over triangles]\n");
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

        // ---- Shared geometry.
        {
            HeatmapTriOptions o;
            o.vmin = 2.0f;
            o.vmax = 4.0f;
            check(heatmap_tri_t(o, 3.0) == 0.5 && heatmap_tri_t(o, 5.0) == 1.5, "geometry: t is (v - vmin) / range");
            o.vmax = 2.0f;
            check(heatmap_tri_t(o, 9.0) == 0.0, "geometry: vmin == vmax puts every value at 0");

            const CoordTransform tr{0.0, 1.0, 0.0, 1.0, 0.0f, 0.0f, 100.0f, 100.0f, 100.0f, 100.0f};
            HeatmapTriPlot p = square();
            check(heatmap_tri_pixels(p, tr).size() == 2, "geometry: both triangles draw");
            p.values.mut()[1] = kNaN;
            const auto one = heatmap_tri_pixels(p, tr);
            check(one.size() == 1 && one[0].index == 1, "geometry: a NaN vertex leaves out its triangle only");

            const CoordTransform lg{1.0, 100.0, 0.0, 1.0, 0.0f, 0.0f, 100.0f, 100.0f, 100.0f, 100.0f, log10s()};
            const HeatmapTriPlot neg = tri_plot({-1, 10, 10, 1}, {0, 0, 1, 1}, {0, 1, 1, 0}, {0, 1, 2, 1, 2, 3});
            const auto kept = heatmap_tri_pixels(neg, lg);
            check(kept.size() == 1 && kept[0].index == 1, "geometry: a vertex <= 0 on log leaves out its triangle");

            const auto r = rasterize_heatmap_tri(square(), tr, 0, 0, 100, 100);
            const float left = r[50 * 100 + 10], right = r[50 * 100 + 89];
            check(std::abs(left - 0.105f) < 0.002f && std::abs(right - 0.895f) < 0.002f,
                  "geometry: the raster interpolates linearly at pixel centres");
            const auto half = rasterize_heatmap_tri(tri_plot({0, 1, 0}, {0, 0, 1}, {0, 0, 0}, {0, 1, 2}), tr, 0, 0, 100, 100);
            check(!std::isnan(half[90 * 100 + 5]) && std::isnan(half[5 * 100 + 90]),
                  "geometry: and leaves pixels outside every triangle empty");
            check(heatmap_tri_at(square(), {}, {}, 0.25, 0.5)->value == 0.25,
                  "geometry: the hit test interpolates in scale space");
        }

        // ---- GL against the transform.
        RenderSnapshot lin = limits(-0.5, 1.5, -0.5, 1.5);
        lin.heatmap_tris.push_back(square());
        RenderSnapshot lg = limits(1.0, 100.0, 0.0, 1.0, log10s());
        lg.heatmap_tris.push_back(tri_plot({1, 100, 100, 1}, {0, 0, 1, 1}, {0, 1, 1, 0}, {0, 1, 2, 0, 2, 3}));
        RenderSnapshot nan = lin;
        nan.heatmap_tris[0].values.mut()[1] = kNaN;
        std::vector<Probe> probes;
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            unsigned long long gen = 0;
            for (const RenderSnapshot* s : {&lin, &lg, &nan}) {
                FigureSnapshot fs = one_axes(*s);
                fs.generation = fs.data_generation = ++gen;
                probes.push_back({render_figure_rgba(dev, data, fs, W, H),
                                  compute_figure_layout(fs, W, H).cells[0].tr});
            }
            FigureSnapshot fs = one_axes(lin);
            fs.generation = fs.data_generation = ++gen;
            export_figure_png(dev, data, fs, "heatmap_tri_square.png", W, H, 1);
            export_figure_svg(fs, "heatmap_tri_square.svg", W, H);
        }
        {
            const Probe& p = probes[0];
            bool ok = true;
            for (double x : {0.1, 0.25, 0.5, 0.75, 0.9})
                for (double y : {0.2, 0.5, 0.8}) ok = ok && near_rgb(p.at(p.tr.to_px(x), p.tr.to_py(y)), lut_at(x));
            check(ok, "GL: the colour at each point is the colormap at the interpolated value");
            const std::uint8_t grey[3] = {128, 128, 128};
            check(near_rgb(p.at(p.tr.to_px(-0.25), p.tr.to_py(0.5)), grey, 2)
                      && near_rgb(p.at(p.tr.to_px(0.5), p.tr.to_py(1.25)), grey, 2),
                  "GL: and outside the triangles the plot area shows");
        }
        {
            const Probe& p = probes[1];
            // Linear on screen: halfway between x = 1 and 100 is x = 10, t = 0.5.
            check(near_rgb(p.at(p.tr.to_px(10.0), p.tr.to_py(0.5)), lut_at(0.5)),
                  "GL: on a log axis the value runs linearly on screen (x = 10 is halfway)");
        }
        {
            const Probe& p = probes[2];
            const std::uint8_t grey[3] = {128, 128, 128};
            check(near_rgb(p.at(p.tr.to_px(0.75), p.tr.to_py(0.2)), grey, 2)
                      && near_rgb(p.at(p.tr.to_px(0.25), p.tr.to_py(0.8)), lut_at(0.25)),
                  "GL: a NaN vertex leaves its triangle out, the other draws");
        }

        // ---- SVG: one raster, matching GL.
        {
            const Probe& p = probes[0];
            const std::string svg = render_figure_svg(one_axes(lin), W, H);
            const int X0 = static_cast<int>(std::floor(p.tr.to_px(0.0)));
            const int X1 = static_cast<int>(std::ceil(p.tr.to_px(1.0)));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "<image x=\"%d\"", X0);
            check(count(svg, "<image") == 1 && svg.find(buf) != std::string::npos,
                  "SVG: a heatmap_tri is one <image> over its triangles' box");
            const int Y0 = static_cast<int>(std::floor(p.tr.to_py(1.0)));
            const int Y1 = static_cast<int>(std::ceil(p.tr.to_py(0.0)));
            const auto t = rasterize_heatmap_tri(lin.heatmap_tris[0], p.tr, X0, Y0, X1 - X0, Y1 - Y0);
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
            std::printf("  SVG raster vs GL: %d samples, worst channel difference %d\n", n, worst);
            check(n > 100 && worst <= 6, "SVG: its pixels match GL's within one colormap step");
        }

        // ---- Limits and the public API.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const std::vector<double> x{0, 2, 2, 0, 1}, y{1, 1, 3, 3, 2}, v{0, 1, 2, 3, kNaN};
            ax->heatmap_tri(x, y, v, {.cmap = Colormap::Magma, .vmax = 3.0f});
            check(ax->xlim().lo == 0.0 && ax->xlim().hi == 2.0 && ax->ylim().lo == 1.0 && ax->ylim().hi == 3.0,
                  "limits: the vertices' extent, unpadded");
            check(ax->heatmap_tri_count() == 1, "api: the Delaunay form adds one heatmap_tri");
            const HeatmapTriData d = ax->heatmap_tri_data(0);
            check(d.tri.size() == 12 && d.x == x && std::isnan(d.values[4]) && d.values[3] == 3.0,
                  "api: read-back has the derived triangles (4 around the centre) and the values, NaN kept");

            const std::vector<std::uint32_t> tri{0, 1, 2};
            ax->heatmap_tri(x, y, v, tri);
            check(ax->heatmap_tri_count() == 2 && ax->heatmap_tri_data(1).tri == tri, "api: the explicit form keeps tri");

            const std::vector<double> x3{0, 1, 2};
            check(throws([&] { ax->heatmap_tri(x3, x3, x3); }), "api: collinear points have no triangulation");
            check(throws([&] { ax->heatmap_tri(x, y, v, std::vector<std::uint32_t>{}); }), "api: an empty tri throws");
            check(throws([&] { ax->heatmap_tri(x, y, v, std::vector<std::uint32_t>{0, 1}); }),
                  "api: tri not a multiple of three throws");
            check(throws([&] { ax->heatmap_tri(x, y, v, std::vector<std::uint32_t>{0, 1, 5}); }),
                  "api: an index past the vertices throws");
            check(throws([&] { ax->heatmap_tri(x3, y, v, tri); }), "api: lengths that differ throw");
            const std::vector<double> xn{0, kNaN, 2, 0, 1};
            check(throws([&] { ax->heatmap_tri(xn, y, v, tri); }), "api: a non-finite x throws");
            check(throws([&] { ax->heatmap_tri(x, y, v, tri, {.zorder = kNaN}); }), "api: a non-finite zorder throws");
            check(ax->heatmap_tri_count() == 2, "api: and a refused call adds nothing");

            ax->set_heatmap_tri_data(1, d);
            check(ax->heatmap_tri_data(1).tri.size() == 12, "set data: the struct form takes new triangles");
            const std::vector<double> moved{0, 3, 3, 0, 1.5};
            ax->set_heatmap_tri_data(1, moved, y, v);
            check(ax->heatmap_tri_data(1).x == moved && ax->heatmap_tri_data(1).tri.size() == 12,
                  "set data: the span form without tri moves vertices and keeps the triangles");
            check(throws([&] { ax->set_heatmap_tri_data(1, x3, x3, x3); }),
                  "set data: and wants the vertex count kept");
            HeatmapTriData bad = d;
            bad.tri = {0, 1, 9};
            check(throws([&] { ax->set_heatmap_tri_data(1, bad); }) && ax->heatmap_tri_data(1).x == moved,
                  "set data: bad triangles throw and change nothing");
            ax->set_heatmap_tri_data(1, x3, x3, x3, tri);
            check(ax->heatmap_tri_data(1).x.size() == 3, "set data: the span form with tri reshapes");

            ax->set_heatmap_tri_visible(1, false);
            check(!ax->heatmap_tri_visible(1), "api: visible");
            const auto found = ax->find_object(ax->last_object());
            check(found && found->kind == PickKind::HeatmapTri && found->index == 1, "api: find_object names the kind");
            check(ax->remove(ax->last_object()) && ax->heatmap_tri_count() == 1, "api: removed by its id");
        }

        // ---- Picks and hover.
        {
            RenderSnapshot s = lin;
            s.heatmap_tris[0].opts.hint_labels = {"a", "b", "c", "d"};
            const CoordTransform tr{-0.5, 1.5, -0.5, 1.5, 0.0f, 0.0f, 200.0f, 200.0f, 200.0f, 200.0f};
            const auto hit = find_pick(s, tr, tr.to_px(0.9), tr.to_py(0.2));
            check(hit && hit->kind == PickKind::HeatmapTri && hit->element == 0 && std::abs(hit->value - 0.9) < 0.01,
                  "pick: the triangle under the cursor and the value there");
            const std::string text = format_pick(s, *hit);
            check(text.find("value=0.9") != std::string::npos && text.find("\nb") != std::string::npos,
                  "hover: x, y, the value, and the nearest vertex's label");
            check(!find_pick(s, tr, tr.to_px(1.3), tr.to_py(0.5)), "pick: nothing outside the triangles");
        }

        // ---- Edits, table, colorbar.
        {
            RenderSnapshot t;
            t.heatmap_tris.push_back(square());
            PlotCellEdit e;
            e.kind = PlotKind::HeatmapTri;
            e.plot_index = 0;
            e.column = 2;
            e.element = 1;
            e.value = 7.0;
            check(apply_plot_data_op(t, e) && t.heatmap_tris[0].values[1] == 7.0, "edit: a value cell edits");
            e.column = 0;
            e.value = 0.5;
            check(apply_plot_data_op(t, e) && t.heatmap_tris[0].x[1] == 0.5, "edit: and an x cell");
            PlotRowEdit r;
            r.kind = PlotKind::HeatmapTri;
            check(!apply_plot_data_op(t, r) && t.heatmap_tris[0].x.size() == 4, "edit: no row insert (triangles name vertices)");
            const auto tables = collect_plot_data_tables(t);
            check(tables.size() == 1 && tables[0].label == "heatmap_tri 0" && tables[0].columns.size() == 3
                      && tables[0].rows_fixed && tables[0].heatmap_tri,
                  "table: x, y and value, rows fixed, with the topology");
            t.heatmap_tris[0].opts.colorbar = true;
            check(find_colorbar_requests(t).size() == 1, "colorbar: when asked");
        }

        // ---- Plane2D.
        {
            auto fig = Figure::create({.width = 400, .height = 400});
            auto ax3 = fig->add_subplot3d(1, 1, 1);
            auto pl = ax3->plane(PlaneOrientation::XY, 0.5);
            const std::vector<double> x{0, 1, 1, 0}, y{0, 0, 1, 1}, v{0, 1, 1, 0};
            pl->heatmap_tri(x, y, v);
            check(pl->heatmap_tri_count() == 1, "plane: heatmap_tri is there too");

            const HeatmapPlot g = heatmap_tri_grid(square(), 8);
            check(g.rows == 8 && g.cols == 8 && std::abs(g.data[0] - 1.0f / 16.0f) < 1e-5f,
                  "plane: the even resampling takes each cell centre's value");

            for (const bool persp : {false, true}) {
                FigureSnapshot fs = make_snapshot3d(1, 1, 1);
                RenderSnapshot3D* s = fs.axes[0].snap3d();
                if (persp) {
                    s->camera.projection = Projection::Perspective;
                    s->camera.fov = 60.0;
                }
                PlaneSnapshot p = make_plane(PlaneOrientation::XY, 0.5, {0.5f}, 1, 1, {0, 1}, {0, 1});
                p.sheet.heatmaps.clear();
                p.sheet.heatmap_tris.push_back(square());
                s->planes.push_back(p);
                const FigureLayout lay = compute_figure_layout(fs, 400, 400);
                const std::vector<PlanePlanItem> plan = plan_planes3d(lay.cells[0].box3d->proj, s->planes);
                if (!persp)
                    check(plan.size() == 1 && plan[0].form == PlanePlanItem::Form::Image,
                          "plane SVG: orthographic writes the resampled image");
                else
                    check(plan.size() == 1 && plan[0].form == PlanePlanItem::Form::Polys
                              && plan[0].polys.size() > 8000,
                          "plane SVG: perspective writes its cells as polygons");
            }
        }
    }
} // namespace lt
