// Legend and colorbar placement (v1.0 step 15.1): anchors, margins, offsets,
// the extended frame, and the inverse layout under all of them.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {

namespace {

using namespace sextant;

constexpr int PW = 900, PH = 640;

// One 2D axes with titles on every side (so the extended frame is visibly
// larger than the frame) and `n` named lines.
FigureSnapshot legend_snapshot(int n, LegendAnchor anchor) {
    FigureSnapshot fs = make_snapshot(1, 1, 1);
    RenderSnapshot& s = *fs.axes[0].snap2d();
    s.title = "Title"; s.xtitle = "x axis"; s.ytitle = "y axis";
    const LinePlot proto = s.lines[0];
    s.lines.clear();
    for (int i = 0; i < n; ++i) {
        LinePlot lp = proto;
        lp.opts.name = "series number " + std::to_string(i);
        s.lines.push_back(std::move(lp));
    }
    s.legend_enabled = true;
    s.legend_opts.anchor = anchor;
    return fs;
}

void add_colorbar(RenderSnapshot& s, const std::string& name = "flux") {
    ScatterZPlot p;
    p.x = CowVec<double>{ std::vector<double>{ 0.1, 0.5 } };
    p.y = CowVec<double>{ std::vector<double>{ 0.2, 0.6 } };
    p.z = CowVec<double>{ std::vector<double>{ 0.0, 1.0 } };
    p.opts.colorbar = true;
    p.opts.vmin = 0.0f; p.opts.vmax = 1.0f;
    p.opts.name = name;
    s.scatter_z.push_back(p);
}

float right(const PlotRect& r)  { return r.x + r.w; }
float bottom(const PlotRect& r) { return r.y + r.h; }

bool same_rect(const PlotRect& a, const PlotRect& b, float tol = 1e-3f) {
    return near_px(a.x, b.x, tol) && near_px(a.y, b.y, tol)
        && near_px(a.w, b.w, tol) && near_px(a.h, b.h, tol);
}

bool inside(const PlotRect& in, const PlotRect& out) {
    return in.x >= out.x - 1e-3f && in.y >= out.y - 1e-3f
        && right(in) <= right(out) + 1e-3f && bottom(in) <= bottom(out) + 1e-3f;
}

}  // namespace

// The extended frame is the frame plus its axis furniture plus frame_margin,
// and frame_margin is reserved on every side.
void test_extended_frame() {
    std::printf("\n[placement: the extended frame]\n");

    FigureSnapshot a = legend_snapshot(1, LegendAnchor::OutsideRT);
    a.axes[0].snap2d()->legend_enabled = false;
    FigureSnapshot b = a;
    b.axes[0].snap2d()->axes_style.frame_margin = 12.0f;

    const CellLayout ca = compute_figure_layout(a, PW, PH).cells[0];
    const CellLayout cb = compute_figure_layout(b, PW, PH).cells[0];

    check(ca.extended.x < ca.frame.x && bottom(ca.extended) > bottom(ca.frame)
          && ca.extended.y < ca.frame.y,
          "extended: a 2D frame's furniture sits inside the extended frame");
    check(near_px(ca.ytitle_x - font_vmetrics("", a.axes[0].snap2d()->axes_style.ytitle_fontsize)
                                    .line_height * 0.5f, ca.extended.x),
          "extended: its left edge is the outer edge of the y title");
    check(near_px(ca.frame.w - cb.frame.w, 24.0f) && near_px(ca.frame.h - cb.frame.h, 24.0f),
          "extended: frame_margin is reserved on all four sides");
    check(near_px(cb.frame.x - cb.extended.x, ca.frame.x - ca.extended.x + 12.0f)
          && near_px(cb.extended.w - cb.frame.w, ca.extended.w - ca.frame.w + 24.0f),
          "extended: and the extended frame grows by it");

    FigureSnapshot three = make_snapshot3d(1, 1, 1);
    three.axes[0].snap3d()->axes_style.frame_margin = 7.0f;
    const CellLayout c3 = compute_figure_layout(three, PW, PH).cells[0];
    check(near_px(c3.extended.x, c3.frame.x - 7.0f) && near_px(c3.extended.w, c3.frame.w + 14.0f),
          "extended: a 3D cell's is its frame plus the margin -- its labels are inside");
}

