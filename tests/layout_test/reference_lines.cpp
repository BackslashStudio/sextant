// Reference lines and spans: Axes::axline() and axspan(). The
// call, read-back, set_*_data() and cla(); automatic limits (the data side
// only, both orientations, an orientation edit, a twin, a share group, log);
// the pixel geometry (frame fractions, log, clipping); the Data panel, its
// cell and style edits with undo, and the journal's replay; legend keys; no
// hover; GL against SVG, draw order and clipping.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "axis_limits.h"
#include "figure_impl.h"
#include "plot_data_view.h"
#include "reference_lines.h"
#include <cmath>

namespace lt {
    using namespace sextant;

    namespace {
        using AxesImpl = detail::FigureAccess::Impl::AxesImpl;

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
        bool same_range(Range a, Range b) { return near(a.lo, b.lo, 1e-9) && near(a.hi, b.hi, 1e-9); }

        std::string range_str(Range r) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g..%g", r.lo, r.hi);
            return buf;
        }

        std::string read_file(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        float attr(const std::string& el, const char* name) {
            const std::string key = std::string(" ") + name + "=\"";
            const std::size_t p = el.find(key);
            return p == std::string::npos ? NAN : std::stof(el.substr(p + key.size()));
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
            // Within 40 levels of (r, g, b).
            bool is(float x, float y, int r, int g, int b) const {
                const unsigned char* p = at(x, y);
                return p && std::abs(p[0] - r) < 40 && std::abs(p[1] - g) < 40 && std::abs(p[2] - b) < 40;
            }
        };

        constexpr Color kRed{0.8f, 0.0f, 0.0f, 1.0f};
        constexpr Color kGreen{0.0f, 0.6f, 0.0f, 1.0f};
        constexpr Color kBlue{0.0f, 0.0f, 0.8f, 1.0f};
        constexpr Color kOrange{0.8f, 0.6f, 0.0f, 1.0f};

        AxLinePlot axline_of(double v, Orientation o, Color c, ObjectId id) {
            AxLinePlot p;
            p.value = v;
            p.opts.orientation = o;
            p.opts.color = c;
            p.id = id;
            return p;
        }

        AxSpanPlot axspan_of(double lo, double hi, Orientation o, Color c, ObjectId id) {
            AxSpanPlot p;
            p.lo = lo;
            p.hi = hi;
            p.opts.orientation = o;
            p.opts.color = c;
            p.opts.alpha = 1.0f;
            p.id = id;
            return p;
        }

        // The limits layout resolves for one snapshot.
        Range xlim_of(const RenderSnapshot& s) {
            const ResolvedLimits l = resolve_limits(s);
            return {l.xmin, l.xmax};
        }
        Range ylim_of(const RenderSnapshot& s) {
            const ResolvedLimits l = resolve_limits(s);
            return {l.ymin, l.ymax};
        }
    } // namespace

    static void reference_api_checks();
    static void reference_limit_checks();
    static void reference_geometry_checks();
    static void reference_panel_checks();

    void test_reference_lines() {
        std::printf("\n[2D reference lines and spans]\n");
        try {
            reference_api_checks();
            reference_limit_checks();
            reference_geometry_checks();
            reference_panel_checks();
        } catch (const std::exception& e) {
            check(false, std::string("reference lines: unexpected exception: ") + e.what());
        }
    }

