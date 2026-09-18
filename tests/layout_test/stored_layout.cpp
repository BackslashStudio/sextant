// The stored layout (v1.0 step 15.2): measurements taken once and laid out at
// any size, the window's LayoutStore that decides when to take them again, the
// allow-list of edits that do not, and exports that keep what is on screen.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {

namespace {

using namespace sextant;

constexpr int SW = 820, SH = 560;

// A 1x2 grid, both cells 2D with titles, the first with a legend and a bar, so
// every kind of measurement is present and the grid solve has two columns.
FigureSnapshot stored_snapshot() {
    FigureSnapshot fs = make_snapshot(1, 2, 2);
    for (auto& fa : fs.axes) {
        RenderSnapshot& s = *fa.snap2d();
        s.title = "Title"; s.xtitle = "x"; s.ytitle = "y";
    }
    RenderSnapshot& s = *fs.axes[0].snap2d();
    s.lines[0].opts.name = "series";
    s.legend_enabled = true;
    ScatterZPlot p;
    p.x = CowVec<double>{ std::vector<double>{ 0.1, 0.5 } };
    p.y = CowVec<double>{ std::vector<double>{ 0.2, 0.6 } };
    p.z = CowVec<double>{ std::vector<double>{ 0.0, 1.0 } };
    p.opts.colorbar = true;
    p.opts.name = "flux";
    s.scatter_z.push_back(p);
    fs.generation = fs.data_generation = fs.layout_generation = 1;
    return fs;
}

// What a pan to a range with much wider y tick labels leaves in the snapshot.
void pan_wide(FigureSnapshot& fs, int cell) {
    RenderSnapshot& s = *fs.axes[cell].snap2d();
    s.ylim_auto = false;
    s.ymin = -150000.0;
    s.ymax =  150000.0;
}

bool same_rect(const PlotRect& a, const PlotRect& b, float tol = 1e-3f) {
    return near_px(a.x, b.x, tol) && near_px(a.y, b.y, tol)
        && near_px(a.w, b.w, tol) && near_px(a.h, b.h, tol);
}

bool same_frames(const FigureLayout& a, const FigureLayout& b) {
    if (a.cells.size() != b.cells.size()) return false;
    for (std::size_t i = 0; i < a.cells.size(); ++i)
        if (!same_rect(a.cells[i].frame, b.cells[i].frame)
            || !same_rect(a.cells[i].legend, b.cells[i].legend)
            || a.cells[i].colorbars.size() != b.cells[i].colorbars.size())
            return false;
    return true;
}

}  // namespace

