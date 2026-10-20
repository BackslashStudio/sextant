// Arrow fields (step 36e-1): Axes::quiver() and Plane2D::quiver(). The call,
// read-back, set_quiver_data(), the automatic amp and cla(); the arrows
// (pivots, what is left out, log), their outline and triangles, automatic
// limits; the Data panel, its cell, row and style edits with undo, the
// journal's replay; legend keys; hover and pick; planes; GL against SVG.
// Colored arrows (step 36e-2): c, the map, the colorbar and key, the panel.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "axis_limits.h"
#include "figure_impl.h"
#include "plot_data_view.h"
#include "colormaps.h"
#include "quiver_geometry.h"
#include <cmath>

namespace lt {
    using namespace sextant;

    namespace {
        using AxesImpl = detail::FigureAccess::Impl::AxesImpl;
        using V = std::vector<double>;

        template <class F>
        bool throws_invalid(F&& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        template <class F>
        bool throws_range(F&& f) {
            try {
                f();
            } catch (const std::out_of_range&) {
                return true;
            }
            return false;
        }

        bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

        std::string range_str(Range r) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g..%g", r.lo, r.hi);
            return buf;
        }

        std::string read_file(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        // Every `<tag ...>` element whose text holds `needle`, in document order.
        std::vector<std::string> elements(const std::string& svg, const std::string& tag, const std::string& needle) {
            std::vector<std::string> out;
            const std::string open = "<" + tag + " ";
            for (std::size_t p = svg.find(open); p != std::string::npos; p = svg.find(open, p + 1)) {
                const std::string el = svg.substr(p, svg.find('>', p) - p);
                if (el.find(needle) != std::string::npos) out.push_back(el);
            }
            return out;
        }

        struct Png {
            int w = 0, h = 0;
            std::vector<unsigned char> px;

            explicit Png(const char* path) {
                int comp = 0;
                unsigned char* p = stbi_load(path, &w, &h, &comp, 4);
                if (p) px.assign(p, p + static_cast<std::size_t>(w) * h * 4);
                stbi_image_free(p);
            }

            const unsigned char* at(float x, float y) const {
                const int ix = static_cast<int>(x), iy = static_cast<int>(y);
                if (ix < 0 || iy < 0 || ix >= w || iy >= h || px.empty()) return nullptr;
                return px.data() + (static_cast<std::size_t>(iy) * w + ix) * 4;
            }
            // Within `tol` levels of (r, g, b).
            bool is(float x, float y, int r, int g, int b, int tol = 40) const {
                const unsigned char* p = at(x, y);
                return p && std::abs(p[0] - r) < tol && std::abs(p[1] - g) < tol && std::abs(p[2] - b) < tol;
            }
        };

        constexpr Color kRed{0.8f, 0.0f, 0.0f, 1.0f};
        constexpr Color kGreen{0.0f, 0.6f, 0.0f, 1.0f};
        constexpr Color kBlue{0.0f, 0.0f, 0.8f, 1.0f};

        QuiverPlot quiver_of(V x, V y, V u, V v, Color c, ObjectId id, double amp = 1.0) {
            QuiverPlot p;
            p.x = std::move(x);
            p.y = std::move(y);
            p.u = std::move(u);
            p.v = std::move(v);
            p.opts.color = c;
            p.opts.amp = amp;
            p.id = id;
            return p;
        }

        struct Arrow {
            std::size_t i;
            double tx, ty, hx, hy;
        };

        std::vector<Arrow> arrows(const QuiverPlot& p, const AxisScale& xs = {}, const AxisScale& ys = {}) {
            std::vector<Arrow> out;
            for_each_quiver_arrow(p, xs, ys, [&](std::size_t i, double tx, double ty, double hx, double hy) {
                out.push_back({i, tx, ty, hx, hy});
            });
            return out;
        }

        // The area an outline's triangles cover.
        double outline_area(int n, const float (&pts)[14]) {
            double a = 0.0;
            arrow_triangles(n, [&](int i, int j, int k) {
                a += 0.5 * std::abs((pts[2 * j] - pts[2 * i]) * (pts[2 * k + 1] - pts[2 * i + 1]) -
                                    (pts[2 * k] - pts[2 * i]) * (pts[2 * j + 1] - pts[2 * i + 1]));
            });
            return a;
        }

        Range xlim_of(const RenderSnapshot& s) {
            const ResolvedLimits l = resolve_limits(s);
            return {l.xmin, l.xmax};
        }
        Range ylim_of(const RenderSnapshot& s) {
            const ResolvedLimits l = resolve_limits(s);
            return {l.ymin, l.ymax};
        }
    } // namespace

    static void quiver_api_checks();
    static void quiver_geometry_checks();
    static void quiver_limit_checks();
    static void quiver_panel_checks();
    static void quiver_color_checks();

    void test_quiver() {
        std::printf("\n[2D quiver]\n");
        try {
            quiver_api_checks();
            quiver_geometry_checks();
            quiver_limit_checks();
            quiver_panel_checks();
            quiver_color_checks();
        } catch (const std::exception& e) {
            check(false, std::string("quiver: unexpected exception: ") + e.what());
        }
    }

