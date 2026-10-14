// Reference lines and spans (step 36c-5): Axes::axline() and axspan(). The
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
