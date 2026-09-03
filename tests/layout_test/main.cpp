// sextant_layout_test -- internal checks for things not reachable through
// <sextant/sextant.h>. Links sextant_static and includes headers out of src/.
//
// Phase 0 -- text metrics. src/text_metrics.cpp claims to reproduce
// fontstash's advance arithmetic exactly, so that the space layout reserves
// for a label is the space NanoVG actually draws into. Every string is
// measured both ways and the two must agree to the last representable bit,
// not within a tolerance: a tolerance would hide precisely the quantization
// mistakes this exists to catch. The comparison runs at devicePxRatio = 1,
// where NanoVG's own quantization happens at the logical size.

// Declarations only: nanovg.c already compiles stb_image's implementation
// into the library, so defining it here again is a duplicate-symbol error.
#include "stb_image.h"

#include "edit_box.h"
#include "figure_edits.h"
#include "figure_export.h"
#include "renderer/data_renderer.h"
#include "renderer/gl_context.h"
#include "renderer/nvg_renderer.h"
#include "font_discovery.h"
#include "renderer/figure_layout.h"
#include "text_metrics.h"
#include "contour.h"
#include "widgets/cell_shading.h"
#include "widgets/data_panel.h"
#include "widgets/panel_state.h"

#include <sextant/sextant.h>

#include "nanovg.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

const std::vector<std::string>& sample_strings() {
    // Every kind of text layout has to reserve room for: tick labels (the
    // widest of which sets the left inset), axis/figure titles, and legend
    // entries. Plus a few adversarial ones — a lone glyph, a string of
    // identical glyphs where a per-glyph rounding error accumulates
    // fastest, a pair that is kerned in most serif faces, and non-ASCII.
    static const std::vector<std::string> s = {
        "0", "-1", "0.5", "1.0", "-0.25", "12345", "1e+08", "-1.5e-08",
        "100000", "0.000001", "2.5", "-273.15",
        "x", "Time (s)", "Amplitude", "sin(x)", "A Title With Spaces",
        "AV", "AVATAR", "WWWWWWWWWW", "iiiiiiiiii", "....", "|",
        "\xC2\xB5m",              // "µm"
        "\xE2\x88\x92" "1.0",     // U+2212 MINUS SIGN, then "1.0"
        "\xF0\x9F\x93\x88",       // U+1F4C8, a codepoint the font will lack
    };
    return s;
}

// Font sizes worth checking: the library's own defaults (label 11, legend/
// colorbar 10, xtitle/ytitle 16.5, title 18, suptitle 21), the extremes the
// Controls panel allows a user to drag to, and a couple of sizes with a
// fractional part that does not survive fontstash's 0.1 px quantization.
const std::vector<float>& sample_sizes() {
    static const std::vector<float> s = {
        1.0f, 2.0f, 5.0f, 8.0f, 10.0f, 11.0f, 12.0f, 16.5f, 18.0f, 21.0f,
        24.0f, 28.0f, 30.0f, 40.0f, 64.0f, 96.0f,
        10.04f, 11.06f, 13.999f, 17.25f,
    };
    return s;
}

// One font's worth of comparison. `nvg_font` must already be created in vg;
// `path` is what text_width() is asked for ("" = the default font).
void compare_font(NVGcontext* vg, int nvg_font, const std::string& path,
                  const char* label) {
    std::printf("[%s]\n", label);

    check(sextant::text_metrics_font_loaded(path),
          std::string(label) + ": real glyph metrics in use (not the fallback estimate)");

    int worst_count = 0;
    float worst_delta = 0.0f;
    std::string worst_case;

    // Split by size on purpose — see the assertion below.
    float worst_overhang_small = 0.0f, worst_overhang_large = 0.0f;
    std::string worst_overhang_case;

    for (float size : sample_sizes()) {
        nvgFontFaceId(vg, nvg_font);
        nvgFontSize(vg, size);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

        for (const auto& s : sample_strings()) {
            // nvgTextBounds' *return value* is the advance — the same pen
            // movement text_width() computes. bounds[2]-bounds[0] is the ink
            // extent instead, which is a different quantity (see below).
            const float nvg_adv  = nvgTextBounds(vg, 0.0f, 0.0f, s.c_str(), nullptr, nullptr);
            const float own_adv  = sextant::text_width(path, size, s);
            const float delta    = std::fabs(nvg_adv - own_adv);

            if (delta != 0.0f) {
                ++worst_count;
                if (delta > worst_delta) {
                    worst_delta = delta;
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "\"%s\" @ %.3g px: nvg %.4f vs own %.4f",
                                  s.c_str(), static_cast<double>(size),
                                  static_cast<double>(nvg_adv), static_cast<double>(own_adv));
                    worst_case = buf;
                }
            }

            // Reserving the advance has to actually contain the glyphs, so
            // track how far the ink extent runs past it. It legitimately runs
            // past by up to ~2 px at any size: NanoVG's quads inset the atlas
            // rect by one pixel per side out of the 2 px padding fontstash
            // adds around each glyph bitmap. Below ~8 px the gap grows
            // further, because rounding every pen step to a whole pixel makes
            // glyphs overlap outright, so the bound is only asserted where
            // layout actually operates.
            if (size >= 8.0f) {
                float bounds[4] = {0, 0, 0, 0};
                nvgTextBounds(vg, 0.0f, 0.0f, s.c_str(), nullptr, bounds);
                const float overhang = (bounds[2] - bounds[0]) - own_adv;
                float& worst = (size >= 32.0f) ? worst_overhang_large : worst_overhang_small;
                if (overhang > worst) {
                    worst = overhang;
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "\"%s\" @ %.3g px", s.c_str(),
                                  static_cast<double>(size));
                    worst_overhang_case = buf;
                }
            }
        }
    }

    const int total = static_cast<int>(sample_sizes().size() * sample_strings().size());
    check(worst_count == 0,
          std::string(label) + ": " + std::to_string(worst_count) + "/" +
          std::to_string(total) + " advances differ (worst: " +
          (worst_case.empty() ? "none" : worst_case) + ")");
    std::printf("  %d string x size combinations, %d mismatched\n", total, worst_count);

    // The bound is a constant ~3 px, and asserting it at both size ranges is
    // what says so: NanoVG's padding is a fixed number of pixels, whereas
    // genuine glyph overhang past the advance would scale with the font
    // size. If the large-size figure ever climbs above the small-size one,
    // this stopped being bookkeeping and became real clipping risk.
    check(worst_overhang_small <= 3.0f && worst_overhang_large <= 3.0f,
          std::string(label) + ": ink overhang past the advance — 8-30 px: " +
          std::to_string(worst_overhang_small) + ", >=32 px: " +
          std::to_string(worst_overhang_large) + " (worst on " +
          (worst_overhang_case.empty() ? "none" : worst_overhang_case) + ")");
    std::printf("  ink overhang past advance: %.3f px at 8-30, %.3f px at >=32\n",
                static_cast<double>(worst_overhang_small),
                static_cast<double>(worst_overhang_large));

    // Vertical metrics: font_vmetrics() must reproduce nvgTextMetrics(),
    // which is what every stacked inset will be measured in.
    for (float size : sample_sizes()) {
        nvgFontFaceId(vg, nvg_font);
        nvgFontSize(vg, size);
        float asc = 0.0f, desc = 0.0f, lineh = 0.0f;
        nvgTextMetrics(vg, &asc, &desc, &lineh);

        const auto vm = sextant::font_vmetrics(path, size);
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "%s: vmetrics @ %.3g px — nvg(%.4f,%.4f,%.4f) own(%.4f,%.4f,%.4f)",
                      label, static_cast<double>(size),
                      static_cast<double>(asc), static_cast<double>(desc), static_cast<double>(lineh),
                      static_cast<double>(vm.ascent), static_cast<double>(vm.descent),
                      static_cast<double>(vm.line_height));
        check(asc == vm.ascent && desc == vm.descent && lineh == vm.line_height, buf);
    }
}

