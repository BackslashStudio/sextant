// Plane2D rendered: one plane, crossing planes, every 2D kind on one. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    void test_plane2d_render() {
        std::printf("\n[3D: a plane, rendered]\n");

        using namespace sextant;

        constexpr int W = 440, H = 380;
        constexpr int R = 3, C = 4;

        // Distinct values, so each pixel identifies its cell.
        std::vector<float> data(R * C);
        for (int i = 0; i < R * C; ++i)
            data[static_cast<std::size_t>(i)] = static_cast<float>(i) / (R * C - 1);

        // A bare box, so only the plane and three axis lines are drawn.
        auto scene = [&](PlaneOrientation orient, double offset, const char* origin,
                         Projection mode) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.projection = mode;
            s->camera.fov = 60.0;
            s->box_style.panes = false;
            s->grid_enabled = false;
            s->xticks_override = std::vector<Tick>{};
            s->yticks_override = std::vector<Tick>{};
            s->zticks_override = std::vector<Tick>{};
            s->xmin = 0;
            s->xmax = 1;
            s->xlim_auto = false;
            s->ymin = 0;
            s->ymax = 1;
            s->ylim_auto = false;
            s->zmin = 0;
            s->zmax = 1;
            s->zlim_auto = false;
            HeatmapOptions ho;
            ho.origin = origin;
            s->planes.push_back(make_plane(orient, offset, data, R, C,
                                           {0.05, 0.95}, {0.05, 0.95}, ho));
            return fs;
        };

        auto render = [&](const FigureSnapshot& fs, const std::string& stem) {
            {
                GLContext ctx({
                    .width = W, .height = H,
                    .title = "layout_test", .visible = false
                });
                NvgRenderer nvg(ctx.nvg());
                DataRenderer data_r;
                export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1);
            }
            export_figure_svg(fs, stem + ".svg", W, H);
        };
        auto read_file = [](const std::string& p) {
            std::ifstream f(p, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        };
        auto count_of = [](const std::string& hay, const std::string& needle) {
            int n = 0;
            for (std::size_t i = hay.find(needle); i != std::string::npos;
                 i = hay.find(needle, i + 1))
                ++n;
            return n;
        };

        const uint8_t* lut = colormaps::get(HeatmapOptions{}.cmap);

        // Each cell centre, projected on the CPU, must carry that cell's color in
        // the GPU output: position, texture orientation and matrix all at once.
        auto check_cells = [&](const FigureSnapshot& fs, const std::string& stem,
                               const char* origin, const char* what) {
            render(fs, stem);
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            const PlaneSnapshot& pl = fs.axes[0].snap3d()->planes[0];
            const HeatmapPlot& hp = pl.sheet.heatmaps[0];
            const Axis3Map m = axis_map(pl.orient);
            const bool flip = (std::string(origin) == "lower");

            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load((stem + ".png").c_str(), &w, &h, &comp, 4);
            int hits = 0, misses = 0;
            for (int r = 0; r < R && px; ++r) {
                const int band = flip ? r : (R - 1 - r);
                const double v = hp.y_at(band + 0.5);
                for (int c = 0; c < C; ++c) {
                    const double u = hp.x_at(c + 0.5);
                    double coord[3];
                    coord[m.u] = u;
                    coord[m.v] = v;
                    coord[m.h] = pl.offset;
                    const Px3 q = proj.project(coord[0], coord[1], coord[2]);
                    const int xi = static_cast<int>(q.x), yi = static_cast<int>(q.y);
                    if (xi < 0 || yi < 0 || xi >= w || yi >= h) {
                        ++misses;
                        continue;
                    }
                    const unsigned char* p = px + (yi * w + xi) * 4;
                    const unsigned char* e =
                            lut + static_cast<int>(
                                std::clamp(data[static_cast<std::size_t>(r) * C + c], 0.0f, 1.0f)
                                * 255.0f) * 4;
                    if (p[0] == e[0] && p[1] == e[1] && p[2] == e[2]) ++hits;
                    else ++misses;
                }
            }
            if (px) stbi_image_free(px);
            check(hits == R * C && misses == 0, what);
            return lay;
        };

        const FigureLayout lay_lo =
                check_cells(scene(PlaneOrientation::XY, 0.5, "lower", Projection::Orthographic),
                            "plane_xy_lower", "lower",
                            "plane: every cell centre carries its own colour, where the projector puts it");
        check_cells(scene(PlaneOrientation::XY, 0.5, "upper", Projection::Orthographic),
                    "plane_xy_upper", "upper",
                    "plane: and origin=upper turns the image over rather than moving it");
        check_cells(scene(PlaneOrientation::YZ, 0.5, "lower", Projection::Orthographic),
                    "plane_yz", "lower",
                    "plane: on a YZ plane, through the same map");
        check_cells(scene(PlaneOrientation::XY, 0.5, "lower", Projection::Perspective),
                    "plane_persp", "lower",
                    "plane: and under a perspective camera, where GL does the divide itself");

        // ---- SVG. Orthographic: the writer emits the plan's exact matrix(...).
        {
            const FigureSnapshot fs = scene(PlaneOrientation::XY, 0.5, "lower",
                                            Projection::Orthographic);
            const Projector3D& proj = lay_lo.cells[0].box3d->proj;
            const std::vector<PlanePlanItem> plan =
                    plan_planes3d(proj, fs.axes[0].snap3d()->planes);
            check(plan.size() == 1 && plan[0].form == PlanePlanItem::Form::Image && plan[0].polys.empty(),
                  "plane SVG: an orthographic plane is one affine <image>");

            // Exact: the unit square maps onto the three corners, and the fourth
            // lands where a parallelogram requires.
            const PlaneQuad q = plane_heatmap_quad(
                fs.axes[0].snap3d()->planes[0].sheet.heatmaps[0],
                PlaneOrientation::XY, 0.5);
            auto at = [&](float u, float v) {
                return std::pair<float, float>{
                    plan[0].matrix[0] * u + plan[0].matrix[2] * v + plan[0].matrix[4],
                    plan[0].matrix[1] * u + plan[0].matrix[3] * v + plan[0].matrix[5]
                };
            };
            // (image u, image v, which quad corner it must land on)
            const int corner_of_uv[4][3] = {
                {0, 0, 3}, {1, 0, 2},
                {0, 1, 0}, {1, 1, 1}
            };
            bool corners_land = true;
            for (const auto& t: corner_of_uv) {
                const Px3 want = proj.project(q.p[t[2]].x, q.p[t[2]].y, q.p[t[2]].z);
                const auto got = at(static_cast<float>(t[0]), static_cast<float>(t[1]));
                if (!near_px(got.first, want.x, 1e-2f) || !near_px(got.second, want.y, 1e-2f))
                    corners_land = false;
            }
            check(corners_land,
                  "plane SVG: whose matrix maps all four image corners onto the projector's "
                  "own pixels -- the fourth is what makes the affine map exact rather than close");

            std::ostringstream mat;
            mat << "matrix(" << plan[0].matrix[0] << ' ' << plan[0].matrix[1] << ' '
                    << plan[0].matrix[2] << ' ' << plan[0].matrix[3] << ' '
                    << plan[0].matrix[4] << ' ' << plan[0].matrix[5] << ')';
            const std::string svg = read_file("plane_xy_lower.svg");
            check(svg.find(mat.str()) != std::string::npos,
                  "plane SVG: and the writer carries the plan's own numbers, not a projection of its own");
            check(count_of(svg, "<!-- sextant:") == 0,
                  "plane SVG: with nothing to warn about");
        }

        // Perspective: one polygon per cell (panes off, so every <polygon> is a
        // cell).
        {
            const FigureSnapshot fs = scene(PlaneOrientation::XY, 0.5, "lower",
                                            Projection::Perspective);
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const std::vector<PlanePlanItem> plan =
                    plan_planes3d(lay.cells[0].box3d->proj, fs.axes[0].snap3d()->planes);
            check(plan.size() == 1 && plan[0].form == PlanePlanItem::Form::Polys &&
                  plan[0].polys.size() == static_cast<std::size_t>(R * C),
                  "plane SVG: a perspective plane is one polygon per cell instead");
            const std::string svg = read_file("plane_persp.svg");
            check(count_of(svg, "<polygon") == R * C,
                  "plane SVG: and every one of them reaches the file");
            check(count_of(svg, "<image") == 0,
                  "plane SVG: with no <image>, which an affine transform could not have placed");
        }

        // Above the cell cap: the affine fallback, with a warning in the file.
        {
            const int N = 160; // 25,600 cells, past kPlane3DCellCap
            check(static_cast<std::size_t>(N) * N > kPlane3DCellCap,
                  "plane SVG: (the fallback case is actually over the cap)");
            FigureSnapshot fs = scene(PlaneOrientation::XY, 0.5, "lower",
                                      Projection::Perspective);
            fs.axes[0].snap3d()->planes[0] =
                    make_plane(PlaneOrientation::XY, 0.5,
                               std::vector<float>(static_cast<std::size_t>(N) * N, 0.5f), N, N,
                               {0.05, 0.95}, {0.05, 0.95});
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const std::vector<PlanePlanItem> plan =
                    plan_planes3d(lay.cells[0].box3d->proj, fs.axes[0].snap3d()->planes);
            check(plan.size() == 1 && plan[0].form == PlanePlanItem::Form::Image && !plan[0].warning.empty(),
                  "plane SVG: past the cap a perspective plane falls back to the affine image");
            export_figure_svg(fs, "plane_cap.svg", W, H);
            const std::string svg = read_file("plane_cap.svg");
            check(count_of(svg, "<!-- sextant:") == 1 && count_of(svg, "<image") == 1,
                  "plane SVG: with the reason recorded in the file itself");
        }

        // ---- Emission order: a plane clear of a bar grid is painted after it
        // when in front and before it when behind. Checked against independently
        // computed distances, not the plan's own key.
        {
            FigureSnapshot fs = scene(PlaneOrientation::XY, 0.05, "lower",
                                      Projection::Orthographic);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            // A second plane at the other end of z.
            s->planes.push_back(make_plane(PlaneOrientation::XY, 0.95, data, R, C,
                                           {0.05, 0.95}, {0.05, 0.95}));
            Bar3DPlot bar;
            bar.u = std::vector<double>{0.5};
            bar.v = std::vector<double>{0.5};
            bar.heights = std::vector<double>{0.5};
            bar.u_width = bar.v_width = 0.6;
            bar.opts.shading = 0.0f;
            s->bars3d.push_back(std::move(bar));

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            export_figure_svg(fs, "plane_order.svg", W, H);
            const std::string svg = read_file("plane_order.svg");

            // Distances computed independently of the plan.
            const double d0 = plane_distance(s->planes[0], proj);
            const double d1 = plane_distance(s->planes[1], proj);
            const double db = bar3d_plot_distance(s->bars3d[0], proj);
            check((d0 > db) != (d1 > db),
                  "plane order: (the scene really does put one plane in front of the bar and one behind)");

            // Each plane's position in the file, found by its matrix.
            const std::vector<PlanePlanItem> plan = plan_planes3d(proj, s->planes);
            auto pos_of = [&](std::size_t which) {
                for (const PlanePlanItem& it: plan) {
                    if (it.plane != which) continue;
                    std::ostringstream mat;
                    mat << "matrix(" << it.matrix[0] << ' ' << it.matrix[1] << ' '
                            << it.matrix[2] << ' ' << it.matrix[3] << ' '
                            << it.matrix[4] << ' ' << it.matrix[5] << ')';
                    return svg.find(mat.str());
                }
                return std::string::npos;
            };
            const std::size_t p0 = pos_of(0), p1 = pos_of(1);
            const std::size_t bars_at = svg.find("<polygon");
            check(p0 != std::string::npos && p1 != std::string::npos &&
                  bars_at != std::string::npos,
                  "plane order: both planes and the bar reach the file");
            // A plane straddled by the grid has bar faces emitted on both sides of
            // it (Newell splits them), which no whole-object order can produce.
            const std::size_t last_bar = svg.rfind("<polygon");
            auto straddled = [&](std::size_t at) {
                return bars_at < at && at < last_bar;
            };
            std::printf("  plane order: first bar at %zu, planes at %zu and %zu, last bar at %zu\n",
                        bars_at, p0, p1, last_bar);
            bool newell_svg = true;
            if (const char* env1 = std::getenv("SEXTANT_NEWELL"))
                newell_svg = std::atoi(env1) != 0;
            if (newell_svg)
                check(last_bar != std::string::npos && (straddled(p0) || straddled(p1)),
                      "plane order: a plane the grid straddles has bar faces both before and "
                      "after it, which a whole-object order can never produce");
            else
                check(!straddled(p0) && !straddled(p1),
                      "plane order (control): the whole-object order puts each plane entirely "
                      "before or entirely after the bars, which is the flaw itself");
        }

        // Translucency: a bar behind a translucent plane shows through (an opaque
        // plane hides it).
        {
            auto stacked = [&](float alpha) {
                FigureSnapshot fs = scene(PlaneOrientation::XY, 0.9, "lower",
                                          Projection::Orthographic);
                fs.axes[0].snap3d()->planes[0].opts.alpha = alpha;
                Bar3DPlot bar;
                bar.u = std::vector<double>{0.5};
                bar.v = std::vector<double>{0.5};
                bar.heights = std::vector<double>{0.6};
                bar.u_width = bar.v_width = 0.7;
                bar.opts.color = {1.0f, 0.0f, 0.0f, 1.0f};
                bar.opts.shading = 0.0f;
                fs.axes[0].snap3d()->bars3d.push_back(std::move(bar));
                return fs;
            };
            auto red_pixels = [&](const std::string& stem) {
                int w = 0, h = 0, comp = 0;
                unsigned char* px = stbi_load((stem + ".png").c_str(), &w, &h, &comp, 4);
                int n = 0;
                if (px) {
                    for (int i = 0; i < w * h; ++i) {
                        const unsigned char* p = px + i * 4;
                        if (p[0] > 150 && p[1] < 90 && p[2] < 90) ++n;
                    }
                    stbi_image_free(px);
                }
                return n;
            };
            render(stacked(1.0f), "plane_opaque");
            render(stacked(0.4f), "plane_glass");
            const int opaque = red_pixels("plane_opaque");
            const int glass = red_pixels("plane_glass");
            check(glass > opaque,
                  "plane alpha: a bar under a translucent plane shows through; under an opaque one it does not");
            std::printf("  bar pixels through the plane: %d opaque, %d translucent\n",
                        opaque, glass);
        }

        // A dashed line on a plane dashes (plane strokes use the 2D stroke shader).
        // Compared with the solid rendering of the same line.
        {
            auto with_line = [&](LineStyle style) {
                FigureSnapshot fs = scene(PlaneOrientation::XY, 0.5, "lower",
                                          Projection::Orthographic);
                RenderSnapshot3D* s = fs.axes[0].snap3d();
                s->planes[0].sheet.heatmaps.clear(); // the line, and nothing else
                LinePlot lp;
                lp.x = std::vector<double>{0.05, 0.95};
                lp.y = std::vector<double>{0.05, 0.95};
                lp.opts.color = {1.0f, 0.0f, 0.0f, 1.0f};
                lp.opts.linewidth = 3.0f;
                lp.opts.linestyle = style;
                s->planes[0].sheet.lines.push_back(std::move(lp));
                return fs;
            };
            auto red_of = [&](const std::string& stem) {
                int w = 0, h = 0, comp = 0, n = 0;
                unsigned char* px = stbi_load((stem + ".png").c_str(), &w, &h, &comp, 4);
                if (px) {
                    for (int i = 0; i < w * h; ++i)
                        if (px[i * 4] > 150 && px[i * 4 + 1] < 90 && px[i * 4 + 2] < 90) ++n;
                    stbi_image_free(px);
                }
                return n;
            };
            render(with_line(LineStyle::Solid), "plane_line_solid");
            render(with_line(LineStyle::Dashed), "plane_line_dashed");
            const int solid = red_of("plane_line_solid");
            const int dashed = red_of("plane_line_dashed");
            check(solid > 0 && dashed > 0 && dashed < solid * 3 / 4,
                  "plane: a dashed line on a plane draws dashed, not solid (step 7a)");
            std::printf("  line pixels on the plane: %d solid, %d dashed\n", solid, dashed);
        }

        // ---- A translucent primitive inside a plane: the raster must accumulate
        // coverage (glBlendFuncSeparate). At 50% alpha the correct raster has
        // coverage 0.5; the plain blend would give 0.25.
        {
            FigureSnapshot fs = scene(PlaneOrientation::XY, 0.5, "lower",
                                      Projection::Orthographic);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->planes[0].sheet.heatmaps.clear();
            BarPlot bp;
            bp.centers = std::vector<double>{0.5};
            bp.heights = std::vector<double>{0.9};
            bp.bar_width = 0.8;
            bp.opts.color = {1.0f, 0.0f, 0.0f, 0.5f}; // half-transparent red
            bp.opts.linewidth = 0.0f;
            s->planes[0].sheet.bars.push_back(std::move(bp));
            render(fs, "plane_halfbar");

            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load("plane_halfbar.png", &w, &h, &comp, 4);
            check(px != nullptr, "plane blend: the half-transparent bar rendered");
            if (px) {
                // The background, read from the picture.
                const unsigned char* bg = px; // top-left, outside the box
                // Under the bar, red rises and green falls by the bar's coverage.
                const double want = 0.5 * 0.0 + 0.5 * bg[1]; // green, correct
                const double bad = 0.5 * 0.0 + 0.75 * bg[1]; // green, if alpha were a^2
                int good_px = 0, bad_px = 0;
                for (int i = 0; i < w * h; ++i) {
                    const unsigned char* p = px + i * 4;
                    if (p[0] <= p[1] || p[0] < 150) continue; // not under the bar
                    if (std::fabs(p[1] - want) <= 2.0) ++good_px;
                    if (std::fabs(p[1] - bad) <= 2.0) ++bad_px;
                }
                check(good_px > 200,
                      "plane blend: a half-transparent bar on a plane composites at half coverage");
                // Antialiased edges give a few stray pixels; a wrong blend would
                // swap the counts outright.
                check(bad_px * 100 < good_px,
                      "plane blend: and not at the quarter a straight-alpha raster would give");
                std::printf("  half-bar green: want %.0f (%d px), a^2 would be %.0f (%d px)\n",
                            want, good_px, bad, bad_px);
                stbi_image_free(px);
            }
        }

        // ---- Two planes, two rasters: 2D caches must key on the plane, or two
        // planes with a line at index 0 share one entry.
        {
            FigureSnapshot fs = scene(PlaneOrientation::XY, 0.25, "lower",
                                      Projection::Orthographic);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->planes[0].sheet.heatmaps.clear();
            // Same point count, different lengths: a plane-less cache key would
            // reuse the first buffer (color is a uniform, so it wouldn't reveal
            // this).
            auto add_line = [](PlaneSnapshot& p, Color c, double y, double x1) {
                LinePlot lp;
                lp.x = std::vector<double>{0.05, x1};
                lp.y = std::vector<double>{y, y};
                lp.opts.color = c;
                lp.opts.linewidth = 4.0f;
                p.sheet.lines.push_back(std::move(lp));
            };
            add_line(s->planes[0], {1.0f, 0.0f, 0.0f, 1.0f}, 0.25, 0.95); // red, long
            s->planes.push_back(s->planes[0]);
            s->planes[1].offset = 0.75;
            s->planes[1].sheet.lines.clear();
            add_line(s->planes[1], {0.0f, 0.0f, 1.0f, 1.0f}, 0.75, 0.35); // blue, short
            render(fs, "plane_two_rasters");

            int w = 0, h = 0, comp = 0, red = 0, blue = 0;
            unsigned char* px = stbi_load("plane_two_rasters.png", &w, &h, &comp, 4);
            if (px) {
                for (int i = 0; i < w * h; ++i) {
                    const unsigned char* p = px + i * 4;
                    if (p[0] > 150 && p[1] < 90 && p[2] < 90) ++red;
                    if (p[2] > 150 && p[0] < 90 && p[1] < 90) ++blue;
                }
                stbi_image_free(px);
            }
            // The short line must come out short.
            check(red > 50 && blue > 20 && blue * 2 < red,
                  "plane: two planes render their own geometry, not the first one's twice");
            std::printf("  two-plane line pixels: %d red (long), %d blue (short)\n", red, blue);
        }
    }

    // A plane carrying the vector-shaped kinds, built directly.
    sextant::PlaneSnapshot make_kinds_plane(sextant::PlaneOrientation o, double offset) {
        using namespace sextant;
        PlaneSnapshot p;
        p.orient = o;
        p.offset = offset;

        BarPlot bp;
        bp.centers = std::vector<double>{1.0, 2.0, 3.0};
        bp.heights = std::vector<double>{1.0, 2.0, 1.5};
        bp.bar_width = 0.8;
        bp.opts.color = Color::Cyan;
        bp.opts.name = "bars";
        bp.opts.linewidth = 1.0f;
        // Error boxes add fills between strokes (the batch list must alternate),
        // plus capped whiskers.
        bp.err.y_box_lo = std::vector<double>{0.2, 0.2, 0.2};
        bp.err.y_cap_lo = std::vector<double>{0.3, 0.3, 0.3};
        bp.opts.errorbar.linewidth = 1.0f;
        bp.opts.errorbar.capsize = 6.0f;
        p.sheet.bars.push_back(std::move(bp));

        LinePlot lp;
        lp.x = std::vector<double>{0.5, 1.5, 2.5, 3.5};
        lp.y = std::vector<double>{0.5, 1.5, 1.0, 2.0};
        lp.opts.color = Color::Blue;
        lp.opts.linewidth = 2.0f;
        lp.opts.name = "line";
        p.sheet.lines.push_back(std::move(lp));

        ScatterPlot sp;
        sp.x = std::vector<double>{1.0, 2.0};
        sp.y = std::vector<double>{1.0, 2.0};
        sp.opts.color = Color::Red;
        sp.opts.size = 20.0f;
        sp.opts.marker = MarkerStyle::Square;
        sp.opts.name = "dots";
        p.sheet.scatters.push_back(std::move(sp));

        ScatterZPlot zp;
        zp.x = std::vector<double>{1.5};
        zp.y = std::vector<double>{1.5};
        zp.z = std::vector<double>{0.5};
        zp.opts.size = 14.0f;
        p.sheet.scatter_z.push_back(std::move(zp));

        return p;
    }

    // -------------------------------------------------------------------------
    // Two translucent planes that cross
    // -------------------------------------------------------------------------
    // Each is in front on one side of the crossing line, so no whole-object order
    // is right. The oracle uses the hit points' extent along eye_dir() (not the
    // shader's own depth), so a comparator sign error can't agree with it.
    void test_plane2d_composite() {
        std::printf("\n[3D: crossing translucent planes]\n");

        using namespace sextant;

        constexpr int W = 420, H = 360;
        constexpr float kAlpha = 0.5f;
        const Range ext{0.02, 0.98};

        // Two constant colors at the ends of the colormap (default vmin/vmax 0..1),
        // so expected pixels are arithmetic.
        auto scene = [&](bool visible, bool with_bar) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.projection = Projection::Orthographic;
            s->box_style.panes = false;
            s->grid_enabled = false;
            s->xticks_override = std::vector<Tick>{};
            s->yticks_override = std::vector<Tick>{};
            s->zticks_override = std::vector<Tick>{};
            s->xmin = 0;
            s->xmax = 1;
            s->xlim_auto = false;
            s->ymin = 0;
            s->ymax = 1;
            s->ylim_auto = false;
            s->zmin = 0;
            s->zmax = 1;
            s->zlim_auto = false;
            s->planes.push_back(make_plane(PlaneOrientation::ZX, 0.5,
                                           std::vector<float>(4, 0.0f), 2, 2, ext, ext));
            s->planes.push_back(make_plane(PlaneOrientation::YZ, 0.5,
                                           std::vector<float>(4, 1.0f), 2, 2, ext, ext));
            for (PlaneSnapshot& p: s->planes) {
                p.opts.alpha = kAlpha;
                p.opts.visible = visible;
            }
            if (with_bar) {
                Bar3DPlot bar;
                bar.u = std::vector<double>{0.5};
                bar.v = std::vector<double>{0.5};
                bar.heights = std::vector<double>{0.6};
                bar.u_width = bar.v_width = 0.7;
                bar.opts.color = {1.0f, 0.0f, 0.0f, 1.0f};
                bar.opts.shading = 0.0f; // one colour on every face, so the
                bar.opts.edges = false; // expected pixel is the colour itself
                s->bars3d.push_back(std::move(bar));
            }
            return fs;
        };

        auto render = [&](const FigureSnapshot& fs, const std::string& stem) {
            GLContext ctx({
                .width = W, .height = H,
                .title = "layout_test", .visible = false
            });
            NvgRenderer nvg(ctx.nvg());
            DataRenderer data_r;
            export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1);
        };
        struct Img {
            unsigned char* px = nullptr;
            int w = 0, h = 0;
            ~Img() { if (px) stbi_image_free(px); }
            const unsigned char* at(int x, int y) const { return px + (y * w + x) * 4; }
        };
        auto load = [](const std::string& stem, Img& im) {
            int comp = 0;
            im.px = stbi_load((stem + ".png").c_str(), &im.w, &im.h, &comp, 4);
            return im.px != nullptr;
        };

        const FigureSnapshot fs = scene(true, false);
        render(fs, "plane_cross");
        render(scene(false, false), "plane_cross_bg");

        Img got, bg;
        if (!load("plane_cross", got) || !load("plane_cross_bg", bg)) {
            check(false, "plane composite: both renders reach the disk");
            return;
        }

        const FigureLayout lay = compute_figure_layout(fs, W, H);
        const Projector3D& proj = lay.cells[0].box3d->proj;
        const RenderSnapshot3D* s = fs.axes[0].snap3d();
        const uint8_t* lut = colormaps::get(Colormap::Viridis);
        const unsigned char* col[2] = {lut, lut + 255 * 4}; // plane 0, plane 1

        // Avoid the heatmap's edge (linear filtering into the margin) and the
        // crossing line itself.
        auto inked = [&](double t) { return t > ext.lo + 0.04 && t < ext.hi - 0.04; };
        auto box_of = [&](const PlaneSnapshot& p, double u, double v) {
            const Vec3 d = plane_point(p.orient, u, v, p.offset);
            return proj.transform().to_box(d.x, d.y, d.z);
        };

        const PlotRect& fr = lay.cells[0].frame;
        int near0 = 0, near1 = 0, ok = 0, bad = 0;
        for (int y = static_cast<int>(fr.y) + 1; y < static_cast<int>(fr.y + fr.h) - 1; ++y) {
            for (int x = static_cast<int>(fr.x) + 1; x < static_cast<int>(fr.x + fr.w) - 1; ++x) {
                const float fx = static_cast<float>(x) + 0.5f;
                const float fy = static_cast<float>(y) + 0.5f;
                double u[2], v[2];
                float d[2];
                bool hit = true;
                for (int i = 0; i < 2 && hit; ++i)
                    hit = plane_ray_hit(proj, s->planes[i].orient, s->planes[i].offset,
                                        fx, fy, u[i], v[i], d[i])
                          && inked(u[i]) && inked(v[i]);
                if (!hit) continue;

                const Vec3 h0 = box_of(s->planes[0], u[0], v[0]);
                const Vec3 h1 = box_of(s->planes[1], u[1], v[1]);
                const double e0 = dot(h0, proj.eye_dir()), e1 = dot(h1, proj.eye_dir());
                if (std::fabs(e0 - e1) < 0.03) continue; // astride the crossing
                const int n = (e0 > e1) ? 0 : 1; // larger extent = nearer

                const unsigned char* cn = col[n];
                const unsigned char* cf = col[1 - n];
                const unsigned char* b = bg.at(x, y);
                const unsigned char* g = got.at(x, y);
                bool good = true;
                for (int k = 0; k < 3; ++k) {
                    const double want = kAlpha * cn[k]
                                        + (1.0 - kAlpha) * (kAlpha * cf[k]
                                                            + (1.0 - kAlpha) * b[k]);
                    if (std::fabs(want - g[k]) > 4.0) good = false;
                }
                if (good) ++ok;
                else ++bad;
                if (n == 0) ++near0;
                else ++near1;
            }
        }
        std::printf("  crossing: %d px with plane 0 in front, %d with plane 1; %d match, %d do not\n",
                    near0, near1, ok, bad);
        check(near0 > 500 && near1 > 500,
              "plane composite: the crossing really does put each plane in front over a large region");
        // The box's edges are drawn over the planes, so a few samples are lines. A
        // whole-object order would get thousands wrong.
        check(bad * 50 < ok,
              "plane composite: both sides of the intersection blend in their own order");

        // The vector path is still whole-object; record the size of the mismatch.
        const std::vector<PlanePlanItem> plan = plan_planes3d(proj, s->planes);
        check(plan.size() == 2, "plane composite: an orthographic plane is still one SVG image");
        const std::size_t svg_on_top = plan.empty() ? 0 : plan.back().plane;
        check((svg_on_top == 0 ? near1 : near0) > 500,
              "plane composite: the SVG's one global order is wrong over a whole region the raster now gets right");

        // The composite writes each sample's own depth, so an opaque bar in front
        // still hides the planes (classified by Px3::depth).
        {
            render(scene(true, true), "plane_cross_bar");
            Img barred;
            if (load("plane_cross_bar", barred)) {
                const double lo[3] = {0.15, 0.15, 0.0};
                const double hi[3] = {0.85, 0.85, 0.6};
                int pure = 0, tinted = 0;
                for (int y = static_cast<int>(fr.y) + 1;
                     y < static_cast<int>(fr.y + fr.h) - 1; ++y) {
                    for (int x = static_cast<int>(fr.x) + 1;
                         x < static_cast<int>(fr.x + fr.w) - 1; ++x) {
                        const float fx = static_cast<float>(x) + 0.5f;
                        const float fy = static_cast<float>(y) + 0.5f;
                        float bd = 0.0f;
                        if (!box_ray_hit(proj, lo, hi, fx, fy, bd)) continue;
                        double u[2], v[2];
                        float d[2];
                        bool hit = true;
                        for (int i = 0; i < 2 && hit; ++i)
                            hit = plane_ray_hit(proj, s->planes[i].orient, s->planes[i].offset,
                                                fx, fy, u[i], v[i], d[i])
                                  && inked(u[i]) && inked(v[i]);
                        if (!hit) continue;
                        // Both planes behind the bar's near face: nothing shows.
                        if (d[0] < bd + 0.002f || d[1] < bd + 0.002f) continue;
                        const unsigned char* g = barred.at(x, y);
                        if (g[0] > 250 && g[1] < 5 && g[2] < 5) ++pure;
                        else ++tinted;
                    }
                }
                std::printf("  bar in front of both planes: %d px pure, %d tinted\n", pure, tinted);
                check(pure > 500 && tinted * 50 < pure,
                      "plane composite: a sample the depth buffer rejects contributes nothing");
            } else {
                check(false, "plane composite: the bar render reaches the disk");
            }
        }
    }

    void test_plane2d_kinds() {
        std::printf("\n[3D: the rest of the 2D kinds, on a plane]\n");

        using namespace sextant;

        const PlaneSnapshot pl = make_kinds_plane(PlaneOrientation::XY, 0.0);
        const PlaneGeometry g = plane_geometry(pl);

        // ---- The batch list is the 2D painter order (fills and strokes
        // alternate; coplanar primitives can't be depth-sorted).
        using Kind = PlaneGeometry::Batch::Kind;
        std::vector<Kind> kinds;
        for (const auto& b: g.batches) kinds.push_back(b.kind);
        check(kinds == std::vector<Kind>({
                  Kind::Tri, Kind::Seg, Kind::Tri,
                  Kind::Seg, Kind::Marker
              }),
              "kinds: the batches run fill, stroke, fill, stroke, markers -- the 2D order");

        // Contiguous and complete: every primitive in exactly one batch.
        std::size_t tri_end = 0, seg_end = 0, mark_end = 0;
        bool contiguous = true;
        for (const auto& b: g.batches) {
            std::size_t& end = (b.kind == Kind::Tri)
                                   ? tri_end
                                   : (b.kind == Kind::Seg)
                                         ? seg_end
                                         : mark_end;
            if (b.begin != end || b.end < b.begin) contiguous = false;
            end = b.end;
        }
        check(contiguous && tri_end == g.tris.size() && seg_end == g.segs.size()
              && mark_end == g.markers.size(),
              "kinds: and they tile the three lists exactly, so nothing is built but undrawn");

        // Expected counts. Bars: 3 bodies = 6 triangles; error boxes: 3 more = 6.
        // Strokes: 3 bar outlines x 4 sides, 3 line segments, 3 box outlines x 4
        // sides, and 3 whiskers with 2 caps each. Markers: 2 scatter + 1 scatter_z.
        check(g.tris.size() == 12, "kinds: a bar body and an error box are two triangles each");
        check(g.segs.size() == 12 + 3 + 12 + 9,
              "kinds: outlines, line segments, box outlines and capped whiskers are all strokes");
        check(g.markers.size() == 3, "kinds: scatter and scatter_z are markers");

        // Every primitive lies on the plane's offset.
        bool on_plane = true;
        for (const auto& t: g.tris) for (const Vec3& p: t.p) on_plane = on_plane && p.z == 0.0;
        for (const auto& s: g.segs) on_plane = on_plane && s.a.z == 0.0 && s.b.z == 0.0;
        for (const auto& m: g.markers) on_plane = on_plane && m.p.z == 0.0;
        check(on_plane, "kinds: every primitive sits exactly on the plane it was put on");

        const PlaneGeometry gyz = plane_geometry(make_kinds_plane(PlaneOrientation::YZ, 4.0));
        bool rotated = !gyz.tris.empty();
        for (const auto& t: gyz.tris) for (const Vec3& p: t.p) rotated = rotated && p.x == 4.0;
        check(rotated, "kinds: through the same Axis3Map, so YZ puts the offset on x");

        // ---- Strokes and markers are both drawn into the plane, so both scale
        // with it: the marker must scale by the plane's projected scale (a ratio
        // check, not just "it changed").
        {
            // The whole scene, so the camera can give the plane's scale directly.
            auto scene_at = [&](double fov, double offset) {
                FigureSnapshot fs = make_snapshot3d(1, 1, 1);
                RenderSnapshot3D* s = fs.axes[0].snap3d();
                s->camera.projection = Projection::Perspective;
                s->camera.fov = fov;
                s->xmin = 0;
                s->xmax = 4;
                s->xlim_auto = false;
                s->ymin = 0;
                s->ymax = 4;
                s->ylim_auto = false;
                s->zmin = -1;
                s->zmax = 1;
                s->zlim_auto = false;
                s->planes.push_back(make_kinds_plane(PlaneOrientation::XY, offset));
                return fs;
            };
            auto plan_at = [&](double fov, double offset) {
                FigureSnapshot fs = scene_at(fov, offset);
                RenderSnapshot3D* s = fs.axes[0].snap3d();
                const FigureLayout lay = compute_figure_layout(fs, 400, 400);
                return plan_planes3d(lay.cells[0].box3d->proj, s->planes);
            };
            // Pixels per box unit of the plane at the marker's position.
            auto plane_scale_at = [&](double fov, double offset, const Vec3& at) {
                FigureSnapshot fs = scene_at(fov, offset);
                const FigureLayout lay = compute_figure_layout(fs, 400, 400);
                const Projector3D& p = lay.cells[0].box3d->proj;
                const Vec3 b = p.transform().to_box(at.x, at.y, offset);
                const Px3 q0 = p.project_box(b);
                const Px3 qu = p.project_box(b + Vec3{0.01, 0.0, 0.0});
                const Px3 qv = p.project_box(b + Vec3{0.0, 0.01, 0.0});
                return std::hypot(qu.x - q0.x, qu.y - q0.y)
                       + std::hypot(qv.x - q0.x, qv.y - q0.y);
            };
            auto widths_and_sizes = [](const std::vector<PlanePlanItem>& plan,
                                       float& stroke_w, float& marker_sz) {
                stroke_w = marker_sz = 0.0f;
                for (const auto& it: plan) {
                    if (it.form == PlanePlanItem::Form::Strokes && !it.strokes.empty())
                        stroke_w = std::max(stroke_w, it.strokes.front().width);
                    if (it.form == PlanePlanItem::Form::Markers && !it.marks.empty())
                        marker_sz = std::max(marker_sz, it.marks.front().size);
                }
            };
            float w_near = 0, s_near = 0, w_far = 0, s_far = 0;
            widths_and_sizes(plan_at(80.0, 0.9), w_near, s_near);
            widths_and_sizes(plan_at(80.0, -0.9), w_far, s_far);
            check(w_near > w_far * 1.05f,
                  "kinds: a stroke on a near plane comes out thicker than the same one far away");
            check(s_near > s_far * 1.05f && s_far > 0.0f,
                  "kinds: and so does a marker -- on a plane it is on the plane, not on the screen");
            // Compared with the plane's projected scale at the marker (the two
            // measurements are at different points, so not each other's oracle).
            const double scale_near = plane_scale_at(80.0, 0.9, Vec3{1.0, 1.0, 0.0});
            const double scale_far = plane_scale_at(80.0, -0.9, Vec3{1.0, 1.0, 0.0});
            check(std::fabs(static_cast<double>(s_near / s_far) - scale_near / scale_far)
                  < 1e-3 * (scale_near / scale_far),
                  "kinds: by exactly the plane's own projected scale at that point");
        }

        // ---- The ribbon lies in the plane (a billboard would z-fight and differ
        // in thickness inside vs outside a bar). Checked against the segment's own
        // projected length, so only foreshortening remains.
        {
            auto at_elev = [&](double elev, float& width, float& along) {
                FigureSnapshot f = make_snapshot3d(1, 1, 1);
                RenderSnapshot3D* sn = f.axes[0].snap3d();
                sn->camera.azimuth = 0.0;
                sn->camera.elevation = elev;
                sn->xmin = 0;
                sn->xmax = 4;
                sn->xlim_auto = false;
                sn->ymin = 0;
                sn->ymax = 4;
                sn->ylim_auto = false;
                sn->zmin = -1;
                sn->zmax = 1;
                sn->zlim_auto = false;
                PlaneSnapshot p;
                p.orient = PlaneOrientation::XY;
                LinePlot lp;
                // Constant x: the segment runs along y (across the screen at
                // azimuth 0); its in-plane perpendicular foreshortens.
                lp.x = std::vector<double>{2.0, 2.0};
                lp.y = std::vector<double>{1.0, 3.0};
                lp.opts.linewidth = 6.0f;
                p.sheet.lines.push_back(std::move(lp));
                sn->planes.push_back(std::move(p));

                const FigureLayout l = compute_figure_layout(f, 400, 400);
                const auto plan = plan_planes3d(l.cells[0].box3d->proj, sn->planes);
                width = along = 0.0f;
                for (const auto& it: plan) {
                    if (it.form != PlanePlanItem::Form::Strokes || it.strokes.empty()) continue;
                    const auto& st = it.strokes[0];
                    width = st.width;
                    along = std::hypot(st.xy[2] - st.xy[0], st.xy[3] - st.xy[1]);
                }
            };
            float w_face = 0, a_face = 0, w_edge = 0, a_edge = 0;
            at_elev(80.0, w_face, a_face);
            at_elev(4.0, w_edge, a_edge);
            check(w_face > 0.0f && a_face > 0.0f && a_edge > 0.0f,
                  "kinds: (a stroke on a plane is drawn at both camera elevations)");
            check(a_edge > a_face * 0.9f && a_edge < a_face * 1.1f,
                  "kinds: (and the segment itself is the same length on screen at both)");
            check(w_edge / a_edge < (w_face / a_face) * 0.4f,
                  "kinds: a stroke foreshortens with the surface it is on -- it lies *in* the "
                  "plane, not billboarded across it");
        }

        // ---- Rendered: a bar's projected centre must carry the bar's color.
        constexpr int W = 700, H = 620;
        auto render = [&](const FigureSnapshot& fs, const std::string& stem) {
            {
                GLContext ctx({
                    .width = W, .height = H,
                    .title = "layout_test", .visible = false
                });
                NvgRenderer nvg(ctx.nvg());
                DataRenderer data_r;
                export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1);
            }
            export_figure_svg(fs, stem + ".svg", W, H);
        };

        // A heatmap and an opaque bar on the same plane: coplanar, so the depth
        // func must not reject the bar.
        FigureSnapshot fs = make_snapshot3d(1, 1, 1); {
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->box_style.panes = false;
            s->grid_enabled = false;
            s->xticks_override = std::vector<Tick>{};
            s->yticks_override = std::vector<Tick>{};
            s->zticks_override = std::vector<Tick>{};
            s->xmin = 0;
            s->xmax = 4;
            s->xlim_auto = false;
            s->ymin = 0;
            s->ymax = 4;
            s->ylim_auto = false;
            s->zmin = -1;
            s->zmax = 1;
            s->zlim_auto = false;

            PlaneSnapshot p;
            p.orient = PlaneOrientation::XY;
            p.offset = 0.0;
            HeatmapPlot hp;
            hp.rows = 2;
            hp.cols = 2;
            hp.data = std::vector<float>(4, 0.0f); // one flat colour
            hp.xrange = {0.0, 4.0};
            hp.yrange = {0.0, 4.0};
            p.sheet.heatmaps.push_back(std::move(hp));

            BarPlot bp;
            bp.centers = std::vector<double>{2.0};
            bp.heights = std::vector<double>{3.0};
            bp.bar_width = 2.0;
            bp.opts.color = {1.0f, 0.0f, 0.0f, 1.0f}; // findable red
            // A thick outline and an error box, coplanar with the bar (the setup
            // that showed z-fighting with billboarded strokes).
            bp.opts.linewidth = 4.0f;
            bp.opts.edgecolor = {0.0f, 0.0f, 1.0f, 1.0f}; // findable blue
            bp.err.y_box_lo = std::vector<double>{0.4};
            bp.opts.errorbar.linewidth = 2.0f;
            bp.opts.errorbar.color = Color{0.0f, 0.6f, 0.0f, 1.0f};
            bp.opts.errorbar.box_alpha = 1.0f;
            p.sheet.bars.push_back(std::move(bp));

            s->planes.push_back(std::move(p));
        }
        render(fs, "plane_kinds"); {
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load("plane_kinds.png", &w, &h, &comp, 4);
            int red = 0, probes = 0;
            // Points inside the bar (x 1..3, y 0..3), clear of its edges.
            for (double bx: {1.5, 2.0, 2.5})
                for (double by: {0.5, 1.5, 2.5}) {
                    const Px3 q = proj.project(bx, by, 0.0);
                    const int xi = static_cast<int>(q.x), yi = static_cast<int>(q.y);
                    ++probes;
                    if (!px || xi < 0 || yi < 0 || xi >= w || yi >= h) continue;
                    const unsigned char* p = px + (yi * w + xi) * 4;
                    if (p[0] > 200 && p[1] < 60 && p[2] < 60) ++red;
                }
            if (px) stbi_image_free(px);
            check(probes == 9 && red == 9,
                  "kinds: a bar coplanar with the heatmap under it draws over it, not under it");
        }

        // PNG and SVG place it identically (both from the plan's pixels).
        {
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const std::vector<PlanePlanItem> plan =
                    plan_planes3d(lay.cells[0].box3d->proj, fs.axes[0].snap3d()->planes);
            std::ifstream f("plane_kinds.svg", std::ios::binary);
            const std::string svg((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
            bool found = false;
            for (const auto& it: plan) {
                if (it.form != PlanePlanItem::Form::Polys || it.polys.empty()) continue;
                for (const auto& poly: it.polys) {
                    if (poly.fill.r < 0.5f) continue; // the heatmap cells
                    std::ostringstream pts;
                    for (std::size_t i = 0; i + 1 < poly.xy.size(); i += 2) {
                        if (i) pts << ' ';
                        pts << poly.xy[i] << ',' << poly.xy[i + 1];
                    }
                    if (svg.find(pts.str()) != std::string::npos) found = true;
                }
            }
            check(found,
                  "kinds: and the SVG carries the plan's own pixels rather than a projection of its own");
        }
    }

    void test_plane2d_legend_and_contours() {
        std::printf("\n[3D: the hoisted legend, and contours on a plane]\n");

        using namespace sextant;

        constexpr int W = 420, H = 380;

        // ---- The legend: planes' keys in plane order via
        // collect_legend_entries().
        FigureSnapshot fs = make_snapshot3d(1, 1, 1);
        RenderSnapshot3D* s = fs.axes[0].snap3d();
        s->planes.push_back(make_kinds_plane(PlaneOrientation::XY, 0.0));
        s->planes.push_back(make_kinds_plane(PlaneOrientation::YZ, 1.0));

        check(compute_cell_decorations(*s).legend_entries.empty(),
              "legend: nothing is keyed until the axes asks for a legend");

        s->legend_enabled = true;
        const CellDecorations dec = compute_cell_decorations(*s);
        check(dec.legend_entries.size() == 6,
              "legend: every labelled series on every plane gets a key");
        check(dec.legend_entries[0].name == "line" && dec.legend_entries[1].name == "dots"
              && dec.legend_entries[2].name == "bars",
              "legend: in the 2D kind order within a plane");
        check(dec.legend_entries[3].name == "line",
              "legend: and in plane order across them");
        check(dec.legend_entries[0].kind == LegendKind::Line
              && dec.legend_entries[1].kind == LegendKind::Marker
              && dec.legend_entries[2].kind == LegendKind::Bar,
              "legend: with the swatch kind each series calls for");

        // ---- Each key has its series' marker shape (a Square series must not be
        // keyed by a circle).
        check(dec.legend_entries[1].marker == MarkerStyle::Square,
              "legend: a marker key carries the series' own marker shape");

        // In the drawn file: the only marker series is a Square, so the swatch must
        // be a <rect>, not a <circle>.
        {
            FigureSnapshot one = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* os = one.axes[0].snap3d();
            os->planes.push_back(make_kinds_plane(PlaneOrientation::XY, 0.0));
            os->legend_enabled = true;
            export_figure_svg(one, "legend_marker.svg", W, H);
            std::ifstream f("legend_marker.svg", std::ios::binary);
            const std::string svg((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
            check(svg.find("r=\"4.5\"") == std::string::npos,
                  "legend: and draws it -- a Square series leaves no disc swatch in the file");
            check(svg.find("width=\"9\" height=\"9\"") != std::string::npos,
                  "legend: it is a 9x9 square, the swatch radius doubled");
        }

        // Carved out of the cell beside the frame, like a colorbar.
        FigureSnapshot bare = fs;
        bare.axes[0].snap3d()->legend_enabled = false;
        const FigureLayout lb = compute_figure_layout(bare, W, H);
        const FigureLayout lw = compute_figure_layout(fs, W, H);
        check(lw.cells[0].has_legend() && !lb.cells[0].has_legend(),
              "legend: the layout carves a box for it");
        check(std::fabs((lb.cells[0].frame.w - lw.cells[0].frame.w) - dec.legend_block) < 1e-3f,
              "legend: by exactly the width the decoration measured");
        const LayoutSize need = figure_size_for_frame(fs, 1, lw.cells[0].frame.w,
                                                      lw.cells[0].frame.h);
        check(std::fabs(need.width - static_cast<float>(W)) < 1e-2f,
              "legend: and figure_size_for_frame() inverts that carve too");

        // ---- Contours are annotation: pixel width and label size, independent
        // of the camera.
        auto contoured = [&](double fov, double offset) {
            FigureSnapshot c = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* cs = c.axes[0].snap3d();
            cs->camera.projection = Projection::Perspective;
            cs->camera.fov = fov;
            cs->xmin = 0;
            cs->xmax = 8;
            cs->xlim_auto = false;
            cs->ymin = 0;
            cs->ymax = 8;
            cs->ylim_auto = false;
            cs->zmin = -1;
            cs->zmax = 1;
            cs->zlim_auto = false;
            PlaneSnapshot p;
            p.orient = PlaneOrientation::XY;
            p.offset = offset;
            HeatmapPlot hp;
            hp.rows = 8;
            hp.cols = 8;
            hp.data.mut().resize(64);
            for (int r = 0; r < 8; ++r)
                for (int col = 0; col < 8; ++col)
                    hp.data.mut()[static_cast<std::size_t>(r) * 8 + col] =
                            static_cast<float>(col) / 7.0f;
            hp.xrange = {0.0, 8.0};
            hp.yrange = {0.0, 8.0};
            hp.opts.contours = {0.5};
            hp.opts.contour_linewidth = 2.0f;
            hp.opts.contour_labels = true;
            p.sheet.heatmaps.push_back(std::move(hp));
            cs->planes.push_back(std::move(p));
            return c;
        };

        FigureSnapshot cnear = contoured(80.0, 0.9);
        const FigureLayout ln = compute_figure_layout(cnear, W, H);
        const std::vector<PlaneContourDraw> pn =
                plan_plane_contours(ln.cells[0].box3d->proj, cnear.axes[0].snap3d()->planes,
                                    "", nullptr, 0, 0);
        check(pn.size() == 1 && !pn[0].draw.runs.empty(),
              "contours: a plane's heatmap traces and plans like a 2D one");
        check(!pn[0].draw.labels.empty(),
              "contours: with the inline level label the 2D path gives it");
        check(pn[0].linewidth == 2.0f,
              "contours: at exactly the width asked for -- a pixel width, not a length in the box");

        FigureSnapshot cfar = contoured(80.0, -0.9);
        const FigureLayout lf = compute_figure_layout(cfar, W, H);
        const std::vector<PlaneContourDraw> pf =
                plan_plane_contours(lf.cells[0].box3d->proj, cfar.axes[0].snap3d()->planes,
                                    "", nullptr, 0, 0);
        check(pf.size() == 1 && pf[0].linewidth == pn[0].linewidth
              && pf[0].fontsize == pn[0].fontsize,
              "contours: and the same width and label size on a plane twice as far away");

        // The geometry did move (so the check isn't vacuous).
        check(!pf[0].draw.runs.empty() && !pn[0].draw.runs.empty()
              && pf[0].draw.runs[0].px[0] != pn[0].draw.runs[0].px[0],
              "contours: (the two cameras really do put the line in different places)");

        // Both outputs draw them from one plan.
        export_figure_svg(cnear, "plane_contours.svg", W, H);
        std::ifstream f("plane_contours.svg", std::ios::binary);
        const std::string svg((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
        std::ostringstream first;
        first << pn[0].draw.runs[0].px[0] << "," << pn[0].draw.runs[0].py[0];
        check(svg.find(first.str()) != std::string::npos,
              "contours: and the SVG carries the plan's own pixels");
    }
} // namespace lt
