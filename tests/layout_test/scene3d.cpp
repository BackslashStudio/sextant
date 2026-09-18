// The raster scene: one order across kinds, and depth peeling per fragment.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {

// -------------------------------------------------------------------------
// Post-step-7d: the scene is ordered across kinds, not by the order of four calls
// -------------------------------------------------------------------------
// Two defects, both found by making a gallery cell's bars translucent, and
// neither of them the interpenetration case the deferred split is for:
//
//   - a translucent surface always painted after a translucent bar grid,
//     because each per-kind draw sorted only its own objects;
//   - an *opaque* surface was drawn after translucent bars, which write no
//     depth, so it painted straight over a translucent bar in front of it.
//
// Both are checked here against geometry that deliberately does **not**
// interpenetrate, because that is the whole point: a whole-object order is
// exact for such a pair, and the old code got it wrong anyway.
void test_scene3d_order() {
    std::printf("\n[3D: the scene is ordered across kinds]\n");

    using namespace sextant;

    constexpr int W = 380, H = 320;

    // A flat sheet well above a grid of short bars: nowhere do the two meet,
    // so exactly one of them is in front at every pixel and no per-fragment
    // machinery is needed to say which.
    constexpr int NU = 5, NV = 5;
    std::vector<double> gu(NU), gv(NV), flat(NU * NV, 2.0), low(NU * NV, 0.6);
    for (int i = 0; i < NU; ++i) gu[static_cast<std::size_t>(i)] = -2.0 + i;
    for (int j = 0; j < NV; ++j) gv[static_cast<std::size_t>(j)] = -2.0 + j;

    auto scene = [&](double elevation, float bar_alpha, float surf_alpha, double surf_z) {
        FigureSnapshot fs = make_snapshot3d(1, 1, 1);
        RenderSnapshot3D* s = fs.axes[0].snap3d();
        s->camera.projection = Projection::Orthographic;
        s->camera.azimuth   = -50.0;
        s->camera.elevation = elevation;
        s->box_style.panes = false;
        s->grid_enabled = false;
        s->xticks_override = std::vector<Tick>{};
        s->yticks_override = std::vector<Tick>{};
        s->zticks_override = std::vector<Tick>{};
        s->xmin = -3; s->xmax = 3; s->xlim_auto = false;
        s->ymin = -3; s->ymax = 3; s->ylim_auto = false;
        s->zmin = -1; s->zmax = 4; s->zlim_auto = false;

        Bar3DPlot b;
        b.u = gu; b.v = gv; b.heights = low;
        b.u_width = b.v_width = 0.8;
        b.opts.color   = { 1.0f, 0.0f, 0.0f, 1.0f };
        b.opts.alpha   = bar_alpha;
        b.opts.shading = 0.0f;
        s->bars3d.push_back(std::move(b));

        SurfacePlot sp;
        sp.u = gu; sp.v = gv;
        sp.heights = std::vector<double>(static_cast<std::size_t>(NU) * NV, surf_z);
        sp.opts.color   = { 0.0f, 0.0f, 1.0f, 1.0f };
        sp.opts.alpha   = surf_alpha;
        sp.opts.shading = 0.0f;
        s->surfaces.push_back(std::move(sp));
        return fs;
    };
    auto render = [&](const FigureSnapshot& fs, const std::string& stem) {
        GLContext ctx({ .width = W, .height = H, .title = "layout_test", .visible = false });
        NvgRenderer  nvg(ctx.nvg());
        DataRenderer data_r;
        export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1);
    };
    struct Img {
        unsigned char* px = nullptr; int w = 0, h = 0;
        ~Img() { if (px) stbi_image_free(px); }
        const unsigned char* at(int x, int y) const { return px + (y * w + x) * 4; }
    };
    auto load = [](const std::string& stem, Img& im) {
        int comp = 0;
        im.px = stbi_load((stem + ".png").c_str(), &im.w, &im.h, &comp, 4);
        return im.px != nullptr;
    };

    // Every pixel the cursor's ray meets *both* objects at. The bar test is
    // over every bar and the surface test over every cell, which is what
    // find_hint3d() does -- so "both are there" is answered by the same
    // inverse the tooltip uses rather than by looking at the picture.
    auto both_hit = [&](const FigureSnapshot& fs, std::vector<std::pair<int,int>>& out) {
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

    // ---- The reported case: two translucent objects of different kinds.
    //
    // The oracle is the *camera*, not a distance function: the sheet is above
    // the bars in the box, so it is nearer whenever the eye is above and
    // farther whenever the eye is below. `eye_dir()` points from the box
    // toward the camera, so the sign of its z component says which, and that
    // is a fact about the basis rather than about the comparator being tested.
    //
    // Red bars and a blue sheet, so which one is on top is a question about a
    // single channel and needs no blend arithmetic: the *set* of layers is the
    // same in both orders, only the order differs, so the dominant channel
    // flips exactly when the order does.
    for (int above = 0; above < 2; ++above) {
        const double elev = above ? 35.0 : -35.0;
        const char*  what = above ? "from above" : "from below";
        const FigureSnapshot fs = scene(elev, 0.6f, 0.6f, 2.0);
        const std::string stem = std::string("scene_order_") + (above ? "hi" : "lo");
        render(fs, stem);

        const FigureLayout lay = compute_figure_layout(fs, W, H);
        const bool sheet_nearer = lay.cells[0].box3d->proj.eye_dir().z > 0.0;
        check(sheet_nearer == (above != 0),
              std::string("scene order: (the camera really is ") + what + ")");

        Img img;
        std::vector<std::pair<int,int>> px;
        both_hit(fs, px);
        if (!load(stem, img)) { check(false, "scene order: the render reaches the disk"); continue; }

        int right = 0, wrong = 0;
        for (const auto& [x, y] : px) {
            const unsigned char* p = img.at(x, y);
            const bool blue_on_top = p[2] > p[0];
            if (blue_on_top == sheet_nearer) ++right; else ++wrong;
        }
        std::printf("  %s: %zu px carry both; %d in the near object's colour, %d not\n",
                    what, px.size(), right, wrong);
        check(px.size() > 400,
              std::string("scene order: the two objects really do overlap on screen (")
                  + what + ")");
        // The rim is the two silhouettes' antialiasing, where a pixel is part
        // one object and part background.
        check(wrong * 40 < right,
              std::string("scene order: the nearer of a bar grid and a surface is the one on "
                          "top (") + what + ")");
    }

    // ---- The second defect: an *opaque* surface must not paint over a
    // translucent bar in front of it. Nothing sorts these -- the depth buffer
    // does -- but only if the opaque half of the scene is drawn first. When it
    // was not, the surface passed the depth test the translucent bars had
    // deliberately not written and erased them.
    {
        const FigureSnapshot fs = scene(35.0, 0.6f, 1.0f, -0.5);   // sheet *below* the bars
        render(fs, "scene_order_opaque");
        Img img;
        std::vector<std::pair<int,int>> px;
        both_hit(fs, px);
        if (load("scene_order_opaque", img)) {
            int tinted = 0, bare = 0;
            for (const auto& [x, y] : px) {
                const unsigned char* p = img.at(x, y);
                // The opaque sheet's own colour is pure blue. A bar in front of
                // it must leave red in the pixel.
                if (p[0] > 60) ++tinted; else ++bare;
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
// Depth peeling: an order per pixel, in a scene that has none per object
// -------------------------------------------------------------------------
// Two flat translucent sheets crossing like an X. On one side of the crossing
// the red one is in front, on the other the blue one is -- so *no* sequence of
// two draws is right, whatever key it is sorted by, and this is the smallest
// scene that says so. The whole-object path (SEXTANT_PEEL_LAYERS=0) draws one
// sheet entirely before the other and therefore gets one of the two sides
// wrong; step 8 gets both, because the order it uses is the depth test's.
//
// The oracle is a ray cast and not a comparator (§13's rule): for each pixel
// the test asks surface_ray_hit() -- the hover hint's own inverse, which knows
// nothing about how anything was drawn -- which sheet the pixel's ray meets
// first, and then predicts the composite from that alone. The background is
// measured rather than assumed, by rendering the same figure with both alphas
// at zero, so the prediction is an exact value and not a dominance test.
void test_depth_peel_order() {
    std::printf("\n[3D: depth peeling orders per pixel (step 8)]\n");

    using namespace sextant;

    // SEXTANT_PEEL_LAYERS=0 is the negative control rather than a skip: the
    // same scene, the same oracle, and the assertion inverted. A test that
    // only ever runs against the path it is testing cannot tell a correct
    // renderer from a lenient check, and this one is worth being sure of --
    // it is the whole claim of the step.
    bool peeling = true;
    if (const char* env = std::getenv("SEXTANT_PEEL_LAYERS"))
        peeling = std::atoi(env) > 0;

    constexpr int W = 360, H = 300;
    constexpr float kAlpha = 0.5f;

    // A 2x2 sheet is one cell, and its four samples are row-major with u
    // major: (u0,v0) (u0,v1) (u1,v0) (u1,v1). Red rises with u, blue falls,
    // so they cross at u = 0.5 and neither is wholly in front of the other.
    const std::vector<double> gu{ 0.05, 0.95 }, gv{ 0.05, 0.95 };
    const std::vector<double> up  { 0.2, 0.2, 0.8, 0.8 };
    const std::vector<double> down{ 0.8, 0.8, 0.2, 0.2 };

    auto scene = [&](float alpha) {
        FigureSnapshot fs = make_snapshot3d(1, 1, 1);
        RenderSnapshot3D* s = fs.axes[0].snap3d();
        s->camera.projection = Projection::Orthographic;
        s->camera.azimuth   = -60.0;
        s->camera.elevation = 35.0;
        s->box_style.panes = false;
        s->grid_enabled = false;
        s->xticks_override = std::vector<Tick>{};
        s->yticks_override = std::vector<Tick>{};
        s->zticks_override = std::vector<Tick>{};
        s->xmin = 0; s->xmax = 1; s->xlim_auto = false;
        s->ymin = 0; s->ymax = 1; s->ylim_auto = false;
        s->zmin = 0; s->zmax = 1; s->zlim_auto = false;

        auto sheet = [&](const std::vector<double>& h, Color c) {
            SurfacePlot sp;
            sp.u = gu; sp.v = gv; sp.heights = h;
            sp.opts.color   = c;
            sp.opts.alpha   = alpha;
            sp.opts.shading = 0.0f;   // so the expected pixel is the colour itself
            return sp;
        };
        s->surfaces.push_back(sheet(up,   { 1.0f, 0.0f, 0.0f, 1.0f }));
        s->surfaces.push_back(sheet(down, { 0.0f, 0.0f, 1.0f, 1.0f }));
        return fs;
    };

    auto render = [&](const FigureSnapshot& fs, const std::string& stem) {
        GLContext ctx({ .width = W, .height = H,
                        .title = "layout_test", .visible = false });
        NvgRenderer  nvg(ctx.nvg());
        DataRenderer data_r;
        export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1);
    };

    const FigureSnapshot lit = scene(kAlpha);
    const FigureSnapshot bg  = scene(0.0f);
    render(lit, "peel_cross");
    render(bg,  "peel_cross_bg");

    const FigureLayout lay = compute_figure_layout(lit, W, H);
    const Projector3D& proj = lay.cells[0].box3d->proj;
    const std::vector<SurfacePlot>& sheets = lit.axes[0].snap3d()->surfaces;

    // The nearest depth this sheet presents to a pixel, or nothing. `depth` is
    // Px3's own quantity, where smaller is nearer -- the same quantity
    // find_hint3d() picks its front-most candidate by.
    auto hit = [&](const SurfacePlot& s, float px, float py, float& out) {
        bool any = false;
        for (std::size_t k = 0; k < s.cell_count(); ++k) {
            float d = 0.0f; std::size_t smp = 0;
            if (!surface_ray_hit(s, k, proj, px, py, d, smp)) continue;
            if (!any || d < out) out = d;
            any = true;
        }
        return any;
    };

    int w = 0, h = 0, comp = 0;
    unsigned char* px  = stbi_load("peel_cross.png", &w, &h, &comp, 4);
    int bw = 0, bh = 0;
    unsigned char* bpx = stbi_load("peel_cross_bg.png", &bw, &bh, &comp, 4);
    check(px && bpx && w == W && h == H && bw == W && bh == H,
          "peel: both renders decode at the requested size");
    if (!px || !bpx) { if (px) stbi_image_free(px); if (bpx) stbi_image_free(bpx); return; }

    int red_front = 0, blue_front = 0, agree = 0, disagree = 0;
    double worst = 0.0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float cx = static_cast<float>(x) + 0.5f;
            const float cy = static_cast<float>(y) + 0.5f;
            float dr = 0.0f, db = 0.0f;
            if (!hit(sheets[0], cx, cy, dr)) continue;
            if (!hit(sheets[1], cx, cy, db)) continue;
            // Near the crossing line the two depths are equal and the pixel is
            // a blend of both orders; a pixel is only evidence where the ray
            // is unambiguous about which it meets first.
            if (std::fabs(dr - db) < 2e-3f) continue;

            const bool red_first = dr < db;
            (red_first ? red_front : blue_front)++;

            // front over back over the measured background, exactly.
            const unsigned char* p = px  + (y * W + x) * 4;
            const unsigned char* b = bpx + (y * W + x) * 4;
            const double a = kAlpha;
            bool ok = true;
            for (int ch = 0; ch < 3; ++ch) {
                const double front = red_first ? (ch == 0 ? 1.0 : 0.0)
                                               : (ch == 2 ? 1.0 : 0.0);
                const double back  = red_first ? (ch == 2 ? 1.0 : 0.0)
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

    // Both sides have to exist, or the scene is not the scene this test is
    // about and passing it would mean nothing.
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
// The raster path's own bound, as an option (PngExportOptions::peel_layers)
// -------------------------------------------------------------------------
// Depth peeling takes eight layers by default and stops early once a pass
// peels nothing, so on almost every scene the count is invisible. It is not
// invisible on a scene where a ray crosses more translucent surfaces than
// that: a translucent bar is two layers on its own, so a sheet threaded
// through a grid of them runs out, and what the eighth layer does not reach is
// simply missing from the picture.
//
// Two scenes, and the pair is the test. The bar grid must *change* when the
// count is raised -- otherwise the option does not reach the renderer -- and
// the two crossing sheets, which never need more than a handful of layers,
// must not change at all, because an option that perturbs a scene it should
// not reach is not a bound, it is a bug.
void test_png_peel_option() {
    std::printf("\n[3D: the peel-layer count as an export option]\n");

    // Whole-file compare rather than a pixel oracle: what is being tested is
    // that the option reaches the renderer at all, and png_writer's output is
    // deterministic, so "these two files differ" is exactly the question.
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
        s->camera.azimuth = -60.0; s->camera.elevation = 30.0;
        s->box_style.panes = false; s->grid_enabled = false;
        s->xticks_override = std::vector<Tick>{};
        s->yticks_override = std::vector<Tick>{};
        s->zticks_override = std::vector<Tick>{};
        return fs;
    };

    // The deep scene: `test_translucent3d`'s sixth cell in miniature -- the
    // sheet is half each bar's height, so it is inside every one of them.
    FigureSnapshot deep = base3d();
    {
        RenderSnapshot3D* s = deep.axes[0].snap3d();
        Bar3DPlot b;
        b.u = gx; b.v = gy; b.heights = ripple;
        b.u_width = b.v_width = 0.42;
        b.opts.alpha = 0.5f; b.opts.bottom = -1.5;
        s->bars3d.push_back(std::move(b));
        SurfacePlot sp;
        sp.u = gx; sp.v = gy; sp.heights = midway;
        sp.opts.alpha = 0.55f;
        s->surfaces.push_back(std::move(sp));
    }

    // The shallow one: two sheets crossing, three layers at the very most.
    FigureSnapshot shallow = base3d();
    {
        RenderSnapshot3D* s = shallow.axes[0].snap3d();
        for (int which = 0; which < 2; ++which) {
            std::vector<double> h(ripple.size());
            for (std::size_t k = 0; k < ripple.size(); ++k)
                h[k] = which ? -ripple[k] : ripple[k];
            SurfacePlot sp;
            sp.u = gx; sp.v = gy; sp.heights = h;
            sp.opts.alpha = 0.55f;
            s->surfaces.push_back(std::move(sp));
        }
    }

    auto render = [&](const FigureSnapshot& fs, int layers, const std::string& stem) {
        GLContext ctx({ .width = W, .height = H,
                        .title = "layout_test", .visible = false });
        NvgRenderer  nvg(ctx.nvg());
        DataRenderer data_r;
        export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1, layers);
        return read_file_bytes(stem + ".png");
    };

    // Four against thirty-two, and *not* the default against thirty-two.
    //
    // It was the latter, and that quietly made this a test of the default as
    // well as of the option: it could only pass while the default was too low
    // for this scene to converge at, so it would break the day the default
    // became sufficient. What the assertion wants is a low *explicit* count,
    // which is a fact about the option and about nothing else.
    const std::string a = render(deep, 4,  "peel_opt_deep_4");
    const std::string b = render(deep, 32, "peel_opt_deep_32");
    const std::string c = render(shallow, 0,  "peel_opt_shallow_auto");
    const std::string d = render(shallow, 32, "peel_opt_shallow_32");
    // And what this scene *requires*, pinned so it stays a measured number.
    // The default is deliberately below it -- eight is a cost compromise, not
    // a sufficient count (see `peel_layer_default()`) -- so this cannot be
    // asked of `layers = 0` without asserting the compromise away.
    const std::string e = render(deep, 12, "peel_opt_deep_12");

    std::printf("  bars+sheet   4 vs 32 layers: %s; 12 vs 32: %s\n",
                a == b ? "identical" : "different",
                e == b ? "identical" : "different");
    std::printf("  two sheets   default vs 32 layers: %s\n",
                c == d ? "identical" : "different");

    check(!a.empty() && !c.empty(), "peel option: the exports produced files");
    if (peeling) {
        check(a != b,
              "peel option: raising the count changes a scene that needs more layers");
        check(c == d,
              "peel option: and changes nothing in a scene that does not");
        check(e == b,
              "peel option: twelve layers is what a sheet through a bar grid needs");
    } else {
        // Peeling off is the whole-object order, which has no layers to take.
        // The override must not resurrect it -- that control is what every
        // step-8 assertion is read against.
        check(a == b && c == d && e == b,
              "peel option (control): with peeling off the count is inert");
    }
}

}  // namespace lt
