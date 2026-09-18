// The inverse layout and subplot spans. Part of sextant_layout_test; see
// layout_test.h.
#include "layout_test.h"

namespace lt {
    // ===========================================================================
    // The inverse layout
    // ===========================================================================

    // Round trip: derive a figure size for a wanted frame, lay out at that size,
    // and get the same frame back. Anything the inverse forgets shows as a miss of
    // exactly that size.
    void test_frame_size_round_trip() {
        std::printf("\n[frame size round trip]\n");

        struct Case {
            const char* name;
            int rows, cols, cells, slot;
            int last = 0; // a span from `slot` to `last`; the cells it covers are dropped
        };
        const Case cases[] = {
            {"1x1", 1, 1, 1, 1},
            {"2x3 grid, cell 1", 2, 3, 6, 1},
            {"2x3 grid, cell 5", 2, 3, 6, 5},
            {"3x1 column", 3, 1, 3, 2},
            // Spans: the inverse removes the gaps inside one.
            {"2x3 grid, span {4,6}", 2, 3, 6, 4, 6},
            {"3x2 grid, span {1,5}", 3, 2, 6, 1, 5},
            {"3x3 grid, span {5,9}", 3, 3, 9, 5, 9},
        };

        int worst_case_count = 0, trips = 0;
        float worst_err = 0.0f;
        std::string worst_desc;

        for (const auto& c: cases) {
            for (int variant = 0; variant < 5; ++variant) {
                auto fs = make_snapshot(c.rows, c.cols, c.cells, 1.0, 1.0e5);
                if (c.last > 0) {
                    sextant::AxesSlot span{c.rows, c.cols, c.slot, c.last};
                    std::erase_if(fs.axes, [&](const sextant::FigureAxesSnapshot& fa) {
                        const int r = fa.slot.row0(), q = fa.slot.col0();
                        return fa.slot.index != c.slot
                               && r >= span.row0() && r <= span.row1()
                               && q >= span.col0() && q <= span.col1();
                    });
                    for (auto& fa: fs.axes)
                        if (fa.slot.index == c.slot) fa.slot.last = c.last;
                }
                // Each variant adds something the inverse must handle (cumulative).
                if (variant >= 1) {
                    for (auto& fa: fs.axes) {
                        fa.snap2d()->title = "A title";
                        fa.snap2d()->xtitle = "x axis";
                        fa.snap2d()->ytitle = "y axis";
                    }
                }
                if (variant >= 2) {
                    fs.margins = {33.0f, 17.0f, 21.0f, 29.0f};
                    fs.col_gap = 13.0f;
                    fs.row_gap = 19.0f;
                }
                if (variant >= 3) {
                    fs.suptitle = "Figure title";
                    fs.suptitle_opts.fontsize = 26.0f;
                }
                if (variant >= 4) {
                    // Only the pinned cell carries this decoration.
                    for (auto& fa: fs.axes) {
                        if (fa.slot.index != c.slot) continue;
                        fa.snap2d()->legend_enabled = true;
                        fa.snap2d()->lines[0].opts.name = "a labelled series";
                    }
                }

                for (auto [fw, fh]: {
                         std::pair{120, 90}, std::pair{400, 300},
                         std::pair{640, 480}, std::pair{37, 23}
                     }) {
                    ++trips;
                    const sextant::LayoutSize s = sextant::figure_size_for_frame(
                        fs, c.slot, static_cast<float>(fw), static_cast<float>(fh));

                    const int W = static_cast<int>(std::lround(s.width));
                    const int H = static_cast<int>(std::lround(s.height));
                    const auto layout = sextant::compute_figure_layout(fs, W, H);

                    const sextant::CellLayout* cell = nullptr;
                    for (const auto& cl: layout.cells)
                        if (cl.slot.index == c.slot) {
                            cell = &cl;
                            break;
                        }
                    if (!cell) {
                        ++worst_case_count;
                        continue;
                    }

                    const float ew = std::fabs(cell->frame.w - static_cast<float>(fw));
                    const float eh = std::fabs(cell->frame.h - static_cast<float>(fh));
                    const float err = std::max(ew, eh);
                    // One pixel of slack for rounding the figure size.
                    if (err > 1.0f) {
                        ++worst_case_count;
                        if (err > worst_err) {
                            worst_err = err;
                            char buf[256];
                            std::snprintf(buf, sizeof(buf),
                                          "%s variant %d: asked %dx%d, got %.2fx%.2f at figure %dx%d",
                                          c.name, variant, fw, fh,
                                          static_cast<double>(cell->frame.w),
                                          static_cast<double>(cell->frame.h), W, H);
                            worst_desc = buf;
                        }
                    }
                }
            }
        }

        check(worst_case_count == 0,
              "every frame round-trips within a pixel (" + std::to_string(worst_case_count) +
              " misses, worst: " + (worst_desc.empty() ? "none" : worst_desc) + ")");
        std::printf("  %zu grids x 5 variants x 4 sizes = %d round trips, %d misses\n",
                    std::size(cases), trips, worst_case_count);
    }