    // --- The call, read-back, set_*_data(), cla(), objects by id --------------
    static void reference_api_checks() {
        auto fig = Figure::create();
        auto ax = fig->add_subplot(1, 1, 1);
        ax->axline(2.0);
        ax->axline(3.0, {.orientation = Orientation::Vertical});
        ax->axspan(5.0, 1.0);
        check(ax->axline_count() == 2 && ax->axline_data(0).value == 2.0 && ax->axline_data(1).value == 3.0,
              "call: axline() stores its value");
        check(ax->axspan_count() == 1 && ax->axspan_data(0).lo == 1.0 && ax->axspan_data(0).hi == 5.0,
              "call: axspan() stores lo > hi swapped");

        const double nan = std::nan(""), inf = HUGE_VAL;
        check(throws_invalid([&] { ax->axline(nan); }) && throws_invalid([&] { ax->axline(inf); }) &&
                  throws_invalid([&] { ax->axline(1.0, {.from = nan}); }) &&
                  throws_invalid([&] { ax->axspan(0.0, inf); }) &&
                  throws_invalid([&] { ax->axspan(0.0, 1.0, {.to = inf}); }) &&
                  ax->axline_count() == 2 && ax->axspan_count() == 1,
              "call: a non-finite value, end, from or to throws, and nothing is added");

        ax->set_axline_data(0, {4.5});
        ax->set_axspan_data(0, {9.0, 7.0});
        check(ax->axline_data(0).value == 4.5 && ax->axspan_data(0).lo == 7.0 && ax->axspan_data(0).hi == 9.0,
              "set_*_data: moves the line, and the span with its ends ordered");
        check(throws_invalid([&] { ax->set_axline_data(0, {nan}); }) && ax->axline_data(0).value == 4.5 &&
                  throws_invalid([&] { ax->set_axspan_data(0, {nan, 1.0}); }) && ax->axspan_data(0).lo == 7.0,
              "set_*_data: a non-finite value throws and changes nothing");
        check(throws_range([&] { (void)ax->axline_data(2); }) && throws_range([&] { ax->set_axspan_data(1, {}); }),
              "read-back: an index past the count throws out_of_range");

        // The color: an axline takes the cycle as a line does; a span takes none.
        auto& fi = detail::FigureAccess::impl(*fig);
        auto cyc = Figure::create();
        auto c1 = cyc->add_subplot(1, 1, 1);
        c1->axline(1.0).line(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0});
        c1->axspan(0.0, 1.0).line(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0});
        const FigureSnapshot cs = detail::FigureAccess::impl(*cyc).build_figure_snapshot();
        const RenderSnapshot& c = *cs.axes[0].snap2d();
        const auto same_color = [](const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        };
        check(same_color(*c.axlines[0].opts.color, Color::Blue) && !same_color(*c.lines[0].opts.color, Color::Blue)
                  && same_color(*c.axspans[0].opts.color, Color::Blue)
                  && !same_color(*c.lines[1].opts.color, *c.lines[0].opts.color),
              "color: axline() takes the next cycle color, axspan() Color::Blue and no cycle color");

        // Removal and restore by id, as other kinds.
        AxesImpl* a = fi.find_slot_impl(1);
        const ObjectId sid = a->axspans[0].id;
        const auto ref = a->find_object(sid);
        auto removed = a->remove_object(sid);
        check(ref && ref->kind == PlotKind::AxSpan && removed && a->axspans.empty(),
              "objects: a span is found and removed by id");
        check(removed && a->restore_object(std::move(*removed)) && a->axspans.size() == 1 && a->axspans[0].id == sid,
              "objects: and restored with its id");

        ax->cla();
        check(ax->axline_count() == 0 && ax->axspan_count() == 0, "cla(): clears lines and spans");
    }

    // --- Limits ------------------------------------------------------------------
    static void reference_limit_checks() {
        const std::vector<double> x{0.0, 10.0}, y{0.0, 10.0};

        // Alone: a value is a point on its axis, the other axis has nothing.
        {
            RenderSnapshot none;
            RenderSnapshot s;
            s.axlines.push_back(axline_of(3.0, Orientation::Horizontal, kRed, 1));
            const Range ys = ylim_of(s);
            check(ys.lo < 3.0 && ys.hi > 3.0 && near(ys.hi - ys.lo, 1.1, 1e-9) && same_range(xlim_of(s), xlim_of(none)),
                  "limits: a lone horizontal line is a point on y, x as an empty axes (" + range_str(ys) + ")");
        }

        auto fig = Figure::create();
        auto ref = fig->add_subplot(1, 2, 1);
        ref->line(x, y);
        auto ax = fig->add_subplot(1, 2, 2);
        ax->line(x, y);
        ax->axline(20.0);
        ax->axspan(-5.0, 2.0, {.orientation = Orientation::Vertical});
        check(same_range(ax->ylim(), {-1.0, 21.0}) && same_range(ax->xlim(), {-5.75, 10.75}),
              "limits: a horizontal line widens y, a vertical span x, padded (" + range_str(ax->xlim()) + ", " +
                  range_str(ax->ylim()) + ")");
        ax->axline(5.0, {.from = -3.0, .to = 9.0});
        ax->axspan(1.0, 2.0, {.from = 0.0, .to = 50.0});
        check(same_range(ax->ylim(), {-1.0, 21.0}) && same_range(ax->xlim(), {-5.75, 10.75}),
              "limits: the fraction side never counts");

        // An orientation edit moves which axis the value counts on.
        {
            RenderSnapshot s;
            LinePlot lp;
            lp.x = x;
            lp.y = y;
            s.lines.push_back(lp);
            s.axlines.push_back(axline_of(30.0, Orientation::Horizontal, kRed, 2));
            check(ylim_of(s).hi > 30.0 && xlim_of(s).hi < 11.0, "limits: y while horizontal");
            AxLineOptions o = s.axlines[0].opts;
            o.orientation = Orientation::Vertical;
            std::vector<PlotStyleEdit> inv;
            apply_plot_style_edits(s, {PlotStyleEdit{0, -1, o}}, &inv);
            check(xlim_of(s).hi > 30.0 && ylim_of(s).hi < 11.0, "limits: x after an orientation edit");
            apply_plot_style_edits(s, inv);
            check(ylim_of(s).hi > 30.0 && !s.axlines[0].on_x(), "limits: and back with its undo");
        }

        // A twin: its own y, the shared x through the group.
        {
            auto tf = Figure::create();
            auto host = tf->add_subplot(1, 1, 1);
            host->line(x, y);
            auto twin = host->twinx();
            twin->line(x, y);
            twin->axline(50.0);
            twin->axline(100.0, {.orientation = Orientation::Vertical});
            check(twin->ylim().hi > 50.0 && host->ylim().hi < 11.0,
                  "limits: a twin's horizontal line widens the twin's y, not the host's");
            check(host->xlim().hi > 100.0 && same_range(host->xlim(), twin->xlim()),
                  "limits: its vertical line widens the shared x, host's and twin's");
        }

        // A share group: a member's line widens every member's shared axis.
        {
            auto sf = Figure::create();
            auto a = sf->add_subplot(2, 1, 1);
            auto b = sf->add_subplot(2, 1, 2);
            a->line(x, y);
            b->line(x, y);
            a->sharex(*b);
            b->axspan(-40.0, -30.0, {.orientation = Orientation::Vertical});
            check(a->xlim().lo < -40.0 && same_range(a->xlim(), b->xlim()) && a->ylim().lo > -1.0,
                  "limits: a span in one member of a sharex group widens the group's x only");
        }

        // Log: only values > 0 count.
        {
            auto lf = Figure::create();
            auto l = lf->add_subplot(1, 1, 1);
            l->line(x, std::vector<double>{1.0, 10.0});
            l->set_yscale(Scale::Log);
            const Range before = l->ylim();
            l->axline(-1.0);
            l->axspan(0.0, 5.0);
            check(same_range(l->ylim(), before), "limits: on log y a value or end <= 0 counts for nothing");
            l->axline(1000.0);
            check(near(l->ylim().hi, std::pow(10.0, 3.15), 1e-6),
                  "limits: and a value > 0 does, padded in decades (" + range_str(l->ylim()) + ")");
        }
    }

    // --- Pixel geometry --------------------------------------------------------
    static void reference_geometry_checks() {
        // A 400 x 200 frame at (50, 20) over 0..10 x 0..10.
        CoordTransform tr{0.0, 10.0, 0.0, 10.0, 50.0f, 20.0f, 400.0f, 200.0f, 500.0f, 300.0f};
        {
            AxLinePlot h = axline_of(5.0, Orientation::Horizontal, kRed, 1);
            h.opts.from = 0.25;
            h.opts.to = 0.75;
            const auto g = axline_segment(h, tr);
            check(g && g->x0 == 150.0f && g->x1 == 350.0f && g->y0 == 120.0f && g->y1 == 120.0f,
                  "geometry: a horizontal line at y, from and to fractions of the frame's width");
            AxLinePlot v = axline_of(2.5, Orientation::Vertical, kRed, 1);
            v.opts.to = 0.5;
            const auto gv = axline_segment(v, tr);
            check(gv && gv->x0 == 150.0f && gv->x1 == 150.0f && gv->y0 == 220.0f && gv->y1 == 120.0f,
                  "geometry: a vertical one at x, from the bottom up");
            const auto off = axline_segment(axline_of(25.0, Orientation::Horizontal, kRed, 1), tr);
            check(off && off->y0 < 20.0f, "geometry: a line out of view still has its place (clipping is the frame's)");
        }
        {
            AxSpanPlot s = axspan_of(2.0, 4.0, Orientation::Vertical, kGreen, 1);
            s.opts.from = 0.1;
            s.opts.to = 0.9;
            const auto r = axspan_rect(s, tr, 2.0f);
            check(r && r->x0 == 130.0f && r->x1 == 210.0f && near(r->y0, 40.0, 1e-4) && near(r->y1, 200.0, 1e-4),
                  "geometry: a vertical span over x, its height the frame fractions");
            const auto big = axspan_rect(axspan_of(-1e30, 1e30, Orientation::Horizontal, kGreen, 1), tr, 3.0f);
            check(big && big->y0 == 17.0f && big->y1 == 223.0f && big->x0 == 50.0f && big->x1 == 450.0f,
                  "geometry: a span past the view is held `reach` outside the frame");
        }
        // Log y: a value <= 0 has no place; a span's end <= 0 lies past the bottom.
        {
            CoordTransform lg = tr;
            lg.ymin = 1.0;
            lg.ymax = 100.0;
            lg.ys = make_axis_scale(Scale::Log, {});
            check(!axline_segment(axline_of(0.0, Orientation::Horizontal, kRed, 1), lg) &&
                      !axline_segment(axline_of(-2.0, Orientation::Horizontal, kRed, 1), lg),
                  "geometry: on log y a line at 0 or below is not drawn");
            const auto g = axline_segment(axline_of(10.0, Orientation::Horizontal, kRed, 1), lg);
            check(g && near(g->y0, 120.0, 1e-3), "geometry: a line at 10 sits mid-frame on 1..100 log");
            const auto r = axspan_rect(axspan_of(-1.0, 10.0, Orientation::Horizontal, kGreen, 1), lg, 2.0f);
            check(r && near(r->y0, 120.0, 1e-3) && r->y1 == 222.0f,
                  "geometry: a log span from -1 runs past the frame's bottom");
            CoordTransform flipped = lg;
            std::swap(flipped.ymin, flipped.ymax);
            const auto rf = axspan_rect(axspan_of(-1.0, 10.0, Orientation::Horizontal, kGreen, 1), flipped, 2.0f);
            check(rf && rf->y0 == 18.0f && near(rf->y1, 120.0, 1e-3),
                  "geometry: on a reversed log axis, past the top instead");
            check(!axspan_rect(axspan_of(-3.0, 0.0, Orientation::Horizontal, kGreen, 1), lg, 2.0f),
                  "geometry: a log span with both ends <= 0 draws nothing");
        }
    }

    // --- Panel, edits, journal, legend, hover -----------------------------------
    static void reference_panel_checks() {
        RenderSnapshot s;
        s.axlines.push_back(axline_of(2.0, Orientation::Horizontal, kRed, 1));
        s.axlines.push_back(axline_of(3.0, Orientation::Vertical, kRed, 2));
        s.axspans.push_back(axspan_of(1.0, 4.0, Orientation::Vertical, kGreen, 3));
        s.axlines[0].data_stamp = s.axlines[1].data_stamp = s.axspans[0].data_stamp = 1;
        {
            const auto t = collect_plot_data_tables(s);
            auto names = [&](std::size_t i) {
                std::string out;
                for (const auto& col: t[i].columns) out += std::string(col.name) + " ";
                return out;
            };
            check(t.size() == 3 && t[0].kind == PlotKind::AxLine && names(0) == "y " && names(1) == "x " &&
                      t[2].kind == PlotKind::AxSpan && names(2) == "xmin xmax " && t[0].rows_fixed &&
                      t[2].rows_fixed && t[2].columns[1].values[0] == 4.0,
                  "panel: one fixed row, columns named for the axis (" + (t.size() == 3 ? names(2) : "") + ")");
        }
        {
            RenderSnapshot c = s;
            std::vector<PlotDataOp> undo;
            apply_plot_data_ops(c, {PlotCellEdit{PlotKind::AxLine, 1, 0, 0, 7.5},
                                    PlotCellEdit{PlotKind::AxSpan, 0, 1, 0, 9.0}}, &undo);
            check(c.axlines[1].value == 7.5 && c.axspans[0].hi == 9.0 && c.axspans[0].lo == 1.0,
                  "cells: a line's value, a span's hi");
            apply_plot_data_ops(c, {PlotCellEdit{PlotKind::AxLine, 0, 1, 0, 1.0},
                                    PlotCellEdit{PlotKind::AxSpan, 0, 0, 1, 1.0}});
            check(c.axlines[0].value == 2.0 && c.axspans[0].lo == 1.0, "cells: no other column or row");
            apply_plot_data_ops(c, undo);
            check(c.axlines[1].value == 3.0 && c.axspans[0].hi == 4.0, "cells: undone");
            const auto rows = apply_plot_data_op(c, PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::AxLine, 0, 1, -1});
            check(!rows, "rows: a reference line has no rows to add");
        }
        {
            RenderSnapshot o = s;
            AxSpanOptions so = o.axspans[0].opts;
            so.color.reset();
            so.alpha = 0.5f;
            std::vector<PlotStyleEdit> inv;
            apply_plot_style_edits(o, {PlotStyleEdit{0, -1, so}}, &inv);
            check(o.axspans[0].opts.alpha == 0.5f && o.axspans[0].opts.color && o.axspans[0].opts.color->g == kGreen.g,
                  "style: a span edit applies, an unset color keeping the span's");
            apply_plot_style_edits(o, inv);
            check(o.axspans[0].opts.alpha == 1.0f, "style: undone");
        }

        // Legend: a line's key, a span's translucent patch, none without a name
        // or for a LineStyle::None line.
        {
            RenderSnapshot l = s;
            l.axlines[0].opts.name = "limit";
            l.axlines[0].opts.linestyle = LineStyle::Dashed;
            l.axlines[1].opts.name = "hidden";
            l.axlines[1].opts.linestyle = LineStyle::None;
            l.axspans[0].opts.name = "band";
            l.axspans[0].opts.alpha = 0.25f;
            const auto e = collect_legend_entries(l);
            check(e.size() == 2 && e[0].name == "limit" && e[0].kind == LegendKind::Line &&
                      e[0].style == LineStyle::Dashed && e[1].name == "band" && e[1].kind == LegendKind::Bar &&
                      near(e[1].color.a, 0.25, 1e-6) && e[1].color.g == kGreen.g,
                  "legend: a line key in its style, a patch at the span's opacity, in call order");
        }

        // No hover on either.
        {
            const CoordTransform tr{0.0, 10.0, 0.0, 10.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
            check(!find_hint(s, tr, tr.to_px(5.0), tr.to_py(2.0)) && !find_hint(s, tr, tr.to_px(2.0), tr.to_py(5.0)),
                  "hover: reference lines and spans are not hovered");
        }

        // The journal: panel edits reach the snapshot, replay onto the live axes,
        // and undo.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->axline(2.0);
            ax->axspan(1.0, 3.0);
            auto& fi = detail::FigureAccess::impl(*fig);
            AxesEdit ae;
            ae.plot_ops.push_back(PlotCellEdit{PlotKind::AxLine, 0, 0, 0, 6.0});
            ae.plot_ops.push_back(PlotCellEdit{PlotKind::AxSpan, 0, 0, 0, -2.0});
            ae.plot_styles.push_back(PlotStyleEdit{0, -1, AxLineOptions{.orientation = Orientation::Vertical,
                                                                       .color = Color::Red}});
            FigureEdits e;
            e.per_axes.push_back({1, ae});
            const FigureEdits undo = fi.apply_edits(e);
            const auto snap = fi.snapshot_box.load();
            const RenderSnapshot& r = *snap->axes[0].snap2d();
            check(r.axlines[0].value == 6.0 && r.axlines[0].on_x() && r.axspans[0].lo == -2.0,
                  "journal: the edits reach the snapshot");
            fi.apply_edits_and_publish();
            const AxesImpl* live = fi.find_slot_impl(1);
            check(ax->axline_data(0).value == 6.0 && ax->axspan_data(0).lo == -2.0 && live->axlines[0].on_x() &&
                      ax->xlim().hi > 6.0,
                  "journal: and replay onto the live axes, the line now vertical");
            fi.apply_edits(undo);
            const auto u = fi.snapshot_box.load();
            const RenderSnapshot& ur = *u->axes[0].snap2d();
            check(ur.axlines[0].value == 2.0 && !ur.axlines[0].on_x() && ur.axspans[0].lo == 1.0,
                  "journal: undo restores value, end and orientation");
        }

        // The Data panel draws both kinds' tabs (Appearance, table, outline
        // colour) with no duplicate widget ids.
        {
            FigureSnapshot fs;
            RenderSnapshot r;
            AxSpanPlot sp = axspan_of(1.0, 2.0, Orientation::Horizontal, kGreen, 5);
            sp.opts.linewidth = 2.0f;
            sp.opts.edgecolor = kRed;
            r.axspans.push_back(sp);
            fs.axes.push_back({{1, 1, 1}, std::move(r)});
            fs.generation = fs.data_generation = 1;
            const IdConflictScan ss = scan_panel_for_id_conflicts(fs, &draw_data_window, "Data", {});
            FigureSnapshot fl;
            RenderSnapshot rl;
            rl.axlines.push_back(axline_of(1.0, Orientation::Vertical, kRed, 6));
            fl.axes.push_back({{1, 1, 1}, std::move(rl)});
            fl.generation = fl.data_generation = 1;
            const IdConflictScan sl = scan_panel_for_id_conflicts(fl, &draw_data_window, "Data", {});
            check(ss.conflicts == 0 && sl.conflicts == 0 && ss.probes > 0 && sl.probes > 0,
                  "panel: a span's and a line's tabs draw without id conflicts");
        }
    }

    // -------------------------------------------------------------------------
    // Rendered: GL against SVG, call order, clipping, dashes
    // -------------------------------------------------------------------------
    void test_reference_lines_rendered() {
        std::printf("\n[2D reference lines and spans, rendered]\n");

        const int W = 400, H = 320;
        // In call order: a black line at y = 5; a green vertical span over x in
        // [2, 4] between 10% and 90% of the height; a red horizontal line at
        // y = 7; a blue dashed vertical line at x = 8; an orange span over y in
        // [9, 15], past the top; a red line at y = -3, below the view.
        FigureSnapshot fs;
        {
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            RenderSnapshot& s = *fa.snap2d();
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            LinePlot lp;
            lp.x = std::vector<double>{0.0, 10.0};
            lp.y = std::vector<double>{5.0, 5.0};
            lp.opts.color = Color::Black;
            lp.opts.linewidth = 3.0f;
            lp.id = 1;
            s.lines.push_back(lp);
            AxSpanPlot green = axspan_of(2.0, 4.0, Orientation::Vertical, kGreen, 2);
            green.opts.from = 0.1;
            green.opts.to = 0.9;
            s.axspans.push_back(green);
            AxLinePlot red = axline_of(7.0, Orientation::Horizontal, kRed, 3);
            red.opts.linewidth = 4.0f;
            s.axlines.push_back(red);
            AxLinePlot blue = axline_of(8.0, Orientation::Vertical, kBlue, 4);
            blue.opts.linewidth = 3.0f;
            blue.opts.linestyle = LineStyle::Dashed;
            s.axlines.push_back(blue);
            s.axspans.push_back(axspan_of(9.0, 15.0, Orientation::Horizontal, kOrange, 5));
            s.axlines.push_back(axline_of(-3.0, Orientation::Horizontal, kRed, 6));
            fs.axes.push_back(std::move(fa));
            fs.generation = fs.data_generation = 1;
        }
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            export_figure_png(dev, data, fs, "reference_lines.png", W, H, 1);
            export_figure_svg(fs, "reference_lines.svg", W, H);
        }
        const CellLayout cell = compute_figure_layout(fs, W, H).cells[0];
        const CoordTransform& tr = cell.tr;
        const PlotRect& f = cell.frame;
        const std::string svg = read_file("reference_lines.svg");
        const Png png("reference_lines.png");

        // SVG: the elements where the geometry puts them.
        const auto reds = elements(svg, "line", "stroke=\"rgb(204,0,0)\"");
        const bool red_at = reds.size() == 2 && near(attr(reds[0], "x1"), f.x, 0.01) &&
                            near(attr(reds[0], "x2"), f.x + f.w, 0.01) && near(attr(reds[0], "y1"), tr.to_py(7.0), 0.01);
        check(red_at, "svg: the red line spans the frame at y = 7, and the one out of view is written too (" +
                          std::to_string(reds.size()) + ")");
        const auto blues = elements(svg, "line", "stroke=\"rgb(0,0,204)\"");
        check(blues.size() == 1 && blues[0].find("stroke-dasharray") != std::string::npos &&
                  near(attr(blues[0], "x1"), tr.to_px(8.0), 0.01),
              "svg: the blue line is dashed, at x = 8");
        const auto greens = elements(svg, "rect", "fill=\"rgb(0,153,0)\"");
        check(greens.size() == 1 && near(attr(greens[0], "x"), tr.to_px(2.0), 0.01) &&
                  near(attr(greens[0], "width"), tr.to_px(4.0) - tr.to_px(2.0), 0.01) &&
                  near(attr(greens[0], "y"), f.y + 0.1 * f.h, 0.01) && near(attr(greens[0], "height"), 0.8 * f.h, 0.01),
              "svg: the green span's rect, its height the frame fractions");
        const auto oranges = elements(svg, "rect", "fill=\"rgb(204,153,0)\"");
        check(oranges.size() == 1 && near(attr(oranges[0], "y"), f.y - 2.0, 0.01) &&
                  near(attr(oranges[0], "y") + attr(oranges[0], "height"), tr.to_py(9.0), 0.01),
              "svg: the span past the top is held 2 px above the frame (clipped there)");
        const std::size_t at_line = svg.find("<polyline"), at_green = svg.find("fill=\"rgb(0,153,0)\""),
                          at_red = svg.find("stroke=\"rgb(204,0,0)\"");
        check(at_line < at_green && at_green < at_red, "svg: in call order, line, span, reference line");

        // GL: the same picture, in call order.
        const float gx = tr.to_px(3.0);
        check(png.is(gx, tr.to_py(5.0), 0, 153, 0) && png.is(tr.to_px(6.0), tr.to_py(5.0), 0, 0, 0),
              "gl: the span covers the line drawn before it, which shows beside it");
        check(png.is(gx, tr.to_py(7.0), 204, 0, 0) && png.is(tr.to_px(6.0), tr.to_py(7.0), 204, 0, 0),
              "gl: the line drawn after the span crosses it");
        check(png.is(gx, tr.to_py(1.5), 0, 153, 0) && !png.is(gx, f.y + f.h * 0.95f, 0, 153, 0) &&
                  !png.is(gx, f.y + f.h * 0.05f, 0, 153, 0),
              "gl: the span stops at its frame fractions");
        check(png.is(tr.to_px(5.0), tr.to_py(9.5), 204, 153, 0) && !png.is(tr.to_px(5.0), f.y - 3.0f, 204, 153, 0),
              "gl: the span past the top is clipped at the frame");
        check(!png.is(tr.to_px(5.0), f.y + f.h + 3.0f, 204, 0, 0) && !png.is(tr.to_px(5.0), f.y + f.h - 2.0f, 204, 0, 0),
              "gl: the line below the view draws nothing");
        int on = 0, off = 0;
        for (float y = tr.to_py(8.5); y < tr.to_py(0.5); y += 1.0f) {
            if (png.is(tr.to_px(8.0), y, 0, 0, 204)) ++on;
            else ++off;
        }
        check(on > 20 && off > 20, "gl: the vertical line is dashed (" + std::to_string(on) + " on, " +
                                       std::to_string(off) + " off)");
    }
} // namespace lt