    // --- The call, read-back, set_quiver_data(), amp, cla(), objects, planes ---
    static void quiver_api_checks() {
        auto fig = Figure::create();
        auto ax = fig->add_subplot(1, 1, 1);
        const V x{0.0, 1.0, 2.0}, y{0.0, 0.0, 0.0}, u{1.0, 0.0, -1.0}, v{0.0, 1.0, 0.0};
        ax->quiver(x, y, u, v, {.amp = 0.5});
        const QuiverData d = ax->quiver_data(0);
        check(ax->quiver_count() == 1 && d.x == x && d.y == y && d.u == u && d.v == v,
              "call: stores x, y, u and v, read back as given");
        check(throws_invalid([&] { ax->quiver(x, V{1.0}, u, v); }) &&
                  throws_invalid([&] { ax->quiver(x, y, u, V{1.0, 2.0}); }) &&
                  throws_invalid([&] { ax->quiver(x, y, u, v, {.amp = 0.0}); }) &&
                  throws_invalid([&] { ax->quiver(x, y, u, v, {.amp = -1.0}); }) &&
                  throws_invalid([&] { ax->quiver(x, y, u, v, {.amp = NAN}); }) && ax->quiver_count() == 1,
              "call: unequal lengths or an amp not finite and > 0 throw, and nothing is added");
        check(ax->quiver(V{}, V{}, V{}, V{}).quiver_count() == 2 && ax->quiver_data(1).x.empty(),
              "call: an empty quiver is allowed (it draws nothing)");

        // The amp: as given, or chosen at the call and kept.
        {
            auto& fi = detail::FigureAccess::impl(*fig);
            AxesImpl* a = fi.find_slot_impl(1);
            check(a->quivers[0].amp() == 0.5, "amp: as given");
            // 3 x 3 unit vectors over x in [0, 2]: N = 9, so max(10, 3) = 10;
            // the mean arrow is 2 / (1.8 * 10).
            V gx, gy, gu, gv;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) {
                    gx.push_back(j);
                    gy.push_back(i);
                    gu.push_back(0.6);
                    gv.push_back(0.8);
                }
            ax->quiver(gx, gy, gu, gv);
            check(near(a->quivers[2].amp(), 2.0 / 18.0, 1e-15),
                  "amp: unset, the mean arrow is the x extent / (1.8 * max(10, sqrt(N))) (" +
                      std::to_string(a->quivers[2].amp()) + ")");
            // 400 arrows: sqrt(N) = 20 wins; a NaN arrow and a zero vector change
            // nothing but the count (the zero one counts in N, not the mean).
            V bx, by, bu, bv;
            for (int k = 0; k < 400; ++k) {
                bx.push_back(k * 0.01);
                by.push_back(0.0);
                bu.push_back(k == 7 ? 0.0 : 2.0);
                bv.push_back(0.0);
            }
            bx.push_back(NAN);
            by.push_back(0.0);
            bu.push_back(50.0);
            bv.push_back(0.0);
            ax->quiver(bx, by, bu, bv);
            check(near(a->quivers[3].amp(), 3.99 / (1.8 * 20.0 * 2.0), 1e-12),
                  "amp: N counts the finite arrows, the mean the nonzero ones (" +
                      std::to_string(a->quivers[3].amp()) + ")");
            // Degenerate: one point uses the y extent then 1; all-zero vectors give 1.
            ax->quiver(V{1.0, 1.0}, V{0.0, 4.0}, V{1.0, 1.0}, V{0.0, 0.0});
            ax->quiver(V{1.0}, V{1.0}, V{0.0}, V{0.0});
            check(near(a->quivers[4].amp(), 4.0 / 18.0, 1e-15) && a->quivers[5].amp() == 1.0,
                  "amp: x extent 0 falls back to the y extent; no nonzero vector gives 1");
            ax->set_quiver_data(2, V{0.0, 100.0}, V{0.0, 0.0}, V{9.0, 9.0}, V{0.0, 0.0});
            check(near(a->quivers[2].amp(), 2.0 / 18.0, 1e-15) && ax->quiver_data(2).x.size() == 2,
                  "set_quiver_data(spans): replaces the arrows and keeps the amp");
        }
        ax->set_quiver_data(0, QuiverData{V{5.0}, V{6.0}, V{7.0}, V{8.0}});
        check(ax->quiver_data(0).u == V{7.0} && ax->quiver_data(0).v == V{8.0}, "set_quiver_data(QuiverData)");
        check(throws_invalid([&] { ax->set_quiver_data(0, QuiverData{V{1.0}, V{1.0, 2.0}, V{1.0}, V{1.0}}); }) &&
                  ax->quiver_data(0).x == V{5.0},
              "set_quiver_data: bad lengths throw and change nothing");
        check(throws_range([&] { (void)ax->quiver_data(9); }) &&
                  throws_range([&] { ax->set_quiver_data(9, x, y, u, v); }),
              "read-back: an index past the count throws out_of_range");

        // Color: the cycle, as a line takes it.
        {
            auto cyc = Figure::create();
            auto c1 = cyc->add_subplot(1, 1, 1);
            c1->quiver(x, y, u, v).line(x, y);
            const FigureSnapshot cs = detail::FigureAccess::impl(*cyc).build_figure_snapshot();
            const RenderSnapshot& c = *cs.axes[0].snap2d();
            const Color& q = *c.quivers[0].opts.color;
            const Color& l = *c.lines[0].opts.color;
            check(q.r == Color::Blue.r && q.g == Color::Blue.g && q.b == Color::Blue.b && !(l.r == q.r && l.g == q.g),
                  "color: quiver() takes the next cycle color");
        }
        // Removal and restore by id, as other kinds; cla().
        {
            auto& fi = detail::FigureAccess::impl(*fig);
            AxesImpl* a = fi.find_slot_impl(1);
            const ObjectId id = a->quivers[1].id;
            const std::size_t n = a->quivers.size();
            const auto ref = a->find_object(id);
            auto removed = a->remove_object(id);
            check(ref && ref->kind == PlotKind::Quiver && ref->index == 1 && removed && a->quivers.size() == n - 1,
                  "objects: a quiver is found and removed by id");
            check(removed && a->restore_object(std::move(*removed)) && a->quivers.size() == n &&
                      a->quivers[1].id == id,
                  "objects: and restored with its id");
        }
        ax->cla();
        check(ax->quiver_count() == 0, "cla(): clears quivers");
        // Planes: the same call, and its arrows feed the parent's limits.
        {
            auto f3 = Figure::create();
            auto a3 = f3->add_subplot3d(1, 1, 1);
            auto pl = a3->plane(PlaneOrientation::XY, 0.5);
            pl->quiver(V{0.0, 1.0}, V{0.0, 0.0}, V{1.0, 1.0}, V{0.0, 0.0}, {.amp = 1.0});
            check(pl->quiver_count() == 1 && pl->quiver_data(0).u == V({1.0, 1.0}), "plane: quiver() and its read-back");
            pl->set_quiver_data(0, V{0.0, 1.0}, V{0.0, 0.0}, V{1.0, 20.0}, V{0.0, 0.0});
            check(pl->quiver_data(0).u.back() == 20.0 && a3->xlim().hi > 21.0,
                  "plane: set_quiver_data(), and the arrows' tips widen the parent's limits");
            pl->cla();
            check(pl->quiver_count() == 0, "plane: cla() clears quivers");
        }
    }

