// surface: ingest, cell order, the render, hints and the Data panel.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {

// -------------------------------------------------------------------------
// Step 7c: surface() -- the type, the ingest, and the geometry both paths read
// -------------------------------------------------------------------------
void test_surface_ingest() {
    std::printf("\n[3D: surface ingest, auto-scale and cells]\n");

    using namespace sextant;

    auto threw = [](auto&& fn) {
        try { fn(); return false; } catch (const std::invalid_argument&) { return true; }
    };

    const std::vector<double> u{ 0.0, 2.0, 4.0 };
    const std::vector<double> v{ 0.0, 1.0 };
    const std::vector<double> h{ 1, 2, 3, 4, 5, 6 };

    auto fig = Figure::create({ .width = 300, .height = 240 });
    auto ax  = fig->add_subplot3d(1, 1, 1);
    check(threw([&]{ ax->surface(PlaneOrientation::XY, u, v, { h.data(), 5 }); }),
          "surface: heights that are not |u| x |v| throw at ingest");
    const std::vector<double> nan_h{ 1, 2, 3, std::numeric_limits<double>::quiet_NaN(), 5, 6 };
    check(threw([&]{ ax->surface(PlaneOrientation::XY, u, v, nan_h); }),
          "surface: a non-finite height throws, once, rather than reaching the vertex buffer");
    // The one condition bar3d does not have: cells live *between* samples.
    const std::vector<double> one{ 0.0 };
    check(threw([&]{ ax->surface(PlaneOrientation::XY, one, v, { h.data(), 2 }); }),
          "surface: a grid with one row has vertices and no cells, and says so");
    check(!threw([&]{ ax->surface(PlaneOrientation::XY, u, v, h); }),
          "surface: a 3 x 2 grid is accepted");

    SurfacePlot s;
    s.u = u; s.v = v; s.heights = h;
    check(s.cell_rows() == 2 && s.cell_cols() == 1 && s.cell_count() == 2,
          "surface: |u| x |v| samples make (|u|-1) x (|v|-1) cells");

    // Auto-scale. A surface has no footprint to widen it -- the samples *are*
    // the extent -- which is the one place it differs from a bar grid, and the
    // difference is only visible from here.
    {
        const DataBounds3D b = auto_scale3d({}, {}, { s }, {}, {}, {}, 0.0);
        check(b.xmin == 0.0 && b.xmax == 4.0 && b.ymin == 0.0 && b.ymax == 1.0,
              "surface: its extent is its samples, with no footprint added");
        check(b.zmin == 1.0 && b.zmax == 6.0,
              "surface: and its heights are its own range, not stretched to include zero");

        SurfacePlot yz = s;
        yz.orient = PlaneOrientation::YZ;
        const DataBounds3D r = auto_scale3d({}, {}, { yz }, {}, {}, {}, 0.0);
        check(r.ymin == 0.0 && r.ymax == 4.0 && r.zmin == 0.0 && r.zmax == 1.0 &&
              r.xmin == 1.0 && r.xmax == 6.0,
              "surface: an orientation moves u, v and the heights together, through one Axis3Map");
    }

    // The colormap range. `vmin == vmax` means the surface's own range, which
    // is the departure from HeatmapOptions the option documents.
    {
        double lo = 0.0, hi = 0.0;
        surface_value_range(s, lo, hi);
        check(lo == 1.0 && hi == 6.0, "surface: an empty vmin/vmax interval means the data's own range");
        SurfacePlot fixed = s;
        fixed.opts.vmin = -10.0f; fixed.opts.vmax = 10.0f;
        surface_value_range(fixed, lo, hi);
        check(lo == -10.0 && hi == 10.0, "surface: and a stated one is used as stated");
    }

    // One cell: the ring, and the value the colormap is sampled at.
    {
        const Transform3D tf{ 0, 4, 0, 1, 0, 8, BoxAspect{ 1, 1, 1 } };
        SurfaceCell c;
        surface_cell(s, 0, 0, tf, c);
        // Samples (0,0) (1,0) (1,1) (0,1) = heights 1, 3, 4, 2.
        check(c.p[0].x == 0.0 && c.p[0].y == 0.0 && c.p[0].z == 1.0 &&
              c.p[1].x == 2.0 && c.p[1].y == 0.0 && c.p[1].z == 3.0 &&
              c.p[2].x == 2.0 && c.p[2].y == 1.0 && c.p[2].z == 4.0 &&
              c.p[3].x == 0.0 && c.p[3].y == 1.0 && c.p[3].z == 2.0,
              "surface: a cell's four corners are its own samples, in ring order");
        check(std::fabs(c.value - 2.5) < 1e-12,
              "surface: and the colormap samples the cell's mean height, not a corner's");
    }

    // Shading takes |n.l|, not max(0, n.l) -- a sheet has two sides and no
    // outside, so the far side of a fold must not go black. Asserted by
    // building the *same* cell with its winding reversed, which is the only
    // thing that changes the normal's sign: the shade may not move.
    // A *flat* sheet, because that is what isolates the winding: reversing v
    // on a bumpy grid mirrors the geometry as well, and then the two normals
    // differ by more than a sign. Flat, the two cells are the same rectangle
    // in space traversed the other way round, so the normal is exactly negated
    // and nothing else moves. Its box normal is +/- z against a light whose z
    // component is positive, so max(0, n.l) would take one of the two to
    // 1 - shading while |n.l| leaves both at the same value.
    {
        const Transform3D tf{ 0, 4, 0, 1, 0, 8, BoxAspect{ 1, 1, 1 } };
        SurfacePlot flat;
        flat.u = u; flat.v = v;
        flat.heights = std::vector<double>(6, 3.0);
        SurfacePlot flipped = flat;
        flipped.v = std::vector<double>{ 1.0, 0.0 };
        SurfaceCell a, b;
        surface_cell(flat,    0, 0, tf, a);
        surface_cell(flipped, 0, 0, tf, b);
        check(std::fabs(a.shade - b.shade) < 1e-6,
              "surface: a cell's shade does not depend on which way its normal happens to point");
        check(a.shade > 0.85f && a.shade <= 1.0f,
              "surface: and a sheet facing the light is lit, from either side of it");
    }

    // Colour: flat or colormapped, shaded, with the plot's alpha.
    {
        SurfaceCell c{};
        c.shade = 0.5f;
        c.value = 3.5;
        SurfacePlot flat = s;
        flat.opts.color = { 1.0f, 0.0f, 0.0f, 1.0f };
        flat.opts.alpha = 0.5f;
        const Color fc = surface_cell_color(flat, c, 1.0, 6.0);
        check(std::fabs(fc.r - 0.5f) < 1e-6 && fc.g == 0.0f && std::fabs(fc.a - 0.5f) < 1e-6,
              "surface: a flat cell's colour is the plot's, shaded, at the plot's alpha");

        SurfacePlot mapped = s;
        mapped.opts.colormap = true;
        mapped.opts.cmap = Colormap::Viridis;
        const Color mc = surface_cell_color(mapped, c, 1.0, 6.0);
        const uint8_t* lut = colormaps::get(Colormap::Viridis)
                           + static_cast<int>(((3.5 - 1.0) / 5.0) * 255.0) * 4;
        check(std::fabs(mc.r - lut[0] / 255.0f * 0.5f) < 1e-5 &&
              std::fabs(mc.g - lut[1] / 255.0f * 0.5f) < 1e-5,
              "surface: a colormapped one is the LUT entry its own height names, shaded the same way");
    }
}