void test_text_metrics() {
    // Headless: no window is shown, but a GL context still has to exist for
    // NanoVG to be created at all.
    sextant::GLContext ctx({.width = 400, .height = 300,
                            .title = "layout_test", .visible = false});
    NVGcontext* vg = ctx.nvg();
    if (!vg) {
        std::printf("FATAL: no NanoVG context\n");
        ++g_failures;
        return;
    }

    const sextant::FontEntry* def = sextant::pick_default_font();
    if (!def) {
        std::printf("FATAL: no system font discovered — nothing to compare\n");
        ++g_failures;
        return;
    }
    std::printf("default font: %s (%s)\n\n", def->name.c_str(), def->path.c_str());

    // "" is the path the whole library uses to mean "the default font", and
    // is what almost every AxesStyle carries — so it is the case that
    // matters. Passing the resolved path explicitly must give the same
    // answer, which the second block checks along with a *different* face.
    const int font_default = nvgCreateFont(vg, "default", def->path.c_str());
    check(font_default != -1, "default font loaded into NanoVG");
    if (font_default != -1) compare_font(vg, font_default, "", "default font (\"\")");

    // A second, explicitly-named face — the font_path branch of
    // AxesStyle/LegendOptions/SuptitleOptions, which resolves a different
    // file rather than falling through to pick_default_font().
    const auto& fonts = sextant::discover_system_fonts();
    const sextant::FontEntry* other = nullptr;
    for (const auto& f : fonts)
        if (f.path != def->path) { other = &f; break; }

    if (other) {
        const int font_other = nvgCreateFont(vg, other->path.c_str(), other->path.c_str());
        check(font_other != -1, "second font loaded into NanoVG");
        if (font_other != -1)
            compare_font(vg, font_other, other->path, other->name.c_str());
    } else {
        std::printf("(only one font on this system — explicit-path case skipped)\n");
    }
}

// A font path that cannot be loaded must not take the layout down with it —
// it falls back to a per-character estimate. Nothing in the repo produces
// such a path today, but AxesStyle::font_path is public and arbitrary.
void test_missing_font_fallback() {
    std::printf("\n[missing font]\n");
    const std::string bogus = "D:/this/font/does/not/exist.ttf";

    check(!sextant::text_metrics_font_loaded(bogus), "bogus path reports no glyph metrics");

    const float w1 = sextant::text_width(bogus, 12.0f, "12345");
    const float w2 = sextant::text_width(bogus, 12.0f, "1234567890");
    check(w1 > 0.0f, "fallback width is positive");
    check(w2 > w1,   "fallback width grows with the string");
    check(sextant::text_width(bogus, 12.0f, "") == 0.0f, "empty string measures 0");

    const auto vm = sextant::font_vmetrics(bogus, 12.0f);
    check(vm.line_height > 0.0f && vm.ascent > 0.0f && vm.descent < 0.0f,
          "fallback vmetrics are sane");
    std::printf("  fallback: \"12345\" @ 12 px = %.2f px, line height %.2f\n",
                static_cast<double>(w1), static_cast<double>(vm.line_height));
}

// Layout runs on the render thread and, inside savefig(), on whatever thread
// the caller happens to be on — so the caches behind text_width() are shared
// across threads by design. This hammers them concurrently and requires every
// answer to match the single-threaded one.
void test_concurrent_measurement() {
    std::printf("\n[concurrency]\n");

    std::vector<float> expected;
    for (const auto& s : sample_strings())
        for (float size : sample_sizes())
            expected.push_back(sextant::text_width("", size, s));

    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&] {
            for (int rep = 0; rep < 20; ++rep) {
                std::size_t i = 0;
                for (const auto& s : sample_strings())
                    for (float size : sample_sizes())
                        if (sextant::text_width("", size, s) != expected[i++])
                            ++mismatches;
            }
        });
    }
    for (auto& th : threads) th.join();

    check(mismatches.load() == 0,
          "8 threads x 20 passes agree with the single-threaded widths (" +
          std::to_string(mismatches.load()) + " mismatches)");
    std::printf("  %zu widths x 8 threads x 20 passes, %d mismatches\n",
                expected.size(), mismatches.load());
}

// ===========================================================================
// Phase 1 — the layout itself
// ===========================================================================

// A one-line figure, built directly rather than through the public API so a
// test can set the pieces layout actually keys on (font sizes, titles, tick
// overrides) without going through Figure/Axes.
sextant::FigureSnapshot make_snapshot(int rows, int cols, int n_cells,
                                      double x_scale = 1.0, double y_scale = 1.0) {
    sextant::FigureSnapshot fs;
    for (int i = 1; i <= n_cells; ++i) {
        sextant::RenderSnapshot rs;
        sextant::LinePlot lp;
        std::vector<double> xs, ys;
        for (int k = 0; k < 50; ++k) {
            xs.push_back(static_cast<double>(k) * x_scale);
            ys.push_back(std::sin(k * 0.1) * y_scale);
        }
        lp.x = sextant::CowVec<double>(std::move(xs));
        lp.y = sextant::CowVec<double>(std::move(ys));
        rs.lines.push_back(std::move(lp));
        fs.axes.push_back({ {rows, cols, i}, std::move(rs) });
    }
    return fs;
}

bool finite_rect(const sextant::PlotRect& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) &&
           std::isfinite(r.w) && std::isfinite(r.h);
}