    // --- Arrows, outline, triangles ----------------------------------------------
    static void quiver_geometry_checks() {
        // Pivots: the vector times the amp, put down at (x, y) by its tail,
        // middle or tip.
        {
            QuiverPlot p = quiver_of(V{1.0}, V{2.0}, V{3.0}, V{-4.0}, kRed, 1, 2.0);
            auto a = arrows(p);
            check(a.size() == 1 && a[0].tx == 1.0 && a[0].ty == 2.0 && a[0].hx == 7.0 && a[0].hy == -6.0,
                  "pivot: Tail, from (x, y) to (x + s*u, y + s*v)");
            p.opts.pivot = QuiverPivot::Middle;
            a = arrows(p);
            check(a[0].tx == -2.0 && a[0].ty == 6.0 && a[0].hx == 4.0 && a[0].hy == -2.0, "pivot: Middle, centred on (x, y)");
            p.opts.pivot = QuiverPivot::Tip;
            a = arrows(p);
            check(a[0].hx == 1.0 && a[0].hy == 2.0 && a[0].tx == -5.0 && a[0].ty == 10.0, "pivot: Tip, ending at (x, y)");
        }
        // Left out: a NaN anywhere, a zero vector, an end <= 0 on a log axis.
        {
            const QuiverPlot p = quiver_of(V{0, 1, 2, NAN, 4, 5}, V{0, 0, 0, 0, NAN, 1}, V{1, 0, 1, 1, 1, NAN},
                                           V{0, 0, INFINITY, 0, 0, 0}, kRed, 1);
            const auto a = arrows(p);
            check(a.size() == 1 && a[0].i == 0, "left out: a zero vector, a NaN or infinite value in any column");
            const AxisScale lg = make_axis_scale(Scale::Log, {});
            const QuiverPlot l = quiver_of(V{1, 10, 1}, V{1, 1, 1}, V{-2, 10, 1}, V{0, 0, 0}, kRed, 1);
            const auto la = arrows(l, lg, AxisScale{});
            check(la.size() == 2 && la[0].i == 1 && near(la[0].tx, 1.0, 1e-12) && near(la[0].hx, std::log10(20.0), 1e-12),
                  "log: an arrow with an end <= 0 is left out; the others are placed on the scale");
        }
        // Outline: 7 points (shaft, head), or 3 for an arrow shorter than its head.
        {
            const QuiverOptions o{.width = 2.0f, .head_width = 8.0f, .head_length = 10.0f};
            float pts[14];
            const int n = arrow_outline(0.0f, 0.0f, 30.0f, 0.0f, o, pts);
            check(n == 7 && pts[0] == 0.0f && std::abs(pts[1]) == 1.0f && pts[2] == 20.0f && pts[4] == 20.0f &&
                      std::abs(pts[5]) == 4.0f && pts[6] == 30.0f && pts[7] == 0.0f && pts[12] == 0.0f &&
                      pts[13] == -pts[1],
                  "outline: shaft corners at the tail and the head's base, head corners, the tip");
            check(near(outline_area(n, pts), 20.0 * 2.0 + 0.5 * 10.0 * 8.0, 1e-4),
                  "outline: the triangles cover shaft + head once (" + std::to_string(outline_area(n, pts)) + ")");
            const int s = arrow_outline(0.0f, 0.0f, 0.0f, 5.0f, o, pts);
            check(s == 3 && pts[2] == 0.0f && pts[3] == 5.0f && pts[1] == 0.0f && std::abs(pts[0]) == 2.0f &&
                      near(outline_area(s, pts), 0.5 * 5.0 * 4.0, 1e-4),
                  "outline: a 5 px arrow is its head alone, scaled to half (base 4 px)");
            check(arrow_outline(1.0f, 1.0f, 1.0f, 1.0f, o, pts) == 0, "outline: a zero-length arrow has none");
        }
    }

