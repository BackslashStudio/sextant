// Polar axes' markers and bars: PolarAxes::scatter(), scatter_z() and bar() --
// automatic r limits over them, a bar's ring sectors, the sheet's mapping
// (points dropped outside the frame, bars as wedges keeping id and zorder),
// the marker clip in GL, SVG structure with the colorbar, its place beside the
// θ labels, hover and picks, read-back, set_*_data and cla. Part of
// sextant_layout_test.
#include "layout_test.h"
#include "figure_impl.h"
#include "polar_geometry.h"
#include "plot_data_view.h"
#include "render_frame.h"

#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;
        constexpr double pi = std::numbers::pi;

        bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

        template <class F>
        bool throws_invalid(F&& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        std::size_t count(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }

        FigureSnapshot snapshot(Figure& fig) { return detail::FigureAccess::impl(fig).build_figure_snapshot(); }

        std::string read_file(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }

        void auto_limits() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->scatter(V{0.0, 1.0}, V{0.5, 2.3});
            check(near(pa->rlim().lo, 0) && near(pa->rlim().hi, 2.5), "rlim: a scatter's r counts");
            pa->set_scatter_visible(0, false);
            check(near(pa->rlim().lo, 0) && near(pa->rlim().hi, 1), "rlim: a hidden scatter doesn't");
            pa->scatter_z(V{0.0, 1.0}, V{1.0, 3.6}, V{0.0, 1.0});
            check(near(pa->rlim().hi, 4), "rlim: a scatter_z's r counts");
            pa->set_scatter_z_visible(0, false);
            pa->bar(V{0.0, pi}, V{3.0, 1.0}, {.bottom = {1.0}});
            const Range rl = pa->rlim();
            check(near(rl.lo, 0) && rl.hi >= 4 && rl.hi < 5, "rlim: a bar's tip (bottom + height) counts");
            pa->set_bar_visible(0, false);
            pa->bar(V{0.0}, V{-2.0}, {.bottom = {-1.0}});
            check(near(pa->rlim().lo, -3) && near(pa->rlim().hi, -1), "rlim: a bar below 0 spans its base and tip");
        }

        void bar_sectors() {
            PolarMap m;
            std::vector<PolarBarSector> out;
            polar_bar_sectors(m, 0.0, pi / 2, 0.25, 0.75, out);
            check(out.size() == 1 && near(out[0].inner, 0.25) && near(out[0].outer, 0.75)
                      && near(out[0].theta1, -45) && near(out[0].theta2, 45),
                  "bar: one ring sector, θ ± width/2, r base to tip");

            out.clear();
            m.offset = pi / 2;
            m.dir = -1.0;
            polar_bar_sectors(m, 0.0, pi / 2, 0.0, 1.0, out);
            check(out.size() == 1 && near(out[0].inner, 0.0) && near(out[0].theta1, 45) && near(out[0].theta2, 135),
                  "bar: zero at N, clockwise, a fan from the centre");

            out.clear();
            m = PolarMap{};
            polar_bar_sectors(m, 0.0, 1.0, -1.0, 0.5, out);
            check(out.size() == 1 && near(out[0].inner, 0.0) && near(out[0].outer, 0.5), "bar: r cut to the limits");
            out.clear();
            polar_bar_sectors(m, 0.0, 1.0, 2.0, 3.0, out);
            check(out.empty(), "bar: past rmax, nothing");
            out.clear();
            polar_bar_sectors(m, 0.0, 1.0, 0.5, 0.5, out);
            check(out.empty(), "bar: no height, nothing");
            out.clear();
            polar_bar_sectors(m, 1.0, 10.0, 0.5, 1.0, out);
            check(out.size() == 1 && out[0].theta2 - out[0].theta1 >= 360.0 - 1e-9, "bar: wider than a turn is the ring");

            // A sector: θ in [0, π].
            m.tmin = 0.0;
            m.tmax = pi;
            out.clear();
            polar_bar_sectors(m, 0.0, pi / 2, 0.0, 1.0, out);
            check(out.size() == 1 && near(out[0].theta1, 0) && near(out[0].theta2, 45),
                  "bar: cut at the sector's start");
            out.clear();
            polar_bar_sectors(m, 2.0 * pi + pi / 2, pi / 2, 0.0, 1.0, out);
            check(out.size() == 1 && near(out[0].theta1, 45) && near(out[0].theta2, 135),
                  "bar: θ taken modulo 2π on a sector");
            out.clear();
            polar_bar_sectors(m, 1.5 * pi, pi / 4, 0.0, 1.0, out);
            check(out.empty(), "bar: outside the sector, nothing");
            // A sector of 0..300°: a bar across its start comes in two pieces.
            m.tmax = 300.0 * kPolarDeg;
            out.clear();
            polar_bar_sectors(m, 0.0, 200.0 * kPolarDeg, 0.0, 1.0, out);
            check(out.size() == 2, "bar: across a wide sector's start, two pieces");
        }

        void sheet_mapping() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->set_rlim(0, 2);
            pa->set_thetalim(0, 180);
            // In; r past rmax; θ outside the sector; in.
            pa->scatter(V{0.5, 1.0, 4.0, 2.0}, V{1.0, 3.0, 1.0, 0.5}, {.sizes = {5, 6, 7, 8}, .zorder = 3});
            pa->scatter_z(V{0.5, 1.0, 4.0}, V{1.0, 1.5, 1.0}, V{10, 20, 30});
            pa->bar(V{0.5, 2.0}, V{1.0, 1.5}, {.zorder = -1});
            const FigureSnapshot fs = snapshot(*fig);
            const RenderSnapshot& s = *fs.axes[0].snap2d();
            const PolarMap m = polar_map(*fs.axes[0].snappolar());
            const RenderSnapshot sh = polar_sheet(s, m);
            const ScatterPlot& sp = sh.scatters[0];
            check(sp.x.size() == 2 && sp.sizes.size() == 2 && near(sp.sizes[0], 5) && near(sp.sizes[1], 8),
                  "sheet: points outside dropped, sizes kept in step");
            check(near(sp.x[1], m.unit_x(2.0, 0.5)) && near(sp.y[1], m.unit_y(2.0, 0.5)),
                  "sheet: a point at its unit position");
            check(sp.id == s.scatters[0].id && near(sp.opts.zorder, 3), "sheet: a scatter keeps id and zorder");
            const ScatterZPlot& zp = sh.scatter_z[0];
            check(zp.x.size() == 2 && zp.z.size() == 2 && near(zp.z[0], 10) && near(zp.z[1], 20),
                  "sheet: scatter_z's z kept in step");
            check(sh.bars.empty() && sh.wedges.size() == 2, "sheet: each bar one wedge, no bars left");
            bool kept = true;
            for (const WedgePlot& w: sh.wedges)
                kept &= w.id == s.bars[0].id && near(w.opts.zorder, -1) && w.opts.color && s.bars[0].opts.color
                        && w.opts.color->r == s.bars[0].opts.color->r && w.opts.color->g == s.bars[0].opts.color->g;
            check(kept, "sheet: wedges keep the bar's id, zorder and color");
            const auto order = draw_order(sh);
            check(!order.empty() && order.front().kind == PlotKind::Wedge && order.back().kind == PlotKind::Scatter,
                  "sheet: draw order still by zorder (bars under, scatter over)");
        }

        void gl_marker_clip() {
            constexpr int W = 400, H = 400;
            auto fig = Figure::create();
            fig->resize(W, H);
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->set_rlim(0, 1);
            pa->grid(false);
            pa->scatter(V{0.0}, V{1.0}, {.color = Color::Red, .size = 40.0f});
            const FigureSnapshot fs = snapshot(*fig);
            const FigureLayout fl = compute_figure_layout(fs, W, H);
            const PolarPlan& pl = *fl.cells[0].polar;
            const RgbaImage img = fig->render_rgba({}, W, H);
            const float k = img.width / static_cast<float>(W);
            const auto red = [&](float x, float y) {
                const int px = static_cast<int>(x * k), py = static_cast<int>(y * k);
                if (px < 0 || py < 0 || px >= img.width || py >= img.height) return false;
                const std::uint8_t* p = &img.pixels[(static_cast<std::size_t>(py) * img.width + px) * 4];
                return p[0] > 180 && p[1] < 120 && p[2] < 120;
            };
            const float x = pl.cx + pl.radius, y = pl.cy;
            check(red(x - 10.0f, y), "gl: the marker's half inside the rim is drawn");
            check(!red(x + 10.0f, y), "gl: its half outside the rim is clipped");
        }

        void svg_and_layout() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            V th, h;
            for (int i = 0; i < 8; ++i) {
                th.push_back(i * pi / 4);
                h.push_back(1.0 + i % 3);
            }
            pa->bar(th, h, {.width = 1.0f, .name = "rose"});
            pa->scatter_z(V{0.3, 1.3, 2.3}, V{1.0, 2.0, 2.5}, V{0.0, 0.5, 1.0},
                          {.cmap = Colormap::Plasma, .colorbar = true});
            pa->scatter(V{4.0}, V{1.5}, {.name = "pt"});
            pa->legend();
            fig->savefig_svg("polar_kinds.svg");
            fig->savefig_png("polar_kinds.png");
            const std::string svg = read_file("polar_kinds.svg");
            check(count(svg, "colorbarGrad0_0") >= 2, "svg: the colorbar's gradient defined and used");
            check(count(svg, "fill-rule=\"evenodd\"") >= 8, "svg: each bar a path");
            const std::size_t clip = svg.find("clip-path=\"url(#plotArea0)\"");
            const std::size_t g_end = svg.find("</g>", clip);
            const std::size_t first_bar = svg.find("fill-rule=\"evenodd\"", clip);
            const std::size_t marker = svg.find("<circle", clip);
            check(clip != std::string::npos && first_bar < g_end && marker < g_end,
                  "svg: bars and markers inside the polar clip");

            const FigureSnapshot fs = snapshot(*fig);
            const FigureLayout fl = compute_figure_layout(fs, 640, 480);
            const CellLayout& c = fl.cells[0];
            check(c.colorbars.size() == 1, "layout: the polar cell has its colorbar");
            float label_r = 0.0f;
            for (const PolarGridLine& g: c.polar->spokes) label_r = std::max(label_r, g.lx);
            check(!c.colorbars.empty() && c.colorbars[0].rect.x > label_r + 8.0f
                      && c.colorbars[0].rect.x > c.polar->cx + c.polar->radius,
                  "layout: the colorbar clears the θ labels");
        }

        void hover_and_picks() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->set_rlim(0, 4);
            pa->bar(V{0.0, pi / 2}, V{2.0, 3.0}, {.bottom = {1.0}, .hint_labels = {"east", "north"}});
            pa->scatter(V{pi}, V{2.0});
            pa->scatter_z(V{3 * pi / 2}, V{3.0}, V{0.25});
            const FigureSnapshot fs = snapshot(*fig);
            const FigureLayout fl = compute_figure_layout(fs, 500, 500);
            const std::vector<AxesLayout> layout = axes_layouts(fl);
            const RenderSnapshotPolar& s = *fs.axes[0].snappolar();
            const PolarMap m = polar_map(s);
            const CoordTransform& tr = layout[0].tr;
            const auto at = [&](double t, double r, float& x, float& y) {
                x = tr.to_px(m.unit_x(t, r));
                y = tr.to_py(m.unit_y(t, r));
            };
            float x, y;
            at(pi, 2.0, x, y);
            auto hit = find_pick(s, tr, x + 1.0f, y);
            check(hit && hit->kind == PickKind::Scatter && hit->element == 0, "pick: the scatter point");
            std::string text = hit ? format_pick(s, *hit) : "";
            check(text == "theta=180\xC2\xB0, r=2", "hover: a polar marker (" + text + ")");

            at(3 * pi / 2, 3.0, x, y);
            hit = find_pick(s, tr, x, y + 1.0f);
            text = hit ? format_pick(s, *hit) : "";
            check(hit && hit->kind == PickKind::ScatterZ && text == "theta=270\xC2\xB0, r=3, z=0.25",
                  "hover: a polar scatter_z (" + text + ")");

            at(pi / 2 + 0.1, 3.5, x, y);
            hit = find_pick(s, tr, x, y);
            text = hit ? format_pick(s, *hit) : "";
            check(hit && hit->kind == PickKind::Bar && hit->element == 1
                      && text == "theta=90\xC2\xB0, height=3, bottom=1\nnorth",
                  "hover: inside a bar (" + text + ")");
            at(-0.2, 2.0, x, y);
            hit = find_pick(s, tr, x, y);
            check(hit && hit->kind == PickKind::Bar && hit->element == 0, "pick: a bar across θ 0 (modulo 2π)");
            at(pi / 2, 0.5, x, y);
            check(!find_pick(s, tr, x, y), "pick: under a bar's bottom, nothing");
            at(pi / 4, 2.0, x, y);
            check(!find_pick(s, tr, x, y), "pick: between bars, nothing");
        }

        void read_back() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            check(throws_invalid([&] { pa->bar(V{0.0}, V{1.0}, {.orientation = Orientation::Horizontal}); }),
                  "checks: a horizontal polar bar throws");
            pa->scatter(V{0.0, 1.0}, V{1.0, 2.0}, {.sizes = {3, 4}});
            pa->scatter_z(V{0.0}, V{1.0}, V{5.0});
            pa->bar(V{0.0, 1.0, 2.0}, V{1.0, 2.0, 3.0});
            check(pa->scatter_count() == 1 && pa->scatter_data(0).x == V({0.0, 1.0})
                      && pa->scatter_data(0).sizes == V({3, 4}),
                  "read-back: scatter_data is θ, r, sizes");
            check(pa->scatter_z_count() == 1 && pa->scatter_z_data(0).z == V({5.0}), "read-back: scatter_z_data");
            const BarData b = pa->bar_data(0);
            check(pa->bar_count() == 1 && b.x == V({0.0, 1.0, 2.0}) && b.height == V({1.0, 2.0, 3.0}),
                  "read-back: bar_data is θ, height");
            pa->set_scatter_data(0, V{2.0, 3.0}, V{0.5, 0.6});
            check(pa->scatter_data(0).y == V({0.5, 0.6}) && pa->scatter_data(0).sizes == V({3, 4}),
                  "read-back: set_scatter_data keeps sizes for the same count");
            pa->set_scatter_z_data(0, ScatterZData{{1.0}, {2.0}, {7.0}, {}});
            check(pa->scatter_z_data(0).z == V({7.0}), "read-back: set_scatter_z_data");
            const double w0 = snapshot(*fig).axes[0].snap2d()->bars[0].bar_width;
            pa->set_bar_data(0, V{0.0, 0.5, 1.0}, V{1.0, 1.0, 1.0});
            const double w1 = snapshot(*fig).axes[0].snap2d()->bars[0].bar_width;
            check(near(w0, 0.8, 1e-6) && near(w1, 0.4, 1e-6),
                  "read-back: set_bar_data re-derives the width from the spacing");
            pa->set_bar_visible(0, false);
            check(!pa->bar_visible(0) && pa->scatter_visible(0) && pa->scatter_z_visible(0), "read-back: visible");
            pa->set_colorbar_style({.width = 20.0f});
            check(near(snapshot(*fig).axes[0].snap2d()->colorbar_opts.width, 20.0), "read-back: set_colorbar_style");

            const std::vector<PlotDataTable> t = collect_plot_data_tables(*snapshot(*fig).axes[0].snappolar());
            bool named = false;
            for (const PlotDataTable& d: t)
                if (d.kind == PlotKind::Bar) named = d.columns[0].name == std::string("theta");
            check(named, "data panel: a polar bar's centres read theta");

            pa->cla();
            check(pa->scatter_count() == 0 && pa->scatter_z_count() == 0 && pa->bar_count() == 0,
                  "cla: clears markers and bars");
        }
    } // namespace

    void test_polar_kinds() {
        auto_limits();
        bar_sectors();
        sheet_mapping();
        gl_marker_clip();
        svg_and_layout();
        hover_and_picks();
        read_back();
    }
} // namespace lt
