// Where a heatmap is placed -- Axes::heatmap()'s xrange/yrange.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {

// The extent a heatmap is placed at (Axes::heatmap()'s xrange/yrange, step 0
// of v1.0). Every consumer that used to assume the [0,cols] x [0,rows] index
// footprint is checked here against the imshow() case it generalizes: where a
// value is a projection of the old one, the two are compared *exactly*
// through x_at()/y_at() rather than re-derived, which is the check that
// catches a path still doing its own index arithmetic. The pixels themselves
// are covered by the standing byte-identity check on the headless export,
// which imshow() must leave untouched.
void test_heatmap_extent() {
    std::printf("\n[heatmap extent]\n");

    // A ramp along x, the same field the contour tests use, over an extent
    // that shares nothing with the indices: 5 columns across 100..400 (60
    // wide, offset), 5 rows across -2..2 (0.8 tall, offset and signed).
    const sextant::Range xr{ 100.0, 400.0 }, yr{ -2.0, 2.0 };
    auto ramp = [](int, int c) { return float(c); };

    sextant::HeatmapOptions o;
    o.contours = { 2.0 };
    const auto idx = make_heatmap(5, 5, ramp, o);
    const auto ext = make_heatmap(5, 5, ramp, o, xr, yr);

    check(idx.cell_w() == 1.0 && idx.cell_h() == 1.0,
          "imshow: one unit per cell");
    check(ext.cell_w() == 60.0 && ext.cell_h() == 0.8,
          "cell size is the span divided by the count");
    check(ext.x_at(0.0) == 100.0 && ext.x_at(5.0) == 400.0,
          "the range bounds are cell edges, not centres");
    check(std::fabs(ext.x_at(2.5) - 250.0) < 1e-9,
          "a cell centre lands mid-cell");

    // Round trip: col_at() is what the hover test inverts x_at() with, so a
    // disagreement between them is a tooltip naming the wrong cell.
    bool round_trip = true;
    for (double c = 0.0; c <= 5.0; c += 0.25) {
        if (std::fabs(ext.col_at(ext.x_at(c)) - c) > 1e-9) round_trip = false;
        if (std::fabs(ext.row_at(ext.y_at(c)) - c) > 1e-9) round_trip = false;
    }
    check(round_trip, "col_at/row_at invert x_at/y_at over the whole span");

    // --- Contours. The traced geometry must be the index-space trace mapped
    // through the extent, point for point: same count, same order, same
    // topology. Anything else means the trace grew a second opinion about
    // where a cell is.
    {
        const auto a = sextant::trace_contours(idx);
        const auto b = sextant::trace_contours(ext);
        check(a.size() == 1 && b.size() == 1, "both extents trace one line");
        if (a.size() == 1 && b.size() == 1) {
            bool same = a[0].x.size() == b[0].x.size()
                     && a[0].level == b[0].level
                     && a[0].closed == b[0].closed;
            if (same)
                for (std::size_t i = 0; i < a[0].x.size(); ++i) {
                    // Rounded to float on both sides: the trace stores floats,
                    // so the comparison has to happen after the same rounding.
                    const auto ex = static_cast<float>(ext.x_at(a[0].x[i]));
                    const auto ey = static_cast<float>(ext.y_at(a[0].y[i]));
                    if (b[0].x[i] != ex || b[0].y[i] != ey) same = false;
                }
            check(same, "an extent traces the imshow line mapped through it, exactly");
            // And it really did move -- otherwise the check above would pass
            // on a trace that ignored the extent entirely.
            check(b[0].x[0] != a[0].x[0], "  (and the two are not the same numbers)");
        }
    }

    // --- The cache keys on the extent. Same data, same generation, moved
    // image: a stale hit here would leave the contours behind at the old place.
    {
        sextant::ContourCache cache;
        const auto& first = cache.get(0, -1, 0, 7, idx);
        check(first.size() == 1, "cache traces the index-extent heatmap");
        const float x0 = first.empty() || first[0].x.empty() ? 0.0f : first[0].x[0];
        const auto& second = cache.get(0, -1, 0, 7, ext);
        check(second.size() == 1 && !second[0].x.empty() && second[0].x[0] != x0,
              "moving the extent re-traces at the same data generation");
    }

    // --- Auto-scale follows the extent, not the shape.
    {
        const std::vector<sextant::LinePlot>     no_l;
        const std::vector<sextant::ScatterPlot>  no_s;
        const std::vector<sextant::BarPlot>      no_b;
        const std::vector<sextant::ScatterZPlot> no_z;
        const std::vector<sextant::HeatmapPlot>  hs{ ext };
        const sextant::AllPlotData all{ no_l, no_s, no_b, hs, no_z };

        const auto b = sextant::auto_scale(all, 0.0);   // no padding, exact
        check(b.xmin == 100.0 && b.xmax == 400.0 && b.ymin == -2.0 && b.ymax == 2.0,
              "auto_scale bounds the extent, not [0,cols] x [0,rows]");

        // A reversed range covers the same ground; the bounds must not come
        // out inverted (which would collapse the axis).
        const std::vector<sextant::HeatmapPlot> rs{
            make_heatmap(5, 5, ramp, {}, sextant::Range{ 400.0, 100.0 },
                                         sextant::Range{ 2.0, -2.0 }) };
        const sextant::AllPlotData all_rev{ no_l, no_s, no_b, rs, no_z };
        const auto rb = sextant::auto_scale(all_rev, 0.0);
        check(rb.xmin == 100.0 && rb.xmax == 400.0 && rb.ymin == -2.0 && rb.ymax == 2.0,
              "a reversed extent bounds the same interval");
    }

    // --- Hover hit-testing. The cell named must be the cell drawn, which
    // with an extent is no longer the cell the raw data coordinate indexes.
    {
        sextant::RenderSnapshot snap;
        // Values distinct per cell, so the reported text identifies exactly
        // one of them; origin defaults to "lower", i.e. row 0 at ymin.
        snap.heatmaps.push_back(make_heatmap(5, 5,
            [](int r, int c) { return float(r * 10 + c); }, {}, xr, yr));
        snap.xmin = 100.0; snap.xmax = 400.0;
        snap.ymin = -2.0;  snap.ymax = 2.0;

        // 300 x 200 px frame at the origin: one cell is 60 x 40 px.
        const sextant::CoordTransform tr{ 100.0, 400.0, -2.0, 2.0,
                                          0.0f, 0.0f, 300.0f, 200.0f, 300.0f, 200.0f };

        // Cursor in the middle of column 3, row 1 counted from the bottom --
        // for origin="lower" that is storage row 1: data (310, -1.0).
        const auto h = sextant::find_hint(snap, tr, tr.to_px(310.0), tr.to_py(-1.0));
        check(h.has_value(), "a cursor inside the extent hits the heatmap");
        if (h) check(h->text == "row=1, col=3, value=13",
                     "  and names the cell under it (row 1, col 3, value 13)");

        // Just outside the extent on each side. The old test was
        // 0 <= x < cols, which every one of these points satisfies.
        check(!sextant::find_hint(snap, tr, tr.to_px(99.0), tr.to_py(0.0)),
              "left of the extent misses");
        check(!sextant::find_hint(snap, tr, tr.to_px(401.0), tr.to_py(0.0)),
              "right of the extent misses");
        check(!sextant::find_hint(snap, tr, tr.to_px(250.0), tr.to_py(-2.5)),
              "below the extent misses");
        check(!sextant::find_hint(snap, tr, tr.to_px(250.0), tr.to_py(2.5)),
              "above the extent misses");
        // Inside the *index* numbers, outside the extent -- the one case that
        // used to hit and now must not.
        check(!sextant::find_hint(snap, tr, tr.to_px(3.0), tr.to_py(1.0)),
              "a point at index coordinates is not inside a moved heatmap");
    }

    // --- The public ingest: imshow() is heatmap() at the index extent, and a
    // range that cannot be divided into cells is refused.
    {
        const std::vector<float> data(25, 0.0f);
        auto fig = sextant::Figure::create({ .width = 400, .height = 300 });
        bool threw_degenerate = false, threw_nonfinite = false, imshow_ok = true;
        try { fig->axes()->heatmap(data, 5, 5, { 1.0, 1.0 }, { 0.0, 5.0 }); }
        catch (const std::invalid_argument&) { threw_degenerate = true; }
        try {
            fig->axes()->heatmap(data, 5, 5, { 0.0, 5.0 },
                                 { 0.0, std::numeric_limits<double>::quiet_NaN() });
        } catch (const std::invalid_argument&) { threw_nonfinite = true; }
        try { fig->axes()->imshow(data, 5, 5); }
        catch (const std::exception&) { imshow_ok = false; }
        check(threw_degenerate, "a zero-width range throws");
        check(threw_nonfinite, "a non-finite range throws");
        check(imshow_ok, "imshow needs no range of its own");
    }
}

