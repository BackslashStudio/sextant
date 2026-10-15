// Filled areas (step 36d-1): Axes::fill_between() and Plane2D::fill_between().
// The call, read-back, set_fill_data() and cla(); the regions (runs, exclude,
// NaN, steps, interpolate, log), the triangles (no overlap, crossings), and
// automatic limits (both orientations, an orientation edit, a twin, log);
// the Data panel, its cell, row and style edits with undo, the journal's
// replay; legend keys; no hover; planes; GL against SVG.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "axis_limits.h"
#include "figure_impl.h"
#include "fill_geometry.h"
#include "plot_data_view.h"
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
        bool same_range(Range a, Range b, double tol = 1e-9) { return near(a.lo, b.lo, tol) && near(a.hi, b.hi, tol); }

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

        FillPlot fill_of(V x, V y1, V y2, Color c, ObjectId id) {
            FillPlot p;
            p.x = std::move(x);
            p.y1 = std::move(y1);
            p.y2 = std::move(y2);
            p.opts.color = c;
            p.id = id;
            return p;
        }

        // The area the triangles of `p` cover (each counted once), in scale
        // space through linear scales.
        double tri_area(const FillPlot& p) {
            double a = 0.0;
            for (const FillRun& run : fill_runs(p, AxisScale{}, AxisScale{}))
                fill_triangles(run, [&](double r0, double v0, double r1, double v1, double r2, double v2) {
                    a += 0.5 * std::abs((r1 - r0) * (v2 - v0) - (r2 - r0) * (v1 - v0));
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

    static void fill_api_checks();
    static void fill_geometry_checks();
    static void fill_limit_checks();
    static void fill_panel_checks();
    static void fill_stack_checks();

    void test_fill_between() {
        std::printf("\n[2D fill_between]\n");
        try {
            fill_api_checks();
            fill_geometry_checks();
            fill_limit_checks();
            fill_panel_checks();
            fill_stack_checks();
        } catch (const std::exception& e) {
            check(false, std::string("fill_between: unexpected exception: ") + e.what());
        }
    }

    // --- The call, read-back, set_fill_data(), cla(), objects by id, planes ---
    static void fill_api_checks() {
        auto fig = Figure::create();
        auto ax = fig->add_subplot(1, 1, 1);
        const V x{0.0, 1.0, 2.0}, y1{1.0, 2.0, 3.0}, y2{0.5, 0.5, 0.5};
        ax->fill(x, y1, y2);
        ax->fill(x, y1);
        ax->fill(x, y1, 4.0, {.exclude = {false, true, false}});
        check(ax->fill_count() == 3 && ax->fill_data(0).y2 == y2 && ax->fill_data(1).y2 == V(3, 0.0) &&
                  ax->fill_data(2).y2 == V(3, 4.0) && ax->fill_data(0).x == x && ax->fill_data(0).y1 == y1,
              "call: stores x, y1 and y2, a scalar y2 (0 by default) repeated per point");
        check(ax->fill_data(0).exclude.empty() &&
                  ax->fill_data(2).exclude == std::vector<bool>{false, true, false},
              "call: the exclude mask is kept as data and read back");

        check(throws_invalid([&] { ax->fill(x, V{1.0, 2.0}, y2); }) &&
                  throws_invalid([&] { ax->fill(x, y1, V{1.0}); }) &&
                  throws_invalid([&] { ax->fill(x, y1, 0.0, {.exclude = {true}}); }) &&
                  ax->fill_count() == 3,
              "call: unequal lengths or a mask of another length throw, and nothing is added");
        check(ax->fill(V{}, V{}, V{}).fill_count() == 4 && ax->fill_data(3).x.empty(),
              "call: an empty fill is allowed (it fills nothing)");

        // set_fill_data: the struct takes the mask as given; the span overload
        // keeps it while the point count holds.
        ax->set_fill_data(2, V{5.0, 6.0, 7.0}, V{1.0, 1.0, 1.0}, V{2.0, 2.0, 2.0});
        check(ax->fill_data(2).x == V({5.0, 6.0, 7.0}) && ax->fill_data(2).exclude.size() == 3,
              "set_fill_data(spans): moves the points, the mask kept at the same count");
        ax->set_fill_data(2, V{5.0, 6.0}, V{1.0, 1.0}, V{2.0, 2.0});
        check(ax->fill_data(2).exclude.empty(), "set_fill_data(spans): and dropped at another count");
        ax->set_fill_data(2, FillData{V{1.0, 2.0}, V{3.0, 3.0}, V{0.0, 0.0}, {true, false}});
        check(ax->fill_data(2).exclude == std::vector<bool>{true, false} && ax->fill_data(2).y1 == V({3.0, 3.0}),
              "set_fill_data(FillData): takes the mask as given");
        check(throws_invalid([&] { ax->set_fill_data(2, FillData{V{1.0}, V{1.0, 2.0}, V{1.0}, {}}); }) &&
                  throws_invalid([&] { ax->set_fill_data(2, FillData{V{1.0}, V{1.0}, V{1.0}, {true, true}}); }) &&
                  ax->fill_data(2).x == V({1.0, 2.0}) && ax->fill_data(2).exclude.size() == 2,
              "set_fill_data: bad lengths throw and change nothing");
        check(throws_range([&] { (void)ax->fill_data(4); }) && throws_range([&] { ax->set_fill_data(9, x, y1, y2); }),
              "read-back: an index past the count throws out_of_range");

        // Color: the cycle, as a line takes it.
        {
            auto cyc = Figure::create();
            auto c1 = cyc->add_subplot(1, 1, 1);
            c1->fill(x, y1).line(x, y1);
            const FigureSnapshot cs = detail::FigureAccess::impl(*cyc).build_figure_snapshot();
            const RenderSnapshot& c = *cs.axes[0].snap2d();
            const Color& f = *c.fills[0].opts.color;
            const Color& l = *c.lines[0].opts.color;
            check(f.r == Color::Blue.r && f.g == Color::Blue.g && f.b == Color::Blue.b && !(l.r == f.r && l.g == f.g),
                  "color: fill_between() takes the next cycle color");
        }

        // Removal and restore by id, as other kinds; cla().
        {
            auto& fi = detail::FigureAccess::impl(*fig);
            AxesImpl* a = fi.find_slot_impl(1);
            const ObjectId id = a->fills[1].id;
            const auto ref = a->find_object(id);
            auto removed = a->remove_object(id);
            check(ref && ref->kind == PlotKind::Fill && ref->index == 1 && removed && a->fills.size() == 3,
                  "objects: a fill is found and removed by id");
            check(removed && a->restore_object(std::move(*removed)) && a->fills.size() == 4 && a->fills[1].id == id,
                  "objects: and restored with its id");
        }
        ax->cla();
        check(ax->fill_count() == 0, "cla(): clears fills");

        // Planes: the same call, and its data feeds the parent's limits.
        {
            auto f3 = Figure::create();
            auto a3 = f3->add_subplot3d(1, 1, 1);
            auto pl = a3->plane(PlaneOrientation::XY, 0.5);
            pl->fill(V{0.0, 10.0}, V{2.0, 3.0}, 1.0, {.exclude = {false, false}});
            check(pl->fill_count() == 1 && pl->fill_data(0).y2 == V({1.0, 1.0}) && pl->fill_data(0).exclude.size() == 2,
                  "plane: fill_between() and its read-back");
            pl->set_fill_data(0, V{0.0, 20.0}, V{2.0, 3.0}, V{1.0, 1.0});
            check(pl->fill_data(0).x.back() == 20.0 && a3->xlim().hi > 20.0,
                  "plane: set_fill_data(), and the fill widens the parent's limits");
            pl->cla();
            check(pl->fill_count() == 0, "plane: cla() clears fills");
        }
    }

    // --- Regions and triangles -------------------------------------------------
    static void fill_geometry_checks() {
        const AxisScale lin{};
        // Runs: NaN and excluded points split; a lone point fills nothing.
        {
            FillPlot p = fill_of(V{0, 1, 2, 3, 4, 5, 6}, V{1, 1, NAN, 1, 1, 1, 1}, V(7, 0.0), kRed, 1);
            p.exclude = V{0, 0, 0, 0, 1, 0, 0};
            const auto runs = fill_runs(p, lin, lin);
            check(runs.size() == 2 && runs[0].r == V({0.0, 1.0}) && runs[1].r == V({5.0, 6.0}),
                  "runs: a NaN and an excluded point split the fill; the lone point between draws nothing (" +
                      std::to_string(runs.size()) + ")");
        }
        // Triangles: the area under a trapezoid, and a crossing pinches.
        {
            const FillPlot t = fill_of(V{0, 2}, V{1, 3}, V{0, 0}, kRed, 1);
            check(near(tri_area(t), 4.0, 1e-12), "triangles: a trapezoid's area");
            // y1 - y2 runs 2 -> -2 over [0, 2]: crossing at 1, two triangles of area 1.
            const FillPlot c = fill_of(V{0, 2}, V{2, 0}, V{0, 2}, kRed, 1);
            int tris = 0;
            for (const FillRun& run : fill_runs(c, lin, lin))
                fill_triangles(run, [&](double, double, double, double, double, double) { ++tris; });
            check(near(tri_area(c), 2.0, 1e-12) && tris == 2,
                  "triangles: crossing curves meet at the crossing (area " + std::to_string(tri_area(c)) + ")");
        }
        // Steps: the expanded run, its area that of the stepped curve.
        {
            FillPlot s = fill_of(V{0, 1, 2}, V{1, 2, 3}, V(3, 0.0), kRed, 1);
            s.opts.drawstyle = DrawStyle::StepsPost;
            const auto runs = fill_runs(s, lin, lin);
            check(runs.size() == 1 && runs[0].size() == 5 && near(tri_area(s), 3.0, 1e-12),
                  "steps: post steps hold each value to the next x (area 1 + 2)");
            s.opts.drawstyle = DrawStyle::StepsMid;
            check(near(tri_area(s), 0.5 + 1.0 + 1.0 + 1.5, 1e-12), "steps: mid steps rise halfway");
        }
        // interpolate: a region ending at an excluded point runs on to the crossing.
        {
            // y1 - y2: 1, 1, -1 over x 0, 1, 2; point 2 excluded.
            FillPlot p = fill_of(V{0, 1, 2}, V{1, 1, 0}, V{0, 0, 1}, kRed, 1);
            p.exclude = V{0, 0, 1};
            check(near(tri_area(p), 1.0, 1e-12), "interpolate: off, the region stops at the last kept point");
            p.opts.interpolate = true;
            const auto runs = fill_runs(p, lin, lin);
            check(runs.size() == 1 && runs[0].r.back() == 1.5 && runs[0].a.back() == 0.5 &&
                      near(tri_area(p), 1.25, 1e-12),
                  "interpolate: on, it runs on to the crossing at x = 1.5");
            FillPlot same = fill_of(V{0, 1, 2}, V{1, 1, 2}, V{0, 0, 1}, kRed, 1);
            same.exclude = V{0, 0, 1};
            same.opts.interpolate = true;
            check(fill_runs(same, lin, lin)[0].r.back() == 1.0, "interpolate: no crossing, no extension");
        }
        // Log: a run coordinate <= 0 breaks the run; a curve <= 0 lies far below.
        {
            const AxisScale lg = make_axis_scale(Scale::Log, {});
            const FillPlot p = fill_of(V{-1, 1, 10, 100}, V{10, 10, 10, 10}, V(4, 0.0), kRed, 1);
            const auto runs = fill_runs(p, lg, lg);
            check(runs.size() == 1 && runs[0].size() == 3 && runs[0].b[0] < -100.0 && runs[0].a[0] == 1.0,
                  "log: x <= 0 breaks the run; y2 = 0 lies far below any view");
        }
    }

    // --- Limits ------------------------------------------------------------------
    static void fill_limit_checks() {
        // Both curves and the run, padded; excluded and NaN points count for nothing.
        {
            RenderSnapshot s;
            FillPlot p = fill_of(V{0, 10, 20}, V{2, 3, 100}, V{-2, 0, 0}, kRed, 1);
            p.exclude = V{0, 0, 1};
            s.fills.push_back(p);
            check(same_range(xlim_of(s), {-0.5, 10.5}) && same_range(ylim_of(s), {-2.25, 3.25}),
                  "limits: both curves and the run axis, padded, the excluded point left out (" +
                      range_str(xlim_of(s)) + ", " + range_str(ylim_of(s)) + ")");
            s.fills[0].opts.interpolate = true;
            s.fills[0].y1 = V{2, 3, -3};
            check(xlim_of(s).hi > 11.0, "limits: with interpolate, the crossing past the last kept point counts");
            s.fills[0].opts.interpolate = false;
            s.fills[0].y1 = V{2, NAN, 100};
            s.fills[0].exclude = V{};
            check(same_range(xlim_of(s), {-1.0, 21.0}) && ylim_of(s).hi > 100.0 && ylim_of(s).lo < -2.0,
                  "limits: a NaN point is left out, the rest counts");
        }
        // Vertical: the run on y, the curves on x; an orientation edit swaps.
        {
            RenderSnapshot s;
            s.fills.push_back(fill_of(V{0, 1}, V{50, 60}, V{40, 40}, kRed, 1));
            check(ylim_of(s).hi > 60.0 && xlim_of(s).hi < 2.0, "limits: horizontal, the curves on y");
            FillOptions o = s.fills[0].opts;
            o.orientation = Orientation::Vertical;
            std::vector<PlotStyleEdit> inv;
            apply_plot_style_edits(s, {PlotStyleEdit{0, -1, o}}, &inv);
            check(xlim_of(s).hi > 60.0 && ylim_of(s).hi < 2.0, "limits: vertical after an orientation edit");
            apply_plot_style_edits(s, inv);
            check(ylim_of(s).hi > 60.0 && !s.fills[0].vertical(), "limits: and back with its undo");
        }
        // Log y: the default y2 = 0 widens nothing; the fill still reaches down.
        {
            auto f = Figure::create();
            auto a = f->add_subplot(1, 1, 1);
            a->fill(V{0.0, 1.0}, V{10.0, 100.0});
            a->set_yscale(Scale::Log);
            check(same_range(a->ylim(), {std::pow(10.0, 0.95), std::pow(10.0, 2.05)}, 1e-6),
                  "limits: on log y, y2 = 0 counts for nothing (" + range_str(a->ylim()) + ")");
        }
        // A twin: its own y, the shared x through the group.
        {
            auto tf = Figure::create();
            auto host = tf->add_subplot(1, 1, 1);
            host->line(V{0, 1}, V{0, 1});
            auto twin = host->twinx();
            twin->fill(V{0, 30}, V{0, 70});
            check(twin->ylim().hi > 70.0 && host->ylim().hi < 2.0 && host->xlim().hi > 30.0,
                  "limits: a twin's fill widens its own y and the shared x");
        }
    }

    // --- Panel, edits, journal, legend, hover -----------------------------------
    static void fill_panel_checks() {
        RenderSnapshot s;
        s.fills.push_back(fill_of(V{0, 1, 2}, V{1, 2, 3}, V{0, 0, 0}, kRed, 1));
        FillPlot masked = fill_of(V{0, 1}, V{1, 1}, V{0, 0}, kGreen, 2);
        masked.opts.orientation = Orientation::Vertical;
        masked.exclude = V{0, 1};
        s.fills.push_back(masked);
        s.fills[0].data_stamp = s.fills[1].data_stamp = 1;
        {
            const auto t = collect_plot_data_tables(s);
            auto names = [&](std::size_t i) {
                std::string out;
                for (const auto& col: t[i].columns) out += std::string(col.name) + " ";
                return out;
            };
            check(t.size() == 2 && t[0].kind == PlotKind::Fill && names(0) == "x y1 y2 " &&
                      names(1) == "y x1 x2 exclude " && !t[0].rows_fixed && t[0].label == "fill 0",
                  "panel: columns named for the orientation, the mask when there is one (" +
                      (t.size() == 2 ? names(1) : "") + ")");
        }
        {
            RenderSnapshot c = s;
            std::vector<PlotDataOp> undo;
            apply_plot_data_ops(c, {PlotCellEdit{PlotKind::Fill, 0, 2, 1, 9.0},
                                    PlotCellEdit{PlotKind::Fill, 1, 3, 0, 1.0}}, &undo);
            check(c.fills[0].y2[1] == 9.0 && c.fills[1].excluded(0), "cells: a y2 cell, an exclude cell");
            apply_plot_data_ops(c, undo);
            check(c.fills[0].y2[1] == 0.0 && !c.fills[1].excluded(0), "cells: undone");

            std::vector<PlotDataOp> rows_undo;
            apply_plot_data_ops(c, {PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Fill, 1, 2},
                                    PlotRowEdit{PlotRowEdit::Op::Remove, PlotKind::Fill, 0, 0}}, &rows_undo);
            check(c.fills[1].x.size() == 3 && c.fills[1].exclude.size() == 3 && c.fills[1].excluded(2) &&
                      c.fills[0].x.size() == 2 && c.fills[0].y1[0] == 2.0,
                  "rows: an insert copies its neighbour (mask included); a remove takes every column");
            apply_plot_data_ops(c, rows_undo);
            check(c.fills[1].x.size() == 2 && c.fills[1].exclude.size() == 2 && c.fills[0].x.size() == 3 &&
                      c.fills[0].y1[0] == 1.0 && c.fills[0].y2[0] == 0.0,
                  "rows: undone");
        }
        {
            RenderSnapshot o = s;
            FillOptions fo = o.fills[0].opts;
            fo.color.reset();
            fo.alpha = 0.5f;
            fo.linewidth = 2.0f;
            std::vector<PlotStyleEdit> inv;
            apply_plot_style_edits(o, {PlotStyleEdit{0, -1, fo}}, &inv);
            check(o.fills[0].opts.alpha == 0.5f && o.fills[0].opts.linewidth == 2.0f && o.fills[0].opts.color &&
                      o.fills[0].opts.color->r == kRed.r,
                  "style: a fill edit applies, an unset color keeping the fill's");
            apply_plot_style_edits(o, inv);
            check(o.fills[0].opts.alpha == 1.0f && o.fills[0].opts.linewidth == 0.0f, "style: undone");
        }

        // Legend: a patch at the fill's opacity, edged when outlined.
        {
            RenderSnapshot l = s;
            l.fills[0].opts.name = "band";
            l.fills[0].opts.alpha = 0.3f;
            l.fills[1].opts.name = "edged";
            l.fills[1].opts.linewidth = 1.5f;
            l.fills[1].opts.edgecolor = kBlue;
            const auto e = collect_legend_entries(l);
            check(e.size() == 2 && e[0].kind == LegendKind::Bar && near(e[0].color.a, 0.3, 1e-6) &&
                      e[0].edge.a == 0.0f && e[1].name == "edged" && e[1].edge.b == kBlue.b && e[1].edge.a == 1.0f,
                  "legend: a patch at the fill's opacity, outlined in the edge color when the fill is");
        }

        // Not hovered.
        {
            const CoordTransform tr{0.0, 2.0, 0.0, 3.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
            check(!find_hint(s, tr, tr.to_px(1.0), tr.to_py(1.0)) && !find_hint(s, tr, tr.to_px(1.0), tr.to_py(2.0)),
                  "hover: a fill is not hovered");
        }

        // The journal: panel edits reach the snapshot, replay onto the live
        // axes, and undo.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->fill(V{0, 1}, V{1, 2});
            auto& fi = detail::FigureAccess::impl(*fig);
            AxesEdit ae;
            ae.plot_ops.push_back(PlotCellEdit{PlotKind::Fill, 0, 1, 1, 7.0});
            ae.plot_styles.push_back(PlotStyleEdit{0, -1, FillOptions{.orientation = Orientation::Vertical,
                                                                     .color = Color::Red}});
            FigureEdits e;
            e.per_axes.push_back({1, ae});
            const FigureEdits undo = fi.apply_edits(e);
            const auto snap = fi.snapshot_box.load();
            const RenderSnapshot& r = *snap->axes[0].snap2d();
            check(r.fills[0].y1[1] == 7.0 && r.fills[0].vertical(), "journal: the edits reach the snapshot");
            fi.apply_edits_and_publish();
            check(ax->fill_data(0).y1[1] == 7.0 && ax->xlim().hi > 7.0,
                  "journal: and replay onto the live axes, the fill now vertical");
            fi.apply_edits(undo);
            const auto u = fi.snapshot_box.load();
            check(u->axes[0].snap2d()->fills[0].y1[1] == 2.0 && !u->axes[0].snap2d()->fills[0].vertical(),
                  "journal: undo restores the cell and the orientation");
        }

        // The Data panel draws a fill's tab (Appearance with its outline block,
        // table) with no duplicate widget ids.
        {
            FigureSnapshot fs;
            RenderSnapshot r;
            FillPlot fp = fill_of(V{0, 1}, V{1, 2}, V{0, 0}, kGreen, 5);
            fp.opts.linewidth = 2.0f;
            fp.opts.edgecolor = kRed;
            fp.exclude = V{0, 0};
            r.fills.push_back(fp);
            fs.axes.push_back({{1, 1, 1}, std::move(r)});
            fs.generation = fs.data_generation = 1;
            const IdConflictScan sc = scan_panel_for_id_conflicts(fs, &draw_data_window, "Data", {});
            check(sc.conflicts == 0 && sc.probes > 0, "panel: a fill's tab draws without id conflicts");
        }

        // A plane: the triangles and the outline in the plane.
        {
            PlaneSnapshot p;
            p.orient = PlaneOrientation::XY;
            FillPlot fp = fill_of(V{0, 2}, V{1, 3}, V{0, 0}, kGreen, 1);
            fp.opts.linewidth = 1.0f;
            p.sheet.fills.push_back(fp);
            const PlaneGeometry g = plane_geometry(p);
            double area = 0.0;
            for (const auto& t : g.tris)
                area += 0.5 * std::abs((t.p[1].x - t.p[0].x) * (t.p[2].y - t.p[0].y) -
                                       (t.p[2].x - t.p[0].x) * (t.p[1].y - t.p[0].y));
            check(g.tris.size() == 2 && near(area, 4.0, 1e-12) && g.segs.size() == 4 && plane_has_data(p),
                  "plane: the fill's triangles and its closed outline");
        }
    }

    // -------------------------------------------------------------------------
    // Rendered: GL against SVG, crossings, translucency, outline, call order
    // -------------------------------------------------------------------------
    // --- stack(): the baselines against matplotlib, the layers as fills -------
    static void fill_stack_checks() {
        const V x{0.0, 1.0, 2.0, 3.0, 4.0};
        const std::vector<V> ys{{1, 2, 3, 2, 1}, {2, 1, 0.5, 3, 4}, {0.5, 0.5, 2, 1, 3}};
        // matplotlib 3.10.6 stackplot(x, *ys, baseline=...): the curves its
        // fill_between() calls receive, the baseline first.
        const std::vector<std::pair<StackBaseline, std::vector<V>>> want{
            {StackBaseline::Zero,
             {{0.0, 0.0, 0.0, 0.0, 0.0}, {1.0, 2.0, 3.0, 2.0, 1.0}, {3.0, 3.0, 3.5, 5.0, 5.0}, {3.5, 3.5, 5.5, 6.0, 8.0}}},
            {StackBaseline::Sym,
             {{-1.75, -1.75, -2.75, -3.0, -4.0}, {-0.75, 0.25, 0.25, -1.0, -3.0}, {1.25, 1.25, 0.75, 2.0, 1.0},
              {1.75, 1.75, 2.75, 3.0, 4.0}}},
            {StackBaseline::Wiggle,
             {{-1.9166666666666667, -2.25, -3.0833333333333335, -3.3333333333333335, -3.3333333333333335},
              {-0.9166666666666667, -0.25, -0.08333333333333348, -1.3333333333333335, -2.3333333333333335},
              {1.0833333333333333, 0.75, 0.4166666666666665, 1.6666666666666665, 1.6666666666666665},
              {1.5833333333333333, 1.25, 2.4166666666666665, 2.6666666666666665, 4.666666666666666}}},
            {StackBaseline::WeightedWiggle,
             {{-1.75, -1.3214285714285716, -2.525974025974026, -2.900974025974026, -4.838474025974026},
              {-0.75, 0.6785714285714284, 0.4740259740259738, -0.9009740259740262, -3.838474025974026},
              {1.25, 1.6785714285714284, 0.9740259740259738, 2.099025974025974, 0.1615259740259738},
              {1.75, 2.1785714285714284, 2.974025974025974, 3.099025974025974, 3.161525974025974}}},
        };
        auto same = [](const V& a, const V& b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (!near(a[i], b[i], 1e-12)) return false;
            return true;
        };
        const char* names[] = {"Zero", "Sym", "Wiggle", "WeightedWiggle"};
        for (std::size_t b = 0; b < want.size(); ++b) {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->stack(x, ys, {.baseline = want[b].first});
            bool ok = ax->fill_count() == 3;
            for (std::size_t k = 0; ok && k < 3; ++k) {
                const FillData d = ax->fill_data(k);
                ok = d.x == x && same(d.y1, want[b].second[k]) && same(d.y2, want[b].second[k + 1]) &&
                     d.exclude.empty();
            }
            check(ok, std::string("stack: ") + names[b] + " baseline, layer k between curves k and k+1 as matplotlib's");
        }

        // Options: each layer a fill with the shared options, its own color and
        // name; the cycle when no colors are given, as three fill_between() calls.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->stack(x, ys, {.orientation = Orientation::Vertical, .names = {"a", "b", "c"}, .alpha = 0.5f,
                              .linewidth = 1.5f, .drawstyle = DrawStyle::StepsMid, .show_legend = false});
            ax->stack(x, std::span(ys).first(2), {.colors = {kRed, kGreen}});
            auto ref = Figure::create();
            auto rx = ref->add_subplot(1, 1, 1);
            for (int k = 0; k < 3; ++k) rx->fill(x, ys[0]);
            const FigureSnapshot fs = detail::FigureAccess::impl(*fig).build_figure_snapshot();
            const FigureSnapshot rs = detail::FigureAccess::impl(*ref).build_figure_snapshot();
            const RenderSnapshot& s = *fs.axes[0].snap2d();
            const RenderSnapshot& r = *rs.axes[0].snap2d();
            bool cyc = true, opts = true;
            for (std::size_t k = 0; k < 3; ++k) {
                const Color& a = *s.fills[k].opts.color;
                const Color& c = *r.fills[k].opts.color;
                cyc = cyc && a.r == c.r && a.g == c.g && a.b == c.b && a.a == c.a;
                const FillOptions& o = s.fills[k].opts;
                opts = opts && o.orientation == Orientation::Vertical && o.alpha == 0.5f && o.linewidth == 1.5f &&
                       o.drawstyle == DrawStyle::StepsMid && !o.show_legend && o.name == std::string(1, static_cast<char>('a' + k));
            }
            check(s.fills.size() == 5 && cyc, "stack: no colors, one cycle color per layer");
            check(opts, "stack: every layer takes the shared options and its own name");
            check(s.fills[3].opts.color->r == kRed.r && s.fills[4].opts.color->g == kGreen.g && s.fills[3].opts.name.empty(),
                  "stack: colors, one per layer; no names, none");
            auto vf = Figure::create();
            auto vx = vf->add_subplot(1, 1, 1);
            vx->stack(x, ys, {.orientation = Orientation::Vertical});
            check(vx->xlim().hi >= 8.0 && vx->ylim().hi <= 4.5, "stack: Vertical stacks along x");
        }

        // Every check before anything is added.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const std::vector<V> bad{{1, 2, 3, 2, 1}, {1, 2}};
            check(throws_invalid([&] { ax->stack(x, bad); }) &&
                      throws_invalid([&] { ax->stack(x, ys, {.colors = {kRed}}); }) &&
                      throws_invalid([&] { ax->stack(x, ys, {.names = {"a", "b"}}); }) && ax->fill_count() == 0,
                  "stack: a layer of another length, or colors/names not one per layer, throw and add nothing");
            ax->stack(x, std::span<const V>{});
            check(ax->fill_count() == 0, "stack: no layers, no fills");
        }

        // NaN carries up the stack; with a centred baseline it reaches every layer.
        {
            std::vector<V> n = ys;
            n[1][2] = std::nan("");
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->stack(x, n);
            ax->stack(x, n, {.baseline = StackBaseline::Sym});
            check(!std::isnan(ax->fill_data(0).y2[2]) && std::isnan(ax->fill_data(1).y2[2]) &&
                      std::isnan(ax->fill_data(2).y1[2]) && std::isnan(ax->fill_data(2).y2[2]) &&
                      !std::isnan(ax->fill_data(1).y2[3]),
                  "stack: Zero, a NaN leaves its point out of its layer and those above");
            check(std::isnan(ax->fill_data(3).y1[2]) && std::isnan(ax->fill_data(3).y2[2]) &&
                      !std::isnan(ax->fill_data(3).y1[3]),
                  "stack: Sym, out of every layer");

            // Afterwards the layers are ordinary fills: an edit does not restack.
            ax->set_fill_data(0, x, V(5, 0.0), V(5, 9.0));
            check(ax->fill_data(1).y1 == V({1.0, 2.0, 3.0, 2.0, 1.0}), "stack: editing a layer leaves the others as they were");
        }

        // Planes, and a legend entry per named layer.
        {
            auto f3 = Figure::create();
            auto a3 = f3->add_subplot3d(1, 1, 1);
            auto pl = a3->plane(PlaneOrientation::XY, 0.0);
            pl->stack(x, ys, {.baseline = StackBaseline::Sym});
            check(pl->fill_count() == 3 && same(pl->fill_data(2).y2, want[1].second[3]), "plane: stack()");

            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->stack(x, ys, {.names = {"a", "b", "c"}});
            const FigureSnapshot fs = detail::FigureAccess::impl(*fig).build_figure_snapshot();
            const auto e = collect_legend_entries(*fs.axes[0].snap2d());
            check(e.size() == 3 && e[0].name == "a" && e[2].name == "c" && e[1].kind == LegendKind::Bar,
                  "legend: a patch per layer, in order");
        }
    }

    void test_fill_between_rendered() {
        std::printf("\n[2D fill_between, rendered]\n");

        const int W = 400, H = 320;
        // Over 0..10 x 0..10, in call order: a red fill between y1 = 2 + x/2
        // and y2 = 6 - x/2 (they cross at x = 4), with x = 8 excluded; a
        // translucent blue band over y in [7, 9] for x in [0, 10] with a green
        // dashed outline; a black line at y = 8, drawn over the band.
        FigureSnapshot fs;
        {
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            RenderSnapshot& s = *fa.snap2d();
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            V x, y1, y2;
            for (int i = 0; i <= 10; ++i) {
                x.push_back(i);
                y1.push_back(2.0 + 0.5 * i);
                y2.push_back(6.0 - 0.5 * i);
            }
            FillPlot red = fill_of(x, y1, y2, kRed, 1);
            red.exclude = V(11, 0.0);
            red.exclude.mut()[8] = 1.0;
            s.fills.push_back(red);
            FillPlot band = fill_of(V{0, 5, 10}, V{9, 9, 9}, V{7, 7, 7}, kBlue, 2);
            band.opts.alpha = 0.5f;
            band.opts.linewidth = 2.0f;
            band.opts.edgecolor = kGreen;
            band.opts.linestyle = LineStyle::Dashed;
            s.fills.push_back(band);
            LinePlot lp;
            lp.x = V{0.0, 10.0};
            lp.y = V{8.0, 8.0};
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
            export_figure_png(dev, data, fs, "fill_between.png", W, H, 1);
            export_figure_svg(fs, "fill_between.svg", W, H);
        }
        const CellLayout cell = compute_figure_layout(fs, W, H).cells[0];
        const CoordTransform& tr = cell.tr;
        const std::string svg = read_file("fill_between.svg");
        const Png png("fill_between.png");

        // SVG: one polygon per region; the band's carries the dashed outline.
        const auto reds = elements(svg, "polygon", "fill=\"rgb(204,0,0)\"");
        const auto blues = elements(svg, "polygon", "fill=\"rgb(0,0,204)\"");
        check(reds.size() == 2 && reds[0].find("fill-opacity") == std::string::npos,
              "svg: the excluded point splits the red fill in two polygons, opaque (" +
                  std::to_string(reds.size()) + ")");
        check(blues.size() == 1 && blues[0].find("fill-opacity=\"0.5\"") != std::string::npos &&
                  blues[0].find("stroke=\"rgb(0,153,0)\"") != std::string::npos &&
                  blues[0].find("stroke-dasharray") != std::string::npos,
              "svg: the band at half opacity, its outline green and dashed");
        const std::size_t at_red = svg.find("fill=\"rgb(204,0,0)\""), at_blue = svg.find("fill=\"rgb(0,0,204)\""),
                          at_line = svg.find("<polyline");
        check(at_red < at_blue && at_blue < at_line, "svg: in call order, fill, band, line");

        // GL: the red fill between the curves, pinched at the crossing.
        check(png.is(tr.to_px(1.0), tr.to_py(4.0), 204, 0, 0) && png.is(tr.to_px(6.5), tr.to_py(4.0), 204, 0, 0),
              "gl: the fill lies between the curves on both sides of the crossing");
        check(!png.is(tr.to_px(1.0), tr.to_py(1.0), 204, 0, 0) && !png.is(tr.to_px(4.0), tr.to_py(5.5), 204, 0, 0) &&
                  !png.is(tr.to_px(4.0), tr.to_py(2.5), 204, 0, 0),
              "gl: and not outside them, nor beside the crossing");
        check(!png.is(tr.to_px(7.5), tr.to_py(4.0), 204, 0, 0) && !png.is(tr.to_px(8.5), tr.to_py(4.0), 204, 0, 0) &&
                  png.is(tr.to_px(9.5), tr.to_py(4.0), 204, 0, 0),
              "gl: nothing either side of the excluded point, the region after it filled");

        // The translucent band: one even color inside (no seams where its
        // triangles meet), the line over it.
        const unsigned char* ref = png.at(tr.to_px(2.0), tr.to_py(7.5));
        bool even = ref && std::abs(ref[2] - 230) < 40 && ref[0] > 90;
        for (float px = tr.to_px(0.5f); even && px < tr.to_px(9.5f); px += 1.0f) {
            const unsigned char* p = png.at(px, tr.to_py(7.5));
            even = p && std::abs(p[0] - ref[0]) <= 2 && std::abs(p[1] - ref[1]) <= 2 && std::abs(p[2] - ref[2]) <= 2;
        }
        check(even, "gl: the translucent band is one even color across its triangles");
        check(png.is(tr.to_px(5.0), tr.to_py(8.0), 0, 0, 0), "gl: the line drawn after the band shows over it");
        int on = 0, off = 0;
        for (float px = tr.to_px(1.0); px < tr.to_px(9.0); px += 1.0f) {
            if (png.is(px, tr.to_py(9.0), 0, 153, 0, 60)) ++on;
            else ++off;
        }
        check(on > 20 && off > 20, "gl: the band's top edge is a dashed green outline (" + std::to_string(on) +
                                       " on, " + std::to_string(off) + " off)");
    }
} // namespace lt
