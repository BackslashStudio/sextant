// Per-object zorder: the paint and hover order is (zorder, ObjectId); the legend
// and colorbars stay in call order. Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    using namespace sextant;

    namespace {
        // A hand-built 2D snapshot over 0..10 (ids and zorders set by the caller).
        RenderSnapshot snap_0_10() {
            RenderSnapshot s;
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            return s;
        }

        template <class P>
        P with_id_z(ObjectId id, double z) {
            P p;
            p.id = id;
            p.opts.zorder = z;
            return p;
        }

        // Pixels within 6 of (r, g, b) on every channel.
        std::size_t count_rgb(const RgbaImage& im, int r, int g, int b) {
            std::size_t n = 0;
            for (std::size_t i = 0; i + 3 < im.pixels.size(); i += 4)
                if (std::abs(int(im.pixels[i]) - r) <= 6 && std::abs(int(im.pixels[i + 1]) - g) <= 6
                    && std::abs(int(im.pixels[i + 2]) - b) <= 6)
                    ++n;
            return n;
        }
        constexpr Color kRed{ 1.0f, 0.0f, 0.0f, 1.0f };
        std::size_t reds(const RgbaImage& im) { return count_rgb(im, 255, 0, 0); }
        std::size_t purples(const RgbaImage& im) { return count_rgb(im, 68, 1, 84); } // viridis at 0

        ScatterOptions red_square(float size, double zorder = 0.0) {
            ScatterOptions o;
            o.color = kRed;
            o.size = size;
            o.marker = MarkerStyle::Square;
            o.alpha = 1.0f;
            o.zorder = zorder;
            return o;
        }

        ScatterZOptions purple_square(float size, double zorder = 0.0) {
            ScatterZOptions o;
            o.size = size;
            o.marker = MarkerStyle::Square;
            o.alpha = 1.0f;
            o.cmap = Colormap::Viridis;
            o.vmin = 0.0f;
            o.vmax = 1.0f;
            o.zorder = zorder;
            return o;
        }

        std::shared_ptr<Figure> figure_0_10(std::shared_ptr<Axes>& ax) {
            auto fig = Figure::create({ .width = 300, .height = 300 });
            ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0.0, 10.0).set_ylim(0.0, 10.0);
            return fig;
        }

        HeatmapPlot heat_0_10(ObjectId id, double z = 0.0) {
            HeatmapPlot hp;
            hp.data = std::vector<float>{ 0.0f, 0.0f, 0.0f, 0.0f };
            hp.rows = hp.cols = 2;
            hp.xrange = { 0.0, 10.0 };
            hp.yrange = { 0.0, 10.0 };
            hp.id = id;
            hp.opts.zorder = z;
            return hp;
        }

        bool is(const ObjectRef& r, PlotKind kind, std::size_t index) {
            return r.kind == kind && r.index == index;
        }
    } // namespace

    void test_zorder_order() {
        std::printf("\n[zorder: draw_order is (zorder, id); call_order stays id-only]\n");

        {
            RenderSnapshot s = snap_0_10();
            s.lines.push_back(with_id_z<LinePlot>(2, -1.0));
            s.scatters.push_back(with_id_z<ScatterPlot>(1, 0.0));
            const auto d = draw_order(s);
            check(d.size() == 2 && is(d[0], PlotKind::Line, 0) && is(d[1], PlotKind::Scatter, 0),
                  "zorder: a lower zorder paints first although it was made later");
            const auto c = call_order(s);
            check(c.size() == 2 && is(c[0], PlotKind::Scatter, 0) && is(c[1], PlotKind::Line, 0),
                  "zorder: call_order ignores zorder (legend and colorbars)");
        }
        {
            RenderSnapshot s = snap_0_10();
            s.lines.push_back(with_id_z<LinePlot>(3, 0.0));
            s.scatters.push_back(with_id_z<ScatterPlot>(1, 0.0));
            s.bars.push_back(with_id_z<BarPlot>(2, 0.0));
            const auto d = draw_order(s);
            const auto c = call_order(s);
            bool same = d.size() == 3 && c.size() == 3;
            for (std::size_t i = 0; same && i < d.size(); ++i)
                same = d[i].kind == c[i].kind && d[i].index == c[i].index;
            check(same && is(d[0], PlotKind::Scatter, 0) && is(d[1], PlotKind::Bar, 0)
                      && is(d[2], PlotKind::Line, 0),
                  "zorder: equal zorders give call order across kinds");
        }
        {
            RenderSnapshot s = snap_0_10();
            s.lines.push_back(with_id_z<LinePlot>(1, 0.5));
            s.scatters.push_back(with_id_z<ScatterPlot>(2, 0.25));
            s.bars.push_back(with_id_z<BarPlot>(3, -7.5));
            const auto d = draw_order(s);
            check(d.size() == 3 && is(d[0], PlotKind::Bar, 0) && is(d[1], PlotKind::Scatter, 0)
                      && is(d[2], PlotKind::Line, 0),
                  "zorder: fractional and negative values order by value");
        }
        {
            RenderSnapshot s = snap_0_10();
            s.lines.push_back(with_id_z<LinePlot>(1, 1.0));
            s.lines.push_back(with_id_z<LinePlot>(2, 1.0));
            s.lines.push_back(with_id_z<LinePlot>(3, 0.0));
            const auto d = draw_order(s);
            check(d.size() == 3 && d[0].index == 2 && d[1].index == 0 && d[2].index == 1,
                  "zorder: ties keep call order");
        }
        {
            RenderSnapshot s = snap_0_10();
            s.lines.push_back(with_id_z<LinePlot>(1, 5.0));
            s.lines.back().opts.visible = false;
            s.scatters.push_back(with_id_z<ScatterPlot>(2, 0.0));
            const auto d = draw_order(s);
            check(d.size() == 1 && is(d[0], PlotKind::Scatter, 0),
                  "zorder: a hidden object is left out of draw_order too");
        }
    }

    namespace {
        struct ZMessages {
            std::vector<std::string> v;
            MessageHandler           old;
            ZMessages() { old = Figure::set_message_handler([this](std::string_view m) { v.emplace_back(m); }); }
            ~ZMessages() { Figure::set_message_handler(old); }
        };

        // One object of each 2D kind a holder takes, giving their ids.
        template <class H>
        std::vector<ObjectId> make_kinds(H& h, bool with_refs) {
            const std::vector<double> x{ 1.0, 2.0, 3.0 }, y{ 1.0, 2.0, 3.0 }, z{ 0.0, 0.5, 1.0 };
            const std::vector<double> cells{ 0.0, 1.0, 2.0, 3.0 };
            std::vector<ObjectId> ids;
            h.line(x, y);          ids.push_back(h.last_object());
            h.scatter(x, y);       ids.push_back(h.last_object());
            h.scatter_z(x, y, z);  ids.push_back(h.last_object());
            h.bar(x, y);           ids.push_back(h.last_object());
            h.heatmap(cells, 2, 2, { 0.0, 1.0 }, { 0.0, 1.0 }); ids.push_back(h.last_object());
            h.fill(x, y, 0.0);     ids.push_back(h.last_object());
            h.quiver(x, y, y, x);  ids.push_back(h.last_object());
            if constexpr (std::is_same_v<H, Axes>) {
                if (with_refs) {
                    h.axline(1.0); ids.push_back(h.last_object());
                    h.axspan(1.0, 2.0); ids.push_back(h.last_object());
                }
            }
            return ids;
        }
    } // namespace

    void test_zorder_api() {
        std::printf("\n[zorder: set_zorder() / zorder(), validation]\n");
        const std::vector<double> x{ 5.0 }, y{ 5.0 }, z{ 0.0 };

        {
            auto fig = Figure::create();
            auto ax  = fig->add_subplot(1, 1, 1);
            const std::vector<ObjectId> ids = make_kinds(*ax, true);
            bool ok = ids.size() == 9;
            for (std::size_t i = 0; ok && i < ids.size(); ++i) {
                ok = ax->zorder(ids[i]) == 0.0 && ax->set_zorder(ids[i], 1.5 + double(i))
                     && ax->zorder(ids[i]) == 1.5 + double(i);
            }
            check(ok, "zorder API: set/get round-trips on each of the nine Axes kinds");

            auto fig3 = Figure::create();
            auto ax3  = fig3->add_subplot3d(1, 1, 1);
            auto pl   = ax3->plane(PlaneOrientation::XY, 0.0);
            const std::vector<ObjectId> pids = make_kinds(*pl, false);
            ok = pids.size() == 7;
            for (std::size_t i = 0; ok && i < pids.size(); ++i)
                ok = pl->set_zorder(pids[i], -2.0 - double(i)) && pl->zorder(pids[i]) == -2.0 - double(i);
            check(ok, "zorder API: set/get round-trips on each kind of a Plane2D");
        }

        {
            auto fig = Figure::create();
            auto ax  = fig->add_subplot(1, 1, 1);
            ax->line(x, y);
            const ObjectId ln = ax->last_object();
            ax->text("t", 1.0, 1.0);
            const ObjectId tx = ax->last_object();
            ax->scatter(x, y);
            const ObjectId sc = ax->last_object();
            ax->remove(sc);

            ZMessages msgs;
            check(!ax->set_zorder(sc, 3.0) && msgs.v.size() == 1
                      && msgs.v[0].find("Axes::set_zorder") != std::string::npos,
                  "zorder API: a removed id gives false and one message");
            check(!ax->set_zorder(tx, 3.0) && msgs.v.size() == 2 && !ax->zorder(tx),
                  "zorder API: a text gives false, a message, and no zorder");
            check(!ax->zorder(sc) && !ax->set_zorder(0, 1.0),
                  "zorder API: nothing for a removed id or id 0");
            check(ax->zorder(ln) == 0.0, "zorder API: a failed set changes nothing");
        }

        {
            auto fig = Figure::create();
            auto ax  = fig->add_subplot(1, 1, 1);
            ax->line(x, y);
            const ObjectId ln = ax->last_object();
            auto throws = [](auto&& f) {
                try { f(); } catch (const std::invalid_argument&) { return true; } catch (...) {}
                return false;
            };
            check(throws([&] { ax->set_zorder(ln, std::nan("")); })
                      && throws([&] { ax->set_zorder(ln, INFINITY); })
                      && throws([&] { ax->line(x, y, { .zorder = std::nan("") }); })
                      && throws([&] { ax->scatter(x, y, { .zorder = -INFINITY }); })
                      && throws([&] { ax->heatmap(std::vector<double>{ 0, 1, 2, 3 }, 2, 2, { 0.0, 1.0 },
                                                  { 0.0, 1.0 }, { .zorder = INFINITY }); })
                      && throws([&] { ax->axline(1.0, { .zorder = std::nan("") }); })
                      && throws([&] { ax->stack(x, std::vector<std::vector<double>>{ y }, { .zorder = INFINITY }); }),
                  "zorder API: NaN and inf throw, from set_zorder() and from the plotting calls");
            check(ax->line_count() == 1 && ax->zorder(ln) == 0.0,
                  "zorder API: a throwing call adds and changes nothing");
        }

        {
            // Kept through set_*_data() and through removing a sibling; stack() hands it to every layer.
            auto fig = Figure::create();
            auto ax  = fig->add_subplot(1, 1, 1);
            ax->line(x, y);
            const ObjectId a = ax->last_object();
            ax->line(x, y);
            const ObjectId b = ax->last_object();
            ax->set_zorder(b, 4.0);
            ax->set_line_data(1, x, y);
            ax->remove(a);
            check(ax->zorder(b) == 4.0, "zorder API: kept by set_line_data() and by removing another object");

            const std::vector<double> sx{ 0.0, 1.0, 2.0 };
            const std::vector<std::vector<double>> ys{ { 1.0, 1.0, 1.0 }, { 2.0, 2.0, 2.0 } };
            ax->stack(sx, ys, { .zorder = 2.0 });
            const ObjectId s1 = ax->last_object();
            check(ax->fill_count() == 2 && ax->zorder(s1) == 2.0 && ax->zorder(s1 - 1) == 2.0,
                  "zorder API: stack() gives every layer StackOptions::zorder");
        }

        {
            // set_zorder() flips the rendered winner.
            auto render = [&](bool flip) {
                std::shared_ptr<Axes> ax;
                auto fig = figure_0_10(ax);
                ax->scatter(x, y, red_square(40.0f));
                const ObjectId r = ax->last_object();
                ax->scatter_z(x, y, z, purple_square(40.0f));
                if (flip) ax->set_zorder(r, 1.0);
                return fig->render_rgba();
            };
            const RgbaImage a = render(false), b = render(true);
            check(purples(a) > 800 && reds(a) < 20 && reds(b) > 800 && purples(b) < 20,
                  "zorder API: set_zorder() changes which object paints on top");
        }
    }

    void test_zorder_edit() {
        std::printf("\n[zorder: a Data-panel style edit and its stamp]\n");

        RenderSnapshot s = snap_0_10();
        s.lines.push_back(with_id_z<LinePlot>(1, 0.0));
        LineOptions styled;
        styled.zorder = 5.0;
        PlotStyleEdit ps{ 0, -1, styled };
        ps.id = 1;

        std::vector<PlotStyleEdit> inv;
        apply_plot_style_edits(s, { ps }, &inv);
        check(s.lines[0].opts.zorder == 5.0 && inv.size() == 1,
              "zorder edit: a style edit carrying zorder 5 sets it");
        apply_plot_style_edits(s, inv);
        check(s.lines[0].opts.zorder == 0.0, "zorder edit: its inverse restores the old zorder");

        // A set_zorder() since the panel drew wins, and visible_seen is unaffected.
        s.lines[0].zorder_stamp = 50;
        ps.zorder_seen = 40;
        apply_plot_style_edits(s, { ps });
        check(s.lines[0].opts.zorder == 0.0,
              "zorder edit: an edit over an older snapshot keeps the program's zorder");
        ps.zorder_seen = 50;
        apply_plot_style_edits(s, { ps });
        check(s.lines[0].opts.zorder == 5.0, "zorder edit: one over the current snapshot applies");

        s.lines[0].visible_stamp = 60;
        s.lines[0].opts.visible = false;
        LineOptions shown = styled;
        shown.visible = true;
        PlotStyleEdit pv{ 0, -1, shown };
        pv.id = 1;
        pv.visible_seen = 10;
        pv.zorder_seen = 50;
        apply_plot_style_edits(s, { pv });
        check(!s.lines[0].opts.visible, "zorder edit: visible_seen still keeps the program's visible");

        // A non-finite zorder (a typed "nan") never reaches the object: it would break the sort.
        s.lines[0].opts.zorder = 2.0;
        LineOptions nan_opts = styled;
        nan_opts.zorder = std::nan("");
        PlotStyleEdit pn{ 0, -1, nan_opts };
        pn.id = 1;
        apply_plot_style_edits(s, { pn });
        nan_opts.zorder = INFINITY;
        pn.opts = nan_opts;
        apply_plot_style_edits(s, { pn });
        check(s.lines[0].opts.zorder == 2.0,
              "zorder edit: a NaN or infinite zorder in a style edit keeps the object's");

        // stamp_edit() records both stamps.
        AxesEdit e;
        e.plot_styles.push_back(PlotStyleEdit{ 0, -1, styled });
        stamp_edit(e, s);
        check(e.plot_styles[0].zorder_seen == 50 && e.plot_styles[0].visible_seen == 60,
              "zorder edit: stamp_edit() records the object's zorder_stamp and visible_stamp");
    }

    void test_zorder_render() {
        std::printf("\n[zorder: the raster, SVG, plane, hover and twins paint by zorder]\n");
        const std::vector<double> x{ 5.0 }, y{ 5.0 }, z{ 0.0 };

        // Raster: a heatmap made after a line covers it unless its zorder is lower.
        auto line_and_heat = [&](double heat_z) {
            std::shared_ptr<Axes> ax;
            auto fig = figure_0_10(ax);
            const std::vector<double> lx{ 0.0, 10.0 }, ly{ 5.0, 5.0 };
            const std::vector<double> cells{ 0.0, 0.0, 0.0, 0.0 };
            LineOptions lo;
            lo.color = kRed;
            lo.linewidth = 6.0f;
            ax->line(lx, ly, lo);
            ax->heatmap(cells, 2, 2, { 0.0, 10.0 }, { 0.0, 10.0 }, { .zorder = heat_z });
            return fig->render_rgba();
        };
        check(reds(line_and_heat(0.0)) < 20 && reds(line_and_heat(-1.0)) > 300,
              "zorder raster: a heatmap made after a line is under it with zorder -1");

        // SVG: a scatter made first with zorder 1 is written after a later line.
        {
            std::shared_ptr<Axes> ax;
            auto fig = figure_0_10(ax);
            const std::vector<double> lx{ 0.0, 10.0 }, ly{ 5.0, 5.0 };
            ScatterOptions so;
            so.marker = MarkerStyle::Circle;
            so.zorder = 1.0;
            ax->scatter(x, y, so);
            ax->line(lx, ly);
            const std::string svg = fig->render_svg().svg;
            const auto pl = svg.find("<polyline"), ci = svg.find("<circle");
            check(pl != std::string::npos && ci != std::string::npos && pl < ci,
                  "zorder SVG: a scatter with a higher zorder is written after a later line");
        }

        // A plane: raster and SVG plan.
        {
            auto squares = [&](double scatter_z_z) {
                auto fig = Figure::create({ .width = 360, .height = 320 });
                auto ax = fig->add_subplot3d(1, 1, 1);
                ax->set_xlim(0.0, 10.0).set_ylim(0.0, 10.0).set_zlim(0.0, 10.0);
                auto pl = ax->plane(PlaneOrientation::XY, 0.0);
                pl->scatter(x, y, red_square(60.0f));
                pl->scatter_z(x, y, z, purple_square(60.0f, scatter_z_z));
                return fig->render_rgba();
            };
            const RgbaImage a = squares(0.0), b = squares(-1.0);
            check(purples(a) > 300 && reds(a) < 20 && reds(b) > 300 && purples(b) < 20,
                  "zorder plane raster: a lower zorder puts the later square under");

            using Kind = PlaneGeometry::Batch::Kind;
            PlaneSnapshot p;
            LinePlot lp;
            lp.x = std::vector<double>{ 0.0, 10.0 };
            lp.y = std::vector<double>{ 5.0, 5.0 };
            lp.id = 2;
            p.sheet.lines.push_back(lp);
            p.sheet.heatmaps.push_back(heat_0_10(1, 3.0));
            std::vector<Kind> kinds;
            for (const auto& bt: plane_geometry(p).batches) kinds.push_back(bt.kind);
            check(kinds == std::vector<Kind>{ Kind::Seg, Kind::Heatmap },
                  "zorder plane SVG plan: a heatmap made first with a higher zorder batches last");
        }

        // Hover.
        {
            const CoordTransform tr{0.0, 10.0, 0.0, 10.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
            auto pick = [&](double line_z, double scatter_z, bool heat) {
                RenderSnapshot s = snap_0_10();
                LinePlot lp;
                lp.x = std::vector<double>{ 5.0 };
                lp.y = std::vector<double>{ 5.0 };
                lp.id = 1;
                lp.opts.zorder = line_z;
                s.lines.push_back(lp);
                ScatterPlot sp;
                sp.x = std::vector<double>{ 5.0 };
                sp.y = std::vector<double>{ 5.0 };
                sp.id = 2;
                sp.opts.zorder = scatter_z;
                s.scatters.push_back(sp);
                if (heat) s.heatmaps.push_back(heat_0_10(3, 100.0));
                const auto hit = find_pick(s, tr, tr.to_px(5.0), tr.to_py(5.0));
                return hit ? std::optional<PickKind>(hit->kind) : std::nullopt;
            };
            check(pick(0.0, 0.0, false) == PickKind::Scatter && pick(1.0, 0.0, false) == PickKind::Line,
                  "zorder hover: of equal distances, the topmost by zorder, then by call");
            check(pick(0.0, 0.0, true) == PickKind::Scatter,
                  "zorder hover: a point still beats a heatmap whatever its zorder");
        }

        // A twinned cell sorts across layers.
        {
            auto build = [&](double host_z) {
                std::shared_ptr<Axes> ax;
                auto fig = figure_0_10(ax);
                auto twin = ax->twinx();
                twin->set_ylim(0.0, 10.0);
                twin->scatter_z(x, y, z, purple_square(60.0f));
                ax->scatter(x, y, red_square(60.0f, host_z));
                return fig->render_rgba();
            };
            const RgbaImage a = build(0.0), b = build(-1.0);
            check(reds(a) > 300 && purples(a) < 20 && purples(b) > 300 && reds(b) < 20,
                  "zorder twins: a host object made later goes under a twin's by zorder");
        }

        // The legend keeps call order.
        {
            RenderSnapshot s = snap_0_10();
            LinePlot lp;
            lp.id = 1;
            lp.opts.name = "first";
            lp.opts.zorder = 9.0;
            s.lines.push_back(lp);
            ScatterPlot sp;
            sp.id = 2;
            sp.opts.name = "second";
            sp.opts.zorder = -9.0;
            s.scatters.push_back(sp);
            const auto e = collect_legend_entries(s);
            check(e.size() == 2 && e[0].name == "first" && e[1].name == "second",
                  "zorder legend: entries stay in call order");
        }
    }
} // namespace lt
