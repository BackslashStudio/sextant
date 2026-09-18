// scatter3d: ingest, the color range, and auto-scale. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "renderer/scatter3d.h"
#include "plot_data_view.h"

namespace lt {
    // -------------------------------------------------------------------------
    // scatter3d(): overloads, throws, auto-scale
    // -------------------------------------------------------------------------
    void test_scatter3d_ingest() {
        std::printf("\n[3D: scatter3d ingest, colour range and auto-scale]\n");

        using namespace sextant;

        auto threw = [](auto&& fn) {
            try {
                fn();
                return false;
            } catch (const std::invalid_argument&) { return true; }
        };

        const std::vector<double> x{0.0, 1.0, 2.0};
        const std::vector<double> y{5.0, 7.0, 6.0};
        const std::vector<double> z{-1.0, 0.0, 3.0};
        const std::vector<double> c{10.0, 20.0, 40.0};

        auto fig = Figure::create({.width = 300, .height = 240});
        auto ax = fig->add_subplot3d(1, 1, 1);

        check(threw([&] { ax->scatter3d({}, {}, {}); }),
              "scatter3d: an empty series throws rather than becoming an invisible plot object");
        check(threw([&] { ax->scatter3d(x, y, {z.data(), 2}); }),
              "scatter3d: three coordinate vectors of different lengths throw");
        check(threw([&] { ax->scatter3d(x, y, z, {c.data(), 2}); }),
              "scatter3d: a colors vector that is neither empty nor |x| long throws");
        const std::vector<double> nan_z{0.0, std::numeric_limits<double>::quiet_NaN(), 1.0};
        check(threw([&] { ax->scatter3d(x, y, nan_z); }),
              "scatter3d: a non-finite coordinate throws, once, rather than reaching the buffer");
        const std::vector<double> nan_c{1.0, 2.0, std::numeric_limits<double>::infinity()};
        check(threw([&] { ax->scatter3d(x, y, z, nan_c); }),
              "scatter3d: and a non-finite colour is rejected on the same terms as a coordinate");
        check(threw([&] {
                  Scatter3DOptions o;
                  o.vmin = std::numeric_limits<float>::quiet_NaN();
                  ax->scatter3d(x, y, z, c, o);
              }),
              "scatter3d: a non-finite vmin/vmax throws -- it is a divisor downstream");

        // Error bars: each span checked separately (accepted at |x| entries,
        // rejected otherwise), through both ErrorBar3D overloads.
        {
            int accepted = 0, rejected = 0;
            std::vector<double> one{0.5, 0.5, 0.5};
            std::vector<double> short_one{0.5, 0.5};
            std::vector < std::span<const double>
            ErrorBar3D:: * > fields{
                &ErrorBar3D::x_cap_lo, &ErrorBar3D::x_cap_hi,
                &ErrorBar3D::x_box_lo, &ErrorBar3D::x_box_hi,
                &ErrorBar3D::y_cap_lo, &ErrorBar3D::y_cap_hi,
                &ErrorBar3D::y_box_lo, &ErrorBar3D::y_box_hi,
                &ErrorBar3D::z_cap_lo, &ErrorBar3D::z_cap_hi,
                &ErrorBar3D::z_box_lo, &ErrorBar3D::z_box_hi,
            };
            for (auto f: fields) {
                ErrorBar3D err;
                err.*f = one;
                if (!threw([&] { ax->scatter3d(x, y, z, err); }) &&
                    !threw([&] { ax->scatter3d(x, y, z, c, err); }))
                    ++accepted;
                err.*f = short_one;
                if (threw([&] { ax->scatter3d(x, y, z, err); }) &&
                    threw([&] { ax->scatter3d(x, y, z, c, err); }))
                    ++rejected;
            }
            check(accepted == 12,
                  "scatter3d: each of ErrorBar3D's twelve spans is accepted at one entry per "
                  "point, through both overloads that take one");
            check(rejected == 12,
                  "scatter3d: and each is rejected at any other length, rather than read "
                  "past its end or silently shortened");

            // Style is never validated.
            Scatter3DOptions styled;
            styled.errorbar.linewidth = 2.0f;
            styled.errorbar.capsize = 3.0f;
            styled.errorbar.capstyle = CapStyle::Arrow;
            styled.errorbar.boxwidth = 4.0f;
            styled.errorbar.box_alpha = 0.5f;
            check(!threw([&] { ax->scatter3d(x, y, z, styled); }),
                  "scatter3d: ErrorBar3DOptions is style, not data, and none of it throws");
            check(!threw([&] { ax->scatter3d(x, y, z, ErrorBar3D{}, styled); }),
                  "scatter3d: and an empty ErrorBar3D is the same as passing none");
        }

        check(!threw([&] { ax->scatter3d(x, y, z); }),
              "scatter3d: three finite vectors of one length are accepted");
        check(!threw([&] { ax->scatter3d(x, y, z, c); }),
              "scatter3d: and so is a fourth colour dimension");

        // Only whether `colors` is non-empty distinguishes the overloads.
        Scatter3DPlot flat;
        flat.x = x;
        flat.y = y;
        flat.z = z;
        Scatter3DPlot mapped = flat;
        mapped.colors = c;
        check(!flat.colormapped() && mapped.colormapped() && flat.count() == 3,
              "scatter3d: a colors vector is the only thing that makes a series colormapped");

        // Color range, as SurfaceOptions (not ScatterZOptions).
        {
            double lo = 0.0, hi = 0.0;
            scatter3d_value_range(mapped, lo, hi);
            check(lo == 10.0 && hi == 40.0,
                  "scatter3d: an empty vmin/vmax interval means the colors' own range");
            Scatter3DPlot fixed = mapped;
            fixed.opts.vmin = 0.0f;
            fixed.opts.vmax = 100.0f;
            scatter3d_value_range(fixed, lo, hi);
            check(lo == 0.0 && hi == 100.0, "scatter3d: and a stated one is used as stated");
            scatter3d_value_range(flat, lo, hi);
            check(lo == 0.0 && hi == 1.0,
                  "scatter3d: a series with no colors has no range to take, and says 0..1");
        }

        // Auto-scale: three coordinates onto three axes, no footprint.
        {
            const DataBounds3D b = auto_scale3d({}, {}, {}, {flat}, {}, {}, 0.0);
            check(b.xmin == 0.0 && b.xmax == 2.0 && b.ymin == 5.0 && b.ymax == 7.0 &&
                  b.zmin == -1.0 && b.zmax == 3.0,
                  "scatter3d: its extent is exactly its points, on all three axes at once");

            // A one-point series still produces a box.
            Scatter3DPlot single;
            single.x = std::vector<double>{4.0};
            single.y = std::vector<double>{4.0};
            single.z = std::vector<double>{4.0};
            const DataBounds3D s = auto_scale3d({}, {}, {}, {single}, {}, {}, 0.0);
            check(s.xmin < s.xmax && s.ymin < s.ymax && s.zmin < s.zmax,
                  "scatter3d: a single point still spans a box rather than a degenerate one");

            // It shares the box with the other kinds.
            const Bar3DPlot bars = bar3d_grid();
            const DataBounds3D both = auto_scale3d({bars}, {}, {}, {flat}, {}, {}, 0.0);
            const DataBounds3D bar_only = auto_scale3d({bars}, {}, {}, {}, {}, {}, 0.0);
            check(both.zmax >= flat.z[2] && both.zmax >= bar_only.zmax,
                  "scatter3d: a cloud and a bar grid in one axes give the union of their extents");
        }
    }

