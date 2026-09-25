// Plane2D: ingest, the raster, the quad, and decoration hoisting. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    // ===========================================================================
    // Plane2D: the 2D kinds in a 3D scene
    // ===========================================================================

    void test_plane2d_ingest() {
        std::printf("\n[3D: Plane2D ingest and placement]\n");

        using namespace sextant;

        auto fig = Figure::create({.width = 320, .height = 260});
        auto ax = fig->add_subplot3d(1, 1, 1);

        auto pl = ax->plane(PlaneOrientation::XY, 0.5);
        check(pl != nullptr, "plane: an axes hands one back");
        check(pl->orientation() == PlaneOrientation::XY && pl->offset() == 0.5,
              "plane: with the orientation and offset it was asked for");

        const std::vector<double> d(12, 0.5);
        pl->heatmap(d, 3, 4, {400.0, 700.0}, {-1.0, 1.0});

        // cla() drops the plane's data, not its placement.
        pl->set_offset(0.25).set_alpha(2.0f);
        check(pl->offset() == 0.25, "plane: set_offset moves it");
        pl->cla();
        check(pl->offset() == 0.25 && pl->orientation() == PlaneOrientation::XY,
              "plane: cla() clears the data and leaves the placement alone");

        // Rejections; every message names Plane2D.
        auto threw_with = [](auto&& fn, const char* needle) {
            try { fn(); } catch (const std::exception& e) {
                return std::string(e.what()).find(needle) != std::string::npos;
            }
            return false;
        };
        check(threw_with([&] {
                             ax->plane(PlaneOrientation::XY,
                                       std::numeric_limits<double>::infinity());
                         },
                         "Axes3D::plane"),
              "plane: a non-finite offset throws");
        check(threw_with([&] { pl->heatmap(d, 0, 4, {0, 1}, {0, 1}); },
                         "Plane2D::heatmap"),
              "plane: and the shared heatmap ingest validates exactly as the 2D one does");
        check(threw_with([&] { pl->heatmap(d, 3, 4, {1.0, 1.0}, {0, 1}); },
                         "Plane2D::heatmap"),
              "plane: including a degenerate extent");
        // Contours are accepted on the 2D terms: sorted, de-duplicated, non-finite
        // rejected.
        {
            HeatmapOptions ho;
            ho.contours = {0.75, 0.25, 0.75};
            pl->heatmap(d, 3, 4, {0, 1}, {0, 1}, ho);
            check(threw_with([&] {
                                 HeatmapOptions bad;
                                 bad.contours = {std::numeric_limits<double>::quiet_NaN()};
                                 pl->heatmap(d, 3, 4, {0, 1}, {0, 1}, bad);
                             },
                             "Plane2D::heatmap"),
                  "plane: a non-finite contour level throws, as it does on an axes");
            pl->cla(); // back to empty, for the count below
        }

        // imshow is heatmap() at the index extent: two planes, one of each, must be
        // placed identically in the file.
        ax->plane(PlaneOrientation::YZ, 0.0)->imshow(d, 3, 4);
        ax->plane(PlaneOrientation::YZ, 0.0)
                ->heatmap(d, 3, 4, {0.0, 4.0}, {0.0, 3.0});

        // Planes and bar3d coexist and survive the snapshot.
        const std::vector<double> u{0, 1}, v{0, 1}, h{1, 2, 3, 4};
        ax->bar3d(PlaneOrientation::XY, u, v, h);

        fig->savefig("plane_ingest.svg");
        std::ifstream f("plane_ingest.svg", std::ios::binary);
        const std::string svg((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
        auto count_of = [](const std::string& hay, const std::string& needle) {
            int n = 0;
            for (std::size_t i = hay.find(needle); i != std::string::npos;
                 i = hay.find(needle, i + 1))
                ++n;
            return n;
        };
        check(count_of(svg, "<image") == 2,
              "plane: the two planes with data reach the output and the cleared one does not");
        check(count_of(svg, "<polygon") > 0,
              "plane: alongside the bar3d faces -- an axes holds both kinds at once");
        const std::size_t m0 = svg.find("matrix(");
        const std::size_t m1 = svg.find("matrix(", m0 + 1);
        check(m0 != std::string::npos && m1 != std::string::npos &&
              svg.substr(m0, svg.find(')', m0) - m0) ==
              svg.substr(m1, svg.find(')', m1) - m1),
              "plane: imshow() lands on exactly the extent the indices give, to the pixel");
    }

    void test_plane_raster() {
        std::printf("\n[3D: the plane's reference raster -- a plane is a little screen]\n");

        using namespace sextant;

        Transform3D tf;
        tf.xmin = -4.0;
        tf.xmax = 4.0; // span 8
        tf.ymin = 0.0;
        tf.ymax = 2.0; // span 2
        tf.zmin = -1.0;
        tf.zmax = 3.0; // span 4
        tf.aspect = {1.0, 0.5, 0.75};

        // ---- The raster's shape comes from the box (the spans here differ from
        // the aspect).
        {
            const PlaneRaster r = plane_raster(tf, PlaneOrientation::XY, 0.0, 800);
            check(r.w == 800 && r.h == 400,
                  "raster: sized from the plane's box-space extent (1.0 : 0.5)");
            check(r.normal_axis == 2, "raster: XY is normal to z");

            const PlaneRaster ryz = plane_raster(tf, PlaneOrientation::YZ, 0.0, 800);
            // YZ spans y then z: sides 0.5 and 0.75, so z is the longer one.
            check(ryz.w == 533 && ryz.h == 800 && ryz.normal_axis == 0,
                  "raster: and the orientation picks which two sides those are");
            const PlaneRaster rzx = plane_raster(tf, PlaneOrientation::ZX, 0.0, 800);
            check(rzx.w == 600 && rzx.h == 800 && rzx.normal_axis == 1,
                  "raster: ZX likewise, u = z and v = x");
        }

        // A cube gives a square raster whatever the data spans.
        {
            Transform3D cube = tf;
            cube.aspect = {1.0, 1.0, 1.0};
            const PlaneRaster r = plane_raster(cube, PlaneOrientation::XY, 0.0, 640);
            check(r.w == 640 && r.h == 640,
                  "raster: a cube's face is square however unequal the data spans");
        }

        // Never below one texel.
        {
            Transform3D flat = tf;
            flat.aspect = {1.0, 1e-6, 1.0};
            const PlaneRaster r = plane_raster(flat, PlaneOrientation::XY, 0.0, 700);
            check(r.w == 700 && r.h == 1, "raster: a degenerate side floors at one texel");
            Transform3D zero = tf;
            zero.aspect = {0.0, 0.0, 0.0};
            const PlaneRaster rz = plane_raster(zero, PlaneOrientation::XY, 0.0, 700);
            check(rz.w == 700 && rz.h == 700, "raster: and an all-zero box does not divide by it");
            const PlaneRaster r0 = plane_raster(tf, PlaneOrientation::XY, 0.0, 0);
            check(r0.w >= 1 && r0.h >= 1, "raster: a zero cap still yields a raster");
        }

        // ---- The transform maps the parent's limits (not the data bounds) onto
        // the raster.
        {
            const PlaneRaster r = plane_raster(tf, PlaneOrientation::XY, 1.5, 800);
            check(r.tr.xmin == -4.0 && r.tr.xmax == 4.0 &&
                  r.tr.ymin == 0.0 && r.tr.ymax == 2.0,
                  "raster: the transform carries the parent's limits on u and v");
            check(r.tr.px == 0.0f && r.tr.py == 0.0f &&
                  r.tr.pw == 800.0f && r.tr.ph == 400.0f &&
                  r.tr.win_w == 800.0f && r.tr.win_h == 400.0f,
                  "raster: onto the whole raster, which is its own little window");
            // Expected corners computed independently.
            check(r.tr.to_px(-4.0) == 0.0f && r.tr.to_px(4.0) == 800.0f,
                  "raster: u_min is the left edge and u_max the right");
            check(r.tr.to_py(0.0) == 400.0f && r.tr.to_py(2.0) == 0.0f,
                  "raster: v_min is the *bottom*, as on any 2D axes");
        }

        // ---- The quad spans the box face at the offset. A rendered framebuffer's
        // t = 0 is its bottom row, so v_min is t = 0 (unlike plane_heatmap_quad()).
        {
            const PlaneRaster r = plane_raster(tf, PlaneOrientation::XY, 1.5, 800);
            check(r.p[0].x == -4.0 && r.p[0].y == 0.0 && r.p[0].z == 1.5 &&
                  r.p[2].x == 4.0 && r.p[2].y == 2.0 && r.p[2].z == 1.5,
                  "raster: the quad spans the box's cross-section at the offset");
            check(r.p[1].x == 4.0 && r.p[1].y == 0.0 &&
                  r.p[3].x == -4.0 && r.p[3].y == 2.0,
                  "raster: with the ring running u first, then v");
            check(r.uv[0][0] == 0.0f && r.uv[0][1] == 0.0f &&
                  r.uv[2][0] == 1.0f && r.uv[2][1] == 1.0f,
                  "raster: v_min carries t = 0, which a rendered framebuffer needs");

            // For each corner, the transform's pixel must be the one the texture
            // coordinate names (catches an inverted t).
            for (int i = 0; i < 4; ++i) {
                const double u = (i == 1 || i == 2) ? tf.xmax : tf.xmin;
                const double v = (i >= 2) ? tf.ymax : tf.ymin;
                const float sx = r.tr.to_px(u) / r.tr.pw;
                // Pixel y down, texture t up: flip this one.
                const float sy = 1.0f - r.tr.to_py(v) / r.tr.ph;
                check(std::fabs(sx - r.uv[i][0]) < 1e-6f &&
                      std::fabs(sy - r.uv[i][1]) < 1e-6f,
                      "raster: corner " + std::to_string(i) +
                      " lands on the texel its uv names");
            }
        }

        // ---- A reversed limit mirrors the raster and the quad together.
        {
            Transform3D rev = tf;
            rev.xmax = -4.0;
            rev.xmin = 4.0;
            const PlaneRaster r = plane_raster(rev, PlaneOrientation::XY, 0.0, 800);
            check(r.tr.to_px(4.0) == 0.0f && r.tr.to_px(-4.0) == 800.0f,
                  "raster: a reversed limit mirrors the raster");
            check(r.p[0].x == 4.0 && r.p[1].x == -4.0,
                  "raster: and the quad's corner 0 follows the limit, not the smaller value");
            // Same pairing check.
            for (int i = 0; i < 4; ++i) {
                const double u = (i == 1 || i == 2) ? rev.xmax : rev.xmin;
                const float sx = r.tr.to_px(u) / r.tr.pw;
                check(std::fabs(sx - r.uv[i][0]) < 1e-6f,
                      "raster: reversed, corner " + std::to_string(i) + " still matches its uv");
            }
        }

        // ---- The camera isn't an input (orbits don't invalidate the raster).
        {
            const PlaneRaster a = plane_raster(tf, PlaneOrientation::XY, 0.0, 800);
            const PlaneRaster b = plane_raster(tf, PlaneOrientation::XY, 0.0, 800);
            check(a.w == b.w && a.h == b.h &&
                  a.tr.xmin == b.tr.xmin && a.tr.ymax == b.tr.ymax,
                  "raster: same inputs, same raster -- nothing hidden and no camera in it");
        }
    }

    void test_plane2d_geometry() {
        std::printf("\n[3D: the plane quad, and what feeds the parent's limits]\n");

        using namespace sextant;

        // ---- The heatmap quad: corners are the extent on the spanned axes and
        // the offset on the third.
        HeatmapPlot hp;
        hp.rows = 2;
        hp.cols = 2;
        hp.xrange = {400.0, 700.0};
        hp.yrange = {-1.0, 1.0};
        hp.data = std::vector<float>{0.0f, 0.25f, 0.75f, 1.0f};

        const PlaneQuad q = plane_heatmap_quad(hp, PlaneOrientation::XY, 0.5);
        check(q.p[0].x == 400.0 && q.p[0].y == -1.0 && q.p[0].z == 0.5 &&
              q.p[2].x == 700.0 && q.p[2].y == 1.0 && q.p[2].z == 0.5,
              "plane: a corner is the extent's own coordinates, and the offset on the third axis");
        check(q.normal_axis == 2, "plane: XY is normal to z");
        // Texture coordinates match the 2D quad.
        check(q.uv[0][0] == 0.0f && q.uv[0][1] == 1.0f &&
              q.uv[2][0] == 1.0f && q.uv[2][1] == 0.0f,
              "plane: with the 2D quad's own texture coordinates on them");

        const PlaneQuad qyz = plane_heatmap_quad(hp, PlaneOrientation::YZ, 7.0);
        check(qyz.p[0].y == 400.0 && qyz.p[0].z == -1.0 && qyz.p[0].x == 7.0 &&
              qyz.normal_axis == 0,
              "plane: the orientation rotates which axis is which, and nothing else");
        const PlaneQuad qzx = plane_heatmap_quad(hp, PlaneOrientation::ZX, 7.0);
        check(qzx.p[0].z == 400.0 && qzx.p[0].x == -1.0 && qzx.p[0].y == 7.0 &&
              qzx.normal_axis == 1,
              "plane: and ZX is the third rotation of the same map");

        // ---- Pixels: row 0 is the yrange.hi edge regardless of `origin`.
        const uint8_t* lut = colormaps::get(hp.opts.cmap);
        auto lut_rgba = [&](float t) {
            return lut + static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 255.0f) * 4;
        };
        HeatmapPlot hl = hp;
        hl.opts.origin = "lower";
        HeatmapPlot hu = hp;
        hu.opts.origin = "upper";
        const std::vector<uint8_t> rl = plane_heatmap_rgba(hl);
        const std::vector<uint8_t> ru = plane_heatmap_rgba(hu);
        check(rl.size() == 16 && ru.size() == 16, "plane: one RGBA quad per cell");
        check(std::memcmp(&rl[0], lut_rgba(0.75f), 4) == 0,
              "plane: origin=lower puts data row 1 at the top of the image, i.e. at yrange.hi");
        check(std::memcmp(&ru[0], lut_rgba(0.0f), 4) == 0,
              "plane: origin=upper puts data row 0 there instead");
        check(std::memcmp(&rl[0], &ru[8], 4) == 0 && std::memcmp(&rl[8], &ru[0], 4) == 0,
              "plane: so the two differ by exactly a row reversal");

        // ---- Auto-scale: plane data feeds the parent's limits; the offset lands
        // on the third axis.
        std::vector<PlaneSnapshot> planes{
            make_plane(PlaneOrientation::XY, 0.5, std::vector<float>(hp.data.begin(),
                                                                     hp.data.end()),
                       2, 2, hp.xrange, hp.yrange)
        };
        const DataBounds3D b = auto_scale3d({}, planes, {}, {}, {}, {}, 0.0);
        check(b.xmin == 400.0 && b.xmax == 700.0 && b.ymin == -1.0 && b.ymax == 1.0,
              "plane: its extent is the parent's limits on the two axes it spans");
        // The third axis gets one value, centred by the degenerate-interval rule.
        check(b.zmin == 0.0 && b.zmax == 1.0,
              "plane: and the third axis is centred on the offset the plane sits at");

        std::vector<PlaneSnapshot> rotated{
            make_plane(PlaneOrientation::YZ, 7.0, std::vector<float>(hp.data.begin(),
                                                                     hp.data.end()),
                       2, 2, hp.xrange, hp.yrange)
        };
        const DataBounds3D r = auto_scale3d({}, rotated, {}, {}, {}, {}, 0.0);
        check(r.ymin == 400.0 && r.ymax == 700.0 && r.zmin == -1.0 && r.zmax == 1.0 &&
              r.xmin == 6.5 && r.xmax == 7.5,
              "plane: through the same Axis3Map a bar's u/v/h go through");

        // An empty plane doesn't pull the third axis to its offset.
        std::vector<PlaneSnapshot> empty{PlaneSnapshot{}};
        empty[0].offset = 99.0;
        const DataBounds3D e = auto_scale3d({}, empty, {}, {}, {}, {}, 0.0);
        check(e.zmin == 0.0 && e.zmax == 1.0 && e.xmin == 0.0 && e.xmax == 1.0,
              "plane: an empty plane contributes nothing at all, offset included");

        // The union with a bar3d object.
        Bar3DPlot bar;
        bar.u = std::vector<double>{800.0};
        bar.v = std::vector<double>{0.0};
        bar.heights = std::vector<double>{4.0};
        bar.u_width = bar.v_width = 0.0;
        const DataBounds3D both = auto_scale3d({bar}, planes, {}, {}, {}, {}, 0.0);
        check(both.xmin == 400.0 && both.xmax == 800.0 && both.zmin == 0.0 && both.zmax == 4.0,
              "plane: bars and planes fill the same three intervals");
    }

    void test_plane2d_decoration_hoist() {
        std::printf("\n[3D: decoration hoisting]\n");

        using namespace sextant;

        // A tall frame, so the box is width-limited and carving a colorbar must
        // shrink it.
        constexpr int W = 320, H = 420;

        HeatmapOptions cb;
        cb.colorbar = true;
        cb.vmin = 0.0f;
        cb.vmax = 100.0f;

        FigureSnapshot bare = make_snapshot3d(1, 1, 1);
        FigureSnapshot with = make_snapshot3d(1, 1, 1);
        with.axes[0].snap3d()->planes.push_back(
            make_plane(PlaneOrientation::XY, 0.0, std::vector<float>(4, 0.5f), 2, 2,
                       {0.0, 1.0}, {0.0, 1.0}, cb));

        const CellDecorations dec = compute_cell_decorations(*with.axes[0].snap3d());
        check(dec.colorbars.size() == 1 && dec.colorbars[0].vmax == 100.0f,
              "hoist: a colorbar asked for by a heatmap *on a plane* belongs to the cell");
        check(compute_cell_decorations(*bare.axes[0].snap3d()).colorbars.empty(),
              "hoist: and an axes with no plane still asks for nothing");
        check(dec.legend_entries.empty(),
              "hoist: no legend yet -- nothing that can be on a plane produces an entry");

        const FigureLayout lb = compute_figure_layout(bare, W, H);
        const FigureLayout lw = compute_figure_layout(with, W, H);
        check(lw.cells[0].has_colorbar(), "hoist: the layout carves a box for it");
        check(std::fabs((lb.cells[0].frame.w - lw.cells[0].frame.w) - dec.colorbar_block) < 1e-3f,
              "hoist: out of the cell, by exactly the width the decoration measured");
        check(lw.cells[0].colorbars[0].rect.x >= lw.cells[0].frame.x + lw.cells[0].frame.w,
              "hoist: beside the frame, not in the scene");

        // The box fits what's left after carving.
        auto box_width = [](const FigureLayout& l) {
            const Projector3D& p = l.cells[0].box3d->proj;
            float x0 = 1e9f, x1 = -1e9f;
            for (int i = 0; i < 8; ++i) {
                const Px3 q = p.project_box({
                    (i & 1) ? 0.5 : -0.5, (i & 2) ? 0.5 : -0.5,
                    (i & 4) ? 0.5 : -0.5
                });
                x0 = std::min(x0, q.x);
                x1 = std::max(x1, q.x);
            }
            return x1 - x0;
        };
        check(box_width(lw) < box_width(lb),
              "hoist: so the box shrinks to make room, exactly as a 2D frame does");

        // The inverse subtracts what the forward direction added.
        const LayoutSize need = figure_size_for_frame(with, 1, lw.cells[0].frame.w,
                                                      lw.cells[0].frame.h);
        check(std::fabs(need.width - static_cast<float>(W)) < 1e-2f &&
              std::fabs(need.height - static_cast<float>(H)) < 1e-2f,
              "hoist: and figure_size_for_frame() inverts the carve for a 3D cell too");

        // Both outputs draw it beside the frame.
        export_figure_svg(with, "plane_colorbar.svg", W, H);
        std::ifstream f("plane_colorbar.svg", std::ios::binary);
        const std::string svg((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
        std::ostringstream box;
        box << "<rect x=\"" << lw.cells[0].colorbars[0].rect.x
                << "\" y=\"" << lw.cells[0].colorbars[0].rect.y;
        check(svg.find(box.str()) != std::string::npos,
              "hoist: and the SVG puts the bar in the box the layout carved");
    }

    // A 3D colorbar's styling is the axes' own (planes' colorbar_opts are unused).
    void test_axes3d_colorbar_style() {
        std::printf("\n[3D: the colorbar's styling is the axes']\n");

        using namespace sextant;

        constexpr int W = 420, H = 420;

        HeatmapOptions cb;
        cb.colorbar = true;
        cb.vmin = 0.0f;
        cb.vmax = 100.0f;

        auto build = [&](ColorbarOptions style) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s3 = fs.axes[0].snap3d();
            s3->planes.push_back(
                make_plane(PlaneOrientation::XY, 0.0, std::vector<float>(4, 0.5f), 2, 2,
                           {0.0, 1.0}, {0.0, 1.0}, cb));
            s3->colorbar_opts = style;
            return fs;
        };

        ColorbarOptions big;
        big.fontsize = 28.0f;

        const FigureSnapshot fs_def = build({});
        const FigureSnapshot fs_big = build(big);

        const CellDecorations d_def = compute_cell_decorations(*fs_def.axes[0].snap3d());
        const CellDecorations d_big = compute_cell_decorations(*fs_big.axes[0].snap3d());

        // A bigger font gives a wider block: the styling reached the measurement.
        check(d_big.colorbars[0].block > d_def.colorbars[0].block,
              "style: a larger colorbar font widens the block measured for it");
        check(compute_figure_layout(fs_big, W, H).cells[0].frame.w
              < compute_figure_layout(fs_def, W, H).cells[0].frame.w,
              "style: and the frame gives up the difference");

        // A plane's own colorbar_opts is inert.
        FigureSnapshot fs_plane = build({});
        fs_plane.axes[0].snap3d()->planes[0].sheet.colorbar_opts = big;
        check(compute_cell_decorations(*fs_plane.axes[0].snap3d()).colorbars[0].block
              == d_def.colorbars[0].block,
              "style: a plane's own colorbar_opts no longer styles the cell's bar");

        // Through the public API into a file.
        auto fig = Figure::create({.width = W, .height = H});
        auto ax3 = fig->add_subplot3d(1, 1, 1);
        ax3->plane(PlaneOrientation::XY, 0.0)
                ->heatmap(std::vector<double>(4, 0.5), 2, 2, {0.0, 1.0}, {0.0, 1.0}, cb);
        ax3->set_colorbar_style(big);
        fig->savefig("axes3d_colorbar_style.svg");
        std::ifstream sf("axes3d_colorbar_style.svg", std::ios::binary);
        const std::string svg((std::istreambuf_iterator<char>(sf)),
                              std::istreambuf_iterator<char>());
        check(svg.find("font-size=\"28\"") != std::string::npos,
              "style: Axes3D::set_colorbar_style() reaches the written file");

        RenderSnapshot3D lane = *fs_def.axes[0].snap3d();
        AxesEdit3D e;
        e.colorbar_opts = big;
        e.legend_enabled = true;
        apply_axes3d_edit(lane, e);
        check(lane.colorbar_opts.fontsize == 28.0f && lane.legend_enabled,
              "style: and AxesEdit3D carries it, alongside the legend switch");

        std::printf("  block %.2f px at 10 px font, %.2f px at 28\n",
                    static_cast<double>(d_def.colorbars[0].block),
                    static_cast<double>(d_big.colorbars[0].block));
    }
} // namespace lt
