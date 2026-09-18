// The computed layout: insets, frames, margins, the edit lane. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    // ===========================================================================
    // Phase 1 — the layout itself
    // ===========================================================================

    bool finite_rect(const sextant::PlotRect& r) {
        return std::isfinite(r.x) && std::isfinite(r.y) &&
               std::isfinite(r.w) && std::isfinite(r.h);
    }

    // Font size drives position: sweep each text size and require the reserved
    // space to track it, the text to stay inside, and the frame to give up exactly
    // what the decorations took.
    void test_font_size_drives_layout() {
        std::printf("\n[font size -> layout]\n");

        constexpr int W = 800, H = 600;
        float prev_top = -1.0f, prev_bottom = -1.0f, prev_left = -1.0f;
        float prev_frame_h = 1e9f, prev_frame_w = 1e9f;
        bool top_monotonic = true, bottom_monotonic = true, left_monotonic = true;
        bool frame_shrinks = true, title_contained = true, xtitle_contained = true;
        bool ytitle_contained = true, all_finite = true;

        for (float size: {6.0f, 9.0f, 12.0f, 18.0f, 24.0f, 32.0f, 48.0f, 64.0f}) {
            auto fs = make_snapshot(1, 1, 1);
            auto& snap = *fs.axes[0].snap2d();
            snap.title = "Axes title";
            snap.xtitle = "x axis";
            snap.ytitle = "y axis";
            snap.axes_style.title_fontsize = size;
            snap.axes_style.xtitle_fontsize = size;
            snap.axes_style.ytitle_fontsize = size;

            const auto layout = sextant::compute_figure_layout(fs, W, H);
            const auto& c = layout.cells[0];
            const auto& in = layout.cells[0].reserved;

            if (prev_top >= 0.0f) {
                if (in.top < prev_top) top_monotonic = false;
                if (in.bottom < prev_bottom) bottom_monotonic = false;
                if (in.left < prev_left) left_monotonic = false;
                if (c.frame.h > prev_frame_h) frame_shrinks = false;
                if (c.frame.w > prev_frame_w) frame_shrinks = false;
            }
            prev_top = in.top;
            prev_bottom = in.bottom;
            prev_left = in.left;
            prev_frame_h = c.frame.h;
            prev_frame_w = c.frame.w;

            if (!finite_rect(c.frame)) all_finite = false;

            // The title fits between the cell top and the frame.
            const float title_lh = sextant::font_vmetrics(snap.axes_style.font_path, size).line_height;
            const float cell_top = fs.margins.top;
            if (c.title_y - title_lh * 0.5f < cell_top - 0.51f) title_contained = false;
            if (c.title_y + title_lh * 0.5f > c.frame.y + 0.51f) title_contained = false;

            // Likewise the x and y titles within their margins.
            if (c.xtitle_y + title_lh * 0.5f > static_cast<float>(H) - fs.margins.bottom + 0.51f)
                xtitle_contained = false;
            if (c.ytitle_x - title_lh * 0.5f < fs.margins.left - 0.51f)
                ytitle_contained = false;

            std::printf("  %5.1f px: insets L%6.2f T%6.2f B%6.2f  frame %.1fx%.1f\n",
                        static_cast<double>(size),
                        static_cast<double>(in.left), static_cast<double>(in.top),
                        static_cast<double>(in.bottom),
                        static_cast<double>(c.frame.w), static_cast<double>(c.frame.h));
        }

        check(top_monotonic, "top inset grows with the title font size");
        check(bottom_monotonic, "bottom inset grows with the x-title font size");
        check(left_monotonic, "left inset grows with the y-title font size");
        check(frame_shrinks, "plot frame gives up exactly what the decorations take");
        check(title_contained, "the axes title's line fits in the band reserved above the frame");
        check(xtitle_contained, "the x title stays inside the figure's bottom margin");
        check(ytitle_contained, "the y title stays inside the figure's left margin");
        check(all_finite, "every frame is finite");
    }

    // Absent decorations cost nothing.
    void test_absent_decorations_cost_nothing() {
        std::printf("\n[absent decorations]\n");

        auto bare = make_snapshot(1, 1, 1);
        auto titled = make_snapshot(1, 1, 1);
        titled.axes[0].snap2d()->title = "T";
        titled.axes[0].snap2d()->xtitle = "x";
        titled.axes[0].snap2d()->ytitle = "y";

        const auto lb = sextant::compute_figure_layout(bare, 800, 600);
        const auto lt = sextant::compute_figure_layout(titled, 800, 600);

        check(lb.cells[0].reserved.top < lt.cells[0].reserved.top, "no title reserves less above the frame");
        check(lb.cells[0].reserved.bottom < lt.cells[0].reserved.bottom, "no x title reserves less below it");
        check(lb.cells[0].reserved.left < lt.cells[0].reserved.left, "no y title reserves less to its left");
        check(lb.cells[0].frame.w > lt.cells[0].frame.w &&
              lb.cells[0].frame.h > lt.cells[0].frame.h,
              "an undecorated axes gets the reclaimed space");
        std::printf("  bare   insets L%.2f T%.2f B%.2f\n",
                    static_cast<double>(lb.cells[0].reserved.left), static_cast<double>(lb.cells[0].reserved.top),
                    static_cast<double>(lb.cells[0].reserved.bottom));
        std::printf("  titled insets L%.2f T%.2f B%.2f\n",
                    static_cast<double>(lt.cells[0].reserved.left), static_cast<double>(lt.cells[0].reserved.top),
                    static_cast<double>(lt.cells[0].reserved.bottom));
    }

    // The left inset is the widest y tick label.
    void test_wide_tick_labels() {
        std::printf("\n[wide tick labels]\n");

        auto narrow = make_snapshot(1, 1, 1, 1.0, 1.0);
        auto wide = make_snapshot(1, 1, 1, 1.0, 1.0e8);

        const auto ln = sextant::compute_figure_layout(narrow, 800, 600);
        const auto lw = sextant::compute_figure_layout(wide, 800, 600);

        check(lw.cells[0].reserved.left > ln.cells[0].reserved.left,
              "1e8-scale y labels reserve more room than unit-scale ones");

        // Exactly the widest label plus tick and gap.
        const auto& st = wide.axes[0].snap2d()->axes_style;
        float widest = 0.0f;
        for (const auto& t: lw.cells[0].yticks)
            widest = std::max(widest, sextant::text_width(st.font_path, st.label_fontsize, t.label));
        const float expect = st.tick_length + sextant::kTickLabelGap + widest;
        check(std::fabs(lw.cells[0].reserved.left - expect) < 0.001f,
              "left inset == tick_length + gap + widest y label (" +
              std::to_string(lw.cells[0].reserved.left) + " vs " + std::to_string(expect) + ")");
        std::printf("  narrow L%.2f, wide L%.2f (widest label %.2f px)\n",
                    static_cast<double>(ln.cells[0].reserved.left), static_cast<double>(lw.cells[0].reserved.left),
                    static_cast<double>(widest));
    }

    // The zero tick prints "0" (accumulating `v += step` once gave
    // "-2.77556e-17"). Swept over ranges, since whether it drifts depends on the
    // binary arithmetic.
    void test_zero_tick() {
        std::printf("\n[the zero tick]\n");

        using namespace sextant;

        int spanning = 0, bad = 0;
        double worst = 0.0;
        const double ranges[][2] = {
            {-0.3, 0.3}, {-1.0, 1.0}, {-0.45, 0.45}, {-3.0, 7.0},
            {-0.7, 0.2}, {-12.0, 5.0}, {-0.05, 0.05}, {-250.0, 250.0},
            {-1.0, 0.0}, {0.0, 1.0}, {-6.4, 1.6}, {-0.123, 0.456},
        };
        for (const auto& r: ranges) {
            const std::vector<Tick> ts = generate_ticks(r[0], r[1]);
            for (const Tick& t: ts) {
                // The tick nearest zero (within a thousandth of the span).
                const double span = r[1] - r[0];
                if (std::fabs(t.value) > span * 1e-3) continue;
                ++spanning;
                if (t.label != "0") {
                    ++bad;
                    worst = std::max(worst, std::fabs(t.value));
                    std::printf("    %g..%g: zero tick reads \"%s\" (pos %g)\n",
                                r[0], r[1], t.label.c_str(), t.value);
                }
            }
        }
        std::printf("  %d ranges, %d zero ticks, %d mislabelled\n",
                    static_cast<int>(std::size(ranges)), spanning, bad);
        check(spanning >= 9,
              "zero tick: the sweep actually produces a zero tick on most of its ranges, "
              "so the check below is about the label rather than about an empty loop");
        check(bad == 0 && worst == 0.0,
              "zero tick: an axis that straddles zero labels it \"0\" -- not \"-0\", and "
              "not the residue of accumulating a step across the axis");
    }

    // Frames line up across the grid: shared left/right edges per column, top/
    // bottom per row. Different columns may differ in width.
    void test_grid_alignment() {
        std::printf("\n[grid alignment]\n");

        auto fs = make_snapshot(2, 3, 6);
        // Only one cell has a long y title and wide data.
        fs.axes[4].snap2d()->ytitle = "a considerably longer y title";
        fs.axes[1].snap2d()->title = "T";
        fs.col_gap = 12.0f;
        fs.row_gap = 14.0f;

        const auto layout = sextant::compute_figure_layout(fs, 1200, 800);

        bool cols_aligned = true, rows_aligned = true;
        for (std::size_t i = 0; i < 3; ++i) {
            // cells i and i+3 are the same column; 0..2 and 3..5 the two rows.
            const auto& a = layout.cells[i].frame;
            const auto& b = layout.cells[i + 3].frame;
            if (std::fabs(a.x - b.x) > 0.001f || std::fabs(a.w - b.w) > 0.001f)
                cols_aligned = false;
            for (std::size_t r: {std::size_t{0}, std::size_t{3}}) {
                const auto& f = layout.cells[r + i].frame;
                const auto& g = layout.cells[r].frame;
                if (std::fabs(f.y - g.y) > 0.001f || std::fabs(f.h - g.h) > 0.001f)
                    rows_aligned = false;
            }
        }

        check(cols_aligned, "frames in a column share their left and right edges");
        check(rows_aligned, "frames in a row share their top and bottom edges");
        // Column 1 (with cell 4's long title) reserves more on its left.
        check(layout.cells[1].frame.w < layout.cells[2].frame.w,
              "a column pays only for its own cells' labels");

        // The gap between cells is insets.right + col_gap + insets.left.
        const float measured = layout.cells[1].frame.x
                               - (layout.cells[0].frame.x + layout.cells[0].frame.w);
        const float expect = layout.cells[0].reserved.right + fs.col_gap + layout.cells[1].reserved.left;
        check(std::fabs(measured - expect) < 0.001f,
              "the column gap separates whole subplots, not bare frames (" +
              std::to_string(measured) + " vs " + std::to_string(expect) + ")");
    }

    // Margins are the figure's outer border; nothing else moves with them.
    void test_margins() {
        std::printf("\n[margins]\n");

        auto a = make_snapshot(1, 1, 1);
        auto b = make_snapshot(1, 1, 1);
        b.margins = {40.0f, 25.0f, 30.0f, 35.0f};

        const auto la = sextant::compute_figure_layout(a, 800, 600);
        const auto lb = sextant::compute_figure_layout(b, 800, 600);

        const float dx = lb.cells[0].frame.x - la.cells[0].frame.x;
        const float dy = lb.cells[0].frame.y - la.cells[0].frame.y;
        check(std::fabs(dx - (b.margins.left - a.margins.left)) < 0.001f,
              "the frame moves right by exactly the left-margin delta");
        check(std::fabs(dy - (b.margins.top - a.margins.top)) < 0.001f,
              "the frame moves down by exactly the top-margin delta");

        const float dw = la.cells[0].frame.w - lb.cells[0].frame.w;
        const float expect_dw = (b.margins.left + b.margins.right)
                                - (a.margins.left + a.margins.right);
        check(std::fabs(dw - expect_dw) < 0.001f,
              "the frame narrows by exactly the horizontal margin delta");
        check(la.cells[0].reserved.left == lb.cells[0].reserved.left && la.cells[0].reserved.top == lb.cells[0].reserved
              .top,
              "margins do not disturb the measured insets");
    }

    // A frame always stays positive, however little room is left.
    void test_degenerate_sizes() {
        std::printf("\n[degenerate sizes]\n");

        bool ok = true;
        for (auto [w, h]: {std::pair{40, 30}, std::pair{10, 10}, std::pair{1, 1}, std::pair{120, 60}}) {
            auto fs = make_snapshot(2, 2, 4);
            auto& s = *fs.axes[0].snap2d();
            s.title = "huge";
            s.xtitle = "huge";
            s.ytitle = "huge";
            s.axes_style.title_fontsize = 64.0f;
            s.axes_style.xtitle_fontsize = 64.0f;
            s.axes_style.ytitle_fontsize = 64.0f;
            s.axes_style.label_fontsize = 48.0f;

            const auto layout = sextant::compute_figure_layout(fs, w, h);
            for (const auto& c: layout.cells) {
                if (!finite_rect(c.frame)) ok = false;
                if (c.frame.w < sextant::kMinFrameSize || c.frame.h < sextant::kMinFrameSize) ok = false;
            }
        }
        check(ok, "a frame never collapses or inverts, however little room is left");
    }

    // Legend and colorbar are carved from their own cell, sized from their text.
    void test_legend_and_colorbar_carve() {
        std::printf("\n[legend / colorbar]\n");

        auto plain = make_snapshot(1, 1, 1);
        auto legended = make_snapshot(1, 1, 1);
        legended.axes[0].snap2d()->legend_enabled = true;
        legended.axes[0].snap2d()->lines[0].opts.name = "short";

        auto long_legend = make_snapshot(1, 1, 1);
        long_legend.axes[0].snap2d()->legend_enabled = true;
        long_legend.axes[0].snap2d()->lines[0].opts.name = "a much longer legend label";

        const auto lp = sextant::compute_figure_layout(plain, 800, 600);
        const auto ll = sextant::compute_figure_layout(legended, 800, 600);
        const auto lo = sextant::compute_figure_layout(long_legend, 800, 600);

        check(!lp.cells[0].has_legend(), "no legend when none was enabled");
        check(ll.cells[0].has_legend(), "a labelled series gets a legend box");
        check(lo.cells[0].legend.w > ll.cells[0].legend.w,
              "a longer label makes a wider box");
        check(lo.cells[0].frame.w < ll.cells[0].frame.w,
              "and the frame gives up that width");
        check(ll.cells[0].legend.x >= ll.cells[0].frame.x + ll.cells[0].frame.w,
              "the legend box sits outside the frame, never over the data");

        // The box holds its text: pad + swatch + gap + widest label.
        const auto& e = lo.cells[0].legend_entries;
        const auto& opts = long_legend.axes[0].snap2d()->legend_opts;
        float widest = 0.0f;
        for (const auto& en: e)
            widest = std::max(widest, sextant::text_width(opts.font_path, opts.fontsize, en.name));
        const float expect = sextant::kLegendPad * 2.0f + sextant::kLegendSwatchW
                             + sextant::kLegendGap + widest;
        check(std::fabs(lo.cells[0].legend.w - expect) < 0.001f,
              "the legend box is exactly wide enough for its widest label");
        std::printf("  legend box %.2f px for \"%s\" (%.2f px of text)\n",
                    static_cast<double>(lo.cells[0].legend.w),
                    e.empty() ? "" : e[0].name.c_str(), static_cast<double>(widest));
    }

    // PNG and SVG put the frame in the same place, checked on the output (the SVG
    // rect and the drawn spine), not just the shared layout.
    void test_png_svg_frame_agreement() {
        std::printf("\n[PNG/SVG frame agreement]\n");

        const int W = 700, H = 500;

        auto fs = make_snapshot(1, 2, 2, 1.0, 1.0e5);
        fs.axes[0].snap2d()->title = "Left";
        fs.axes[0].snap2d()->ytitle = "y axis";
        fs.axes[0].snap2d()->xtitle = "x axis";
        fs.axes[0].snap2d()->legend_enabled = true;
        fs.axes[0].snap2d()->lines[0].opts.name = "series one"; // the case that used to diverge
        fs.axes[1].snap2d()->title = "Right";
        fs.generation = 1;
        fs.data_generation = 1;

        const auto layout = sextant::compute_figure_layout(fs, W, H);

        const std::string png_path = "layout_agreement.png";
        const std::string svg_path = "layout_agreement.svg"; {
            // supersample=1, so spine pixels aren't box-filtered.
            sextant::GLContext ctx({
                .width = W, .height = H,
                .title = "layout_test", .visible = false
            });
            sextant::NvgRenderer nvg(ctx.nvg());
            sextant::DataRenderer data;
            sextant::export_figure_png(ctx, nvg, data, fs, png_path, W, H, 1);
        }
        sextant::export_figure_svg(fs, svg_path, W, H);

        // --- SVG: every axes opens with its background rect, in cell order.
        std::ifstream svg(svg_path);
        check(svg.good(), "SVG written");
        std::string svg_text((std::istreambuf_iterator<char>(svg)), std::istreambuf_iterator<char>());

        std::size_t pos = 0;
        bool svg_ok = true;
        int svg_found = 0;
        for (const auto& c: layout.cells) {
            const std::size_t at = svg_text.find("\" fill=\"white\"/>", pos);
            if (at == std::string::npos) {
                svg_ok = false;
                break;
            }
            const std::size_t start = svg_text.rfind("<rect x=\"", at);
            if (start == std::string::npos) {
                svg_ok = false;
                break;
            }

            float x = 0, y = 0, w = 0, h = 0;
            if (std::sscanf(svg_text.c_str() + start, "<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\"",
                            &x, &y, &w, &h) != 4) {
                svg_ok = false;
                break;
            }
            if (std::fabs(x - c.frame.x) > 0.01f || std::fabs(y - c.frame.y) > 0.01f ||
                std::fabs(w - c.frame.w) > 0.01f || std::fabs(h - c.frame.h) > 0.01f) {
                svg_ok = false;
                std::printf("  SVG rect (%.2f,%.2f,%.2f,%.2f) vs layout (%.2f,%.2f,%.2f,%.2f)\n",
                            static_cast<double>(x), static_cast<double>(y),
                            static_cast<double>(w), static_cast<double>(h),
                            static_cast<double>(c.frame.x), static_cast<double>(c.frame.y),
                            static_cast<double>(c.frame.w), static_cast<double>(c.frame.h));
                break;
            }
            ++svg_found;
            pos = at + 1;
        }
        check(svg_ok && svg_found == static_cast<int>(layout.cells.size()),
              "every emitted SVG frame rect equals its computed frame (" +
              std::to_string(svg_found) + "/" + std::to_string(layout.cells.size()) + ")");

        // --- PNG: the axes background is the only pure white, so its extent
        // locates the frame.
        int pw = 0, ph = 0, comp = 0;
        unsigned char* px = stbi_load(png_path.c_str(), &pw, &ph, &comp, 4);
        check(px != nullptr && pw == W && ph == H, "PNG decoded at the requested size");

        if (px) {
            bool raster_ok = true;
            for (const auto& c: layout.cells) {
                // Probe just inside and just outside each edge.
                auto is_white = [&](int x, int y) {
                    if (x < 0 || y < 0 || x >= pw || y >= ph) return false;
                    const unsigned char* p = px + (static_cast<std::size_t>(y) * pw + x) * 4;
                    return p[0] > 250 && p[1] > 250 && p[2] > 250;
                };
                const int x0 = static_cast<int>(std::lround(c.frame.x));
                const int y0 = static_cast<int>(std::lround(c.frame.y));
                const int x1 = static_cast<int>(std::lround(c.frame.x + c.frame.w));
                const int y1 = static_cast<int>(std::lround(c.frame.y + c.frame.h));
                const int cy = (y0 + y1) / 2, cx = (x0 + x1) / 2;

                // Inside is the background; outside is the figure's grey (2 px
                // slack for the spine).
                if (!is_white(x0 + 3, cy) || !is_white(x1 - 3, cy)) raster_ok = false;
                if (!is_white(cx, y0 + 3) || !is_white(cx, y1 - 3)) raster_ok = false;
                if (is_white(x0 - 3, cy) || is_white(x1 + 3, cy)) raster_ok = false;
                if (is_white(cx, y0 - 3) || is_white(cx, y1 + 3)) raster_ok = false;
            }
            check(raster_ok,
                  "the raster frame the PNG actually contains matches the computed frame on all four edges");
            stbi_image_free(px);
        }

        std::printf("  frames: ");
        for (const auto& c: layout.cells)
            std::printf("(%.1f,%.1f %.1fx%.1f) ", static_cast<double>(c.frame.x),
                        static_cast<double>(c.frame.y), static_cast<double>(c.frame.w),
                        static_cast<double>(c.frame.h));
        std::printf("\n");
    }

    // compute_figure_layout() timed in isolation (it measures text every frame).
    void test_layout_cost() {
        std::printf("\n[layout cost]\n");

        auto one = make_snapshot(1, 1, 1);
        one.axes[0].snap2d()->title = "Heatmap (viridis)";
        one.axes[0].snap2d()->xtitle = "x axis";
        one.axes[0].snap2d()->ytitle = "y axis";

        auto grid = make_snapshot(3, 4, 12);
        for (auto& fa: grid.axes) fa.snap2d()->title = "cell";

        auto time_it = [](const sextant::FigureSnapshot& fs, int reps) {
            // One untimed call to warm the font caches.
            volatile float sink = sextant::compute_figure_layout(fs, 1280, 800).cells[0].frame.w;
            (void) sink;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < reps; ++i) {
                const auto l = sextant::compute_figure_layout(fs, 1280, 800);
                sink = l.cells[0].frame.w;
            }
            const auto t1 = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::micro>(t1 - t0).count() / reps;
        };

        const double us_one = time_it(one, 2000);
        const double us_grid = time_it(grid, 2000);
        std::printf("  1 axes:  %7.2f us/frame\n", us_one);
        std::printf("  12 axes: %7.2f us/frame\n", us_grid);

        // Bounds per build configuration. They catch a regression like
        // pick_default_font() re-scanning ~130 families per measurement.