    // -------------------------------------------------------------------------
    // Billboards keep their pixel size; the depth cue
    // -------------------------------------------------------------------------
    void test_scatter3d_render() {
        std::printf("\n[3D: scatter3d rendered -- pixel size and the depth cue]\n");

        using namespace sextant;

        constexpr int W = 420, H = 360;

        // Looking down +x: x is depth and y horizontal, so two points at different
        // depths land far apart on screen.
        auto bare = [&](Projection mode) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.projection = mode;
            s->camera.azimuth = 0.0;
            s->camera.elevation = 0.0;
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

        // A square marker fills its quad, so its drawn width is `size`.
        const std::vector<double> px_{0.08, 0.92};
        const std::vector<double> py_{0.30, 0.70};
        const std::vector<double> pz_{0.50, 0.50};

        auto make = [&](Projection mode, float depthshade) {
            FigureSnapshot fs = bare(mode);
            Scatter3DPlot s;
            s.x = px_;
            s.y = py_;
            s.z = pz_;
            s.opts.color = Color::Red;
            s.opts.marker = MarkerStyle::Square;
            s.opts.size = 20.0f;
            s.opts.alpha = 1.0f;
            s.opts.depthshade = depthshade;
            fs.axes[0].snap3d()->scatter3d.push_back(s);
            return fs;
        };

        // The run of marker pixels across the row through a point's centre.
        auto span_at = [&](const unsigned char* px, int w, int h, Px3 q) {
            const int yi = static_cast<int>(std::lround(q.y));
            const int xc = static_cast<int>(std::lround(q.x));
            if (yi < 0 || yi >= h || xc < 0 || xc >= w) return 0;
            auto marked = [&](int x) {
                const unsigned char* p = px + (yi * w + x) * 4;
                return p[0] > p[1] + 20 && p[0] > p[2] + 20; // any red, shaded or not
            };
            if (!marked(xc)) return 0;
            int lo = xc, hi = xc;
            while (lo > 0 && marked(lo - 1)) --lo;
            while (hi + 1 < w && marked(hi + 1)) ++hi;
            return hi - lo + 1;
        };

        // ---- A marker is its requested size at any depth
        for (const Projection mode: {Projection::Orthographic, Projection::Perspective}) {
            const bool persp = mode == Projection::Perspective;
            const FigureSnapshot fs = make(mode, 0.0f);
            render(fs, persp ? "scatter3d_persp" : "scatter3d_ortho");

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;

            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load(persp ? "scatter3d_persp.png" : "scatter3d_ortho.png",
                                          &w, &h, &comp, 4);
            int near_w = 0, far_w = 0;
            double near_depth = 0.0, far_depth = 0.0;
            if (px) {
                const Px3 a = proj.project(px_[0], py_[0], pz_[0]);
                const Px3 b = proj.project(px_[1], py_[1], pz_[1]);
                // The camera is at +x, so the larger x is nearer.
                far_depth = a.depth;
                near_depth = b.depth;
                far_w = span_at(px, w, h, a);
                near_w = span_at(px, w, h, b);
                stbi_image_free(px);
            }
            std::printf("  %-13s depths %.3f / %.3f, marker widths %d / %d px\n",
                        persp ? "perspective" : "orthographic",
                        near_depth, far_depth, near_w, far_w);
            // Within a pixel of 20 and equal (a missing divide cancellation would
            // show here).
            check(near_w >= 19 && near_w <= 21 && far_w >= 19 && far_w <= 21,
                  std::string(persp ? "scatter3d: under perspective " : "scatter3d: under ortho ")
                  + "a marker is drawn the pixel size it was given");
            check(near_w == far_w,
                  std::string(persp ? "scatter3d: under perspective " : "scatter3d: under ortho ")
                  + "distance does not change a marker's size -- a symbol, not geometry");
            if (persp)
                check(far_depth > near_depth * 1.2,
                      "scatter3d: and the two points really were at different depths");
        }

        // ---- depthshade darkens by exactly the CPU's ramp
        {
            const FigureSnapshot lit = make(Projection::Orthographic, 0.0f);
            const FigureSnapshot dim = make(Projection::Orthographic, 1.0f);
            render(dim, "scatter3d_shaded");

            const FigureLayout lay = compute_figure_layout(lit, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            float dmin = 0.0f, dmax = 1.0f;
            box_depth_range(proj, dmin, dmax);

            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load("scatter3d_shaded.png", &w, &h, &comp, 4);
            int matched = 0, probed = 0;
            int near_r = -1, far_r = -1;
            for (int i = 0; i < 2 && px; ++i) {
                const Px3 q = proj.project(px_[static_cast<std::size_t>(i)],
                                           py_[static_cast<std::size_t>(i)],
                                           pz_[static_cast<std::size_t>(i)]);
                const int xi = static_cast<int>(std::lround(q.x));
                const int yi = static_cast<int>(std::lround(q.y));
                if (xi < 0 || yi < 0 || xi >= w || yi >= h) continue;
                const unsigned char* p = px + (yi * w + xi) * 4;
                const float t = (q.depth - dmin) / (dmax - dmin);
                const Color want = scatter3d_depth_shade(Color::Red, 1.0f, t);
                ++probed;
                if (std::abs(p[0] - static_cast<int>(std::lround(want.r * 255.0f))) <= 2 &&
                    std::abs(p[1] - static_cast<int>(std::lround(want.g * 255.0f))) <= 2 &&
                    std::abs(p[2] - static_cast<int>(std::lround(want.b * 255.0f))) <= 2)
                    ++matched;
                (i == 0 ? far_r : near_r) = p[0];
            }
            if (px) stbi_image_free(px);
            std::printf("  depthshade=1 red channel: near %d, far %d\n", near_r, far_r);
            check(probed == 2 && matched == 2,
                  "scatter3d: the shader's depth ramp is scatter3d_depth_shade() over "
                  "box_depth_range(), to the pixel");
            check(near_r > far_r,
                  "scatter3d: and it darkens with distance rather than toward it");

            // Negative control: with depthshade off nothing darkens.
            render(lit, "scatter3d_lit");
            int w2 = 0, h2 = 0, c2 = 0;
            unsigned char* q2 = stbi_load("scatter3d_lit.png", &w2, &h2, &c2, 4);
            int flat = 0;
            for (int i = 0; i < 2 && q2; ++i) {
                const Px3 q = proj.project(px_[static_cast<std::size_t>(i)],
                                           py_[static_cast<std::size_t>(i)],
                                           pz_[static_cast<std::size_t>(i)]);
                const int xi = static_cast<int>(std::lround(q.x));
                const int yi = static_cast<int>(std::lround(q.y));
                if (xi < 0 || yi < 0 || xi >= w2 || yi >= h2) continue;
                const unsigned char* p = q2 + (yi * w2 + xi) * 4;
                // Against Color::Red itself (tab10's 214,39,40).
                const int want[3] = {
                    static_cast<int>(std::lround(Color::Red.r * 255.0f)),
                    static_cast<int>(std::lround(Color::Red.g * 255.0f)),
                    static_cast<int>(std::lround(Color::Red.b * 255.0f))
                };
                if (std::abs(p[0] - want[0]) <= 2 && std::abs(p[1] - want[1]) <= 2 &&
                    std::abs(p[2] - want[2]) <= 2)
                    ++flat;
            }
            if (q2) stbi_image_free(q2);
            check(flat == 2,
                  "scatter3d: depthshade 0 leaves both markers the colour the caller asked for");
        }

        // ---- A colormapped point carries its own entry over the series' range.
        {
            FigureSnapshot fs = bare(Projection::Orthographic);
            Scatter3DPlot s;
            s.x = px_;
            s.y = py_;
            s.z = pz_;
            s.colors = std::vector<double>{10.0, 40.0};
            s.opts.marker = MarkerStyle::Square;
            s.opts.size = 20.0f;
            fs.axes[0].snap3d()->scatter3d.push_back(s);
            render(fs, "scatter3d_cmap");

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            const Scatter3DPlot& sp = fs.axes[0].snap3d()->scatter3d[0];
            double vmin = 0.0, vmax = 1.0;
            scatter3d_value_range(sp, vmin, vmax);

            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load("scatter3d_cmap.png", &w, &h, &comp, 4);
            int hits = 0;
            for (std::size_t i = 0; i < sp.count() && px; ++i) {
                const Px3 q = proj.project(sp.x[i], sp.y[i], sp.z[i]);
                const int xi = static_cast<int>(std::lround(q.x));
                const int yi = static_cast<int>(std::lround(q.y));
                if (xi < 0 || yi < 0 || xi >= w || yi >= h) continue;
                const unsigned char* p = px + (yi * w + xi) * 4;
                const Color want = scatter3d_point_color(sp, i, vmin, vmax);
                if (std::abs(p[0] - static_cast<int>(std::lround(want.r * 255.0f))) <= 2 &&
                    std::abs(p[1] - static_cast<int>(std::lround(want.g * 255.0f))) <= 2 &&
                    std::abs(p[2] - static_cast<int>(std::lround(want.b * 255.0f))) <= 2)
                    ++hits;
            }
            if (px) stbi_image_free(px);
            check(hits == 2,
                  "scatter3d: a colormapped point is drawn its own colour, over the series' "
                  "own range -- the ends of it being the two ends of the colormap");
        }

        // ---- The depth buffer resolves markers against solids: hidden behind an
        // opaque sheet, visible in front.
        {
            FigureSnapshot fs = bare(Projection::Orthographic);
            RenderSnapshot3D* snap = fs.axes[0].snap3d();
            // An opaque sheet across the box, normal along x.
            SurfacePlot sheet;
            sheet.orient = PlaneOrientation::YZ;
            sheet.u = std::vector<double>{0.0, 1.0}; // y
            sheet.v = std::vector<double>{0.0, 1.0}; // z
            sheet.heights = std::vector<double>{0.5, 0.5, 0.5, 0.5};
            sheet.opts.color = Color::Green;
            sheet.opts.shading = 0.0f;
            snap->surfaces.push_back(sheet);

            Scatter3DPlot s;
            s.x = std::vector<double>{0.15, 0.85}; // behind the sheet, in front of it
            s.y = std::vector<double>{0.35, 0.65};
            s.z = std::vector<double>{0.50, 0.50};
            s.opts.color = Color::Red;
            s.opts.marker = MarkerStyle::Square;
            s.opts.size = 20.0f;
            snap->scatter3d.push_back(s);
            render(fs, "scatter3d_occluded");

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load("scatter3d_occluded.png", &w, &h, &comp, 4);
            bool hidden = false, shown = false;
            if (px) {
                const Px3 a = proj.project(s.x[0], s.y[0], s.z[0]); // the far one
                const Px3 b = proj.project(s.x[1], s.y[1], s.z[1]); // the near one
                auto red_at = [&](Px3 q) {
                    const int xi = static_cast<int>(std::lround(q.x));
                    const int yi = static_cast<int>(std::lround(q.y));
                    if (xi < 0 || yi < 0 || xi >= w || yi >= h) return false;
                    const unsigned char* p = px + (yi * w + xi) * 4;
                    return p[0] > p[1] + 20 && p[0] > p[2] + 20;
                };
                hidden = !red_at(a);
                shown = red_at(b);
                stbi_image_free(px);
            }
            check(hidden && shown,
                  "scatter3d: a marker behind an opaque sheet is hidden by it and one in "
                  "front is not -- the billboard carries the point's own depth");
        }
    }

    // -------------------------------------------------------------------------
    // SVG markers and their order: for every marker a polygon covers, the nearer
    // of the two is emitted last.
    // -------------------------------------------------------------------------
    void test_scatter3d_svg_order() {
        std::printf("\n[3D: a cloud through a sheet, ordered for the SVG]\n");

        using namespace sextant;

        constexpr int W = 460, H = 400;

        auto build = [&](float sheet_alpha) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.azimuth = -50.0;
            s->camera.elevation = 22.0;
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

            // A coarse sheet at x = 0.5 spanning the y-z face, with half the cloud
            // behind it.
            SurfacePlot sheet;
            sheet.orient = PlaneOrientation::YZ;
            sheet.u = std::vector<double>{0.0, 0.5, 1.0};
            sheet.v = std::vector<double>{0.0, 0.5, 1.0};
            sheet.heights = std::vector<double>(9, 0.5);
            sheet.opts.color = Color::Green;
            sheet.opts.shading = 0.0f;
            sheet.opts.alpha = sheet_alpha;
            s->surfaces.push_back(std::move(sheet));

            Scatter3DPlot cloud;
            std::vector<double> cx, cy, cz;
            for (int i = 0; i < 5; ++i)
                for (int j = 0; j < 5; ++j) {
                    const double u = 0.15 + 0.175 * i, v = 0.15 + 0.175 * j;
                    cx.push_back(0.2);
                    cy.push_back(u);
                    cz.push_back(v);
                    cx.push_back(0.8);
                    cy.push_back(u);
                    cz.push_back(v);
                }
            cloud.x = cx;
            cloud.y = cy;
            cloud.z = cz;
            cloud.opts.color = Color::Red;
            cloud.opts.marker = MarkerStyle::Circle;
            cloud.opts.size = 9.0f;
            s->scatter3d.push_back(std::move(cloud));
            return fs;
        };

        bool newell_on = true;
        if (const char* env = std::getenv("SEXTANT_NEWELL"))
            newell_on = std::atoi(env) != 0;

        for (const float alpha: {0.45f, 1.0f}) {
            const FigureSnapshot fs = build(alpha);
            const RenderSnapshot3D* s = fs.axes[0].snap3d();
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& pj = lay.cells[0].box3d->proj;

            const std::vector<Surface3DPolygon> splan = plan_surfaces3d(pj, s->surfaces);
            const std::vector<Scatter3DMarker> mplan = plan_scatter3d(pj, s->scatter3d);
            PaintOrderStats st;
            const std::vector<ScenePaint> scene = plan_scene3d(pj, {}, splan, {}, mplan, {}, {}, {}, &st);

            check(mplan.size() == 50,
                  "scatter3d/svg: every point of the cloud is planned as a marker");
            check(!st.bailed, "scatter3d/svg: the scene stays inside the work bound");

            // Where each emitted item landed in the order, and what it is.
            struct Slot {
                bool is_marker;
                std::size_t index;
            };
            std::vector<Slot> slots;
            slots.reserve(scene.size());
            std::vector<std::vector<float>> rings(scene.size());
            std::vector<Vec3> plane_p0(scene.size()), plane_n(scene.size());
            std::vector<char> plane_ok(scene.size(), 0);
            for (std::size_t k = 0; k < scene.size(); ++k) {
                const ScenePaint& sp = scene[k];
                if (sp.kind == ScenePaint::Kind::Scatter) {
                    slots.push_back({true, sp.index});
                    continue;
                }
                slots.push_back({false, sp.index});
                if (sp.kind != ScenePaint::Kind::Surface || sp.index >= splan.size()) continue;
                rings[k] = sp.xy.empty() ? splan[sp.index].xy : sp.xy;
                Vec3 p0, n;
                if (ring_plane(splan[sp.index].box, p0, n)) {
                    plane_p0[k] = p0;
                    plane_n[k] = n;
                    plane_ok[k] = 1;
                }
            }

            auto inside = [](const std::vector<float>& xy, float px, float py) {
                if (xy.size() < 6) return false;
                const std::size_t n = xy.size() / 2;
                int sign = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t j = (i + 1) % n;
                    const float ex = xy[j * 2] - xy[i * 2];
                    const float ey = xy[j * 2 + 1] - xy[i * 2 + 1];
                    const float cr = ex * (py - xy[i * 2 + 1]) - ey * (px - xy[i * 2]);
                    if (std::fabs(cr) < 1e-6f) continue;
                    const int sg = cr > 0 ? 1 : -1;
                    if (sign == 0) sign = sg;
                    else if (sg != sign) return false;
                }
                return sign != 0;
            };

            int covered = 0, wrong = 0, in_front = 0, behind = 0;
            for (std::size_t i = 0; i < slots.size(); ++i) {
                if (!slots[i].is_marker) continue;
                const Scatter3DMarker& m = mplan[slots[i].index];
                for (std::size_t j = 0; j < slots.size(); ++j) {
                    if (slots[j].is_marker || !plane_ok[j] || !inside(rings[j], m.cx, m.cy))
                        continue;
                    // The polygon's depth along the marker's ray.
                    const Projector3D::Ray3 r = pj.ray_from_pixel(m.cx, m.cy);
                    const double den = dot(plane_n[j], r.dir);
                    if (std::fabs(den) < 1e-12) continue;
                    const double t = dot(plane_n[j], plane_p0[j] - r.origin) / den;
                    const float d = pj.project_box(r.origin + r.dir * t).depth;
                    if (std::fabs(d - m.depth) < 1e-4f) continue; // coincident: either order
                    ++covered;
                    const bool marker_nearer = m.depth < d;
                    ++(marker_nearer ? in_front : behind);
                    if (marker_nearer ? (i < j) : (i > j)) ++wrong;
                }
            }
            std::printf("  %s sheet: %zu polys -> %zu, %d covers (%d in front, %d behind), "
                        "%d out of order\n",
                        alpha < 1.0f ? "translucent" : "opaque     ",
                        st.input, st.output, covered, in_front, behind, wrong);
            // Both directions occur (so the depth sort alone can't pass).
            check(in_front > 5 && behind > 5,
                  "scatter3d/svg: the sheet covers markers on both sides of itself, so the "
                  "order below is asked in both directions");
            if (newell_on)
                check(wrong == 0,
                      "scatter3d/svg: every marker a polygon covers is emitted on the right "
                      "side of it -- point_vs_polygon() is exact, and never splits");
            else
                check(wrong > 0,
                      "scatter3d/svg (control): the whole-object order gets markers against "
                      "a sheet wrong, which is what makes the check above about the painter");
            // Markers are never cut.
            int split_markers = 0;
            for (const ScenePaint& sp: scene)
                if (sp.kind == ScenePaint::Kind::Scatter && !sp.xy.empty()) ++split_markers;
            check(split_markers == 0,
                  "scatter3d/svg: and no marker is ever split -- a point has no plane to cut");
        }