namespace lt {
    using namespace sextant;

    namespace {
        // 0..10 on both axes over a 400 px frame at (50, 40): 40 px a unit.
        CoordTransform sloped_tr() {
            CoordTransform tr{};
            tr.xmin = 0.0; tr.xmax = 10.0; tr.ymin = 0.0; tr.ymax = 10.0;
            tr.px = 50.0f; tr.py = 40.0f; tr.pw = 400.0f; tr.ph = 400.0f;
            return tr;
        }

        AxLinePlot sloped_of(Pos x, Pos y, double slope) {
            AxLinePlot p;
            p.mode = AxLineMode::PointSlope;
            p.x = x; p.y = y; p.slope = slope;
            p.opts.color = Color::Black;
            return p;
        }

        AxLinePlot through_of(Pos x1, Pos y1, Pos x2, Pos y2) {
            AxLinePlot p;
            p.mode = AxLineMode::TwoPoints;
            p.x = x1; p.y = y1; p.x2 = x2; p.y2 = y2;
            p.opts.color = Color::Black;
            return p;
        }

        // The distance from point q to the infinite line through a and b.
        double off_line(const RefPt& a, const RefPt& b, const RefPt& q) {
            const double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
            return std::fabs((q.x - a.x) * dy - (q.y - a.y) * dx) / len;
        }
    } // namespace

