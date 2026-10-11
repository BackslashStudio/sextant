// Draw order follows call order across 2D kinds: the raster, the SVG, a plane's
// raster and plan, the legend and colorbars, and the hover hint. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    using namespace sextant;

    namespace {
        // Pixels within 6 of (r, g, b) on every channel.
        std::size_t count_rgb(const RgbaImage& im, int r, int g, int b) {
            std::size_t n = 0;
            for (std::size_t i = 0; i + 3 < im.pixels.size(); i += 4)
                if (std::abs(int(im.pixels[i]) - r) <= 6 && std::abs(int(im.pixels[i + 1]) - g) <= 6
                    && std::abs(int(im.pixels[i + 2]) - b) <= 6)
                    ++n;
            return n;
        }

        // Viridis at 0, the colour of a scatter_z point at z = vmin.
        constexpr int kPurple[3] = { 68, 1, 84 };
        constexpr Color kGreen{ 0.0f, 0.6f, 0.0f, 1.0f };
        constexpr Color kRed{ 1.0f, 0.0f, 0.0f, 1.0f };

        std::size_t reds(const RgbaImage& im) { return count_rgb(im, 255, 0, 0); }
        std::size_t purples(const RgbaImage& im) { return count_rgb(im, kPurple[0], kPurple[1], kPurple[2]); }
        std::size_t greens(const RgbaImage& im) { return count_rgb(im, 0, 153, 0); }

        ScatterOptions red_square(float size) {
            ScatterOptions o;
            o.color = kRed;
            o.size = size;
            o.marker = MarkerStyle::Square;
            o.alpha = 1.0f;
            return o;
        }

        ScatterZOptions purple_square(float size) {
            ScatterZOptions o;
            o.size = size;
            o.marker = MarkerStyle::Square;
            o.alpha = 1.0f;
            o.cmap = Colormap::Viridis;
            o.vmin = 0.0f;
            o.vmax = 1.0f;
            return o;
        }

        std::shared_ptr<Figure> figure_0_10(std::shared_ptr<Axes>& ax) {
            auto fig = Figure::create({ .width = 300, .height = 300 });
            ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0.0, 10.0).set_ylim(0.0, 10.0);
            return fig;
        }

        // A hand-built 2D snapshot over 0..10 (ids set by the caller).
        RenderSnapshot snap_0_10() {
            RenderSnapshot s;
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            return s;
        }

        HeatmapPlot heat_0_10(ObjectId id) {
            HeatmapPlot hp;
            hp.data = std::vector<float>{ 0.0f, 0.0f, 0.0f, 0.0f };
            hp.rows = hp.cols = 2;
            hp.xrange = { 0.0, 10.0 };
            hp.yrange = { 0.0, 10.0 };
            hp.id = id;
            return hp;
        }
    } // namespace

    // -------------------------------------------------------------------------
    // The 2D raster and SVG
    // -------------------------------------------------------------------------
    void test_draw_order_2d() {
        std::printf("\n[draw order: the 2D raster and SVG follow call order]\n");

        const std::vector<double> x{ 5.0 }, y{ 5.0 }, z{ 0.0 };

        // Two equal squares on one point: whichever came later is the one seen.
        // Before call order a scatter_z always drew over a scatter.
        auto squares = [&](bool z_last, bool reset_data) {
            std::shared_ptr<Axes> ax;
            auto fig = figure_0_10(ax);
            if (z_last) {
                ax->scatter(x, y, red_square(40.0f));
                ax->scatter_z(x, y, z, purple_square(40.0f));
            } else {
                ax->scatter_z(x, y, z, purple_square(40.0f));
                ax->scatter(x, y, red_square(40.0f));
            }
            // New data keeps an object's place (its id).
            if (reset_data) ax->set_scatter_data(0, x, y);
            return fig->render_rgba();
        };
        {
            const RgbaImage a = squares(true, false), b = squares(false, false);
            std::printf("  scatter then scatter_z: %zu red, %zu purple; reversed: %zu red, %zu purple\n",
                        reds(a), purples(a), reds(b), purples(b));
            check(reds(a) < 20 && purples(a) > 800,
                  "raster: a scatter_z made after a scatter covers it");
            check(purples(b) < 20 && reds(b) > 800,
                  "raster: and a scatter made after a scatter_z covers that");
            const RgbaImage c = squares(true, true);
            check(reds(c) < 20 && purples(c) > 800,
                  "raster: set_scatter_data() keeps the object's place in the order");
        }

        // A heatmap made after a line covers it (unlike matplotlib's zorder defaults).
        auto line_and_heat = [&](bool heat_last) {
            std::shared_ptr<Axes> ax;
            auto fig = figure_0_10(ax);
            const std::vector<double> lx{ 0.0, 10.0 }, ly{ 5.0, 5.0 };
            const std::vector<double> cells{ 0.0, 0.0, 0.0, 0.0 };
            LineOptions lo;
            lo.color = kRed;
            lo.linewidth = 6.0f;
            if (!heat_last) ax->heatmap(cells, 2, 2, { 0.0, 10.0 }, { 0.0, 10.0 });
            ax->line(lx, ly, lo);
            if (heat_last) ax->heatmap(cells, 2, 2, { 0.0, 10.0 }, { 0.0, 10.0 });
            return fig->render_rgba();
        };
        {
            const RgbaImage a = line_and_heat(true), b = line_and_heat(false);
            check(reds(a) < 20 && reds(b) > 300,
                  "raster: a heatmap made after a line covers it, one made before does not");
        }

        // Error bars draw with their own object: B's whisker, made after A, crosses
        // A's square. Before, every error bar drew under every marker.
        auto whisker = [&](int a_when) { // 0 = no A, 1 = A before B, 2 = A after B
            std::shared_ptr<Axes> ax;
            auto fig = figure_0_10(ax);
            const std::vector<double> ax_x{ 5.0 }, ax_y{ 6.5 }, bx{ 5.0 }, by{ 3.0 }, hi{ 6.0 };
            ScatterOptions bo;
            bo.color = Color::Blue;
            bo.size = 6.0f;
            bo.alpha = 1.0f;
            bo.errorbar.color = kGreen;
            bo.errorbar.linewidth = 4.0f;
            bo.errorbar.capsize = 0.0f;
            if (a_when == 1) ax->scatter(ax_x, ax_y, red_square(60.0f));
            ax->scatter(bx, by, ErrorBar{ .y_cap_hi = hi }, bo);
            if (a_when == 2) ax->scatter(ax_x, ax_y, red_square(60.0f));
            return fig->render_rgba();
        };
        {
            const std::size_t alone = greens(whisker(0)), over = greens(whisker(1)),
                              under = greens(whisker(2));
            std::printf("  whisker green px: alone %zu, made after the square %zu, before it %zu\n",
                        alone, over, under);
            check(under + 100 < alone, "raster: a square made after a whisker hides part of it");
            check(over + 10 >= alone,
                  "raster: an object's error bars draw with it, over an object made earlier");
        }

        // The SVG writes elements in the same order.
        auto svg = [&](bool line_first) {
            std::shared_ptr<Axes> ax;
            auto fig = figure_0_10(ax);
            const std::vector<double> lx{ 0.0, 10.0 }, ly{ 5.0, 5.0 };
            ScatterOptions so;
            so.marker = MarkerStyle::Circle;
            if (line_first) ax->line(lx, ly);
            ax->scatter(x, y, so);
            if (!line_first) ax->line(lx, ly);
            return fig->render_svg().svg;
        };
        {
            const std::string a = svg(true), b = svg(false);
            const auto pl_a = a.find("<polyline"), ci_a = a.find("<circle");
            const auto pl_b = b.find("<polyline"), ci_b = b.find("<circle");
            check(pl_a != std::string::npos && ci_a != std::string::npos && pl_a < ci_a
                  && pl_b != std::string::npos && ci_b != std::string::npos && ci_b < pl_b,
                  "SVG: elements in call order (line then scatter, and the reverse)");
        }
    }

    // -------------------------------------------------------------------------
    // A plane's sheet
    // -------------------------------------------------------------------------
    void test_draw_order_plane() {
        std::printf("\n[draw order: a plane's raster and SVG plan follow call order]\n");

        const std::vector<double> x{ 5.0 }, y{ 5.0 }, z{ 0.0 };
        auto squares = [&](bool z_last) {
            auto fig = Figure::create({ .width = 360, .height = 320 });
            auto ax = fig->add_subplot3d(1, 1, 1);
            ax->set_xlim(0.0, 10.0).set_ylim(0.0, 10.0).set_zlim(0.0, 10.0);
            auto pl = ax->plane(PlaneOrientation::XY, 0.0);
            if (z_last) {
                pl->scatter(x, y, red_square(60.0f));
                pl->scatter_z(x, y, z, purple_square(60.0f));
            } else {
                pl->scatter_z(x, y, z, purple_square(60.0f));
                pl->scatter(x, y, red_square(60.0f));
            }
            return fig->render_rgba();
        };
        const RgbaImage a = squares(true), b = squares(false);
        std::printf("  plane, scatter then scatter_z: %zu red, %zu purple; reversed: %zu red, %zu purple\n",
                    reds(a), purples(a), reds(b), purples(b));
        check(reds(a) < 20 && purples(a) > 300 && purples(b) < 20 && reds(b) > 300,
              "plane raster: the later of two squares on a plane is the one seen");

        // The SVG plan's batches: a heatmap sits where its call put it.
        using Kind = PlaneGeometry::Batch::Kind;
        auto kinds = [](ObjectId line_id, ObjectId heat_id) {
            PlaneSnapshot p;
            LinePlot lp;
            lp.x = std::vector<double>{ 0.0, 10.0 };
            lp.y = std::vector<double>{ 5.0, 5.0 };
            lp.id = line_id;
            p.sheet.lines.push_back(lp);
            p.sheet.heatmaps.push_back(heat_0_10(heat_id));
            std::vector<Kind> out;
            for (const auto& bt: plane_geometry(p).batches) out.push_back(bt.kind);
            return out;
        };
        check(kinds(1, 2) == std::vector<Kind>{ Kind::Seg, Kind::Heatmap }
              && kinds(2, 1) == std::vector<Kind>{ Kind::Heatmap, Kind::Seg },
              "plane SVG plan: a heatmap batch in call order among the others");
    }

    // -------------------------------------------------------------------------
    // Legend, colorbars, hover
    // -------------------------------------------------------------------------
    void test_draw_order_keys_and_hint() {
        std::printf("\n[draw order: legend, colorbars and hover follow call order]\n");

        {
            RenderSnapshot s = snap_0_10();
            auto named = [](auto plot, const char* name, ObjectId id) {
                plot.opts.name = name;
                plot.id = id;
                return plot;
            };
            s.lines.push_back(named(LinePlot{}, "line", 3));
            s.scatters.push_back(named(ScatterPlot{}, "dots", 4));
            s.scatter_z.push_back(named(ScatterZPlot{}, "flux", 2));
            s.bars.push_back(named(BarPlot{}, "bars", 1));
            const auto e = collect_legend_entries(s);
            check(e.size() == 4 && e[0].name == "bars" && e[1].name == "flux"
                  && e[2].name == "line" && e[3].name == "dots",
                  "legend: entries in call order, mixing kinds");

            HeatmapPlot hp = heat_0_10(5);
            hp.opts.colorbar = true;
            hp.opts.name = "heat";
            s.heatmaps.push_back(hp);
            s.scatter_z[0].opts.colorbar = true;
            const auto cb = find_colorbar_requests(s);
            check(cb.size() == 2 && cb[0].name == "flux" && cb[1].name == "heat",
                  "colorbars: in call order (outward from the frame), not heatmaps first");
        }

        // Hover: of two points at one spot, the later object's; a heatmap made
        // later hides the point, so its cell wins.
        const CoordTransform tr{0.0, 10.0, 0.0, 10.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
        auto pick = [&](ObjectId line_id, ObjectId scatter_id, ObjectId heat_id) {
            RenderSnapshot s = snap_0_10();
            LinePlot lp;
            lp.x = std::vector<double>{ 5.0 };
            lp.y = std::vector<double>{ 5.0 };
            lp.id = line_id;
            s.lines.push_back(lp);
            ScatterPlot sp;
            sp.x = std::vector<double>{ 5.0 };
            sp.y = std::vector<double>{ 5.0 };
            sp.id = scatter_id;
            s.scatters.push_back(sp);
            if (heat_id) s.heatmaps.push_back(heat_0_10(heat_id));
            const auto hit = find_pick(s, tr, tr.to_px(5.0), tr.to_py(5.0));
            return hit ? std::optional<PickKind>(hit->kind) : std::nullopt;
        };
        check(pick(2, 1, 0) == PickKind::Line && pick(1, 2, 0) == PickKind::Scatter,
              "hover: of equal distances, the object made later (the one on top)");
        check(pick(1, 2, 3) == PickKind::Heatmap && pick(2, 3, 1) == PickKind::Scatter,
              "hover: a heatmap made after the point hides it; one made before does not");
    }
} // namespace lt
