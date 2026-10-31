// Stacked bars: Axes::bar_stack() and Plane2D::bar_stack().
// The stacking (negatives down, NaN, bottom), read-back and set_bar_stack_data(),
// options and the color cycle, the checks, limits, visibility, planes, legend
// keys, the journal's cell, row and style edits with undo, GL against SVG, and
// the Data panel. Hover is in pick_area.cpp.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "figure_impl.h"
#include <cmath>

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;

        template <class E, class F>
        bool throws_as(F&& f) {
            try {
                f();
            } catch (const E&) {
                return true;
            }
            return false;
        }

        // The bar stack's drawn layers, from a snapshot.
        std::vector<BarPlot> layers_of(Figure& f, std::size_t i = 0) {
            const FigureSnapshot fs = detail::FigureAccess::impl(f).build_figure_snapshot();
            const std::span<const BarPlot> l = bar_stack_layers(fs.axes[0].snap2d()->bar_stacks[i]);
            return std::vector<BarPlot>(l.begin(), l.end());
        }

        bool eq_color(const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }
    } // namespace

    void test_bar_stack() {
        std::printf("\n[2D bar_stack]\n");
        try {
            const V x{0.0, 1.0, 2.0};
            const std::vector<V> hs{{1, -1, 2}, {2, -2, std::nan("")}, {1, 3, -1}};

            // Positive heights stack up and negative ones down, each on its own
            // running sum from the bottom; a NaN draws no bar and moves neither.
            {
                auto fig = Figure::create();
                auto ax = fig->add_subplot(1, 1, 1);
                ax->bar_stack(x, hs);
                const std::vector<BarPlot> l = layers_of(*fig);
                bool ok = ax->bar_stack_count() == 1 && ax->bar_count() == 0 && l.size() == 3;
                const std::vector<V> base{{0, 0, 0}, {1, -1, 2}, {3, 0, 0}};
                for (std::size_t k = 0; ok && k < 3; ++k)
                    ok = l[k].centers.get() == x && l[k].bottom.get() == base[k] && l[k].bar_width == static_cast<double>(0.8f);
                check(ok, "bar_stack: positives up, negatives down, NaN skipped, from 0");
                check(ax->ylim().lo <= -3.0 && ax->ylim().hi >= 4.0, "bar_stack: the limits reach both totals");

                ax->bar_stack(x, hs, {.bottom = {10.0}});
                const std::vector<BarPlot> b = layers_of(*fig, 1);
                check(b[0].bottom.get() == V{10, 10, 10} && b[1].bottom.get() == V{11, 9, 12} &&
                          b[2].bottom.get() == V{13, 10, 10},
                      "bar_stack: a bottom, one value for every bar, starts both sums");
            }

            // Read back as given: the layers, and the bottom only when one came.
            {
                auto fig = Figure::create();
                auto ax = fig->add_subplot(1, 1, 1);
                const std::vector<V> h{{1, 2, 3}, {3, 2, 1}};
                ax->bar_stack(x, h);
                ax->bar_stack(x, h, {.bottom = {1, 2, 3}});
                const BarStackData d = ax->bar_stack_data(0), e = ax->bar_stack_data(1);
                check(d.x == x && d.heights == h && d.bottom.empty() && e.bottom == V{1, 2, 3},
                      "bar_stack_data: x, the layers and the bottom as given");
            }

            // Options: each layer drawn with the shared options, its own color
            // and name; the cycle when no colors are given, as three bar() calls;
            // the width a fraction of the spacing.
            {
                auto fig = Figure::create();
                auto ax = fig->add_subplot(1, 1, 1);
                const V x2{0.0, 2.0, 4.0};
                const std::vector<V> h{{1, 2, 3}, {3, 2, 1}, {1, 1, 1}};
                ax->bar_stack(x2, h, {.orientation = Orientation::Horizontal, .names = {"a", "b", "c"}, .width = 0.5f,
                                      .alpha = 0.5f, .edgecolor = Color::Red, .linewidth = 2.0f, .show_legend = false});
                ax->bar_stack(x2, std::span(h).first(2), {.colors = {Color::Red, Color::Green}});
                auto ref = Figure::create();
                auto rx = ref->add_subplot(1, 1, 1);
                for (int k = 0; k < 3; ++k) rx->bar(x2, h[0]);
                const FigureSnapshot rs = detail::FigureAccess::impl(*ref).build_figure_snapshot();
                const RenderSnapshot& r = *rs.axes[0].snap2d();
                const std::vector<BarPlot> l = layers_of(*fig), l2 = layers_of(*fig, 1);
                bool cyc = true, opts = true;
                for (std::size_t k = 0; k < 3; ++k) {
                    cyc = cyc && eq_color(*l[k].opts.color, *r.bars[k].opts.color);
                    const BarOptions& o = l[k].opts;
                    opts = opts && o.orientation == Orientation::Horizontal && o.alpha == 0.5f &&
                           eq_color(o.edgecolor, Color::Red) && o.linewidth == 2.0f && !o.show_legend &&
                           o.name == std::string(1, static_cast<char>('a' + k)) && l[k].bar_width == 1.0;
                }
                check(ax->bar_stack_count() == 2 && l.size() == 3 && cyc, "bar_stack: no colors, one cycle color per layer");
                check(opts, "bar_stack: every layer takes the shared options, its own name, the width x spacing");
                check(l2.size() == 2 && eq_color(*l2[0].opts.color, Color::Red) && eq_color(*l2[1].opts.color, Color::Green) &&
                          l2[0].opts.name.empty(),
                      "bar_stack: colors, one per layer; no names, none");
                check(ax->xlim().hi >= 5.0 && ax->ylim().lo <= -0.5,
                      "bar_stack: Horizontal stacks along x, bars across y");
            }

            // Every check before anything is added.
            {
                auto fig = Figure::create();
                auto ax = fig->add_subplot(1, 1, 1);
                const std::vector<V> bad{{1, 2, 3}, {1, 2}};
                const std::vector<V> h{{1, 2, 3}, {3, 2, 1}};
                check(throws_as<std::invalid_argument>([&] { ax->bar_stack(x, bad); }) &&
                          throws_as<std::invalid_argument>([&] { ax->bar_stack(x, h, {.colors = {Color::Red}}); }) &&
                          throws_as<std::invalid_argument>([&] { ax->bar_stack(x, h, {.names = {"a"}}); }) &&
                          throws_as<std::invalid_argument>([&] { ax->bar_stack(x, h, {.bottom = {1, 2}}); }) &&
                          throws_as<std::invalid_argument>([&] { ax->bar_stack(x, h, {.zorder = INFINITY}); }) &&
                          ax->bar_stack_count() == 0,
                      "bar_stack: a layer of another length, colors/names not one per layer, a bad bottom or zorder "
                      "throw and add nothing");
                ax->bar_stack(x, std::span<const V>{});
                check(ax->bar_stack_count() == 1 && layers_of(*fig).empty(), "bar_stack: no layers, an empty stack");
            }

            // An edit restacks; the span form keeps the bottom while the bar count
            // holds; a new layer count trims or extends colors and names.
            {
                auto fig = Figure::create();
                auto ax = fig->add_subplot(1, 1, 1);
                const std::vector<V> h{{1, 2, 3}, {3, 2, 1}, {1, 1, 1}};
                ax->bar_stack(x, h, {.bottom = {5.0}, .colors = {Color::Red, Color::Green, Color::Blue}, .names = {"a", "b", "c"}});
                std::vector<V> e = h;
                e[0] = V(3, 10.0);
                ax->set_bar_stack_data(0, x, e);
                std::vector<BarPlot> l = layers_of(*fig);
                check(l[1].bottom.get() == V(3, 15.0) && l[2].bottom[0] == 18.0 && ax->bar_stack_data(0).bottom == V(3, 5.0),
                      "set_bar_stack_data: the layers above follow an edit below, the bottom kept");
                ax->set_bar_stack_data(0, BarStackData{x, {h[0], h[1]}, {}});
                l = layers_of(*fig);
                check(l.size() == 2 && l[0].bottom.get() == V(3, 0.0) && eq_color(*l[1].opts.color, Color::Green) &&
                          l[1].opts.name == "b",
                      "set_bar_stack_data: the struct sets the bottom; fewer layers trim colors and names");
                ax->set_bar_stack_data(0, BarStackData{x, h, {1.0}});
                ax->set_bar_stack_data(0, V{0, 1}, std::vector<V>{{1, 1}, {1, 1}, {1, 1}, {1, 1}});
                l = layers_of(*fig);
                check(l.size() == 4 && l[3].opts.name.empty() && ax->bar_stack_data(0).bottom.empty() &&
                          l[3].bottom.get() == V(2, 3.0),
                      "set_bar_stack_data: more layers, names empty; a new bar count drops the bottom");
                check(throws_as<std::invalid_argument>([&] { ax->set_bar_stack_data(0, x, std::vector<V>{V{1.0}}); }) &&
                          throws_as<std::invalid_argument>(
                              [&] { ax->set_bar_stack_data(0, BarStackData{x, h, {1, 2}}); }) &&
                          throws_as<std::out_of_range>([&] { ax->set_bar_stack_data(1, x, h); }),
                      "set_bar_stack_data: a layer of another length, a bad bottom, or no such stack, throws");
            }

            // One visibility flag for the whole stack; ids and zorder, one object.
            {
                auto fig = Figure::create();
                auto ax = fig->add_subplot(1, 1, 1);
                ax->bar_stack(V{0.0, 1.0}, std::vector<V>{{100, 100}, {100, 100}}, {.visible = false, .zorder = 2.0});
                const ObjectId id = ax->last_object();
                check(!ax->bar_stack_visible(0) && ax->ylim().hi < 50.0, "visible: hidden, it widens no limits");
                ax->set_bar_stack_visible(0, true);
                check(ax->bar_stack_visible(0) && ax->ylim().hi > 200.0, "visible: shown, all its layers count");
                ax->set_bar_stack_data(0, V{0.0, 1.0}, std::vector<V>{{1, 1}});
                check(ax->zorder(id) == 2.0 && ax->last_object() == id, "zorder: kept by set_bar_stack_data()");
                ax->remove(id);
                check(ax->bar_stack_count() == 0, "remove: the whole stack by its id");
            }

            // Planes, and a legend entry per named layer.
            {
                auto f3 = Figure::create();
                auto a3 = f3->add_subplot3d(1, 1, 1);
                auto pl = a3->plane(PlaneOrientation::XY, 0.0);
                const std::vector<V> h{{1, 2, 3}, {3, 2, 1}};
                pl->bar_stack(x, h, {.bottom = {1.0}});
                check(pl->bar_stack_count() == 1 && pl->bar_count() == 0 && pl->bar_stack_data(0).heights == h &&
                          pl->bar_stack_data(0).bottom == V(3, 1.0),
                      "plane: bar_stack()");
                pl->set_bar_stack_visible(0, false);
                check(!pl->bar_stack_visible(0), "plane: set_bar_stack_visible()");

                auto fig = Figure::create();
                auto ax = fig->add_subplot(1, 1, 1);
                ax->bar_stack(x, std::vector<V>{{1, 2, 3}, {3, 2, 1}, {1, 1, 1}}, {.names = {"a", "", "c"}});
                const FigureSnapshot fs = detail::FigureAccess::impl(*fig).build_figure_snapshot();
                const auto e = collect_legend_entries(*fs.axes[0].snap2d());
                check(e.size() == 2 && e[0].name == "a" && e[1].name == "c" && e[1].kind == LegendKind::Bar,
                      "legend: a patch per named layer, in order");
            }

            // The journal: a cell, a row and the style from the Data panel
            // restack, replay onto the live axes, and undo.
            {
                auto fig = Figure::create();
                auto ax = fig->add_subplot(1, 1, 1);
                const std::vector<V> h{{1, 2, 3}, {3, 2, 1}, {1, 1, 1}};
                ax->bar_stack(x, h, {.bottom = {1, 2, 3}, .colors = {Color::Red, Color::Green, Color::Blue}, .hint_labels = {"p", "q", "r"}});
                auto& fi = detail::FigureAccess::impl(*fig);
                AxesEdit ae;
                ae.plot_ops.push_back(PlotCellEdit{PlotKind::BarStack, 0, 1, 0, 10.0});
                ae.plot_ops.push_back(PlotCellEdit{PlotKind::BarStack, 0, 4, 1, 7.0});
                PlotRowEdit row;
                row.kind = PlotKind::BarStack;
                row.row = 3;
                ae.plot_ops.push_back(row);
                ae.plot_styles.push_back(
                    PlotStyleEdit{0, -1, BarStackOptions{.colors = {Color::Blue, Color::Green, Color::Red}, .width = 0.5f, .alpha = 0.5f}});
                FigureEdits e;
                e.per_axes.push_back({1, ae});
                const FigureEdits undo = fi.apply_edits(e);
                const auto snap = fi.snapshot_box.load();
                const BarStackPlot& sp = snap->axes[0].snap2d()->bar_stacks[0];
                const std::span<const BarPlot> l = bar_stack_layers(sp);
                check(sp.x.size() == 4 && sp.heights[2].size() == 4 && sp.bottom.size() == 4 && sp.bottom[3] == 3.0 &&
                          sp.opts.hint_labels.size() == 4 && l[1].bottom[0] == 11.0 && l[1].bottom[1] == 9.0 &&
                          eq_color(*l[0].opts.color, Color::Blue) && l[0].opts.alpha == 0.5f && l[0].bar_width == 0.5,
                      "journal: a bar stack's cells (layer, bottom), row and style reach the snapshot, restacked");
                fi.apply_edits_and_publish();
                check(ax->bar_stack_data(0).heights[0][0] == 10.0 && ax->bar_stack_data(0).x.size() == 4,
                      "journal: and replay onto the live axes");
                fi.apply_edits(undo);
                const auto u = fi.snapshot_box.load();
                const BarStackPlot& up = u->axes[0].snap2d()->bar_stacks[0];
                check(up.x.size() == 3 && up.bottom.get() == V{1, 2, 3} && bar_stack_layers(up)[1].bottom[0] == 2.0 &&
                          eq_color(*bar_stack_layers(up)[0].opts.color, Color::Red) && bar_stack_layers(up)[0].bar_width == static_cast<double>(0.8f),
                      "journal: undo restores the bar stack");

                AxesEdit bad;
                bad.plot_styles.push_back(PlotStyleEdit{0, -1, BarStackOptions{.colors = {Color::Blue}}});
                FigureEdits be;
                be.per_axes.push_back({1, bad});
                fi.apply_edits(be);
                check(eq_color(*bar_stack_layers(fi.snapshot_box.load()->axes[0].snap2d()->bar_stacks[0])[2].opts.color,
                                 Color::Blue),
                      "journal: a style without one color per layer keeps the colors");
            }

            // SVG: a bar stack is written as its layers, as the same bar() calls
            // with their bottoms.
            {
                const std::vector<V> h{{1, -1, 2}, {2, -2, 1}, {1, 3, -1}};
                auto sf = Figure::create();
                sf->add_subplot(1, 1, 1)->bar_stack(x, h, {.bottom = {0.5}, .colors = {Color::Red, Color::Green, Color::Blue},
                                                           .names = {"a", "b", "c"}, .linewidth = 1.0f});
                auto ff = Figure::create();
                auto fx = ff->add_subplot(1, 1, 1);
                const std::vector<V> base{{0.5, 0.5, 0.5}, {1.5, -0.5, 2.5}, {3.5, 0.5, 0.5}};
                const Color cs[] = {Color::Red, Color::Green, Color::Blue};
                const char* ns[] = {"a", "b", "c"};
                for (int k = 0; k < 3; ++k)
                    fx->bar(x, h[k], {.color = cs[k], .bottom = base[k], .name = ns[k], .edgecolor = Color::Black,
                                      .linewidth = 1.0f});
                const std::string a = sf->render_svg({}, 400, 300).svg, b = ff->render_svg({}, 400, 300).svg;
                check(!a.empty() && a == b, "svg: a bar stack is its layers' bars");
            }

            // The Data panel draws a bar stack's tab with no duplicate widget ids.
            {
                FigureSnapshot fs;
                RenderSnapshot r;
                BarStackPlot sp;
                sp.x = x;
                sp.heights = {CowVec<double>(V{1, 2, 3}), CowVec<double>(V{3, 2, 1}), CowVec<double>(V{1, 1, 1})};
                sp.bottom = V{1, 1, 1};
                sp.opts.colors = {Color::Red, Color::Green, Color::Blue};
                sp.opts.names = {"a", "", "c"};
                sp.id = 5;
                bar_stack_rebuild(sp);
                r.bar_stacks.push_back(sp);
                fs.axes.push_back({{1, 1, 1}, std::move(r)});
                fs.generation = fs.data_generation = 1;
                const IdConflictScan sc = scan_panel_for_id_conflicts(fs, &draw_data_window, "Data", {});
                check(sc.conflicts == 0 && sc.probes > 0, "panel: a bar stack's tab draws without id conflicts");
            }
        } catch (const std::exception& e) {
            check(false, std::string("bar_stack: unexpected exception: ") + e.what());
        }
    }
} // namespace lt
