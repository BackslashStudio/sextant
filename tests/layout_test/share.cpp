// Share groups (v1.2 step 36b): Axes::sharex()/sharey() -- one set of limits
// and one scale for every member, through the setters, the panel's edits (the
// snapshot patch and the caller's replay), undo and cla(); automatic limits
// over the whole group; the refusals; and the inner tick labels that hide by
// themselves. Part of sextant_layout_test.
#include "layout_test.h"
#include "figure_impl.h"

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 900, H = 600;

        const std::vector<double> kX1{0.0, 5.0, 10.0}, kY1{0.0, 1.0, 0.5};
        const std::vector<double> kX2{5.0, 12.0, 20.0}, kY2{3.0, 2.0, 4.0};

        bool same(Range a, Range b) { return a.lo == b.lo && a.hi == b.hi; }

        // Two rows sharing x, each with its own data.
        struct Column {
            std::shared_ptr<Figure> fig = Figure::create();
            std::shared_ptr<Axes> top, bottom;
            detail::FigureAccess::Impl* fi = nullptr;

            Column() {
                top = fig->add_subplot(2, 1, 1);
                bottom = fig->add_subplot(2, 1, 2);
                top->line(kX1, kY1);
                bottom->line(kX2, kY2);
                bottom->sharex(*top);
                fi = &detail::FigureAccess::impl(*fig);
            }
            FigureLayout layout() const { return compute_figure_layout(fi->build_figure_snapshot(), W, H); }
        };

        void test_setters() {
            Column c;
            const Range x = c.top->xlim();
            check(same(x, c.bottom->xlim()), "share: both members report one x range");
            check(x.lo < 0.0 && x.hi > 20.0, "share: automatic x covers both members' data, padded once");
            check(!same(c.top->ylim(), c.bottom->ylim()), "share: y stays each axes' own");

            const FigureLayout l = c.layout();
            check(l.cells[0].tr.xmin == x.lo && l.cells[0].tr.xmax == x.hi
                      && l.cells[1].tr.xmin == x.lo && l.cells[1].tr.xmax == x.hi,
                  "share: layout draws both with the getter's x range");

            c.bottom->set_xlim(2.0, 8.0);
            check(same(c.top->xlim(), Range{2.0, 8.0}), "share: set_xlim() on one member moves the other");
            c.top->set_xscale(Scale::Log);
            check(c.bottom->xscale() == Scale::Log, "share: set_xscale() on one member is the group's");
            c.top->set_xscale(Scale::Linear);

            // cla() keeps membership and the group's limits.
            c.bottom->cla();
            check(same(c.bottom->xlim(), Range{2.0, 8.0}), "share: cla() keeps the group's set limits");
            c.bottom->set_xlim(1.0, 3.0);
            check(same(c.top->xlim(), Range{1.0, 3.0}), "share: and the cleared axes is still a member");

        }

        void test_merge_and_refusals() {
            auto fig = Figure::create();
            auto a = fig->add_subplot(2, 2, 1);
            auto b = fig->add_subplot(2, 2, 2);
            auto c = fig->add_subplot(2, 2, 3);
            auto d = fig->add_subplot(2, 2, 4);
            a->set_xlim(0.0, 1.0);
            b->sharex(*a);
            d->set_xlim(10.0, 20.0);
            c->sharex(*d);
            check(same(c->xlim(), Range{10.0, 20.0}), "share: joining takes the other's set limits");
            c->sharex(*a);
            check(same(d->xlim(), Range{0.0, 1.0}) && same(c->xlim(), Range{0.0, 1.0}),
                  "share: joining a group brings one's whole old group along");
            d->set_xlim(5.0, 6.0);
            check(same(a->xlim(), Range{5.0, 6.0}) && same(b->xlim(), Range{5.0, 6.0}),
                  "share: the merged group moves as one");
            a->sharex(*a);
            c->sharex(*b);
            check(same(a->xlim(), Range{5.0, 6.0}), "share: sharing with itself or within a group changes nothing");

            auto other = Figure::create()->add_subplot(1, 1, 1);
            bool threw = false;
            try { other->sharex(*a); } catch (const std::invalid_argument&) { threw = true; }
            check(threw, "share: an axes of another figure is refused");

            // A heatmap's even mesh takes no log scale, through the group too.
            auto fig2 = Figure::create();
            auto p = fig2->add_subplot(1, 3, 1);
            auto q = fig2->add_subplot(1, 3, 2);
            auto r = fig2->add_subplot(1, 3, 3);
            const std::vector<double> cells{1.0, 2.0, 3.0, 4.0};
            r->heatmap(cells, 2, 2, {1.0, 3.0}, {1.0, 3.0});
            p->set_xscale(Scale::Log);
            threw = false;
            try { r->sharex(*p); } catch (const std::invalid_argument&) { threw = true; }
            check(threw && r->xscale() == Scale::Linear, "share: a heatmap axes can't join a log group");
            r->set_xlim(0.0, 9.0);
            check(!same(p->xlim(), Range{0.0, 9.0}), "share: and it did not join");
            r->sharex(*q);
            threw = false;
            try { q->set_xscale(Scale::Log); } catch (const std::invalid_argument&) { threw = true; }
            check(threw && q->xscale() == Scale::Linear && r->xscale() == Scale::Linear,
                  "share: set_xscale(Log) is refused when any member holds a heatmap");
        }

        void test_lifetime() {
            std::shared_ptr<Axes> kept;
            {
                auto fig = Figure::create();
                kept = fig->add_subplot(1, 2, 1);
                auto gone = fig->add_subplot(1, 2, 2);
                gone->line(kX2, kY2);
                kept->sharey(*gone);
            }
            // The other member died with its figure; the survivor no longer sees it.
            kept->line(kX1, kY1);
            const Range y = kept->ylim();
            check(y.lo < 0.0 && y.hi < 2.0, "share: a member that is gone leaves the group (no dangling member)");

            // Its figure is gone too: a new figure (which may sit at the old one's
            // address) is still another figure.
            auto fresh = Figure::create()->add_subplot(1, 1, 1);
            bool threw = false;
            try { fresh->sharex(*kept); } catch (const std::invalid_argument&) { threw = true; }
            check(threw, "share: an axes of a figure that is gone is not the new figure's");
        }

        // Panel edits: the render thread's patch moves every member, the caller's
        // replay agrees, and undo puts the group back.
        void test_edits() {
            Column c;
            auto s0 = c.fi->host_frame();
            const Range x0 = c.top->xlim();
            c.fi->edit_box.update(2, [](AxesEdit& e) {
                e.xmin = 3.0; e.xmax = 7.0; e.xlim_auto = false;
            });
            FigureEdits inv;
            auto s1 = c.fi->host_frame(&inv);
            const RenderSnapshot& t1 = *s1->axes[0].snap2d();
            check(!t1.xlim_auto && t1.xmin == 3.0 && t1.xmax == 7.0,
                  "share edit: a limit edit on one member patches the other's snapshot");
            check(t1.limit_stamps.x == s1->axes[1].snap2d()->limit_stamps.x,
                  "share edit: and the members' limit stamps stay one");
            c.fi->apply_edits_and_publish();
            check(same(c.top->xlim(), Range{3.0, 7.0}) && same(c.bottom->xlim(), Range{3.0, 7.0}),
                  "share edit: the caller's replay moves every live member");

            c.fi->apply_edits(inv);
            c.fi->apply_edits_and_publish();
            check(same(c.top->xlim(), x0) && same(c.bottom->xlim(), x0),
                  "share edit: undo returns the whole group to automatic");

            // Edits on alternating members between publishes: the replay ends on
            // the latest one, whatever order the journal keeps its slots in.
            const auto lim = [](double lo, double hi) {
                return [lo, hi](AxesEdit& e) { e.xmin = lo; e.xmax = hi; e.xlim_auto = false; };
            };
            const auto scale = [](Scale k) {
                return [k](AxesEdit& e) { AxisScale s; s.kind = k; e.xscale = s; };
            };
            c.fi->host_frame();
            c.fi->edit_box.update(2, lim(1.0, 2.0));
            c.fi->edit_box.update(2, scale(Scale::Symlog));
            c.fi->host_frame();
            c.fi->edit_box.update(1, lim(3.0, 4.0));
            c.fi->edit_box.update(1, scale(Scale::Log));
            c.fi->host_frame();
            c.fi->edit_box.update(2, lim(5.0, 6.0));
            c.fi->edit_box.update(2, scale(Scale::Symlog));
            c.fi->host_frame();
            c.fi->edit_box.update(1, scale(Scale::Log));
            c.fi->host_frame();
            c.fi->apply_edits_and_publish();
            check(same(c.top->xlim(), Range{5.0, 6.0}) && same(c.bottom->xlim(), Range{5.0, 6.0}),
                  "share edit: the replay ends on the latest limits, B A B");
            check(c.top->xscale() == Scale::Log && c.bottom->xscale() == Scale::Log,
                  "share edit: and on the latest scale, A B A");
            c.top->set_xscale(Scale::Linear);

            // A scale edit the group can't take is dropped, on both paths.
            auto fig = Figure::create();
            auto a = fig->add_subplot(1, 2, 1);
            auto h = fig->add_subplot(1, 2, 2);
            const std::vector<double> cells{1.0, 2.0, 3.0, 4.0};
            h->heatmap(cells, 2, 2, {1.0, 3.0}, {1.0, 3.0});
            a->sharex(*h);
            auto& fi = detail::FigureAccess::impl(*fig);
            fi.host_frame();
            fi.edit_box.update(1, [](AxesEdit& e) {
                AxisScale s;
                s.kind = Scale::Log;
                e.xscale = s;
            });
            auto s2 = fi.host_frame();
            check(s2->axes[0].snap2d()->xscale.linear() && s2->axes[1].snap2d()->xscale.linear(),
                  "share edit: a log scale is refused when a member holds a heatmap");
            fi.apply_edits_and_publish();
            check(a->xscale() == Scale::Linear && h->xscale() == Scale::Linear,
                  "share edit: and the replay refuses it too");
            (void)s0;
        }

        // Inner tick labels hide by themselves and give their room back.
        void test_inner_labels() {
            auto plain = Figure::create();
            plain->add_subplot(2, 1, 1)->line(kX1, kY1);
            plain->add_subplot(2, 1, 2)->line(kX2, kY2);
            const FigureLayout lp = compute_figure_layout(
                detail::FigureAccess::impl(*plain).build_figure_snapshot(), W, H);

            Column c;
            const FigureLayout l = c.layout();
            check(!l.cells[0].xlabels && l.cells[1].xlabels && l.cells[0].ylabels,
                  "labels: the upper member hides its x labels, the lower keeps them, y stays");
            check(l.cells[0].frame.h > lp.cells[0].frame.h, "labels: the hidden labels' band is given back");
            check(!l.cells[0].xticks.empty(), "labels: the ticks (marks) are still there");

            AxesStyle st;
            st.show_xtick_labels = true;
            c.top->set_axes_style(st);
            check(c.layout().cells[0].xlabels, "labels: show_xtick_labels = true keeps them");
            st.show_xtick_labels = false;
            c.top->set_axes_style(st);
            st.show_xtick_labels.reset();
            c.bottom->set_axes_style(st);
            const FigureLayout l2 = c.layout();
            check(!l2.cells[0].xlabels && l2.cells[1].xlabels, "labels: false hides them, unset follows the rule");

            // The SVG agrees: fewer labels than the unshared figure.
            const std::string shared = c.fig->render_svg().svg;
            const std::string unshared = plain->render_svg().svg;
            std::size_t ns = 0, nu = 0;
            for (std::size_t at = shared.find("<text"); at != std::string::npos; at = shared.find("<text", at + 1)) ++ns;
            for (std::size_t at = unshared.find("<text"); at != std::string::npos; at = unshared.find("<text", at + 1)) ++nu;
            check(ns < nu, "labels: the SVG draws no labels where they are hidden");

            // sharey across a row: the right member hides its y labels; a member to
            // the right of an axes with its y axis on the right hides that one's.
            auto fig = Figure::create();
            auto left = fig->add_subplot(1, 2, 1);
            auto right = fig->add_subplot(1, 2, 2);
            left->line(kX1, kY1);
            right->line(kX2, kY2);
            right->sharey(*left);
            const FigureLayout lr = compute_figure_layout(detail::FigureAccess::impl(*fig).build_figure_snapshot(), W, H);
            check(lr.cells[0].ylabels && !lr.cells[1].ylabels && lr.cells[1].xlabels,
                  "labels: sharey hides the right member's y labels only");
            AxesStyle hi;
            hi.yaxis_x = AxisPosition::High;
            left->set_axes_style(hi);
            right->set_axes_style(hi);
            const FigureLayout lh = compute_figure_layout(detail::FigureAccess::impl(*fig).build_figure_snapshot(), W, H);
            check(!lh.cells[0].ylabels && lh.cells[1].ylabels,
                  "labels: with y on the right it is the left member that hides them");
        }

        // Frames along a shared axis keep one extent: hiding the other axis in a
        // member gives no room back on those sides (it would misalign the ticks).
        void test_shared_extent() {
            auto fig = Figure::create();
            std::shared_ptr<Axes> col[3];
            for (int i = 0; i < 3; ++i) {
                col[i] = fig->add_subplot(3, 2, 1 + 2 * i);
                col[i]->line(kX1, kY1).set_ytitle("y");
                fig->add_subplot(3, 2, 2 + 2 * i)->line(kX2, kY2);
                if (i) col[i]->sharex(*col[0]);
            }
            auto& fi = detail::FigureAccess::impl(*fig);
            const FigureLayout before = compute_figure_layout(fi.build_figure_snapshot(), W, H);
            AxesStyle st;
            st.show_yticks = false;
            col[0]->set_axes_style(st);
            const FigureLayout after = compute_figure_layout(fi.build_figure_snapshot(), W, H);
            bool aligned = true;
            for (int i : {0, 2, 4})
                aligned = aligned && after.cells[i].frame.x == after.cells[0].frame.x
                          && after.cells[i].frame.w == after.cells[0].frame.w;
            check(aligned, "extent: hiding y in one sharex member keeps the column's frames on one x span");
            check(after.cells[0].frame.x == before.cells[0].frame.x
                      && after.cells[0].frame.w == before.cells[0].frame.w,
                  "extent: the member's frame does not widen");
            check(after.cells[1].frame.x == before.cells[1].frame.x
                      && after.cells[1].frame.w == before.cells[1].frame.w,
                  "extent: and the unshared column beside it does not move");

            // sharey across a row: hiding x in one member keeps one y span.
            auto f2 = Figure::create();
            auto l = f2->add_subplot(1, 2, 1);
            auto r = f2->add_subplot(1, 2, 2);
            l->line(kX1, kY1);
            r->line(kX2, kY2);
            r->sharey(*l);
            AxesStyle nx;
            nx.show_xticks = false;
            l->set_axes_style(nx);
            const FigureLayout lr = compute_figure_layout(detail::FigureAccess::impl(*f2).build_figure_snapshot(), W, H);
            check(lr.cells[0].frame.y == lr.cells[1].frame.y && lr.cells[0].frame.h == lr.cells[1].frame.h,
                  "extent: hiding x in one sharey member keeps the row's frames on one y span");
        }

        // Outside legends and colorbars (36b-1b): members along a shared axis on
        // the same grid lines reserve alike, so their frames keep one span.
        void test_shared_decorations() {
            const auto snap = [](const std::shared_ptr<Figure>& f) {
                return detail::FigureAccess::impl(*f).build_figure_snapshot();
            };
            const auto same_x = [](const PlotRect& a, const PlotRect& b) { return a.x == b.x && a.w == b.w; };
            const auto same_y = [](const PlotRect& a, const PlotRect& b) { return a.y == b.y && a.h == b.h; };

            // A column: a legend on the right of the top cell, a colorbar on the bottom one's.
            const auto column = [&](bool shared) {
                auto fig = Figure::create();
                auto top = fig->add_subplot(2, 1, 1);
                auto bottom = fig->add_subplot(2, 1, 2);
                top->line(kX1, kY1, {.name = "a long legend entry"}).legend();
                bottom->scatter_z(kX2, kY2, kY2, {.colorbar = true});
                if (shared) bottom->sharex(*top);
                return fig;
            };
            const auto plain = column(false), shared = column(true);
            const FigureLayout lp = compute_figure_layout(snap(plain), W, H);
            const FigureLayout ls = compute_figure_layout(snap(shared), W, H);
            check(!same_x(lp.cells[0].frame, lp.cells[1].frame),
                  "decorations: unshared, a legend and a colorbar give a column two frame widths");
            check(same_x(ls.cells[0].frame, ls.cells[1].frame),
                  "decorations: sharing x, the column's frames keep one x span");
            check(ls.cells[0].frame.w <= lp.cells[0].frame.w && ls.cells[0].frame.w <= lp.cells[1].frame.w,
                  "decorations: the narrower reservation wins (each takes the larger)");
            check(ls.cells[0].has_legend() && ls.cells[1].has_colorbar(),
                  "decorations: each keeps its own legend or colorbar");

            // The inverse agrees, for the member without the legend.
            const FigureSnapshot fs = snap(shared);
            const LayoutSize sz = figure_size_for_frame(fs, 2, 320.0f, 180.0f);
            const FigureLayout inv = compute_figure_layout(fs, int(std::lround(sz.width)), int(std::lround(sz.height)));
            check(near_px(inv.cells[1].frame.w, 320.0f, 1.0f) && near_px(inv.cells[1].frame.h, 180.0f, 1.0f),
                  "decorations: figure_size_for_frame() gives a shared member the frame asked for");

            // A row sharing y: a legend wrapped above the left cell keeps one y span.
            auto row = Figure::create();
            auto l = row->add_subplot(1, 2, 1);
            auto r = row->add_subplot(1, 2, 2);
            l->line(kX1, kY1, {.name = "one"}).line(kX2, kY2, {.name = "two"})
              .legend({.anchor = LegendAnchor::OutsideTL});
            r->line(kX2, kY2);
            r->sharey(*l);
            const FigureSnapshot rs = snap(row);
            const FigureLayout lr = compute_figure_layout(rs, W, H);
            check(same_y(lr.cells[0].frame, lr.cells[1].frame),
                  "decorations: sharing y, a legend above one member keeps the row on one y span");
            const LayoutSize rz = figure_size_for_frame(rs, 2, 300.0f, 200.0f);
            const FigureLayout rinv = compute_figure_layout(rs, int(std::lround(rz.width)), int(std::lround(rz.height)));
            check(near_px(rinv.cells[1].frame.w, 300.0f, 1.0f) && near_px(rinv.cells[1].frame.h, 200.0f, 1.0f),
                  "decorations: and the inverse gives the other member its frame");
        }
    } // namespace

    void test_share() {
        std::printf("\n[share groups: sharex / sharey]\n");
        test_setters();
        test_merge_and_refusals();
        test_lifetime();
        test_edits();
        test_inner_labels();
        test_shared_extent();
        test_shared_decorations();
    }
} // namespace lt
