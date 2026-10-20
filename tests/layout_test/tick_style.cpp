// Per-axis tick and label styling: AxesStyle's x/y/z fields
// over the shared ones, in layout, both outputs and the 3D box; a twinned
// cell's axis colors. And a twin's setters (36i): its shared axis' calls and
// the cell's half of set_axes_style() reach its host. Part of
// sextant_layout_test.
#include "layout_test.h"
#include "figure_impl.h"
#include "describe.h"
#include "tick_style.h"
#include <cmath>
#include <map>
#include <sstream>
#include <type_traits>

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 600, H = 400;
        const std::vector<double> kX{0.0, 1.0, 2.0, 3.0}, kY{0.0, 1.0, 0.5, 2.0};
        const Color kRed{1.0f, 0.0f, 0.0f, 1.0f}, kGreen{0.0f, 1.0f, 0.0f, 1.0f}, kBlue{0.0f, 0.0f, 1.0f, 1.0f};

        FigureSnapshot snap(Figure& f) { return detail::FigureAccess::impl(f).build_figure_snapshot(); }

        CellLayout cell0(Figure& f) { return compute_figure_layout(snap(f), W, H).cells[0]; }

        std::shared_ptr<Figure> one(const AxesStyle& st) {
            auto fig = Figure::create({.width = W, .height = H});
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(kX, kY);
            ax->set_axes_style(st);
            return fig;
        }

        // Pixels within 6 levels of (r, g, b) in [x0, x1) x [y0, y1).
        std::size_t count_in(const RgbaImage& im, int x0, int y0, int x1, int y1, int r, int g, int b) {
            std::size_t n = 0;
            for (int y = std::max(y0, 0); y < std::min(y1, im.height); ++y)
                for (int x = std::max(x0, 0); x < std::min(x1, im.width); ++x) {
                    const std::size_t i = (static_cast<std::size_t>(y) * im.width + x) * 4;
                    if (std::abs(int(im.pixels[i]) - r) <= 6 && std::abs(int(im.pixels[i + 1]) - g) <= 6
                        && std::abs(int(im.pixels[i + 2]) - b) <= 6)
                        ++n;
                }
            return n;
        }

        std::size_t count(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
            return n;
        }

        // --- every AxesStyle field, by name ------------------------------------

        template <class T> struct is_optional : std::false_type {};
        template <class T> struct is_optional<std::optional<T>> : std::true_type {};

        template <class X>
        std::string text_of(const X& x) {
            if constexpr (std::is_same_v<X, std::string>) return x;
            else if constexpr (std::is_same_v<X, Color>) {
                std::ostringstream o;
                o << x.r << ',' << x.g << ',' << x.b << ',' << x.a;
                return o.str();
            } else if constexpr (is_optional<X>::value) return x ? "+" + text_of(*x) : "-";
            else if constexpr (std::is_enum_v<X>) return std::to_string(static_cast<int>(x));
            else {
                std::ostringstream o;
                o << x;
                return o.str();
            }
        }

        template <class X>
        void change(X& x) {
            if constexpr (std::is_same_v<X, bool>) x = !x;
            else if constexpr (std::is_same_v<X, std::string>) x += "changed";
            else if constexpr (std::is_same_v<X, Color>) x = {0.125f, 0.25f, 0.375f, 0.5f};
            else if constexpr (is_optional<X>::value) {
                typename X::value_type v{};
                change(v);
                x = v;
            } else if constexpr (std::is_enum_v<X>) x = static_cast<X>(static_cast<int>(x) + 1);
            else x += 1.5f;
        }

        std::map<std::string, std::string> leaves(const AxesStyle& s) {
            std::map<std::string, std::string> m;
            for_each_leaf(s, [&](std::string_view path, const auto& x) { m[std::string(path)] = text_of(x); });
            return m;
        }

        // Where set_axes_style() on a twinx puts each field: its own (y) axis
        // and the shared fields that axis falls back on stay on the twin; its
        // shared (x) axis' and the cell's go to the host; the rest is dropped.
        // A twiny is the same with x and y swapped. A field missing here fails,
        // so a new one must be placed.
        std::string twinx_home(const std::string& f) {
            static const std::map<std::string, std::string> home = [] {
                std::map<std::string, std::string> m;
                for (const char* n : {"tick_color", "tick_length", "tick_linewidth", "minor_tick_length",
                                      "label_color", "label_fontsize", "font_path", "show_yticks",
                                      "show_ytick_labels", "minor_yticks", "ytick_color", "ytick_length",
                                      "ytick_linewidth", "minor_ytick_length", "ylabel_color", "ylabel_fontsize",
                                      "ytitle_color", "ytitle_fontsize", "yaxis_color"})
                    m[n] = "twin";
                for (const char* n : {"background", "spine_color", "spine_linewidth", "spine_bottom", "spine_left",
                                      "spine_top", "spine_right", "frame_margin", "title_color", "title_fontsize",
                                      "show_xticks", "show_xtick_labels", "minor_xticks", "xtick_color",
                                      "xtick_length", "xtick_linewidth", "minor_xtick_length", "xlabel_color",
                                      "xlabel_fontsize", "xtitle_color", "xtitle_fontsize", "xaxis_color"})
                    m[n] = "host";
                for (const char* n : {"xaxis_y", "xaxis_z", "yaxis_x", "yaxis_z", "zaxis_x", "zaxis_y", "origin_x",
                                      "origin_y", "origin_z", "show_zticks", "ztick_color", "ztick_length",
                                      "ztick_linewidth", "zlabel_color", "zlabel_fontsize", "ztitle_color",
                                      "ztitle_fontsize"})
                    m[n] = "none";
                return m;
            }();
            const auto it = home.find(f);
            return it == home.end() ? "unplaced" : it->second;
        }

        void style_split(bool x_twin) {
            auto fig = Figure::create();
            auto host = fig->add_subplot(1, 1, 1);
            auto twin = x_twin ? host->twinx() : host->twiny();
            const FigureSnapshot before = snap(*fig);
            AxesStyle all;
            for_each_leaf(all, [](std::string_view, auto& x) { change(x); });
            twin->set_axes_style(all);
            const FigureSnapshot after = snap(*fig);

            const auto hb = leaves(before.axes[0].snap2d()->axes_style), ha = leaves(after.axes[0].snap2d()->axes_style);
            const auto tb = leaves(before.axes[0].twins[0].snap.axes_style);
            const auto ta = leaves(after.axes[0].twins[0].snap.axes_style);
            std::string bad;
            for (const auto& [name, v] : ha) {
                const bool on_host = hb.at(name) != v, on_twin = tb.at(name) != ta.at(name);
                const std::string got = on_host && on_twin ? "both" : on_host ? "host" : on_twin ? "twin" : "none";
                // The field's twinx name: x and y swapped, except in "axis".
                std::string as_x = name;
                if (!x_twin)
                    for (std::size_t i = 0; i < as_x.size(); ++i) {
                        if (as_x.compare(i, 4, "axis") == 0) i += 3;
                        else if (as_x[i] == 'x') as_x[i] = 'y';
                        else if (as_x[i] == 'y') as_x[i] = 'x';
                    }
                const std::string want = twinx_home(as_x);
                if (got != want) bad += " " + name + " (" + got + ", want " + want + ")";
            }
            check(bad.empty(), std::string("twin setters: set_axes_style() on a ") + (x_twin ? "twinx" : "twiny")
                                   + " splits every field between twin, host and nowhere" + bad);
        }
    } // namespace

    void test_tick_style() {
        std::printf("\n[tick style: per-axis tick and label looks, axis colors, twin setters]\n");

        // --- 36h: the resolver -----------------------------------------------
        {
            AxesStyle s;
            s.tick_color = kRed;
            s.ytick_color = kBlue;
            s.xlabel_fontsize = 20.0f;
            s.minor_ytick_length = 7.0f;
            const TickLook x = tick_look(s, TickAxis::X), y = tick_look(s, TickAxis::Y), z = tick_look(s, TickAxis::Z);
            check(same_color(x.color, kRed) && same_color(y.color, kBlue) && same_color(z.color, kRed),
                  "tick look: an unset axis takes the shared color, a set one its own");
            check(x.label_fontsize == 20.0f && y.label_fontsize == s.label_fontsize
                      && y.minor_length == 7.0f && x.minor_length == s.minor_tick_length
                      && z.minor_length == s.minor_tick_length,
                  "tick look: sizes and the minor length per axis (z has no minor)");
        }

        // --- 36h: layout reserves each axis' own length and label size --------
        {
            const CellLayout base = cell0(*one({}));
            AxesStyle ly;
            ly.ytick_length = 25.0f;
            const CellLayout cy = cell0(*one(ly));
            AxesStyle lx;
            lx.xtick_length = 25.0f;
            const CellLayout cx = cell0(*one(lx));
            std::printf("  frame x/bottom: default %.1f/%.1f, ytick_length 25 %.1f/%.1f, xtick_length 25 %.1f/%.1f\n",
                        base.frame.x, base.frame.y + base.frame.h, cy.frame.x, cy.frame.y + cy.frame.h, cx.frame.x,
                        cx.frame.y + cx.frame.h);
            check(near_px(cy.frame.x - base.frame.x, 20.0f, 0.5f)
                      && near_px(cy.frame.y + cy.frame.h, base.frame.y + base.frame.h, 0.5f),
                  "tick layout: a longer y mark moves the left edge only");
            check(near_px((base.frame.y + base.frame.h) - (cx.frame.y + cx.frame.h), 20.0f, 0.5f)
                      && near_px(cx.frame.x, base.frame.x, 0.5f),
                  "tick layout: a longer x mark moves the bottom edge only");
            check(near_px(cy.ylabel_x, cy.yaxis_x - 25.0f - kTickLabelGap, 0.01f)
                      && near_px(cy.xlabel_top, cy.xaxis_y + base.xlabel_top - base.xaxis_y, 0.01f),
                  "tick layout: each axis' labels sit past its own marks");

            AxesStyle fx;
            fx.xlabel_fontsize = 24.0f;
            const CellLayout cfx = cell0(*one(fx));
            AxesStyle fy;
            fy.ylabel_fontsize = 24.0f;
            const CellLayout cfy = cell0(*one(fy));
            check(cfx.frame.y + cfx.frame.h < base.frame.y + base.frame.h - 5.0f && near_px(cfx.frame.x, base.frame.x, 0.5f),
                  "tick layout: larger x labels take room at the bottom, not the left");
            check(cfy.frame.x > base.frame.x + 5.0f
                      && near_px(cfy.frame.y + cfy.frame.h, base.frame.y + base.frame.h, 0.5f),
                  "tick layout: larger y labels take room at the left, not the bottom");
        }

        // --- 36h: GL draws each axis in its own look ---------------------------
        {
            AxesStyle s;
            s.ytick_color = kRed;
            s.ytick_length = 12.0f;
            s.ytick_linewidth = 3.0f;
            s.xlabel_color = kGreen;
            s.xlabel_fontsize = 20.0f;
            auto fig = one(s);
            const CellLayout c = cell0(*fig);
            const RgbaImage im = fig->render_rgba();
            check(im.width == W && im.height == H, "tick render: (the image is the layout's size)");
            const int fx = int(c.frame.x), fy = int(c.frame.y), fr = int(c.frame.x + c.frame.w),
                      fb = int(c.frame.y + c.frame.h);
            const std::size_t red_left = count_in(im, fx - 14, fy, fx, fb + 1, 255, 0, 0);
            const std::size_t red_below = count_in(im, fx, fb + 1, fr + 1, H, 255, 0, 0);
            const std::size_t green_below = count_in(im, fx - 20, fb + 1, fr + 20, H, 0, 255, 0);
            const std::size_t green_left = count_in(im, 0, 0, fx - 1, fb - 2, 0, 255, 0);
            std::printf("  red marks left %zu / below %zu; green labels below %zu / left %zu\n", red_left, red_below,
                        green_below, green_left);
            check(red_left > 30 && red_below == 0, "tick render: the y marks take ytick_color, the x marks keep theirs");
            check(green_below > 10 && green_left == 0, "tick render: the x labels take xlabel_color, the y labels keep theirs");

            // SVG: one group per look, and one group as before when they agree.
            const std::string svg = fig->render_svg().svg;
            const std::string plain = one({})->render_svg().svg;
            check(count(plain, "<g stroke=\"") == 1 && count(plain, "<g font-family=") == 1,
                  "tick SVG: alike axes share one mark group and one label group");
            check(count(svg, "<g stroke=\"rgb(255,0,0)\" stroke-opacity=\"1\" stroke-width=\"3\">") == 1
                      && count(svg, "<g stroke=\"") == 2,
                  "tick SVG: the y marks get a group of their own");
            check(count(svg, "font-size=\"20\" fill=\"rgb(0,255,0)\"") == 1 && count(svg, "<g font-family=") == 2,
                  "tick SVG: and the x labels");
        }

        // --- 36h: a twinned cell's axis colors ---------------------------------
        {
            const auto twinned = [](bool twin) {
                auto fig = Figure::create({.width = W, .height = H});
                auto host = fig->add_subplot(1, 1, 1);
                host->line(kX, kY);
                if (twin) {
                    auto t = host->twinx();
                    t->line(kX, kY);
                    AxesStyle ts;
                    ts.yaxis_color = kBlue;
                    ts.spine_linewidth = 3.0f;
                    t->set_axes_style(ts);
                }
                AxesStyle hs;
                hs.yaxis_color = kRed;
                hs.xaxis_color = kGreen;   // no twiny: stays the frame color
                hs.spine_linewidth = 3.0f;
                host->set_axes_style(hs);
                return fig;
            };
            auto fig = twinned(true);
            const CellLayout c = cell0(*fig);
            const RgbaImage im = fig->render_rgba();
            const int fx = int(std::lround(c.frame.x)), fy = int(c.frame.y), fr = int(std::lround(c.frame.x + c.frame.w)),
                      fb = int(c.frame.y + c.frame.h);
            const std::size_t red = count_in(im, fx - 2, fy, fx + 2, fb, 255, 0, 0);
            const std::size_t blue = count_in(im, fr - 2, fy, fr + 2, fb, 0, 0, 255);
            const std::size_t green = count_in(im, 0, 0, W, H, 0, 255, 0);
            std::printf("  twinned edges: red left %zu, blue right %zu (frame h %.0f), green %zu\n", red, blue,
                        c.frame.h, green);
            check(red > c.frame.h * 0.8f && blue > c.frame.h * 0.8f,
                  "axis colors: the host's y edge and the twin's in their own colors");
            check(green == 0, "axis colors: an axis that isn't twinned keeps the frame color");
            const std::string svg = fig->render_svg().svg;
            check(count(svg, "stroke=\"rgb(255,0,0)\" stroke-opacity=\"1\" stroke-width=\"3\"/>") == 1
                      && count(svg, "stroke=\"rgb(0,0,255)\" stroke-opacity=\"1\" stroke-width=\"3\"/>") == 1,
                  "axis colors: SVG draws each colored edge as a path of its own");

            auto alone = twinned(false);
            const RgbaImage ia = alone->render_rgba();
            check(count_in(ia, 0, 0, W, H, 255, 0, 0) == 0, "axis colors: without a twin the frame color draws it all");
        }

        // --- 36h: the 3D box ---------------------------------------------------
        {
            Box3DPlan plan;
            plan.tick_marks.resize(6);
            plan.tick_begin[0] = 0;
            plan.tick_begin[1] = 2;
            plan.tick_begin[2] = 4;
            AxesStyle s;
            check(tick_runs(plan, s).size() == 1 && tick_runs(plan, s)[0].marks.size() == 6,
                  "3D ticks: one run while the axes look alike");
            s.ztick_color = kRed;
            const auto two = tick_runs(plan, s);
            check(two.size() == 2 && two[0].marks.size() == 4 && same_color(two[1].color, kRed),
                  "3D ticks: an axis of its own color gets its own run");
            s.ytick_linewidth = 3.0f;
            check(tick_runs(plan, s).size() == 3, "3D ticks: one per axis when all differ");
            plan.tick_begin[1] = 4;   // y behind the eye: no marks
            check(tick_runs(plan, s).size() == 2, "3D ticks: an axis without marks gets no run");

            auto fig = Figure::create({.width = W, .height = H});
            auto ax = fig->add_subplot3d(1, 1, 1);
            ax->surface(PlaneOrientation::XY, std::vector<double>{0.0, 1.0, 2.0}, std::vector<double>{0.0, 1.0},
                        std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
            AxesStyle s3;
            s3.ztick_color = kRed;
            s3.ztick_length = 12.0f;
            s3.ztick_linewidth = 3.0f;
            ax->set_axes_style(s3);
            const RgbaImage im = fig->render_rgba();
            const std::string svg = fig->render_svg().svg;
            check(count_in(im, 0, 0, W, H, 255, 0, 0) > 20 && count(svg, "rgb(255,0,0)") > 0,
                  "3D ticks: the z marks draw in ztick_color (PNG and SVG)");
        }

        // --- 36i: a twin's shared-axis setters and getters are its host's ------
        {
            auto fig = Figure::create();
            auto host = fig->add_subplot(1, 1, 1);
            auto tx = host->twinx();
            tx->set_xtitle("shared x", 14.0f);
            tx->set_xticks(std::vector<double>{1.0, 2.0}, {"a", "b"});
            tx->set_xtick_format("%.1f");
            tx->set_ytitle("own y");
            const FigureSnapshot s = snap(*fig);
            const RenderSnapshot& h = *s.axes[0].snap2d();
            const RenderSnapshot& t = s.axes[0].twins[0].snap;
            check(h.xtitle == "shared x" && h.axes_style.xtitle_fontsize == 14.0f && t.xtitle.empty(),
                  "twin setters: a twinx's set_xtitle() titles the host's x axis");
            check(h.xticks_override && h.xticks_override->size() == 2 && !t.xticks_override && h.xtick_format == "%.1f"
                      && t.xtick_format.empty(),
                  "twin setters: and its set_xticks() and set_xtick_format() set the host's");
            check(t.ytitle == "own y" && h.ytitle.empty(), "twin setters: its own axis' setters stay its own");
            check(tx->xtitle() == "shared x" && host->xtitle() == "shared x" && tx->ytitle() == "own y",
                  "twin setters: xtitle() on the twin reads the host's");

            auto ty = host->twiny();
            ty->set_ytitle("shared y");
            ty->set_yticks(std::vector<double>{0.5}, {});
            ty->set_ytick_format("%.2f");
            const FigureSnapshot s2 = snap(*fig);
            const RenderSnapshot& h2 = *s2.axes[0].snap2d();
            const RenderSnapshot& t2 = s2.axes[0].twins[1].snap;
            check(h2.ytitle == "shared y" && t2.ytitle.empty() && h2.yticks_override && !t2.yticks_override
                      && h2.ytick_format == "%.2f" && ty->ytitle() == "shared y",
                  "twin setters: a twiny's y calls are the host's");

            // The bug: a title on each drew both, one over the other.
            auto f2 = Figure::create();
            auto h3 = f2->add_subplot(1, 1, 1);
            h3->line(kX, kY);
            h3->set_xtitle("Hours");
            auto t3 = h3->twinx();
            t3->line(kX, kY);
            t3->set_xtitle("Minutes");
            const std::string svg = f2->render_svg().svg;
            check(count(svg, ">Minutes<") == 1 && count(svg, ">Hours<") == 0,
                  "twin setters: the cell draws one x title, the last one set");
        }

        // --- 36i: set_axes_style() on a twin ------------------------------------
        style_split(true);
        style_split(false);
        {
            // A fresh style can't show a twin's shared axis again.
            auto fig = Figure::create();
            auto host = fig->add_subplot(1, 1, 1);
            auto tx = host->twinx();
            tx->set_axes_style({});
            const FigureSnapshot s = snap(*fig);
            check(!s.axes[0].twins[0].snap.axes_style.show_xticks && s.axes[0].snap2d()->axes_style.show_xticks,
                  "twin setters: a twin's x axis stays hidden, the host's shown");
        }
    }
} // namespace lt