// A row-major matrix mirrored on either axis -- what a reversed Range means
// for the picture, and so the reference the render checks compare against.
std::vector<float> mirror_of(const std::vector<float>& m, int rows, int cols,
                             bool flip_x, bool flip_y) {
    std::vector<float> out(m.size());
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const int sr = flip_y ? rows - 1 - r : r;
            const int sc = flip_x ? cols - 1 - c : c;
            out[static_cast<std::size_t>(r) * cols + c] =
                m[static_cast<std::size_t>(sr) * cols + sc];
        }
    return out;
}

// What the extent actually draws, in both outputs. The checks above are all
// on derived numbers; this is on the rendered bytes, which is the only place
// a render path still carrying its own index arithmetic would show up.
//
// The trick that makes exact comparison possible is that an extent is an
// affine change of variables: a heatmap over xrange x yrange, *viewed*
// through limits equal to that extent, must land on precisely the pixels the
// index-space one lands on when viewed through [0,cols] x [0,rows]. With tick
// labels overridden away the two figures then have nothing else that could
// differ, so the whole file is comparable byte for byte -- and at
// supersample=1, so no box filter can hide a one-pixel disagreement.
void test_heatmap_extent_render() {
    std::printf("\n[heatmap extent: rendered]\n");

    constexpr int W = 300, H = 220;
    constexpr int R = 6, C = 8;

    // Asymmetric in both directions and distinct per cell, so a mirror or a
    // transpose shows up rather than cancelling itself out.
    std::vector<float> img(static_cast<std::size_t>(R) * C);
    for (int r = 0; r < R; ++r)
        for (int c = 0; c < C; ++c)
            img[static_cast<std::size_t>(r) * C + c] =
                static_cast<float>(r * C + c) / static_cast<float>(R * C - 1);

    auto snapshot_of = [&](const std::vector<float>& data, sextant::Range xr, sextant::Range yr) {
        sextant::RenderSnapshot rs;
        rs.heatmaps.push_back(sextant::HeatmapPlot{
            data, R, C, xr, yr, sextant::HeatmapOptions{} });
        // Limits *equal* to the extent: the image fills the frame either way,
        // which is what makes the two renders comparable at all.
        rs.xmin = std::min(xr.lo, xr.hi); rs.xmax = std::max(xr.lo, xr.hi);
        rs.ymin = std::min(yr.lo, yr.hi); rs.ymax = std::max(yr.lo, yr.hi);
        rs.xlim_auto = rs.ylim_auto = false;
        // No ticks at all -- otherwise the two figures label different
        // numbers, the label widths differ, and so do the frames.
        rs.xticks_override = std::vector<sextant::Tick>{};
        rs.yticks_override = std::vector<sextant::Tick>{};

        sextant::FigureSnapshot fs;
        fs.axes.push_back({ {1, 1, 1}, std::move(rs) });
        fs.generation = 1;
        fs.data_generation = 1;
        return fs;
    };

    // A GL context per render: DataRenderer's heatmap texture cache is keyed
    // on (axes, plot, data_generation), and these figures deliberately share a
    // generation while differing in data, so a reused renderer would answer
    // the second render out of the first one's texture.
    auto render = [&](const sextant::FigureSnapshot& fs, const std::string& stem) {
        {
            sextant::GLContext ctx({.width = W, .height = H,
                                    .title = "layout_test", .visible = false});
            sextant::NvgRenderer  nvg(ctx.nvg());
            sextant::DataRenderer data;
            sextant::export_figure_png(ctx, nvg, data, fs, stem + ".png", W, H, 1);
        }
        sextant::export_figure_svg(fs, stem + ".svg", W, H);
    };

    auto same_file = [](const std::string& a, const std::string& b) {
        std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
        if (!fa.good() || !fb.good()) return false;
        const std::string sa((std::istreambuf_iterator<char>(fa)), std::istreambuf_iterator<char>());
        const std::string sb((std::istreambuf_iterator<char>(fb)), std::istreambuf_iterator<char>());
        return !sa.empty() && sa == sb;
    };

    // Differing pixels, and how they are distributed: a whole picture drawn
    // wrong and a single seam of rounding are both "not identical", and the
    // mirror checks below have to tell them apart.
    // `residual` is what is left after discarding the single worst pixel
    // column and the single worst pixel row -- i.e. after allowing exactly
    // one seam in each direction. Zero means every difference lies on one,
    // which is the shape a rounded cell boundary has and no real mis-mapping
    // does.
    struct PngDiff { int px = -1, worst = 0, residual = -1; };
    auto png_diff = [](const std::string& a, const std::string& b) {
        int aw = 0, ah = 0, bw = 0, bh = 0, comp = 0;
        unsigned char* pa = stbi_load((a + ".png").c_str(), &aw, &ah, &comp, 4);
        unsigned char* pb = stbi_load((b + ".png").c_str(), &bw, &bh, &comp, 4);
        PngDiff d;
        if (pa && pb && aw == bw && ah == bh) {
            d.px = 0;
            std::vector<int> col(aw, 0), row(ah, 0);
            std::vector<std::pair<int, int>> hits;
            for (int y = 0; y < ah; ++y)
                for (int x = 0; x < aw; ++x) {
                    const std::size_t i = (static_cast<std::size_t>(y) * aw + x) * 4;
                    int m = 0;
                    for (int k = 0; k < 4; ++k)
                        m = std::max(m, std::abs(int(pa[i + k]) - int(pb[i + k])));
                    if (m) {
                        ++d.px; d.worst = std::max(d.worst, m);
                        ++col[x]; ++row[y]; hits.emplace_back(x, y);
                    }
                }
            const int cx = aw ? static_cast<int>(std::max_element(col.begin(), col.end()) - col.begin()) : -1;
            const int ry = ah ? static_cast<int>(std::max_element(row.begin(), row.end()) - row.begin()) : -1;
            d.residual = 0;
            for (const auto& [x, y] : hits)
                if (x != cx && y != ry) ++d.residual;
        }
        if (pa) stbi_image_free(pa);
        if (pb) stbi_image_free(pb);
        return d;
    };

    // --- The index extent (what imshow() builds) against an arbitrary one.
    render(snapshot_of(img, { 0.0, C }, { 0.0, R }), "heat_index");
    render(snapshot_of(img, { 100.0, 400.0 }, { -2.0, 2.0 }), "heat_extent");
    check(same_file("heat_index.png", "heat_extent.png"),
          "an extent renders the index-space pixels exactly (PNG)");
    check(same_file("heat_index.svg", "heat_extent.svg"),
          "an extent renders the index-space pixels exactly (SVG)");

    // --- Reversed ranges mirror the image. The reference is the same render
    // with the data itself mirrored, which is what a reversed range means --
    // and it is what keeps the two paths honest about it, since GL mirrors by
    // folding its quad's corners while SVG, which can only place an upright
    // box, has to mirror the pixels instead.
    //
    // Not byte-identity, for the raster only: a cell boundary is generally
    // not on a pixel boundary (here the frame is ~253 px across 8 columns),
    // and mirroring the quad moves each boundary to the other side of the
    // pixel centre it straddles. GL_NEAREST then resolves that one pixel to
    // the neighbouring cell, leaving a one-pixel seam -- which can be a whole
    // colour step wide, so its magnitude proves nothing and its *shape* is
    // the assertion instead. Anything actually drawn wrong -- transposed, off
    // by a cell, not mirrored at all -- moves whole cells, which no single
    // row and column can absorb.
    auto mirrored = [&](const std::string& a, const std::string& b,
                        const std::string& what) {
        const PngDiff d = png_diff(a, b);
        check(d.px == 0 || d.residual == 0, what + " (PNG)");
        if (d.px) std::printf("    %s: %d px differ, %d off the seam, worst delta %d\n",
                              what.c_str(), d.px, d.residual, d.worst);
        check(same_file(a + ".svg", b + ".svg"), what + " (SVG)");
    };

    render(snapshot_of(img, { 400.0, 100.0 }, { -2.0, 2.0 }), "heat_revx");
    render(snapshot_of(mirror_of(img, R, C, true, false), { 100.0, 400.0 }, { -2.0, 2.0 }), "heat_refx");
    mirrored("heat_revx", "heat_refx", "a reversed xrange mirrors left-right");

    render(snapshot_of(img, { 100.0, 400.0 }, { 2.0, -2.0 }), "heat_revy");
    render(snapshot_of(mirror_of(img, R, C, false, true), { 100.0, 400.0 }, { -2.0, 2.0 }), "heat_refy");
    mirrored("heat_revy", "heat_refy", "a reversed yrange mirrors top-bottom");

    render(snapshot_of(img, { 400.0, 100.0 }, { 2.0, -2.0 }), "heat_revxy");
    render(snapshot_of(mirror_of(img, R, C, true, true), { 100.0, 400.0 }, { -2.0, 2.0 }), "heat_refxy");
    mirrored("heat_revxy", "heat_refxy", "both reversed mirrors both ways");

    // Every comparison above would also pass on a renderer that ignored the
    // heatmap and drew an empty frame, so: the picture is not blank, and the
    // mirrored one is a different picture from the upright one.
    const PngDiff d = png_diff("heat_index", "heat_revx");
    check(d.px > 500 && d.worst > 32,
          "the mirrored render is a visibly different picture from the upright one");
}

}  // namespace lt