// The cell order, checked against the geometry rather than against the key
// that produced it -- §13's rule, and the family of bug that shipped twice.
void test_surface_draw_order() {
    std::printf("\n[3D: a surface's cells, back to front]\n");

    using namespace sextant;

    constexpr int NU = 6, NV = 5;
    std::vector<double> u(NU), v(NV), h(NU * NV);
    for (int i = 0; i < NU; ++i) u[static_cast<std::size_t>(i)] = -2.0 + i;
    for (int j = 0; j < NV; ++j) v[static_cast<std::size_t>(j)] = -2.0 + j;
    for (int i = 0; i < NU; ++i)
        for (int j = 0; j < NV; ++j)
            h[static_cast<std::size_t>(i * NV + j)] =
                std::sin(u[static_cast<std::size_t>(i)]) * std::cos(v[static_cast<std::size_t>(j)]);

    SurfacePlot s;
    s.u = u; s.v = v; s.heights = h;

    const PlotRect frame{ 0.0f, 0.0f, 300.0f, 240.0f };
    const Transform3D tf{ -2, 3, -2, 2, -1.5, 1.5, BoxAspect{ 1, 1, 1 } };

    for (int cam_i = 0; cam_i < 4; ++cam_i) {
        Camera3D cam;
        cam.azimuth   = -140.0 + 70.0 * cam_i;
        cam.elevation = 15.0 + 12.0 * cam_i;
        const Projector3D proj(tf, cam, frame, 0.0f);

        std::vector<std::size_t> order;
        surface_draw_order(s, proj, order);
        check(order.size() == s.cell_count(), "surface order: every cell, once");

        // The oracle is a ray cast, not the sort key: for each *pair* of cells
        // whose footprints a common pixel's ray passes through, the one the ray
        // reaches first must be drawn later. Sampling the shared grid line
        // between neighbours is enough -- it is the only place two cells can
        // occlude each other at all, since they are separated by it.
        int checked = 0, wrong = 0;
        std::vector<std::size_t> pos(s.cell_count());
        for (std::size_t k = 0; k < order.size(); ++k) pos[order[k]] = k;
        const Vec3 eye = eye_coord(proj);
        for (std::size_t i = 0; i + 1 < s.cell_rows(); ++i) {
            for (std::size_t j = 0; j + 1 < s.cell_cols(); ++j) {
                const std::size_t a = i * s.cell_cols() + j;
                for (const std::size_t b : { a + 1, a + s.cell_cols() }) {
                    // Which cell's own grid corner is nearer the eye, in box
                    // space -- a distance, not the |component| the comparator
                    // sorts on.
                    auto corner = [&](std::size_t k) {
                        const std::size_t ci = k / s.cell_cols(), cj = k % s.cell_cols();
                        return tf.to_box(s.u[ci], s.v[cj], 0.0);
                    };
                    const double da = length(corner(a) - eye);
                    const double db = length(corner(b) - eye);
                    if (std::fabs(da - db) < 1e-9) continue;
                    ++checked;
                    if ((da < db) != (pos[a] > pos[b])) ++wrong;
                }
            }
        }
        check(checked > 20 && wrong == 0,
              "surface order: of two neighbouring cells the nearer is always drawn later");
    }
}