// Measuring and laying out separately is the same layout as doing both at once,
// at every size, and a stored measure is laid out with this frame's limits.
void test_stored_measure() {
    std::printf("\n[stored layout: measure, then lay out]\n");

    const FigureSnapshot fs = stored_snapshot();
    const FigureMeasure  m  = measure_figure(fs);
    check(measure_fits(m, fs), "stored: a measure fits the snapshot it was taken from");

    bool all_same = true;
    for (const auto& [w, h] : { std::pair{ SW, SH }, std::pair{ 400, 300 }, std::pair{ 1600, 900 } })
        all_same = all_same && same_frames(compute_figure_layout(fs, m, w, h),
                                           compute_figure_layout(fs, w, h));
    check(all_same, "stored: a measure laid out at three sizes is the fresh layout at each");

    FigureMeasure out;
    compute_figure_layout(fs, SW, SH, &out);
    check(same_frames(compute_figure_layout(fs, out, 700, 500), compute_figure_layout(fs, 700, 500)),
          "stored: the measure a fresh layout hands back lays out the same");

    // Navigation: the y labels get much wider, and the stored measure does
    // not see it -- the frame stays put, while the transform follows the pan.
    FigureSnapshot panned = fs;
    pan_wide(panned, 0);
    const FigureLayout frozen = compute_figure_layout(panned, m, SW, SH);
    const FigureLayout fresh  = compute_figure_layout(panned, SW, SH);
    const FigureLayout before = compute_figure_layout(fs, SW, SH);
    check(fresh.cells[0].frame.x > before.cells[0].frame.x + 10.0f,
          "stored: (the pan really does widen the y labels a fresh layout reserves)");
    check(same_rect(frozen.cells[0].frame, before.cells[0].frame),
          "stored: laid out from the stored measure, the panned frame does not move");
    check(frozen.cells[0].tr.ymax == 150000.0
              && frozen.cells[0].yticks.front().label != before.cells[0].yticks.front().label,
          "stored: while its limits and ticks are this frame's");

    // A snapshot the measure does not fit is laid out afresh instead.
    FigureSnapshot grown = fs;
    grown.axes.pop_back();
    grown.axes[0].slot = AxesSlot{ 1, 1, 1 };
    check(!measure_fits(m, grown) && same_frames(compute_figure_layout(grown, m, SW, SH),
                                                 compute_figure_layout(grown, SW, SH)),
          "stored: a grid the measure was not taken from gets a fresh layout");
    FigureSnapshot more = fs;
    more.axes[1].snap2d()->lines[0].opts.name = "now keyed";
    more.axes[1].snap2d()->legend_enabled = true;
    check(!measure_fits(m, more), "stored: nor does one with a legend entry the measure lacks");
    FigureSnapshot three = fs;
    three.axes[0].snap2d()->scatter_z.push_back(three.axes[0].snap2d()->scatter_z[0]);
    check(!measure_fits(m, three), "stored: or a colorbar it lacks");
    FigureSnapshot kind = fs;
    kind.axes[1] = make_snapshot3d(1, 2, 2).axes[0];
    check(!measure_fits(m, kind), "stored: or a cell of the other kind");
}

// The inverse from a stored measure inverts the layout from that measure, so
// a window's Resize dialog answers for the frame it shows.
void test_stored_inverse() {
    std::printf("\n[stored layout: the inverse]\n");

    const FigureSnapshot fs = stored_snapshot();
    const FigureMeasure  m  = measure_figure(fs);
    FigureSnapshot panned = fs;
    pan_wide(panned, 0);

    int misses = 0, trips = 0;
    for (int slot = 1; slot <= 2; ++slot)
        for (const auto& [fw, fh] : { std::pair{ 300, 200 }, std::pair{ 520, 410 } }) {
            const LayoutSize sz = figure_size_for_frame(panned, m, slot, float(fw), float(fh));
            const FigureLayout lay = compute_figure_layout(panned, m, int(std::lround(sz.width)),
                                                           int(std::lround(sz.height)));
            const PlotRect& f = lay.cells[slot - 1].frame;
            ++trips;
            if (std::fabs(f.w - fw) > 1.0f || std::fabs(f.h - fh) > 1.0f) ++misses;
        }
    check(misses == 0, "stored: the stored inverse round-trips through the stored layout ("
                       + std::to_string(trips) + " trips)");
    const LayoutSize a = figure_size_for_frame(panned, m, 1, 300.0f, 200.0f);
    const LayoutSize b = figure_size_for_frame(panned, 1, 300.0f, 200.0f);
    check(b.width > a.width + 10.0f,
          "stored: and differs from the fresh inverse by exactly what the pan froze");

    // An export's measure: the window's furniture, everything else its own.
    FigureSnapshot renamed = panned;
    renamed.axes[0].snap2d()->lines[0].opts.name = "a much longer series name";
    const FigureMeasure mixed = measure_figure(renamed, &m);
    check(mixed.cells[0].axis.left == m.cells[0].axis.left
              && mixed.cells[0].dec.legend_box_w > m.cells[0].dec.legend_box_w,
          "stored: a measure over a frozen one keeps its furniture and measures its legend");
    FigureSnapshot swapped = renamed;
    swapped.axes[0] = make_snapshot3d(1, 2, 1).axes[0];
    const FigureMeasure over3d = measure_figure(swapped, &m);
    check(over3d.cells[0].axis.left == 0.0f,
          "stored: and a cell that changed kind takes nothing from the frozen one");
}

