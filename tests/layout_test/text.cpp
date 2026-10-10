// In-plot text and annotations: Axes::text()/annotate() and the
// Axes3D ones. Placement (data, frame fraction, blended), the block layout, the
// hide rule, the arrow, pixels and SVG, read-back and set_text_data(), and the
// Data-panel ops with their inverses. Part of sextant_layout_test; see
// layout_test.h.
#include "layout_test.h"
#include "figure_impl.h"
#include "renderer/text_plan.h"

namespace lt {
    using namespace sextant;

    namespace {
        using AxesImpl   = detail::FigureAccess::Impl::AxesImpl;
        using Axes3DImpl = detail::FigureAccess::Impl::Axes3DImpl;

        // Pixels within `tol` of an opaque RGB, optionally inside a rectangle.
        std::size_t count_near(const RgbaImage& img, int r, int g, int b, int tol,
                               int x0 = 0, int y0 = 0, int x1 = 1 << 30, int y1 = 1 << 30) {
            std::size_t n = 0;
            for (int y = std::max(0, y0); y < std::min(img.height, y1); ++y)
                for (int x = std::max(0, x0); x < std::min(img.width, x1); ++x) {
                    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
                    if (std::abs(img.pixels[i] - r) <= tol && std::abs(img.pixels[i + 1] - g) <= tol
                        && std::abs(img.pixels[i + 2] - b) <= tol)
                        ++n;
                }
            return n;
        }

        std::size_t count_of(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }

        const Color kRed{1.0f, 0.0f, 0.0f, 1.0f};
        const Color kGreen{0.0f, 0.8f, 0.0f, 1.0f};
        const Color kBlue{0.0f, 0.0f, 1.0f, 1.0f};

        TextOptions red(float size = 24.0f) {
            TextOptions o;
            o.color = kRed;
            o.fontsize = size;
            return o;
        }

        // A 400x300 figure, limits 0..10 both ways.
        struct Fig2 {
            std::shared_ptr<Figure> fig;
            std::shared_ptr<Axes> ax;
            Fig2() {
                FigureOptions fo;
                fo.width = 400;
                fo.height = 300;
                fig = Figure::create(fo);
                ax = fig->add_subplot(1, 1, 1);
                ax->set_xlim(0, 10).set_ylim(0, 10);
            }
            std::string svg() {
                auto& fi = detail::FigureAccess::impl(*fig);
                return render_figure_svg(*fi.host_frame(), 400, 300);
            }
            CellLayout cell() {
                auto& fi = detail::FigureAccess::impl(*fig);
                return compute_figure_layout(*fi.host_frame(), 400, 300).cells[0];
            }
        };

        TextPlot plot_of(std::string s, Pos x, Pos y, TextOptions o = {}) {
            TextPlot p;
            p.content.text = std::move(s);
            p.content.x = x;
            p.content.y = y;
            p.opts.text = std::move(o);
            return p;
        }

        // A 100x100 frame at (50, 50) showing 0..10 both ways.
        CoordTransform unit_tr() {
            CoordTransform tr{};
            tr.xmin = 0.0; tr.xmax = 10.0; tr.ymin = 0.0; tr.ymax = 10.0;
            tr.px = 50.0f; tr.py = 50.0f; tr.pw = 100.0f; tr.ph = 100.0f;
            return tr;
        }
        const PlotRect kFrame{50.0f, 50.0f, 100.0f, 100.0f};

        bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }
    } // namespace

    // Where a block lands against its anchor, from the plan alone.
    static void test_text_plan() {
        const CoordTransform tr = unit_tr();
        const FontVMetrics vm = font_vmetrics("", 20.0f);
        TextOptions o;
        o.fontsize = 20.0f;

        // Placement: data, fraction, blended; dx/dy y up.
        {
            o.dx = 3.0f;
            o.dy = 4.0f;
            const auto d = plan_texts({plot_of("a", 5.0, Pos::fraction(1.0), o)}, tr, kFrame);
            check(d.size() == 1 && near(d[0].ax, 103.0f) && near(d[0].ay, 46.0f),
                  "text plan: x in data, y a frame fraction, nudged by dx and dy (y up)");
            o.dx = o.dy = 0.0f;
        }

        // Baseline (the default): the only line's baseline on the anchor.
        {
            const auto d = plan_texts({plot_of("Ag", 5.0, 5.0, o)}, tr, kFrame);
            check(d.size() == 1 && d[0].lines.size() == 1 && near(d[0].lines[0].y, 0.0f)
                  && near(d[0].lines[0].x, 0.0f) && d[0].ha == HAlign::Left,
                  "text plan: left/baseline puts the baseline's start on the anchor");
            check(near(d[0].ax, 100.0f) && near(d[0].ay, 100.0f), "text plan: (5, 5) is the frame's centre");
        }

        // Top, Center, Bottom over two lines; Right and Center horizontally.
        {
            o.linespacing = 1.5f;
            const float adv = 30.0f, h = vm.ascent - vm.descent + adv;
            o.va = VAlign::Top;
            auto d = plan_texts({plot_of("one\ntwo", 5.0, 5.0, o)}, tr, kFrame);
            check(d[0].lines.size() == 2 && near(d[0].lines[0].y, vm.ascent)
                  && near(d[0].lines[1].y, vm.ascent + adv),
                  "text plan: Top hangs the first line's ascent from the anchor; lines linespacing apart");
            o.va = VAlign::Bottom;
            d = plan_texts({plot_of("one\ntwo", 5.0, 5.0, o)}, tr, kFrame);
            check(near(d[0].lines[1].y, vm.descent), "text plan: Bottom stands the last descent on it");
            o.va = VAlign::Center;
            d = plan_texts({plot_of("one\ntwo", 5.0, 5.0, o)}, tr, kFrame);
            check(near(d[0].lines[0].y, -h * 0.5f + vm.ascent), "text plan: Center centres the block");
            o.va = VAlign::Baseline;
            d = plan_texts({plot_of("one\ntwo", 5.0, 5.0, o)}, tr, kFrame);
            check(near(d[0].lines[1].y, 0.0f), "text plan: Baseline is the last line's baseline");

            const float w = std::max(text_width("", 20.0f, "one"), text_width("", 20.0f, "two"));
            o.ha = HAlign::Right;
            d = plan_texts({plot_of("one\ntwo", 5.0, 5.0, o)}, tr, kFrame);
            check(near(d[0].lines[0].x, 0.0f) && d[0].ha == HAlign::Right,
                  "text plan: Right aligns every line's end on the anchor");
            o.ha = HAlign::Center;
            o.edge_linewidth = 1.0f;
            o.pad = 5.0f;
            d = plan_texts({plot_of("one\ntwo", 5.0, 5.0, o)}, tr, kFrame);
            check(d[0].has_box && near(d[0].box.x, -w * 0.5f - 5.0f) && near(d[0].box.w, w + 10.0f)
                  && near(d[0].box.h, h + 10.0f),
                  "text plan: the box is the block padded by `pad` on every side");
            o = TextOptions{};
            o.fontsize = 20.0f;
        }

        // No box unless something of it shows; rotation is clockwise radians.
        {
            o.rotation = 90.0f;
            const auto d = plan_texts({plot_of("a", 5.0, 5.0, o)}, tr, kFrame);
            check(!d[0].has_box && near(d[0].angle, -1.5707963f, 1e-5f),
                  "text plan: no fill and no outline draw no box; 90 degrees CCW is -pi/2");
            o.rotation = 0.0f;
        }

        // Hidden while a data coordinate is out of view; fractions never are.
        {
            const auto d = plan_texts({plot_of("a", 11.0, 5.0, o), plot_of("b", 5.0, -0.5, o),
                                       plot_of("c", Pos::fraction(1.5), Pos::fraction(-0.2), o),
                                       plot_of("d", 10.0, 0.0, o)},
                                      tr, kFrame);
            check(d.size() == 2 && d[0].lines[0].text == "c" && d[1].lines[0].text == "d",
                  "text plan: data coordinates out of view hide it (the limits themselves don't); "
                  "fractions draw anywhere");
            CoordTransform inv = tr;
            std::swap(inv.xmin, inv.xmax);
            check(plan_texts({plot_of("a", 3.0, 3.0, o)}, inv, kFrame).size() == 1,
                  "text plan: an inverted axis still counts as in view");
        }

        // The arrow: from the box edge to the point, a filled head ending on it.
        {
            o.va = VAlign::Center;   // the block's centre on the anchor: a level shaft
            TextPlot p = plot_of("label", 2.0, 5.0, o);
            p.content.arrow = true;
            p.content.px = 8.0;
            p.content.py = 5.0;
            p.opts.arrow.gap_text = 0.0f;
            auto d = plan_texts({p}, tr, kFrame);
            const float w = text_width("", 20.0f, "label");
            check(d.size() == 1 && d[0].heads.size() == 1 && d[0].heads[0].kind == ArrowHead::Filled
                  && near(d[0].heads[0].xy[2], 130.0f) && near(d[0].heads[0].xy[3], 100.0f, 0.5f),
                  "text plan: a filled head's tip is on the point");
            check(d[0].shaft.size() >= 4 && near(d[0].shaft[0], 70.0f + w, 0.05f)
                  && near(d[0].shaft[d[0].shaft.size() - 2], 120.0f),
                  "text plan: the shaft leaves the text's edge and stops at the head's base");

            p.opts.arrow.gap_point = 5.0f;
            p.opts.arrow.head = ArrowHead::Open;
            p.opts.arrow.tail = ArrowHead::Bar;
            d = plan_texts({p}, tr, kFrame);
            check(d[0].heads.size() == 2 && near(d[0].heads[0].xy[2], 125.0f)
                  && near(d[0].shaft[d[0].shaft.size() - 2], 125.0f) && d[0].heads[1].kind == ArrowHead::Bar,
                  "text plan: gap_point stops short; an open head keeps the shaft to its tip; a tail");

            // With no text the arrow runs anchor to point whatever the font: a
            // 60 px chord whose control point is 18 px off it, so the curve
            // peaks 9 px out.
            TextPlot bare = p;
            bare.content.text.clear();
            bare.opts.arrow.arc = 0.3f;
            d = plan_texts({bare}, tr, kFrame);
            float maxdev = 0.0f;
            for (std::size_t i = 1; i < d[0].shaft.size(); i += 2)
                maxdev = std::max(maxdev, std::fabs(d[0].shaft[i] - 100.0f));
            check(d[0].shaft.size() > 4 && maxdev > 8.0f && maxdev < 9.5f,
                  "text plan: arc bows the shaft off the straight line");
            p.opts.arrow.arc = 0.3f;

            p.content.px = 11.0;
            check(plan_texts({p}, tr, kFrame).empty(), "text plan: an annotation hides with its point");
            p.content.px = 2.0;
            p.content.py = 5.0;
            p.opts.arrow.arc = 0.0f;
            d = plan_texts({p}, tr, kFrame);
            check(d.size() == 1 && d[0].shaft.empty() && d[0].heads.empty(),
                  "text plan: no arrow when the point is under the text");
        }
    }

    static void test_text_api() {
        Fig2 f;
        auto& ax = *f.ax;
        ax.text("a", 1.0, 2.0);
        ax.text("b", 3, Pos::fraction(0.5), red());
        ax.annotate(4.0, 5.0, "c", Pos::fraction(0.1), 6.0);
        check(ax.text_count() == 3, "text api: three texts");
        const TextData a = ax.text_data(0), b = ax.text_data(1), c = ax.text_data(2);
        check(a.text == "a" && a.x.v == 1.0 && a.x.space == Coords::Data && !a.arrow,
              "text api: text() reads back");
        check(b.y.space == Coords::Fraction && b.y.v == 0.5 && b.x.v == 3.0, "text api: blended placement reads back");
        check(c.arrow && c.px == 4.0 && c.py == 5.0 && c.x.space == Coords::Fraction && c.y.v == 6.0,
              "text api: annotate() reads back its point and text position");

        bool threw = false;
        try { ax.text("x", std::nan(""), 0.0); } catch (const std::invalid_argument&) { threw = true; }
        check(threw && ax.text_count() == 3, "text api: a non-finite coordinate throws and adds nothing");
        threw = false;
        try { (void)ax.text_data(3); } catch (const std::out_of_range&) { threw = true; }
        check(threw, "text api: text_data past the count throws");

        ax.set_text_data(2, "c2", 7.0, 8.0);
        const TextData c2 = ax.text_data(2);
        check(c2.text == "c2" && c2.x.v == 7.0 && c2.x.space == Coords::Data && c2.arrow && c2.px == 4.0,
              "text api: set_text_data(i, s, x, y) keeps the arrow and its point");
        TextData nd = ax.text_data(0);
        nd.arrow = true;
        nd.px = 9.0;
        ax.set_text_data(0, nd);
        check(ax.text_data(0).arrow && ax.text_data(0).px == 9.0, "text api: set_text_data(i, data) can add an arrow");

        // Text does not touch the auto limits.
        auto g2 = Figure::create();
        auto gx = g2->add_subplot(1, 1, 1);
        const std::vector<double> x{0.0, 1.0}, y{0.0, 1.0};
        gx->line(x, y);
        const Range before = gx->xlim();
        gx->text("far", 100.0, 100.0);
        gx->annotate(-50.0, -50.0, "far too", 200.0, 200.0);
        check(gx->xlim().lo == before.lo && gx->xlim().hi == before.hi, "text api: text never widens the auto limits");
        gx->cla();
        check(gx->text_count() == 0, "text api: cla() clears texts");
    }

    static void test_text_pixels() {
        // A big red text in the middle draws red pixels near its anchor.
        {
            Fig2 f;
            f.ax->text("HHH", 5.0, 5.0, red(30.0f));
            const CellLayout c = f.cell();
            const int ax = static_cast<int>(c.tr.to_px(5.0)), ay = static_cast<int>(c.tr.to_py(5.0));
            const RgbaImage img = f.fig->render_rgba();
            const std::size_t near_anchor = count_near(img, 255, 0, 0, 60, ax, ay - 30, ax + 80, ay + 2);
            const std::size_t all = count_near(img, 255, 0, 0, 60);
            check(near_anchor > 100 && near_anchor == all,
                  "text pixels: drawn up and right of a left/baseline anchor, and nowhere else");
        }
        // Out of view: nothing.
        {
            Fig2 f;
            f.ax->text("HHH", 12.0, 5.0, red(30.0f));
            check(count_near(f.fig->render_rgba(), 255, 0, 0, 60) == 0, "text pixels: hidden out of view");
        }
        // A box and an arrow.
        {
            Fig2 f;
            TextOptions o = red(20.0f);
            o.background = kBlue;
            ArrowOptions a;
            a.color = kGreen;
            a.linewidth = 3.0f;
            f.ax->annotate(8.0, 2.0, "HH", 2.0, 7.0, o, a);
            const RgbaImage img = f.fig->render_rgba();
            check(count_near(img, 0, 0, 255, 10) > 200, "text pixels: the background fills a box");
            check(count_near(img, 0, 204, 0, 40) > 150, "text pixels: the arrow is drawn in its own color");
        }
        // clip_to_frame cuts at the frame; without it a text may overhang.
        {
            const auto overhang = [](bool clip) {
                Fig2 f;
                TextOptions o = red(30.0f);
                o.clip_to_frame = clip;
                f.ax->text("HHHHHH", Pos::fraction(0.9), Pos::fraction(0.5), o);
                const CellLayout c = f.cell();
                return count_near(f.fig->render_rgba(), 255, 0, 0, 60,
                                  static_cast<int>(c.frame.x + c.frame.w) + 2, 0);
            };
            check(overhang(false) > 50 && overhang(true) == 0, "text pixels: clip_to_frame stops it at the frame");
        }
        // For the eye: alignments, rotation, a box, every arrow head, an arc.
        {
            FigureOptions fo;
            fo.width = 640;
            fo.height = 420;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 10).set_ylim(0, 10).grid(true);
            const std::vector<double> x{1.0, 3.0, 5.0, 7.0, 9.0}, y{2.0, 4.0, 3.0, 6.0, 5.0};
            ax->line(x, y).scatter(x, y);
            TextOptions o;
            o.background = Color{1.0f, 1.0f, 0.85f, 1.0f};
            o.edge_linewidth = 1.0f;
            ax->annotate(7.0, 6.0, "peak\n(7, 6)", 8.5, 8.5, o);
            ArrowOptions curved;
            curved.arc = 0.3f;
            curved.head = ArrowHead::Open;
            ax->annotate(3.0, 4.0, "open, curved", Pos::fraction(0.05), Pos::fraction(0.9), {}, curved);
            ArrowOptions both;
            both.tail = ArrowHead::Filled;
            both.linestyle = LineStyle::Dashed;
            ax->annotate(5.0, 3.0, "both ends, dashed", 5.0, 0.8, TextOptions{.ha = HAlign::Center}, both);
            ArrowOptions bar;
            bar.head = ArrowHead::Bar;
            ax->annotate(1.0, 2.0, "bar", 0.5, 4.0, {}, bar);
            ax->text("threshold", 9.8, Pos::fraction(0.98),
                     TextOptions{.ha = HAlign::Right, .va = VAlign::Top});
            ax->text("rotated", 9.5, 1.0, TextOptions{.va = VAlign::Center, .rotation = 90.0f});
            fig->savefig("text_annotations.png");
            fig->savefig("text_annotations.svg");
        }
    }

    static void test_text_svg() {
        Fig2 f;
        TextOptions o = red();
        o.ha = HAlign::Center;
        o.rotation = 30.0f;
        o.edge_linewidth = 2.0f;
        f.ax->text("A<B", 5.0, 5.0, o);
        TextOptions c;
        c.clip_to_frame = true;
        f.ax->annotate(5.0, 5.0, "two\nlines", 2.0, 8.0, c);
        f.ax->text("hidden", 50.0, 5.0);
        const std::string s = f.svg();
        check(count_of(s, ">A&lt;B</text>") == 1 && count_of(s, "text-anchor=\"middle\"") >= 1,
              "text svg: escaped, centred");
        check(count_of(s, "rotate(-30)") == 1, "text svg: rotation turns its group");
        check(count_of(s, "stroke-width=\"2\"") >= 1 && count_of(s, "<rect x=") >= 1, "text svg: an outlined box");
        check(count_of(s, ">two</text>") == 1 && count_of(s, ">lines</text>") == 1,
              "text svg: one <text> per line");
        check(count_of(s, "<polygon fill=") >= 1 && count_of(s, "clip-path=\"url(#plotArea0)\"") >= 2,
              "text svg: a filled head; clip_to_frame uses the frame's clip");
        check(count_of(s, "hidden") == 0, "text svg: a hidden text writes nothing");
    }

    static void test_text_3d() {
        FigureOptions fo;
        fo.width = 500;
        fo.height = 400;
        auto fig = Figure::create(fo);
        auto ax = fig->add_subplot3d(1, 1, 1);
        ax->set_xlim(0, 1).set_ylim(0, 1).set_zlim(0, 1);
        ax->text("in", 0.5, 0.5, 0.5, red(20.0f));
        ax->text("out", 0.5, 0.5, 2.0, red(20.0f));
        ax->text2d("t = 1.5 s", 0.02, 0.95, TextOptions{.va = VAlign::Top});
        ax->annotate(1.0, 1.0, 1.0, "corner", -60.0, 30.0);
        check(ax->text_count() == 4, "text 3d: four texts");
        const Text3DData t2 = ax->text_data(2), an = ax->text_data(3);
        check(t2.in_frame && !t2.arrow && t2.x == 0.02 && an.arrow && an.dx == -60.0 && an.z == 1.0,
              "text 3d: text2d() and annotate() read back");
        bool threw = false;
        Text3DData bad = an;
        bad.in_frame = true;
        try { ax->set_text_data(3, bad); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "text 3d: arrow with in_frame throws");
        ax->set_text_data(2, "t = 2.0 s", 0.03, 0.9, 7.0);
        check(ax->text_data(2).in_frame && ax->text_data(2).text == "t = 2.0 s",
              "text 3d: set_text_data(i, s, x, y, z) keeps a text2d in the frame");

        // The annotation-invariance rule: two cameras a dolly apart move the
        // anchors but not the size.
        auto& fi = detail::FigureAccess::impl(*fig);
        const auto layout_at = [&](double zoom) {
            Camera3D cam = ax->camera();
            cam.projection = Projection::Perspective;
            cam.zoom = zoom;
            ax->set_camera(cam);
            fi.apply_edits_and_publish();   // a host publishes after a setter
            auto snap = fi.host_frame();
            const CellLayout c = compute_figure_layout(*snap, 500, 400).cells[0];
            return std::pair{plan_texts3d(snap->axes[0].snap3d()->texts, c.box3d->proj, c.frame),
                             render_figure_svg(*snap, 500, 400)};
        };
        const auto [near_plan, near_svg] = layout_at(1.0);
        const auto [far_plan, far_svg] = layout_at(0.5);
        check(near_plan.size() == 3 && far_plan.size() == 3, "text 3d: the point outside the limits is hidden");
        check(near_plan[0].fontsize == far_plan[0].fontsize && near_plan[0].lines[0].x == far_plan[0].lines[0].x
              && (near_plan[2].ax != far_plan[2].ax || near_plan[2].ay != far_plan[2].ay),
              "text 3d: a zoom moves the projected anchor, not the text's size");
        check(near_plan[1].ax == far_plan[1].ax && near_plan[1].ay == far_plan[1].ay,
              "text 3d: text2d stays put whatever the camera does");
        check(!near_plan[2].heads.empty() && count_of(near_svg, ">corner</text>") == 1
              && count_of(near_svg, "font-size=\"20\"") == 1 && count_of(far_svg, "font-size=\"20\"") == 1,
              "text 3d: annotate's arrow, and the SVG at either camera");
        const RgbaImage img = fig->render_rgba();
        check(count_near(img, 255, 0, 0, 100) > 20, "text 3d: drawn over the scene");
        fig->savefig("text_annotations3d.png");
    }

    // Data-panel ops: a text's content and its style, with inverses, through the
    // journal onto the live axes.
    static void test_text_edits() {
        auto fig = Figure::create();
        auto ax = fig->add_subplot(1, 2, 1);
        auto ax3 = fig->add_subplot3d(1, 2, 2);
        ax->text("a", 1.0, 2.0);
        ax->annotate(1.0, 1.0, "b", 3.0, 3.0);
        ax3->text("c", 0.5, 0.5, 0.5);
        auto& fi = detail::FigureAccess::impl(*fig);
        auto s0 = fi.host_frame();
        const std::string before = render_figure_svg(*s0, 640, 360);

        TextContent moved;
        moved.text = "moved";
        moved.x = Pos::fraction(0.5);
        moved.y = 4.0;
        TextStyle st;
        st.text.fontsize = 30.0f;
        st.arrow.head = ArrowHead::Bar;
        fi.edit_box.update(1, [&](AxesEdit& e) {
            e.plot_ops.push_back(TextDataEdit{0, moved});
            e.plot_styles.push_back({1, -1, st});
        });
        TextContent c3;
        c3.text = "c3";
        c3.x = 0.1;
        c3.y = 0.2;
        c3.z = 0.3;
        fi.edit_box.update3d(2, [&](AxesEdit3D& e) {
            e.plot_ops.push_back(TextDataEdit{0, c3});
            e.texts.push_back({0, st});
        });
        FigureEdits inv;
        auto s1 = fi.host_frame(&inv);
        const RenderSnapshot& r1 = *s1->axes[0].snap2d();
        const RenderSnapshot3D& q1 = *s1->axes[1].snap3d();
        check(r1.texts[0].content.text == "moved" && r1.texts[0].content.x.space == Coords::Fraction
              && r1.texts[1].opts.text.fontsize == 30.0f && r1.texts[1].opts.arrow.head == ArrowHead::Bar,
              "text edits: the content op and the style edit land on the snapshot");
        check(q1.texts[0].content.text == "c3" && q1.texts[0].opts.text.fontsize == 30.0f,
              "text edits: the 3D op and the 3D style lane land");
        check(inv.per_axes.size() == 1 && inv.per_axes[0].second.plot_ops.size() == 1
              && std::get<TextDataEdit>(inv.per_axes[0].second.plot_ops[0]).content.text == "a"
              && inv.per_axes3d.size() == 1 && inv.per_axes3d[0].second.texts.size() == 1,
              "text edits: each has its inverse");

        fi.apply_edits_and_publish();
        check(ax->text_data(0).text == "moved" && ax->text_data(0).x.space == Coords::Fraction
              && ax3->text_data(0).text == "c3",
              "text edits: the journal replays them onto the live axes");
        fi.apply_edits(inv);
        check(render_figure_svg(*fi.snapshot_box.load(), 640, 360) == before,
              "text edits: the inverse restores the picture");

        // A stale op (made before set_text_data) is dropped.
        auto s2 = fi.host_frame();
        const unsigned long long seen = s2->axes[0].snap2d()->texts[0].data_stamp;
        ax->set_text_data(0, "program", 1.0, 1.0);
        fi.apply_edits_and_publish();
        fi.edit_box.update(1, [&](AxesEdit& e) {
            TextDataEdit op{0, moved};
            op.seen = seen;
            e.plot_ops.push_back(op);
        });
        fi.host_frame();
        fi.apply_edits_and_publish();
        check(ax->text_data(0).text == "program", "text edits: an op over an older copy is dropped");

        // Removal and restore through the R9/R10 machinery.
        AxesImpl* a2 = fi.find_slot_impl(1);
        const ObjectId id = a2->texts[1].id;
        auto removed = a2->remove_object(id);
        check(removed && a2->texts.size() == 1 && a2->find_object(id) == std::nullopt,
              "text edits: a text is removed by id");
        check(a2->restore_object(std::move(*removed)) && a2->texts.size() == 2 && a2->texts[1].id == id,
              "text edits: and put back where it was");
    }

    void test_text() {
        std::printf("\n[text and annotations]\n");
        test_text_plan();
        test_text_api();
        test_text_pixels();
        test_text_svg();
        test_text_3d();
        test_text_edits();
    }
} // namespace lt