        // ---- The file carries the markers with the plan's colors.
        {
            const FigureSnapshot fs = build(0.45f);
            export_figure_svg(fs, "scatter3d_scene.svg", W, H);
            std::ifstream f("scatter3d_scene.svg", std::ios::binary);
            const std::string svg((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& pj = lay.cells[0].box3d->proj;
            const std::vector<Scatter3DMarker> mplan =
                    plan_scatter3d(pj, fs.axes[0].snap3d()->scatter3d);

            int found = 0;
            for (const Scatter3DMarker& m: mplan) {
                std::ostringstream pat;
                pat << "<circle cx=\"" << m.cx << "\" cy=\"" << m.cy
                        << "\" r=\"" << m.radius << '"';
                if (svg.find(pat.str()) != std::string::npos) ++found;
            }
            check(found == static_cast<int>(mplan.size()),
                  "scatter3d/svg: every planned marker reaches the file at its own pixel, "
                  "as the <circle> marker_shape() says a circle is");
        }
    }

    // -------------------------------------------------------------------------
    // The legend key and the colorbar
    // -------------------------------------------------------------------------
    void test_scatter3d_legend_and_colorbar() {
        std::printf("\n[3D: a cloud's legend key and colorbar]\n");

        using namespace sextant;

        // Values away from 0..1, so resolved and declared ranges differ.
        Scatter3DPlot s;
        s.x = std::vector<double>{0.1, 0.5, 0.9};
        s.y = std::vector<double>{0.2, 0.5, 0.8};
        s.z = std::vector<double>{0.3, 0.5, 0.7};
        s.colors = std::vector<double>{20.0, 50.0, 80.0};
        s.opts.colorbar = true;
        s.opts.name = "cloud";
        s.opts.marker = MarkerStyle::Diamond;

        auto with_cloud = [&](Scatter3DPlot sp) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            fs.axes[0].snap3d()->scatter3d.push_back(std::move(sp));
            return fs;
        };

        // ---- The bar
        {
            const auto reqs = find_colorbar_requests(*with_cloud(s).axes[0].snap3d());
            check(reqs.size() == 1 && reqs[0].vmin == 20.0f && reqs[0].vmax == 80.0f,
                  "scatter3d cb: a colormapped cloud that asks for a bar gets one, spanning "
                  "its own colors");
            check(reqs[0].name == "cloud",
                  "scatter3d cb: and the series' label names the scale on it");

            Scatter3DPlot fixed = s;
            fixed.opts.vmin = 0.0f;
            fixed.opts.vmax = 100.0f;
            const auto fr = find_colorbar_requests(*with_cloud(fixed).axes[0].snap3d());
            check(fr.size() == 1 && fr[0].vmin == 0.0f && fr[0].vmax == 100.0f,
                  "scatter3d cb: a declared range is taken as declared");

            // Only with a `colors` vector.
            Scatter3DPlot flat = s;
            flat.colors = CowVec<double>{};
            check(find_colorbar_requests(*with_cloud(flat).axes[0].snap3d()).empty(),
                  "scatter3d cb: a flat cloud asking for a bar gets none -- there is no "
                  "mapping for it to explain");
        }

        // ---- The key, in both forms
        {
            FigureSnapshot fs = with_cloud(s);
            fs.axes[0].snap3d()->legend_enabled = true;
            const auto keys = collect_legend_entries(*fs.axes[0].snap3d());
            check(keys.size() == 1 && keys[0].kind == LegendKind::Marker &&
                  keys[0].marker == MarkerStyle::Diamond,
                  "scatter3d legend: a cloud is keyed by its own marker shape");
            check(keys[0].color.r == 1.0f && keys[0].color.g == 1.0f && keys[0].color.b == 1.0f &&
                  keys[0].edge.a == 1.0f && keys[0].edge.r == 0.0f,
                  "scatter3d legend: a colormapped one is white with a black edge, since it "
                  "has no one colour a swatch could show");

            Scatter3DPlot flat = s;
            flat.colors = CowVec<double>{};
            flat.opts.color = Color::Purple;
            FigureSnapshot ff = with_cloud(flat);
            ff.axes[0].snap3d()->legend_enabled = true;
            const auto fk = collect_legend_entries(*ff.axes[0].snap3d());
            check(fk.size() == 1 && fk[0].color.r == Color::Purple.r &&
                  fk[0].color.g == Color::Purple.g && fk[0].edge.a == 0.0f,
                  "scatter3d legend: and a flat one is a swatch of the colour it actually is, "
                  "with no edge");

            // Colormapped clouds are still keyed (unlike surfaces).
            Scatter3DPlot mapped = s;
            FigureSnapshot ms = with_cloud(mapped);
            ms.axes[0].snap3d()->legend_enabled = true;
            SurfacePlot sheet;
            sheet.u = std::vector<double>{0.0, 1.0};
            sheet.v = std::vector<double>{0.0, 1.0};
            sheet.heights = std::vector<double>(4, 0.5);
            sheet.opts.colormap = true;
            sheet.opts.name = "sheet";
            ms.axes[0].snap3d()->surfaces.push_back(sheet);
            const auto both = collect_legend_entries(*ms.axes[0].snap3d());
            check(both.size() == 1 && both[0].name == "cloud",
                  "scatter3d legend: a colormapped cloud keeps its key where a colormapped "
                  "surface loses its own");

            // The two gates every keyed kind has.
            Scatter3DPlot off = s;
            off.opts.show_legend = false;
            FigureSnapshot os = with_cloud(off);
            os.axes[0].snap3d()->legend_enabled = true;
            check(collect_legend_entries(*os.axes[0].snap3d()).empty(),
                  "scatter3d legend: show_legend off drops the key without losing the text");
            Scatter3DPlot unnamed = s;
            unnamed.opts.name.clear();
            FigureSnapshot us = with_cloud(unnamed);
            us.axes[0].snap3d()->legend_enabled = true;
            check(collect_legend_entries(*us.axes[0].snap3d()).empty(),
                  "scatter3d legend: and an empty label still draws nothing");

            // MarkerStyle::None draws nothing, so no key.
            Scatter3DPlot invisible = s;
            invisible.opts.marker = MarkerStyle::None;
            FigureSnapshot is = with_cloud(invisible);
            is.axes[0].snap3d()->legend_enabled = true;
            check(collect_legend_entries(*is.axes[0].snap3d()).empty(),
                  "scatter3d legend: a marker-less series is not keyed, as a stroke-less "
                  "line is not");
        }

        // ---- Axes' own kinds before planes'; the cell carves room for both
        // decorations.
        {
            FigureSnapshot fs = with_cloud(s);
            RenderSnapshot3D* sn = fs.axes[0].snap3d();
            sn->legend_enabled = true;
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            pl.offset = 0.5;
            ScatterPlot on_plane;
            on_plane.x = std::vector<double>{0.2, 0.8};
            on_plane.y = std::vector<double>{0.2, 0.8};
            on_plane.opts.name = "on the plane";
            pl.sheet.scatters.push_back(std::move(on_plane));
            sn->planes.push_back(std::move(pl));

            const auto keys = collect_legend_entries(*sn);
            check(keys.size() == 2 && keys[0].name == "cloud" &&
                  keys[1].name == "on the plane",
                  "scatter3d legend: the axes' own cloud is keyed before a plane's series");

            const CellDecorations d = compute_cell_decorations(*sn);
            check(d.colorbars.size() == 1 && d.legend_block > 0.0f,
                  "scatter3d: one cell carries the cloud's bar and the legend at once, both "
                  "carved out of the frame");
        }
    }