    // axline(x, y, slope) and axline(x1, y1, x2, y2): the calls and read-back,
    // validation, limits, and the pixel geometry -- straight in data units on
    // linear axes, bent on a log axis, straight on screen for frame positions.
    void test_axline_sloped() {
        std::printf("\n[axline: a slope, or two points]\n");
        using V = std::vector<double>;
        const CoordTransform tr = sloped_tr();

        // The calls.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->axline(2.0, 3.0, 0.5);
            ax->axline(1.0, 1.0, 4.0, 7.0, {.linestyle = LineStyle::Dashed});
            ax->axline(3.0);
            check(ax->axline_count() == 3, "axline: all three forms are the same kind");
            const AxLineData a = ax->axline_data(0), b = ax->axline_data(1), c = ax->axline_data(2);
            check(a.mode == AxLineMode::PointSlope && a.x.v == 2.0 && a.y.v == 3.0 && a.slope == 0.5,
                  "axline: a point and slope read back");
            check(b.mode == AxLineMode::TwoPoints && b.x2.v == 4.0 && b.y2.v == 7.0 && c.mode == AxLineMode::Value
                      && c.value == 3.0,
                  "axline: two points read back, the value form unchanged");
            check(throws_invalid([&] { ax->axline(1.0, 1.0, std::nan("")); })
                      && throws_invalid([&] { ax->axline(std::nan(""), 1.0, 1.0); })
                      && throws_invalid([&] { ax->axline(1.0, 1.0, 1.0, 1.0); })
                      && throws_invalid([&] { ax->axline(1.0, 1.0, std::numeric_limits<double>::infinity(), 2.0); })
                      && throws_invalid([&] { ax->axline(0.0, 0.0, 1.0, {.zorder = std::nan("")}); })
                      && ax->axline_count() == 3,
                  "axline: a non-finite number, equal points or a bad zorder throw, and add nothing");
            ax->axline(Pos::fraction(0.5), Pos::fraction(0.5), Pos::fraction(0.9), Pos::pixels(10.0));   // mixed spaces
            ax->set_axline_data(0, {.mode = AxLineMode::PointSlope, .x = Pos(1.0), .y = Pos(1.0), .slope = 2.0});
            check(ax->axline_data(0).slope == 2.0 && ax->axline_data(0).x.v == 1.0, "axline: set_axline_data() moves a sloped line");
            ax->set_axline_data(0, {.value = 5.0});
            check(ax->axline_data(0).mode == AxLineMode::Value && ax->axline_data(0).value == 5.0,
                  "axline: and may turn it into a value line");
            check(throws_invalid([&] { ax->set_axline_data(1, {.mode = AxLineMode::TwoPoints, .x = Pos(1.0), .y = Pos(1.0),
                                                              .x2 = Pos(1.0), .y2 = Pos(1.0)}); }),
                  "axline: set_axline_data() validates as the call");
            // Sloped lines are not drawn into the cycle twice: one cycle colour each.
            auto g = Figure::create();
            auto a2 = g->add_subplot(1, 1, 1);
            a2->axline(0.0, 0.0, 1.0);
            a2->axline(0.0, 0.0, 2.0);
            check(a2->axline_count() == 2, "axline: two sloped lines");
        }