void test_legend_anchors() {
    std::printf("\n[placement: legend anchors]\n");

    FigureSnapshot plain = legend_snapshot(3, LegendAnchor::OutsideRT);
    plain.axes[0].snap2d()->legend_enabled = false;
    const CellLayout p = compute_figure_layout(plain, PW, PH).cells[0];

    // ---- Inside: over the data, reserving nothing.
    const LegendAnchor inside_anchors[] = { LegendAnchor::InsideTL, LegendAnchor::InsideTR,
                                            LegendAnchor::InsideBL, LegendAnchor::InsideBR };
    bool frames_same = true, in_frame = true, cornered = true;
    for (LegendAnchor an : inside_anchors) {
        const FigureSnapshot fs = legend_snapshot(3, an);
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        const float m = fs.axes[0].snap2d()->legend_opts.margin;
        if (!same_rect(c.frame, p.frame)) frames_same = false;
        if (!inside(c.legend, c.frame)) in_frame = false;
        const bool left = an == LegendAnchor::InsideTL || an == LegendAnchor::InsideBL;
        const bool top  = an == LegendAnchor::InsideTL || an == LegendAnchor::InsideTR;
        const float dx = left ? c.legend.x - c.frame.x : right(c.frame) - right(c.legend);
        const float dy = top  ? c.legend.y - c.frame.y : bottom(c.frame) - bottom(c.legend);
        if (!near_px(dx, m) || !near_px(dy, m)) cornered = false;
    }
    check(frames_same, "legend inside: the frame is exactly what it is with no legend");
    check(in_frame,    "legend inside: the box is within the frame");
    check(cornered,    "legend inside: `margin` from its corner on both axes");

    // ---- Outside, beside the extended frame.
    {
        const FigureSnapshot fs = legend_snapshot(3, LegendAnchor::OutsideRT);
        const CellDecorations d = compute_cell_decorations(*fs.axes[0].snap2d());
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        const float m = fs.axes[0].snap2d()->legend_opts.margin;
        check(near_px(p.frame.w - c.frame.w, d.legend_block) && near_px(c.frame.x, p.frame.x),
              "legend right: carved from the right, by margin + box width");
        check(near_px(c.legend.x, right(c.extended) + m) && near_px(c.legend.y, c.extended.y + m),
              "legend RT: margin right of the extended frame, margin down from its top");
        std::set<float> xs;
        for (const auto& s : c.legend_slots) xs.insert(s.x);
        check(xs.size() == 1 && c.legend_slots.size() == 3,
              "legend right: entries in one column");

        const FigureSnapshot rb = legend_snapshot(3, LegendAnchor::OutsideRB);
        const CellLayout cr = compute_figure_layout(rb, PW, PH).cells[0];
        check(near_px(bottom(cr.legend), bottom(cr.extended) - m) && same_rect(cr.frame, c.frame),
              "legend RB: flush with the extended frame's bottom instead, same frame");
    }
    {
        const FigureSnapshot fs = legend_snapshot(3, LegendAnchor::OutsideLT);
        const CellDecorations d = compute_cell_decorations(*fs.axes[0].snap2d());
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        const float m = fs.axes[0].snap2d()->legend_opts.margin;
        check(near_px(c.frame.x - p.frame.x, d.legend_block) && near_px(right(c.frame), right(p.frame)),
              "legend left: carved from the left, by the same margin + box width");
        check(near_px(right(c.legend) + m, c.extended.x),
              "legend LT: margin left of the extended frame -- outboard of the y title");
    }

    // ---- Outside, above and below: rows, wrapped to the extended frame.
    {
        const FigureSnapshot fs = legend_snapshot(9, LegendAnchor::OutsideTL);
        const CellLayout c = compute_figure_layout(fs, 700, PH).cells[0];
        const float m = fs.axes[0].snap2d()->legend_opts.margin;
        std::set<float> rows;
        for (const auto& s : c.legend_slots) rows.insert(s.cy);
        check(rows.size() > 1 && rows.size() < 9,
              "legend TL: nine entries wrap onto several rows, several to a row (" +
              std::to_string(rows.size()) + " rows)");
        check(c.legend.w <= c.extended.w - 2.0f * m + 1e-3f,
              "legend TL: no row is wider than the extended frame allows");
        check(near_px(bottom(c.legend) + m, c.extended.y) && near_px(c.legend.x, c.extended.x + m),
              "legend TL: margin above the extended frame, margin in from its left");
        const CellLayout pn = compute_figure_layout(plain, 700, PH).cells[0];
        check(near_px(pn.frame.h - c.frame.h, m + c.legend.h) && near_px(pn.frame.w, c.frame.w),
              "legend TL: exactly margin + box height, and no width");
        const float title_lh = font_vmetrics("", fs.axes[0].snap2d()->axes_style.title_fontsize)
                                   .line_height;
        check(c.title_y + title_lh * 0.5f <= c.legend.y + 1e-3f,
              "legend TL: the axes title stays above it");

        const FigureSnapshot wide = legend_snapshot(2, LegendAnchor::OutsideTR);
        const CellLayout cw = compute_figure_layout(wide, 1600, PH).cells[0];
        check(cw.legend_slots.size() == 2 && near_px(cw.legend_slots[0].cy, cw.legend_slots[1].cy)
              && cw.legend_slots[1].x > cw.legend_slots[0].x,
              "legend TR: with room, two entries share one row");
        check(near_px(right(cw.legend) + m, right(cw.extended)),
              "legend TR: flush with the extended frame's right edge");
    }
    {
        const FigureSnapshot fs = legend_snapshot(3, LegendAnchor::OutsideBR);
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        const float m = fs.axes[0].snap2d()->legend_opts.margin;
        const float xt_lh = font_vmetrics("", fs.axes[0].snap2d()->axes_style.xtitle_fontsize)
                                .line_height;
        check(near_px(c.legend.y, bottom(c.extended) + m),
              "legend BR: margin below the extended frame");
        check(c.xtitle_y + xt_lh * 0.5f <= c.legend.y + 1e-3f,
              "legend BR: outboard of the x tick labels and x title");
    }

    // ---- Offset: moves the box, reserves nothing.
    {
        FigureSnapshot a = legend_snapshot(3, LegendAnchor::OutsideRT);
        FigureSnapshot b = a;
        b.axes[0].snap2d()->legend_opts.offset_x = 25.0f;
        b.axes[0].snap2d()->legend_opts.offset_y = -15.0f;
        const CellLayout ca = compute_figure_layout(a, PW, PH).cells[0];
        const CellLayout cb = compute_figure_layout(b, PW, PH).cells[0];
        check(same_rect(ca.frame, cb.frame), "legend offset: the frame does not move");
        check(near_px(cb.legend.x - ca.legend.x, 25.0f) && near_px(cb.legend.y - ca.legend.y, -15.0f)
              && near_px(cb.legend_slots[2].x - ca.legend_slots[2].x, 25.0f)
              && near_px(cb.legend_slots[2].cy - ca.legend_slots[2].cy, -15.0f),
              "legend offset: the box and every entry move by exactly the offset");
    }
}