// Font size drives position. Sweep
// each text size independently and require that the space reserved for it
// tracks it, that the text stays inside that space, and that the plot frame
// gives up exactly what the decorations took.
void test_font_size_drives_layout() {
    std::printf("\n[font size -> layout]\n");

    constexpr int W = 800, H = 600;
    float prev_top = -1.0f, prev_bottom = -1.0f, prev_left = -1.0f;
    float prev_frame_h = 1e9f, prev_frame_w = 1e9f;
    bool  top_monotonic = true, bottom_monotonic = true, left_monotonic = true;
    bool  frame_shrinks = true, title_contained = true, xtitle_contained = true;
    bool  ytitle_contained = true, all_finite = true;

    for (float size : {6.0f, 9.0f, 12.0f, 18.0f, 24.0f, 32.0f, 48.0f, 64.0f}) {
        auto fs = make_snapshot(1, 1, 1);
        auto& snap = fs.axes[0].snap;
        snap.title  = "Axes title";
        snap.xtitle = "x axis";
        snap.ytitle = "y axis";
        snap.axes_style.title_fontsize  = size;
        snap.axes_style.xtitle_fontsize = size;
        snap.axes_style.ytitle_fontsize = size;

        const auto layout = sextant::compute_figure_layout(fs, W, H);
        const auto& c  = layout.cells[0];
        const auto& in = layout.insets;

        if (prev_top >= 0.0f) {
            if (in.top    < prev_top)    top_monotonic    = false;
            if (in.bottom < prev_bottom) bottom_monotonic = false;
            if (in.left   < prev_left)   left_monotonic   = false;
            if (c.frame.h > prev_frame_h) frame_shrinks = false;
            if (c.frame.w > prev_frame_w) frame_shrinks = false;
        }
        prev_top = in.top; prev_bottom = in.bottom; prev_left = in.left;
        prev_frame_h = c.frame.h; prev_frame_w = c.frame.w;

        if (!finite_rect(c.frame)) all_finite = false;

        // The title's own line has to fit between the top of the cell and
        // the frame — that band is what insets.top was sized to hold.
        const float title_lh = sextant::font_vmetrics(snap.axes_style.font_path, size).line_height;
        const float cell_top = fs.margins.top;
        if (c.title_y - title_lh * 0.5f < cell_top - 0.51f) title_contained = false;
        if (c.title_y + title_lh * 0.5f > c.frame.y + 0.51f) title_contained = false;

        // Likewise the x title above the figure's bottom margin, and the y
        // title right of its left margin.
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

    check(top_monotonic,    "top inset grows with the title font size");
    check(bottom_monotonic, "bottom inset grows with the x-title font size");
    check(left_monotonic,   "left inset grows with the y-title font size");
    check(frame_shrinks,    "plot frame gives up exactly what the decorations take");
    check(title_contained,  "the axes title's line fits in the band reserved above the frame");
    check(xtitle_contained, "the x title stays inside the figure's bottom margin");
    check(ytitle_contained, "the y title stays inside the figure's left margin");
    check(all_finite,       "every frame is finite");
}

// Absent decorations must cost nothing — the whole point of dropping
// tight_layout, which only did this when asked and only in fixed steps.
void test_absent_decorations_cost_nothing() {
    std::printf("\n[absent decorations]\n");

    auto bare = make_snapshot(1, 1, 1);
    auto titled = make_snapshot(1, 1, 1);
    titled.axes[0].snap.title  = "T";
    titled.axes[0].snap.xtitle = "x";
    titled.axes[0].snap.ytitle = "y";

    const auto lb = sextant::compute_figure_layout(bare,   800, 600);
    const auto lt = sextant::compute_figure_layout(titled, 800, 600);

    check(lb.insets.top < lt.insets.top,       "no title reserves less above the frame");
    check(lb.insets.bottom < lt.insets.bottom, "no x title reserves less below it");
    check(lb.insets.left < lt.insets.left,     "no y title reserves less to its left");
    check(lb.cells[0].frame.w > lt.cells[0].frame.w &&
          lb.cells[0].frame.h > lt.cells[0].frame.h,
          "an undecorated axes gets the reclaimed space");
    std::printf("  bare   insets L%.2f T%.2f B%.2f\n",
                static_cast<double>(lb.insets.left), static_cast<double>(lb.insets.top),
                static_cast<double>(lb.insets.bottom));
    std::printf("  titled insets L%.2f T%.2f B%.2f\n",
                static_cast<double>(lt.insets.left), static_cast<double>(lt.insets.top),
                static_cast<double>(lt.insets.bottom));
}

// The left inset is the widest y tick label. Wide numbers are what used to
// overrun the fixed 70 px and collide with the y title.
void test_wide_tick_labels() {
    std::printf("\n[wide tick labels]\n");

    auto narrow = make_snapshot(1, 1, 1, 1.0, 1.0);
    auto wide   = make_snapshot(1, 1, 1, 1.0, 1.0e8);

    const auto ln = sextant::compute_figure_layout(narrow, 800, 600);
    const auto lw = sextant::compute_figure_layout(wide,   800, 600);

    check(lw.insets.left > ln.insets.left,
          "1e8-scale y labels reserve more room than unit-scale ones");

    // And the reservation is exactly the widest label plus the tick and gap,
    // not a guess with slack in it.
    const auto& st = wide.axes[0].snap.axes_style;
    float widest = 0.0f;
    for (const auto& t : lw.cells[0].yticks)
        widest = std::max(widest, sextant::text_width(st.font_path, st.label_fontsize, t.label));
    const float expect = st.tick_length + sextant::kTickLabelGap + widest;
    check(std::fabs(lw.insets.left - expect) < 0.001f,
          "left inset == tick_length + gap + widest y label (" +
          std::to_string(lw.insets.left) + " vs " + std::to_string(expect) + ")");
    std::printf("  narrow L%.2f, wide L%.2f (widest label %.2f px)\n",
                static_cast<double>(ln.insets.left), static_cast<double>(lw.insets.left),
                static_cast<double>(widest));
}

// A grid's frames must be identical in size and aligned in both directions,
// which is the reason insets are uniform across the figure rather than
// per-cell.
void test_grid_alignment() {
    std::printf("\n[grid alignment]\n");

    auto fs = make_snapshot(2, 3, 6);
    // Only one cell carries a long y title and wide data — under per-cell
    // insets this is exactly the case whose frames would come out different.
    fs.axes[4].snap.ytitle = "a considerably longer y title";
    fs.axes[1].snap.title  = "T";
    fs.col_gap = 12.0f;
    fs.row_gap = 14.0f;

    const auto layout = sextant::compute_figure_layout(fs, 1200, 800);

    bool same_size = true, cols_aligned = true, rows_aligned = true;
    const auto& c0 = layout.cells[0];
    for (const auto& c : layout.cells) {
        if (std::fabs(c.frame.w - c0.frame.w) > 0.001f) same_size = false;
        if (std::fabs(c.frame.h - c0.frame.h) > 0.001f) same_size = false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        // cells i and i+3 are the same column, i and i+1 the same row.
        if (std::fabs(layout.cells[i].frame.x - layout.cells[i + 3].frame.x) > 0.001f)
            cols_aligned = false;
        if (std::fabs(layout.cells[i].frame.y - layout.cells[0].frame.y) > 0.001f)
            rows_aligned = false;
    }

    check(same_size,    "every cell in a 2x3 grid has the same frame size");
    check(cols_aligned, "cells in a column share an x");
    check(rows_aligned, "cells in a row share a y");

    // Gaps separate whole subplots now, so the distance between one cell's
    // right edge and the next cell's left edge is insets.right + col_gap +
    // insets.left.
    const float measured = layout.cells[1].frame.x
                           - (layout.cells[0].frame.x + layout.cells[0].frame.w);
    const float expect = layout.insets.right + fs.col_gap + layout.insets.left;
    check(std::fabs(measured - expect) < 0.001f,
          "the column gap separates whole subplots, not bare frames (" +
          std::to_string(measured) + " vs " + std::to_string(expect) + ")");
}

// Margins are now the figure's outer border, and nothing else should move
// when they change.
void test_margins() {
    std::printf("\n[margins]\n");

    auto a = make_snapshot(1, 1, 1);
    auto b = make_snapshot(1, 1, 1);
    b.margins = { 40.0f, 25.0f, 30.0f, 35.0f };

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
    check(la.insets.left == lb.insets.left && la.insets.top == lb.insets.top,
          "margins do not disturb the measured insets");
}

// However little room is left, a frame must stay positive — CoordTransform
// divides by its width.
void test_degenerate_sizes() {
    std::printf("\n[degenerate sizes]\n");

    bool ok = true;
    for (auto [w, h] : {std::pair{40, 30}, std::pair{10, 10}, std::pair{1, 1}, std::pair{120, 60}}) {
        auto fs = make_snapshot(2, 2, 4);
        auto& s = fs.axes[0].snap;
        s.title = "huge"; s.xtitle = "huge"; s.ytitle = "huge";
        s.axes_style.title_fontsize = 64.0f;
        s.axes_style.xtitle_fontsize = 64.0f;
        s.axes_style.ytitle_fontsize = 64.0f;
        s.axes_style.label_fontsize = 48.0f;

        const auto layout = sextant::compute_figure_layout(fs, w, h);
        for (const auto& c : layout.cells) {
            if (!finite_rect(c.frame)) ok = false;
            if (c.frame.w < sextant::kMinFrameSize || c.frame.h < sextant::kMinFrameSize) ok = false;
        }
    }
    check(ok, "a frame never collapses or inverts, however little room is left");
}

// The legend and colorbar are carved from the cell that owns them, and the
// space they take is measured from their own text.
void test_legend_and_colorbar_carve() {
    std::printf("\n[legend / colorbar]\n");

    auto plain = make_snapshot(1, 1, 1);
    auto legended = make_snapshot(1, 1, 1);
    legended.axes[0].snap.legend_enabled = true;
    legended.axes[0].snap.lines[0].opts.label = "short";

    auto long_legend = make_snapshot(1, 1, 1);
    long_legend.axes[0].snap.legend_enabled = true;
    long_legend.axes[0].snap.lines[0].opts.label = "a much longer legend label";

    const auto lp = sextant::compute_figure_layout(plain, 800, 600);
    const auto ll = sextant::compute_figure_layout(legended, 800, 600);
    const auto lo = sextant::compute_figure_layout(long_legend, 800, 600);

    check(!lp.cells[0].has_legend(), "no legend when none was enabled");
    check(ll.cells[0].has_legend(),  "a labelled series gets a legend box");
    check(lo.cells[0].legend.w > ll.cells[0].legend.w,
          "a longer label makes a wider box");
    check(lo.cells[0].frame.w < ll.cells[0].frame.w,
          "and the frame gives up that width");
    check(ll.cells[0].legend.x >= ll.cells[0].frame.x + ll.cells[0].frame.w,
          "the legend box sits outside the frame, never over the data");

    // The box has to hold its own text: pad + swatch + gap + widest label.
    const auto& e = lo.cells[0].legend_entries;
    const auto& opts = long_legend.axes[0].snap.legend_opts;
    float widest = 0.0f;
    for (const auto& en : e)
        widest = std::max(widest, sextant::text_width(opts.font_path, opts.fontsize, en.label));
    const float expect = sextant::kLegendPad * 2.0f + sextant::kLegendSwatchW
                       + sextant::kLegendGap + widest;
    check(std::fabs(lo.cells[0].legend.w - expect) < 0.001f,
          "the legend box is exactly wide enough for its widest label");
    std::printf("  legend box %.2f px for \"%s\" (%.2f px of text)\n",
                static_cast<double>(lo.cells[0].legend.w),
                e.empty() ? "" : e[0].label.c_str(), static_cast<double>(widest));
}

// The invariant this buys: the PNG and the SVG put the plot frame in the same
// place. Both paths call compute_figure_layout(), so agreeing *in memory* is
// trivial and proves nothing. What this checks is the output -- the rect the
// SVG writer actually emitted and the spine the raster path actually drew,
// against the layout both were given.
void test_png_svg_frame_agreement() {
    std::printf("\n[PNG/SVG frame agreement]\n");

    const int W = 700, H = 500;

    auto fs = make_snapshot(1, 2, 2, 1.0, 1.0e5);
    fs.axes[0].snap.title  = "Left";
    fs.axes[0].snap.ytitle = "y axis";
    fs.axes[0].snap.xtitle = "x axis";
    fs.axes[0].snap.legend_enabled = true;
    fs.axes[0].snap.lines[0].opts.label = "series one";   // the case that used to diverge
    fs.axes[1].snap.title = "Right";
    fs.generation = 1;
    fs.data_generation = 1;

    const auto layout = sextant::compute_figure_layout(fs, W, H);

    const std::string png_path = "layout_agreement.png";
    const std::string svg_path = "layout_agreement.svg";
    {
        // supersample=1 so a spine pixel is a spine pixel, not a box-filtered
        // blend of one — the same reasoning steps 28 and 29 used.
        sextant::GLContext ctx({.width = W, .height = H,
                                .title = "layout_test", .visible = false});
        sextant::NvgRenderer  nvg(ctx.nvg());
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
    int  svg_found = 0;
    for (const auto& c : layout.cells) {
        const std::size_t at = svg_text.find("\" fill=\"white\"/>", pos);
        if (at == std::string::npos) { svg_ok = false; break; }
        const std::size_t start = svg_text.rfind("<rect x=\"", at);
        if (start == std::string::npos) { svg_ok = false; break; }

        float x = 0, y = 0, w = 0, h = 0;
        if (std::sscanf(svg_text.c_str() + start, "<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\"",
                        &x, &y, &w, &h) != 4) { svg_ok = false; break; }
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

    // --- PNG: find the drawn spine. The axes background is pure white and
    // nothing else in the figure is, so its extent locates the frame without
    // having to identify the 1 px spine stroke itself.
    int pw = 0, ph = 0, comp = 0;
    unsigned char* px = stbi_load(png_path.c_str(), &pw, &ph, &comp, 4);
    check(px != nullptr && pw == W && ph == H, "PNG decoded at the requested size");

    if (px) {
        bool raster_ok = true;
        for (const auto& c : layout.cells) {
            // Probe just inside each edge of the frame, and just outside it.
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

            // Inside is the axes background; a few px outside is the figure's
            // own grey. 2 px of slack absorbs the spine stroke's own width.
            if (!is_white(x0 + 3, cy) || !is_white(x1 - 3, cy)) raster_ok = false;
            if (!is_white(cx, y0 + 3) || !is_white(cx, y1 - 3)) raster_ok = false;
            if (is_white(x0 - 3, cy) || is_white(x1 + 3, cy))   raster_ok = false;
            if (is_white(cx, y0 - 3) || is_white(cx, y1 + 3))   raster_ok = false;
        }
        check(raster_ok,
              "the raster frame the PNG actually contains matches the computed frame on all four edges");
        stbi_image_free(px);
    }

    std::printf("  frames: ");
    for (const auto& c : layout.cells)
        std::printf("(%.1f,%.1f %.1fx%.1f) ", static_cast<double>(c.frame.x),
                    static_cast<double>(c.frame.y), static_cast<double>(c.frame.w),
                    static_cast<double>(c.frame.h));
    std::printf("\n");
}

// compute_figure_layout() runs once per frame, and it now measures text where
// the old fixed margins were constants. sextant_perf_test showed a constant
// ~0.1 ms/frame appearing regardless of data size, which is the
// signature of exactly this. Timed here in isolation so the cost has a number
// attached to it rather than an inference from a frame total.
void test_layout_cost() {
    std::printf("\n[layout cost]\n");

    auto one = make_snapshot(1, 1, 1);
    one.axes[0].snap.title  = "Heatmap (viridis)";
    one.axes[0].snap.xtitle = "x axis";
    one.axes[0].snap.ytitle = "y axis";

    auto grid = make_snapshot(3, 4, 12);
    for (auto& fa : grid.axes) fa.snap.title = "cell";

    auto time_it = [](const sextant::FigureSnapshot& fs, int reps) {
        // One untimed call so first-use costs (font file load, glyph cache
        // fill) are not attributed to the steady state.
        volatile float sink = sextant::compute_figure_layout(fs, 1280, 800).cells[0].frame.w;
        (void)sink;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < reps; ++i) {
            const auto l = sextant::compute_figure_layout(fs, 1280, 800);
            sink = l.cells[0].frame.w;
        }
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(t1 - t0).count() / reps;
    };

    const double us_one  = time_it(one, 2000);
    const double us_grid = time_it(grid, 2000);
    std::printf("  1 axes:  %7.2f us/frame\n", us_one);
    std::printf("  12 axes: %7.2f us/frame\n", us_grid);

    // Separate bounds per configuration, because one set loose enough for
    // Debug would not catch anything in Release. What they are written to
    // catch: the first version of this cost 97 us for one axes and 1070 us for
    // twelve, because pick_default_font() re-scanned ~130 families on every
    // measurement -- which sextant_perf_test saw as a constant ~0.1 ms on
    // every frame at every data size.
#ifdef NDEBUG
    const double one_limit = 20.0, grid_limit = 200.0;
#else
    const double one_limit = 150.0, grid_limit = 1200.0;
#endif
    check(us_one < one_limit,   "single-axes layout costs under " +
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
// Phase 3 — the edit lane and the public entry points
// ===========================================================================

// The Controls panel's Layout section pushes margins and gaps through
// FigureEdits' figure-level lane, which has no per-axes entry to ride along
// with. FigureEditBox drains on FigureEdits::empty(), so a field missing from
// that predicate is not a compile error -- it is a control that silently does
// nothing except when some unrelated per-axes edit happens to be pending in
// the same frame. That bug has shipped once already, for the suptitle, and was
// found by reading the code; this is the check that finds it by running.
void test_figure_edit_lane() {
    std::printf("\n[figure edit lane]\n");

    {
        sextant::FigureEditBox box;
        check(!box.load_and_clear().has_value(), "an untouched box drains to nothing");
    }
    {
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
    }
    {
        sextant::FigureEditBox box;
        box.update_figure([](sextant::FigureEdits& f) { f.col_gap = 12.0f; f.row_gap = 13.0f; });
        auto drained = box.load_and_clear();
        check(drained && drained->col_gap && *drained->col_gap == 12.0f
                      && drained->row_gap && *drained->row_gap == 13.0f,
              "a gaps-only edit survives the drain");
    }
    {
        // The live-preview drain the render thread uses is a different
        // function with its own emptiness test, so it needs its own check.
        sextant::FigureEditBox box;
        box.update_figure([](sextant::FigureEdits& f) {
            f.margins = sextant::FigureMargins{5.0f, 5.0f, 5.0f, 5.0f};
        });
        auto drained = box.load_and_clear_journaled();
        check(drained && drained->margins,
              "a margins-only edit also survives the render thread's drain");
    }
}

// Reads the first axes-background rect out of an SVG — the frame, as the
// writer actually emitted it.
bool read_first_svg_frame(const std::string& path, sextant::PlotRect& out) {
    std::ifstream f(path);
    if (!f) return false;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::size_t at = text.find("\" fill=\"white\"/>");
    if (at == std::string::npos) return false;
    const std::size_t start = text.rfind("<rect x=\"", at);
    if (start == std::string::npos) return false;
    return std::sscanf(text.c_str() + start,
                       "<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\"",
                       &out.x, &out.y, &out.w, &out.h) == 4;
}

// Both public entry points for margins, checked against the file that comes
// out rather than against the layout struct: FigureOptions::margins at
// construction, and Figure::set_margins() afterwards. They must agree, and
// the frame must move by exactly the margin delta.
void test_public_margins_api() {
    std::printf("\n[public margins API]\n");

    std::vector<double> x(40), y(40);
    for (int i = 0; i < 40; ++i) { x[i] = i; y[i] = std::sin(i * 0.2); }

    const sextant::FigureMargins custom{40.0f, 25.0f, 30.0f, 35.0f};
    sextant::FigureMargins def;   // 10 all round

    auto render = [&](const char* path, bool via_options, bool via_setter) {
        sextant::FigureOptions o{};
        o.width = 640; o.height = 480;
        if (via_options) o.margins = custom;
        auto fig = sextant::Figure::create(o);
        fig->axes()->line(x, y);
        if (via_setter) fig->set_margins(custom);
        fig->savefig(path);
    };

    render("margins_default.svg", false, false);
    render("margins_options.svg", true,  false);
    render("margins_setter.svg",  false, true);

    sextant::PlotRect a{}, b{}, c{};
    const bool read_ok = read_first_svg_frame("margins_default.svg", a)
                       & read_first_svg_frame("margins_options.svg", b)
                       & read_first_svg_frame("margins_setter.svg",  c);
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

// ===========================================================================
// Contour tracing
// ===========================================================================
//
// trace_contours() and plan_contours() are what both render paths call, and
// neither is reachable through <sextant/sextant.h>. Checking them here rather
// than by eye means a wrong answer is a number: the two failures this guards
// against -- a level traced against the wrong row for origin="upper", and a
// saddle cell joined the wrong way -- both produce plausible pictures.

sextant::HeatmapPlot make_heatmap(int rows, int cols,
                                  const std::function<float(int, int)>& f,
                                  sextant::HeatmapOptions opts) {
    std::vector<float> v(static_cast<std::size_t>(rows) * cols);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            v[static_cast<std::size_t>(r) * cols + c] = f(r, c);
    return sextant::HeatmapPlot{ std::move(v), rows, cols, std::move(opts) };
}

// A frame that maps the 5x5 grid's [0,5]x[0,5] data space onto 500x500
// pixels, so one data unit is exactly 100 px and every expected coordinate
// below is a round number.
sextant::CoordTransform unit_tr() {
    return sextant::CoordTransform{ 0.0, 5.0, 0.0, 5.0,
                                    0.0f, 0.0f, 500.0f, 500.0f, 500.0f, 500.0f };
}

void test_contour_tracing() {
    std::printf("\n[contour tracing]\n");

    // --- A ramp along x. The level-2 iso-line has one place it can be: the
    // column of sample centres whose value is exactly 2, i.e. x = 2.5,
    // running the full height of the sample grid.
    {
        sextant::HeatmapOptions o;
        o.contours = { 2.0 };
        const auto hp = make_heatmap(5, 5, [](int, int c) { return float(c); }, o);
        const auto set = sextant::trace_contours(hp);

        check(set.size() == 1, "ramp: one level traces one line");
        if (set.size() == 1) {
            const auto& l = set[0];
            check(l.level == 2.0, "  and it carries its level");
            check(!l.closed, "  and it is open (it runs off the grid)");
            check(l.x.size() == 5, "  with one vertex per sample row");
            bool all_x = true, y_ok = true;
            for (std::size_t i = 0; i < l.x.size(); ++i) {
                if (l.x[i] != 2.5f) all_x = false;
                if (std::fabs(l.y[i] - (0.5f + static_cast<float>(i))) > 1e-6f) y_ok = false;
            }
            check(all_x, "  every vertex at x=2.5 (the sample centre, not a cell edge)");
            check(y_ok, "  spanning y=0.5..4.5 in order");
        }
    }

    // --- Origin. Storage is row-major with row 0 first either way; only
    // which *data* row a sample reads changes. Trace the same buffer both
    // ways and the line has to move — if it does not, the trace is ignoring
    // origin and will sit mirrored on top of the image for "upper".
    {
        auto ramp_rows = [](int r, int) { return float(r); };
        sextant::HeatmapOptions lo;  lo.contours = { 1.0 };  lo.origin = "lower";
        sextant::HeatmapOptions up;  up.contours = { 1.0 };  up.origin = "upper";
        const auto low_set = sextant::trace_contours(make_heatmap(5, 5, ramp_rows, lo));
        const auto up_set  = sextant::trace_contours(make_heatmap(5, 5, ramp_rows, up));

        check(low_set.size() == 1 && up_set.size() == 1, "origin: both trace one line");
        if (low_set.size() == 1 && up_set.size() == 1) {
            check(std::fabs(low_set[0].y[0] - 1.5f) < 1e-6f,
                  "  origin=lower puts level 1 at y=1.5");
            check(std::fabs(up_set[0].y[0] - 3.5f) < 1e-6f,
                  "  origin=upper puts the same level at y=3.5 (row 1 counted from the top)");
        }
    }

    // --- A single hot cell. The contour around it must close, and close on
    // the cell it surrounds — a chaining bug typically yields two open halves
    // that still draw as a diamond and look right.
    {
        sextant::HeatmapOptions o;
        o.contours = { 5.0 };
        const auto hp = make_heatmap(5, 5,
            [](int r, int c) { return (r == 2 && c == 2) ? 10.0f : 0.0f; }, o);
        const auto set = sextant::trace_contours(hp);

        check(set.size() == 1, "island: one closed line around the hot cell");
        if (set.size() == 1) {
            const auto& l = set[0];
            check(l.closed, "  reported closed");
            check(l.x.size() == 5, "  four crossings, the first repeated to close it");
            check(l.x.front() == l.x.back() && l.y.front() == l.y.back(),
                  "  and the repeat really is the same point");
            float cx = 0.0f, cy = 0.0f;
            for (std::size_t i = 0; i + 1 < l.x.size(); ++i) { cx += l.x[i]; cy += l.y[i]; }
            cx /= 4.0f; cy /= 4.0f;
            check(std::fabs(cx - 2.5f) < 1e-5f && std::fabs(cy - 2.5f) < 1e-5f,
                  "  centred on the cell it encloses");
        }
    }

    // --- Levels, and the two ways of asking for nothing.
    {
        sextant::HeatmapOptions o;
        o.contours = { 1.0, 2.0, 3.0 };
        const auto set = sextant::trace_contours(
            make_heatmap(5, 5, [](int, int c) { return float(c); }, o));
        check(set.size() == 3, "three levels trace three lines");
        check(set[0].level == 1.0 && set[1].level == 2.0 && set[2].level == 3.0,
              "  in the order the levels were given");

        sextant::HeatmapOptions out_of_range;
        out_of_range.contours = { 99.0 };
        check(sextant::trace_contours(
                  make_heatmap(5, 5, [](int, int c) { return float(c); }, out_of_range)).empty(),
              "a level outside the data traces nothing");

        sextant::HeatmapOptions thin;
        thin.contours = { 0.5 };
        check(sextant::trace_contours(
                  make_heatmap(1, 5, [](int, int c) { return float(c); }, thin)).empty(),
              "a one-row heatmap traces nothing (marching squares needs four samples)");
    }
}

void test_contour_planning() {
    std::printf("\n[contour planning]\n");

    sextant::HeatmapOptions o;
    o.contours = { 2.0 };
    const auto hp  = make_heatmap(5, 5, [](int, int c) { return float(c); }, o);
    const auto set = sextant::trace_contours(hp);
    const auto tr  = unit_tr();

    // Unlabelled: the line is projected and handed over whole.
    {
        const auto d = sextant::plan_contours(set, tr, o, "");
        check(d.runs.size() == 1 && d.labels.empty(),
              "no labels: one unbroken run, no text");
        if (d.runs.size() == 1) {
            check(d.runs[0].px.size() == 5, "  every vertex kept");
            check(std::fabs(d.runs[0].px[0] - 250.0f) < 1e-3f &&
                  std::fabs(d.runs[0].py[0] - 450.0f) < 1e-3f,
                  "  projected through the transform (x=2.5 -> 250 px, y=0.5 -> 450 px)");
        }
    }

    // Labelled: the line is broken around the text, and the hole is exactly
    // as wide as the text plus its padding. This is the assertion that the
    // gap is measured against the font rather than being a fixed guess — a
    // hardcoded gap would not track the font size below.
    {
        sextant::HeatmapOptions lo = o;
        lo.contour_labels = true;
        const auto d = sextant::plan_contours(set, tr, lo, "");

        check(d.runs.size() == 2 && d.labels.size() == 1,
              "labels on: the line is broken in two around one label");
        if (d.runs.size() == 2 && d.labels.size() == 1) {
            check(d.labels[0].text == "2", "  labelled with the level, %g-formatted");

            const float gap_top    = d.runs[0].py.back();
            const float gap_bottom = d.runs[1].py.front();
            const float hole = std::fabs(gap_top - gap_bottom);
            const float want = sextant::text_width("", lo.contour_fontsize, "2") + 6.0f;
            check(std::fabs(hole - want) < 0.05f,
                  "  the hole is the text width plus 2x3px of padding");

            check(std::fabs(d.labels[0].x - 250.0f) < 1e-3f &&
                  std::fabs(d.labels[0].y - 250.0f) < 1e-3f,
                  "  the label sits at the line's arc-length midpoint");
            check(std::fabs(d.labels[0].angle + 1.5707963f) < 1e-4f,
                  "  a vertical line labels bottom-to-top (-90 degrees), not upside down");
        }

        // Same line, bigger text: the hole has to grow with it.
        sextant::HeatmapOptions big = lo;
        big.contour_fontsize = 30.0f;
        const auto d2 = sextant::plan_contours(set, tr, big, "");
        if (d2.runs.size() == 2 && d.runs.size() == 2) {
            const float hole1 = std::fabs(d.runs[0].py.back()  - d.runs[1].py.front());
            const float hole2 = std::fabs(d2.runs[0].py.back() - d2.runs[1].py.front());
            check(hole2 > hole1 + 5.0f, "  and a larger font cuts a wider hole");
        }
    }

    // A horizontal line labels horizontally.
    {
        sextant::HeatmapOptions ho;
        ho.contours = { 2.0 };
        ho.contour_labels = true;
        const auto hset = sextant::trace_contours(
            make_heatmap(5, 5, [](int r, int) { return float(r); }, ho));
        const auto d = sextant::plan_contours(hset, tr, ho, "");
        check(d.labels.size() == 1 && std::fabs(d.labels[0].angle) < 1e-4f,
              "a horizontal line's label is not rotated");
    }

    // Too short to break: label dropped, line drawn whole. Squeeze the same
    // line into 20 px of frame so it cannot host its own text.
    {
        sextant::HeatmapOptions lo = o;
        lo.contour_labels = true;
        sextant::CoordTransform tiny = tr;
        tiny.ph = 20.0f; tiny.pw = 20.0f;
        const auto d = sextant::plan_contours(set, tiny, lo, "");
        check(d.runs.size() == 1 && d.labels.empty(),
              "a line too short for its label keeps the line and drops the label");
    }

    // Panned right off the frame: nothing to draw at all.
    {
        sextant::CoordTransform away = tr;
        away.xmin = 100.0; away.xmax = 105.0;
        const auto d = sextant::plan_contours(set, away, o, "");
        check(d.runs.empty(), "a line outside the frame is rejected before projection");
    }
}

void test_contour_cache() {
    std::printf("\n[contour cache]\n");

    sextant::HeatmapOptions o;
    o.contours = { 2.0 };
    auto hp = make_heatmap(5, 5, [](int, int c) { return float(c); }, o);

    sextant::ContourCache cache;
    check(cache.get(0, 0, 7, hp).size() == 1, "first call traces");

    // Flatten the data behind the cache's back, keeping the generation. A
    // cache that re-traced here would report 0 lines; holding the old answer
    // is the whole point — a pan republishes the snapshot every frame without
    // touching the data, and re-running marching squares on each of those
    // frames is what this exists to avoid.
    hp.data.mut().assign(25, 0.0f);
    check(cache.get(0, 0, 7, hp).size() == 1,
          "same data generation reuses the trace, even though the data changed");
    check(cache.get(0, 0, 8, hp).empty(),
          "a new data generation re-traces and sees the flat field");

    // The level list is part of the key: changing it must re-trace even
    // though the data (and its generation) did not move.
    auto hp2 = make_heatmap(5, 5, [](int, int c) { return float(c); }, o);
    check(cache.get(1, 0, 9, hp2).size() == 1, "second plot slot traces on its own");
    hp2.opts.contours = { 1.0, 2.0, 3.0 };
    check(cache.get(1, 0, 9, hp2).size() == 3,
          "changing the levels re-traces at the same generation");

    // Generation 0 means "unstamped", which must never be treated as a hit.
    sextant::ContourCache fresh;
    check(fresh.get(0, 0, 0, hp2).size() == 3, "generation 0 traces");
    hp2.opts.contours = { 2.0 };
    check(fresh.get(0, 0, 0, hp2).size() == 1, "generation 0 never caches");

    // What the cache is worth, on a grid big enough for the trace to matter.
    // The number is the point: this is per level per frame otherwise, on
    // every frame of a drag, on the render thread.
    {
        const int n = 512;
        sextant::HeatmapOptions o;
        o.contours = { 0.2, 0.4, 0.6, 0.8, 1.0, 1.2, 1.4, 1.6 };
        auto big = make_heatmap(n, n, [n](int r, int c) {
            const float dx = (c - n / 2) / (n / 4.0f), dy = (r - n / 2) / (n / 4.0f);
            return std::sqrt(dx * dx + dy * dy);
        }, o);

        sextant::ContourCache cc;
        const auto t0 = std::chrono::steady_clock::now();
        const std::size_t lines = cc.get(0, 0, 1, big).size();
        const auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) cc.get(0, 0, 1, big);
        const auto t2 = std::chrono::steady_clock::now();

        const double trace_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double hit_ms   = std::chrono::duration<double, std::milli>(t2 - t1).count() / 100.0;
        std::printf("  512x512, 8 levels: trace %.2f ms, cached lookup %.4f ms (%zu lines)\n",
                    trace_ms, hit_ms, lines);
        check(lines >= 8, "  the big field traces at least one line per level");
        check(hit_ms * 20.0 < trace_ms,
              "  and a cache hit is at least 20x cheaper than re-tracing");
    }
}

// ===========================================================================
// Data-panel cell shading
// ===========================================================================
//
// What is asserted here is what a screenshot would not settle anyway: that the
// ramp runs blue to red rather than the other way round, that it stays light
// enough everywhere for dark text over it, and that the range cache holds
// across a frame but not across a data edit.

int shade_channel(ImU32 c, int shift) { return static_cast<int>((c >> shift) & 0xFF); }

void test_cell_shading_ramp() {
    std::printf("\n[cell shading: the ramp]\n");

    const ImU32 lo = sextant::shade_color(0.0f);
    const ImU32 mid = sextant::shade_color(0.5f);
    const ImU32 hi = sextant::shade_color(1.0f);

    check(shade_channel(lo, IM_COL32_B_SHIFT) > shade_channel(lo, IM_COL32_R_SHIFT) + 40,
          "the low end is blue");
    check(shade_channel(hi, IM_COL32_R_SHIFT) > shade_channel(hi, IM_COL32_B_SHIFT) + 40,
          "the high end is red");

    // Every stop light, because the cell's text is drawn over it. The dark
    // text is ~24/255, so a stop below ~180 would start to swallow it.
    bool light = true;
    int  darkest = 255;
    for (int i = 0; i <= 20; ++i) {
        const ImU32 c = sextant::shade_color(i / 20.0f);
        for (int s : { IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT })
            darkest = std::min(darkest, shade_channel(c, s));
    }
    light = darkest >= 130;
    check(light, "every stop stays light enough for dark text over it (darkest channel "
                 + std::to_string(darkest) + ")");

    // The middle is the lightest point — a straight blue-to-red lerp would
    // instead dip through a saturated purple here.
    auto luma = [](ImU32 c) {
        return shade_channel(c, IM_COL32_R_SHIFT) + shade_channel(c, IM_COL32_G_SHIFT)
             + shade_channel(c, IM_COL32_B_SHIFT);
    };
    check(luma(mid) > luma(lo) && luma(mid) > luma(hi),
          "and the middle is the lightest stop, not a purple dip");

    // Direction, sampled across the whole ramp. The quantity that has to move
    // monotonically is *blueness against redness*, not either channel alone:
    // both rise on the way to the light middle and only then diverge, so a
    // per-channel monotonicity check would describe a two-stop lerp rather
    // than this ramp. B - R is what a swapped or reversed ramp fails.
    bool monotone = true;
    for (int i = 1; i <= 20; ++i) {
        auto bias = [&](int k) {
            const ImU32 c = sextant::shade_color(k / 20.0f);
            return shade_channel(c, IM_COL32_B_SHIFT) - shade_channel(c, IM_COL32_R_SHIFT);
        };
        if (bias(i) > bias(i - 1)) monotone = false;
    }
    check(monotone, "blue-versus-red falls monotonically from one end to the other");

    // Out-of-range t is clamped, not wrapped or extrapolated.
    check(sextant::shade_color(-3.0f) == lo && sextant::shade_color(9.0f) == hi,
          "t outside [0,1] clamps to the end stops");

    // Hover/active keep the value's own colour rather than a flat highlight.
    const ImU32 hov = sextant::shade_highlight(0.0f, 0.22f);
    check(luma(hov) > luma(lo) && shade_channel(hov, IM_COL32_B_SHIFT)
                                > shade_channel(hov, IM_COL32_R_SHIFT),
          "the hover tint is lighter but still the same colour");
}

void test_cell_shading_range() {
    std::printf("\n[cell shading: the range]\n");

    sextant::ValueRange r;
    r.lo = 10.0; r.hi = 20.0; r.valid = true;
    check(r.norm(10.0) == 0.0f && r.norm(20.0) == 1.0f && r.norm(15.0) == 0.5f,
          "norm is 0 at lo, 1 at hi, and linear between");
    check(r.norm(-100.0) == 0.0f && r.norm(1e9) == 1.0f, "and clamps outside");
    check(r.norm(std::nan("")) == 0.5f, "a non-finite value reads as the neutral middle");

    sextant::ValueRange flat;
    flat.lo = flat.hi = 7.0; flat.valid = true;
    check(flat.norm(7.0) == 0.5f, "a flat column is neutral, not a division by zero");
    check(sextant::ValueRange{}.norm(1.0) == 0.5f, "so is an invalid range");

    sextant::CellShadingCache cache;
    const double col[] = { 3.0, std::nan(""), -1.0, 8.0,
                           std::numeric_limits<double>::infinity() };
    const auto& vr = cache.column(1, 1, sextant::PlotKind::Line, 0, 0, col, 5);
    check(vr.valid && vr.lo == -1.0 && vr.hi == 8.0,
          "the scan skips non-finite values rather than being poisoned by them");

    const double all_bad[] = { std::nan(""), std::nan("") };
    check(!cache.column(1, 1, sextant::PlotKind::Line, 0, 1, all_bad, 2).valid,
          "a column with nothing finite reports no range at all");

    // Columns are independent: a shared range would flatten the narrower one.
    const double wide[]   = { 0.0, 1000.0 };
    const double narrow[] = { 0.0, 1.0 };
    cache.column(1, 1, sextant::PlotKind::Line, 0, 2, wide, 2);
    const auto& n = cache.column(1, 1, sextant::PlotKind::Line, 0, 3, narrow, 2);
    check(n.hi == 1.0, "each column keeps its own range");
}

void test_cell_shading_cache() {
    std::printf("\n[cell shading: the cache]\n");

    sextant::CellShadingCache cache;
    double col[] = { 0.0, 10.0 };
    check(cache.column(5, 1, sextant::PlotKind::Line, 0, 0, col, 2).hi == 10.0,
          "first call scans");

    // Move the data behind the cache's back, keeping the generation and the
    // length. A cache that re-scanned here would cost a full pass over every
    // column on every frame, which is the scrolling case.
    col[1] = 999.0;
    check(cache.column(5, 1, sextant::PlotKind::Line, 0, 0, col, 2).hi == 10.0,
          "the same data generation reuses the scan");
    check(cache.column(6, 1, sextant::PlotKind::Line, 0, 0, col, 2).hi == 999.0,
          "a new generation re-scans — which is what a cell edit produces");

    // A length change at the same generation cannot happen (a row insert is a
    // data op), but it must not silently return a range over the wrong span.
    const double longer[] = { 0.0, 10.0, 50.0 };
    check(cache.column(6, 1, sextant::PlotKind::Line, 0, 0, longer, 3).hi == 50.0,
          "a changed length re-scans too");

    sextant::CellShadingCache fresh;
    check(fresh.column(0, 1, sextant::PlotKind::Line, 0, 0, col, 2).hi == 999.0,
          "generation 0 scans");
    col[1] = 4.0;
    check(fresh.column(0, 1, sextant::PlotKind::Line, 0, 0, col, 2).hi == 4.0,
          "generation 0 never caches");

    // Distinct plots, axes and kinds must not share an entry — the packed key
    // is the one place a collision would be silent.
    sextant::CellShadingCache k;
    const double a[] = { 0.0, 1.0 };
    const double b[] = { 0.0, 2.0 };
    k.column(1, 1, sextant::PlotKind::Line, 0, 0, a, 2);
    check(k.column(1, 1, sextant::PlotKind::Scatter, 0, 0, b, 2).hi == 2.0, "kind is in the key");
    check(k.column(1, 1, sextant::PlotKind::Line, 1, 0, b, 2).hi == 2.0, "plot index is in the key");
    check(k.column(2, 1, sextant::PlotKind::Line, 0, 0, b, 2).hi == 2.0, "axes slot is in the key");

    // A matrix shades against one range for the whole grid.
    sextant::HeatmapPlot hp = make_heatmap(4, 4,
        [](int r, int c) { return static_cast<float>(r * 4 + c); }, {});
    const auto& m = cache.matrix(7, 1, 0, hp.data);
    check(m.valid && m.lo == 0.0 && m.hi == 15.0,
          "a heatmap's range spans the whole matrix, not one column");
}

// The Data panel really drawn, with no window and no backend.
//
// ImGui needs neither: a context with a display size and a claimed texture
// capability produces real ImDrawData off a NewFrame()/Render() pair, and
// every rectangle the panel emitted is in there with its colour. So the half
// that looked interactive-only -- does the toggle reach the cells, and are
// they coloured *by value* -- is assertable after all. Legibility and layout
// stay on the human checklist.
//
// Names exact ramp colours rather than counting them, which keeps the
// assertions independent of the legend strip: it samples the ramp at 16 fixed
// points, none of them 0, 0.5 or 1, the three the checks below look for.
struct PanelFrameColors {
    std::size_t        ramp_vertices = 0;
    std::vector<ImU32> colors;   // sorted, unique

    bool has(ImU32 c) const {
        return std::binary_search(colors.begin(), colors.end(), c);
    }
};

PanelFrameColors run_data_panel(sextant::PanelState& st,
                                const sextant::FigureSnapshot& fsnap, int frames) {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 900.0f);
    io.DeltaTime   = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    // Claim texture handling so Render() does not expect a backend to service
    // the atlas — the documented null-backend arrangement in 1.92.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();

    // Every colour the ramp can produce, so a vertex can be recognised as
    // "shaded" without knowing which cell it came from.
    std::vector<ImU32> ramp;
    for (int i = 0; i <= 1000; ++i) ramp.push_back(sextant::shade_color(i / 1000.0f));
    std::sort(ramp.begin(), ramp.end());
    ramp.erase(std::unique(ramp.begin(), ramp.end()), ramp.end());

    PanelFrameColors out;
    sextant::FigureEditBox box;
    for (int f = 0; f < frames; ++f) {
        ImGui::NewFrame();
        // The panel opens its own window, and ImGui's default size leaves the
        // scrolling table only a few pixels of height — so the clipper draws
        // almost no rows and the check below would be measuring the legend.
        ImGui::SetNextWindowSize(ImVec2(760.0f, 820.0f));
        sextant::draw_data_panel(fsnap, box, st);
        ImGui::Render();

        // Only the last frame counts: the tab bar and the list clipper both
        // need a frame to settle, so an early frame draws no cells at all.
        if (f + 1 < frames) continue;
        std::vector<ImU32> seen;
        const ImDrawData* dd = ImGui::GetDrawData();
        for (int n = 0; n < dd->CmdListsCount; ++n) {
            const ImDrawList* dl = dd->CmdLists[n];
            for (int v = 0; v < dl->VtxBuffer.Size; ++v) {
                const ImU32 c = dl->VtxBuffer[v].col;
                if (!std::binary_search(ramp.begin(), ramp.end(), c)) continue;
                ++out.ramp_vertices;
                seen.push_back(c);
            }
        }
        std::sort(seen.begin(), seen.end());
        seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
        out.colors = std::move(seen);
    }

    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(nullptr);
    return out;
}

sextant::FigureSnapshot one_line_snapshot(const std::vector<double>& x,
                                          const std::vector<double>& y) {
    sextant::LinePlot lp;
    lp.x = x;
    lp.y = y;

    sextant::FigureSnapshot fs;
    sextant::FigureAxesSnapshot fa;
    fa.slot = sextant::AxesSlot{1, 1, 1};
    fa.snap.lines.push_back(std::move(lp));
    fs.axes.push_back(std::move(fa));
    fs.generation = fs.data_generation = 1;
    return fs;
}

void test_data_panel_shading() {
    std::printf("\n[cell shading: the panel actually drawn]\n");

    // Small enough that every row is on screen — the clipper only draws what
    // fits, so a longer column would never render its own maximum.
    std::vector<double> ramp8(8), flat8(8, 5.0);
    for (std::size_t i = 0; i < ramp8.size(); ++i) ramp8[i] = static_cast<double>(i);
    const auto fs_spread = one_line_snapshot(ramp8, ramp8);
    const auto fs_flat   = one_line_snapshot(flat8, flat8);

    const ImU32 c_lo  = sextant::shade_color(0.0f);
    const ImU32 c_mid = sextant::shade_color(0.5f);
    const ImU32 c_hi  = sextant::shade_color(1.0f);

    sextant::PanelState off;
    off.shade_cells = false;
    const auto r_off = run_data_panel(off, fs_spread, 4);
    check(r_off.ramp_vertices == 0,
          "shading off: not one vertex in a ramp colour, legend included");

    sextant::PanelState on;
    on.shade_cells = true;
    const auto r_on = run_data_panel(on, fs_spread, 4);
    check(r_on.ramp_vertices > 0, "shading on: the cells are drawn in ramp colours");
    // The column's own extremes land on the ramp's own extremes. Neither
    // colour is one the legend strip samples, so this is the cells speaking.
    check(r_on.has(c_lo) && r_on.has(c_hi),
          "  the column's min and max land on the ends of the ramp");

    sextant::PanelState flat;
    flat.shade_cells = true;
    const auto r_flat = run_data_panel(flat, fs_flat, 4);
    check(r_flat.has(c_mid) && !r_flat.has(c_lo) && !r_flat.has(c_hi),
          "  a flat column is drawn entirely at the neutral middle");

    std::printf("  ramp vertices off/on %zu/%zu; %zu distinct ramp colours on screen\n",
                r_off.ramp_vertices, r_on.ramp_vertices, r_on.colors.size());
}

// ===========================================================================
// The inverse layout
// ===========================================================================

// figure_size_for_frame() claims to be exact and closed-form. The check is a
// round trip: derive a figure size for a wanted frame, lay the figure out at
// that size, and require the frame that comes back to be the one asked for.
// Anything the forward direction accounts for and the inverse forgets — a
// margin, a gap, the suptitle band, a legend's width — shows up here as a
// miss of exactly that size, which is also what makes a failure readable.
void test_frame_size_round_trip() {
    std::printf("\n[frame size round trip]\n");

    struct Case {
        const char* name;
        int rows, cols, cells, slot;
    };
    const Case cases[] = {
        { "1x1",                    1, 1, 1,  1 },
        { "2x3 grid, cell 1",       2, 3, 6,  1 },
        { "2x3 grid, cell 5",       2, 3, 6,  5 },
        { "3x1 column",             3, 1, 3,  2 },
    };

    int worst_case_count = 0;
    float worst_err = 0.0f;
    std::string worst_desc;

    for (const auto& c : cases) {
        for (int variant = 0; variant < 5; ++variant) {
            auto fs = make_snapshot(c.rows, c.cols, c.cells, 1.0, 1.0e5);
            // Each variant turns on something the inverse has to account
            // for. They accumulate, so the last one carries all of them.
            if (variant >= 1) {
                for (auto& fa : fs.axes) {
                    fa.snap.title  = "A title";
                    fa.snap.xtitle = "x axis";
                    fa.snap.ytitle = "y axis";
                }
            }
            if (variant >= 2) {
                fs.margins = { 33.0f, 17.0f, 21.0f, 29.0f };
                fs.col_gap = 13.0f;
                fs.row_gap = 19.0f;
            }
            if (variant >= 3) {
                fs.suptitle = "Figure title";
                fs.suptitle_opts.fontsize = 26.0f;
            }
            if (variant >= 4) {
                // The decoration that makes "which frame" a real question:
                // carved from its own cell, so only that cell's frame is
                // pinned by the requested size.
                auto& target = fs.axes[static_cast<std::size_t>(c.slot) - 1].snap;
                target.legend_enabled = true;
                target.lines[0].opts.label = "a labelled series";
            }

            for (auto [fw, fh] : {std::pair{120, 90}, std::pair{400, 300},
                                  std::pair{640, 480}, std::pair{37, 23}}) {
                const sextant::LayoutSize s = sextant::figure_size_for_frame(
                    fs, c.slot, static_cast<float>(fw), static_cast<float>(fh));

                const int W = static_cast<int>(std::lround(s.width));
                const int H = static_cast<int>(std::lround(s.height));
                const auto layout = sextant::compute_figure_layout(fs, W, H);

                const sextant::CellLayout* cell = nullptr;
                for (const auto& cl : layout.cells)
                    if (cl.slot.index == c.slot) { cell = &cl; break; }
                if (!cell) { ++worst_case_count; continue; }

                const float ew = std::fabs(cell->frame.w - static_cast<float>(fw));
                const float eh = std::fabs(cell->frame.h - static_cast<float>(fh));
                const float err = std::max(ew, eh);
                // A whole pixel of slack, and no more: the figure size has to
                // be rounded to an integer, and the insets it is built from
                // are fractional. Nothing else is allowed to leak in.
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
    std::printf("  4 grids x 5 variants x 4 sizes = 80 round trips, %d misses\n",
                worst_case_count);
}

// The inverse must move with everything the forward direction depends on.
// If it ignored one of these the round trip above would still pass whenever
// that thing happened to be zero, so each is checked to actually *change*
// the derived size.
void test_frame_size_responds_to_layout() {
    std::printf("\n[inverse responds to layout]\n");

    auto base = make_snapshot(2, 2, 4);
    const auto size_of = [](const sextant::FigureSnapshot& fs) {
        return sextant::figure_size_for_frame(fs, 1, 300.0f, 200.0f);
    };
    const sextant::LayoutSize b = size_of(base);

    auto bigger_margins = base;  bigger_margins.margins = {50.0f, 50.0f, 50.0f, 50.0f};
    auto bigger_gaps    = base;  bigger_gaps.col_gap = 40.0f; bigger_gaps.row_gap = 40.0f;
    auto with_suptitle  = base;  with_suptitle.suptitle = "S";
    auto with_titles    = base;
    for (auto& fa : with_titles.axes) { fa.snap.title = "T"; fa.snap.xtitle = "x"; fa.snap.ytitle = "y"; }
    auto with_legend    = base;
    with_legend.axes[0].snap.legend_enabled = true;
    with_legend.axes[0].snap.lines[0].opts.label = "series";

    check(size_of(bigger_margins).width > b.width, "larger margins need a larger figure");
    check(size_of(bigger_gaps).width   > b.width,  "larger gaps need a larger figure");
    check(size_of(with_suptitle).height > b.height, "a suptitle needs a taller figure");
    check(size_of(with_titles).width   > b.width,  "axis titles need a larger figure");
    check(size_of(with_legend).width   > b.width,  "a legend needs a wider figure");
    check(std::fabs(size_of(with_legend).height - b.height) < 0.001f,
          "...but not a taller one — the legend is carved horizontally");

    // Only the named slot is pinned. Asking for slot 2's frame on a figure
    // where slot 1 carries the legend gives a different answer than asking
    // for slot 1's, which is the whole reason the argument exists.
    const auto s1 = sextant::figure_size_for_frame(with_legend, 1, 300.0f, 200.0f);
    const auto s2 = sextant::figure_size_for_frame(with_legend, 2, 300.0f, 200.0f);
    check(s1.width > s2.width,
          "the slot argument matters: a legend-bearing cell needs more figure than a bare one");
    std::printf("  slot 1 (legend) %.0f wide vs slot 2 (bare) %.0f\n",
                static_cast<double>(s1.width), static_cast<double>(s2.width));
}

// Both public entry points, checked against the emitted SVG rather than the
// layout struct — resize_to_frame() has to reach savefig()'s default size,
// which is a different field from the one the layout reads.
void test_public_frame_resize() {
    std::printf("\n[public frame resize]\n");

    std::vector<double> x(40), y(40);
    for (int i = 0; i < 40; ++i) { x[i] = i; y[i] = std::sin(i * 0.2); }

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

    // Margins are part of the inversion, so changing them must change the
    // figure size while leaving the frame where it was asked to be.
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

} // namespace

int main() {
    std::printf("=== sextant_layout_test ===\n\n");
    test_text_metrics();
    test_missing_font_fallback();
    test_concurrent_measurement();

    test_font_size_drives_layout();
    test_absent_decorations_cost_nothing();
    test_wide_tick_labels();
    test_grid_alignment();
    test_margins();
    test_degenerate_sizes();
    test_legend_and_colorbar_carve();
    test_png_svg_frame_agreement();
    test_layout_cost();
    test_figure_edit_lane();
    test_public_margins_api();

    test_frame_size_round_trip();
    test_frame_size_responds_to_layout();
    test_public_frame_resize();

    test_contour_tracing();
    test_contour_planning();
    test_contour_cache();

    test_cell_shading_ramp();
    test_cell_shading_range();
    test_cell_shading_cache();
    test_data_panel_shading();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