    // --- Limits ------------------------------------------------------------------
    static void quiver_limit_checks() {
        // Tails and tips, padded; arrows left out count for nothing.
        {
            RenderSnapshot s;
            s.quivers.push_back(quiver_of(V{0, 10, 50}, V{0, 0, 50}, V{2, 0, NAN}, V{4, -10, 0}, kRed, 1, 1.0));
            // Arrows: (0,0)->(2,4); (10,0)->(10,-10); the third left out.
            check(near(xlim_of(s).lo, -0.5, 1e-9) && near(xlim_of(s).hi, 10.5, 1e-9) &&
                      near(ylim_of(s).lo, -10.7, 1e-9) && near(ylim_of(s).hi, 4.7, 1e-9),
                  "limits: both ends of every arrow, padded, a left-out arrow not counted (" +
                      range_str(xlim_of(s)) + ", " + range_str(ylim_of(s)) + ")");
            QuiverOptions o = s.quivers[0].opts;
            o.pivot = QuiverPivot::Tip;
            std::vector<PlotStyleEdit> inv;
            apply_plot_style_edits(s, {PlotStyleEdit{0, -1, o}}, &inv);
            check(near(ylim_of(s).hi, 10.7, 1e-9), "limits: follow a pivot edit (" + range_str(ylim_of(s)) + ")");
            apply_plot_style_edits(s, inv);
            check(near(ylim_of(s).hi, 4.7, 1e-9), "limits: and its undo");
        }
        // Log x: an arrow reaching to or below 0 counts for nothing.
        {
            auto f = Figure::create();
            auto a = f->add_subplot(1, 1, 1);
            a->quiver(V{1.0, 10.0}, V{0.0, 0.0}, V{-5.0, 90.0}, V{0.0, 0.0}, {.amp = 1.0});
            a->set_xscale(Scale::Log);
            check(near(a->xlim().lo, std::pow(10.0, 0.95), 1e-6) && near(a->xlim().hi, std::pow(10.0, 2.05), 1e-6),
                  "limits: on log x, only the arrow placed there counts (" + range_str(a->xlim()) + ")");
        }
        // A twin: its own y, the shared x through the group.
        {
            auto tf = Figure::create();
            auto host = tf->add_subplot(1, 1, 1);
            host->line(V{0, 1}, V{0, 1});
            auto twin = host->twinx();
            twin->quiver(V{0.0}, V{0.0}, V{30.0}, V{70.0}, {.amp = 1.0});
            check(twin->ylim().hi > 70.0 && host->ylim().hi < 2.0 && host->xlim().hi > 30.0,
                  "limits: a twin's quiver widens its own y and the shared x");
        }
    }