        // Limits: data points count, frame positions do not, the line is infinite.
        {
            auto f = Figure::create();
            auto a = f->add_subplot(1, 1, 1);
            a->axline(2.0, 3.0, 5.0);
            check(near(a->xlim().lo, 2.0, 1.0) && near(a->xlim().hi, 2.0, 1.0) && near(a->ylim().lo, 3.0, 1.0),
                  "limits: a point and slope counts as its point (" + std::to_string(a->xlim().lo) + ")");
            auto g = Figure::create();
            auto b = g->add_subplot(1, 1, 1);
            b->axline(0.0, 0.0, 10.0, 4.0);
            check(b->xlim().hi >= 10.0 && b->ylim().hi >= 4.0, "limits: two points count both");
            auto h = Figure::create();
            auto c = h->add_subplot(1, 1, 1);
            c->line(V{0, 1}, V{0, 1});
            const Range x0 = c->xlim(), y0 = c->ylim();
            c->axline(Pos::fraction(0.5), Pos::fraction(0.5), 3.0);
            c->axline(Pos::pixels(500.0), 700.0, Pos::fraction(2.0), Pos::fraction(3.0));
            check(c->xlim().hi == x0.hi && c->ylim().hi == y0.hi && c->xlim().lo == x0.lo && c->ylim().lo == y0.lo,
                  "limits: frame positions move nothing");
        }