    // The inverse must change with each input (the round trip alone passes when
    // an input is zero).
    void test_frame_size_responds_to_layout() {
        std::printf("\n[inverse responds to layout]\n");

        auto base = make_snapshot(2, 2, 4);
        const auto size_of = [](const sextant::FigureSnapshot& fs) {
            return sextant::figure_size_for_frame(fs, 1, 300.0f, 200.0f);
        };
        const sextant::LayoutSize b = size_of(base);

        auto bigger_margins = base;
        bigger_margins.margins = {50.0f, 50.0f, 50.0f, 50.0f};
        auto bigger_gaps = base;
        bigger_gaps.col_gap = 40.0f;
        bigger_gaps.row_gap = 40.0f;
        auto with_suptitle = base;
        with_suptitle.suptitle = "S";
        auto with_titles = base;
        for (auto& fa: with_titles.axes) {
            fa.snap2d()->title = "T";
            fa.snap2d()->xtitle = "x";
            fa.snap2d()->ytitle = "y";
        }
        auto with_legend = base;
        with_legend.axes[0].snap2d()->legend_enabled = true;
        with_legend.axes[0].snap2d()->lines[0].opts.name = "series";

        check(size_of(bigger_margins).width > b.width, "larger margins need a larger figure");
        check(size_of(bigger_gaps).width > b.width, "larger gaps need a larger figure");
        check(size_of(with_suptitle).height > b.height, "a suptitle needs a taller figure");
        check(size_of(with_titles).width > b.width, "axis titles need a larger figure");
        check(size_of(with_legend).width > b.width, "a legend needs a wider figure");
        check(std::fabs(size_of(with_legend).height - b.height) < 0.001f,
              "...but not a taller one — the legend is carved horizontally");

        // Only the named slot is pinned (slot 1 carries the legend).
        const auto s1 = sextant::figure_size_for_frame(with_legend, 1, 300.0f, 200.0f);
        const auto s2 = sextant::figure_size_for_frame(with_legend, 2, 300.0f, 200.0f);
        check(s1.width > s2.width,
              "the slot argument matters: a legend-bearing cell needs more figure than a bare one");
        std::printf("  slot 1 (legend) %.0f wide vs slot 2 (bare) %.0f\n",
                    static_cast<double>(s1.width), static_cast<double>(s2.width));
    }