#ifdef NDEBUG
        const double one_limit = 20.0, grid_limit = 200.0;
#else
        const double one_limit = 150.0, grid_limit = 1200.0;
#endif
        check(us_one < one_limit, "single-axes layout costs under " +
                                  std::to_string(one_limit) + " us/frame (" +
                                  std::to_string(us_one) + ")");
        check(us_grid < grid_limit, "12-axes layout costs under " +
                                    std::to_string(grid_limit) + " us/frame (" +
                                    std::to_string(us_grid) + ")");
        check(us_grid < us_one * 20.0,
              "layout cost scales with the cell count, not worse (" +
              std::to_string(us_grid / us_one) + "x for 12 cells)");
    }

    // ===========================================================================
    // The edit lane and the public entry points
    // ===========================================================================

    // Figure-level edits (margins, gaps) have no per-axes entry, so
    // FigureEdits::empty() must include them or the drain drops them.
    void test_figure_edit_lane() {
        std::printf("\n[figure edit lane]\n"); {
            sextant::FigureEditBox box;
            check(!box.load_and_clear().has_value(), "an untouched box drains to nothing");
        } {
            sextant::FigureEditBox box;
            box.update_figure([](sextant::FigureEdits& f) {
                f.margins = sextant::FigureMargins{1.0f, 2.0f, 3.0f, 4.0f};
            });
            auto drained = box.load_and_clear();
            check(drained.has_value(), "a margins-only edit survives the drain");
            check(drained && drained->margins && drained->margins->left == 1.0f
                  && drained->margins->bottom == 4.0f,
                  "and arrives with its values intact");
            check(!box.load_and_clear().has_value(), "the drain is destructive");
        } {
            sextant::FigureEditBox box;
            box.update_figure([](sextant::FigureEdits& f) {
                f.col_gap = 12.0f;
                f.row_gap = 13.0f;
            });
            auto drained = box.load_and_clear();
            check(drained && drained->col_gap && *drained->col_gap == 12.0f
                  && drained->row_gap && *drained->row_gap == 13.0f,
                  "a gaps-only edit survives the drain");
        } {
            // The render thread's live-preview drain has its own emptiness test.
            sextant::FigureEditBox box;
            box.update_figure([](sextant::FigureEdits& f) {
                f.margins = sextant::FigureMargins{5.0f, 5.0f, 5.0f, 5.0f};
            });
            auto drained = box.load_and_clear_journaled();
            check(drained && drained->margins,
                  "a margins-only edit also survives the render thread's drain");
        }
    }

    // Both margin entry points (FigureOptions::margins and set_margins()) must
    // agree in the output file, and the frame must move by the margin delta.
    void test_public_margins_api() {
        std::printf("\n[public margins API]\n");

        std::vector<double> x(40), y(40);
        for (int i = 0; i < 40; ++i) {
            x[i] = i;
            y[i] = std::sin(i * 0.2);
        }

        const sextant::FigureMargins custom{40.0f, 25.0f, 30.0f, 35.0f};
        sextant::FigureMargins def; // 10 all round

        auto render = [&](const char* path, bool via_options, bool via_setter) {
            sextant::FigureOptions o{};
            o.width = 640;
            o.height = 480;
            if (via_options) o.margins = custom;
            auto fig = sextant::Figure::create(o);
            fig->axes()->line(x, y);
            if (via_setter) fig->set_margins(custom);
            fig->savefig(path);
        };

        render("margins_default.svg", false, false);
        render("margins_options.svg", true, false);
        render("margins_setter.svg", false, true);

        sextant::PlotRect a{}, b{}, c{};
        const bool read_ok = read_first_svg_frame("margins_default.svg", a)
                             & read_first_svg_frame("margins_options.svg", b)
                             & read_first_svg_frame("margins_setter.svg", c);
        check(read_ok, "all three SVGs parsed");
        if (!read_ok) return;

        check(std::fabs((b.x - a.x) - (custom.left - def.left)) < 0.01f,
              "FigureOptions::margins moves the frame right by exactly the left delta");
        check(std::fabs((b.y - a.y) - (custom.top - def.top)) < 0.01f,
              "and down by exactly the top delta");
        check(std::fabs((a.w - b.w) - ((custom.left + custom.right) - (def.left + def.right))) < 0.01f,
              "and narrows it by exactly the horizontal delta");
        check(std::fabs((a.h - b.h) - ((custom.top + custom.bottom) - (def.top + def.bottom))) < 0.01f,
              "and shortens it by exactly the vertical delta");

        check(std::fabs(b.x - c.x) < 0.01f && std::fabs(b.y - c.y) < 0.01f &&
              std::fabs(b.w - c.w) < 0.01f && std::fabs(b.h - c.h) < 0.01f,
              "set_margins() after construction lands in exactly the same place");

        std::printf("  default (%.1f,%.1f %.1fx%.1f) -> custom (%.1f,%.1f %.1fx%.1f)\n",
                    static_cast<double>(a.x), static_cast<double>(a.y),
                    static_cast<double>(a.w), static_cast<double>(a.h),
                    static_cast<double>(b.x), static_cast<double>(b.y),
                    static_cast<double>(b.w), static_cast<double>(b.h));
    }

    // Several colorbars on one axes: every request gets a bar, and blocks are
    // summed, each sized for its own numbers.
    void test_multiple_colorbars() {
        std::printf("\n[several colorbars on one axes]\n");

        using namespace sextant;

        const int W = 900, H = 600;

        // scatter_z on 0..1 and on 0..100000 (much wider numbers).
        auto with_one = make_snapshot(1, 1, 1);
        auto with_two = make_snapshot(1, 1, 1);
        auto narrow_first = [](RenderSnapshot& s) {
            ScatterZPlot p;
            p.x = CowVec<double>{std::vector<double>{0.1, 0.5}};
            p.y = CowVec<double>{std::vector<double>{0.2, 0.6}};
            p.z = CowVec<double>{std::vector<double>{0.0, 1.0}};
            p.opts.colorbar = true;
            p.opts.cmap = Colormap::Viridis;
            p.opts.vmin = 0.0f;
            p.opts.vmax = 1.0f;
            s.scatter_z.push_back(p);
        };
        auto wide_second = [](RenderSnapshot& s) {
            ScatterZPlot p;
            p.x = CowVec<double>{std::vector<double>{0.3, 0.7}};
            p.y = CowVec<double>{std::vector<double>{0.4, 0.8}};
            p.z = CowVec<double>{std::vector<double>{0.0, 1.0}};
            p.opts.colorbar = true;
            p.opts.vmin = 0.0f;
            p.opts.vmax = 123456.0f;
            s.scatter_z.push_back(p);
        };
        narrow_first(*with_one.axes[0].snap2d());
        narrow_first(*with_two.axes[0].snap2d());
        wide_second(*with_two.axes[0].snap2d());

        const CellDecorations d1 = compute_cell_decorations(*with_one.axes[0].snap2d());
        const CellDecorations d2 = compute_cell_decorations(*with_two.axes[0].snap2d());

        check(d1.colorbars.size() == 1 && d2.colorbars.size() == 2,
              "every object that asks for a colorbar gets one, not just the first");
        // Each bar carries its own range (Colormap has one entry, so cmap can't
        // differ yet).
        check(d2.colorbars[0].vmax == 1.0f && d2.colorbars[1].vmax == 123456.0f,
              "each bar carries its own scale, in request order");
        check(d2.colorbars[1].block > d2.colorbars[0].block,
              "a bar labelled 123456 is wider than one labelled 1 -- measured per bar");
        check(std::fabs(d2.colorbar_block
                        - (d2.colorbars[0].block + d2.colorbars[1].block)) < 1e-3f,
              "and the carve is their sum, which is what total_carve() reports");

        const auto l1 = compute_figure_layout(with_one, W, H);
        const auto l2 = compute_figure_layout(with_two, W, H);

        check(l2.cells[0].colorbars.size() == 2, "the layout places both");
        check(std::fabs((l1.cells[0].frame.w - l2.cells[0].frame.w)
                        - d2.colorbars[1].block) < 1e-3f,
              "the second bar costs the frame exactly its own block, no more");

        const auto& b0 = l2.cells[0].colorbars[0].rect;
        const auto& b1 = l2.cells[0].colorbars[1].rect;
        check(b0.x >= l2.cells[0].frame.x + l2.cells[0].frame.w,
              "the first bar sits outside the frame");
        check(b1.x >= b0.x + b0.w,
              "and the second outboard of the first, never overlapping it");
        check(std::fabs((b1.x - b0.x) - d2.colorbars[0].block) < 1e-3f,
              "spaced by the first bar's block, so its numbers have the room measured for them");
        check(b0.y == l2.cells[0].frame.y && b0.h == l2.cells[0].frame.h
              && b1.y == b0.y && b1.h == b0.h,
              "both span the frame's height");

        // The inverse subtracts the sum of the blocks.
        const LayoutSize need = figure_size_for_frame(with_two, 1,
                                                      l2.cells[0].frame.w, l2.cells[0].frame.h);
        check(std::fabs(need.width - static_cast<float>(W)) < 1e-2f
              && std::fabs(need.height - static_cast<float>(H)) < 1e-2f,
              "figure_size_for_frame() inverts a carve of several bars");

        // Both bars reach the file with distinct gradient ids (duplicates would
        // silently reuse the first gradient).
        export_figure_svg(with_two, "two_colorbars.svg", W, H);
        std::ifstream f("two_colorbars.svg", std::ios::binary);
        const std::string svg((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

        check(svg.find("id=\"colorbarGrad0_0\"") != std::string::npos
              && svg.find("id=\"colorbarGrad0_1\"") != std::string::npos,
              "the SVG defines one gradient per bar, under ids that cannot collide");
        auto has_rect_at = [&](const PlotRect& r) {
            std::ostringstream s;
            s << "<rect x=\"" << r.x << "\" y=\"" << r.y;
            return svg.find(s.str()) != std::string::npos;
        };
        check(has_rect_at(b0) && has_rect_at(b1),
              "and draws both, in the boxes the layout carved");
        check(svg.find("url(#colorbarGrad0_1)") != std::string::npos,
              "with the second bar referencing the second gradient");

        std::printf("  blocks %.2f + %.2f = %.2f px carved; bars at x %.1f and %.1f\n",
                    static_cast<double>(d2.colorbars[0].block),
                    static_cast<double>(d2.colorbars[1].block),
                    static_cast<double>(d2.colorbar_block),
                    static_cast<double>(b0.x), static_cast<double>(b1.x));
    }

    // A colorbar's name (on the plot object, not ColorbarOptions). Drawn rotated
    // along the bar, so it costs a line height whatever its length.
    void test_colorbar_labels() {
        std::printf("\n[a colorbar's name]\n");

        using namespace sextant;

        const int W = 900, H = 600;

        auto build = [&](const char* first, const char* second) {
            auto fs = make_snapshot(1, 1, 1);
            RenderSnapshot* s = fs.axes[0].snap2d();
            for (const char* lbl: {first, second}) {
                if (!lbl) continue;
                ScatterZPlot p;
                p.x = CowVec<double>{std::vector<double>{0.1, 0.5}};
                p.y = CowVec<double>{std::vector<double>{0.2, 0.6}};
                p.z = CowVec<double>{std::vector<double>{0.0, 1.0}};
                p.opts.colorbar = true;
                p.opts.name = lbl;
                s->scatter_z.push_back(p);
            }
            return fs;
        };

        const FigureSnapshot none = build("", nullptr);
        const FigureSnapshot named = build("flux", nullptr);
        const FigureSnapshot longn = build("flux density, in arbitrary units", nullptr);

        const CellDecorations d_none = compute_cell_decorations(*none.axes[0].snap2d());
        const CellDecorations d_named = compute_cell_decorations(*named.axes[0].snap2d());
        const CellDecorations d_long = compute_cell_decorations(*longn.axes[0].snap2d());

        check(d_named.colorbars[0].name == "flux",
              "cb name: the request carries the text the plot object was given");
        check(d_named.colorbars[0].block > d_none.colorbars[0].block,
              "cb name: a named bar reserves more width than an unnamed one");
        check(d_long.colorbars[0].block == d_named.colorbars[0].block,
              "cb name: and a long name costs exactly what a short one does -- it is rotated, "
              "so its length runs down the frame");
        check(d_none.colorbars[0].name.empty()
              && compute_figure_layout(none, W, H).cells[0].colorbars[0].name_x == 0.0f,
              "cb name: an empty name draws nothing and reserves nothing");

        // The text lies within the block reserved for it: outboard of the numbers,
        // inboard of the next strip.
        const FigureSnapshot two = build("flux", "depth");
        const CellDecorations d2 = compute_cell_decorations(*two.axes[0].snap2d());
        const auto l2 = compute_figure_layout(two, W, H);
        const auto& b0 = l2.cells[0].colorbars[0];
        const auto& b1 = l2.cells[0].colorbars[1];

        check(b0.name == "flux" && b1.name == "depth",
              "cb name: each bar gets its own object's name, in request order");
        check(b0.name_x > b0.rect.x + b0.rect.w,
              "cb name: the text sits outboard of its own bar");
        check(b0.name_x < b1.rect.x,
              "cb name: and inboard of the next bar's strip");
        check(b0.name_y == b0.rect.y + b0.rect.h * 0.5f,
              "cb name: centred down the frame, which is what a rotated line is centred on");
        check(b0.name_x < l2.cells[0].frame.x + l2.cells[0].frame.w + d2.colorbar_block,
              "cb name: and inside the width the carve reserved");

        // Clear of the numbers, measured independently.
        {
            const ColorbarOptions& co = two.axes[0].snap2d()->colorbar_opts;
            const float lh = font_vmetrics(co.font_path, co.fontsize).line_height;
            const float numbers_end = b0.rect.x + b0.rect.w + kColorbarLabelGap
                                      + std::max(text_width(co.font_path, co.fontsize, "0"),
                                                 text_width(co.font_path, co.fontsize, "1"));
            check(b0.name_x - lh * 0.5f >= numbers_end - 0.001f,
                  "cb name: and clear of the numbers it sits beside, not over them");
        }

        // Both outputs via the public API; the SVG name must be rotated.
        auto fig = Figure::create({.width = W, .height = H});
        const std::vector<double> zx{0.1, 0.5}, zy{0.2, 0.6}, zz{0.0, 1.0};
        fig->axes()->scatter_z(zx, zy, zz, {.colorbar = true, .name = "flux"});
        fig->savefig("colorbar_label.svg");

        std::ifstream f("colorbar_label.svg", std::ios::binary);
        const std::string svg((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
        const std::size_t bar = svg.find("url(#colorbarGrad0_0)");
        const std::string tail = bar == std::string::npos ? std::string() : svg.substr(bar);
        check(tail.find(">flux</text>") != std::string::npos,
              "cb name: ScatterZOptions::label reaches the written file");
        check(tail.find("rotate(-90,") != std::string::npos,
              "cb name: rotated, as the width reserved for it assumes");

        std::printf("  block %.2f px unnamed, %.2f px named (long name: %.2f)\n",
                    static_cast<double>(d_none.colorbars[0].block),
                    static_cast<double>(d_named.colorbars[0].block),
                    static_cast<double>(d_long.colorbars[0].block));
    }

    // `show_legend`: defaults to true, so a key is drawn iff
    // `show_legend && !label.empty()`.
    void test_show_legend_and_new_keys() {
        std::printf("\n[legend: show_legend, and the two kinds that had no key]\n");

        using namespace sextant;

        // Every 2D kind, each labelled.
        auto build = [&] {
            auto fs = make_snapshot(1, 1, 1);
            RenderSnapshot* s = fs.axes[0].snap2d();
            s->legend_enabled = true;
            s->lines[0].opts.name = "line";

            ScatterPlot sp;
            sp.x = CowVec<double>{std::vector<double>{0.1, 0.5}};
            sp.y = CowVec<double>{std::vector<double>{0.2, 0.6}};
            sp.opts.name = "dots";
            sp.opts.marker = MarkerStyle::Diamond;
            s->scatters.push_back(sp);

            ScatterZPlot zp;
            zp.x = CowVec<double>{std::vector<double>{0.1, 0.5}};
            zp.y = CowVec<double>{std::vector<double>{0.2, 0.6}};
            zp.z = CowVec<double>{std::vector<double>{0.0, 1.0}};
            zp.opts.name = "flux";
            zp.opts.marker = MarkerStyle::Square;
            s->scatter_z.push_back(zp);

            BarPlot bp;
            bp.centers = CowVec<double>{std::vector<double>{1.0, 2.0}};
            bp.heights = CowVec<double>{std::vector<double>{1.0, 2.0}};
            bp.opts.name = "bars";
            s->bars.push_back(bp);
            return fs;
        };

        const FigureSnapshot all = build();
        const auto base = collect_legend_entries(*all.axes[0].snap2d());
        check(base.size() == 4, "show_legend: every labelled 2D kind is keyed by default");
        check(base[0].name == "line" && base[1].name == "dots"
              && base[2].name == "flux" && base[3].name == "bars",
              "show_legend: in kind order, with scatter_z between the scatters and the bars");

        // Each kind's switch removes exactly its own key.
        struct Off {
            const char* name;

            void (*apply)(RenderSnapshot&);
        };
        const Off offs[] = {
            {"line", [](RenderSnapshot& s) { s.lines[0].opts.show_legend = false; }},
            {"dots", [](RenderSnapshot& s) { s.scatters[0].opts.show_legend = false; }},
            {"flux", [](RenderSnapshot& s) { s.scatter_z[0].opts.show_legend = false; }},
            {"bars", [](RenderSnapshot& s) { s.bars[0].opts.show_legend = false; }},
        };
        bool gate_ok = true;
        for (const Off& o: offs) {
            FigureSnapshot fs = build();
            o.apply(*fs.axes[0].snap2d());
            const auto e = collect_legend_entries(*fs.axes[0].snap2d());
            if (e.size() != 3) {
                gate_ok = false;
                continue;
            }
            for (const auto& en: e) if (en.name == o.name) gate_ok = false;
        }
        check(gate_ok, "show_legend: switching one kind off drops exactly its own key");

        // The text is kept.
        {
            FigureSnapshot fs = build();
            fs.axes[0].snap2d()->scatters[0].opts.show_legend = false;
            check(fs.axes[0].snap2d()->scatters[0].opts.name == "dots",
                  "show_legend: and leaves the label alone, so switching it back restores the key");
        }

        // scatter_z's key: the marker in white with a black edge.
        check(base[2].kind == LegendKind::Marker && base[2].marker == MarkerStyle::Square,
              "scatter_z key: the series' own marker shape");
        check(base[2].color.r == 1.0f && base[2].color.g == 1.0f && base[2].color.b == 1.0f,
              "scatter_z key: filled white, not with a colour it does not have");
        check(base[2].edge.a > 0.0f && base[2].edge.r == 0.0f,
              "scatter_z key: and edged black, which is what makes a white fill visible");
        check(base[1].edge.a == 0.0f,
              "scatter_z key: while an ordinary marker key has no edge at all");

        // bar3d is keyed, before any plane's keys.
        {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s3 = fs.axes[0].snap3d();
            s3->legend_enabled = true;

            Bar3DPlot g;
            g.u = CowVec<double>{std::vector<double>{0.0, 1.0}};
            g.v = CowVec<double>{std::vector<double>{0.0, 1.0}};
            g.heights = CowVec<double>{std::vector<double>{1.0, 2.0, 3.0, 4.0}};
            g.opts.name = "counts";
            s3->bars3d.push_back(g);

            HeatmapOptions ho;
            s3->planes.push_back(
                make_plane(PlaneOrientation::XY, 0.0, std::vector<float>(4, 0.5f), 2, 2,
                           {0.0, 1.0}, {0.0, 1.0}, ho));
            LinePlot lp;
            lp.x = CowVec<double>{std::vector<double>{0.0, 1.0}};
            lp.y = CowVec<double>{std::vector<double>{0.0, 1.0}};
            lp.opts.name = "on the plane";
            s3->planes[0].sheet.lines.push_back(lp);

            const auto e = collect_legend_entries(*s3);
            check(e.size() == 2 && e[0].name == "counts" && e[1].name == "on the plane",
                  "bar3d key: a labelled grid is keyed, before any plane's own keys");
            check(e[0].kind == LegendKind::Bar,
                  "bar3d key: by a swatch, since every bar of a grid is the one colour");

            s3->bars3d[0].opts.show_legend = false;
            check(collect_legend_entries(*s3).size() == 1,
                  "bar3d key: and the same gate switches it off");
        }

        // In the file: the white fill.
        auto fig = Figure::create({.width = 640, .height = 420});
        const std::vector<double> zx{0.1, 0.5}, zy{0.2, 0.6}, zz{0.0, 1.0};
        fig->axes()->scatter_z(zx, zy, zz,
                               {.marker = MarkerStyle::Square, .name = "flux"}).legend();
        fig->savefig("legend_scatter_z.svg");

        std::ifstream f("legend_scatter_z.svg", std::ios::binary);
        const std::string svg((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
        check(svg.find(">flux</text>") != std::string::npos,
              "scatter_z key: reaches the file");
        check(svg.find("fill=\"rgb(255,255,255)\" fill-opacity=\"1\" stroke=\"rgb(0,0,0)\"")
              != std::string::npos,
              "scatter_z key: as a white swatch with a black edge");
    }
} // namespace lt