        // A straight line of the data on linear axes: through its point, with its slope.
        {
            const AxLinePlot p = sloped_of(Pos(5.0), Pos(5.0), 1.0);
            const auto runs = axline_paths(p, tr);
            check(runs.size() == 1 && runs[0].size() == 2, "geometry: a linear sloped line is one segment");
            const RefPt a = runs[0][0], b = runs[0][1];
            check(off_line(a, b, {tr.to_px(5.0), tr.to_py(5.0)}) < 0.01
                      && near((b.y - a.y) / (b.x - a.x), -1.0, 1e-3),
                  "geometry: through (5, 5), rising one pixel per pixel (y is down on screen)");
            check(std::min({a.x, b.x}) >= tr.px - 5.0f && std::max({a.x, b.x}) <= tr.px + tr.pw + 5.0f
                      && std::min({a.y, b.y}) >= tr.py - 5.0f && std::max({a.y, b.y}) <= tr.py + tr.ph + 5.0f,
                  "geometry: cut to the frame and a few pixels");
            const auto steep = axline_paths(sloped_of(Pos(5.0), Pos(5.0), 20.0), tr);
            check(steep.size() == 1 && (near(steep[0][0].y, tr.py - 4.0, 1.0) || near(steep[0][1].y, tr.py - 4.0, 1.0)),
                  "geometry: a steep line runs out of the top");
            const auto miss = axline_paths(sloped_of(Pos(5.0), Pos(50.0), 0.0), tr);
            check(miss.empty(), "geometry: a line outside the frame gives nothing");
        }
        {
            const auto runs = axline_paths(through_of(Pos(1.0), Pos(2.0), Pos(6.0), Pos(4.0)), tr);
            check(runs.size() == 1 && off_line(runs[0][0], runs[0][1], {tr.to_px(1.0), tr.to_py(2.0)}) < 0.01
                      && off_line(runs[0][0], runs[0][1], {tr.to_px(6.0), tr.to_py(4.0)}) < 0.01,
                  "geometry: through two points");
            const auto vert = axline_paths(through_of(Pos(3.0), Pos(1.0), Pos(3.0), Pos(9.0)), tr);
            check(vert.size() == 1 && near(vert[0][0].x, tr.to_px(3.0), 1e-3) && near(vert[0][1].x, tr.to_px(3.0), 1e-3),
                  "geometry: two points with one x are a vertical line");
        }