    // Both public entry points, checked against the emitted SVG: resize_to_frame()
    // must set savefig()'s default size.
    void test_public_frame_resize() {
        std::printf("\n[public frame resize]\n");

        std::vector<double> x(40), y(40);
        for (int i = 0; i < 40; ++i) {
            x[i] = i;
            y[i] = std::sin(i * 0.2);
        }

        auto fig = sextant::Figure::create({.width = 640, .height = 480});
        fig->axes()->line(x, y).set_title("T").set_xtitle("x").set_ytitle("y");

        const sextant::FigureSize s = fig->size_for_frame(420, 260);
        check(s.width > 420 && s.height > 260,
              "size_for_frame() returns a figure larger than the frame it must contain");

        fig->resize_to_frame(420, 260);
        fig->savefig("frame_resize.svg");

        sextant::PlotRect r{};
        check(read_first_svg_frame("frame_resize.svg", r), "SVG written and parsed");
        check(std::fabs(r.w - 420.0f) <= 1.0f && std::fabs(r.h - 260.0f) <= 1.0f,
              "the saved figure's plot frame is the requested 420x260 (got " +
              std::to_string(r.w) + "x" + std::to_string(r.h) + ")");

        // Changing margins changes the figure size but not the frame.
        fig->set_margins({60.0f, 60.0f, 60.0f, 60.0f});
        const sextant::FigureSize s2 = fig->size_for_frame(420, 260);
        check(s2.width > s.width && s2.height > s.height,
              "wider margins raise the figure size needed for the same frame");

        fig->resize_to_frame(420, 260);
        fig->savefig("frame_resize_margins.svg");
        sextant::PlotRect r2{};
        check(read_first_svg_frame("frame_resize_margins.svg", r2), "second SVG parsed");
        check(std::fabs(r2.w - 420.0f) <= 1.0f && std::fabs(r2.h - 260.0f) <= 1.0f,
              "and the frame is still 420x260 after the margin change");
        check(std::fabs(r2.x - r.x) > 1.0f,
              "while the frame itself has moved, so the margins really were applied");

        std::printf("  frame 420x260 -> figure %dx%d, then %dx%d with 60px margins\n",
                    s.width, s.height, s2.width, s2.height);
    }