    // --- Panel, edits, journal, legend, hover -----------------------------------
    static void quiver_panel_checks() {
        RenderSnapshot s;
        s.quivers.push_back(quiver_of(V{0, 1, 2}, V{0, 0, 0}, V{1, 1, 1}, V{0, 1, 2}, kRed, 1, 0.5));
        s.quivers[0].data_stamp = 1;
        s.quivers[0].opts.hint_labels = {"a", "b", "c"};
        {
            const auto t = collect_plot_data_tables(s);
            std::string names;
            if (t.size() == 1)
                for (const auto& col: t[0].columns) names += std::string(col.name) + " ";
            check(t.size() == 1 && t[0].kind == PlotKind::Quiver && names == "x y u v " && !t[0].rows_fixed &&
                      t[0].label == "quiver 0",
                  "panel: a table of x, y, u, v (" + names + ")");
        }
        {
            RenderSnapshot c = s;
            std::vector<PlotDataOp> undo;
            apply_plot_data_ops(c, {PlotCellEdit{PlotKind::Quiver, 0, 2, 1, 9.0},
                                    PlotCellEdit{PlotKind::Quiver, 0, 3, 2, -4.0}}, &undo);
            check(c.quivers[0].u[1] == 9.0 && c.quivers[0].v[2] == -4.0, "cells: a u cell, a v cell");
            apply_plot_data_ops(c, undo);
            check(c.quivers[0].u[1] == 1.0 && c.quivers[0].v[2] == 2.0, "cells: undone");
            std::vector<PlotDataOp> rows_undo;
            apply_plot_data_ops(c, {PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Quiver, 0, 3},
                                    PlotRowEdit{PlotRowEdit::Op::Remove, PlotKind::Quiver, 0, 0}}, &rows_undo);
            check(c.quivers[0].x.size() == 3 && c.quivers[0].v.size() == 3 && c.quivers[0].x[0] == 1.0 &&
                      c.quivers[0].v[2] == 2.0 && c.quivers[0].opts.hint_labels.size() == 3,
                  "rows: an insert copies its neighbour; a remove takes every column and its label");
            apply_plot_data_ops(c, rows_undo);
            check(c.quivers[0].x.size() == 3 && c.quivers[0].x[0] == 0.0 && c.quivers[0].v[0] == 0.0 &&
                      c.quivers[0].opts.hint_labels[0] == "a",
                  "rows: undone");
        }
        {
            RenderSnapshot o = s;
            QuiverOptions qo = o.quivers[0].opts;
            qo.color.reset();
            qo.amp.reset();
            qo.hint_labels.clear();
            qo.width = 5.0f;
            std::vector<PlotStyleEdit> inv;
            apply_plot_style_edits(o, {PlotStyleEdit{0, -1, qo}}, &inv);
            check(o.quivers[0].opts.width == 5.0f && o.quivers[0].opts.color->r == kRed.r &&
                      o.quivers[0].amp() == 0.5 && o.quivers[0].opts.hint_labels.size() == 3,
                  "style: an edit applies; an unset color or amp keeps the quiver's, the labels stay");
            qo.amp = 3.0;
            apply_plot_style_edits(o, {PlotStyleEdit{0, -1, qo}});
            check(o.quivers[0].amp() == 3.0, "style: an amp edit");
            apply_plot_style_edits(o, inv);
            check(o.quivers[0].opts.width == 2.0f, "style: undone");
        }
        // Legend: an arrow at the quiver's color and opacity.
        {
            RenderSnapshot l = s;
            l.quivers[0].opts.name = "wind";
            l.quivers[0].opts.alpha = 0.4f;
            const auto e = collect_legend_entries(l);
            check(e.size() == 1 && e[0].kind == LegendKind::Arrow && e[0].color.r == kRed.r &&
                      near(e[0].color.a, 0.4, 1e-6) && e[0].edge.a == 0.0f,
                  "legend: an arrow key at the quiver's color and opacity");
            float pts[14];
            check(legend_arrow_outline(0.0f, kLegendSwatchW, 5.0f, pts) == 7 && pts[6] == kLegendSwatchW,
                  "legend: the key is a whole arrow across the swatch");
        }
        // Hover and pick: at the arrow's (x, y), with u and v and its label.
        {
            const CoordTransform tr{-1.0, 3.0, -1.0, 3.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
            const auto h = find_hint(s, tr, tr.to_px(1.0) + 2.0f, tr.to_py(0.0));
            check(h && h->text == "x=1, y=0, u=1, v=1\nb" && h->anchor_x == tr.to_px(1.0),
                  "hover: at the anchor, x, y, u and v and the label (" + (h ? h->text : std::string("none")) + ")");
            const auto p = find_pick(s, tr, tr.to_px(2.0), tr.to_py(0.0));
            check(p && p->kind == PickKind::Quiver && p->object == 0 && p->element == 2,
                  "pick: PickKind::Quiver, the arrow's index");
            check(!find_hint(s, tr, tr.to_px(2.0), tr.to_py(1.5)), "hover: not along the arrow, only at its anchor");
        }
        // The journal: panel edits reach the snapshot, replay onto the live
        // axes, and undo.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->quiver(V{0, 1}, V{0, 0}, V{1, 1}, V{0, 0}, {.amp = 1.0});
            auto& fi = detail::FigureAccess::impl(*fig);
            AxesEdit ae;
            ae.plot_ops.push_back(PlotCellEdit{PlotKind::Quiver, 0, 2, 1, 7.0});
            ae.plot_styles.push_back(PlotStyleEdit{0, -1, QuiverOptions{.color = Color::Red, .amp = 2.0}});
            FigureEdits e;
            e.per_axes.push_back({1, ae});
            const FigureEdits undo = fi.apply_edits(e);
            const auto snap = fi.snapshot_box.load();
            const RenderSnapshot& r = *snap->axes[0].snap2d();
            check(r.quivers[0].u[1] == 7.0 && r.quivers[0].amp() == 2.0, "journal: the edits reach the snapshot");
            fi.apply_edits_and_publish();
            check(ax->quiver_data(0).u[1] == 7.0 && ax->xlim().hi > 15.0,
                  "journal: and replay onto the live axes, the longer arrow widening x");
            fi.apply_edits(undo);
            const auto u = fi.snapshot_box.load();
            check(u->axes[0].snap2d()->quivers[0].u[1] == 1.0 && u->axes[0].snap2d()->quivers[0].amp() == 1.0,
                  "journal: undo restores the cell and the amp");
        }
        // The Data panel draws a quiver's tab with no duplicate widget ids.
        {
            FigureSnapshot fs;
            RenderSnapshot r;
            r.quivers.push_back(quiver_of(V{0, 1}, V{1, 2}, V{1, 1}, V{0, 0}, kGreen, 5));
            fs.axes.push_back({{1, 1, 1}, std::move(r)});
            fs.generation = fs.data_generation = 1;
            const IdConflictScan sc = scan_panel_for_id_conflicts(fs, &draw_data_window, "Data", {});
            check(sc.conflicts == 0 && sc.probes > 0, "panel: a quiver's tab draws without id conflicts");
        }
        // A plane: each arrow's triangles in the plane.
        {
            PlaneSnapshot p;
            p.orient = PlaneOrientation::XY;
            p.sheet.quivers.push_back(quiver_of(V{0, 0}, V{0, 1}, V{2, 2}, V{0, 0}, kGreen, 1));
            const PlaneGeometry g = plane_geometry(p);
            bool inside = true;
            for (const auto& t : g.tris)
                for (const Vec3& q : t.p) inside = inside && q.x >= -1e-9 && q.x <= 2.0 + 1e-9 && q.z == 0.0;
            check(g.tris.size() == 6 && inside && plane_has_data(p),
                  "plane: three triangles per arrow, between its tail and tip");
        }
    }

    // --- Colored arrows (step 36e-2): c, the map, colorbar, legend, panel ------
    static void quiver_color_checks() {
        const V x{0.0, 1.0, 2.0}, y{0.0, 0.0, 0.0}, u{1.0, 1.0, 1.0}, v{0.0, 0.0, 0.0}, c{0.0, 0.5, 1.0};
        // The call: c stored and read back; a c of another length throws.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->quiver(x, y, u, v, c, {.amp = 0.5});
            check(ax->quiver_count() == 1 && ax->quiver_data(0).c == c, "c: stored and read back");
            check(throws_invalid([&] { ax->quiver(x, y, u, v, V{1.0, 2.0}); }) &&
                      throws_invalid([&] { ax->quiver(x, y, u, v, V{}); }) && ax->quiver_count() == 1,
                  "c: one per arrow, else it throws and nothing is added");
            // The cycle: a colormapped quiver keeps a color without using one up.
            ax->line(x, y);
            const FigureSnapshot fs = detail::FigureAccess::impl(*fig).build_figure_snapshot();
            const RenderSnapshot& s = *fs.axes[0].snap2d();
            const Color& q = *s.quivers[0].opts.color;
            const Color& l = *s.lines[0].opts.color;
            check(q.r == Color::Blue.r && q.b == Color::Blue.b && l.r == Color::Blue.r && l.b == Color::Blue.b,
                  "c: the quiver holds the next cycle color without advancing the cycle");
            // set_quiver_data: QuiverData's c as given (empty = flat); spans keep it
            // while the count holds.
            ax->set_quiver_data(0, x, y, u, v);
            check(ax->quiver_data(0).c == c, "set_quiver_data(spans): keeps c at the same count");
            ax->set_quiver_data(0, V{0.0}, V{0.0}, V{1.0}, V{0.0});
            check(ax->quiver_data(0).c.empty(), "set_quiver_data(spans): drops it at another count");
            ax->set_quiver_data(0, QuiverData{x, y, u, v, V{3.0, 2.0, 1.0}});
            check(ax->quiver_data(0).c == V({3.0, 2.0, 1.0}), "set_quiver_data(QuiverData): takes c as given");
            check(throws_invalid([&] { ax->set_quiver_data(0, QuiverData{x, y, u, v, V{1.0}}); }) &&
                      ax->quiver_data(0).c.size() == 3,
                  "set_quiver_data: a c of another length throws and changes nothing");
            ax->set_quiver_data(0, QuiverData{x, y, u, v, {}});
            check(ax->quiver_data(0).c.empty(), "set_quiver_data(QuiverData): an empty c makes it flat");
            // Planes.
            auto f3 = Figure::create();
            auto a3 = f3->add_subplot3d(1, 1, 1);
            auto pl = a3->plane(PlaneOrientation::XY, 0.0);
            pl->quiver(x, y, u, v, c);
            check(pl->quiver_count() == 1 && pl->quiver_data(0).c == c, "plane: quiver() with c");
        }
        // The map: each arrow's c through cmap/vmin/vmax; a cyclic map wraps; a
        // non-finite c leaves the arrow out (and out of the limits).
        {
            QuiverPlot p = quiver_of(V{0, 1, 2, 3}, V{0, 0, 0, 0}, V{1, 1, 1, 1}, V{0, 0, 0, 0}, kRed, 1);
            p.c = V{0.0, 1.0, 0.5, NAN};
            const uint8_t* lut = colormaps::get(Colormap::Viridis);
            auto same = [](const Color& a, const uint8_t* b) {
                return std::abs(a.r * 255.0f - b[0]) < 0.5f && std::abs(a.g * 255.0f - b[1]) < 0.5f &&
                       std::abs(a.b * 255.0f - b[2]) < 0.5f;
            };
            check(same(quiver_arrow_color(p, 0), lut) && same(quiver_arrow_color(p, 1), lut + 255 * 4) &&
                      same(quiver_arrow_color(p, 2), lut + colormaps::index(0.5, false) * 4),
                  "map: vmin, vmax and the middle through the colormap");
            const auto a = arrows(p);
            check(a.size() == 3 && a.back().i == 2, "map: an arrow whose c is NaN is left out");
            RenderSnapshot s;
            s.quivers.push_back(p);
            check(near(xlim_of(s).hi, 3.15, 1e-9), "limits: the left-out arrow counts for nothing (" +
                                                      range_str(xlim_of(s)) + ")");
            p.opts.cmap = Colormap::Twilight;
            p.c = V{0.25, 1.25};
            check(quiver_arrow_color(p, 0).r == quiver_arrow_color(p, 1).r &&
                      quiver_arrow_color(p, 0).g == quiver_arrow_color(p, 1).g,
                  "map: a cyclic map wraps (vmax + d draws as vmin + d)");
            p.c = CowVec<double>{};
            check(quiver_arrow_color(p, 0).r == kRed.r, "map: a flat quiver is its color");
        }
        // Colorbar, legend, hover.
        {
            RenderSnapshot s;
            QuiverPlot mapped = quiver_of(V{0, 1}, V{0, 0}, V{1, 1}, V{0, 0}, kRed, 1);
            mapped.c = V{0.0, 2.0};
            mapped.opts.colorbar = true;
            mapped.opts.cmap = Colormap::Plasma;
            mapped.opts.vmax = 2.0f;
            mapped.opts.name = "speed";
            QuiverPlot flat = quiver_of(V{0, 1}, V{1, 1}, V{1, 1}, V{0, 0}, kGreen, 2);
            flat.opts.colorbar = true;
            flat.opts.name = "flat";
            s.quivers.push_back(mapped);
            s.quivers.push_back(flat);
            const auto cb = find_colorbar_requests(s);
            check(cb.size() == 1 && cb[0].cmap == Colormap::Plasma && cb[0].vmax == 2.0f && cb[0].name == "speed",
                  "colorbar: a colormapped quiver's, none for a flat one even with colorbar set");
            const auto e = collect_legend_entries(s);
            check(e.size() == 2 && e[0].kind == LegendKind::Arrow && e[0].color.r == 1.0f && e[0].color.b == 1.0f &&
                      e[0].edge.a == 1.0f && e[0].edge.r == 0.0f && e[1].color.g == kGreen.g && e[1].edge.a == 0.0f,
                  "legend: a colormapped key is white with a black edge, a flat one its color");
            const CoordTransform tr{-1.0, 3.0, -1.0, 3.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
            const auto h = find_hint(s, tr, tr.to_px(1.0), tr.to_py(0.0));
            check(h && h->text == "x=1, y=0, u=1, v=0, c=2", "hover: adds c (" + (h ? h->text : std::string("none")) + ")");
        }
        // Panel: a c column; cell and row edits on it, undone; a map edit; no id conflicts.
        {
            RenderSnapshot s;
            QuiverPlot q = quiver_of(V{0, 1}, V{0, 0}, V{1, 1}, V{0, 0}, kRed, 1);
            q.c = V{0.25, 0.75};
            q.data_stamp = 1;
            s.quivers.push_back(q);
            const auto t = collect_plot_data_tables(s);
            check(t.size() == 1 && t[0].columns.size() == 5 && std::string(t[0].columns[4].name) == "c",
                  "panel: a colormapped quiver's table ends with c");
            std::vector<PlotDataOp> undo;
            apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Quiver, 0, 4, 1, 9.0}}, &undo);
            check(s.quivers[0].c[1] == 9.0, "cells: a c cell");
            apply_plot_data_ops(s, undo);
            check(s.quivers[0].c[1] == 0.75, "cells: undone");
            std::vector<PlotDataOp> rows_undo;
            apply_plot_data_ops(s, {PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Quiver, 0, 2}}, &rows_undo);
            check(s.quivers[0].c.size() == 3 && s.quivers[0].c[2] == 0.75, "rows: an insert copies its neighbour's c");
            apply_plot_data_ops(s, rows_undo);
            check(s.quivers[0].c.size() == 2, "rows: undone");
            QuiverOptions o = s.quivers[0].opts;
            o.cmap = Colormap::Coolwarm;
            o.vmax = 4.0f;
            o.colorbar = true;
            std::vector<PlotStyleEdit> inv;
            apply_plot_style_edits(s, {PlotStyleEdit{0, -1, o}}, &inv);
            check(s.quivers[0].opts.cmap == Colormap::Coolwarm && s.quivers[0].opts.vmax == 4.0f &&
                      s.quivers[0].colormapped(),
                  "style: a map edit");
            apply_plot_style_edits(s, inv);
            check(s.quivers[0].opts.cmap == Colormap::Viridis && !s.quivers[0].opts.colorbar, "style: undone");

            FigureSnapshot fs;
            fs.axes.push_back({{1, 1, 1}, std::move(s)});
            fs.generation = fs.data_generation = 1;
            const IdConflictScan sc = scan_panel_for_id_conflicts(fs, &draw_data_window, "Data", {});
            check(sc.conflicts == 0 && sc.probes > 0, "panel: a colormapped quiver's tab draws without id conflicts");
        }
        // A plane: each arrow's triangles in its own color.
        {
            PlaneSnapshot p;
            p.orient = PlaneOrientation::XY;
            QuiverPlot q = quiver_of(V{0, 0}, V{0, 1}, V{2, 2}, V{0, 0}, kGreen, 1);
            q.c = V{0.0, 1.0};
            p.sheet.quivers.push_back(q);
            const PlaneGeometry g = plane_geometry(p);
            check(g.tris.size() == 6 && g.tris[0].fill.r == g.tris[2].fill.r && g.tris[0].fill.r != g.tris[3].fill.r,
                  "plane: each arrow in its own mapped color");
        }
    }

    // -------------------------------------------------------------------------
    // Rendered: GL against SVG, pivot, a short arrow, translucency, call order
    // -------------------------------------------------------------------------
    void test_quiver_rendered() {
        std::printf("\n[2D quiver, rendered]\n");

        const int W = 400, H = 320;
        // Over 0..10 x 0..10, in call order: a red arrow from (1, 2) along +x
        // (6 units, shaft 6 px, head 20 x 16 px) and a red arrow at (2, 5)
        // shorter than its head; a translucent blue arrow centred on (5, 7)
        // running up 4 units; a black line at y = 7, drawn over it.
        FigureSnapshot fs;
        {
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            RenderSnapshot& s = *fa.snap2d();
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            QuiverPlot red = quiver_of(V{1.0, 2.0}, V{2.0, 5.0}, V{6.0, 0.5}, V{0.0, 0.0}, kRed, 1);
            red.opts.width = 6.0f;
            red.opts.head_width = 16.0f;
            red.opts.head_length = 20.0f;
            s.quivers.push_back(red);
            QuiverPlot blue = quiver_of(V{5.0}, V{7.0}, V{0.0}, V{2.0}, kBlue, 2, 2.0);
            blue.opts.pivot = QuiverPivot::Middle;
            blue.opts.alpha = 0.5f;
            blue.opts.width = 8.0f;
            s.quivers.push_back(blue);
            LinePlot lp;
            lp.x = V{0.0, 10.0};
            lp.y = V{7.0, 7.0};
            lp.opts.color = Color::Black;
            lp.opts.linewidth = 3.0f;
            lp.id = 3;
            s.lines.push_back(lp);
            fs.axes.push_back(std::move(fa));
            fs.generation = fs.data_generation = 1;
        }
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            export_figure_png(dev, data, fs, "quiver.png", W, H, 1);
            export_figure_svg(fs, "quiver.svg", W, H);
        }
        const CellLayout cell = compute_figure_layout(fs, W, H).cells[0];
        const CoordTransform& tr = cell.tr;
        const std::string svg = read_file("quiver.svg");
        const Png png("quiver.png");

        // SVG: one polygon per arrow, the fill on the group; the short arrow a
        // head alone.
        const std::size_t red_g = svg.find("<g fill=\"rgb(204,0,0)\">");
        const std::size_t blue_g = svg.find("<g fill=\"rgb(0,0,204)\" fill-opacity=\"0.5\">");
        const auto polys = elements(svg, "polygon", "points=");
        auto points_in = [](const std::string& el) {
            const std::size_t a = el.find("points=\"") + 8;
            int n = 0;
            for (std::size_t k = a; k < el.size() && el[k] != '"'; ++k) n += el[k] == ',';
            return n;
        };
        check(red_g != std::string::npos && blue_g != std::string::npos && red_g < blue_g &&
                  blue_g < svg.find("<polyline"),
              "svg: a group per quiver carrying its fill (the blue at half opacity), in call order");
        check(polys.size() == 3 && points_in(polys[0]) == 7 && points_in(polys[1]) == 3 && points_in(polys[2]) == 7,
              "svg: a 7-point polygon per arrow, 3 for the one shorter than its head");

        // GL: the red arrow's shaft and head where the outline puts them.
        const float y2 = tr.to_py(2.0), x7 = tr.to_px(7.0);
        check(png.is(tr.to_px(2.0), y2, 204, 0, 0) && png.is(tr.to_px(2.0), y2 + 2.0f, 204, 0, 0) &&
                  !png.is(tr.to_px(2.0), y2 + 5.0f, 204, 0, 0),
              "gl: the shaft, 6 px across");
        check(png.is(x7 - 16.0f, y2 + 5.0f, 204, 0, 0) && png.is(x7 - 3.0f, y2, 204, 0, 0) &&
                  !png.is(x7 - 3.0f, y2 + 5.0f, 204, 0, 0) && !png.is(x7 + 3.0f, y2, 204, 0, 0),
              "gl: the head, wide at its base, narrowing to the tip at x = 7");
        // The short arrow: its head scaled down, no shaft behind it.
        const float sx = tr.to_px(2.0), sy = tr.to_py(5.0), slen = tr.to_px(2.5) - sx;
        check(png.is(sx + slen * 0.3f, sy, 204, 0, 0) && !png.is(sx - 2.0f, sy, 204, 0, 0),
              "gl: an arrow shorter than its head is a small head alone (" + std::to_string(slen) + " px)");
        // The blue arrow, centred on (5, 7): from y = 5 to 9, translucent, under the line.
        const float bx = tr.to_px(5.0);
        const unsigned char* mid = png.at(bx, tr.to_py(6.0));
        check(mid && mid[2] > 150 && mid[0] > 80 && mid[0] < 160 && !png.is(bx, tr.to_py(4.7), 0, 0, 204, 80),
              "gl: the Middle pivot centres the arrow, at half opacity");
        // Within a pixel of the line's centre: the Mac's GPU draws this 3 px
        // line 1 to 3 px high along its length.
        const float ly = tr.to_py(7.0);
        check(png.is(bx, ly - 1.0f, 0, 0, 0) || png.is(bx, ly, 0, 0, 0) || png.is(bx, ly + 1.0f, 0, 0, 0),
              "gl: the line drawn after the arrow shows over it");

        // Colored arrows (step 36e-2), a scene of their own: three thick arrows
        // along +x at y = 2, 5, 8 with c = 0, 0.5, 1 through Viridis, and a fourth
        // at y = 9.5 whose c is NaN; a colorbar.
        FigureSnapshot cfs;
        {
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            RenderSnapshot& s = *fa.snap2d();
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            QuiverPlot q = quiver_of(V{1.0, 1.0, 1.0, 1.0}, V{2.0, 5.0, 8.0, 9.5}, V{8.0, 8.0, 8.0, 8.0},
                                     V{0.0, 0.0, 0.0, 0.0}, kRed, 1);
            q.c = V{0.0, 0.5, 1.0, NAN};
            q.opts.width = 10.0f;
            q.opts.head_width = 20.0f;
            q.opts.head_length = 20.0f;
            q.opts.colorbar = true;
            s.quivers.push_back(q);
            cfs.axes.push_back(std::move(fa));
            cfs.generation = cfs.data_generation = 1;
        }
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            export_figure_png(dev, data, cfs, "quiver_colored.png", W, H, 1);
            export_figure_svg(cfs, "quiver_colored.svg", W, H);
        }
        const CellLayout ccell = compute_figure_layout(cfs, W, H).cells[0];
        const CoordTransform& ct = ccell.tr;
        const std::string csvg = read_file("quiver_colored.svg");
        const Png cpng("quiver_colored.png");
        const uint8_t* lut = colormaps::get(Colormap::Viridis);
        auto lut_at = [&](double t) { return lut + colormaps::index(t, false) * 4; };
        bool gl_ok = true;
        const double ts[3] = {0.0, 0.5, 1.0};
        for (int k = 0; k < 3; ++k) {
            const uint8_t* want = lut_at(ts[k]);
            gl_ok = gl_ok && cpng.is(ct.to_px(4.0), ct.to_py(2.0 + 3.0 * k), want[0], want[1], want[2], 6);
        }
        check(gl_ok, "gl: each arrow in its c's color through the map");
        check(!cpng.is(ct.to_px(4.0), ct.to_py(9.5), lut[0], lut[1], lut[2], 60) &&
                  cpng.is(ct.to_px(4.0), ct.to_py(9.5), 255, 255, 255, 6),
              "gl: the arrow whose c is NaN is not drawn");
        const auto cpolys = elements(csvg, "polygon", "points=");
        auto fill_of_poly = [](const std::string& el) {
            const std::size_t a = el.find("fill=\"");
            return a == std::string::npos ? std::string() : el.substr(a + 6, el.find('"', a + 6) - a - 6);
        };
        auto rgb_of = [](const uint8_t* c) {
            return "rgb(" + std::to_string(c[0]) + "," + std::to_string(c[1]) + "," + std::to_string(c[2]) + ")";
        };
        check(cpolys.size() == 3 && fill_of_poly(cpolys[0]) == rgb_of(lut_at(0.0)) &&
                  fill_of_poly(cpolys[1]) == rgb_of(lut_at(0.5)) && fill_of_poly(cpolys[2]) == rgb_of(lut_at(1.0)) &&
                  csvg.find("<g>\n      <polygon fill=") != std::string::npos,
              "svg: a polygon per drawn arrow, each with its own fill, the group with none (" +
                  (cpolys.empty() ? std::string("none") : fill_of_poly(cpolys[0])) + ")");
        check(compute_cell_decorations(cfs.axes[0]).colorbars.size() == 1,
              "colorbar: the colormapped quiver's colorbar is laid out");
    }
} // namespace lt