// -------------------------------------------------------------------------
// Step 7c: a surface, rendered
// -------------------------------------------------------------------------
// The headline is the same shape as the plane's: each *cell's* centre,
// projected by the CPU projector, must carry that cell's own colour in the
// GPU's output. Counting colours would pass on a surface placed anywhere.
// -------------------------------------------------------------------------
void test_surface_render() {
    std::printf("\n[3D: a surface, rendered]\n");

    using namespace sextant;

    constexpr int W = 400, H = 340;
    constexpr int NU = 5, NV = 4;

    // Distinct heights, so every cell has its own colormap entry and a pixel
    // says which cell it came from. Monotone in both directions, which also
    // keeps the surface single-valued from the camera below.
    std::vector<double> gu(NU), gv(NV), gh(NU * NV);
    for (int i = 0; i < NU; ++i) gu[static_cast<std::size_t>(i)] = i / double(NU - 1);
    for (int j = 0; j < NV; ++j) gv[static_cast<std::size_t>(j)] = j / double(NV - 1);
    for (int i = 0; i < NU; ++i)
        for (int j = 0; j < NV; ++j)
            gh[static_cast<std::size_t>(i * NV + j)] =
                0.15 + 0.7 * (i * NV + j) / double(NU * NV - 1);

    auto bare = [&](Projection mode) {
        FigureSnapshot fs = make_snapshot3d(1, 1, 1);
        RenderSnapshot3D* s = fs.axes[0].snap3d();
        s->camera.projection = mode;
        s->camera.fov = 60.0;
        s->box_style.panes = false;
        s->grid_enabled = false;
        s->xticks_override = std::vector<Tick>{};
        s->yticks_override = std::vector<Tick>{};
        s->zticks_override = std::vector<Tick>{};
        s->xmin = 0; s->xmax = 1; s->xlim_auto = false;
        s->ymin = 0; s->ymax = 1; s->ylim_auto = false;
        s->zmin = 0; s->zmax = 1; s->zlim_auto = false;
        return fs;
    };
    auto render = [&](const FigureSnapshot& fs, const std::string& stem) {
        {
            GLContext ctx({ .width = W, .height = H,
                            .title = "layout_test", .visible = false });
            NvgRenderer  nvg(ctx.nvg());
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

    // ---- Every cell centre carries its own colour, where the projector says
    {
        FigureSnapshot fs = bare(Projection::Orthographic);
        SurfacePlot sp;
        sp.u = gu; sp.v = gv; sp.heights = gh;
        sp.opts.colormap = true;
        sp.opts.shading  = 0.0f;   // so the expected pixel is the LUT entry itself
        fs.axes[0].snap3d()->surfaces.push_back(sp);
        render(fs, "surface_cells");

        const FigureLayout lay = compute_figure_layout(fs, W, H);
        const Projector3D& proj = lay.cells[0].box3d->proj;
        const SurfacePlot& s = fs.axes[0].snap3d()->surfaces[0];
        double vmin = 0.0, vmax = 1.0;
        surface_value_range(s, vmin, vmax);
        const uint8_t* lut = colormaps::get(Colormap::Viridis);

        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load("surface_cells.png", &w, &h, &comp, 4);
        int hits = 0, misses = 0;
        SurfaceCell cell;
        for (std::size_t k = 0; k < s.cell_count() && px; ++k) {
            surface_cell(s, k / s.cell_cols(), k % s.cell_cols(), proj.transform(), cell);
            Vec3 c{};
            for (const Vec3& p : cell.p) c = c + p;
            c = c * 0.25;
            const Px3 q = proj.project(c.x, c.y, c.z);
            const int xi = static_cast<int>(q.x), yi = static_cast<int>(q.y);
            if (xi < 0 || yi < 0 || xi >= w || yi >= h) { ++misses; continue; }
            const unsigned char* p = px + (yi * w + xi) * 4;
            const unsigned char* e =
                lut + static_cast<int>(std::clamp((cell.value - vmin) / (vmax - vmin), 0.0, 1.0)
                                       * 255.0) * 4;
            if (std::abs(p[0] - e[0]) <= 1 && std::abs(p[1] - e[1]) <= 1 &&
                std::abs(p[2] - e[2]) <= 1) ++hits; else ++misses;
        }
        if (px) stbi_image_free(px);
        check(hits == static_cast<int>(s.cell_count()) && misses == 0,
              "surface: every cell centre carries its own colour, where the projector puts it");

        // The SVG carries the same cells, and the plan's own pixels appear in
        // it verbatim -- counting elements alone would pass on a writer that
        // projected the surface itself and got it wrong (§13, step 1's rule).
        const std::string svg = read_file("surface_cells.svg");
        const std::vector<Surface3DPolygon> plan = plan_surfaces3d(proj, { s });
        // **Two polygons per cell, not one, and that is the fix rather than a
        // detail.** A cell's four samples are not coplanar in general, so the
        // quad the SVG used to emit was a different shape from the two
        // triangles the raster path draws -- and, since step 9, a polygon with
        // no plane cannot take part in an exact painter's order at all. This
        // surface is flat enough that the two forms look alike; the assertion
        // is here so that anything folding the triangles back into a quad has
        // to argue with it.
        check(plan.size() == s.cell_count() * 2,
              "surface: the SVG plan is two triangles per cell, on the diagonal the "
              "raster path splits on");
        int found = 0;
        for (const Surface3DPolygon& poly : plan) {
            std::ostringstream pt;
            pt << poly.xy[0] << ',' << poly.xy[1] << ' ' << poly.xy[2] << ',' << poly.xy[3];
            if (svg.find(pt.str()) != std::string::npos) ++found;
        }
        check(found == static_cast<int>(plan.size()),
              "surface: and every planned cell's own pixels reach the file");
    }

    // ---- A flat surface lands exactly where a plane at the same offset does
    //
    // The two are drawn by completely different machinery -- a plane is one
    // textured quad of its own raster, a surface is a grid of shaded triangles
    // -- so agreeing on the silhouette to the pixel is a statement that the
    // new kind is on the same coordinate chain as the old one, not merely that
    // it draws something.
    {
        const uint8_t* lut = colormaps::get(Colormap::Viridis);
        const Color flat{ lut[0] / 255.0f, lut[1] / 255.0f, lut[2] / 255.0f, 1.0f };
        const Range ext{ 0.05, 0.95 };

        FigureSnapshot fs_s = bare(Projection::Orthographic);
        {
            SurfacePlot sp;
            sp.u = std::vector<double>{ ext.lo, ext.hi };
            sp.v = std::vector<double>{ ext.lo, ext.hi };
            sp.heights = std::vector<double>(4, 0.5);
            sp.opts.color   = flat;
            sp.opts.shading = 0.0f;
            fs_s.axes[0].snap3d()->surfaces.push_back(sp);
        }
        FigureSnapshot fs_p = bare(Projection::Orthographic);
        fs_p.axes[0].snap3d()->planes.push_back(
            make_plane(PlaneOrientation::XY, 0.5, std::vector<float>(4, 0.0f), 2, 2, ext, ext));

        render(fs_s, "surface_flat");
        render(fs_p, "surface_flat_plane");

        auto mask = [&](const std::string& stem, std::vector<unsigned char>& out,
                        int& w, int& h) {
            int comp = 0;
            unsigned char* px = stbi_load((stem + ".png").c_str(), &w, &h, &comp, 4);
            out.assign(static_cast<std::size_t>(w) * h, 0);
            if (!px) return;
            for (int i = 0; i < w * h; ++i) {
                const unsigned char* p = px + i * 4;
                out[static_cast<std::size_t>(i)] =
                    (std::abs(p[0] - lut[0]) <= 2 && std::abs(p[1] - lut[1]) <= 2 &&
                     std::abs(p[2] - lut[2]) <= 2) ? 1 : 0;
            }
            stbi_image_free(px);
        };
        std::vector<unsigned char> ms, mp;
        int sw = 0, sh = 0, pw = 0, ph = 0;
        mask("surface_flat", ms, sw, sh);
        mask("surface_flat_plane", mp, pw, ph);
        int both = 0, only = 0;
        for (std::size_t i = 0; i < ms.size() && i < mp.size(); ++i) {
            if (ms[i] && mp[i]) ++both;
            else if (ms[i] || mp[i]) ++only;
        }
        std::printf("  flat surface vs plane: %d px shared, %d px in one only\n", both, only);
        // The rim is the two paths' antialiasing at the silhouette, which is a
        // quad's edge in one and a triangle pair's in the other. The body has
        // to be the same body.
        check(both > 5000 && only * 100 < both,
              "surface: a flat surface covers the same pixels as a plane at the same offset");
    }

    // ---- The wireframe, and that shading is doing something
    {
        FigureSnapshot fs = bare(Projection::Orthographic);
        SurfacePlot sp;
        sp.u = gu; sp.v = gv; sp.heights = gh;
        sp.opts.color = { 0.5f, 0.5f, 0.5f, 1.0f };
        fs.axes[0].snap3d()->surfaces.push_back(sp);
        render(fs, "surface_plain");

        FigureSnapshot fw = fs;
        fw.axes[0].snap3d()->surfaces[0].opts.edges = true;
        fw.axes[0].snap3d()->surfaces[0].opts.edgecolor = { 1.0f, 0.0f, 0.0f, 1.0f };
        fw.axes[0].snap3d()->surfaces[0].opts.edge_linewidth = 2.0f;
        render(fw, "surface_wire");

        auto count = [&](const std::string& stem, auto&& pred) {
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load((stem + ".png").c_str(), &w, &h, &comp, 4);
            int n = 0;
            if (px) {
                for (int i = 0; i < w * h; ++i) if (pred(px + i * 4)) ++n;
                stbi_image_free(px);
            }
            return n;
        };
        auto red = [](const unsigned char* p) { return p[0] > 180 && p[1] < 80 && p[2] < 80; };
        check(count("surface_plain", red) == 0 && count("surface_wire", red) > 300,
              "surface: edges draw the cell grid, and nothing draws it when they are off");

        // Shading is per cell, so a single-coloured surface is not a
        // silhouette: distinct greys have to appear, and exactly one grey
        // would mean the light never reached the buffer.
        auto greys = [&](const std::string& stem) {
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load((stem + ".png").c_str(), &w, &h, &comp, 4);
            std::set<int> seen;
            if (px) {
                for (int i = 0; i < w * h; ++i) {
                    const unsigned char* p = px + i * 4;
                    if (p[0] == p[1] && p[1] == p[2] && p[0] > 40 && p[0] < 200)
                        seen.insert(p[0]);
                }
                stbi_image_free(px);
            }
            return static_cast<int>(seen.size());
        };
        check(greys("surface_plain") >= 3,
              "surface: per-cell shading gives a single-coloured surface its shape back");
    }

    // ---- One renderer, four frames: a rebuild that happens while the
    // wireframe is off must not take the wireframe with it (plan 10.1).
    //
    // Every other render in this file builds a fresh DataRenderer, so the
    // geometry cache is always cold and this could not appear. The Cosmetic
    // panel drives *one* renderer across frames, which is where it did: the
    // stale test asked whether the edge VBO existed rather than whether it
    // held this plot's edges, so turning "Colour by height" on while
    // "Wireframe" was off rebuilt the buffer without edges -- and since the
    // VBO itself was still allocated, nothing afterwards ever asked for them
    // again. Two toggles, each of which works alone, and one order of them
    // that silently disables the other.
    //
    // A cold cache is also what a zero data generation means, so the
    // snapshots here name one: without it every frame rebuilds and the test
    // asserts nothing.
    {
        auto count_px = [&](const std::string& stem, auto&& pred) {
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load((stem + ".png").c_str(), &w, &h, &comp, 4);
            int n = 0;
            if (px) {
                for (int i = 0; i < w * h; ++i) if (pred(px + i * 4)) ++n;
                stbi_image_free(px);
            }
            return n;
        };
        auto red   = [](const unsigned char* p) { return p[0] > 180 && p[1] < 80 && p[2] < 80; };
        auto green = [](const unsigned char* p) { return p[1] > 150 && p[0] < 80 && p[2] < 80; };

        // The surface: "Colour by height" is the other toggle.
        {
            GLContext ctx({ .width = W, .height = H,
                            .title = "layout_test", .visible = false });
            NvgRenderer  nvg(ctx.nvg());
            DataRenderer data_r;

            FigureSnapshot fs = bare(Projection::Orthographic);
            fs.generation = fs.data_generation = 1;
            SurfacePlot sp;
            sp.u = gu; sp.v = gv; sp.heights = gh;
            sp.opts.color          = { 0.5f, 0.5f, 0.5f, 1.0f };
            sp.opts.edgecolor      = { 1.0f, 0.0f, 0.0f, 1.0f };
            sp.opts.edge_linewidth = 2.0f;
            fs.axes[0].snap3d()->surfaces.push_back(sp);

            auto frame = [&](bool colormap, bool edges, const std::string& stem) {
                SurfaceOptions& o = fs.axes[0].snap3d()->surfaces[0].opts;
                o.colormap = colormap;
                o.edges    = edges;
                export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1);
            };
            frame(false, true,  "surface_toggle_a");   // the wireframe, as a baseline
            frame(false, false, "surface_toggle_b");   // off
            frame(true,  false, "surface_toggle_c");   // a rebuild while it is off
            frame(true,  true,  "surface_toggle_d");   // and asked for again
            check(count_px("surface_toggle_a", red) > 300 &&
                  count_px("surface_toggle_b", red) == 0 &&
                  count_px("surface_toggle_d", red) > 300,
                  "surface: the wireframe comes back after the colormap rebuilt the "
                  "buffer without it");
        }

        // The bar grid, whose Edges checkbox sits on the same defect: its
        // rebuild is driven by the bar widths, the bottom and the shading
        // rather than by a colormap, so a dragged shading is the toggle.
        {
            GLContext ctx({ .width = W, .height = H,
                            .title = "layout_test", .visible = false });
            NvgRenderer  nvg(ctx.nvg());
            DataRenderer data_r;

            FigureSnapshot fs = bare(Projection::Orthographic);
            fs.generation = fs.data_generation = 1;
            Bar3DPlot bp;
            bp.u = std::vector<double>{ 0.35, 0.65 };
            bp.v = std::vector<double>{ 0.35, 0.65 };
            bp.heights = std::vector<double>{ 0.4, 0.6, 0.5, 0.7 };
            bp.u_width = bp.v_width = 0.2;
            bp.opts.color          = { 0.5f, 0.5f, 0.5f, 1.0f };
            bp.opts.edgecolor      = { 0.0f, 1.0f, 0.0f, 1.0f };
            bp.opts.edge_linewidth = 2.0f;
            fs.axes[0].snap3d()->bars3d.push_back(bp);

            auto frame = [&](float shading, bool edges, const std::string& stem) {
                Bar3DOptions& o = fs.axes[0].snap3d()->bars3d[0].opts;
                o.shading = shading;
                o.edges   = edges;
                export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1);
            };
            frame(0.5f, true,  "bar3d_toggle_a");
            frame(0.5f, false, "bar3d_toggle_b");
            frame(0.0f, false, "bar3d_toggle_c");
            frame(0.0f, true,  "bar3d_toggle_d");
            check(count_px("bar3d_toggle_a", green) > 300 &&
                  count_px("bar3d_toggle_b", green) == 0 &&
                  count_px("bar3d_toggle_d", green) > 300,
                  "bar3d: the edges come back after a shading change rebuilt the "
                  "buffer without them");
        }
    }
}

