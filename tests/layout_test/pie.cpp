// Pie: Axes::pie() — slice angles, explode, rings, labels and percents
// (autopct), legend, read-back and set_pie_data(), the call's checks, hover and
// picks, the Data panel's edit, and GL against SVG. Part of sextant_layout_test.
#include "layout_test.h"
#include "figure_impl.h"
#include "pie_geometry.h"

#include <array>
#include <cmath>
#include <numbers>

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;

        std::size_t count(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }

        template <class F>
        bool throws_invalid(F&& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

        PiePlot make_plot(V values, PieOptions o = {}, V explode = {}, std::vector<std::string> labels = {}) {
            PiePlot p;
            p.values = std::move(values);
            p.explode = std::move(explode);
            p.labels = std::move(labels);
            p.opts = std::move(o);
            return p;
        }

        void angles() {
            // 1, 1, 2 of 4: a quarter, a quarter, a half, from +x counter-clockwise.
            auto s = pie_slices(make_plot({1, 1, 2}));
            check(s.size() == 3 && near(s[0].theta1, 0) && near(s[0].theta2, 90) && near(s[1].theta1, 90)
                      && near(s[1].theta2, 180) && near(s[2].theta1, 180) && near(s[2].theta2, 360),
                  "angles: shares of the sum, counter-clockwise from +x");
            check(near(s[0].frac, 0.25) && near(s[2].frac, 0.5), "angles: fractions of a turn");

            PieOptions o;
            o.startangle = 90;
            o.counterclock = false;
            s = pie_slices(make_plot({1, 3}, o));
            check(near(s[0].theta1, 0) && near(s[0].theta2, 90) && near(s[1].theta1, -270) && near(s[1].theta2, 0),
                  "angles: startangle and clockwise, as matplotlib");
            check(near(std::cos(s[0].mid), std::cos(std::numbers::pi / 4)), "angles: mid of a clockwise slice");

            PieOptions part;
            part.normalize = false;
            s = pie_slices(make_plot({0.25, 0.25}, part));
            check(near(s[1].theta2, 180), "angles: normalize=false leaves a gap (fractions as given)");

            o = {};
            o.radius = 2;
            s = pie_slices(make_plot({1, 1}, o, {0.5, 0}));
            check(near(s[0].cx, 0, 1e-12) && near(s[0].cy, 1.0) && near(s[1].cx, 0) && near(s[1].cy, 0),
                  "explode: a fraction of the radius along the slice's middle");
        }

        void autopct() {
            check(parse_autopct("%1.1f%%") && format_pct(*parse_autopct("%1.1f%%"), 37.5) == "37.5%",
                  "autopct: matplotlib's %1.1f%%");
            check(format_pct(*parse_autopct("%d%%"), 37.9) == "37%", "autopct: %d truncates as Python's");
            check(format_pct(*parse_autopct("(%.0f)"), 50) == "(50)", "autopct: text around the conversion");
            check(format_pct(*parse_autopct("%i"), 12.2) == "12", "autopct: %i as %d");
            check(parse_autopct("") && parse_autopct("")->conv == 0, "autopct: empty = none");
            for (const char* bad : {"%", "%s", "%f %f", "%100f", "%.123f", "abc", "%n", "%*f"})
                check(!parse_autopct(bad), std::string("autopct: rejects \"") + bad + "\"");
        }

        void checks() {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const auto bad = [&](V v, PieOptions o = {}) { return throws_invalid([&] { ax->pie(v, o); }); };
            check(bad({}), "check: no values");
            check(bad({1, -1}), "check: a negative value");
            check(bad({1, NAN}), "check: a NaN value");
            check(bad({0, 0}), "check: a zero sum");
            check(bad({0.6, 0.6}, PieOptions{.normalize = false}), "check: normalize=false past a whole");
            check(bad({1, 2}, PieOptions{.labels = {"a"}}), "check: labels of another size");
            check(bad({1, 2}, PieOptions{.explode = {0.1}}), "check: explode of another size");
            check(bad({1, 2}, PieOptions{.explode = {0.1, -0.1}}), "check: a negative explode");
            check(bad({1, 2}, PieOptions{.autopct = "%s"}), "check: a bad autopct");
            check(bad({1, 2}, PieOptions{.radius = 0}), "check: radius 0");
            check(bad({1, 2}, PieOptions{.inner = 1}), "check: a hole as wide as the pie");
            check(bad({1, 2}, PieOptions{.pctdistance = INFINITY}), "check: an infinite distance");
            check(ax->pie_count() == 0, "check: nothing added by a refused call");
            ax->pie(V{1, 2});
            check(throws_invalid([&] { ax->set_pie_data(0, PieData{{1, 2}, {"a"}, {}}); }),
                  "check: set_pie_data checks the same");
            check(throws_invalid([&] { ax->set_pie_data(0, PieData{{-1, 2}, {}, {}}); }),
                  "check: set_pie_data rejects a negative value");
        }

        void readback_and_axes() {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->pie(V{3, 1}, PieOptions{.labels = {"a", "b"}, .explode = {0.1, 0}, .name = "P"});
            check(ax->pie_count() == 1, "pie_count");
            PieData d = ax->pie_data(0);
            check(d.values == V{3, 1} && d.labels == std::vector<std::string>{"a", "b"} && d.explode == V{0.1, 0},
                  "pie_data: values, labels, explode as given");
            check(!ax->axis_visible() && near(ax->aspect(), 1.0), "pie(): axis off and aspect 1");
            const Range xl = ax->xlim();
            check(xl.lo < -1.1 && xl.hi > 1.1, "limits: cover the slices and the labels at 1.1 x radius");

            ax->set_pie_data(0, PieData{{1, 1, 1}, {}, {}});
            d = ax->pie_data(0);
            check(d.values.size() == 3 && d.labels.empty(), "set_pie_data: replaces the data whole");
            check(ax->pie_visible(0), "pie_visible: true by default");
            ax->set_pie_visible(0, false);
            check(!ax->pie_visible(0), "set_pie_visible");
        }

        void svg_parts() {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->pie(V{1, 1, 2},
                    PieOptions{.labels = {"east", "north", "west"}, .colors = {Color{0.8f, 0.0f, 0.0f}},
                               .autopct = "%1.0f%%"});
            ax->legend();
            const std::string svg = fig->render_svg().svg;
            check(count(svg, "rgb(204,0,0)") >= 3, "svg: one colour for every slice (colors wrap)");
            check(svg.find(">25%<") != std::string::npos && svg.find(">50%<") != std::string::npos,
                  "svg: autopct percents");
            check(svg.find(">east<") != std::string::npos, "svg: labels drawn");
            // The west label sits left of the centre: right-aligned.
            const std::size_t w = svg.rfind("<text", svg.find(">west<"));
            check(w != std::string::npos && svg.substr(w, svg.find(">west<") - w).find("text-anchor=\"end\"")
                                                != std::string::npos,
                  "svg: a label left of the centre is right-aligned");
            const std::size_t e = svg.rfind("<text", svg.find(">east<"));
            check(e != std::string::npos && svg.substr(e, svg.find(">east<") - e).find("text-anchor=\"start\"") != std::string::npos,
                  "svg: a label right of the centre is left-aligned");
            check(count(svg, ">east<") == 2 && count(svg, ">west<") == 2, "legend: one entry per labelled slice");

            auto f2 = Figure::create();
            auto a2 = f2->add_subplot(1, 1, 1);
            a2->pie(V{1, 1}, PieOptions{.labeldistance = std::nullopt});
            const std::string s2 = f2->render_svg().svg;
            check(count(s2, "<text") == 0, "svg: no labels, no percents, no axis = no text");
        }

        void picks() {
            RenderSnapshot s;
            s.xmin = s.ymin = -2.0;
            s.xmax = s.ymax = 2.0;
            s.xlim_auto = s.ylim_auto = false;
            s.pies.push_back(make_plot({1, 1, 2}, PieOptions{}, {}, {"a", "b", "c"}));
            s.pies.back().id = 77;
            const CoordTransform tr{-2.0, 2.0, -2.0, 2.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
            const auto at = [&](double x, double y) { return find_pick(s, tr, tr.to_px(x), tr.to_py(y)); };
            const auto h1 = at(-0.3, 0.6);
            check(h1 && h1->kind == PickKind::Pie && h1->element == 1, "pick: the second slice (90..180 deg)");
            const auto h2 = at(0.0, -0.5);
            check(h2 && h2->kind == PickKind::Pie && h2->element == 2, "pick: the third slice");
            check(!at(1.5, 1.5), "pick: a miss outside the radius");
            if (h1) check(format_pick(s, *h1) == "b: 1 (25.0%)", "hover: label: value (pct%), got " + format_pick(s, *h1));
            s.pies.back().opts.inner = 0.5;
            check(!at(0.1, 0.1), "pick: a miss in a ring's hole");
        }

        void panel_edit() {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->pie(V{1, 2});
            auto& fi = detail::FigureAccess::impl(*fig);
            fi.apply_edits_and_publish();
            PieDataEdit op;
            op.data = PieData{{1, 2, 3}, {"a", "b", "c"}, {}};
            op.seen = fi.snapshot_box.load()->axes[0].snap2d()->pies[0].data_stamp;
            FigureEdits e;
            AxesEdit ae;
            ae.plot_ops.push_back(op);
            e.per_axes.push_back({1, ae});
            const FigureEdits undo = fi.apply_edits(e);
            check(fi.snapshot_box.load()->axes[0].snap2d()->pies[0].values.get().size() == 3,
                  "panel: a pie edit reaches the snapshot");
            fi.apply_edits_and_publish();
            check(ax->pie_data(0).labels.size() == 3, "panel: the replay lands on the live axes");
            fi.apply_edits(undo);
            fi.apply_edits_and_publish();
            check(ax->pie_data(0).values == V{1, 2} && ax->pie_data(0).labels.empty(), "panel: undo restores the pie");

            PieDataEdit badop;
            badop.data = PieData{{1, -2}, {}, {}};
            FigureEdits eb;
            AxesEdit ab;
            ab.plot_ops.push_back(badop);
            eb.per_axes.push_back({1, ab});
            fi.apply_edits(eb);
            fi.apply_edits_and_publish();
            check(ax->pie_data(0).values == V{1, 2}, "panel: a bad edit is dropped");
        }

        void raster() {
            // GL against SVG: the same pie's slice colours where the SVG puts them.
            auto fig = Figure::create(FigureOptions{.width = 400, .height = 400});
            auto ax = fig->add_subplot(1, 1, 1);
            ax->pie(V{1, 1, 1, 1}, PieOptions{.colors = {Color{0.8f, 0, 0}, Color{0, 0.6f, 0}, Color{0, 0, 0.8f},
                                                         Color{0.8f, 0.6f, 0}}});
            const RgbaImage img = fig->render_rgba();
            const std::string svg = fig->render_svg().svg;
            PlotRect f{};
            const std::size_t at = svg.find("<clipPath id=\"plotArea0\"><rect x=\"");
            check(at != std::string::npos
                      && std::sscanf(svg.c_str() + at, "<clipPath id=\"plotArea0\"><rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\"",
                                     &f.x, &f.y, &f.w, &f.h) == 4,
                  "raster: the frame from the SVG");
            const float cx = f.x + f.w / 2, cy = f.y + f.h / 2, r = std::min(f.w, f.h) * 0.3f;
            const auto px = [&](float x, float y) {
                const std::size_t i = (static_cast<std::size_t>(y) * img.width + static_cast<std::size_t>(x)) * 4;
                return std::array<int, 3>{img.pixels[i], img.pixels[i + 1], img.pixels[i + 2]};
            };
            const auto is = [](std::array<int, 3> p, int r0, int g0, int b0) {
                return std::abs(p[0] - r0) < 8 && std::abs(p[1] - g0) < 8 && std::abs(p[2] - b0) < 8;
            };
            check(is(px(cx + r, cy - r), 204, 0, 0), "raster: first quarter red (up right)");
            check(is(px(cx - r, cy - r), 0, 153, 0), "raster: second quarter green (up left)");
            check(is(px(cx - r, cy + r), 0, 0, 204), "raster: third quarter blue (down left)");
            check(is(px(cx + r, cy + r), 204, 153, 0), "raster: fourth quarter amber (down right)");
            check(svg.find("rgb(204,0,0)") != std::string::npos && svg.find("rgb(0,153,0)") != std::string::npos,
                  "raster: the SVG has the same colours");
        }

        void cycle_colors() {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->pie(V{1, 1, 1});
            auto& fi = detail::FigureAccess::impl(*fig);
            fi.apply_edits_and_publish();
            const auto& o = fi.snapshot_box.load()->axes[0].snap2d()->pies[0].opts;
            check(o.colors.size() == 3 && o.colors[0].r == Color::Blue.r && o.colors[0].b == Color::Blue.b
                      && o.colors[1].r != o.colors[0].r,
                  "colors: one cycle colour per slice, resolved at the call");
        }
    } // namespace

    void test_pie() {
        std::printf("\n[pie]\n");
        angles();
        autopct();
        checks();
        readback_and_axes();
        svg_parts();
        picks();
        panel_edit();
        raster();
        cycle_colors();
    }
} // namespace lt
