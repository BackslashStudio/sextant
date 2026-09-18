// The Data panel's cell tint: the ramp, its range, its cache, and the drawn panel.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {

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
    const auto& vr = cache.column(1, 1, -1, sextant::PlotKind::Line, 0, 0, col, 5);
    check(vr.valid && vr.lo == -1.0 && vr.hi == 8.0,
          "the scan skips non-finite values rather than being poisoned by them");

    const double all_bad[] = { std::nan(""), std::nan("") };
    check(!cache.column(1, 1, -1, sextant::PlotKind::Line, 0, 1, all_bad, 2).valid,
          "a column with nothing finite reports no range at all");

    // Columns are independent: a shared range would flatten the narrower one.
    const double wide[]   = { 0.0, 1000.0 };
    const double narrow[] = { 0.0, 1.0 };
    cache.column(1, 1, -1, sextant::PlotKind::Line, 0, 2, wide, 2);
    const auto& n = cache.column(1, 1, -1, sextant::PlotKind::Line, 0, 3, narrow, 2);
    check(n.hi == 1.0, "each column keeps its own range");
}

void test_cell_shading_cache() {
    std::printf("\n[cell shading: the cache]\n");

    sextant::CellShadingCache cache;
    double col[] = { 0.0, 10.0 };
    check(cache.column(5, 1, -1, sextant::PlotKind::Line, 0, 0, col, 2).hi == 10.0,
          "first call scans");

    // Move the data behind the cache's back, keeping the generation and the
    // length. A cache that re-scanned here would cost a full pass over every
    // column on every frame, which is the scrolling case.
    col[1] = 999.0;
    check(cache.column(5, 1, -1, sextant::PlotKind::Line, 0, 0, col, 2).hi == 10.0,
          "the same data generation reuses the scan");
    check(cache.column(6, 1, -1, sextant::PlotKind::Line, 0, 0, col, 2).hi == 999.0,
          "a new generation re-scans — which is what a cell edit produces");

    // A length change at the same generation cannot happen (a row insert is a
    // data op), but it must not silently return a range over the wrong span.
    const double longer[] = { 0.0, 10.0, 50.0 };
    check(cache.column(6, 1, -1, sextant::PlotKind::Line, 0, 0, longer, 3).hi == 50.0,
          "a changed length re-scans too");

    sextant::CellShadingCache fresh;
    check(fresh.column(0, 1, -1, sextant::PlotKind::Line, 0, 0, col, 2).hi == 999.0,
          "generation 0 scans");
    col[1] = 4.0;
    check(fresh.column(0, 1, -1, sextant::PlotKind::Line, 0, 0, col, 2).hi == 4.0,
          "generation 0 never caches");

    // Distinct plots, axes and kinds must not share an entry — the packed key
    // is the one place a collision would be silent.
    sextant::CellShadingCache k;
    const double a[] = { 0.0, 1.0 };
    const double b[] = { 0.0, 2.0 };
    k.column(1, 1, -1, sextant::PlotKind::Line, 0, 0, a, 2);
    check(k.column(1, 1, -1, sextant::PlotKind::Scatter, 0, 0, b, 2).hi == 2.0, "kind is in the key");
    check(k.column(1, 1, -1, sextant::PlotKind::Line, 1, 0, b, 2).hi == 2.0, "plot index is in the key");
    check(k.column(2, 1, -1, sextant::PlotKind::Line, 0, 0, b, 2).hi == 2.0, "axes slot is in the key");
    check(k.column(1, 1, 0, sextant::PlotKind::Line, 0, 0, b, 2).hi == 2.0,
          "the plane is in the key too -- two planes of one cell each hold a 'line 0'");

    // A matrix shades against one range for the whole grid.
    sextant::HeatmapPlot hp = make_heatmap(4, 4,
        [](int r, int c) { return static_cast<float>(r * 4 + c); }, {});
    const auto& m = cache.matrix(7, 1, -1, 0, hp.data);
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

}  // namespace lt
