// Equal aspect: Axes::set_aspect() -- Box shrinks the frame
// to the ratio, centred, with its furniture; Datalim widens one axis' limits
// instead; scale units on log axes; the setters' and edits' refusals; pan
// and zoom keep the ratio. Part of sextant_layout_test.
#include "layout_test.h"
#include "figure_impl.h"
#include "coord_transform.h"
#include <cmath>
#include <functional>

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 900, H = 600;

        const std::vector<double> kX{0.0, 10.0}, kY{0.0, 5.0};

        FigureSnapshot snap(Figure& f) { return detail::FigureAccess::impl(f).build_figure_snapshot(); }

        CellLayout cell0(Figure& f) { return compute_figure_layout(snap(f), W, H).cells[0]; }

        bool throws(const std::function<void()>& fn) {
            try { fn(); } catch (const std::invalid_argument&) { return true; }
            return false;
        }

        // Pixels per scale unit along y over those along x.
        double drawn_ratio(const CellLayout& c) {
            return (static_cast<double>(c.frame.h) / c.tr.syspan()) / (static_cast<double>(c.frame.w) / c.tr.sxspan());
        }

        void test_api() {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 2, 1);
            auto other = fig->add_subplot(1, 2, 2);
            check(ax->aspect() == 0.0 && ax->adjustable() == Adjustable::Box, "aspect: auto and Box by default");
            ax->set_aspect(2.0, Adjustable::Datalim);
            check(ax->aspect() == 2.0 && ax->adjustable() == Adjustable::Datalim, "aspect: set_aspect() reads back");
            check(snap(*fig).axes[0].snap2d()->aspect == AxesAspect{2.0, Adjustable::Datalim},
                  "aspect: the snapshot carries it");

            check(throws([&] { ax->set_aspect(-1.0); }) && throws([&] { ax->set_aspect(std::nan("")); })
                      && throws([&] { ax->set_aspect(INFINITY); }),
                  "aspect: a negative or non-finite ratio throws");
            check(ax->aspect() == 2.0, "aspect: nothing changes on a throw");
            check(throws([&] { ax->twinx()->set_aspect(1.0); }), "aspect: set_aspect() on a twin throws");

            // ax now shares x with its twin; Datalim widens y. Sharing y too is refused.
            check(throws([&] { ax->sharey(*other); }), "aspect: sharey() refuses to leave a Datalim axes no free axis");
            check(snap(*fig).axes[0].snap2d()->yshare == 0 && snap(*fig).axes[1].snap2d()->yshare == 0,
                  "aspect: and joins nothing");
            check(throws([&] { other->sharey(*ax); }), "aspect: from the other side too");
            check(throws([&] { ax->twiny(); }), "aspect: twiny() over a Datalim axes with a twinx throws");
            check(snap(*fig).axes[0].twins.size() == 1, "aspect: and makes no twin");

            ax->set_aspect(1.0);   // Box: any sharing is fine
            ax->sharey(*other);
            check(throws([&] { ax->set_aspect(1.0, Adjustable::Datalim); }),
                  "aspect: Datalim with both axes shared throws");
            ax->set_aspect(0.0, Adjustable::Datalim);
            check(ax->aspect() == 0.0, "aspect: Datalim at auto needs no free axis");

            ax->set_aspect(3.0);
            ax->cla();
            check(ax->aspect() == 0.0 && ax->adjustable() == Adjustable::Box, "aspect: cla() resets it");
        }

        void test_box() {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(kX, kY).set_xlim(0.0, 10.0).set_ylim(0.0, 5.0).set_title("t");
            const CellLayout full = cell0(*fig);
            ax->set_aspect(1.0);
            const CellLayout c = cell0(*fig);
            std::printf("  box: frame %gx%g in %gx%g\n", c.frame.w, c.frame.h, full.frame.w, full.frame.h);
            check(std::abs(c.frame.w - 2.0f * c.frame.h) < 1.0f, "aspect box: a 10 x 5 view draws 2:1");
            check(std::abs(drawn_ratio(c) - 1.0) < 1e-3, "aspect box: one y unit as long as one x unit");
            check(c.frame.w == full.frame.w && c.frame.h < full.frame.h, "aspect box: the frame shortens here");
            check(near_px(c.frame.y + c.frame.h * 0.5f, full.frame.y + full.frame.h * 0.5f)
                      && near_px(c.frame.x, full.frame.x),
                  "aspect box: centred in its space");
            check(near_px(c.xtitle_y - (c.frame.y + c.frame.h), full.xtitle_y - (full.frame.y + full.frame.h))
                      && near_px(c.xaxis_y, c.frame.y + c.frame.h),
                  "aspect box: the axis and its title follow the frame");
            check(c.extended.y > full.extended.y && near_px(c.frame.y - c.extended.y, full.frame.y - full.extended.y),
                  "aspect box: the extended frame follows it");
            check(near_px(c.frame.y - c.title_y, full.frame.y - full.title_y), "aspect box: so does the title");
            check(c.tr.xmin == 0.0 && c.tr.xmax == 10.0 && c.tr.ymin == 0.0 && c.tr.ymax == 5.0,
                  "aspect box: the limits stay");

            ax->set_aspect(2.0);
            const CellLayout c2 = cell0(*fig);
            check(std::abs(c2.frame.w - c2.frame.h) < 1.0f && std::abs(drawn_ratio(c2) - 2.0) < 1e-3,
                  "aspect box: ratio 2 draws a y unit twice as long");

            // A pan and a wheel zoom keep the ratio, so the frame stays put (over
            // the stored measure, as navigation leaves it).
            ax->set_aspect(1.0);
            FigureMeasure m;
            const CellLayout before = compute_figure_layout(snap(*fig), W, H, &m).cells[0];
            AxisLimits p = pan_limits(before.tr, 37.0f, -21.0f);
            ax->set_xlim(p.xmin, p.xmax).set_ylim(p.ymin, p.ymax);
            const CellLayout panned = compute_figure_layout(snap(*fig), m, W, H).cells[0];
            AxisLimits z = zoom_limits(panned.tr, panned.frame.x + 10.0f, panned.frame.y + 30.0f, 0.7f);
            ax->set_xlim(z.xmin, z.xmax).set_ylim(z.ymin, z.ymax);
            const CellLayout zoomed = compute_figure_layout(snap(*fig), m, W, H).cells[0];
            check(near_px(panned.frame.h, before.frame.h) && near_px(zoomed.frame.h, before.frame.h)
                      && near_px(zoomed.frame.y, before.frame.y) && near_px(zoomed.frame.w, before.frame.w),
                  "aspect box: pan and zoom leave the frame where it was");

            // Log-log: decades.
            auto lf = Figure::create();
            auto la = lf->add_subplot(1, 1, 1);
            la->set_xscale(Scale::Log).set_yscale(Scale::Log).set_xlim(1.0, 1e4).set_ylim(1.0, 100.0);
            la->set_aspect(1.0);
            const CellLayout lc = cell0(*lf);
            check(std::abs(lc.frame.w - 2.0f * lc.frame.h) < 1.0f, "aspect box: log-log, 4 decades by 2 draw 2:1");

            // Size for a frame ignores the shrink: the unshrunk frame is the request.
            auto sf = Figure::create();
            auto sa = sf->add_subplot(1, 1, 1);
            sa->line(kX, kY);
            const FigureSize s0 = sf->size_for_frame(500, 250);
            sa->set_aspect(1.0);
            const FigureSize s1 = sf->size_for_frame(500, 250);
            check(s0.width == s1.width && s0.height == s1.height, "aspect box: size_for_frame() is unchanged");

            // In a grid only that cell's frame moves.
            auto gf = Figure::create();
            auto g1 = gf->add_subplot(2, 1, 1);
            auto g2 = gf->add_subplot(2, 1, 2);
            g1->line(kX, kY);
            g2->line(kX, kY);
            const FigureLayout g0l = compute_figure_layout(snap(*gf), W, H);
            g1->set_aspect(1.0);
            const FigureLayout gl = compute_figure_layout(snap(*gf), W, H);
            const PlotRect& a = gl.cells[1].frame;
            const PlotRect& b = g0l.cells[1].frame;
            check(a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h, "aspect box: the other cell keeps its frame");
            check(gl.cells[0].frame.w < g0l.cells[0].frame.w - 10.0f, "aspect box: the cell with it narrows");
        }

        void test_datalim() {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(kX, kY).set_xlim(0.0, 10.0).set_ylim(0.0, 10.0);
            const CellLayout full = cell0(*fig);
            ax->set_aspect(1.0, Adjustable::Datalim);
            const CellLayout c = cell0(*fig);
            std::printf("  datalim: x %g..%g, y %g..%g\n", c.tr.xmin, c.tr.xmax, c.tr.ymin, c.tr.ymax);
            check(c.frame.x == full.frame.x && c.frame.w == full.frame.w && c.frame.h == full.frame.h,
                  "aspect datalim: the frame stays");
            check(std::abs(drawn_ratio(c) - 1.0) < 1e-6, "aspect datalim: the limits draw the ratio");
            check(c.tr.xmin < 0.0 && c.tr.xmax > 10.0 && std::abs(c.tr.xmin + c.tr.xmax - 10.0) < 1e-9
                      && c.tr.ymin == 0.0 && c.tr.ymax == 10.0,
                  "aspect datalim: a wide frame widens x about its centre");
            check(ax->xlim().lo == 0.0 && ax->xlim().hi == 10.0, "aspect datalim: xlim() reports them unwidened");
            check(c.xticks.front().value < 0.0 || c.xticks.back().value > 10.0,
                  "aspect datalim: the ticks cover the widened view");

            // x shared: y takes the change, even shrinking.
            auto f2 = Figure::create();
            auto a = f2->add_subplot(1, 2, 1);
            auto b = f2->add_subplot(1, 2, 2);
            a->sharex(*b);
            a->set_xlim(0.0, 10.0).set_ylim(0.0, 10.0);
            a->set_aspect(1.0, Adjustable::Datalim);
            const FigureLayout l2 = compute_figure_layout(snap(*f2), W, H);
            const CellLayout& c2 = l2.cells[0];
            check(c2.tr.xmin == 0.0 && c2.tr.xmax == 10.0 && std::abs(drawn_ratio(c2) - 1.0) < 1e-6,
                  "aspect datalim: x shared, y adjusts and x stays");
            check(l2.cells[1].tr.xmin == 0.0 && l2.cells[1].tr.xmax == 10.0, "aspect datalim: the group keeps its x");

            // Log-log: decades.
            auto lf = Figure::create();
            auto la = lf->add_subplot(1, 1, 1);
            la->set_xscale(Scale::Log).set_yscale(Scale::Log).set_xlim(1.0, 100.0).set_ylim(1.0, 100.0);
            la->set_aspect(1.0, Adjustable::Datalim);
            const CellLayout lc = cell0(*lf);
            check(std::abs(drawn_ratio(lc) - 1.0) < 1e-6 && std::abs(std::log10(lc.tr.xmin * lc.tr.xmax) - 2.0) < 1e-9,
                  "aspect datalim: log-log widens in decades about the centre decade");
        }

        void test_edits() {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 2, 1);
            auto other = fig->add_subplot(1, 2, 2);
            ax->line(kX, kY);
            auto& fi = detail::FigureAccess::impl(*fig);
            // Nothing published yet: no snapshot to compare with.
            const auto s0 = fi.snapshot_box.load();
            const unsigned long long gen0 = s0 ? s0->layout_generation : 0;

            FigureEdits e;
            AxesEdit ae;
            ae.aspect = AxesAspect{1.0, Adjustable::Box};
            e.per_axes.push_back({1, ae});
            const FigureEdits undo = fi.apply_edits(e);
            const auto s = fi.snapshot_box.load();
            check(s->axes[0].snap2d()->aspect == AxesAspect{1.0, Adjustable::Box}, "aspect edits: reach the snapshot");
            check(s->layout_generation != gen0, "aspect edits: a change is a layout change");
            fi.apply_edits_and_publish();
            check(ax->aspect() == 1.0, "aspect edits: the caller's replay lands on the live axes");
            fi.apply_edits(undo);
            check(fi.snapshot_box.load()->axes[0].snap2d()->aspect.ratio == 0.0, "aspect edits: undo restores auto");
            fi.apply_edits_and_publish();

            // Both shared: a Datalim edit is skipped on both sides.
            ax->sharex(*other).sharey(*other);
            FigureEdits d;
            AxesEdit de;
            de.aspect = AxesAspect{1.0, Adjustable::Datalim};
            d.per_axes.push_back({1, de});
            fi.apply_edits(d);
            fi.apply_edits_and_publish();
            check(fi.snapshot_box.load()->axes[0].snap2d()->aspect.ratio == 0.0 && ax->aspect() == 0.0,
                  "aspect edits: Datalim on a both-shared axes is skipped");

            // A setter after the panel drew wins.
            FigureEdits late;
            AxesEdit le;
            le.aspect = AxesAspect{3.0, Adjustable::Box};
            le.style_seen = fi.snapshot_box.load()->axes[0].snap2d()->style_stamps;
            ax->set_aspect(2.0);
            late.per_axes.push_back({1, le});
            fi.apply_edits(late);
            fi.apply_edits_and_publish();
            check(ax->aspect() == 2.0, "aspect edits: a newer set_aspect() wins over an older edit");
        }
    } // namespace

    void test_aspect() {
        std::printf("\n[aspect: set_aspect(), Box and Datalim]\n");
        test_api();
        test_box();
        test_datalim();
        test_edits();
    }
} // namespace lt