// The window's store re-measures on an event and on nothing else.
void test_layout_store() {
    std::printf("\n[stored layout: the window's store]\n");

    LayoutStore store;
    check(store.load() == nullptr, "store: nothing published before the first frame");

    FigureSnapshot fs = stored_snapshot();
    const FigureLayout first = store.fit(fs, SW, SH);
    const auto m1 = store.load();
    check(m1 != nullptr && same_frames(first, compute_figure_layout(fs, SW, SH)),
          "store: the first frame measures and publishes");

    // A pan: new limits, same layout generation.
    FigureSnapshot panned = fs;
    pan_wide(panned, 0);
    const FigureLayout nav = store.fit(panned, SW, SH);
    check(store.load() == m1 && same_rect(nav.cells[0].frame, first.cells[0].frame)
              && nav.cells[0].tr.ymax == 150000.0,
          "store: navigation lays out from what it holds -- the frame stays, the limits move");

    const FigureLayout resized = store.fit(panned, SW + 40, SH);
    const auto m2 = store.load();
    check(m2 != m1 && same_frames(resized, compute_figure_layout(panned, SW + 40, SH)),
          "store: a resize re-measures, so the panned labels get their room");
    store.fit(panned, SW + 40, SH);
    check(store.load() == m2, "store: and the frame after it does not");

    store.request_refit();
    store.fit(panned, SW + 40, SH);
    const auto m3 = store.load();
    check(m3 != m2, "store: File > Refit layout re-measures at the same size");
    check(m1->cells.size() == 2 && m1->cells[0].axis.left < m3->cells[0].axis.left,
          "store: (and an old measure a caller still holds is untouched by it)");

    FigureSnapshot edited = panned;
    edited.layout_generation = 2;
    edited.axes[1].snap2d()->title = "";
    const FigureLayout after = store.fit(edited, SW + 40, SH);
    check(store.load() != m3 && same_frames(after, compute_figure_layout(edited, SW + 40, SH)),
          "store: a new layout generation re-measures");

    FigureSnapshot unstamped = edited;
    unstamped.layout_generation = 0;
    const auto m4 = store.load();
    store.fit(unstamped, SW + 40, SH);
    check(store.load() != m4, "store: an unstamped snapshot never counts as unchanged");

    FigureSnapshot regrid = edited;
    regrid.layout_generation = 3;
    store.fit(regrid, SW + 40, SH);
    regrid.axes.pop_back();
    regrid.axes[0].slot = AxesSlot{ 1, 1, 1 };
    const auto m5 = store.load();
    const FigureLayout one = store.fit(regrid, SW + 40, SH);
    check(store.load() != m5 && one.cells.size() == 1,
          "store: nor does one its measure no longer fits, whatever its generation");
}