    // -------------------------------------------------------------------------
    // Hovering a marker
    // -------------------------------------------------------------------------
    void test_scatter3d_hints() {
        std::printf("\n[3D: hover hints over a cloud]\n");

        using namespace sextant;

        Transform3D tf;
        tf.xmin = 0.0;
        tf.xmax = 10.0;
        tf.ymin = 0.0;
        tf.ymax = 10.0;
        tf.zmin = 0.0;
        tf.zmax = 10.0;

        const PlotRect frame{20.0f, 15.0f, 400.0f, 320.0f};
        Camera3D cam;
        cam.azimuth = -55.0;
        cam.elevation = 24.0;

        Scatter3DPlot cloud;
        cloud.x = std::vector<double>{2.0, 5.0, 8.0};
        cloud.y = std::vector<double>{2.0, 6.0, 3.0};
        cloud.z = std::vector<double>{3.0, 5.0, 7.0};
        cloud.opts.size = 14.0f;

        for (int mode = 0; mode < 2; ++mode) {
            cam.projection = mode ? Projection::Perspective : Projection::Orthographic;
            const char* what = mode ? "perspective" : "orthographic";
            const Projector3D proj(tf, cam, frame, 0.1f);

            RenderSnapshot3D snap;
            snap.scatter3d.push_back(cloud);

            // ---- The cursor on a marker's projected centre names that point.
            int named = 0;
            for (std::size_t i = 0; i < cloud.count(); ++i) {
                const Px3 q = proj.project(cloud.x[i], cloud.y[i], cloud.z[i]);
                const auto h = find_hint3d(snap, proj, q.x, q.y);
                if (!h) continue;
                char want[64];
                std::snprintf(want, sizeof(want), "x=%.4g, y=%.4g, z=%.4g",
                              cloud.x[i], cloud.y[i], cloud.z[i]);
                if (h->text == want) ++named;
            }
            check(named == 3, std::string("scatter3d hint (") + what +
                              "): the cursor on a marker names that marker's own point");

            // ---- Far from every marker: nothing (the radius is the marker's).
            {
                const Px3 q = proj.project(cloud.x[0], cloud.y[0], cloud.z[0]);
                check(!find_hint3d(snap, proj, q.x + 60.0f, q.y + 60.0f),
                      std::string("scatter3d hint (") + what +
                      "): and 60 px away from every marker, nothing answers");
            }

            // ---- `colors` adds a value line.
            {
                RenderSnapshot3D cs;
                Scatter3DPlot mapped = cloud;
                mapped.colors = std::vector<double>{11.0, 22.0, 33.0};
                cs.scatter3d.push_back(mapped);
                const Px3 q = proj.project(mapped.x[1], mapped.y[1], mapped.z[1]);
                const auto h = find_hint3d(cs, proj, q.x, q.y);
                check(h && h->text.find("\nc=22") != std::string::npos,
                      std::string("scatter3d hint (") + what +
                      "): a colormapped series reports the colour value too");
            }

            // ---- hint_labels.
            {
                RenderSnapshot3D ls;
                Scatter3DPlot labelled = cloud;
                labelled.opts.hint_labels = {"first", "second", "third"};
                ls.scatter3d.push_back(labelled);
                const Px3 q = proj.project(labelled.x[2], labelled.y[2], labelled.z[2]);
                const auto h = find_hint3d(ls, proj, q.x, q.y);
                check(h && h->text.find("third") != std::string::npos,
                      std::string("scatter3d hint (") + what +
                      "): and the caller's own label is appended");
            }
        }

        // ---- A marker behind an opaque sheet loses to it; in front it wins (as
        // in the pixels).
        {
            cam.projection = Projection::Orthographic;
            cam.azimuth = 0.0;
            cam.elevation = 0.0; // looking down +x, so x is depth
            const Projector3D proj(tf, cam, frame, 0.1f);

            RenderSnapshot3D snap;
            SurfacePlot sheet;
            sheet.orient = PlaneOrientation::YZ;
            sheet.u = std::vector<double>{0.0, 10.0};
            sheet.v = std::vector<double>{0.0, 10.0};
            sheet.heights = std::vector<double>(4, 5.0);
            snap.surfaces.push_back(sheet);

            Scatter3DPlot two;
            two.x = std::vector<double>{1.0, 9.0}; // behind the sheet, in front of it
            two.y = std::vector<double>{4.0, 6.0};
            two.z = std::vector<double>{4.0, 6.0};
            two.opts.size = 14.0f;
            snap.scatter3d.push_back(two);

            const Px3 back = proj.project(two.x[0], two.y[0], two.z[0]);
            const Px3 front = proj.project(two.x[1], two.y[1], two.z[1]);
            const auto hb = find_hint3d(snap, proj, back.x, back.y);
            const auto hf = find_hint3d(snap, proj, front.x, front.y);
            // The sheet at x = 5 reports "x=5"; "x=1" would be the hidden marker.
            check(hb && hb->text.find("x=5") != std::string::npos
                  && hb->text.find("x=1") == std::string::npos,
                  "scatter3d hint: a marker behind an opaque sheet does not answer -- the "
                  "sheet in front of it does");
            check(hf && hf->text.find("x=9") != std::string::npos,
                  "scatter3d hint: and a marker in front of the sheet answers over it");
        }
    }

