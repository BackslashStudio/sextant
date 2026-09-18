// The raster scene: one order across kinds, and depth peeling. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    // -------------------------------------------------------------------------
    // The scene is ordered across kinds
    // -------------------------------------------------------------------------
    // Non-interpenetrating geometry, where a whole-object order is exact:
    //   - a translucent surface vs translucent bars must follow the camera;
    //   - an opaque surface must not paint over a translucent bar in front of it.
    void test_scene3d_order() {
        std::printf("\n[3D: the scene is ordered across kinds]\n");

        using namespace sextant;

        constexpr int W = 380, H = 320;

        // A flat sheet well above short bars: they never meet.
        constexpr int NU = 5, NV = 5;
        std::vector<double> gu(NU), gv(NV), flat(NU * NV, 2.0), low(NU * NV, 0.6);
        for (int i = 0; i < NU; ++i) gu[static_cast<std::size_t>(i)] = -2.0 + i;
        for (int j = 0; j < NV; ++j) gv[static_cast<std::size_t>(j)] = -2.0 + j;

        auto scene = [&](double elevation, float bar_alpha, float surf_alpha, double surf_z) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.projection = Projection::Orthographic;
            s->camera.azimuth = -50.0;
            s->camera.elevation = elevation;
            s->box_style.panes = false;
            s->grid_enabled = false;
            s->xticks_override = std::vector<Tick>{};
            s->yticks_override = std::vector<Tick>{};
            s->zticks_override = std::vector<Tick>{};
            s->xmin = -3;
            s->xmax = 3;
            s->xlim_auto = false;
            s->ymin = -3;
            s->ymax = 3;
            s->ylim_auto = false;
            s->zmin = -1;
            s->zmax = 4;
            s->zlim_auto = false;

            Bar3DPlot b;
            b.u = gu;
            b.v = gv;
            b.heights = low;
            b.u_width = b.v_width = 0.8;
            b.opts.color = {1.0f, 0.0f, 0.0f, 1.0f};
            b.opts.alpha = bar_alpha;
            b.opts.shading = 0.0f;
            s->bars3d.push_back(std::move(b));

            SurfacePlot sp;
            sp.u = gu;
            sp.v = gv;
            sp.heights = std::vector<double>(static_cast<std::size_t>(NU) * NV, surf_z);
            sp.opts.color = {0.0f, 0.0f, 1.0f, 1.0f};
            sp.opts.alpha = surf_alpha;
            sp.opts.shading = 0.0f;
            s->surfaces.push_back(std::move(sp));
            return fs;
        };
        auto render = [&](const FigureSnapshot& fs, const std::string& stem) {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
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

        // Pixels whose ray meets both objects, found with the hover hint's own
        // inverse.
        auto both_hit = [&](const FigureSnapshot& fs, std::vector<std::pair<int, int>>& out) {
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            const RenderSnapshot3D* s = fs.axes[0].snap3d();
            const PlotRect& fr = lay.cells[0].frame;
            out.clear();
            for (int y = static_cast<int>(fr.y) + 1; y < static_cast<int>(fr.y + fr.h) - 1; ++y)
                for (int x = static_cast<int>(fr.x) + 1; x < static_cast<int>(fr.x + fr.w) - 1; ++x) {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    bool bar = false, surf = false;
                    float d = 0.0f;
                    for (std::size_t k = 0; k < s->bars3d[0].count() && !bar; ++k)
                        bar = bar3d_ray_hit(s->bars3d[0], k, proj, px, py, d);
                    std::size_t sample = 0;
                    for (std::size_t k = 0; k < s->surfaces[0].cell_count() && !surf; ++k)
                        surf = surface_ray_hit(s->surfaces[0], k, proj, px, py, d, sample);
                    if (bar && surf) out.emplace_back(x, y);
                }
        };

        // ---- Two translucent objects of different kinds. Oracle: the sign of
        // eye_dir().z says whether the sheet (above the bars) is nearer. Red bars,
        // blue sheet: the dominant channel flips with the order.
        for (int above = 0; above < 2; ++above) {
            const double elev = above ? 35.0 : -35.0;
            const char* what = above ? "from above" : "from below";
            const FigureSnapshot fs = scene(elev, 0.6f, 0.6f, 2.0);
            const std::string stem = std::string("scene_order_") + (above ? "hi" : "lo");
            render(fs, stem);

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const bool sheet_nearer = lay.cells[0].box3d->proj.eye_dir().z > 0.0;
            check(sheet_nearer == (above != 0),
                  std::string("scene order: (the camera really is ") + what + ")");

            Img img;
            std::vector<std::pair<int, int>> px;
            both_hit(fs, px);
            if (!load(stem, img)) {
                check(false, "scene order: the render reaches the disk");
                continue;
            }

            int right = 0, wrong = 0;
            for (const auto& [x, y]: px) {
                const unsigned char* p = img.at(x, y);
                const bool blue_on_top = p[2] > p[0];
                if (blue_on_top == sheet_nearer) ++right;
                else ++wrong;
            }
            std::printf("  %s: %zu px carry both; %d in the near object's colour, %d not\n",
                        what, px.size(), right, wrong);
            check(px.size() > 400,
                  std::string("scene order: the two objects really do overlap on screen (")
                  + what + ")");
            // The rim is antialiasing at the silhouettes.
            check(wrong * 40 < right,
                  std::string("scene order: the nearer of a bar grid and a surface is the one on "
                      "top (") + what + ")");
        }

        // ---- An opaque surface must not paint over a translucent bar in front
        // (the opaque half must be drawn first).
        {
            const FigureSnapshot fs = scene(35.0, 0.6f, 1.0f, -0.5); // sheet *below* the bars
            render(fs, "scene_order_opaque");
            Img img;
            std::vector<std::pair<int, int>> px;
            both_hit(fs, px);
            if (load("scene_order_opaque", img)) {
                int tinted = 0, bare = 0;
                for (const auto& [x, y]: px) {
                    const unsigned char* p = img.at(x, y);
                    // A bar in front of the pure-blue sheet leaves red.
                    if (p[0] > 60) ++tinted;
                    else ++bare;
                }
                std::printf("  opaque sheet under translucent bars: %d px keep the bar, %d bare\n",
                            tinted, bare);
                check(px.size() > 400 && bare * 40 < tinted,
                      "scene order: an opaque surface does not erase a translucent bar in front of it");
            } else {
                check(false, "scene order: the opaque-sheet render reaches the disk");
            }
        }
    }

    // -------------------------------------------------------------------------
    // Depth peeling: an order per pixel
    // -------------------------------------------------------------------------
    // Two translucent sheets crossing like an X: no two-draw sequence is right.
    // Oracle: surface_ray_hit() gives the first sheet per pixel, and the composite
    // is predicted exactly over a background measured with both alphas at zero.
    void test_depth_peel_order() {
        std::printf("\n[3D: depth peeling orders per pixel (step 8)]\n");

        using namespace sextant;

        // SEXTANT_PEEL_LAYERS=0 is the negative control: same oracle, assertion
        // inverted.
        bool peeling = true;
        if (const char* env = std::getenv("SEXTANT_PEEL_LAYERS"))
            peeling = std::atoi(env) > 0;

        constexpr int W = 360, H = 300;
        constexpr float kAlpha = 0.5f;

        // A 2x2 sheet is one cell, samples (u0,v0) (u0,v1) (u1,v0) (u1,v1). Red
        // rises with u, blue falls: they cross at u = 0.5.
        const std::vector<double> gu{0.05, 0.95}, gv{0.05, 0.95};
        const std::vector<double> up{0.2, 0.2, 0.8, 0.8};
        const std::vector<double> down{0.8, 0.8, 0.2, 0.2};

        auto scene = [&](float alpha) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.projection = Projection::Orthographic;
            s->camera.azimuth = -60.0;
            s->camera.elevation = 35.0;
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

            auto sheet = [&](const std::vector<double>& h, Color c) {
                SurfacePlot sp;
                sp.u = gu;
                sp.v = gv;
                sp.heights = h;
                sp.opts.color = c;
                sp.opts.alpha = alpha;
                sp.opts.shading = 0.0f; // so the expected pixel is the colour itself
                return sp;
            };
            s->surfaces.push_back(sheet(up, {1.0f, 0.0f, 0.0f, 1.0f}));
            s->surfaces.push_back(sheet(down, {0.0f, 0.0f, 1.0f, 1.0f}));
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

        const FigureSnapshot lit = scene(kAlpha);
        const FigureSnapshot bg = scene(0.0f);
        render(lit, "peel_cross");
        render(bg, "peel_cross_bg");

        const FigureLayout lay = compute_figure_layout(lit, W, H);
        const Projector3D& proj = lay.cells[0].box3d->proj;
        const std::vector<SurfacePlot>& sheets = lit.axes[0].snap3d()->surfaces;

        // The nearest depth this sheet presents to a pixel (Px3 depth, smaller is
        // nearer), or nothing.
        auto hit = [&](const SurfacePlot& s, float px, float py, float& out) {
            bool any = false;
            for (std::size_t k = 0; k < s.cell_count(); ++k) {
                float d = 0.0f;
                std::size_t smp = 0;
                if (!surface_ray_hit(s, k, proj, px, py, d, smp)) continue;
                if (!any || d < out) out = d;
                any = true;
            }
            return any;
        };

        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load("peel_cross.png", &w, &h, &comp, 4);
        int bw = 0, bh = 0;
        unsigned char* bpx = stbi_load("peel_cross_bg.png", &bw, &bh, &comp, 4);
        check(px && bpx && w == W && h == H && bw == W && bh == H,
              "peel: both renders decode at the requested size");
        if (!px || !bpx) {
            if (px) stbi_image_free(px);
            if (bpx) stbi_image_free(bpx);
            return;
        }

        int red_front = 0, blue_front = 0, agree = 0, disagree = 0;
        double worst = 0.0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const float cx = static_cast<float>(x) + 0.5f;
                const float cy = static_cast<float>(y) + 0.5f;
                float dr = 0.0f, db = 0.0f;
                if (!hit(sheets[0], cx, cy, dr)) continue;
                if (!hit(sheets[1], cx, cy, db)) continue;
                // Skip pixels near the crossing, where the order is ambiguous.
                if (std::fabs(dr - db) < 2e-3f) continue;

                const bool red_first = dr < db;
                (red_first ? red_front : blue_front)++;

                // front over back over the measured background, exactly.
                const unsigned char* p = px + (y * W + x) * 4;
                const unsigned char* b = bpx + (y * W + x) * 4;
                const double a = kAlpha;
                bool ok = true;
                for (int ch = 0; ch < 3; ++ch) {
                    const double front = red_first
                                             ? (ch == 0 ? 1.0 : 0.0)
                                             : (ch == 2 ? 1.0 : 0.0);
                    const double back = red_first
                                            ? (ch == 2 ? 1.0 : 0.0)
                                            : (ch == 0 ? 1.0 : 0.0);
                    const double bgv = b[ch] / 255.0;
                    const double want = a * front + (1 - a) * (a * back + (1 - a) * bgv);
                    const double err = std::fabs(want * 255.0 - p[ch]);
                    worst = std::max(worst, err);
                    if (err > 4.0) ok = false;
                }
                (ok ? agree : disagree)++;
            }
        }
        stbi_image_free(px);
        stbi_image_free(bpx);

        std::printf("  overlap: %d px with red in front, %d with blue; "
                    "%d match the ray cast, %d do not (worst channel error %.1f)\n",
                    red_front, blue_front, agree, disagree, worst);

        // Both sides must occur.
        check(red_front > 500 && blue_front > 500,
              "peel: the two sheets each lead at thousands of pixels, so no whole-object order exists");
        if (peeling)
            check(disagree * 200 < agree,
                  "peel: every overlapped pixel composites in the order its own ray meets the sheets");
        else
            check(disagree > red_front / 2 && disagree > blue_front / 2,
                  "peel (control): the whole-object path gets a whole side of the crossing wrong");
    }

    // -------------------------------------------------------------------------
    // PngExportOptions::peel_layers
    // -------------------------------------------------------------------------
    // A sheet through translucent bars must change when the layer count is raised
    // (the option reaches the renderer); two crossing sheets (few layers) must not
    // change at all.
    void test_png_peel_option() {
        std::printf("\n[3D: the peel-layer count as an export option]\n");

        // Whole-file compare (png_writer is deterministic).
        auto read_file_bytes = [](const std::string& p) {
            std::ifstream f(p, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(f),
                               std::istreambuf_iterator<char>());
        };

        using namespace sextant;

        constexpr int W = 380, H = 320;

        bool peeling = true;
        if (const char* env = std::getenv("SEXTANT_PEEL_LAYERS"))
            peeling = std::atoi(env) > 0;

        constexpr int NU = 11, NV = 11;
        std::vector<double> gx(NU), gy(NV), ripple(NU * NV), midway(NU * NV);
        for (int i = 0; i < NU; ++i) gx[static_cast<std::size_t>(i)] = -3.0 + 6.0 * i / (NU - 1);
        for (int j = 0; j < NV; ++j) gy[static_cast<std::size_t>(j)] = -3.0 + 6.0 * j / (NV - 1);
        for (int i = 0; i < NU; ++i)
            for (int j = 0; j < NV; ++j) {
                const double r = std::hypot(gx[static_cast<std::size_t>(i)],
                                            gy[static_cast<std::size_t>(j)]);
                const std::size_t k = static_cast<std::size_t>(i * NV + j);
                ripple[k] = 1.6 * std::exp(-r / 2.0) * std::cos(r * 1.7);
                midway[k] = -1.5 + 0.5 * ripple[k];
            }

        auto base3d = [] {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.projection = Projection::Orthographic;
            s->camera.azimuth = -60.0;
            s->camera.elevation = 30.0;
            s->box_style.panes = false;
            s->grid_enabled = false;
            s->xticks_override = std::vector<Tick>{};
            s->yticks_override = std::vector<Tick>{};
            s->zticks_override = std::vector<Tick>{};
            return fs;
        };

        // The deep scene: the sheet at half the bars' height, inside every bar.
        FigureSnapshot deep = base3d(); {
            RenderSnapshot3D* s = deep.axes[0].snap3d();
            Bar3DPlot b;
            b.u = gx;
            b.v = gy;
            b.heights = ripple;
            b.u_width = b.v_width = 0.42;
            b.opts.alpha = 0.5f;
            b.opts.bottom = -1.5;
            s->bars3d.push_back(std::move(b));
            SurfacePlot sp;
            sp.u = gx;
            sp.v = gy;
            sp.heights = midway;
            sp.opts.alpha = 0.55f;
            s->surfaces.push_back(std::move(sp));
        }

        // The shallow one: two crossing sheets, at most three layers.
        FigureSnapshot shallow = base3d(); {
            RenderSnapshot3D* s = shallow.axes[0].snap3d();
            for (int which = 0; which < 2; ++which) {
                std::vector<double> h(ripple.size());
                for (std::size_t k = 0; k < ripple.size(); ++k)
                    h[k] = which ? -ripple[k] : ripple[k];
                SurfacePlot sp;
                sp.u = gx;
                sp.v = gy;
                sp.heights = h;
                sp.opts.alpha = 0.55f;
                s->surfaces.push_back(std::move(sp));
            }
        }

        auto render = [&](const FigureSnapshot& fs, int layers, const std::string& stem) {
            GLContext ctx({
                .width = W, .height = H,
                .title = "layout_test", .visible = false
            });
            NvgRenderer nvg(ctx.nvg());
            DataRenderer data_r;
            export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1, layers);
            return read_file_bytes(stem + ".png");
        };

        auto px_diff = [](const std::string& a, const std::string& b) {
            return png_pixel_diff(a + ".png", b + ".png").px;
        };

        // Four against thirty-two (explicit, so this doesn't depend on the
        // default).
        const std::string a = render(deep, 4, "peel_opt_deep_4");
        const std::string b = render(deep, 32, "peel_opt_deep_32");
        const std::string c = render(shallow, 0, "peel_opt_shallow_auto");
        const std::string d = render(shallow, 32, "peel_opt_shallow_32");
        // What this scene requires, pinned (the default is deliberately lower).
        const std::string e = render(deep, 12, "peel_opt_deep_12");
        // The same export again: the comparisons above assume a renderer that
        // repeats itself exactly.
        const std::string b2 = render(deep, 32, "peel_opt_deep_32_again");

        std::printf("  bars+sheet   4 vs 32 layers: %s; 12 vs 32: %s; 32 twice: %s\n",
                    a == b ? "identical" : "different",
                    e == b ? "identical" : "different",
                    b2 == b ? "identical" : "different");
        std::printf("  two sheets   default vs 32 layers: %s\n",
                    c == d ? "identical" : "different");

        // Apple's software renderer (the macOS CI runner) does not repeat a
        // translucent export exactly (v1.0 step 21.1: 292 px between two
        // identical exports of the deep scene, against ~6800 for 4 vs 32 layers).
        // There, "identical" becomes "small next to what the layer count changes".
        const bool noisy = !renderer_repeats_exactly();

        check(!a.empty() && !c.empty(), "peel option: the exports produced files");
        if (peeling && noisy) {
            const int noise = px_diff("peel_opt_deep_32", "peel_opt_deep_32_again");
            const int deep4 = px_diff("peel_opt_deep_4", "peel_opt_deep_32");
            const int deep12 = px_diff("peel_opt_deep_12", "peel_opt_deep_32");
            const int sheets = px_diff("peel_opt_shallow_auto", "peel_opt_shallow_32");
            std::printf("  %s: px differing, 32 twice %d, 4 vs 32 %d, 12 vs 32 %d, "
                        "two sheets %d\n", gl_renderer().c_str(), noise, deep4, deep12, sheets);
            check(noise >= 0 && deep4 > 5 * noise,
                  "peel option (noisy renderer): raising the count changes a scene that "
                  "needs more layers, far beyond run-to-run noise");
            check(sheets >= 0 && 10 * sheets < deep4,
                  "peel option (noisy renderer): and changes a scene that does not only by noise");
            check(deep12 >= 0 && 10 * deep12 < deep4,
                  "peel option (noisy renderer): twelve layers is what a sheet through a bar "
                  "grid needs, up to noise");
        } else if (peeling) {
            check(b2 == b, "peel option: the same export twice is byte-identical");
            check(a != b,
                  "peel option: raising the count changes a scene that needs more layers");
            check(c == d,
                  "peel option: and changes nothing in a scene that does not");
            check(e == b,
                  "peel option: twelve layers is what a sheet through a bar grid needs");
        } else {
            // With peeling off, the override must not re-enable it.
            check(b2 == b, "peel option (control): the same export twice is byte-identical");
            check(a == b && c == d && e == b,
                  "peel option (control): with peeling off the count is inert");
        }
    }
} // namespace lt
