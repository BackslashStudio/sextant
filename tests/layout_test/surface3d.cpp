// surface: ingest, cell order, rendering, hints and the Data panel. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    // -------------------------------------------------------------------------
    // surface(): the type, ingest and geometry
    // -------------------------------------------------------------------------
    void test_surface_ingest() {
        std::printf("\n[3D: surface ingest, auto-scale and cells]\n");

        using namespace sextant;

        auto threw = [](auto&& fn) {
            try {
                fn();
                return false;
            } catch (const std::invalid_argument&) { return true; }
        };

        const std::vector<double> u{0.0, 2.0, 4.0};
        const std::vector<double> v{0.0, 1.0};
        const std::vector<double> h{1, 2, 3, 4, 5, 6};

        auto fig = Figure::create({.width = 300, .height = 240});
        auto ax = fig->add_subplot3d(1, 1, 1);
        check(threw([&] { ax->surface(PlaneOrientation::XY, u, v, {h.data(), 5}); }),
              "surface: heights that are not |u| x |v| throw at ingest");
        const std::vector<double> nan_h{1, 2, 3, std::numeric_limits<double>::quiet_NaN(), 5, 6};
        check(threw([&] { ax->surface(PlaneOrientation::XY, u, v, nan_h); }),
              "surface: a non-finite height throws, once, rather than reaching the vertex buffer");
        // Cells live between samples, so at least 2 x 2.
        const std::vector<double> one{0.0};
        check(threw([&] { ax->surface(PlaneOrientation::XY, one, v, {h.data(), 2}); }),
              "surface: a grid with one row has vertices and no cells, and says so");
        check(!threw([&] { ax->surface(PlaneOrientation::XY, u, v, h); }),
              "surface: a 3 x 2 grid is accepted");

        SurfacePlot s;
        s.u = u;
        s.v = v;
        s.heights = h;
        check(s.cell_rows() == 2 && s.cell_cols() == 1 && s.cell_count() == 2,
              "surface: |u| x |v| samples make (|u|-1) x (|v|-1) cells");

        // Auto-scale: the samples are the extent (no footprint, unlike bars).
        {
            const DataBounds3D b = auto_scale3d({}, {}, {s}, {}, {}, {}, 0.0);
            check(b.xmin == 0.0 && b.xmax == 4.0 && b.ymin == 0.0 && b.ymax == 1.0,
                  "surface: its extent is its samples, with no footprint added");
            check(b.zmin == 1.0 && b.zmax == 6.0,
                  "surface: and its heights are its own range, not stretched to include zero");

            SurfacePlot yz = s;
            yz.orient = PlaneOrientation::YZ;
            const DataBounds3D r = auto_scale3d({}, {}, {yz}, {}, {}, {}, 0.0);
            check(r.ymin == 0.0 && r.ymax == 4.0 && r.zmin == 0.0 && r.zmax == 1.0 &&
                  r.xmin == 1.0 && r.xmax == 6.0,
                  "surface: an orientation moves u, v and the heights together, through one Axis3Map");
        }

        // Colormap range: `vmin == vmax` means the surface's own range.
        {
            double lo = 0.0, hi = 0.0;
            surface_value_range(s, lo, hi);
            check(lo == 1.0 && hi == 6.0, "surface: an empty vmin/vmax interval means the data's own range");
            SurfacePlot fixed = s;
            fixed.opts.vmin = -10.0f;
            fixed.opts.vmax = 10.0f;
            surface_value_range(fixed, lo, hi);
            check(lo == -10.0 && hi == 10.0, "surface: and a stated one is used as stated");
        }

        // One cell: the ring and its colormap value.
        {
            const Transform3D tf{0, 4, 0, 1, 0, 8, BoxAspect{1, 1, 1}};
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

        // Shading uses |n.l|: the same flat cell with reversed winding (normal
        // exactly negated) must shade identically. Flat, so only the sign changes;
        // max(0, n.l) would differ.
        {
            const Transform3D tf{0, 4, 0, 1, 0, 8, BoxAspect{1, 1, 1}};
            SurfacePlot flat;
            flat.u = u;
            flat.v = v;
            flat.heights = std::vector<double>(6, 3.0);
            SurfacePlot flipped = flat;
            flipped.v = std::vector<double>{1.0, 0.0};
            SurfaceCell a, b;
            surface_cell(flat, 0, 0, tf, a);
            surface_cell(flipped, 0, 0, tf, b);
            check(std::fabs(a.shade - b.shade) < 1e-6,
                  "surface: a cell's shade does not depend on which way its normal happens to point");
            check(a.shade > 0.85f && a.shade <= 1.0f,
                  "surface: and a sheet facing the light is lit, from either side of it");
        }

        // Color: flat or colormapped, shaded, with alpha.
        {
            SurfaceCell c{};
            c.shade = 0.5f;
            c.value = 3.5;
            SurfacePlot flat = s;
            flat.opts.color = {1.0f, 0.0f, 0.0f, 1.0f};
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

    // Cell order, checked against the geometry, not the sort key.
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
        s.u = u;
        s.v = v;
        s.heights = h;

        const PlotRect frame{0.0f, 0.0f, 300.0f, 240.0f};
        const Transform3D tf{-2, 3, -2, 2, -1.5, 1.5, BoxAspect{1, 1, 1}};

        for (int cam_i = 0; cam_i < 4; ++cam_i) {
            Camera3D cam;
            cam.azimuth = -140.0 + 70.0 * cam_i;
            cam.elevation = 15.0 + 12.0 * cam_i;
            const Projector3D proj(tf, cam, frame, 0.0f);

            std::vector<std::size_t> order;
            surface_draw_order(s, proj, order);
            check(order.size() == s.cell_count(), "surface order: every cell, once");

            // Oracle: for each pair of neighbouring cells, sampled along their
            // shared grid line, the one the ray reaches first must be drawn later.
            int checked = 0, wrong = 0;
            std::vector<std::size_t> pos(s.cell_count());
            for (std::size_t k = 0; k < order.size(); ++k) pos[order[k]] = k;
            const Vec3 eye = eye_coord(proj);
            for (std::size_t i = 0; i + 1 < s.cell_rows(); ++i) {
                for (std::size_t j = 0; j + 1 < s.cell_cols(); ++j) {
                    const std::size_t a = i * s.cell_cols() + j;
                    for (const std::size_t b: {a + 1, a + s.cell_cols()}) {
                        // Which cell's grid corner is nearer the eye (a distance, not
                        // the comparator's key).
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
    // A surface, rendered: each cell centre (CPU-projected) must carry that cell's
    // color in the GPU output.
    // -------------------------------------------------------------------------
    void test_surface_render() {
        std::printf("\n[3D: a surface, rendered]\n");

        using namespace sextant;

        constexpr int W = 400, H = 340;
        constexpr int NU = 5, NV = 4;

        // Distinct, monotone heights: each pixel identifies its cell, and the
        // surface stays single-valued from this camera.
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

        // ---- Every cell centre carries its own colour, where the projector says
        {
            FigureSnapshot fs = bare(Projection::Orthographic);
            SurfacePlot sp;
            sp.u = gu;
            sp.v = gv;
            sp.heights = gh;
            sp.opts.colormap = true;
            sp.opts.shading = 0.0f; // so the expected pixel is the LUT entry itself
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
                for (const Vec3& p: cell.p) c = c + p;
                c = c * 0.25;
                const Px3 q = proj.project(c.x, c.y, c.z);
                const int xi = static_cast<int>(q.x), yi = static_cast<int>(q.y);
                if (xi < 0 || yi < 0 || xi >= w || yi >= h) {
                    ++misses;
                    continue;
                }
                const unsigned char* p = px + (yi * w + xi) * 4;
                const unsigned char* e =
                        lut + static_cast<int>(std::clamp((cell.value - vmin) / (vmax - vmin), 0.0, 1.0)
                                               * 255.0) * 4;
                if (std::abs(p[0] - e[0]) <= 1 && std::abs(p[1] - e[1]) <= 1 &&
                    std::abs(p[2] - e[2]) <= 1)
                    ++hits;
                else ++misses;
            }
            if (px) stbi_image_free(px);
            check(hits == static_cast<int>(s.cell_count()) && misses == 0,
                  "surface: every cell centre carries its own colour, where the projector puts it");

            // The SVG carries the plan's own pixels verbatim.
            const std::string svg = read_file("surface_cells.svg");
            const std::vector<Surface3DPolygon> plan = plan_surfaces3d(proj, {s});
            // Two polygons per cell (a cell's four samples aren't coplanar, and
            // the painter needs planar polygons).
            check(plan.size() == s.cell_count() * 2,
                  "surface: the SVG plan is two triangles per cell, on the diagonal the "
                  "raster path splits on");
            int found = 0;
            for (const Surface3DPolygon& poly: plan) {
                std::ostringstream pt;
                pt << poly.xy[0] << ',' << poly.xy[1] << ' ' << poly.xy[2] << ',' << poly.xy[3];
                if (svg.find(pt.str()) != std::string::npos) ++found;
            }
            check(found == static_cast<int>(plan.size()),
                  "surface: and every planned cell's own pixels reach the file");
        }

        // ---- A flat surface covers exactly what a plane at the same offset
        // does (different machinery, same coordinate chain).
        {
            const uint8_t* lut = colormaps::get(Colormap::Viridis);
            const Color flat{lut[0] / 255.0f, lut[1] / 255.0f, lut[2] / 255.0f, 1.0f};
            const Range ext{0.05, 0.95};

            FigureSnapshot fs_s = bare(Projection::Orthographic); {
                SurfacePlot sp;
                sp.u = std::vector<double>{ext.lo, ext.hi};
                sp.v = std::vector<double>{ext.lo, ext.hi};
                sp.heights = std::vector<double>(4, 0.5);
                sp.opts.color = flat;
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
                     std::abs(p[2] - lut[2]) <= 2)
                        ? 1
                        : 0;
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
            // The rims differ by antialiasing; the bodies must match.
            check(both > 5000 && only * 100 < both,
                  "surface: a flat surface covers the same pixels as a plane at the same offset");
        }

        // ---- The wireframe, and that shading does something
        {
            FigureSnapshot fs = bare(Projection::Orthographic);
            SurfacePlot sp;
            sp.u = gu;
            sp.v = gv;
            sp.heights = gh;
            sp.opts.color = {0.5f, 0.5f, 0.5f, 1.0f};
            fs.axes[0].snap3d()->surfaces.push_back(sp);
            render(fs, "surface_plain");

            FigureSnapshot fw = fs;
            fw.axes[0].snap3d()->surfaces[0].opts.edges = true;
            fw.axes[0].snap3d()->surfaces[0].opts.edgecolor = {1.0f, 0.0f, 0.0f, 1.0f};
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

            // Per-cell shading: several distinct greys must appear.
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

        // ---- One renderer across frames: rebuilding the surface while the
        // wireframe is off must not lose it when it's turned back on (the cache
        // must record what it was built with). Non-zero data generations, so
        // caching is active.
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
            auto red = [](const unsigned char* p) { return p[0] > 180 && p[1] < 80 && p[2] < 80; };
            auto green = [](const unsigned char* p) { return p[1] > 150 && p[0] < 80 && p[2] < 80; };

            // The surface: "Colour by height" is the other toggle.
            {
                GLContext ctx({
                    .width = W, .height = H,
                    .title = "layout_test", .visible = false
                });
                NvgRenderer nvg(ctx.nvg());
                DataRenderer data_r;

                FigureSnapshot fs = bare(Projection::Orthographic);
                fs.generation = fs.data_generation = 1;
                SurfacePlot sp;
                sp.u = gu;
                sp.v = gv;
                sp.heights = gh;
                sp.opts.color = {0.5f, 0.5f, 0.5f, 1.0f};
                sp.opts.edgecolor = {1.0f, 0.0f, 0.0f, 1.0f};
                sp.opts.edge_linewidth = 2.0f;
                fs.axes[0].snap3d()->surfaces.push_back(sp);

                auto frame = [&](bool colormap, bool edges, const std::string& stem) {
                    SurfaceOptions& o = fs.axes[0].snap3d()->surfaces[0].opts;
                    o.colormap = colormap;
                    o.edges = edges;
                    export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1);
                };
                frame(false, true, "surface_toggle_a"); // the wireframe, as a baseline
                frame(false, false, "surface_toggle_b"); // off
                frame(true, false, "surface_toggle_c"); // a rebuild while it is off
                frame(true, true, "surface_toggle_d"); // and asked for again
                check(count_px("surface_toggle_a", red) > 300 &&
                      count_px("surface_toggle_b", red) == 0 &&
                      count_px("surface_toggle_d", red) > 300,
                      "surface: the wireframe comes back after the colormap rebuilt the "
                      "buffer without it");
            }

            // The bar grid's edges, with a shading change as the toggle.
            {
                GLContext ctx({
                    .width = W, .height = H,
                    .title = "layout_test", .visible = false
                });
                NvgRenderer nvg(ctx.nvg());
                DataRenderer data_r;

                FigureSnapshot fs = bare(Projection::Orthographic);
                fs.generation = fs.data_generation = 1;
                Bar3DPlot bp;
                bp.u = std::vector<double>{0.35, 0.65};
                bp.v = std::vector<double>{0.35, 0.65};
                bp.heights = std::vector<double>{0.4, 0.6, 0.5, 0.7};
                bp.u_width = bp.v_width = 0.2;
                bp.opts.color = {0.5f, 0.5f, 0.5f, 1.0f};
                bp.opts.edgecolor = {0.0f, 1.0f, 0.0f, 1.0f};
                bp.opts.edge_linewidth = 2.0f;
                fs.axes[0].snap3d()->bars3d.push_back(bp);

                auto frame = [&](float shading, bool edges, const std::string& stem) {
                    Bar3DOptions& o = fs.axes[0].snap3d()->bars3d[0].opts;
                    o.shading = shading;
                    o.edges = edges;
                    export_figure_png(ctx, nvg, data_r, fs, stem + ".png", W, H, 1);
                };
                frame(0.5f, true, "bar3d_toggle_a");
                frame(0.5f, false, "bar3d_toggle_b");
                frame(0.0f, false, "bar3d_toggle_c");
                frame(0.0f, true, "bar3d_toggle_d");
                check(count_px("bar3d_toggle_a", green) > 300 &&
                      count_px("bar3d_toggle_b", green) == 0 &&
                      count_px("bar3d_toggle_d", green) > 300,
                      "bar3d: the edges come back after a shading change rebuilt the "
                      "buffer without them");
            }
        }
    }

    // ---------------------------------------------------------------------------
    // A surface under the pointer and in the panels: op addressing across the
    // thread boundary, and tooltip/picture agreement.
    // ---------------------------------------------------------------------------

    void test_surface_data_panel() {
        std::printf("\n[3D: the Data panel's surface grid]\n");

        using namespace sextant;

        auto snap = [] {
            RenderSnapshot3D s;
            s.surfaces.push_back(ripple_surface());
            return s;
        };

        // ---- The three cell-edit columns (no base column).
        {
            RenderSnapshot3D s = snap();
            apply_plot_data_ops(s, {
                                    PlotCellEdit{PlotKind::Surface, 0, 0, 2, 9.5}, // u[2]
                                    PlotCellEdit{PlotKind::Surface, 0, 1, 0, -1.5}, // v[0]
                                    PlotCellEdit{PlotKind::Surface, 0, 2, 4, 42.0}, // heights[1][1]
                                    PlotCellEdit{PlotKind::Surface, 0, 3, 4, -7.0}, // no such column
                                });
            const SurfacePlot& sp = s.surfaces[0];
            check(sp.u[2] == 9.5 && sp.v[0] == -1.5,
                  "surface panel: a cell edit reaches the grid's own coordinates");
            check(sp.heights[4] == 42.0,
                  "surface panel: and its matrix, at the row-major index");
            check(sp.u[0] == 0.0 && sp.v[2] == 5.0 && sp.heights[3] == 4.0,
                  "surface panel: leaving every neighbour alone");
        }

        // ---- Structural: MatrixLineEdit re-strides coordinate, matrix and labels.
        {
            RenderSnapshot3D s = snap();
            apply_plot_data_ops(s, {
                                    MatrixLineEdit{
                                        MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                                        0, 1, -1, PlotKind::Surface
                                    },
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

        // ---- The edit lane refuses to go below 2 lines (a 1 x n grid has no
        // cells).
        {
            RenderSnapshot3D s = snap();
            apply_plot_data_ops(s, {
                                    MatrixLineEdit{
                                        MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                                        0, 0, -1, PlotKind::Surface
                                    },
                                });
            check(s.surfaces[0].u.size() == 2 && s.surfaces[0].heights.size() == 6,
                  "surface panel: removing a u line takes its row of the matrix with it");
        }

        // ---- Addressing: a surface op doesn't reach a bar grid with the same
        // index, nor a plane's objects.
        {
            RenderSnapshot3D s = two_plane_snapshot();
            s.bars3d.push_back(bar3d_grid());
            s.surfaces.push_back(ripple_surface());
            apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Surface, 0, 2, 0, 99.0, 0}});
            check(s.surfaces[0].heights[0] == 1.0,
                  "surface panel: a Surface op addressed at a plane does not reach the axes' surface");
            apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Surface, 0, 2, 0, 99.0, -1}});
            check(s.surfaces[0].heights[0] == 99.0,
                  "surface panel: (and at the axes it does, so that is the address doing it)");
            check(s.bars3d[0].heights[0] == 1.0,
                  "surface panel: and it leaves the bar3d grid at the same index alone");
            apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Bar3D, 0, 2, 0, 55.0, -1}});
            check(s.bars3d[0].heights[0] == 55.0 && s.surfaces[0].heights[0] == 99.0,
                  "surface panel: (and the bar3d op reaches the bars and not the surface)");
        }

        // ---- The tables the panel enumerates.
        {
            RenderSnapshot3D s = two_plane_snapshot();
            s.bars3d.push_back(bar3d_grid());
            s.surfaces.push_back(ripple_surface());
            const std::vector<PlotDataTable> t = collect_plot_data_tables(s);
            int grids = 0, surfaces = 0;
            for (const PlotDataTable& x: t) {
                if (x.is_grid()) ++grids;
                if (x.surface) ++surfaces;
            }
            check(grids == 2 && surfaces == 1,
                  "surface panel: a surface is enumerated as the grid table shape, beside the bar grid");
            for (const PlotDataTable& x: t)
                if (x.kind == PlotKind::Surface)
                    check(x.plane_index == -1 && x.surface == &s.surfaces[0],
                          "surface panel: on the axes, plane -1, pointing at its own plot");
        }
    }

    void test_surface_hints() {
        std::printf("\n[3D: hover hints over a surface]\n");

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

        // A tilted surface over the bar tests' span (non-degenerate from every
        // camera below).
        SurfacePlot s;
        s.u = std::vector<double>{1.0, 4.0, 7.0, 9.0};
        s.v = std::vector<double>{1.0, 4.0, 7.0, 9.0};
        for (std::size_t i = 0; i < 4; ++i)
            for (std::size_t j = 0; j < 4; ++j)
                s.heights.mut().push_back(3.0 + 0.4 * static_cast<double>(i)
                                          + 0.7 * static_cast<double>(j));

        for (int mode = 0; mode < 2; ++mode) {
            cam.projection = mode ? Projection::Perspective : Projection::Orthographic;
            const char* what = mode ? "perspective" : "orthographic";
            const Projector3D proj(tf, cam, frame, 0.1f);

            // ---- The ray through a cell's projected centre meets the surface (at
            // that cell or a neighbour), at the centre's depth.
            int cells = 0, met = 0;
            SurfaceCell cell;
            for (std::size_t k = 0; k < s.cell_count(); ++k) {
                surface_cell(s, k / s.cell_cols(), k % s.cell_cols(), tf, cell);
                Vec3 c{0.0, 0.0, 0.0};
                for (const Vec3& p: cell.p) c = c + p * 0.25;
                const Px3 px = proj.project(c.x, c.y, c.z);
                if (!px.in_front()) continue;
                ++cells;
                float depth = 0.0f;
                std::size_t sample = 0;
                if (surface_ray_hit(s, k, proj, px.x, px.y, depth, sample)
                    && std::fabs(depth - px.depth) < 1e-3f)
                    ++met;
            }
            check(cells > 0 && met == cells,
                  std::string("surface hints: the ray through a cell's centre meets that cell, "
                      "all ") + std::to_string(cells) + " of them, at its own depth ("
                  + what + ")");

            // ---- And misses outside it.
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

            // ---- The tooltip names the hit cell's nearest corner (the hint_labels
            // index); aimed at each corner in turn.
            int corners = 0, named = 0;
            for (std::size_t k = 0; k < s.cell_count(); ++k) {
                const std::size_t i = k / s.cell_cols(), j = k % s.cell_cols();
                surface_cell(s, i, j, tf, cell);
                const std::size_t want[4] = {
                    s.index_of(i, j), s.index_of(i + 1, j),
                    s.index_of(i + 1, j + 1), s.index_of(i, j + 1)
                };
                for (int c = 0; c < 4; ++c) {
                    // 80% from the cell centre toward this corner.
                    Vec3 mid{0.0, 0.0, 0.0};
                    for (const Vec3& p: cell.p) mid = mid + p * 0.25;
                    const Vec3 aim = mid + (cell.p[c] - mid) * 0.8;
                    const Px3 px = proj.project(aim.x, aim.y, aim.z);
                    if (!px.in_front()) continue;
                    ++corners;
                    float depth = 0.0f;
                    std::size_t sample = 0;
                    if (surface_ray_hit(s, k, proj, px.x, px.y, depth, sample)
                        && sample == want[c])
                        ++named;
                }
            }
            check(corners > 0 && named == corners,
                  std::string("surface hints: the sample reported is the nearest of the cell's "
                      "four, all ") + std::to_string(corners) + " (" + what + ")");
        }

        // ---- The tooltip text, with the label below.
        {
            const Projector3D proj(tf, cam, frame, 0.1f);
            RenderSnapshot3D snap;
            SurfacePlot ls = s;
            ls.opts.hint_labels.assign(ls.count(), std::string());
            ls.opts.hint_labels[ls.index_of(1, 1)] = "the middle";
            snap.surfaces.push_back(ls);

            SurfaceCell cell;
            surface_cell(ls, 1, 1, tf, cell);
            // Aim at sample (1,1) so its own label is reported.
            Vec3 mid{0.0, 0.0, 0.0};
            for (const Vec3& p: cell.p) mid = mid + p * 0.25;
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

        // ---- Against the rest of the scene: a plane between eye and surface
        // wins; behind it, it doesn't.
        {
            const Projector3D proj(tf, cam, frame, 0.1f);
            SurfaceCell cell;
            surface_cell(s, 1, 1, tf, cell);
            Vec3 mid{0.0, 0.0, 0.0};
            for (const Vec3& p: cell.p) mid = mid + p * 0.25;
            const Px3 px = proj.project(mid.x, mid.y, mid.z);

            // The plane carries a line through the cursor ray's hit point, so it
            // has something to report.
            auto with_plane = [&](double z) {
                RenderSnapshot3D snap;
                snap.surfaces.push_back(s);
                PlaneSnapshot pl;
                pl.orient = PlaneOrientation::XY;
                pl.offset = z;
                double hu = 0.0, hv = 0.0;
                float hd = 0.0f;
                plane_ray_hit(proj, pl.orient, pl.offset, px.x, px.y, hu, hv, hd);
                LinePlot lp;
                lp.x = std::vector<double>{hu - 1.0, hu, hu + 1.0};
                lp.y = std::vector<double>{hv, hv, hv};
                pl.sheet.lines.push_back(std::move(lp));
                snap.planes.push_back(std::move(pl));
                return snap;
            };
            // From elevation 24 a higher plane is nearer (checked via the
            // projector).
            const Px3 hi = proj.project(mid.x, mid.y, mid.z + 3.0);
            const Px3 lo = proj.project(mid.x, mid.y, mid.z - 3.0);
            const double near_z = (hi.depth < lo.depth) ? mid.z + 3.0 : mid.z - 3.0;
            const double far_z = (hi.depth < lo.depth) ? mid.z - 3.0 : mid.z + 3.0;

            const RenderSnapshot3D in_front = with_plane(near_z);
            const RenderSnapshot3D behind = with_plane(far_z);
            const auto a = find_hint3d(in_front, proj, px.x, px.y, nullptr);
            const auto b = find_hint3d(behind, proj, px.x, px.y, nullptr);
            // The two answers differ in content (two vs three coordinates).
            check(b.has_value() && b->text.find("z=") != std::string::npos,
                  "surface hints: a plane behind the surface does not take the answer from it");
            check(a.has_value() && a->text.find("z=") == std::string::npos,
                  "surface hints: and a plane in front of it does");
        }
    }

    // A surface's colorbar (from the axes' own kinds). With vmin == vmax the bar is
    // labelled with the resolved (data) range.
    void test_surface_colorbar() {
        std::printf("\n[3D: a surface's colorbar]\n");

        using namespace sextant;

        constexpr int W = 460, H = 420;

        // Heights away from 0..1, so resolved and declared ranges differ.
        SurfacePlot s;
        s.u = CowVec<double>{std::vector<double>{0.0, 1.0, 2.0}};
        s.v = CowVec<double>{std::vector<double>{0.0, 1.0, 2.0}};
        s.heights = CowVec<double>{
            std::vector<double>{
                20.0, 30.0, 40.0, 30.0, 55.0, 60.0, 40.0, 60.0, 80.0
            }
        };
        s.opts.colormap = true;
        s.opts.colorbar = true;

        auto with_surface = [&](SurfacePlot sp) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            fs.axes[0].snap3d()->surfaces.push_back(std::move(sp));
            return fs;
        }; {
            const auto reqs = find_colorbar_requests(*with_surface(s).axes[0].snap3d());
            check(reqs.size() == 1, "surface cb: a surface that asks for a bar gets one");
            check(reqs[0].vmin == 20.0f && reqs[0].vmax == 80.0f,
                  "surface cb: spanning its own heights, since vmin == vmax means exactly that");
        }

        // Declared limits are used as given.
        {
            SurfacePlot fixed = s;
            fixed.opts.vmin = 0.0f;
            fixed.opts.vmax = 100.0f;
            const auto reqs = find_colorbar_requests(*with_surface(fixed).axes[0].snap3d());
            check(reqs.size() == 1 && reqs[0].vmin == 0.0f && reqs[0].vmax == 100.0f,
                  "surface cb: and a declared range is taken as declared");
        }

        // Only with `colormap` on.
        {
            SurfacePlot flat = s;
            flat.opts.colormap = false;
            check(find_colorbar_requests(*with_surface(flat).axes[0].snap3d()).empty(),
                  "surface cb: a flat-coloured surface gets none, whatever the flag says");
        }

        // The axes' own kinds before the planes' (as the legend orders them).
        {
            HeatmapOptions hcb;
            hcb.colorbar = true;
            hcb.vmin = 0.0f;
            hcb.vmax = 1.0f;
            FigureSnapshot fs = with_surface(s);
            fs.axes[0].snap3d()->planes.push_back(
                make_plane(PlaneOrientation::XY, 0.0, std::vector<float>(4, 0.5f), 2, 2,
                           {0.0, 1.0}, {0.0, 1.0}, hcb));
            const auto reqs = find_colorbar_requests(*fs.axes[0].snap3d());
            check(reqs.size() == 2 && reqs[0].vmax == 80.0f && reqs[1].vmax == 1.0f,
                  "surface cb: the axes' own bar comes before a plane's");

            const CellDecorations dec = compute_cell_decorations(*fs.axes[0].snap3d());
            check(dec.colorbars.size() == 2
                  && dec.colorbars[0].block > dec.colorbars[1].block,
                  "surface cb: and is the wider block, being labelled 20..80 against 0..1");
        }

        // Into a file via the public API: the bar shows the resolved numbers.
        {
            auto fig = Figure::create({.width = W, .height = H});
            auto ax3 = fig->add_subplot3d(1, 1, 1);
            const std::vector<double> u{0.0, 1.0, 2.0}, v{0.0, 1.0, 2.0};
            const std::vector<double> h{20.0, 30.0, 40.0, 30.0, 55.0, 60.0, 40.0, 60.0, 80.0};
            ax3->surface(PlaneOrientation::XY, u, v, h,
                         {.colormap = true, .colorbar = true});
            fig->savefig("surface_colorbar.svg");

            std::ifstream f("surface_colorbar.svg", std::ios::binary);
            const std::string svg((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
            const std::size_t bar = svg.find("url(#colorbarGrad0_0)");
            check(bar != std::string::npos,
                  "surface cb: Axes3D::surface({.colorbar = true}) puts a bar in the file");
            // Searched after the bar's rect (a z tick label also reads "80").
            const std::string tail = bar == std::string::npos ? std::string() : svg.substr(bar);
            check(tail.find(">80</text>") != std::string::npos
                  && tail.find(">20</text>") != std::string::npos,
                  "surface cb: labelled with the heights it resolved, not with 0 and 0");
        }
    }

    // A flat surface is keyed in the legend; a colormapped one isn't (it has a
    // colorbar). One label serves whichever applies.
    void test_surface_legend_key() {
        std::printf("\n[3D: a flat surface's legend key]\n");

        using namespace sextant;

        auto build = [](bool colormapped) {
            RenderSnapshot3D r;
            r.legend_enabled = true;
            SurfacePlot s;
            s.u = CowVec<double>{std::vector<double>{0.0, 1.0, 2.0}};
            s.v = CowVec<double>{std::vector<double>{0.0, 1.0, 2.0}};
            s.heights = CowVec<double>{
                std::vector<double>{
                    20.0, 30.0, 40.0, 30.0, 55.0, 60.0, 40.0, 60.0, 80.0
                }
            };
            s.opts.color = Color::Cyan;
            s.opts.name = "terrain";
            s.opts.colormap = colormapped;
            // Requested either way, so the gate is `colormap`.
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

        // Each half of the label where it belongs: flat = key, mapped = bar.
        check(find_colorbar_requests(flat).empty(),
              "surface key: a flat surface asks for no colorbar, whatever the flag says");
        check(find_colorbar_requests(mapped).size() == 1
              && find_colorbar_requests(mapped)[0].name == "terrain",
              "surface key: and a mapped one puts the same label on its bar");

        // `show_legend` gates the key without clearing the text.
        RenderSnapshot3D off = build(false);
        off.surfaces[0].opts.show_legend = false;
        check(collect_legend_entries(off).empty()
              && off.surfaces[0].opts.name == "terrain",
              "surface key: show_legend switches it off without clearing the name");

        // Ordered with the axes' own kinds, before any plane's.
        RenderSnapshot3D both = build(false);
        Bar3DPlot g;
        g.u = CowVec<double>{std::vector<double>{0.0, 1.0}};
        g.v = CowVec<double>{std::vector<double>{0.0, 1.0}};
        g.heights = CowVec<double>{std::vector<double>{1.0, 2.0, 3.0, 4.0}};
        g.opts.name = "counts";
        both.bars3d.push_back(g);
        const auto eb = collect_legend_entries(both);
        check(eb.size() == 2 && eb[0].name == "counts" && eb[1].name == "terrain",
              "surface key: bar grids before surfaces, both before any plane's keys");
    }
} // namespace lt
