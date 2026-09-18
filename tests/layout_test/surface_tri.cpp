// surface_tri: ingest and topology, per-fragment color, the SVG per-triangle
// gradient, keys, hover/panel, and Delaunay. Part of sextant_layout_test; see
// layout_test.h.
#include "layout_test.h"
#include "renderer/surface_tri.h"
#include "delaunay.h"
#include "plot_data_view.h"
#include "hint.h"
#include "widgets/data_panel.h"

namespace lt {
    namespace {
        // A unit square as two triangles at z = `h`, corners (0,0) (1,0) (1,1) (0,1).
        sextant::SurfaceTriPlot unit_quad(double h = 0.5) {
            using namespace sextant;
            SurfaceTriPlot m;
            m.x = std::vector<double>{0.0, 1.0, 1.0, 0.0};
            m.y = std::vector<double>{0.0, 0.0, 1.0, 1.0};
            m.z = std::vector<double>{h, h, h, h};
            m.tri = std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3};
            return m;
        }
    } // namespace

    // -------------------------------------------------------------------------
    // Ingest: overloads, throws, topology accessors, auto-scale, has_data gate
    // -------------------------------------------------------------------------
    void test_surface_tri_ingest() {
        std::printf("\n[3D: surface_tri ingest, topology and auto-scale]\n");

        using namespace sextant;

        auto threw = [](auto&& fn) {
            try {
                fn();
                return false;
            } catch (const std::invalid_argument&) { return true; }
        };

        const std::vector<double> x{0.0, 1.0, 1.0, 0.0};
        const std::vector<double> y{0.0, 0.0, 1.0, 1.0};
        const std::vector<double> z{0.0, 0.5, 1.0, 0.25};
        const std::vector<double> c{10.0, 20.0, 40.0, 30.0};
        const std::vector<std::uint32_t> tri{0, 1, 2, 0, 2, 3};

        auto fig = Figure::create({.width = 300, .height = 240});
        auto ax = fig->add_subplot3d(1, 1, 1);

        // ---- Vertex rules: at least three vertices.
        const std::vector<double> two{0.0, 1.0};
        check(threw([&] { ax->surface_tri({}, {}, {}, std::span<const std::uint32_t>{}); }),
              "surface_tri: an empty mesh throws");
        check(threw([&] { ax->surface_tri(two, two, two, tri); }),
              "surface_tri: and so does a mesh of two vertices, which spans no area");
        check(threw([&] { ax->surface_tri(x, y, {z.data(), 3}, tri); }),
              "surface_tri: three coordinate vectors of different lengths throw");
        check(threw([&] { ax->surface_tri(x, y, z, tri, {c.data(), 3}); }),
              "surface_tri: a colors vector that is neither empty nor |x| long throws -- a "
              "value belongs to a vertex, and a triangle interpolates between its three");
        const std::vector<double> nan_z{
            0.0, std::numeric_limits<double>::quiet_NaN(),
            1.0, 0.0
        };
        check(threw([&] { ax->surface_tri(x, y, nan_z, tri); }),
              "surface_tri: a non-finite coordinate throws, once, rather than reaching the "
              "buffer");
        check(threw([&] {
                  SurfaceTriOptions o;
                  o.vmin = std::numeric_limits<float>::infinity();
                  ax->surface_tri(x, y, z, tri, c, o);
              }),
              "surface_tri: a non-finite vmin/vmax throws -- it is a divisor downstream");

        // ---- Topology rules.
        check(threw([&] { ax->surface_tri(x, y, z, std::span<const std::uint32_t>{}); }),
              "surface_tri: an empty tri throws -- a mesh that draws nothing while still "
              "feeding the limits and the legend is the silence line3d rejects for a "
              "one-point path");
        const std::vector<std::uint32_t> ragged{0, 1, 2, 0};
        check(threw([&] { ax->surface_tri(x, y, z, ragged); }),
              "surface_tri: a tri that is not a multiple of three throws -- three vertex "
              "indices per triangle, row-major");
        const std::vector<std::uint32_t> oob{0, 1, 4};
        check(threw([&] { ax->surface_tri(x, y, z, oob); }),
              "surface_tri: an index past the last vertex throws, rather than silently "
              "naming vertex 0");
        check(!threw([&] { ax->surface_tri(x, y, z, tri); }),
              "surface_tri: four vertices and two triangles are a mesh");
        check(!threw([&] { ax->surface_tri(x, y, z, tri, c); }),
              "surface_tri: and so is one with a fourth colour dimension");

        // Degenerate triangles are kept, so the face count matches the caller's.
        {
            const std::vector<std::uint32_t> flat{0, 1, 2, 0, 0, 1};
            auto f2 = Figure::create({.width = 200, .height = 160});
            auto a2 = f2->add_subplot3d(1, 1, 1);
            check(!threw([&] { a2->surface_tri(x, y, z, flat); }),
                  "surface_tri: a degenerate (zero-area) triangle is accepted at ingest "
                  "rather than dropped -- dropping rows would make the caller's face "
                  "count and ours disagree, and a face count is how a caller checks "
                  "their own mesh");

            // Its normal is guarded: it takes the light head-on (not NaN, not
            // black).
            SurfaceTriPlot m;
            m.x = x;
            m.y = y;
            m.z = z;
            m.tri = flat;
            m.opts.shading = 0.9f;
            Transform3D tf3;
            tf3.xmin = 0;
            tf3.xmax = 1;
            tf3.ymin = 0;
            tf3.ymax = 1;
            tf3.zmin = 0;
            tf3.zmax = 1;
            SurfaceTriFace face;
            surface_tri_face(m, 1, tf3, face);
            check(m.face_count() == 2 && face.shade == 1.0f,
                  "surface_tri: and a degenerate face has no normal, so it takes the "
                  "light head-on rather than going black or NaN");
        }

        // ---- Topology accessors.
        {
            SurfaceTriPlot m = unit_quad();
            check(m.count() == 4 && m.face_count() == 2,
                  "surface_tri: |x| vertices and |tri|/3 faces");
            std::size_t a = 9, b = 9, cc = 9;
            m.face_verts(0, a, b, cc);
            const bool f0 = (a == 0 && b == 1 && cc == 2);
            m.face_verts(1, a, b, cc);
            const bool f1 = (a == 0 && b == 2 && cc == 3);
            check(f0 && f1,
                  "surface_tri: face_verts() is the one definition of which vertices a face "
                  "joins -- the buffer, the SVG plan, the wireframe and the ray cast all "
                  "read it rather than re-deriving 3f + k");
            m.face_verts(7, a, b, cc);
            check(a == 0 && b == 0 && cc == 0,
                  "surface_tri: a face past the end reads as vertex 0 rather than off the "
                  "end -- a guard for a hand-built snapshot, since ingest rejects every "
                  "index the public API could produce");

            SurfaceTriPlot mapped = m;
            mapped.colors = c;
            check(!m.colormapped() && mapped.colormapped(),
                  "surface_tri: a colors vector is the only thing that makes a mesh "
                  "colormapped -- there is no `colormap` flag, because a mesh has no "
                  "height axis to colour by");
        }

        // ---- Color range, as for clouds and paths.
        {
            SurfaceTriPlot m = unit_quad();
            m.colors = c;
            double lo = 0.0, hi = 0.0;
            surface_tri_value_range(m, lo, hi);
            check(lo == 10.0 && hi == 40.0,
                  "surface_tri: an empty vmin/vmax interval means the colors' own range");
            SurfaceTriPlot fixed = m;
            fixed.opts.vmin = 0.0f;
            fixed.opts.vmax = 100.0f;
            surface_tri_value_range(fixed, lo, hi);
            check(lo == 0.0 && hi == 100.0, "surface_tri: and a stated one is used as stated");
            SurfaceTriPlot flat = unit_quad();
            surface_tri_value_range(flat, lo, hi);
            check(lo == 0.0 && hi == 1.0,
                  "surface_tri: a mesh with no colors has no range to take, and says 0..1");
        }

        // ---- Auto-scale: the vertices are the extent.
        {
            SurfaceTriPlot m;
            m.x = x;
            m.y = y;
            m.z = z;
            m.tri = tri;
            const DataBounds3D b = auto_scale3d({}, {}, {}, {}, {}, {m}, 0.0);
            check(b.xmin == 0.0 && b.xmax == 1.0 && b.ymin == 0.0 && b.ymax == 1.0 &&
                  b.zmin == 0.0 && b.zmax == 1.0,
                  "surface_tri: its extent is exactly its vertices, on all three axes at once");

            // Including vertices no triangle uses (limits describe the data).
            SurfaceTriPlot lone = m;
            auto lx = x;
            lx.push_back(5.0);
            auto ly = y;
            ly.push_back(0.0);
            auto lz = z;
            lz.push_back(0.0);
            lone.x = lx;
            lone.y = ly;
            lone.z = lz;
            const DataBounds3D lb = auto_scale3d({}, {}, {}, {}, {}, {lone}, 0.0);
            check(lb.xmax == 5.0,
                  "surface_tri: a vertex named by no triangle still feeds the limits");

            // Mixed with another kind, to show the mesh reaches auto_scale3d().
            Bar3DPlot bars;
            bars.u = std::vector<double>{3.0};
            bars.v = std::vector<double>{3.0};
            bars.heights = std::vector<double>{3.0};
            bars.u_width = bars.v_width = 0.0;
            const DataBounds3D both = auto_scale3d({bars}, {}, {}, {}, {}, {m}, 0.0);
            const DataBounds3D bar_only = auto_scale3d({bars}, {}, {}, {}, {}, {}, 0.0);
            // The bar alone sits on one coordinate, widened to +-0.5 by the
            // degenerate-interval rule.
            check(both.xmin == 0.0 && bar_only.xmin > 2.0 && both.xmax == 3.0,
                  "surface_tri: and a mesh widens a box the other kinds had to themselves");
        }

        // ---- prepare()'s has_data gate: an axes with only a mesh must
        // auto-scale (unit checks alone missed this for line3d).
        {
            auto f2 = Figure::create({.width = 400, .height = 320});
            auto a2 = f2->add_subplot3d(1, 1, 1);
            const std::vector<double> mx{2.0, 6.0, 6.0};
            const std::vector<double> my{2.0, 2.0, 6.0};
            const std::vector<double> mz{2.0, 2.0, 6.0};
            a2->surface_tri(mx, my, mz, std::vector<std::uint32_t>{0, 1, 2});
            f2->savefig("surface_tri_box.svg");
            std::ifstream f("surface_tri_box.svg");
            const std::string svg((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
            check(!svg.empty() && svg.find(">5<") != std::string::npos &&
                  svg.find(">0.2<") == std::string::npos,
                  "surface_tri: a figure holding only a mesh resolves its limits from that "
                  "mesh -- the axes are annotated over the mesh's own 2..6 rather than the "
                  "empty axes' 0..1. The has_data gate knows this kind, which is exactly "
                  "the omission step 13 shipped for its own while every unit check passed");
        }
    }

    // -------------------------------------------------------------------------
    // Raster mesh: per-fragment color, flat per-face shade
    // -------------------------------------------------------------------------
    void test_surface_tri_render() {
        std::printf("\n[3D: surface_tri rendered -- the colour inside a triangle]\n");

        using namespace sextant;

        constexpr int W = 420, H = 360;

        // Looking down +x at zero elevation: y is horizontal, z vertical, x depth.
        auto bare = [&]() {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.azimuth = 0.0;
            s->camera.elevation = 0.0;
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
        auto pixel = [](const unsigned char* px, int w, int h, float fx, float fy,
                        float rgb[3]) {
            const int xi = static_cast<int>(std::lround(fx));
            const int yi = static_cast<int>(std::lround(fy));
            if (xi < 0 || xi >= w || yi < 0 || yi >= h) return false;
            const unsigned char* p = px + (yi * w + xi) * 4;
            rgb[0] = p[0] / 255.0f;
            rgb[1] = p[1] / 255.0f;
            rgb[2] = p[2] / 255.0f;
            return true;
        };
        auto lut_color = [](Colormap cm, double t, float out[3]) {
            const int e = static_cast<int>(std::clamp(t, 0.0, 1.0) * 255.0);
            const uint8_t* l = colormaps::get(cm) + e * 4;
            out[0] = l[0] / 255.0f;
            out[1] = l[1] / 255.0f;
            out[2] = l[2] / 255.0f;
        };
        auto dist3 = [](const float a[3], const float b[3]) {
            return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1])
                             + (a[2] - b[2]) * (a[2] - b[2]));
        };

        // ---- The color inside a triangle must equal the colormap of the
        // interpolated value (a per-corner RGB blend agrees only at vertices). The
        // wrong answer is printed beside the right one.
        {
            FigureSnapshot fs = bare();
            SurfaceTriPlot m;
            // One triangle in the screen plane, at constant depth.
            m.x = std::vector<double>{0.5, 0.5, 0.5};
            m.y = std::vector<double>{0.15, 0.85, 0.50};
            m.z = std::vector<double>{0.15, 0.15, 0.85};
            m.tri = std::vector<std::uint32_t>{0, 1, 2};
            m.colors = std::vector<double>{0.0, 0.5, 1.0};
            m.opts.vmin = 0.0f;
            m.opts.vmax = 1.0f;
            m.opts.shading = 0.0f; // so the shade cannot mask the colour
            fs.axes[0].snap3d()->surface_tri.push_back(m);
            render(fs, "surface_tri_ramp");

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load("surface_tri_ramp.png", &w, &h, &comp, 4);

            float got[3] = {0, 0, 0}, want[3] = {0, 0, 0}, blend[3] = {0, 0, 0};
            bool read = false;
            if (px) {
                // The centroid, where the interpolated value is the corners' mean.
                Px3 q[3];
                for (int i = 0; i < 3; ++i) q[i] = proj.project(m.x[i], m.y[i], m.z[i]);
                const float cx = (q[0].x + q[1].x + q[2].x) / 3.0f;
                const float cy = (q[0].y + q[1].y + q[2].y) / 3.0f;
                read = pixel(px, w, h, cx, cy, got);
                lut_color(Colormap::Viridis, 0.5, want);
                // What the rejected rule would draw: the mean of the corner colors.
                for (int i = 0; i < 3; ++i) {
                    float c3[3];
                    lut_color(Colormap::Viridis, m.colors[i], c3);
                    for (int k = 0; k < 3; ++k) blend[k] += c3[k] / 3.0f;
                }
                stbi_image_free(px);
            }
            std::printf("  inside a triangle: drawn (%.2f,%.2f,%.2f) vs colormap "
                        "(%.2f,%.2f,%.2f); a corner blend would be (%.2f,%.2f,%.2f)\n",
                        got[0], got[1], got[2], want[0], want[1], want[2],
                        blend[0], blend[1], blend[2]);
            check(read && dist3(got, want) < 0.06f,
                  "surface_tri: a point inside a triangle is the colormap at that point's "
                  "interpolated *value* -- the lookup is per fragment");
            check(read && dist3(got, blend) > 0.15f,
                  "surface_tri: and measurably not the blend of its three corners' colours, "
                  "which is the rule that agrees at every vertex and nowhere else");
        }

        // ---- Shade is flat per face and two-sided: two faces tilted differently
        // each have one brightness, and they differ.
        {
            FigureSnapshot fs = bare();
            // Two faces disjoint on screen (a probe crossing a shared seam would
            // read the other face). The camera looks along +x, so the screen is
            // (y, z); one face is at constant x, the other tilted.
            SurfaceTriPlot m;
            m.x = std::vector<double>{0.50, 0.50, 0.50, 0.20, 0.80, 0.50};
            m.y = std::vector<double>{0.15, 0.45, 0.30, 0.55, 0.85, 0.70};
            m.z = std::vector<double>{0.20, 0.20, 0.55, 0.20, 0.20, 0.55};
            // The second face is wound the other way; with |n.l| both are lit.
            m.tri = std::vector<std::uint32_t>{0, 1, 2, 5, 4, 3};
            // Not white, so a probe that strays onto the background fails.
            m.opts.color = Color{0.8f, 0.2f, 0.2f, 1.0f};
            m.opts.shading = 0.9f;
            fs.axes[0].snap3d()->surface_tri.push_back(m);
            render(fs, "surface_tri_shade");

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load("surface_tri_shade.png", &w, &h, &comp, 4);
            float a1[3] = {0, 0, 0}, a2[3] = {0, 0, 0};
            int spread = 0;
            bool read = false;
            if (px) {
                // Two probes inside each face, 1/4 and 3/4 from centroid to corner.
                auto probe_face = [&](std::size_t f, float out[3], int& worst) {
                    std::size_t vi[3];
                    m.face_verts(f, vi[0], vi[1], vi[2]);
                    Px3 q[3];
                    for (int i = 0; i < 3; ++i)
                        q[i] = proj.project(m.x[vi[i]], m.y[vi[i]], m.z[vi[i]]);
                    const float cx = (q[0].x + q[1].x + q[2].x) / 3.0f;
                    const float cy = (q[0].y + q[1].y + q[2].y) / 3.0f;
                    pixel(px, w, h, cx, cy, out);
                    for (int i = 0; i < 3; ++i) {
                        float s[3];
                        if (!pixel(px, w, h, cx + (q[i].x - cx) * 0.5f,
                                   cy + (q[i].y - cy) * 0.5f, s))
                            continue;
                        const int d = static_cast<int>(std::lround(
                            std::fabs(s[0] - out[0]) * 255.0f));
                        worst = std::max(worst, d);
                    }
                };
                probe_face(0, a1, spread);
                probe_face(1, a2, spread);
                read = true;
                stbi_image_free(px);
            }
            std::printf("  roof faces: %.0f and %.0f (of 255); worst within-face spread %d\n",
                        a1[0] * 255.0f, a2[0] * 255.0f, spread);
            check(read && spread <= 2,
                  "surface_tri: a face is one brightness throughout -- the shade is flat "
                  "per face, where the colour is per vertex");
            check(read && std::fabs(a1[0] - a2[0]) > 0.05f,
                  "surface_tri: and two faces at different tilts are lit differently, so it "
                  "is per face rather than per mesh");
            check(read && a1[0] > 0.05f && a2[0] > 0.05f,
                  "surface_tri: neither face goes black, though they are wound oppositely "
                  "-- the shade is |n.l| and nothing is ever backface-culled, because a "
                  "sheet has two sides and no outside");
        }

        // ---- The wireframe: all three edges of every face.
        {
            auto wire_pixels = [&](bool on) {
                FigureSnapshot fs = bare();
                SurfaceTriPlot m = unit_quad();
                // Inside the box's span, so the mesh is well within the frame.
                m.x = std::vector<double>{0.5, 0.5, 0.5, 0.5};
                m.y = std::vector<double>{0.15, 0.85, 0.85, 0.15};
                m.z = std::vector<double>{0.15, 0.15, 0.85, 0.85};
                m.opts.color = Color::White;
                m.opts.shading = 0.0f;
                m.opts.edges = on;
                m.opts.edgecolor = Color::Red;
                m.opts.edge_linewidth = 3.0f;
                fs.axes[0].snap3d()->surface_tri.push_back(m);
                render(fs, on ? "surface_tri_wire_on" : "surface_tri_wire_off");
                int w = 0, h = 0, comp = 0;
                unsigned char* px = stbi_load(on
                                                  ? "surface_tri_wire_on.png"
                                                  : "surface_tri_wire_off.png",
                                              &w, &h, &comp, 4);
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
            const int off = wire_pixels(false), on = wire_pixels(true);
            std::printf("  wireframe pixels off/on: %d/%d\n", off, on);
            check(off == 0 && on > 100,
                  "surface_tri: the wireframe draws, and only when it is asked for");
        }
    }

    // -------------------------------------------------------------------------
    // SVG: the painter and the per-triangle gradient
    // -------------------------------------------------------------------------
    void test_surface_tri_svg() {
        std::printf("\n[3D: a mesh through a sheet, and its gradients]\n");

        using namespace sextant;

        constexpr int W = 460, H = 400;

        // A mesh crossing a flat sheet, so faces are both in front and behind.
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

            SurfacePlot sheet;
            sheet.orient = PlaneOrientation::YZ;
            sheet.u = std::vector<double>{0.0, 0.5, 1.0};
            sheet.v = std::vector<double>{0.0, 0.5, 1.0};
            sheet.heights = std::vector<double>(9, 0.5);
            sheet.opts.color = Color::Green;
            sheet.opts.shading = 0.0f;
            sheet.opts.alpha = sheet_alpha;
            s->surfaces.push_back(std::move(sheet));

            // A fan straddling x = 0.5: every face is cut.
            SurfaceTriPlot m;
            std::vector<double> mx, my, mz, mc;
            std::vector<std::uint32_t> tri;
            for (int i = 0; i < 8; ++i) {
                mx.push_back(i % 2 == 0 ? 0.12 : 0.88);
                my.push_back(0.12 + 0.10 * i);
                mz.push_back(0.18 + 0.08 * i);
                mc.push_back(static_cast<double>(i));
            }
            for (std::uint32_t i = 0; i + 2 < 8; ++i) {
                tri.push_back(i);
                tri.push_back(i + 1);
                tri.push_back(i + 2);
            }
            m.x = mx;
            m.y = my;
            m.z = mz;
            m.colors = mc;
            m.opts.shading = 0.0f;
            m.tri = tri;
            s->surface_tri.push_back(std::move(m));
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
            const std::vector<SurfaceTriPolygon> mplan = plan_surface_tri3d(pj, s->surface_tri);
            PaintOrderStats st;
            const std::vector<ScenePaint> scene =
                    plan_scene3d(pj, {}, splan, {}, {}, {}, mplan, {}, &st);

            check(mplan.size() == 6,
                  "surface_tri/svg: every face of the mesh is planned, and only the faces");
            check(!st.bailed, "surface_tri/svg: the scene stays inside the work bound");

            // ---- Order oracle: for each overlapping (face, sheet cell) pair, the
            // one nearer at the overlap is emitted later. Depths from each plane's
            // ray intersection (exact under perspective).
            std::vector<std::size_t> pos(scene.size());
            int covers = 0, wrong = 0, in_front = 0;
            auto ring_of = [&](const ScenePaint& sp) {
                if (!sp.xy.empty()) return sp.xy;
                if (sp.kind == ScenePaint::Kind::Mesh) return mplan[sp.index].xy;
                return splan[sp.index].xy;
            };
            auto inside = [](const std::vector<float>& xy, float px, float py) {
                if (xy.size() < 6) return false;
                const std::size_t n = xy.size() / 2;
                int sign = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t j = (i + 1) % n;
                    const float ex = xy[j * 2] - xy[i * 2];
                    const float ey = xy[j * 2 + 1] - xy[i * 2 + 1];
                    const float len = std::sqrt(ex * ex + ey * ey);
                    if (len < 1e-6f) continue;
                    const float cr = (ex * (py - xy[i * 2 + 1])
                                      - ey * (px - xy[i * 2])) / len;
                    if (std::fabs(cr) < 0.75f) return false;
                    const int sg = cr > 0 ? 1 : -1;
                    if (sign == 0) sign = sg;
                    else if (sg != sign) return false;
                }
                return sign != 0;
            };
            // Depth where this pixel's ray meets the polygon's plane.
            auto plane_depth = [&](const std::vector<Vec3>& box, float px, float py,
                                   float& out) {
                Vec3 p0, n;
                if (!ring_plane(box, p0, n)) return false;
                const Projector3D::Ray3 r = pj.ray_from_pixel(px, py);
                const double den = dot(r.dir, n);
                if (std::fabs(den) < 1e-12) return false;
                const double t = dot(p0 - r.origin, n) / den;
                const Vec3 hit = r.origin + r.dir * t;
                if (!pj.in_front(hit)) return false;
                out = pj.project_box(hit).depth;
                return true;
            };
            for (std::size_t k = 0; k < scene.size(); ++k) pos[scene[k].index] = k;
            for (std::size_t k = 0; k < scene.size(); ++k) {
                if (scene[k].kind != ScenePaint::Kind::Mesh) continue;
                const std::vector<float> mr = ring_of(scene[k]);
                if (mr.size() < 6) continue;
                float cx = 0.0f, cy = 0.0f;
                for (std::size_t i = 0; i + 1 < mr.size(); i += 2) {
                    cx += mr[i];
                    cy += mr[i + 1];
                }
                cx /= static_cast<float>(mr.size() / 2);
                cy /= static_cast<float>(mr.size() / 2);
                // Several probes per face (a centroid-only probe passes even with
                // SEXTANT_NEWELL=0).
                std::vector<std::pair<float, float>> probes{{cx, cy}};
                for (std::size_t i = 0; i + 1 < mr.size(); i += 2)
                    probes.push_back({
                        cx + (mr[i] - cx) * 0.7f,
                        cy + (mr[i + 1] - cy) * 0.7f
                    });
                for (const auto& [qx, qy]: probes) {
                    float md = 0.0f;
                    if (!plane_depth(mplan[scene[k].index].box, qx, qy, md)) continue;
                    if (!inside(mr, qx, qy)) continue;
                    for (std::size_t j = 0; j < scene.size(); ++j) {
                        if (scene[j].kind != ScenePaint::Kind::Surface) continue;
                        const std::vector<float> sr = ring_of(scene[j]);
                        if (!inside(sr, qx, qy)) continue;
                        float sd = 0.0f;
                        if (!plane_depth(splan[scene[j].index].box, qx, qy, sd)) continue;
                        if (std::fabs(sd - md) < 0.005f) continue; // effectively touching
                        ++covers;
                        const bool mesh_nearer = md < sd;
                        if (mesh_nearer) ++in_front;
                        if (mesh_nearer != (k > j)) ++wrong;
                    }
                }
            }
            std::printf("  %-11s sheet: %zu polys -> %zu, %zu splits, %d covers "
                        "(%d mesh in front), %d misordered\n",
                        alpha < 1.0f ? "translucent" : "opaque",
                        st.input, st.output, st.splits, covers, in_front, wrong);
            check(covers >= 12,
                  "surface_tri/svg: the probe scene really does overlap the two objects, in "
                  "both directions");
            check(in_front > 0 && in_front < covers,
                  "surface_tri/svg: and in both directions, so an order that always put the "
                  "mesh on one side would be caught");
            if (newell_on)
                check(wrong == 0,
                      "surface_tri/svg: every overlapping face is emitted on the right side "
                      "of the sheet -- a mesh face is an ordinary blade and an ordinary "
                      "victim, so the painter needed nothing new for it");
        }

        // ---- A split piece re-derives its gradient axis from its own ring (not
        // the parent's compressed ramp).
        {
            const FigureSnapshot fs = build(0.45f);
            export_figure_svg(fs, "surface_tri_grad.svg", W, H);
            std::ifstream f("surface_tri_grad.svg");
            const std::string svg((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());

            int grads = 0, referenced = 0, outside = 0, stop_short = 0;
            std::size_t p = 0;
            while ((p = svg.find("<linearGradient id=\"meshGrad", p)) != std::string::npos) {
                ++grads;
                const std::size_t end = svg.find("</linearGradient>", p);
                const std::string block = svg.substr(p, end - p);
                int stops = 0;
                std::size_t q = 0;
                while ((q = block.find("<stop ", q)) != std::string::npos) {
                    ++stops;
                    q += 6;
                }
                if (stops < 5) ++stop_short;

                auto attr = [&](const char* name) {
                    const std::size_t a = block.find(name);
                    return a == std::string::npos
                               ? 0.0f
                               : std::strtof(block.c_str() + a + std::strlen(name), nullptr);
                };
                const float x1 = attr("x1=\""), y1 = attr("y1=\"");
                const float x2 = attr("x2=\""), y2 = attr("y2=\"");

                // The polygon this gradient paints is the next one in the file.
                const std::size_t poly = svg.find("<polygon points=\"", end);
                const std::size_t pend = svg.find("\"", poly + 17);
                const std::string pts = svg.substr(poly + 17, pend - poly - 17);
                if (svg.find("url(#meshGrad", pend) != std::string::npos &&
                    svg.find("url(#meshGrad", pend) < pend + 32)
                    ++referenced;

                // The axis length must be this polygon's own extent along the
                // direction; using the parent triangle would make it longer on
                // every cut piece.
                const float len = std::hypot(x2 - x1, y2 - y1);
                const float dx = len > 1e-6f ? (x2 - x1) / len : 1.0f;
                const float dy = len > 1e-6f ? (y2 - y1) / len : 0.0f;
                float tlo = 1e30f, thi = -1e30f;
                const char* c = pts.c_str();
                while (*c) {
                    char* e1 = nullptr;
                    const float vx = std::strtof(c, &e1);
                    if (e1 == c || *e1 != ',') break;
                    char* e2 = nullptr;
                    const float vy = std::strtof(e1 + 1, &e2);
                    const float t = vx * dx + vy * dy;
                    tlo = std::min(tlo, t);
                    thi = std::max(thi, t);
                    c = e2;
                    while (*c == ' ') ++c;
                }
                if (thi > tlo && std::fabs(len - (thi - tlo)) > 0.25f) ++outside;
                p = end;
            }
            std::printf("  svg: %d mesh gradients, %d referenced, %d whose axis does not "
                        "span their own polygon\n", grads, referenced, outside);
            check(grads >= 4,
                  "surface_tri/svg: a colormapped face is filled with a gradient of its own");
            check(stop_short == 0,
                  "surface_tri/svg: each one carries stops sampled from the colormap, not a "
                  "two-stop ramp between two colours -- the straight RGB line between two "
                  "distant entries leaves the map");
            check(referenced == grads,
                  "surface_tri/svg: and every one of them is actually referenced by the "
                  "polygon it precedes, rather than emitted beside a flat fill");
            check(outside == 0,
                  "surface_tri/svg: every gradient's axis spans exactly the polygon it "
                  "paints -- which for a split piece is the piece's own ring, not its "
                  "parent's ramp compressed into a fragment of itself");
        }

        // ---- Degenerate cases fall back to a flat fill: edge-on faces and
        // three equal values.
        {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            s->camera.azimuth = 0.0;
            s->camera.elevation = 0.0;
            s->xmin = 0;
            s->xmax = 1;
            s->xlim_auto = false;
            s->ymin = 0;
            s->ymax = 1;
            s->ylim_auto = false;
            s->zmin = 0;
            s->zmax = 1;
            s->zlim_auto = false;
            SurfaceTriPlot m;
            m.x = std::vector<double>{0.5, 0.5, 0.5};
            m.y = std::vector<double>{0.2, 0.8, 0.5};
            m.z = std::vector<double>{0.2, 0.2, 0.8};
            m.tri = std::vector<std::uint32_t>{0, 1, 2};
            m.colors = std::vector<double>{7.0, 7.0, 7.0}; // one value, no ramp
            s->surface_tri.push_back(m);

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const auto plan = plan_surface_tri3d(lay.cells[0].box3d->proj, s->surface_tri);
            check(plan.size() == 1 && plan[0].gx == 0.0f && plan[0].gy == 0.0f,
                  "surface_tri/svg: three equal values give no gradient direction, which is "
                  "the writer's cue to fall back to the flat fill");

            // Edge-on: projects to a line, no screen-space gradient.
            SurfaceTriPlot e;
            e.x = std::vector<double>{0.2, 0.5, 0.8};
            e.y = std::vector<double>{0.5, 0.5, 0.5};
            e.z = std::vector<double>{0.2, 0.5, 0.8};
            e.tri = std::vector<std::uint32_t>{0, 1, 2};
            e.colors = std::vector<double>{0.0, 0.5, 1.0};
            RenderSnapshot3D* s2 = fs.axes[0].snap3d();
            s2->surface_tri.clear();
            s2->surface_tri.push_back(e);
            const auto eplan = plan_surface_tri3d(lay.cells[0].box3d->proj, s2->surface_tri);
            check(eplan.size() == 1 && eplan[0].gx == 0.0f && eplan[0].gy == 0.0f,
                  "surface_tri/svg: and so does a face whose projection is a line");
        }
    }

    // -------------------------------------------------------------------------
    // The legend key and the colorbar
    // -------------------------------------------------------------------------
    void test_surface_tri_legend_and_colorbar() {
        std::printf("\n[3D: a mesh's legend key and colorbar]\n");

        using namespace sextant;

        SurfaceTriPlot m = unit_quad();
        m.colors = std::vector<double>{20.0, 40.0, 60.0, 80.0};
        m.opts.colorbar = true;
        m.opts.name = "mesh";

        auto with_mesh = [&](SurfaceTriPlot mm) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            fs.axes[0].snap3d()->surface_tri.push_back(std::move(mm));
            return fs;
        };

        // ---- The bar
        {
            const auto reqs = find_colorbar_requests(*with_mesh(m).axes[0].snap3d());
            check(reqs.size() == 1 && reqs[0].vmin == 20.0f && reqs[0].vmax == 80.0f,
                  "surface_tri cb: a colormapped mesh that asks for a bar gets one, spanning "
                  "its own colors");
            check(reqs[0].name == "mesh",
                  "surface_tri cb: and the mesh's label names the scale on it");

            SurfaceTriPlot fixed = m;
            fixed.opts.vmin = 0.0f;
            fixed.opts.vmax = 100.0f;
            const auto fr = find_colorbar_requests(*with_mesh(fixed).axes[0].snap3d());
            check(fr.size() == 1 && fr[0].vmin == 0.0f && fr[0].vmax == 100.0f,
                  "surface_tri cb: a declared range is taken as declared");

            SurfaceTriPlot flat = unit_quad();
            flat.opts.colorbar = true;
            check(find_colorbar_requests(*with_mesh(flat).axes[0].snap3d()).empty(),
                  "surface_tri cb: a flat mesh asking for a bar gets none -- there is no "
                  "mapping for it to explain, and no flag to ask on besides the vector");
        }

        // ---- A colormapped mesh has no legend key (surface rule, unlike a path's);
        // asserted beside a path.
        {
            check(collect_legend_entries(*with_mesh(m).axes[0].snap3d()).empty(),
                  "surface_tri legend: a colormapped mesh is not keyed -- it has no one "
                  "colour a swatch could honestly show, and it does have a shape in the "
                  "picture to be recognized by");

            SurfaceTriPlot flat = unit_quad();
            flat.opts.name = "sheet";
            flat.opts.color = Color::Orange;
            const auto fe = collect_legend_entries(*with_mesh(flat).axes[0].snap3d());
            check(fe.size() == 1 && fe[0].kind == LegendKind::Bar && fe[0].name == "sheet" &&
                  fe[0].color.r == Color::Orange.r,
                  "surface_tri legend: a flat one is keyed by a swatch of its one colour, "
                  "which is the whole truth about it");

            Line3DPlot path;
            path.x = std::vector<double>{0.1, 0.9};
            path.y = std::vector<double>{0.1, 0.9};
            path.z = std::vector<double>{0.1, 0.9};
            path.colors = std::vector<double>{1.0, 2.0};
            path.opts.name = "path";
            FigureSnapshot both = with_mesh(m);
            both.axes[0].snap3d()->lines3d.push_back(std::move(path));
            const auto be = collect_legend_entries(*both.axes[0].snap3d());
            check(be.size() == 1 && be[0].name == "path" && be[0].swept,
                  "surface_tri legend: a colormapped *path* in the same axes is still keyed "
                  "-- every line's swatch is the same shape, so shape cannot tell two apart, "
                  "while a sheet's shape is already in the picture");

            SurfaceTriPlot quiet = unit_quad();
            quiet.opts.name = "sheet";
            quiet.opts.show_legend = false;
            check(collect_legend_entries(*with_mesh(quiet).axes[0].snap3d()).empty(),
                  "surface_tri legend: show_legend is the second gate, as it is for every "
                  "kind that can be keyed");
        }
    }

    // -------------------------------------------------------------------------
    // Hover at vertices, the Data-panel tab, and the edit lanes
    // -------------------------------------------------------------------------
    void test_surface_tri_hints_and_panel() {
        std::printf("\n[3D: a mesh under the pointer, and in the panels]\n");

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

        SurfaceTriPlot mesh;
        mesh.x = std::vector<double>{2.0, 8.0, 8.0, 2.0};
        mesh.y = std::vector<double>{2.0, 2.0, 8.0, 8.0};
        mesh.z = std::vector<double>{3.0, 4.0, 7.0, 5.0};
        mesh.tri = std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3};

        for (int mode = 0; mode < 2; ++mode) {
            cam.projection = mode ? Projection::Perspective : Projection::Orthographic;
            const char* what = mode ? "perspective" : "orthographic";
            const Projector3D proj(tf, cam, frame, 0.1f);

            RenderSnapshot3D snap;
            snap.surface_tri.push_back(mesh);

            // ---- Hover reports the nearest vertex (the hint_labels index).
            int named = 0;
            for (std::size_t i = 0; i < mesh.count(); ++i) {
                const Px3 q = proj.project(mesh.x[i], mesh.y[i], mesh.z[i]);
                const auto r = find_hint3d(snap, proj, q.x, q.y);
                if (!r) continue;
                char want[64];
                std::snprintf(want, sizeof(want), "x=%.4g, y=%.4g, z=%.4g",
                              mesh.x[i], mesh.y[i], mesh.z[i]);
                if (r->text == want) ++named;
            }
            check(named == 4,
                  std::string("surface_tri hint (") + what + "): the pointer on a vertex "
                  "names that vertex, by its three coordinates (named " +
                  std::to_string(named) + " of 4)");

            // ---- The middle of a face answers too (the ray hits the surface),
            // naming the nearest vertex.
            {
                std::size_t a = 0, b = 0, c = 0;
                mesh.face_verts(0, a, b, c);
                const double wx = 0.15 * mesh.x[a] + 0.70 * mesh.x[b] + 0.15 * mesh.x[c];
                const double wy = 0.15 * mesh.y[a] + 0.70 * mesh.y[b] + 0.15 * mesh.y[c];
                const double wz = 0.15 * mesh.z[a] + 0.70 * mesh.z[b] + 0.15 * mesh.z[c];
                const Px3 q = proj.project(wx, wy, wz);
                const auto r = find_hint3d(snap, proj, q.x, q.y);
                char want[64];
                std::snprintf(want, sizeof(want), "x=%.4g, y=%.4g, z=%.4g",
                              mesh.x[b], mesh.y[b], mesh.z[b]);
                check(r && r->text == want,
                      std::string("surface_tri hint (") + what + "): a point inside a face "
                      "names the nearest of its three vertices");
            }

            // ---- Off the mesh: nothing.
            {
                const Px3 q = proj.project(9.8, 9.8, 9.8);
                check(!find_hint3d(snap, proj, q.x, q.y).has_value(),
                      std::string("surface_tri hint (") + what + "): and a cursor off the "
                      "mesh answers nothing");
            }

            // ---- A colormapped mesh reports its value, with the hint_label below.
            {
                RenderSnapshot3D cs;
                SurfaceTriPlot cm = mesh;
                cm.colors = std::vector<double>{11.0, 22.0, 33.0, 44.0};
                cm.opts.hint_labels = {"a", "b", "c", "d"};
                cs.surface_tri.push_back(cm);
                const Px3 q = proj.project(cm.x[1], cm.y[1], cm.z[1]);
                const auto r = find_hint3d(cs, proj, q.x, q.y);
                check(r && r->text.find("c=22") != std::string::npos &&
                      r->text.find("\nb") != std::string::npos,
                      std::string("surface_tri hint (") + what + "): a colormapped mesh adds "
                      "the c value and the vertex's own label");
            }
        }

        // ---- The mesh shares one depth list with the other kinds.
        {
            const Projector3D proj(tf, cam, frame, 0.1f);
            RenderSnapshot3D snap;
            snap.surface_tri.push_back(mesh);
            Bar3DPlot bar;
            bar.orient = PlaneOrientation::XY;
            bar.u = std::vector<double>{5.0};
            bar.v = std::vector<double>{5.0};
            bar.heights = std::vector<double>{9.5};
            bar.u_width = bar.v_width = 3.0;
            snap.bars3d.push_back(bar);
            // At the bar's top centre, the bar is nearest.
            const Px3 q = proj.project(5.0, 5.0, 9.4);
            const auto r = find_hint3d(snap, proj, q.x, q.y);
            check(r && r->text.find("height=9.5") != std::string::npos,
                  "surface_tri hint: a mesh takes part in the one depth-sorted list every "
                  "kind shares, so whatever is nearest answers");
        }

        // ---- The Data panel's table, topology and tab.
        {
            RenderSnapshot3D snap;
            SurfaceTriPlot cm = mesh;
            cm.colors = std::vector<double>{11.0, 22.0, 33.0, 44.0};
            cm.opts.name = "scan";
            snap.surface_tri.push_back(cm);
            snap.surface_tri.push_back(mesh); // flat: three columns

            const auto tables = collect_plot_data_tables(snap);
            check(tables.size() == 2 && tables[0].kind == PlotKind::SurfaceTri &&
                  tables[0].label == "scan",
                  "surface_tri panel: a mesh gets a table of its own, named by its label");
            check(tables[0].columns.size() == 4 &&
                  tables[0].columns[3].name == std::string("c"),
                  "surface_tri panel: a colormapped mesh shows the fourth column");
            check(tables[1].columns.size() == 3,
                  "surface_tri panel: and a flat one shows three");
            check(tables[0].columns[0].count == 4,
                  "surface_tri panel: the rows are the mesh's *vertices*, not its faces, "
                  "which have no values of their own and would report three row numbers");
            check(tables[0].mesh != nullptr && tables[0].mesh->face_count() == 2,
                  "surface_tri panel: the topology rides along, so the panel can show the "
                  "face count a caller checks their own mesh by");
            check(tables[0].rows_fixed,
                  "surface_tri panel: and the rows are fixed -- a face names its vertices by "
                  "index, so adding or removing one would renumber the topology under it");
            check(!tables[0].is_grid(),
                  "surface_tri panel: a mesh is the vector shape, not the grid one -- it has "
                  "no u, no v and no cell addressed by a low corner");

            const auto tabs = data_panel_tabs(tables, 0);
            check(tabs.size() == 2 && tabs[0].table == 0 && tabs[1].table == 1,
                  "surface_tri panel: each mesh gets its own tab");
        }

        // ---- Edit lanes: appearance and a data op.
        {
            RenderSnapshot3D dst;
            dst.surface_tri.push_back(mesh);
            dst.surface_tri[0].opts.hint_labels = {"a", "b", "c", "d"};

            AxesEdit3D e;
            SurfaceTriOptions o = mesh.opts;
            o.color = Color::Green;
            o.edges = true;
            e.surface_tri.push_back({0, o});
            apply_axes3d_edit(dst, e);
            check(dst.surface_tri[0].opts.edges &&
                  dst.surface_tri[0].opts.color.g == Color::Green.g,
                  "surface_tri lane: an appearance edit reaches the mesh it names");
            check(dst.surface_tri[0].opts.hint_labels.size() == 4,
                  "surface_tri lane: and does not carry a stale copy of the hint labels back "
                  "over the ones the Data panel edits");

            AxesEdit3D d2;
            d2.plot_ops.push_back(PlotCellEdit{PlotKind::SurfaceTri, 0, 2, 1, 4.25, -1});
            apply_axes3d_edit(dst, d2);
            check(dst.surface_tri[0].z[1] == 4.25,
                  "surface_tri lane: a data op moves the vertex it names, in the column it "
                  "names");
            check(dst.surface_tri[0].tri.size() == 6 && dst.surface_tri[0].tri[0] == 0,
                  "surface_tri lane: and leaves the topology alone -- an edit moves a point, "
                  "it does not re-mesh, which is the consequence the Delaunay overloads take "
                  "deliberately");

            // Addressed by kind: a path and a mesh at index 0 are different objects.
            RenderSnapshot3D both;
            both.surface_tri.push_back(mesh);
            Line3DPlot path;
            path.x = mesh.x;
            path.y = mesh.y;
            path.z = mesh.z;
            both.lines3d.push_back(path);
            AxesEdit3D d3;
            d3.plot_ops.push_back(PlotCellEdit{PlotKind::SurfaceTri, 0, 0, 0, 9.5, -1});
            apply_axes3d_edit(both, d3);
            check(both.surface_tri[0].x[0] == 9.5 && both.lines3d[0].x[0] == 2.0,
                  "surface_tri lane: an op naming a mesh reaches the mesh and not the path "
                  "at the same index");
        }
    }

    // -------------------------------------------------------------------------
    // Delaunay, checked against properties
    // -------------------------------------------------------------------------
    void test_delaunay() {
        std::printf("\n[3D: the Delaunay the orient overloads derive]\n");

        using namespace sextant;

        // Three independent oracles:
        //   - the empty-circumcircle property over every (triangle, vertex) pair;
        //   - Euler: 2n - 2 - h triangles for n points with h on the hull (hull by
        //     an independent monotone chain);
        //   - the triangles' total area equals the hull's.
        auto hull = [](const std::vector<double>& u, const std::vector<double>& v) {
            std::vector<std::size_t> idx(u.size());
            for (std::size_t i = 0; i < u.size(); ++i) idx[i] = i;
            std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
                return u[a] != u[b] ? u[a] < u[b] : v[a] < v[b];
            });
            // Distinct points only.
            idx.erase(std::unique(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
                return u[a] == u[b] && v[a] == v[b];
            }), idx.end());
            auto cross = [&](std::size_t o, std::size_t a, std::size_t b) {
                return (u[a] - u[o]) * (v[b] - v[o]) - (v[a] - v[o]) * (u[b] - u[o]);
            };
            // Collinear boundary points are kept (`< 0`): Euler counts every point
            // on the hull, not just corners.
            std::vector<std::size_t> h;
            for (int pass = 0; pass < 2; ++pass) {
                const std::size_t start = h.size();
                for (std::size_t k = 0; k < idx.size(); ++k) {
                    const std::size_t i = pass == 0 ? idx[k] : idx[idx.size() - 1 - k];
                    while (h.size() >= start + 2 &&
                           cross(h[h.size() - 2], h[h.size() - 1], i) < 0.0)
                        h.pop_back();
                    h.push_back(i);
                }
                h.pop_back();
            }
            return h;
        };

        auto check_mesh = [&](const char* what,
                              const std::vector<double>& u, const std::vector<double>& v) {
            std::vector<std::uint32_t> tri;
            const bool ok = delaunay_triangulate(u, v, tri);
            if (!ok) {
                check(false, std::string("delaunay ") + what + ": triangulated at all");
                return;
            }
            const std::size_t m = tri.size() / 3;

            // 1. Empty circumcircle, strict with a tolerance (grids have cocircular
            //    points).
            int violations = 0;
            for (std::size_t f = 0; f < m; ++f) {
                const std::uint32_t ia = tri[f * 3], ib = tri[f * 3 + 1], ic = tri[f * 3 + 2];
                for (std::size_t p = 0; p < u.size(); ++p) {
                    if (p == ia || p == ib || p == ic) continue;
                    const double ax = u[ia] - u[p], ay = v[ia] - v[p];
                    const double bx = u[ib] - u[p], by = v[ib] - v[p];
                    const double cx = u[ic] - u[p], cy = v[ic] - v[p];
                    const double det = (ax * ax + ay * ay) * (bx * cy - cx * by)
                                       - (bx * bx + by * by) * (ax * cy - cx * ay)
                                       + (cx * cx + cy * cy) * (ax * by - bx * ay);
                    if (det > 1e-9) ++violations;
                }
            }

            // 2. Euler: 2n - 2 - h triangles, with n the distinct points.
            std::vector<std::size_t> pts(u.size());
            for (std::size_t i = 0; i < u.size(); ++i) pts[i] = i;
            std::sort(pts.begin(), pts.end(), [&](std::size_t a, std::size_t b) {
                return u[a] != u[b] ? u[a] < u[b] : v[a] < v[b];
            });
            pts.erase(std::unique(pts.begin(), pts.end(), [&](std::size_t a, std::size_t b) {
                return u[a] == u[b] && v[a] == v[b];
            }), pts.end());
            const std::vector<std::size_t> hv = hull(u, v);
            const std::size_t want = 2 * pts.size() - 2 - hv.size();

            // 3. The union is the hull: equal areas.
            double tri_area = 0.0;
            for (std::size_t f = 0; f < m; ++f) {
                const std::uint32_t a = tri[f * 3], b = tri[f * 3 + 1], c = tri[f * 3 + 2];
                tri_area += std::fabs((u[b] - u[a]) * (v[c] - v[a])
                                      - (v[b] - v[a]) * (u[c] - u[a])) * 0.5;
            }
            double hull_area = 0.0;
            for (std::size_t i = 0; i < hv.size(); ++i) {
                const std::size_t j = (i + 1) % hv.size();
                hull_area += u[hv[i]] * v[hv[j]] - u[hv[j]] * v[hv[i]];
            }
            hull_area = std::fabs(hull_area) * 0.5;

            std::printf("  %-14s %zu points -> %zu triangles (Euler: %zu), hull %zu, "
                        "%d circumcircle violations\n",
                        what, pts.size(), m, want, hv.size(), violations);
            check(violations == 0,
                  std::string("delaunay ") + what + ": no vertex lies inside any triangle's "
                  "circumcircle -- the property that *is* the definition");
            check(m == want,
                  std::string("delaunay ") + what + ": the triangle count is Euler's "
                  "2n - 2 - h, from a convex hull computed by unrelated code");
            check(std::fabs(tri_area - hull_area) < hull_area * 1e-9,
                  std::string("delaunay ") + what + ": and the triangles tile exactly the "
                  "convex hull, with no gap and no overlap");
        };

        // A regular grid (cocircular quads).
        {
            std::vector<double> u, v;
            for (int i = 0; i < 5; ++i)
                for (int j = 0; j < 5; ++j) {
                    u.push_back(i);
                    v.push_back(j);
                }
            check_mesh("regular grid", u, v);
        }
        // Scattered points, from a fixed sequence.
        {
            std::vector<double> u, v;
            unsigned s = 12345u;
            auto next = [&] {
                s = s * 1103515245u + 12345u;
                return ((s >> 16) & 0x7fff) / 32767.0;
            };
            for (int i = 0; i < 40; ++i) {
                u.push_back(next());
                v.push_back(next());
            }
            check_mesh("scattered", u, v);
        }

        // ---- Duplicates keep their slots (colors/hint_labels stay aligned).
        {
            std::vector<double> u{0.0, 1.0, 0.0, 1.0, 1.0};
            std::vector<double> v{0.0, 0.0, 1.0, 1.0, 1.0}; // 4 == 3
            std::vector<std::uint32_t> tri;
            check(delaunay_triangulate(u, v, tri),
                  "delaunay: a point set with a duplicate still triangulates");
            std::uint32_t hi = 0;
            for (const std::uint32_t i: tri) hi = std::max(hi, i);
            check(tri.size() == 6,
                  "delaunay: the duplicate adds no triangle -- it is deduplicated for the "
                  "triangulation only");
            check(hi < u.size(),
                  "delaunay: and every index still names one of the caller's own vertices, "
                  "so colors and hint_labels stay aligned with what they were written for");
        }

        // ---- Collinear input reports no triangulation.
        {
            std::vector<double> u{0.0, 1.0, 2.0, 3.0};
            std::vector<double> v{0.0, 1.0, 2.0, 3.0};
            std::vector<std::uint32_t> tri;
            check(!delaunay_triangulate(u, v, tri),
                  "delaunay: collinear points have no triangulation, and it is reported "
                  "rather than returned empty");
        }

        // ---- Through the public API: the `orient` overloads, their throws, and a
        // concave domain.
        {
            auto threw = [](auto&& fn) {
                try {
                    fn();
                    return false;
                } catch (const std::invalid_argument&) { return true; }
            };
            auto fig = Figure::create({.width = 300, .height = 240});
            auto ax = fig->add_subplot3d(1, 1, 1);

            std::vector<double> x, y, z, c;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) {
                    x.push_back(i);
                    y.push_back(j);
                    z.push_back(0.1 * i * j);
                    // Distinct per vertex, so no face falls back to a flat fill.
                    c.push_back(i * 4 + j);
                }
            check(!threw([&] { ax->surface_tri(x, y, z, PlaneOrientation::XY); }),
                  "surface_tri: the orient overload triangulates and is accepted");
            check(!threw([&] { ax->surface_tri(x, y, z, PlaneOrientation::XY, c); }),
                  "surface_tri: and so is the one with a fourth dimension");

            // The triangulation is stored as indices at ingest; counted via the
            // SVG's gradients (one per colormapped face): 18 for a 4x4 grid, as
            // Euler says.
            {
                auto f2 = Figure::create({.width = 420, .height = 340});
                auto a2 = f2->add_subplot3d(1, 1, 1);
                a2->surface_tri(x, y, z, PlaneOrientation::XY, c);
                f2->savefig("surface_tri_delaunay.svg");
                std::ifstream f("surface_tri_delaunay.svg");
                const std::string svg((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
                int grads = 0;
                std::size_t q = 0;
                while ((q = svg.find("<linearGradient id=\"meshGrad", q))
                       != std::string::npos) {
                    ++grads;
                    q += 10;
                }
                std::printf("  through the API: %d faces in the file\n", grads);
                check(grads == 18,
                      "surface_tri: the orient overload stores a real triangulation -- the "
                      "file carries one filled face per triangle, and 18 is Euler's count "
                      "for this grid");
            }

            // Different projection planes give different meshes (the orientation
            // only decides how the mesh is made).
            {
                std::vector<std::uint32_t> t_xy, t_yz;
                const bool a_ok = delaunay_triangulate(x, y, t_xy);
                const bool b_ok = delaunay_triangulate(y, z, t_yz);
                check(a_ok && b_ok && t_xy.size() != t_yz.size(),
                      "surface_tri: a different orientation triangulates a different "
                      "projection, which is what makes `orient` a parameter rather than a "
                      "hardcoded plane");
            }

            // Collinear in the given plane: throws.
            const std::vector<double> lx{0.0, 1.0, 2.0, 3.0};
            const std::vector<double> ly{0.0, 1.0, 2.0, 3.0};
            const std::vector<double> lz{5.0, 1.0, 9.0, 2.0};
            check(threw([&] { ax->surface_tri(lx, ly, lz, PlaneOrientation::XY); }),
                  "surface_tri: vertices collinear in the plane given throw, rather than "
                  "rendering an empty box");

            // A concave domain is filled to its convex hull (no masking yet),
            // measured by area.
            {
                // A U shape; its convex hull bridges the opening.
                std::vector<double> cx, cy, cz;
                for (int i = 0; i <= 4; ++i) {
                    cx.push_back(i);
                    cy.push_back(0);
                    cz.push_back(0.0);
                }
                for (int j = 1; j <= 4; ++j) {
                    cx.push_back(0);
                    cy.push_back(j);
                    cz.push_back(0.0);
                }
                for (int j = 1; j <= 4; ++j) {
                    cx.push_back(4);
                    cy.push_back(j);
                    cz.push_back(0.0);
                }
                auto f3 = Figure::create({.width = 300, .height = 240});
                auto a3 = f3->add_subplot3d(1, 1, 1);
                check(!threw([&] { a3->surface_tri(cx, cy, cz, PlaneOrientation::XY); }),
                      "surface_tri: a concave domain is accepted");
                std::vector<std::uint32_t> tri;
                delaunay_triangulate(cx, cy, tri);
                double area = 0.0;
                for (std::size_t f = 0; f * 3 + 2 < tri.size(); ++f) {
                    const std::uint32_t a = tri[f * 3], b = tri[f * 3 + 1],
                            cc = tri[f * 3 + 2];
                    area += std::fabs((cx[b] - cx[a]) * (cy[cc] - cy[a])
                                      - (cy[b] - cy[a]) * (cx[cc] - cx[a])) * 0.5;
                }
                check(std::fabs(area - 16.0) < 1e-9,
                      "surface_tri: a concave domain is spanned -- Delaunay fills the convex "
                      "hull, so the U comes back as the full square with its opening "
                      "bridged. A boundary the header states, not a bug; masking is "
                      "deferred");
            }
        }
    }
} // namespace lt