        // Frame positions: a straight screen line, the slope the screen's.
        {
            const auto runs = axline_paths(sloped_of(Pos::fraction(0.5), Pos::fraction(0.5), 1.0), tr);
            check(runs.size() == 1 && off_line(runs[0][0], runs[0][1], {tr.px + 200.0f, tr.py + 200.0f}) < 0.01
                      && near((runs[0][1].y - runs[0][0].y) / (runs[0][1].x - runs[0][0].x), -1.0, 1e-3),
                  "geometry: a fraction point with slope 1 rises at 45 degrees on screen");
            const auto mixed = axline_paths(through_of(Pos(5.0), Pos::fraction(0.1), Pos::fraction(0.9), Pos::pixels(100.0)), tr);
            check(mixed.size() == 1 && off_line(mixed[0][0], mixed[0][1], {tr.to_px(5.0), tr.py + tr.ph * 0.9f}) < 0.01
                      && off_line(mixed[0][0], mixed[0][1], {tr.px + 0.9f * tr.pw, tr.py + tr.ph - 100.0f}) < 0.01,
                  "geometry: mixed spaces are straight on screen through each resolved point");
        }

        // A log axis bends it: sampled in data, never off the equation, absent where there is no place.
        {
            CoordTransform lg = tr;
            lg.ys.kind = Scale::Log;
            lg.ymin = 1.0; lg.ymax = 1000.0;
            const AxLinePlot p = sloped_of(Pos(3.0), Pos(10.0), 10.0);   // y = 10 + 10 (x - 3): <= 0 for x <= 2
            const auto runs = axline_paths(p, lg);
            check(runs.size() == 1 && runs[0].size() > 4, "geometry: on a log axis a straight data line is a curve of many points");
            bool on = true;
            for (const RefPt& q : runs[0]) {
                const double x = lg.to_data_x(q.x);
                on = on && near(lg.to_py(10.0 + 10.0 * (x - 3.0)), q.y, 0.6);
            }
            check(on, "geometry: every sampled point is on the data line");
            check(runs[0].front().x >= lg.to_px(2.0) - 2.0f, "geometry: nothing where y <= 0 (the log axis has no place)");
            // The chord is not the curve: the middle sample is off the straight line between the ends.
            const RefPath& r = runs[0];
            check(off_line(r.front(), r.back(), r[r.size() / 2]) > 1.0, "geometry: and it bends");
            // A line wholly in the undefined region gives nothing.
            check(axline_paths(sloped_of(Pos(3.0), Pos(-5.0), 0.0), lg).empty(), "geometry: y = -5 has no place on a log axis");
            // A steep line is sampled along y.
            const auto steep = axline_paths(sloped_of(Pos(5.0), Pos(10.0), 1000.0), lg);
            bool all = !steep.empty();
            for (const RefPath& run : steep)
                for (const RefPt& q : run) all = all && std::isfinite(q.x) && std::isfinite(q.y);
            check(all, "geometry: a steep line on a log axis is finite");
        }

        // A symlog axis too.
        {
            CoordTransform sl = tr;
            sl.xs.kind = Scale::Symlog;
            sl.xmin = -10.0; sl.xmax = 10.0;
            const auto runs = axline_paths(sloped_of(Pos(0.0), Pos(1.0), 0.5), sl);
            check(runs.size() == 1 && runs[0].size() > 4, "geometry: on a symlog axis it is sampled too");
        }
    }

    // Drawn: the straight line, the curve on a log axis and the frame line, in PNG and SVG.
    void test_axline_sloped_rendered() {
        std::printf("\n[axline: a slope, rendered]\n");
        const int W = 400, H = 320;
        FigureSnapshot fs;
        {
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            RenderSnapshot& s = *fa.snap2d();
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            // Red, y = x; green, through (8, 1) and (8, 9) vertically; blue dashed, a frame line at 45 degrees from the centre.
            AxLinePlot r = sloped_of(Pos(0.0), Pos(0.0), 1.0);
            r.opts.color = Color{0.8f, 0.0f, 0.0f, 1.0f};
            r.opts.linewidth = 3.0f;
            r.id = 1;
            s.axlines.push_back(r);
            AxLinePlot g = through_of(Pos(8.0), Pos(1.0), Pos(8.0), Pos(9.0));
            g.opts.color = Color{0.0f, 0.6f, 0.0f, 1.0f};
            g.opts.linewidth = 3.0f;
            g.id = 2;
            s.axlines.push_back(g);
            AxLinePlot b = sloped_of(Pos::fraction(0.5), Pos::fraction(0.5), -1.0);
            b.opts.color = Color{0.0f, 0.0f, 0.8f, 1.0f};
            b.opts.linewidth = 3.0f;
            b.opts.linestyle = LineStyle::Dashed;
            b.id = 3;
            s.axlines.push_back(b);
            fs.axes.push_back(std::move(fa));
            fs.generation = fs.data_generation = 1;
        }
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            export_figure_png(dev, data, fs, "axline_sloped.png", W, H, 1);
            export_figure_svg(fs, "axline_sloped.svg", W, H);
        }
        const CellLayout cell = compute_figure_layout(fs, W, H).cells[0];
        const CoordTransform& tr = cell.tr;
        Png png("axline_sloped.png");
        check(png.is(tr.to_px(3.0), tr.to_py(3.0), 204, 0, 0) && png.is(tr.to_px(7.0), tr.to_py(7.0), 204, 0, 0)
                  && !png.is(tr.to_px(3.0), tr.to_py(6.0), 204, 0, 0),
              "gl: the line y = x through the data");
        check(png.is(tr.to_px(8.0), tr.to_py(5.0), 0, 153, 0) && png.is(tr.to_px(8.0), tr.to_py(9.8), 0, 153, 0),
              "gl: the vertical line through two points, to the top of the frame");
        int on = 0, off = 0;
        const float cx = tr.px + tr.pw * 0.5f, cy = tr.py + tr.ph * 0.5f;
        for (float t = -80.0f; t <= 80.0f; t += 1.0f) (png.is(cx + t, cy + t, 0, 0, 204) ? on : off)++;
        check(on > 20 && off > 10, "gl: the frame line runs down at 45 degrees on screen, dashed (" +
                                       std::to_string(on) + " on, " + std::to_string(off) + " off)");

        const std::string svg = read_file("axline_sloped.svg");
        check(elements(svg, "polyline", "rgb(204,0,0)").size() == 1 && elements(svg, "polyline", "rgb(0,153,0)").size() == 1,
              "svg: a polyline per sloped line");
        const auto dashed = elements(svg, "polyline", "rgb(0,0,204)");
        check(dashed.size() == 1 && dashed[0].find("stroke-dasharray") != std::string::npos, "svg: the dashed one is dashed");

        // On a log axis it is a curve: the pixel at the data line's place is coloured, the chord's place not.
        FigureSnapshot ls;
        {
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            RenderSnapshot& s = *fa.snap2d();
            s.xmin = 0.0; s.xmax = 10.0; s.ymin = 1.0; s.ymax = 1000.0;
            s.xlim_auto = s.ylim_auto = false;
            s.yscale.kind = Scale::Log;
            AxLinePlot r = sloped_of(Pos(5.0), Pos(100.0), 100.0);   // y = 100 + 100 (x - 5)
            r.opts.color = Color{0.8f, 0.0f, 0.0f, 1.0f};
            r.opts.linewidth = 3.0f;
            r.id = 1;
            s.axlines.push_back(r);
            ls.axes.push_back(std::move(fa));
            ls.generation = ls.data_generation = 1;
        }
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            export_figure_png(dev, data, ls, "axline_sloped_log.png", W, H, 1);
        }
        const CellLayout lcell = compute_figure_layout(ls, W, H).cells[0];
        const CoordTransform& lt_ = lcell.tr;
        Png lpng("axline_sloped_log.png");
        check(lpng.is(lt_.to_px(7.0), lt_.to_py(300.0), 204, 0, 0) && lpng.is(lt_.to_px(5.0), lt_.to_py(100.0), 204, 0, 0)
                  && lpng.is(lt_.to_px(9.0), lt_.to_py(500.0), 204, 0, 0),
              "gl: on a log axis the line passes its data points");
        check(!lpng.is(lt_.to_px(3.0), lt_.to_py(100.0), 204, 0, 0), "gl: and is not the straight chord");
    }
} // namespace lt