void test_colorbar_anchors() {
    std::printf("\n[placement: colorbar anchors]\n");

    FigureSnapshot plain = legend_snapshot(1, LegendAnchor::OutsideRT);
    plain.axes[0].snap2d()->legend_enabled = false;
    const CellLayout p = compute_figure_layout(plain, PW, PH).cells[0];

    auto with_bar = [&](ColorbarAnchor an) {
        FigureSnapshot fs = plain;
        add_colorbar(*fs.axes[0].snap2d());
        fs.axes[0].snap2d()->colorbar_opts.anchor = an;
        return fs;
    };

    {
        const FigureSnapshot fs = with_bar(ColorbarAnchor::Right);
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        const ColorbarBox& b = c.colorbars[0];
        check(!b.horizontal && near_px(b.rect.x, right(c.extended) + 15.0f)
              && near_px(b.rect.w, 15.0f) && near_px(b.rect.h, c.frame.h),
              "colorbar right: margin 15 out from the extended frame, 15 wide, frame-tall");
        check(b.num_align == HAlign::Left && near_px(b.vmax_y, c.frame.y)
              && near_px(b.vmin_y, bottom(c.frame)),
              "colorbar right: vmax at the top, vmin at the bottom, numbers to its right");
    }
    {
        const FigureSnapshot fs = with_bar(ColorbarAnchor::Left);
        const CellDecorations d = compute_cell_decorations(*fs.axes[0].snap2d());
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        const ColorbarBox& b = c.colorbars[0];
        check(near_px(c.frame.x - p.frame.x, d.colorbar_block),
              "colorbar left: carved from the left by its block");
        check(near_px(right(b.rect) + 15.0f, c.extended.x) && b.num_align == HAlign::Right
              && b.vmin_x < b.rect.x && b.name_x < b.vmin_x,
              "colorbar left: margin left of the extended frame, numbers and name outboard");
    }
    {
        const FigureSnapshot fs = with_bar(ColorbarAnchor::Top);
        const CellDecorations d = compute_cell_decorations(*fs.axes[0].snap2d());
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        const ColorbarBox& b = c.colorbars[0];
        const float lh = font_vmetrics("", fs.axes[0].snap2d()->colorbar_opts.fontsize).line_height;
        check(b.horizontal && near_px(b.rect.x, c.frame.x) && near_px(b.rect.w, c.frame.w)
              && near_px(b.rect.h, 15.0f),
              "colorbar top: horizontal, as wide as the frame, 15 tall");
        check(near_px(bottom(b.rect) + 15.0f, c.extended.y),
              "colorbar top: margin above the extended frame");
        check(near_px(d.colorbars[0].block, 15.0f + 15.0f + kColorbarLabelGap + lh
                                            + kColorbarLabelGap + lh),
              "colorbar top: its numbers and name each cost a line, not a width");
        check(near_px(p.frame.h - c.frame.h, d.colorbar_block) && near_px(p.frame.w, c.frame.w),
              "colorbar top: carved from the height, not the width");
        check(b.num_align == HAlign::Center && near_px(b.vmin_x, c.frame.x)
              && near_px(b.vmax_x, right(c.frame)) && b.vmin_y < b.rect.y && b.name_y < b.vmin_y,
              "colorbar top: vmin at the left end, vmax at the right, name above the numbers");
        check(c.title_y < b.name_y, "colorbar top: the axes title stays above it");
    }
    {
        const FigureSnapshot fs = with_bar(ColorbarAnchor::Bottom);
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        const ColorbarBox& b = c.colorbars[0];
        check(b.horizontal && near_px(b.rect.y, bottom(c.extended) + 15.0f)
              && b.vmin_y > bottom(b.rect) && b.name_y > b.vmin_y,
              "colorbar bottom: margin below the extended frame, numbers then name below it");
    }
    {
        FigureSnapshot fs = with_bar(ColorbarAnchor::Right);
        FigureSnapshot wide = fs;
        wide.axes[0].snap2d()->colorbar_opts.width = 30.0f;
        const CellDecorations d  = compute_cell_decorations(*fs.axes[0].snap2d());
        const CellDecorations dw = compute_cell_decorations(*wide.axes[0].snap2d());
        const CellLayout cw = compute_figure_layout(wide, PW, PH).cells[0];
        check(near_px(cw.colorbars[0].rect.w, 30.0f)
              && near_px(dw.colorbars[0].block - d.colorbars[0].block, 15.0f),
              "colorbar width: the bar is as thick as asked, and the block grows by the difference");

        FigureSnapshot moved = fs;
        moved.axes[0].snap2d()->colorbar_opts.offset_x = -40.0f;
        moved.axes[0].snap2d()->colorbar_opts.offset_y = 12.0f;
        const CellLayout c0 = compute_figure_layout(fs, PW, PH).cells[0];
        const CellLayout cm = compute_figure_layout(moved, PW, PH).cells[0];
        const ColorbarBox& b0 = c0.colorbars[0];
        const ColorbarBox& bm = cm.colorbars[0];
        check(same_rect(c0.frame, cm.frame)
              && near_px(bm.rect.x - b0.rect.x, -40.0f) && near_px(bm.rect.y - b0.rect.y, 12.0f)
              && near_px(bm.vmin_x - b0.vmin_x, -40.0f) && near_px(bm.name_y - b0.name_y, 12.0f),
              "colorbar offset: bar, numbers and name move; the frame does not");
    }

    // ---- A legend and bars on one side: the legend nearest the frame.
    {
        FigureSnapshot fs = legend_snapshot(2, LegendAnchor::OutsideRT);
        add_colorbar(*fs.axes[0].snap2d());
        const CellDecorations d = compute_cell_decorations(*fs.axes[0].snap2d());
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        check(near_px(c.colorbars[0].rect.x, right(c.extended) + d.legend_block + 15.0f)
              && right(c.legend) < c.colorbars[0].rect.x,
              "same side, right: the bars stack outboard of the legend");
    }
    {
        FigureSnapshot fs = legend_snapshot(2, LegendAnchor::OutsideTL);
        add_colorbar(*fs.axes[0].snap2d());
        fs.axes[0].snap2d()->colorbar_opts.anchor = ColorbarAnchor::Top;
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        check(near_px(bottom(c.colorbars[0].rect) + 15.0f, c.legend.y),
              "same side, top: the bars stack above the legend");
    }
    {
        FigureSnapshot fs = legend_snapshot(2, LegendAnchor::OutsideLT);
        add_colorbar(*fs.axes[0].snap2d());
        fs.axes[0].snap2d()->colorbar_opts.anchor = ColorbarAnchor::Right;
        const CellLayout c = compute_figure_layout(fs, PW, PH).cells[0];
        check(near_px(c.colorbars[0].rect.x, right(c.extended) + 15.0f),
              "different sides: a left legend does not push a right bar");
    }
}