    // -------------------------------------------------------------------------
    // The Data-panel table and the edit lanes
    // -------------------------------------------------------------------------
    void test_scatter3d_data_panel() {
        std::printf("\n[3D: the Data panel's cloud]\n");

        using namespace sextant;

        auto snap = [](bool mapped) {
            RenderSnapshot3D s;
            Scatter3DPlot c;
            c.x = std::vector<double>{1.0, 2.0, 3.0};
            c.y = std::vector<double>{4.0, 5.0, 6.0};
            c.z = std::vector<double>{7.0, 8.0, 9.0};
            if (mapped) c.colors = std::vector<double>{10.0, 20.0, 30.0};
            c.opts.hint_labels = {"a", "b", "c"};
            s.scatter3d.push_back(std::move(c));
            return s;
        };

        // ---- The table: the plain vector shape.
        {
            const RenderSnapshot3D flat = snap(false);
            const auto ft = collect_plot_data_tables(flat);
            check(ft.size() == 1 && ft[0].kind == PlotKind::Scatter3D &&
                  ft[0].plane_index == -1 && !ft[0].is_grid() && ft[0].heatmap == nullptr,
                  "cloud panel: a cloud is a vector table on the axes, at plane -1");
            check(ft[0].columns.size() == 3 && std::string(ft[0].columns[0].name) == "x" &&
                  std::string(ft[0].columns[2].name) == "z",
                  "cloud panel: three coordinate columns when the series is flat");

            const RenderSnapshot3D mapped = snap(true);
            const auto mt = collect_plot_data_tables(mapped);
            check(mt[0].columns.size() == 4 && std::string(mt[0].columns[3].name) == "c" &&
                  mt[0].columns[3].values[1] == 20.0,
                  "cloud panel: and a fourth for the colour dimension when it has one");

            // The caller's label when there is one.
            RenderSnapshot3D named = snap(false);
            named.scatter3d[0].opts.name = "cloud A";
            check(collect_plot_data_tables(named)[0].label == "cloud A",
                  "cloud panel: the series' own name titles its tab");
        }

        // ---- Tab order: the axes' own objects before any plane's.
        {
            RenderSnapshot3D s = snap(false);
            s.surfaces.push_back(ripple_surface());
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            pl.offset = 0.5;
            s.planes.push_back(std::move(pl));
            const auto tables = collect_plot_data_tables(s);
            const auto tabs = data_panel_tabs(tables, 1);
            check(tables.size() == 2 && tables[0].kind == PlotKind::Surface &&
                  tables[1].kind == PlotKind::Scatter3D,
                  "cloud panel: the axes' own tables are surface then cloud, in snapshot order");
            check(tabs.size() == 3 && tabs[2].table == -1 && tabs[2].plane == 0,
                  "cloud panel: and the empty plane's own tab still comes after them");
        }

        // ---- A cell edit on each of the four columns.
        {
            RenderSnapshot3D s = snap(true);
            apply_plot_data_ops(s, {
                                    PlotCellEdit{PlotKind::Scatter3D, 0, 0, 1, 99.0}, // x[1]
                                    PlotCellEdit{PlotKind::Scatter3D, 0, 1, 2, -5.0}, // y[2]
                                    PlotCellEdit{PlotKind::Scatter3D, 0, 2, 0, 0.25}, // z[0]
                                    PlotCellEdit{PlotKind::Scatter3D, 0, 3, 1, 21.0}, // colors[1]
                                    PlotCellEdit{PlotKind::Scatter3D, 0, 4, 0, 7.0}, // no such column
                                });
            const Scatter3DPlot& c = s.scatter3d[0];
            check(c.x[1] == 99.0 && c.y[2] == -5.0 && c.z[0] == 0.25 && c.colors[1] == 21.0,
                  "cloud panel: a cell edit reaches each of the four columns at the point index");
            check(c.x[0] == 1.0 && c.y[0] == 4.0 && c.z[2] == 9.0 && c.colors[0] == 10.0,
                  "cloud panel: leaving every neighbour alone");
        }

        // A flat series has no `colors`: column 3 is a no-op (not a resize).
        {
            RenderSnapshot3D s = snap(false);
            apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Scatter3D, 0, 3, 1, 4.0}});
            check(s.scatter3d[0].colors.empty(),
                  "cloud panel: the colour column writes nothing on a flat series");
        }

        // ---- The appearance lane must not carry stale `hint_labels` back.
        {
            RenderSnapshot3D s = snap(true);
            AxesEdit3D e;
            Scatter3DOptions o = s.scatter3d[0].opts;
            o.name = "renamed";
            o.marker = MarkerStyle::Triangle;
            o.depthshade = 0.6f;
            o.hint_labels.clear(); // as a panel's own local copy has them
            e.scatter3d.push_back({0, o});
            apply_axes3d_edit(s, e);

            const Scatter3DPlot& c = s.scatter3d[0];
            check(c.opts.name == "renamed" && c.opts.marker == MarkerStyle::Triangle &&
                  c.opts.depthshade == 0.6f,
                  "cloud panel: an appearance edit reaches the cloud's own options");
            check(c.opts.hint_labels.size() == 3 && c.opts.hint_labels[2] == "c",
                  "cloud panel: and the hover labels survive it, since they are data and "
                  "the appearance lane does not carry them");
            check(c.x[0] == 1.0 && c.colors[2] == 30.0,
                  "cloud panel: the data itself is untouched by an appearance edit");
        }

        // ---- Addressed by index among several clouds.
        {
            RenderSnapshot3D s = snap(false);
            s.scatter3d.push_back(s.scatter3d[0]);
            AxesEdit3D e;
            Scatter3DOptions o = s.scatter3d[1].opts;
            o.size = 33.0f;
            e.scatter3d.push_back({1, o});
            apply_axes3d_edit(s, e);
            check(s.scatter3d[1].opts.size == 33.0f && s.scatter3d[0].opts.size != 33.0f,
                  "cloud panel: an edit names one cloud among several by its index");
        }
    }
} // namespace lt