// ---------------------------------------------------------------------------
// v1.0 step 7d: a surface under the pointer and in the panels
//
// The 6b/6c pattern applied to the new kind, so the checks are about the same
// two things those steps' were: the *address* (an op naming a surface lands on
// that surface and on nothing else, across the thread boundary), and the
// *agreement* between the tooltip and the picture.
// ---------------------------------------------------------------------------

void test_surface_data_panel() {
    std::printf("\n[3D: the Data panel's surface grid]\n");

    using namespace sextant;

    auto snap = [] {
        RenderSnapshot3D s;
        s.surfaces.push_back(ripple_surface());
        return s;
    };

    // ---- The three columns of a cell edit. A surface has no fourth: it has
    // no base to stand on, which is the one column a bar grid has and it does.
    {
        RenderSnapshot3D s = snap();
        apply_plot_data_ops(s, {
            PlotCellEdit{ PlotKind::Surface, 0, 0, 2, 9.5 },    // u[2]
            PlotCellEdit{ PlotKind::Surface, 0, 1, 0, -1.5 },   // v[0]
            PlotCellEdit{ PlotKind::Surface, 0, 2, 4, 42.0 },   // heights[1][1]
            PlotCellEdit{ PlotKind::Surface, 0, 3, 4, -7.0 },   // no such column
        });
        const SurfacePlot& sp = s.surfaces[0];
        check(sp.u[2] == 9.5 && sp.v[0] == -1.5,
              "surface panel: a cell edit reaches the grid's own coordinates");
        check(sp.heights[4] == 42.0,
              "surface panel: and its matrix, at the row-major index");
        check(sp.u[0] == 0.0 && sp.v[2] == 5.0 && sp.heights[3] == 4.0,
              "surface panel: leaving every neighbour alone");
    }

    // ---- Structural, and this is where the shared machinery earns its keep:
    // the same MatrixLineEdit a bar grid uses, re-striding the coordinate, the
    // matrix and the labels in one step.
    {
        RenderSnapshot3D s = snap();
        apply_plot_data_ops(s, {
            MatrixLineEdit{ MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                            0, 1, -1, PlotKind::Surface },
        });
        const SurfacePlot& sp = s.surfaces[0];
        check(sp.u.size() == 4 && sp.v.size() == 3 && sp.heights.size() == 12 &&
              sp.opts.hint_labels.size() == 12,
              "surface panel: inserting a u line grows the coordinate, the matrix and the labels at once");
        check(sp.u[1] == 1.0,
              "surface panel: and the new line sits between its neighbours");
        check(sp.heights[3] == 1.0 && sp.heights[5] == 3.0,
              "surface panel: its values copy the line above, so the sheet does not jump");
        check(sp.opts.hint_labels[6] == "S3" && sp.opts.hint_labels[11] == "S8",
              "surface panel: and the labels re-stride with them rather than sliding one line");
        check(sp.cell_rows() == 3 && sp.cell_cols() == 2,
              "surface panel: the cell count follows, since cells are the gaps");
    }

    // ---- The floor is 2, not 1. Axes3D::surface() refuses a 1 x n grid
    // because it has no cells, so the panel must not be able to make one --
    // and the *edit lane* is where that has to hold, since the panel's own
    // disabled button is only a courtesy.
    {
        RenderSnapshot3D s = snap();
        apply_plot_data_ops(s, {
            MatrixLineEdit{ MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                            0, 0, -1, PlotKind::Surface },
        });
        check(s.surfaces[0].u.size() == 2 && s.surfaces[0].heights.size() == 6,
              "surface panel: removing a u line takes its row of the matrix with it");
    }

    // ---- The address, both ways. A surface op is addressed at the axes, and
    // must not reach a bar grid that shares its index, nor a plane's objects.
    {
        RenderSnapshot3D s = two_plane_snapshot();
        s.bars3d.push_back(bar3d_grid());
        s.surfaces.push_back(ripple_surface());
        apply_plot_data_ops(s, { PlotCellEdit{ PlotKind::Surface, 0, 2, 0, 99.0, 0 } });
        check(s.surfaces[0].heights[0] == 1.0,
              "surface panel: a Surface op addressed at a plane does not reach the axes' surface");
        apply_plot_data_ops(s, { PlotCellEdit{ PlotKind::Surface, 0, 2, 0, 99.0, -1 } });
        check(s.surfaces[0].heights[0] == 99.0,
              "surface panel: (and at the axes it does, so that is the address doing it)");
        check(s.bars3d[0].heights[0] == 1.0,
              "surface panel: and it leaves the bar3d grid at the same index alone");
        apply_plot_data_ops(s, { PlotCellEdit{ PlotKind::Bar3D, 0, 2, 0, 55.0, -1 } });
        check(s.bars3d[0].heights[0] == 55.0 && s.surfaces[0].heights[0] == 99.0,
              "surface panel: (and the bar3d op reaches the bars and not the surface)");
    }

    // ---- The tables the panel actually enumerates.
    {
        RenderSnapshot3D s = two_plane_snapshot();
        s.bars3d.push_back(bar3d_grid());
        s.surfaces.push_back(ripple_surface());
        const std::vector<PlotDataTable> t = collect_plot_data_tables(s);
        int grids = 0, surfaces = 0;
        for (const PlotDataTable& x : t) {
            if (x.is_grid()) ++grids;
            if (x.surface)   ++surfaces;
        }
        check(grids == 2 && surfaces == 1,
              "surface panel: a surface is enumerated as the grid table shape, beside the bar grid");
        for (const PlotDataTable& x : t)
            if (x.kind == PlotKind::Surface)
                check(x.plane_index == -1 && x.surface == &s.surfaces[0],
                      "surface panel: on the axes, plane -1, pointing at its own plot");
    }
}