// A cell's own legend, colorbars and frame_margin are carved out of that cell
// alone: whatever one subplot does with them, no other subplot's frame moves.
// Only what every cell has -- axis furniture, and the title band -- lines
// frames up across the grid. And a 3D cell takes the same placement rules as a
// 2D one.
void test_placement_grid() {
    std::printf("\n[placement: grids]\n");

    FigureSnapshot base = make_snapshot(2, 2, 4);
    for (auto& fa : base.axes) { fa.snap2d()->xtitle = "x"; fa.snap2d()->ytitle = "y"; }
    add_colorbar(*base.axes[0].snap2d());
    base.axes[0].snap2d()->legend_enabled = true;
    base.axes[0].snap2d()->lines[0].opts.name = "series";
    const FigureLayout ref = compute_figure_layout(base, PW, PH);

    // Every placement cell 0 can choose, one at a time. The one that was
    // reported: changing a colorbar's anchor moved the subplots beside it.
    int moved = 0, tried = 0;
    auto others_unmoved = [&](const FigureSnapshot& fs) {
        const FigureLayout lay = compute_figure_layout(fs, PW, PH);
        ++tried;
        for (std::size_t i = 1; i < lay.cells.size(); ++i)
            if (!same_rect(lay.cells[i].frame, ref.cells[i].frame)) { ++moved; return; }
    };
    for (ColorbarAnchor an : { ColorbarAnchor::Left, ColorbarAnchor::Right,
                               ColorbarAnchor::Top, ColorbarAnchor::Bottom }) {
        FigureSnapshot fs = base;
        fs.axes[0].snap2d()->colorbar_opts.anchor = an;
        others_unmoved(fs);
    }
    for (int a = 0; a <= static_cast<int>(LegendAnchor::OutsideRB); ++a) {
        FigureSnapshot fs = base;
        fs.axes[0].snap2d()->legend_opts.anchor = static_cast<LegendAnchor>(a);
        others_unmoved(fs);
    }
    {
        FigureSnapshot fs = base;
        auto& s = *fs.axes[0].snap2d();
        s.axes_style.frame_margin = 20.0f;
        s.legend_opts.margin = 40.0f;
        s.colorbar_opts.margin = 40.0f;
        s.colorbar_opts.width = 50.0f;
        s.colorbar_opts.offset_x = 30.0f;
        others_unmoved(fs);
    }
    check(moved == 0, "grid: no placement, margin or width on one subplot moves another's frame (" +
                      std::to_string(moved) + " of " + std::to_string(tried) + " did)");

    // What does line frames up is untouched: a long y label in cell 0 still
    // widens column 0's left reservation for cell 2 as well.
    FigureSnapshot wide = make_snapshot(2, 2, 4);
    wide.axes[0].snap2d()->ytitle = "a considerably longer y title";
    wide.axes[0].snap2d()->axes_style.ytitle_fontsize = 30.0f;
    const FigureLayout lw = compute_figure_layout(wide, PW, PH);
    check(near_px(lw.cells[0].frame.x, lw.cells[2].frame.x)
          && lw.cells[2].frame.x - lw.cells[2].cell.x > lw.cells[3].frame.x - lw.cells[3].cell.x + 1.0f,
          "grid: axis furniture still aligns a column");

    // A 3D cell: the bar and the legend placed against its frame.
    FigureSnapshot three = make_snapshot3d(1, 1, 1);
    RenderSnapshot3D& s3 = *three.axes[0].snap3d();
    HeatmapOptions ho; ho.colorbar = true;
    s3.planes.push_back(make_plane(PlaneOrientation::XY, 0.0, std::vector<float>(4, 0.5f), 2, 2,
                                   { 0.0, 1.0 }, { 0.0, 1.0 }, ho));
    s3.planes[0].sheet.lines.push_back(make_snapshot(1, 1, 1).axes[0].snap2d()->lines[0]);
    s3.planes[0].sheet.lines[0].opts.name = "on the plane";
    s3.legend_enabled = true;
    s3.legend_opts.anchor = LegendAnchor::OutsideBL;
    s3.colorbar_opts.anchor = ColorbarAnchor::Left;
    const CellLayout c3 = compute_figure_layout(three, PW, PH).cells[0];
    check(c3.has_legend() && near_px(c3.legend.y, bottom(c3.frame) + s3.legend_opts.margin)
          && near_px(c3.legend.x, c3.frame.x + s3.legend_opts.margin),
          "3D: a bottom legend sits margin below the frame itself");
    check(c3.has_colorbar() && near_px(right(c3.colorbars[0].rect) + 15.0f, c3.frame.x),
          "3D: a left bar sits margin left of the frame itself");
}

