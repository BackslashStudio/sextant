// Horizontal bars and bar bases: BarOptions::orientation and
// BarOptions::bottom. The call's length check, read-back and set_bar_data,
// limits (stacked, lying, negative, error bars at the tip, log), hist, hover,
// the Data panel's columns and its row, cell and orientation edits, GL
// against SVG, the caches' orientation key, planes.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "describe.h"
#include "hint.h"
#include "hint_index.h"
#include "plot_data_view.h"
#include <cmath>

namespace lt {
    using namespace sextant;

    namespace {
        // `centers`/`heights`/`bottom` as one bar series, red with no outline.
        BarPlot bars_of(std::vector<double> centers, std::vector<double> heights,
                        std::vector<double> bottom, Orientation o) {
            BarPlot bp;
            bp.centers = std::move(centers);
            bp.heights = std::move(heights);
            bp.bottom = std::move(bottom);
            bp.bar_width = 0.8;
            bp.opts.orientation = o;
            bp.opts.color = Color{0.8f, 0.0f, 0.0f, 1.0f};
            bp.opts.linewidth = 0.0f;
            bp.id = 1;
            return bp;
        }

        DataBounds bounds_of(const BarPlot& bp) {
            const std::vector<LinePlot> no_l;
            const std::vector<ScatterPlot> no_s;
            const std::vector<HeatmapPlot> no_h;
            const std::vector<ScatterZPlot> no_z;
            const std::vector<BarPlot> b{bp};
            return auto_scale(AllPlotData{no_l, no_s, b, no_h, no_z}, 0.0);
        }

        bool box_is(const DataBounds& b, double x0, double x1, double y0, double y1) {
            return std::abs(b.xmin - x0) < 1e-12 && std::abs(b.xmax - x1) < 1e-12 &&
                   std::abs(b.ymin - y0) < 1e-12 && std::abs(b.ymax - y1) < 1e-12;
        }