// Which drains leave the stored layout alone: navigation, and only navigation.
void test_navigation_edits() {
    std::printf("\n[stored layout: navigation edits]\n");

    auto nav = [](auto&& fill) {
        FigureEditBox box;
        fill(box);
        const auto e = box.load_and_clear_journaled();
        return e && is_navigation_only(*e);
    };
    check(nav([](FigureEditBox& b) { b.update(1, [](AxesEdit& e) {
              e.xmin = 1.0; e.xmax = 2.0; e.xlim_auto = false; e.ymin = 0.0; e.ymax = 5.0; }); }),
          "nav: a 2D pan or zoom is navigation");
    check(nav([](FigureEditBox& b) { b.update(1, [](AxesEdit& e) {
              e.xlim_auto = true; e.ylim_auto = true; }); }),
          "nav: a 2D reset is navigation");
    check(nav([](FigureEditBox& b) { b.update(2, [](AxesEdit& e) {
              e.yticks_override = std::vector<Tick>{ { 0.0, "zero" } }; }); }),
          "nav: a tick override is navigation");
    check(nav([](FigureEditBox& b) { b.update3d(1, [](AxesEdit3D& e) { e.camera = Camera3D{}; }); }),
          "nav: a 3D camera move is navigation");

    check(!nav([](FigureEditBox& b) { b.update(1, [](AxesEdit& e) {
               e.xmin = 1.0; e.title = std::string("t"); }); }),
          "nav: a pan that carries a title with it is not");
    check(!nav([](FigureEditBox& b) { b.update(1, [](AxesEdit& e) {
               e.legend_opts = LegendOptions{}; }); }),
          "nav: a legend edit is not");
    check(!nav([](FigureEditBox& b) { b.update(1, [](AxesEdit& e) {
               e.colorbar_opts = ColorbarOptions{}; }); }),
          "nav: a colorbar edit is not");
    check(!nav([](FigureEditBox& b) { b.update(1, [](AxesEdit& e) {
               e.plot_ops.push_back(PlotCellEdit{}); }); }),
          "nav: a data edit is not");
    check(!nav([](FigureEditBox& b) { b.update3d(1, [](AxesEdit3D& e) {
               e.camera = Camera3D{}; e.zmin = 0.0; }); }),
          "nav: a 3D limits edit is not (only the camera is listed)");
    check(!nav([](FigureEditBox& b) { b.update_figure([](FigureEdits& f) { f.col_gap = 3.0f; }); }),
          "nav: a figure-level gap is not");
    check(!nav([](FigureEditBox& b) { b.update(1, [](AxesEdit& e) {
               e.axes_style = AxesStyle{}; }); }),
          "nav: an axes style is not");
}

// An export given the window's measurements lays the figure out as the window
// does, at any size; without them it is a fresh fit.
void test_stored_export() {
    std::printf("\n[stored layout: exports]\n");

    const FigureSnapshot fs = stored_snapshot();
    const FigureMeasure  m  = measure_figure(fs);
    FigureSnapshot panned = fs;
    pan_wide(panned, 0);

    const int W = 760, H = 520;
    export_figure_svg(panned, "stored_on_screen.svg", W, H, {}, nullptr, &m);
    export_figure_svg(panned, "stored_fresh.svg", W, H);
    PlotRect on{}, fr{};
    check(read_first_svg_frame("stored_on_screen.svg", on) && read_first_svg_frame("stored_fresh.svg", fr),
          "export: both SVGs written and parsed");
    const PlotRect want_on = compute_figure_layout(panned, m, W, H).cells[0].frame;
    const PlotRect want_fr = compute_figure_layout(panned, W, H).cells[0].frame;
    check(std::fabs(on.x - want_on.x) <= 0.5f && std::fabs(on.w - want_on.w) <= 0.5f,
          "export: an SVG given the on-screen measure has the on-screen frame at its own size");
    check(std::fabs(fr.x - want_fr.x) <= 0.5f && fr.x > on.x + 10.0f,
          "export: and one given none is a fresh fit, room made for the wide labels");

    // The PNG path: the frame's white background starts where the layout says.
    {
        GLContext ctx({ .width = W, .height = H, .title = "layout_test", .visible = false });
        NvgRenderer  nvg(ctx.nvg());
        DataRenderer data;
        export_figure_png(ctx, nvg, data, panned, "stored_on_screen.png", W, H, 1, 0, &m);
    }
    int pw = 0, ph = 0, comp = 0;
    unsigned char* px = stbi_load("stored_on_screen.png", &pw, &ph, &comp, 4);
    check(px != nullptr && pw == W && ph == H, "export: the PNG decoded");
    if (px) {
        // Between the on-screen frame's left edge and the fresh one's, a
        // quarter of the way down, clear of the line along the middle: white
        // only if the PNG used the stored frame (the fresh layout has y tick
        // labels on grey there).
        const int y  = static_cast<int>(want_on.y + want_on.h * 0.25f);
        const int x  = static_cast<int>((want_on.x + want_fr.x) * 0.5f);
        const unsigned char* p = &px[(y * W + x) * 4];
        check(p[0] > 245 && p[1] > 245 && p[2] > 245,
              "export: the PNG given the on-screen measure draws the on-screen frame");
        stbi_image_free(px);
    }
}

}  // namespace lt