// figure_size_for_frame() under every kind of reservation 15.1 adds: the
// width first, then heights that depend on it through a wrapped legend.
void test_frame_size_round_trip_anchors() {
    std::printf("\n[placement: inverse layout]\n");

    struct Variant {
        const char*    name;
        LegendAnchor   legend;
        ColorbarAnchor bar;
        float          frame_margin;
        int            entries;
    };
    const Variant variants[] = {
        { "legend TL (wrapped) + bar bottom", LegendAnchor::OutsideTL, ColorbarAnchor::Bottom, 0.0f, 9 },
        { "legend LB + bar left + margin",    LegendAnchor::OutsideLB, ColorbarAnchor::Left,   8.0f, 3 },
        { "legend BR (wrapped) + bar top",    LegendAnchor::OutsideBR, ColorbarAnchor::Top,    3.0f, 7 },
        { "legend inside + bar right",        LegendAnchor::InsideBR,  ColorbarAnchor::Right,  0.0f, 2 },
    };

    int misses = 0, trips = 0;
    std::string worst;
    for (const Variant& v : variants) {
        for (int slot : { 1, 4 }) {
            FigureSnapshot fs = make_snapshot(2, 2, 4);
            for (auto& fa : fs.axes) {
                RenderSnapshot& s = *fa.snap2d();
                s.title = "T"; s.xtitle = "x"; s.ytitle = "y";
                s.axes_style.frame_margin = v.frame_margin;
            }
            // The pinned slot carries the decorations, and so does slot 2 --
            // which must make no difference to the pinned frame at all.
            for (int idx : { slot - 1, 1 }) {
                RenderSnapshot& s = *fs.axes[idx].snap2d();
                const LinePlot proto = s.lines[0];
                s.lines.clear();
                for (int i = 0; i < v.entries; ++i) {
                    LinePlot lp = proto;
                    lp.opts.name = "entry " + std::to_string(i);
                    s.lines.push_back(std::move(lp));
                }
                s.legend_enabled = true;
                s.legend_opts.anchor = v.legend;
                add_colorbar(s);
                s.colorbar_opts.anchor = v.bar;
            }
            for (auto [fw, fh] : { std::pair{ 300, 200 }, std::pair{ 520, 380 }, std::pair{ 90, 60 } }) {
                ++trips;
                const LayoutSize sz = figure_size_for_frame(fs, slot, static_cast<float>(fw),
                                                            static_cast<float>(fh));
                const int W = static_cast<int>(std::lround(sz.width));
                const int H = static_cast<int>(std::lround(sz.height));
                const FigureLayout lay = compute_figure_layout(fs, W, H);
                const CellLayout& c = lay.cells[slot - 1];
                const float err = std::max(std::fabs(c.frame.w - static_cast<float>(fw)),
                                           std::fabs(c.frame.h - static_cast<float>(fh)));
                if (err > 1.0f) {
                    ++misses;
                    char buf[256];
                    std::snprintf(buf, sizeof buf, "%s slot %d: asked %dx%d, got %.2fx%.2f",
                                  v.name, slot, fw, fh, static_cast<double>(c.frame.w),
                                  static_cast<double>(c.frame.h));
                    worst = buf;
                }
            }
        }
    }
    check(misses == 0, "inverse: every anchored frame round-trips within a pixel (" +
                       std::to_string(misses) + " misses" +
                       (worst.empty() ? std::string() : ", e.g. " + worst) + ")");
    std::printf("  %zu variants x 2 slots x 3 sizes = %d round trips, %d misses\n",
                std::size(variants), trips, misses);
}