void test_surface_hints() {
    std::printf("\n[3D: hover hints over a surface]\n");

    using namespace sextant;

    Transform3D tf;
    tf.xmin = 0.0; tf.xmax = 10.0;
    tf.ymin = 0.0; tf.ymax = 10.0;
    tf.zmin = 0.0; tf.zmax = 10.0;

    const PlotRect frame{ 20.0f, 15.0f, 400.0f, 320.0f };
    Camera3D cam;
    cam.azimuth = -55.0; cam.elevation = 24.0;

    // A surface over the same span the bar grid tests use, tilted so that it
    // is not degenerate from any of the cameras below.
    SurfacePlot s;
    s.u = std::vector<double>{ 1.0, 4.0, 7.0, 9.0 };
    s.v = std::vector<double>{ 1.0, 4.0, 7.0, 9.0 };
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 4; ++j)
            s.heights.mut().push_back(3.0 + 0.4 * static_cast<double>(i)
                                          + 0.7 * static_cast<double>(j));

    for (int mode = 0; mode < 2; ++mode) {
        cam.projection = mode ? Projection::Perspective : Projection::Orthographic;
        const char* what = mode ? "perspective" : "orthographic";
        const Projector3D proj(tf, cam, frame, 0.1f);

        // ---- The ray through a cell's own projected centre must meet that
        // cell. Exact: a cell's centre-of-corners is on one of its two
        // triangles only if the quad is planar, so the claim is deliberately
        // weaker and stronger at once -- it must meet *the surface*, at that
        // cell or a neighbour, and the depth must match the centre's own.
        int cells = 0, met = 0;
        SurfaceCell cell;
        for (std::size_t k = 0; k < s.cell_count(); ++k) {
            surface_cell(s, k / s.cell_cols(), k % s.cell_cols(), tf, cell);
            Vec3 c{ 0.0, 0.0, 0.0 };
            for (const Vec3& p : cell.p) c = c + p * 0.25;
            const Px3 px = proj.project(c.x, c.y, c.z);
            if (!px.in_front()) continue;
            ++cells;
            float depth = 0.0f;
            std::size_t sample = 0;
            if (surface_ray_hit(s, k, proj, px.x, px.y, depth, sample)
                && std::fabs(depth - px.depth) < 1e-3f) ++met;
        }
        check(cells > 0 && met == cells,
              std::string("surface hints: the ray through a cell's centre meets that cell, "
                          "all ") + std::to_string(cells) + " of them, at its own depth ("
                          + what + ")");

        // ---- And misses what it should, or the check above would pass on a
        // hit test that always said yes.
        int stray = 0;
        for (int i = 0; i < 40; ++i) {
            const float px = frame.x - 60.0f - static_cast<float>(i);
            for (std::size_t k = 0; k < s.cell_count(); ++k) {
                float depth = 0.0f;
                std::size_t sample = 0;
                if (surface_ray_hit(s, k, proj, px, frame.y + 5.0f, depth, sample)) ++stray;
            }
        }
        check(stray == 0,
              std::string("surface hints: and a pixel outside the box meets no cell (")
                  + what + ")");

        // ---- The sample the tooltip names is the nearest corner of the cell
        // the ray hit, which is the index a caller's hint_labels entry is
        // written against. Checked by aiming at each corner in turn.
        int corners = 0, named = 0;
        for (std::size_t k = 0; k < s.cell_count(); ++k) {
            const std::size_t i = k / s.cell_cols(), j = k % s.cell_cols();
            surface_cell(s, i, j, tf, cell);
            const std::size_t want[4] = { s.index_of(i, j),         s.index_of(i + 1, j),
                                          s.index_of(i + 1, j + 1), s.index_of(i, j + 1) };
            for (int c = 0; c < 4; ++c) {
                // A point 80% of the way from the cell's centre toward this
                // corner: unambiguously nearest it, and unambiguously inside
                // the cell.
                Vec3 mid{ 0.0, 0.0, 0.0 };
                for (const Vec3& p : cell.p) mid = mid + p * 0.25;
                const Vec3 aim = mid + (cell.p[c] - mid) * 0.8;
                const Px3 px = proj.project(aim.x, aim.y, aim.z);
                if (!px.in_front()) continue;
                ++corners;
                float depth = 0.0f;
                std::size_t sample = 0;
                if (surface_ray_hit(s, k, proj, px.x, px.y, depth, sample)
                    && sample == want[c]) ++named;
            }
        }
        check(corners > 0 && named == corners,
              std::string("surface hints: the sample reported is the nearest of the cell's "
                          "four, all ") + std::to_string(corners) + " (" + what + ")");
    }

    // ---- The tooltip text, and the label appended below it.
    {
        const Projector3D proj(tf, cam, frame, 0.1f);
        RenderSnapshot3D snap;
        SurfacePlot ls = s;
        ls.opts.hint_labels.assign(ls.count(), std::string());
        ls.opts.hint_labels[ls.index_of(1, 1)] = "the middle";
        snap.surfaces.push_back(ls);

        SurfaceCell cell;
        surface_cell(ls, 1, 1, tf, cell);
        // Aim at the corner that *is* sample (1,1), so the label is the one
        // reported rather than a neighbour's.
        Vec3 mid{ 0.0, 0.0, 0.0 };
        for (const Vec3& p : cell.p) mid = mid + p * 0.25;
        const Vec3 aim = mid + (cell.p[0] - mid) * 0.8;
        const Px3 px = proj.project(aim.x, aim.y, aim.z);

        const auto r = find_hint3d(snap, proj, px.x, px.y, nullptr);
        check(r.has_value(), "surface hints: the cursor over a surface gets an answer");
        if (r) {
            check(r->text.find("x=4") != std::string::npos &&
                  r->text.find("y=4") != std::string::npos &&
                  r->text.find("z=") != std::string::npos,
                  "surface hints: which names the sample's own coordinates, in the axes' letters");
            check(r->text.find("height=") == std::string::npos,
                  "surface hints: and calls the third one z, not height -- a sheet has no base "
                  "to measure a height from");
            check(r->text.find("the middle") != std::string::npos,
                  "surface hints: with the caller's label for that sample appended");
        }
    }

    // ---- Against the rest of the scene: whichever is drawn in front of the
    // cursor answers. A plane placed between the eye and the surface must win,
    // and the same plane behind it must not.
    {
        const Projector3D proj(tf, cam, frame, 0.1f);
        SurfaceCell cell;
        surface_cell(s, 1, 1, tf, cell);
        Vec3 mid{ 0.0, 0.0, 0.0 };
        for (const Vec3& p : cell.p) mid = mid + p * 0.25;
        const Px3 px = proj.project(mid.x, mid.y, mid.z);

        // A plane at z, carrying a line through the point the cursor's own ray
        // meets it at -- so the plane genuinely has something under the cursor
        // and can answer. Placing the line anywhere else would test nothing:
        // a plane with no data near the pointer declines and falls through,
        // which is correct and would make "the surface answered" prove
        // nothing about the order.
        auto with_plane = [&](double z) {
            RenderSnapshot3D snap;
            snap.surfaces.push_back(s);
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            pl.offset = z;
            double hu = 0.0, hv = 0.0;
            float  hd = 0.0f;
            plane_ray_hit(proj, pl.orient, pl.offset, px.x, px.y, hu, hv, hd);
            LinePlot lp;
            lp.x = std::vector<double>{ hu - 1.0, hu, hu + 1.0 };
            lp.y = std::vector<double>{ hv, hv, hv };
            pl.sheet.lines.push_back(std::move(lp));
            snap.planes.push_back(std::move(pl));
            return snap;
        };
        // The camera looks down from elevation 24, so a *higher* plane is the
        // nearer one -- derived from the projector rather than assumed.
        const Px3 hi = proj.project(mid.x, mid.y, mid.z + 3.0);
        const Px3 lo = proj.project(mid.x, mid.y, mid.z - 3.0);
        const double near_z = (hi.depth < lo.depth) ? mid.z + 3.0 : mid.z - 3.0;
        const double far_z  = (hi.depth < lo.depth) ? mid.z - 3.0 : mid.z + 3.0;

        const RenderSnapshot3D in_front = with_plane(near_z);
        const RenderSnapshot3D behind   = with_plane(far_z);
        const auto a = find_hint3d(in_front, proj, px.x, px.y, nullptr);
        const auto b = find_hint3d(behind,   proj, px.x, px.y, nullptr);
        // The plane's line reports two coordinates; the surface reports three,
        // the third named for the axis it rises along. So the two answers are
        // told apart by what they say rather than by which object was asked.
        check(b.has_value() && b->text.find("z=") != std::string::npos,
              "surface hints: a plane behind the surface does not take the answer from it");
        check(a.has_value() && a->text.find("z=") == std::string::npos,
              "surface hints: and a plane in front of it does");
    }
}

