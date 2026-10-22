// contour() / contourf(): the levels, the four-triangle split, the traced
// lines (whole through grid points, closed around an island, on the fill's
// surface), colours, GL fills and lines (in zorder), SVG, the colorbar, hover
// and picks, the calls and read-back, edits that rebuild, a plane, and the
// panel. Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "contour_plot.h"
#include "figure_edits.h"
#include "figure_impl.h"
#include "hint.h"

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

        // A contour built as the calls build it (levels sorted, geometry derived).
        ContourPlot make_contour(std::vector<double> x, std::vector<double> y, std::vector<double> z,
                                 ContourOptions o) {
            check_contour(x, y, z, o, "test");
            ContourPlot p;
            p.x = std::move(x);
            p.y = std::move(y);
            p.z = std::move(z);
            p.opts = std::move(o);
            p.data_stamp = 1;
            p.id = 900;
            contour_rebuild(p);
            return p;
        }

        // z = x over x 0..n-1 and y {0, 1}.
        ContourPlot ramp(int n, ContourOptions o) {
            std::vector<double> x(static_cast<std::size_t>(n)), z;
            for (int j = 0; j < n; ++j) x[static_cast<std::size_t>(j)] = j;
            for (int i = 0; i < 2; ++i) z.insert(z.end(), x.begin(), x.end());
            return make_contour(x, {0, 1}, z, std::move(o));
        }
    } // namespace

    void test_contour_kind() {
        std::printf("\n[contour / contourf]\n");

        // ---- Levels.
        {
            const std::vector<double> z01{0.0, 0.25, 0.5, 1.0};
            const auto a = contour_levels({}, z01);
            bool even = a.size() == 11 && a.front() == 0.0 && std::abs(a.back() - 1.0) < 1e-12;
            for (std::size_t k = 1; even && k < a.size(); ++k) even = std::abs(a[k] - a[k - 1] - 0.1) < 1e-9;
            check(even, "levels: 7 nice values over 0..1 step 0.1, from <= min to >= max");
            const auto b = contour_levels({}, std::vector<double>{0.3, 9.7, kNaN});
            check(b.front() == 0.0 && b.back() == 10.0 && b.size() == 11,
                  "levels: over 0.3..9.7 (NaN ignored) 0 to 10 in ones");
            check(contour_levels({.nlevels = 2}, std::vector<double>{0.0, 1.0}) == std::vector<double>{0.0, 0.5, 1.0},
                  "levels: nlevels sets their number");
            check(contour_levels({.levels = {3, 1, 1, 2}}, z01) == std::vector<double>{1, 2, 3},
                  "levels: given ones sorted and de-duplicated");
            check(contour_levels({}, std::vector<double>{5, 5}) == std::vector<double>{5}
                      && contour_levels({}, std::vector<double>{kNaN}).empty(),
                  "levels: a point range gives one, no finite value none");
        }

        // ---- The four-triangle split.
        {
            const ContourPlot p = make_contour({0, 1, 2}, {0, 1, 2}, {0, 1, 2, 3, 4, 5, 6, 7, 8}, {.filled = true});
            const HeatmapTriPlot& f = p.geom->fill;
            check(f.x.size() == 13 && f.tri.size() == 48 && std::abs(f.values[9] - 2.0) < 1e-12
                      && f.x[9] == 0.5 && f.y[9] == 0.5,
                  "split: 3x3 points give 4 squares of 4 triangles, each centre the corner mean");
            const ContourPlot q = make_contour({0, 1, 2}, {0, 1, 2}, {0, 1, 2, 3, kNaN, 5, 6, 7, 8}, {.filled = true});
            check(q.geom->fill.tri.empty(), "split: a NaN point leaves out every square it touches");
            check(f.opts.norm == ColorNorm::boundaries(p.geom->levels) && f.band_t == p.geom->band_t,
                  "split: the fill is a boundary norm of the levels in the bands' colours");
        }

        // ---- Lines.
        {
            const ContourPlot mid = ramp(4, {.levels = {1.5}});
            bool vertical = mid.geom->lines.size() == 1;
            for (std::size_t i = 0; vertical && i < mid.geom->lines[0].x.size(); ++i)
                vertical = std::abs(mid.geom->lines[0].x[i] - 1.5f) < 1e-6f;
            check(vertical && mid.geom->lines[0].y.front() != mid.geom->lines[0].y.back(),
                  "lines: z = x at 1.5 is one line at x = 1.5 across the grid");
            const ContourPlot at = ramp(4, {.levels = {1.0}});
            check(at.geom->lines.size() == 1 && at.geom->lines[0].x.size() == 2 && at.geom->lines[0].x[0] == 1.0f
                      && at.geom->lines[0].x[1] == 1.0f,
                  "lines: a level through grid points is one whole line along them");
            const ContourPlot island = make_contour({0, 1, 2}, {0, 1, 2}, {0, 0, 0, 0, 1, 0, 0, 0, 0}, {.levels = {0.5}});
            check(island.geom->lines.size() == 1 && island.geom->lines[0].closed, "lines: around an island, one closed line");
            const ContourPlot three = ramp(5, {.levels = {1.5, 2.5, 0.5}});
            check(three.geom->lines.size() == 3 && three.geom->line_level == std::vector<std::size_t>{0, 1, 2},
                  "lines: level by level, each knowing its level");

            // On the fill's surface: every line point has its level's value there.
            std::vector<double> x(9), y(7), z;
            for (int j = 0; j < 9; ++j) x[static_cast<std::size_t>(j)] = j * 0.5;
            for (int i = 0; i < 7; ++i) y[static_cast<std::size_t>(i)] = i * 0.7;
            for (double yy : y)
                for (double xx : x) z.push_back(std::sin(xx) * std::cos(yy * 0.8));
            const ContourPlot lines = make_contour(x, y, z, {.levels = {0.3}});
            const ContourPlot fill = make_contour(x, y, z, {.levels = {-1, 0.3, 1}, .filled = true});
            double worst = 0.0;
            std::size_t n = 0;
            for (const ContourLine& l : lines.geom->lines)
                for (std::size_t i = 0; i < l.x.size(); ++i) {
                    const auto h = heatmap_tri_at(fill.geom->fill, AxisScale{}, AxisScale{}, l.x[i], l.y[i]);
                    if (!h) continue;
                    worst = std::max(worst, std::abs(h->value - 0.3));
                    ++n;
                }
            std::printf("  line points on the fill's surface: %zu, worst |value - level| %.2g\n", n, worst);
            check(n > 20 && worst < 1e-5, "lines: on the same triangles as the fill (a contour follows a contourf)");
        }

        // ---- Colours.
        {
            const ContourPlot p = ramp(3, {.levels = {0, 1, 2}});
            check(p.geom->level_t == std::vector<double>{0.0, 0.5, 1.0} && p.geom->band_t == std::vector<double>{0.25, 0.75},
                  "colours: a line at its level's place, a band at its midpoint's (limits the first and last level)");
            const ContourPlot v = ramp(3, {.levels = {0, 1, 2}, .vmin = 0.0f, .vmax = 4.0f});
            check(v.geom->level_t[2] == 0.5, "colours: vmin/vmax set the limits");
            const ContourPlot one = ramp(3, {.levels = {0, 1, 2}, .line_color = Color::Red});
            check(contour_line_color(one, 1)->r == Color::Red.r && contour_line_color(one, 0)->g == Color::Red.g, "colours: line_color colours every line");
            const ContourPlot lg = ramp(3, {.levels = {-1, 1, 2}, .norm = ColorNorm::log(), .vmin = 1.0f, .vmax = 2.0f});
            check(!contour_line_color(lg, 0) && contour_line_color(lg, 1), "colours: a level the norm masks has no line");
        }

        // ---- GL.
        RenderSnapshot filled = limits(0, 1, 0, 1);
        filled.contours.push_back(make_contour({0, 1}, {0, 1}, {0, 1, 0, 1}, {.levels = {0, 0.5, 1}, .filled = true}));
        RenderSnapshot inside = limits(0, 1, 0, 1);
        inside.contours.push_back(make_contour({0, 1}, {0, 1}, {0, 1, 0, 1}, {.levels = {0.2, 0.8}, .filled = true}));
        RenderSnapshot lines = limits(0, 4, 0, 1);
        lines.contours.push_back(ramp(5, {.levels = {1, 2, 3}, .linewidth = 4.0f}));
        // A heatmap over the lines: in zorder, the lines under it, then over it.
        RenderSnapshot under = lines;
        under.heatmaps.push_back(make_heatmap(1, 1, [](int, int) { return 0.0f; }, {}, Range{1.8, 2.2}, Range{0, 1}));
        under.heatmaps[0].id = 901;
        RenderSnapshot over = under;
        over.contours[0].opts.zorder = 1.0;
        std::vector<Probe> probes;
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            unsigned long long gen = 0;
            for (const RenderSnapshot* s : {&filled, &inside, &lines, &under, &over}) {
                FigureSnapshot fs = one_axes(*s);
                fs.generation = fs.data_generation = ++gen;
                probes.push_back({render_figure_rgba(dev, data, fs, W, H), compute_figure_layout(fs, W, H).cells[0].tr});
            }
            RenderSnapshot both = limits(0, 4, 0, 1);
            std::vector<double> x(9), y(5), z;
            for (int j = 0; j < 9; ++j) x[static_cast<std::size_t>(j)] = j * 0.5;
            for (int i = 0; i < 5; ++i) y[static_cast<std::size_t>(i)] = i * 0.25;
            for (double yy : y)
                for (double xx : x) z.push_back(std::sin(xx * 1.3) + yy);
            both.contours.push_back(make_contour(x, y, z, {.filled = true, .colorbar = true}));
            both.contours.push_back(make_contour(x, y, z, {.line_color = Color::Black, .linewidth = 1.0f, .labels = true}));
            both.contours[1].id = 902;
            FigureSnapshot fs = one_axes(both);
            fs.generation = fs.data_generation = ++gen;
            export_figure_png(dev, data, fs, "contour_kind.png", W, H, 1);
            export_figure_svg(fs, "contour_kind.svg", W, H);
        }
        {
            const Probe& p = probes[0];
            check(near_rgb(p.data(0.25, 0.5), lut_at(0.25)) && near_rgb(p.data(0.75, 0.5), lut_at(0.75)),
                  "GL contourf: each band in its midpoint's colour");
            const Probe& q = probes[1];
            check(near_rgb(q.data(0.1, 0.5), kGrey, 2) && near_rgb(q.data(0.9, 0.5), kGrey, 2)
                      && near_rgb(q.data(0.5, 0.5), lut_at(0.5)),
                  "GL contourf: values outside the levels are not filled");
            const Probe& l = probes[2];
            check(near_rgb(l.data(1.0, 0.5), lut_at(0.0), 10) && near_rgb(l.data(2.0, 0.5), lut_at(0.5), 10)
                      && near_rgb(l.data(3.0, 0.5), lut_at(1.0), 10) && near_rgb(l.data(2.5, 0.5), kGrey, 2),
                  "GL contour: each line in its level's colour, nothing between");
            const std::uint8_t* hm = lut_at(0.0);  // the heatmap's single cell, value 0 of 0..1
            check(near_rgb(probes[3].data(2.0, 0.5), hm, 2) && near_rgb(probes[4].data(2.0, 0.5), lut_at(0.5), 10),
                  "GL contour: drawn in zorder (under a later heatmap, over it with zorder 1)");
        }

        // ---- SVG.
        {
            const std::string fs = render_figure_svg(one_axes(filled), W, H);
            check(count(fs, "<image") == 1 && count(fs, "<polyline") == 0, "SVG contourf: one <image>");
            const std::string ls = render_figure_svg(one_axes(lines), W, H);
            char buf[32];
            const std::uint8_t* mid = lut_at(0.5);
            std::snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)", mid[0], mid[1], mid[2]);
            check(count(ls, "<polyline") == 3 && ls.find(buf) != std::string::npos,
                  "SVG contour: a polyline per line in its colour");
            RenderSnapshot lab = lines;
            lab.contours[0].opts.labels = true;
            const std::string ts = render_figure_svg(one_axes(lab), W, H);
            check(ts.find(">2</text>") != std::string::npos && count(ts, "<polyline") == 6,
                  "SVG contour: labels break each line in two and name the level");
        }

        // ---- The colorbar.
        {
            RenderSnapshot cf = filled;
            cf.contours[0].opts.colorbar = true;
            const auto r = find_colorbar_requests(cf);
            check(r.size() == 1 && r[0].bounds == std::vector<double>{0, 0.5, 1}
                      && r[0].band_t == std::vector<double>{0.25, 0.75},
                  "colorbar: a contourf's is stepped at its levels, in its bands' colours");
            RenderSnapshot cl = lines;
            cl.contours[0].opts.colorbar = true;
            const auto q = find_colorbar_requests(cl);
            check(q.size() == 1 && q[0].bounds.empty() && q[0].vmin == 1.0f && q[0].vmax == 3.0f,
                  "colorbar: a contour's is the gradient over its limits");
        }

        // ---- Hover and picks.
        {
            const CoordTransform& tr = probes[0].tr;
            const auto hit = find_pick(filled, HintProjector(tr), tr.to_px(0.75), tr.to_py(0.5));
            check(hit && hit->kind == PickKind::Contour && hit->element == 1 && hit->area
                      && format_pick(filled, *hit).find("levels 0.5..1") != std::string::npos,
                  "hover contourf: the band under the cursor, its value and levels");
            const CoordTransform& lt = probes[2].tr;
            const auto h2 = find_pick(lines, HintProjector(lt), lt.to_px(2.0) + 3.0f, lt.to_py(0.5));
            check(h2 && h2->kind == PickKind::Contour && h2->element == 1 && !h2->area
                      && format_pick(lines, *h2) == "level=2",
                  "hover contour: near a line, its level");
            check(!find_pick(lines, HintProjector(lt), lt.to_px(2.5), lt.to_py(0.5)),
                  "hover contour: nothing between the lines");
        }

        // ---- The calls and read-back.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const std::vector<double> x{0, 1, 2}, y{0, 1}, z{0, 1, 2, 3, 4, 5};
            check(throws([&] { ax->contour(std::vector<double>{0}, y, std::vector<double>{0, 1}); })
                      && throws([&] { ax->contour(std::vector<double>{0, 2, 1}, y, z); })
                      && throws([&] { ax->contour(x, y, std::vector<double>{0, 1, 2}); })
                      && throws([&] { ax->contour(std::vector<double>{0, kNaN, 2}, y, z); })
                      && throws([&] { ax->contour(x, y, z, {.levels = {1, kNaN}}); })
                      && throws([&] { ax->contour(x, y, z, {.nlevels = 0}); })
                      && throws([&] { ax->contourf(x, y, z, {.levels = {0, 1}, .norm = ColorNorm::log()}); })
                      && ax->contour_count() == 0,
                  "api: bad grids, levels, nlevels and norms throw and add nothing");
            ax->contour(x, std::vector<double>{1, 0}, z, {.levels = {3, 1}});
            const ObjectId id = ax->last_object();
            ax->contourf(x, y, z);
            check(ax->contour_count() == 2 && ax->find_object(id)->kind == PickKind::Contour,
                  "api: contour and contourf are one kind; descending y is taken");
            const ContourData d0 = ax->contour_data(0), d1 = ax->contour_data(1);
            check(d0.levels == std::vector<double>{1, 3} && !d0.filled && d1.filled && d1.levels.size() >= 2
                      && d1.z == z && d1.x == x,
                  "api: contour_data gives the grid, the levels drawn and filled");
            ax->set_contour_data(0, ContourData{x, y, z, {}, true});
            check(ax->contour_data(0).filled && ax->contour_data(0).levels.size() > 2,
                  "api: set_contour_data with empty levels resolves them again; filled follows");
            ax->set_contour_data(0, x, y, std::vector<double>{0, 0, 0, 0, 0, 10});
            check(ax->contour_data(0).levels.back() == 10.0, "api: the span form keeps the options");
            check(throws([&] { ax->set_contour_data(0, x, y, std::vector<double>{1}); }), "api: and checks the grid");
            ax->set_contour_visible(1, false);
            check(!ax->contour_visible(1), "api: visibility");
            const Range xl = ax->xlim();
            check(xl.lo == 0.0 && xl.hi == 2.0, "api: limits are the grid's extent, unpadded");
            check(ax->remove(id) && ax->contour_count() == 1, "api: removed by its id");

            auto fig3 = Figure::create();
            auto pl = fig3->add_subplot3d(1, 1, 1)->plane(PlaneOrientation::XY, 0.5);
            pl->contourf(x, y, z, {.colorbar = true});
            pl->contour(x, y, z, {.labels = true});
            check(pl->contour_count() == 2 && throws([&] { pl->contour(x, y, std::vector<double>{1}); }),
                  "api: Plane2D takes both and checks the same");
            const FigureSnapshot fs3 = detail::FigureAccess::impl(*fig3).build_figure_snapshot();
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            const RgbaImage im = render_figure_rgba(dev, data, fs3, W, H);
            const std::string svg3 = render_figure_svg(fs3, W, H);
            check(im.width == W && count(svg3, "<image") >= 1, "plane: a contourf and a contour render (GL and SVG)");
        }

        // ---- Edits rebuild the drawing data.
        {
            RenderSnapshot s = lines;
            PlotStyleEdit e;
            e.plot_index = 0;
            ContourOptions o = s.contours[0].opts;
            o.levels = {2.5};
            e.opts = o;
            apply_plot_style_edit(s, e);
            check(s.contours[0].geom->levels == std::vector<double>{2.5} && s.contours[0].geom->lines.size() == 1,
                  "edit: a style edit re-derives levels and lines");
            PlotCellEdit c;
            c.kind = PlotKind::Contour;
            c.plot_index = 0;
            c.column = 0;
            c.element = 0;
            c.value = 9.0;
            check(apply_plot_data_op(s, c) && s.contours[0].z[0] == 9.0 && s.contours[0].geom->fill.values[0] == 9.0,
                  "edit: a z cell edit, and the triangles follow");
        }

        // ---- The panel draws a tab of each.
        {
            FigureSnapshot fs;
            RenderSnapshot r = lines;
            r.contours.push_back(filled.contours[0]);
            r.contours[1].id = 903;
            fs.axes.push_back({{1, 1, 1}, std::move(r)});
            fs.generation = fs.data_generation = 1;
            const IdConflictScan sc = scan_panel_for_id_conflicts(fs, &draw_data_window, "Data", {});
            check(sc.conflicts == 0 && sc.probes > 0, "panel: a contour's and a contourf's tabs draw without id conflicts");
        }
    }
} // namespace lt