// Both outputs draw what the layout placed: a horizontal bar's gradient runs
// vmin on the left in the PNG and in the SVG, and a wrapped legend's entries
// land on the slots the layout gave them.
void test_placement_rendered() {
    std::printf("\n[placement: rendered]\n");

    const int W = 700, H = 520;
    FigureSnapshot fs = legend_snapshot(6, LegendAnchor::OutsideBL);
    add_colorbar(*fs.axes[0].snap2d());
    fs.axes[0].snap2d()->colorbar_opts.anchor = ColorbarAnchor::Top;
    fs.axes[0].snap2d()->colorbar_opts.width  = 20.0f;
    fs.generation = fs.data_generation = 1;
    const CellLayout c = compute_figure_layout(fs, W, H).cells[0];

    const std::string png_path = "placement.png", svg_path = "placement.svg";
    {
        GLContext ctx({ .width = W, .height = H, .title = "layout_test", .visible = false });
        NvgRenderer  nvg(ctx.nvg());
        DataRenderer data;
        export_figure_png(ctx, nvg, data, fs, png_path, W, H, 1);
    }
    export_figure_svg(fs, svg_path, W, H);

    int pw = 0, ph = 0, comp = 0;
    unsigned char* px = stbi_load(png_path.c_str(), &pw, &ph, &comp, 4);
    check(px != nullptr && pw == W && ph == H, "rendered: PNG decoded");
    if (px) {
        const PlotRect& r = c.colorbars[0].rect;
        auto at = [&](float x, float y) {
            const int ix = static_cast<int>(x), iy = static_cast<int>(y);
            return &px[(iy * W + ix) * 4];
        };
        const uint8_t* lut = colormaps::get(Colormap::Viridis);
        auto near_rgb = [](const uint8_t* a, const uint8_t* b) {
            return std::abs(int(a[0]) - int(b[0])) <= 12 && std::abs(int(a[1]) - int(b[1])) <= 12
                && std::abs(int(a[2]) - int(b[2])) <= 12;
        };
        const float cy = r.y + r.h * 0.5f;
        check(near_rgb(at(r.x + 3.0f, cy), &lut[0]) && near_rgb(at(right(r) - 4.0f, cy), &lut[255 * 4]),
              "rendered: the PNG's top bar runs vmin at its left end to vmax at its right");
        stbi_image_free(px);
    }

    std::ifstream f(svg_path);
    const std::string svg((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    check(svg.find("id=\"colorbarGrad0_0\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\"") != std::string::npos,
          "rendered: the SVG's gradient for it runs left to right");
    // Each entry's text sits at its slot, in whichever row the wrap put it.
    bool all_found = true;
    for (const LegendSlot& s : c.legend_slots) {
        std::ostringstream want;
        want << "<text x=\"" << s.x + kLegendSwatchW + kLegendGap << "\"";
        if (svg.find(want.str()) == std::string::npos) all_found = false;
    }
    std::set<float> rows;
    for (const auto& s : c.legend_slots) rows.insert(s.cy);
    check(all_found && rows.size() > 1,
          "rendered: every legend entry's SVG text is at its slot, across " +
          std::to_string(rows.size()) + " rows");
}

}  // namespace lt