    // Subplots spanning cells, and the shape-less add_subplot() overloads. Mixed
    // grid shapes and shared cells are refused.
    void test_subplot_spans() {
        std::printf("\n[subplot spans on one grid]\n");

        using namespace sextant;
        auto threw = [](auto&& fn) {
            try {
                fn();
                return false;
            } catch (const std::invalid_argument&) { return true; }
        };
        auto near_px = [](float a, float b) { return std::fabs(a - b) <= 1e-3f; };

        // ---- The API's rules.
        {
            auto fig = Figure::create({.width = 600, .height = 400});
            check(threw([&] { fig->add_subplot(1); }) && threw([&] { fig->add_subplot({1, 2}); }) &&
                  threw([&] { fig->add_subplot3d(1); }) && threw([&] { fig->add_subplot3d({1, 2}); }),
                  "spans: a shape-less call with no grid shape fixed yet throws");

            auto a1 = fig->add_subplot(2, 3, 1);
            auto a2 = fig->add_subplot(2);
            auto a3 = fig->add_subplot3d(3);
            auto bottom = fig->add_subplot({4, 6});
            check(a1 && a2 && a3 && bottom && a2 != a1,
                  "spans: shape-less cells and a span on the fixed 2x3 grid");
            check(fig->add_subplot(2) == a2 && fig->add_subplot(2, 3, 2) == a2 &&
                  fig->add_subplot3d(3) == a3,
                  "spans: a shape-less re-request returns the same subplot as the shaped one");
            check(fig->add_subplot({4, 6}) == bottom && fig->add_subplot(2, 3, {4, 6}) == bottom,
                  "spans: re-requesting the same span returns it");
            check(threw([&] { fig->add_subplot(5); }) && threw([&] { fig->add_subplot(4); }),
                  "spans: a single index inside a span throws, its first cell included");
            check(threw([&] { fig->add_subplot({4, 5}); }),
                  "spans: so does a span that only partly matches one");
            check(threw([&] { fig->add_subplot3d({4, 6}); }),
                  "spans: and a span of the other kind");
            check(threw([&] { fig->add_subplot(2, 2, 1); }) &&
                  threw([&] { fig->add_subplot(1, 3, {1, 3}); }),
                  "spans: a different grid shape still throws, span or not");
            check(threw([&] { fig->add_subplot(7); }) && threw([&] { fig->add_subplot({0, 1}); }),
                  "spans: out-of-range cells throw");

            auto fig2 = Figure::create({.width = 600, .height = 400});
            fig2->add_subplot(2, 3, 5);
            check(threw([&] { fig2->add_subplot({3, 4}); }),
                  "spans: {3, 4} on 2x3 is no rectangle (last is left of first) and throws");
            check(threw([&] { fig2->add_subplot({2, 1}); }),
                  "spans: nor is a span whose last cell precedes its first");
            check(threw([&] { fig2->add_subplot({2, 6}); }),
                  "spans: a span over an occupied single cell throws");
            check(fig2->add_subplot({1, 4}) != nullptr,
                  "spans: while one beside it does not");

            auto fig3 = Figure::create({.width = 600, .height = 400});
            auto implicit = fig3->axes();
            check(fig3->add_subplot(1) == implicit,
                  "spans: axes() fixes a 1x1 grid, and add_subplot(1) is that axes");
            check(threw([&] { fig3->add_subplot(2); }) && threw([&] { fig3->add_subplot(2, 2, 1); }),
                  "spans: which stays strict -- nothing else fits on it");
        }

        // ---- A span's cell runs from its first cell to its last, gaps included,
        // and its frame lines up with the cells it covers.
        {
            auto fs = make_snapshot(2, 3, 4); // cells 1, 2, 3 and 4
            fs.axes[3].slot.last = 6; // 4 becomes the bottom row
            fs.col_gap = 17.0f;
            fs.row_gap = 11.0f;
            fs.margins = {30.0f, 20.0f, 15.0f, 25.0f};
            for (auto& fa: fs.axes) {
                fa.snap2d()->title = "T";
                fa.snap2d()->ytitle = "y";
            }
            const FigureLayout lay = compute_figure_layout(fs, 900, 600);

            auto cell_of = [&](int index) -> const CellLayout* {
                for (const auto& c: lay.cells) if (c.slot.index == index) return &c;
                return nullptr;
            };
            const CellLayout *c1 = cell_of(1), *c3 = cell_of(3), *sp = cell_of(4);
            check(c1 && c3 && sp, "spans: every slot is laid out");
            if (c1 && c3 && sp) {
                check(near_px(sp->cell.x, c1->cell.x) &&
                      near_px(sp->cell.x + sp->cell.w, c3->cell.x + c3->cell.w),
                      "spans: the span's cell runs from the first column's left edge to the last's right");
                check(near_px(sp->cell.y, c1->cell.y + c1->cell.h + fs.row_gap),
                      "spans: one row gap below the row above it");
                check(near_px(sp->frame.x, c1->frame.x) &&
                      near_px(sp->frame.x + sp->frame.w, c3->frame.x + c3->frame.w),
                      "spans: and its frame lines up with the outer frames of the cells it covers");
                std::printf("  2x3 with {4,6}: span frame x %.1f..%.1f, top row %.1f..%.1f\n",
                            static_cast<double>(sp->frame.x),
                            static_cast<double>(sp->frame.x + sp->frame.w),
                            static_cast<double>(c1->frame.x),
                            static_cast<double>(c3->frame.x + c3->frame.w));
            }

            // A click in a gap inside the span selects the span.
            std::vector<AxesLayout> al;
            for (const auto& c: lay.cells) {
                AxesLayout a;
                a.slot = c.slot;
                a.cell = c.cell;
                al.push_back(a);
            }
            if (c1 && sp) {
                const float gap_x = c1->cell.x + c1->cell.w + fs.col_gap * 0.5f;
                const AxesLayout* hit = find_cell_at(al, gap_x, sp->cell.y + sp->cell.h * 0.5f);
                check(hit && hit->slot.index == 4,
                      "spans: a click on a gap the span covers selects the span");
                check(find_cell_at(al, gap_x, c1->cell.y + c1->cell.h * 0.5f) == nullptr,
                      "spans: while the same gap between two single cells belongs to neither");
            }
        }
    }
} // namespace lt