        std::string box_str(const DataBounds& b) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "x %g..%g, y %g..%g", b.xmin, b.xmax, b.ymin, b.ymax);
            return buf;
        }

        // One 2D axes over fixed 0..10 limits holding `bp`.
        FigureSnapshot one_axes(BarPlot bp, unsigned long long gen = 1) {
            FigureSnapshot fs;
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            RenderSnapshot& s = *fa.snap2d();
            s.xmin = 0.0;
            s.xmax = 10.0;
            s.xlim_auto = false;
            s.ymin = 0.0;
            s.ymax = 10.0;
            s.ylim_auto = false;
            s.bars.push_back(std::move(bp));
            fs.axes.push_back(std::move(fa));
            fs.generation = fs.data_generation = gen;
            return fs;
        }

        std::string read_file(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        struct SvgRect {
            float x = 0, y = 0, w = 0, h = 0;
        };

        float attr(const std::string& el, const char* name) {
            const std::string key = std::string(" ") + name + "=\"";
            const std::size_t p = el.find(key);
            return p == std::string::npos ? NAN : std::stof(el.substr(p + key.size()));
        }

        // Every <rect> filled `fill`, in document order.
        std::vector<SvgRect> rects_filled(const std::string& svg, const std::string& fill) {
            std::vector<SvgRect> out;
            for (std::size_t p = svg.find("<rect "); p != std::string::npos; p = svg.find("<rect ", p + 1)) {
                const std::string el = svg.substr(p, svg.find('>', p) - p);
                if (el.find("fill=\"" + fill + "\"") == std::string::npos) continue;
                out.push_back({attr(el, "x"), attr(el, "y"), attr(el, "width"), attr(el, "height")});
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
            bool red(float x, float y) const {
                const unsigned char* p = at(x, y);
                return p && p[0] > 150 && p[1] < 80 && p[2] < 80;
            }
            bool dark(float x, float y) const {
                const unsigned char* p = at(x, y);
                return p && p[0] < 90 && p[1] < 90 && p[2] < 90;
            }
        };

        template <class F>
        bool throws_invalid(F&& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
    } // namespace

    static void bar_orient_checks();

    void test_bar_orientation() {
        std::printf("\n[2D bars: orientation and bottom]\n");
        try {
            bar_orient_checks();
        } catch (const std::exception& e) {
            check(false, std::string("bar orientation: unexpected exception: ") + e.what());
        }
    }

    static void bar_orient_checks() {
        const std::vector<double> x3{1.0, 2.0, 3.0}, h3{1.0, 2.0, 3.0};

        // --- The call, read-back and set_bar_data ------------------------------
        {
            auto ax = Figure::create()->add_subplot(1, 1, 1);
            ax->bar(x3, h3, {.bottom = {5.0}});
            check(ax->bar_data(0).bottom == std::vector<double>{5.0, 5.0, 5.0},
                  "call: one bottom value is every bar's, read back one per bar");
            ax->bar(x3, h3, {.bottom = {1.0, 2.0, 3.0}});
            check(ax->bar_data(1).bottom == std::vector<double>{1.0, 2.0, 3.0}, "call: one per bar as given");
            ax->bar(x3, h3);
            check(ax->bar_data(2).bottom.empty(), "call: no bottom reads back empty");
            check(throws_invalid([&] { ax->bar(x3, h3, {.bottom = {1.0, 2.0}}); }) && ax->bar_count() == 3,
                  "call: any other length throws, and nothing is added");

            ax->set_bar_data(1, x3, std::vector<double>{4.0, 4.0, 4.0});
            check(ax->bar_data(1).bottom == std::vector<double>{1.0, 2.0, 3.0},
                  "set_bar_data: the span overload keeps the bases while the count holds");
            ax->set_bar_data(1, std::vector<double>{1.0, 2.0}, std::vector<double>{4.0, 4.0});
            check(ax->bar_data(1).bottom.empty(), "set_bar_data: and drops them when it changes");
            ax->set_bar_data(0, BarData{x3, h3, {7.0}});
            check(ax->bar_data(0).bottom == std::vector<double>{7.0, 7.0, 7.0},
                  "set_bar_data: BarData's one value spreads");
            ax->set_bar_data(0, BarData{x3, h3, {}});
            check(ax->bar_data(0).bottom.empty(), "set_bar_data: BarData's empty bottom clears");
            check(throws_invalid([&] { ax->set_bar_data(0, BarData{x3, {2.0, 2.0, 2.0}, {1.0, 1.0}}); }) &&
                      ax->bar_data(0).height == h3,
                  "set_bar_data: a bad bottom throws and changes nothing");

            // A single bar takes its one value as its own.
            ax->bar(std::vector<double>{1.0}, std::vector<double>{2.0}, {.bottom = {3.0}});
            check(ax->bar_data(3).bottom == std::vector<double>{3.0}, "call: a lone bar's one value");
        }

        // --- Limits: base to tip, either orientation --------------------------
        {
            const BarPlot up = bars_of({1.0, 2.0}, {1.0, 4.0}, {2.0, 3.0}, Orientation::Vertical);
            const DataBounds bu = bounds_of(up);
            check(box_is(bu, 0.6, 2.4, 2.0, 7.0),
                  "limits: stacked bars span base to tip, the 0 baseline not counted (" + box_str(bu) + ")");
            BarPlot lying = up;
            lying.opts.orientation = Orientation::Horizontal;
            const DataBounds bl = bounds_of(lying);
            check(box_is(bl, 2.0, 7.0, 0.6, 2.4), "limits: lying bars swap the axes (" + box_str(bl) + ")");
            const DataBounds bn = bounds_of(bars_of({1.0}, {-2.0}, {5.0}, Orientation::Horizontal));
            check(box_is(bn, 3.0, 5.0, 0.6, 1.4),
                  "limits: a negative length runs below its base (" + box_str(bn) + ")");
            const DataBounds b0 = bounds_of(bars_of({1.0, 2.0}, {1.0, 4.0}, {}, Orientation::Horizontal));
            check(box_is(b0, 0.0, 4.0, 0.6, 2.4), "limits: without bottom the base is 0 (" + box_str(b0) + ")");

            // Error bars hang off the tip; the value errors are x_* when lying.
            BarPlot ex = lying;
            ex.err.x_cap_hi = std::vector<double>{1.0, 1.5};
            const DataBounds be = bounds_of(ex);
            check(box_is(be, 2.0, 8.5, 0.6, 2.4), "limits: a lying bar's x error grows from its tip (" +
                                                      box_str(be) + ")");
            BarPlot ey = up;
            ey.err.y_cap_hi = std::vector<double>{1.0, 1.5};
            const DataBounds bey = bounds_of(ey);
            check(box_is(bey, 0.6, 2.4, 2.0, 8.5), "limits: a stacked bar's y error from its tip (" +
                                                       box_str(bey) + ")");

            // Log: a base of 0 counts for nothing, a set base does.
            auto lx = Figure::create()->add_subplot(1, 1, 1);
            lx->bar(x3, std::vector<double>{1.0, 10.0, 100.0}, {.orientation = Orientation::Horizontal});
            lx->set_xscale(Scale::Log);
            check(near(lx->xlim().lo, std::pow(10.0, -0.1), 1e-9),
                  "limits: lying bars on log x skip the 0 base");
            auto lb = Figure::create()->add_subplot(1, 1, 1);
            lb->bar(x3, std::vector<double>{9.0, 90.0, 990.0},
                    {.orientation = Orientation::Horizontal, .bottom = {10.0}});
            lb->set_xscale(Scale::Log);
            check(near(lb->xlim().lo, std::pow(10.0, 0.9), 1e-9) && near(lb->xlim().hi, std::pow(10.0, 3.1), 1e-9),
                  "limits: and count a set base (10 .. 1000, padded a tenth of the decades)");
        }

        // --- hist: orientation and bottom through bar_opts --------------------
        {
            auto ax = Figure::create()->add_subplot(1, 1, 1);
            const std::vector<double> data{0.0, 1.0, 1.0, 2.0, 2.0, 2.0, 3.0, 3.0, 4.0, 5.0};
            ax->hist(data, 5, {.orientation = Orientation::Horizontal, .bottom = {1.0}, .width = 1.0f});
            const BarData d = ax->bar_data(0);
            const double yspan = ax->ylim().hi - ax->ylim().lo;
            check(d.bottom == std::vector<double>(5, 1.0) && ax->xlim().lo < 1.0 && ax->xlim().lo > 0.5 &&
                      ax->ylim().lo < 0.0 && ax->ylim().hi > 5.0 && yspan < 6.0,
                  "hist: horizontal bins lie along y, counted from a base of 1 along x");
        }

        // --- Hover: the tip, worded by orientation ----------------------------
        {
            const CoordTransform tr{0.0, 10.0, 0.0, 10.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
            RenderSnapshot s;
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            s.bars.push_back(bars_of({2.0, 6.0}, {3.0, 5.0}, {1.0, 1.0}, Orientation::Horizontal));
            const auto h = find_hint(s, tr, tr.to_px(4.0), tr.to_py(2.0));
            check(h && h->text == "y=2, width=3, left=1",
                  "hover: a lying bar at its tip reads y, width and left (got '" + (h ? h->text : "none") + "')");
            check(!find_hint(s, tr, tr.to_px(2.0), tr.to_py(4.0)),
                  "hover: nothing where a standing bar's tip would be");
            s.bars[0].opts.orientation = Orientation::Vertical;
            const auto v = find_hint(s, tr, tr.to_px(2.0), tr.to_py(4.0));
            check(v && v->text == "x=2, height=3, bottom=1",
                  "hover: a standing one reads x, height and bottom (got '" + (v ? v->text : "none") + "')");
            s.bars[0].bottom = std::vector<double>{};
            s.bars[0].opts.orientation = Orientation::Horizontal;
            s.bars[0].err.x_cap_lo = std::vector<double>{0.5, 0.5};
            const auto e = find_hint(s, tr, tr.to_px(3.0), tr.to_py(2.0));
            check(e && e->text == "y=2, width=3 cap \xC2\xB1" "0.5",
                  "hover: a lying bar's x error is its width's (got '" + (e ? e->text : "none") + "')");

            // The bucket grid holds (center, tip) either way, so flipping the
            // orientation with the data generation unchanged still finds the tip.
            RenderSnapshot many;
            std::vector<double> c(kHintIndexMinPoints + 8), w(c.size(), 3.0);
            for (std::size_t i = 0; i < c.size(); ++i) c[i] = 10.0 * static_cast<double>(i) / c.size();
            many.bars.push_back(bars_of(c, w, {}, Orientation::Vertical));
            HintIndexCache cache;
            cache.set_frame_key(9, 0);
            const std::size_t k = c.size() / 2;
            const auto up = find_pick(many, tr, tr.to_px(c[k]), tr.to_py(3.0), &cache);
            many.bars[0].opts.orientation = Orientation::Horizontal;
            const auto side = find_pick(many, tr, tr.to_px(3.0), tr.to_py(c[k]), &cache);
            check(up && side && side->kind == PickKind::Bar &&
                      std::abs(side->anchor_y - tr.to_py(c[k])) < 1.0f && std::abs(side->anchor_x - tr.to_px(3.0)) < 1.0f,
                  "hover: an orientation flip under a cached grid hits the lying tip");
        }

        // --- Data panel: columns, row and cell edits, the orientation edit ----
        {
            RenderSnapshot s;
            s.bars.push_back(bars_of({1.0, 2.0, 3.0}, {1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}, Orientation::Vertical));
            s.bars.push_back(bars_of({1.0, 2.0}, {1.0, 2.0}, {7.0, 8.0}, Orientation::Horizontal));
            s.bars.push_back(bars_of({1.0, 2.0}, {1.0, 2.0}, {}, Orientation::Horizontal));
            const auto t = collect_plot_data_tables(s);
            auto names = [&](std::size_t i) {
                std::string out;
                for (const auto& col: t[i].columns) out += std::string(col.name) + " ";
                return out;
            };
            check(t.size() == 3 && names(0) == "center height bottom " && names(1) == "center width left " &&
                      names(2) == "center width ",
                  "panel: the columns follow the orientation, a base column only when set (" + names(0) + "| " +
                      names(1) + "| " + names(2) + ")");

            RenderSnapshot r = s;
            apply_plot_data_ops(r, {PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Bar, 0, 0, -1}});
            check(r.bars[0].bottom.size() == 4 && r.bars[0].bottom[0] == 4.0, "rows: a new bar copies a base");
            apply_plot_data_ops(r, {PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Bar, 2, 2, -1}});
            check(r.bars[2].bottom.empty(), "rows: a series without bases stays without");
            RenderSnapshot u = s;
            std::vector<PlotDataOp> undo;
            apply_plot_data_ops(u, {PlotRowEdit{PlotRowEdit::Op::Remove, PlotKind::Bar, 0, 1, -1}}, &undo);
            check(u.bars[0].bottom.size() == 2 && u.bars[0].bottom[1] == 6.0, "rows: a remove takes its base");
            apply_plot_data_ops(u, undo);
            check(u.bars[0].bottom.get() == std::vector<double>{4.0, 5.0, 6.0}, "rows: and its undo puts it back");

            RenderSnapshot c = s;
            std::vector<PlotDataOp> cu;
            apply_plot_data_ops(c, {PlotCellEdit{PlotKind::Bar, 0, 2, 1, 9.0}}, &cu);
            check(c.bars[0].bottom[1] == 9.0 && c.bars[0].heights[1] == 2.0, "cells: column 2 is the base");
            apply_plot_data_ops(c, cu);
            check(c.bars[0].bottom[1] == 5.0, "cells: undone");

            // The orientation is an option: a style edit, undone like the rest,
            // and it moves the extent to the other axis.
            RenderSnapshot o = s;
            BarOptions lie = o.bars[0].opts;
            lie.orientation = Orientation::Horizontal;
            std::vector<PlotStyleEdit> inv;
            apply_plot_style_edits(o, {PlotStyleEdit{0, -1, lie}}, &inv);
            const DataBounds flipped = bounds_of(o.bars[0]);
            check(o.bars[0].horizontal() && box_is(flipped, 4.0, 9.0, 0.6, 3.4) &&
                      o.bars[0].bottom.get() == std::vector<double>{4.0, 5.0, 6.0},
                  "edit: an orientation edit lays the bars down, bases kept (" + box_str(flipped) + ")");
            apply_plot_style_edits(o, inv);
            check(!o.bars[0].horizontal(), "edit: and its undo stands them up");
        }

        // --- Planes: the same rects -----------------------------------------
        {
            auto extent = [](const BarPlot& bp) {
                PlaneSnapshot p;
                p.orient = PlaneOrientation::XY;
                p.sheet.bars.push_back(bp);
                const PlaneGeometry g = plane_geometry(p);
                DataBounds b{1e9, -1e9, 1e9, -1e9};
                for (const auto& t: g.tris)
                    for (const Vec3& v: t.p) {
                        b.xmin = std::min(b.xmin, v.x);
                        b.xmax = std::max(b.xmax, v.x);
                        b.ymin = std::min(b.ymin, v.y);
                        b.ymax = std::max(b.ymax, v.y);
                    }
                return b;
            };
            const DataBounds up = extent(bars_of({1.0, 2.0}, {1.0, 4.0}, {2.0, 3.0}, Orientation::Vertical));
            const DataBounds side = extent(bars_of({1.0, 2.0}, {1.0, 4.0}, {2.0, 3.0}, Orientation::Horizontal));
            check(box_is(up, 0.6, 2.4, 2.0, 7.0) && box_is(side, 2.0, 7.0, 0.6, 2.4),
                  "plane: stacked and lying bars fill the same rects as 2D (" + box_str(up) + "; " +
                      box_str(side) + ")");
        }
    }

    // -------------------------------------------------------------------------
    // Rendered: GL fill inside each SVG rect, outside the base; the caches
    // -------------------------------------------------------------------------
    void test_bar_orientation_rendered() {
        std::printf("\n[2D bars: orientation and bottom, rendered]\n");

        const int W = 400, H = 320;
        // Two stacked bars: [4, 6] at x = 3 and [1, 4] at x = 7; error bars of +1.5
        // along x (whiskers across a standing bar, along a lying one).
        auto stacked = [](Orientation o) {
            BarPlot bp = bars_of({3.0, 7.0}, {2.0, 3.0}, {4.0, 1.0}, o);
            bp.err.x_cap_hi = std::vector<double>{1.5, 1.5};
            bp.opts.edgecolor = Color::Black;
            bp.opts.errorbar.linewidth = 3.0f;
            return bp;
        };
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            // One DataRenderer and one data generation: the bar and error-bar
            // caches must key on the orientation.
            for (Orientation o: {Orientation::Vertical, Orientation::Horizontal}) {
                const std::string base = "bar_orient_" + std::string(enum_name(o));
                FigureSnapshot fs = one_axes(stacked(o));
                export_figure_png(dev, data, fs, base + ".png", W, H, 1);
                export_figure_svg(fs, base + ".svg", W, H);
            }
        }
        const CoordTransform tr = compute_figure_layout(one_axes(stacked(Orientation::Vertical)), W, H)
                                      .cells[0].tr;

        for (Orientation o: {Orientation::Vertical, Orientation::Horizontal}) {
            const bool lying = o == Orientation::Horizontal;
            const std::string base = "bar_orient_" + std::string(enum_name(o));
            const auto rects = rects_filled(read_file(base + ".svg"), "rgb(204,0,0)");
            check(rects.size() == 2, "rendered: " + base + ".svg has two bar rects (" +
                                         std::to_string(rects.size()) + ")");
            if (rects.size() != 2) continue;

            // The SVG rects are the data rects.
            const double c[2] = {3.0, 7.0}, lo[2] = {4.0, 1.0}, hi[2] = {6.0, 4.0};
            bool placed = true;
            for (int i = 0; i < 2; ++i) {
                const float x0 = lying ? tr.to_px(lo[i]) : tr.to_px(c[i] - 0.4);
                const float x1 = lying ? tr.to_px(hi[i]) : tr.to_px(c[i] + 0.4);
                const float y0 = lying ? tr.to_py(c[i] + 0.4) : tr.to_py(hi[i]);
                const float y1 = lying ? tr.to_py(c[i] - 0.4) : tr.to_py(lo[i]);
                placed = placed && std::abs(rects[i].x - x0) < 0.01f && std::abs(rects[i].w - (x1 - x0)) < 0.01f &&
                         std::abs(rects[i].y - y0) < 0.01f && std::abs(rects[i].h - (y1 - y0)) < 0.01f;
            }
            check(placed, "rendered: " + base + ".svg's rects run base to tip");

            // GL fills each one and nothing past its base.
            const Png png((base + ".png").c_str());
            int inside = 0, below = 0;
            for (int i = 0; i < 2; ++i) {
                const SvgRect& r = rects[i];
                // A quarter across, clear of a lying bar's whisker along its middle.
                inside += png.red(r.x + (lying ? 0.5f : 0.25f) * r.w, r.y + (lying ? 0.25f : 0.5f) * r.h);
                // 4 px past the base, along the value axis.
                const float bx = lying ? r.x - 4.0f : r.x + 0.5f * r.w;
                const float by = lying ? r.y + 0.5f * r.h : r.y + r.h + 4.0f;
                below += png.red(bx, by);
            }
            check(png.w == W && inside == 2 && below == 0,
                  "rendered: GL fills both " + base + " rects and stops at the base (" + std::to_string(inside) +
                      " in, " + std::to_string(below) + " past)");

            // The whisker runs right from the tip: halfway along it is dark.
            const float wx = lying ? tr.to_px(6.75) : tr.to_px(3.75);
            const float wy = lying ? tr.to_py(3.0) : tr.to_py(6.0);
            check(png.dark(wx, wy), "rendered: " + base + "'s error bar hangs off the tip");
        }
    }
} // namespace lt