namespace lt {
    using namespace sextant;

    // Plane2D::axspan(): the call and read-back, the parent's limits, the band in
    // the raster and in the SVG scene.
    void test_plane_axspan() {
        std::printf("\n[axspan: on a plane]\n");

        auto fig = Figure::create({.width = 400, .height = 320});
        auto a3 = fig->add_subplot3d(1, 1, 1);
        a3->set_xlim(0, 10).set_ylim(0, 10).set_zlim(0, 10);
        auto pl = a3->plane(PlaneOrientation::XY, 0.0);
        pl->axspan(2.0, 6.0, {.color = Color{0.8f, 0.0f, 0.0f, 1.0f}, .alpha = 1.0f});
        pl->axspan(3.0, 4.0, {.orientation = Orientation::Vertical, .color = Color{0.0f, 0.0f, 0.8f, 1.0f}, .alpha = 1.0f});
        check(pl->axspan_count() == 2 && pl->axspan_data(0).lo == 2.0 && pl->axspan_data(0).hi == 6.0,
              "plane axspan: the call and its read-back");
        pl->axspan(9.0, 7.0);
        check(pl->axspan_data(2).lo == 7.0 && pl->axspan_data(2).hi == 9.0, "plane axspan: stored ordered");
        bool bad = false;
        try {
            pl->axspan(std::nan(""), 1.0);
        } catch (const std::invalid_argument&) {
            bad = true;
        }
        check(bad && pl->axspan_count() == 3, "plane axspan: a non-finite end throws and adds nothing");
        pl->set_axspan_data(2, {1.0, 0.5});
        check(pl->axspan_data(2).lo == 0.5 && pl->axspan_data(2).hi == 1.0, "plane axspan: set_axspan_data() orders it too");
        pl->set_axspan_visible(2, false);
        check(!pl->axspan_visible(2) && pl->axspan_visible(0), "plane axspan: set_axspan_visible()");
        check(pl->remove(pl->last_object()) && pl->axspan_count() == 2, "plane axspan: remove() takes it by id");

        // Limits: the parent's auto limits take a band's ends on the plane's axis.
        {
            auto f = Figure::create();
            auto b3 = f->add_subplot3d(1, 1, 1);
            auto p = b3->plane(PlaneOrientation::XY, 0.0);
            p->axspan(20.0, 30.0);
            check(b3->ylim().hi > 30.0 - 1e-9 && b3->ylim().lo < 20.0 + 1e-9, "plane axspan: a band widens the parent's limits");
        }

        // The raster and the SVG scene.
        fig->savefig("plane_axspan.png");
        fig->savefig_svg("plane_axspan.svg");
        Png png("plane_axspan.png");
        std::size_t red = 0, blue = 0;
        for (int y = 0; y < png.h; ++y)
            for (int x = 0; x < png.w; ++x) {
                red += png.is(static_cast<float>(x), static_cast<float>(y), 204, 0, 0);
                blue += png.is(static_cast<float>(x), static_cast<float>(y), 0, 0, 204);
            }
        check(red > 1500 && blue > 400, "plane axspan gl: both bands are drawn (" + std::to_string(red) + " red, " +
                                            std::to_string(blue) + " blue px)");
        const std::string svg = read_file("plane_axspan.svg");
        check(svg.find("rgb(204,0,0)") != std::string::npos && svg.find("rgb(0,0,204)") != std::string::npos,
              "plane axspan svg: both bands are written");

        // cla() clears them.
        pl->cla();
        check(pl->axspan_count() == 0, "plane axspan: cla() clears them");
    }
} // namespace lt