// A surface can ask for a colorbar (v1.0 step 11.3). It is the first kind that
// is the *axes'* own rather than a plane's, so it is also what the 3D request
// lookup grew an axes-own arm for -- until now it walked planes and nothing
// else, which is why a surface's colour scale had no key at all.
//
// The wrinkle worth checking is the range. `SurfaceOptions::vmin == vmax`
// means "the surface's own heights", so the bar has to be labelled with the
// *resolved* pair; a bar reading 0 to 0 would explain nothing, and the numbers
// are also what its block is measured wide enough for.
void test_surface_colorbar() {
    std::printf("\n[3D: a surface's colorbar]\n");

    using namespace sextant;

    constexpr int W = 460, H = 420;

    // Heights well away from 0..1, so "resolved" and "declared" cannot be
    // confused for each other.
    SurfacePlot s;
    s.u = CowVec<double>{ std::vector<double>{ 0.0, 1.0, 2.0 } };
    s.v = CowVec<double>{ std::vector<double>{ 0.0, 1.0, 2.0 } };
    s.heights = CowVec<double>{ std::vector<double>{
        20.0, 30.0, 40.0, 30.0, 55.0, 60.0, 40.0, 60.0, 80.0 } };
    s.opts.colormap = true;
    s.opts.colorbar = true;

    auto with_surface = [&](SurfacePlot sp) {
        FigureSnapshot fs = make_snapshot3d(1, 1, 1);
        fs.axes[0].snap3d()->surfaces.push_back(std::move(sp));
        return fs;
    };

    {
        const auto reqs = find_colorbar_requests(*with_surface(s).axes[0].snap3d());
        check(reqs.size() == 1, "surface cb: a surface that asks for a bar gets one");
        check(reqs[0].vmin == 20.0f && reqs[0].vmax == 80.0f,
              "surface cb: spanning its own heights, since vmin == vmax means exactly that");
    }

    // Declared limits are used as given -- the resolution is a default, not an
    // override.
    {
        SurfacePlot fixed = s;
        fixed.opts.vmin = 0.0f; fixed.opts.vmax = 100.0f;
        const auto reqs = find_colorbar_requests(*with_surface(fixed).axes[0].snap3d());
        check(reqs.size() == 1 && reqs[0].vmin == 0.0f && reqs[0].vmax == 100.0f,
              "surface cb: and a declared range is taken as declared");
    }

    // Gated on `colormap`. Without it the sheet is one flat colour and a bar
    // would be a key to a mapping that is not in the picture.
    {
        SurfacePlot flat = s;
        flat.opts.colormap = false;
        check(find_colorbar_requests(*with_surface(flat).axes[0].snap3d()).empty(),
              "surface cb: a flat-coloured surface gets none, whatever the flag says");
    }

    // The axes' own kinds come before the planes', which is the order the
    // legend's keys use and is pinned here so the two cannot drift apart.
    {
        HeatmapOptions hcb;
        hcb.colorbar = true;
        hcb.vmin = 0.0f; hcb.vmax = 1.0f;
        FigureSnapshot fs = with_surface(s);
        fs.axes[0].snap3d()->planes.push_back(
            make_plane(PlaneOrientation::XY, 0.0, std::vector<float>(4, 0.5f), 2, 2,
                       { 0.0, 1.0 }, { 0.0, 1.0 }, hcb));
        const auto reqs = find_colorbar_requests(*fs.axes[0].snap3d());
        check(reqs.size() == 2 && reqs[0].vmax == 80.0f && reqs[1].vmax == 1.0f,
              "surface cb: the axes' own bar comes before a plane's");

        const CellDecorations dec = compute_cell_decorations(*fs.axes[0].snap3d());
        check(dec.colorbars.size() == 2
              && dec.colorbars[0].block > dec.colorbars[1].block,
              "surface cb: and is the wider block, being labelled 20..80 against 0..1");
    }

    // Into a file, through the public API -- and the numbers on the bar are
    // the resolved ones, which is the half a layout check cannot see.
    {
        auto fig = Figure::create({ .width = W, .height = H });
        auto ax3 = fig->add_subplot3d(1, 1, 1);
        const std::vector<double> u{ 0.0, 1.0, 2.0 }, v{ 0.0, 1.0, 2.0 };
        const std::vector<double> h{ 20.0, 30.0, 40.0, 30.0, 55.0, 60.0, 40.0, 60.0, 80.0 };
        ax3->surface(PlaneOrientation::XY, u, v, h,
                     { .colormap = true, .colorbar = true });
        fig->savefig("surface_colorbar.svg");

        std::ifstream f("surface_colorbar.svg", std::ios::binary);
        const std::string svg((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
        const std::size_t bar = svg.find("url(#colorbarGrad0_0)");
        check(bar != std::string::npos,
              "surface cb: Axes3D::surface({.colorbar = true}) puts a bar in the file");
        // Anchored past the bar's own rect rather than searched for anywhere:
        // the z axis is ticked over these same heights, so a bare find() for
        // "80" matches a tick label and passes however the bar is labelled.
        const std::string tail = bar == std::string::npos ? std::string() : svg.substr(bar);
        check(tail.find(">80</text>") != std::string::npos
              && tail.find(">20</text>") != std::string::npos,
              "surface cb: labelled with the heights it resolved, not with 0 and 0");
    }
}

// A flat surface is keyed; a colormapped one is not. The follow-up to 11.5,
// which left `surface` out because its colours are explained by the bar 11.3
// gave it -- true of a *colormapped* sheet and never true of a flat one, which
// has exactly one colour and so nothing a swatch could be dishonest about.
//
// One `label` serves whichever applies, and the two are mutually exclusive by
// `colormap`: with it on the text names the colorbar, with it off it keys the
// legend. A caller never has to know in advance which their surface will get.
void test_surface_legend_key() {
    std::printf("\n[3D: a flat surface's legend key]\n");

    using namespace sextant;

    auto build = [](bool colormapped) {
        RenderSnapshot3D r;
        r.legend_enabled = true;
        SurfacePlot s;
        s.u = CowVec<double>{ std::vector<double>{ 0.0, 1.0, 2.0 } };
        s.v = CowVec<double>{ std::vector<double>{ 0.0, 1.0, 2.0 } };
        s.heights = CowVec<double>{ std::vector<double>{
            20.0, 30.0, 40.0, 30.0, 55.0, 60.0, 40.0, 60.0, 80.0 } };
        s.opts.color    = Color::Cyan;
        s.opts.name     = "terrain";
        s.opts.colormap = colormapped;
        // Asked for either way, so the gate below is the gate and not the flag.
        s.opts.colorbar = true;
        r.surfaces.push_back(std::move(s));
        return r;
    };

    const RenderSnapshot3D flat = build(false);
    const RenderSnapshot3D mapped = build(true);

    const auto ef = collect_legend_entries(flat);
    check(ef.size() == 1 && ef[0].name == "terrain" && ef[0].kind == LegendKind::Bar,
          "surface key: a flat surface is keyed, by a swatch of its one colour");
    check(ef[0].color.g == Color::Cyan.g && ef[0].color.b == Color::Cyan.b,
          "surface key: and the swatch is that colour, not a default");

    check(collect_legend_entries(mapped).empty(),
          "surface key: a colormapped one is not -- it has no one colour to show, and the "
          "bar it asks for is what explains it");

    // The two halves of the one label, each where it belongs and neither in
    // both: a flat surface asks for no bar, a mapped one draws no key.
    check(find_colorbar_requests(flat).empty(),
          "surface key: a flat surface asks for no colorbar, whatever the flag says");
    check(find_colorbar_requests(mapped).size() == 1
          && find_colorbar_requests(mapped)[0].name == "terrain",
          "surface key: and a mapped one puts the same label on its bar");

    // `show_legend` is the same gate every kind has, and does not cost the text.
    RenderSnapshot3D off = build(false);
    off.surfaces[0].opts.show_legend = false;
    check(collect_legend_entries(off).empty()
          && off.surfaces[0].opts.name == "terrain",
          "surface key: show_legend switches it off without clearing the name");

    // Ordered with the axes' own kinds, before any plane's -- a surface is the
    // second axes-own kind that can be keyed, so this is where the two could
    // have been written in either order and drifted.
    RenderSnapshot3D both = build(false);
    Bar3DPlot g;
    g.u = CowVec<double>{ std::vector<double>{ 0.0, 1.0 } };
    g.v = CowVec<double>{ std::vector<double>{ 0.0, 1.0 } };
    g.heights = CowVec<double>{ std::vector<double>{ 1.0, 2.0, 3.0, 4.0 } };
    g.opts.name = "counts";
    both.bars3d.push_back(g);
    const auto eb = collect_legend_entries(both);
    check(eb.size() == 2 && eb[0].name == "counts" && eb[1].name == "terrain",
          "surface key: bar grids before surfaces, both before any plane's keys");
}

}  // namespace lt
