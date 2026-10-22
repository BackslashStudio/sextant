// Axes3D::quiver3d(): the call, read-back, set_quiver3d_data(), the automatic
// amp, visibility, removal and cla(); which arrows are drawn and where (pivot,
// masking), their shape in the box (facing the eye, all head, edge-on),
// colours (flat, the map over the own or given range, the norms), limits; GL
// pixels, translucency and the SVG pieces; the colorbar and legend key; hover
// and picks; the Data panel's table, style and cell edits. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "renderer/quiver3d.h"
#include "colormaps.h"
#include "figure_edits.h"
#include "figure_impl.h"
#include "hint.h"
#include "plot_data_view.h"
#include "widgets/data_panel.h"

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
        constexpr int W = 420, H = 360;
        constexpr Color kPureRed{1.0f, 0.0f, 0.0f, 1.0f};

        template <class F>
        bool throws_invalid(F&& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        template <class F>
        bool throws_range(F&& f) {
            try {
                f();
            } catch (const std::out_of_range&) {
                return true;
            }
            return false;
        }

        bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

        Quiver3DPlot field(V x, V y, V z, V u, V v, V w, Color c = kPureRed) {
            Quiver3DPlot q;
            q.x = std::move(x);
            q.y = std::move(y);
            q.z = std::move(z);
            q.u = std::move(u);
            q.v = std::move(v);
            q.w = std::move(w);
            q.opts.color = c;
            q.opts.amp = 1.0;
            q.id = 7001;
            return q;
        }

        // A unit box looked at down +x (y across the screen, z up), no furniture.
        FigureSnapshot bare(Projection mode) {
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
            s->xmin = s->ymin = s->zmin = 0.0;
            s->xmax = s->ymax = s->zmax = 1.0;
            s->xlim_auto = s->ylim_auto = s->zlim_auto = false;
            return fs;
        }

        struct Image {
            int w = 0, h = 0;
            std::vector<unsigned char> px;
            const unsigned char* at(float x, float y) const {
                const int xi = static_cast<int>(std::lround(x)), yi = static_cast<int>(std::lround(y));
                if (xi < 0 || yi < 0 || xi >= w || yi >= h) return nullptr;
                return px.data() + (static_cast<std::size_t>(yi) * w + xi) * 4;
            }
        };

        Image render(const FigureSnapshot& fs, const std::string& stem) {
            {
                GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
                RenderDevice dev;
                DataRenderer data;
                export_figure_png(dev, data, fs, stem + ".png", W, H, 1);
            }
            export_figure_svg(fs, stem + ".svg", W, H);
            Image im;
            int comp = 0;
            unsigned char* p = stbi_load((stem + ".png").c_str(), &im.w, &im.h, &comp, 4);
            if (p) {
                im.px.assign(p, p + static_cast<std::size_t>(im.w) * im.h * 4);
                stbi_image_free(p);
            }
            return im;
        }

        bool red(const unsigned char* p) { return p && p[0] > 200 && p[1] < 60 && p[2] < 60; }

        std::string read_file(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        std::size_t count_of(const std::string& s, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t p = s.find(needle); p != std::string::npos; p = s.find(needle, p + 1)) ++n;
            return n;
        }
    } // namespace

    static void quiver3d_api_checks();
    static void quiver3d_geometry_checks();
    static void quiver3d_render_checks();
    static void quiver3d_decor_checks();

    void test_quiver3d() {
        std::printf("\n[3D quiver]\n");
        try {
            quiver3d_api_checks();
            quiver3d_geometry_checks();
            quiver3d_render_checks();
            quiver3d_decor_checks();
        } catch (const std::exception& e) {
            check(false, std::string("quiver3d: unexpected exception: ") + e.what());
        }
    }

    // --- The call, read-back, set, amp, cycle, visibility, removal, cla() ---
    static void quiver3d_api_checks() {
        auto fig = Figure::create();
        auto ax = fig->add_subplot3d(1, 1, 1);
        auto& fi = detail::FigureAccess::impl(*fig);
        const V x{0.0, 1.0, 2.0}, y{0.0, 0.5, 1.0}, z{1.0, 1.0, 2.0};
        const V u{1.0, 0.0, 0.0}, v{0.0, 1.0, 0.0}, w{0.0, 0.0, 1.0};
        ax->quiver3d(x, y, z, u, v, w, {.amp = 0.5});
        const Quiver3DData d = ax->quiver3d_data(0);
        check(ax->quiver3d_count() == 1 && d.x == x && d.y == y && d.z == z && d.u == u && d.v == v && d.w == w &&
                  d.c.empty(),
              "call: stores x, y, z, u, v and w, read back as given; flat has no c");
        const V c2{1.0, 2.0};
        check(throws_invalid([&] { ax->quiver3d(x, y, V{1.0}, u, v, w); }) &&
                  throws_invalid([&] { ax->quiver3d(x, y, z, u, v, V{1.0, 2.0}); }) &&
                  throws_invalid([&] { ax->quiver3d(x, y, z, u, v, w, c2); }) &&
                  throws_invalid([&] { ax->quiver3d(x, y, z, u, v, w, V{}); }) &&
                  throws_invalid([&] { ax->quiver3d(x, y, z, u, v, w, {.amp = 0.0}); }) &&
                  throws_invalid([&] { ax->quiver3d(x, y, z, u, v, w, {.amp = kNaN}); }) &&
                  throws_invalid([&] { ax->quiver3d(x, y, z, u, v, w, {.vmax = NAN}); }) &&
                  ax->quiver3d_count() == 1,
              "call: unequal lengths, a c not one per arrow, an amp not finite and > 0 or a NaN vmin/vmax throw, "
              "and nothing is added");
        const V cneg{-1.0, 0.0, -2.0}, cpos{1.0, 2.0, 4.0};
        check(throws_invalid([&] { ax->quiver3d(x, y, z, u, v, w, cneg, {.norm = ColorNorm::log()}); }) &&
                  throws_invalid([&] { ax->quiver3d(x, y, z, u, v, w, cpos, {.norm = ColorNorm::two_slope(9.0f)}); }) &&
                  throws_invalid([&] {
                      ax->quiver3d(x, y, z, u, v, w, cpos, {.vmin = -1.0f, .vmax = 4.0f, .norm = ColorNorm::log()});
                  }) &&
                  throws_invalid([&] { ax->quiver3d(x, y, z, u, v, w, cpos, {.norm = ColorNorm::boundaries({2.0, 1.0})}); }) &&
                  ax->quiver3d_count() == 1,
              "call: a norm that doesn't fit the resolved range throws (log over no c above 0, a centre outside "
              "the own range, log over a given vmin <= 0, unusable boundaries)");
        check(!throws_invalid([&] { ax->quiver3d(x, y, z, u, v, w, V{-1.0, 1.0, 4.0}, {.norm = ColorNorm::log()}); }) &&
                  !throws_invalid([&] { ax->quiver3d(V{kNaN, 1.0, 2.0}, y, z, u, v, w); }) &&
                  ax->quiver3d_count() == 3,
              "call: log over own c with some above 0 fits; a NaN number is taken (that arrow is not drawn)");

        // The amp: as given, or chosen at the call and kept.
        auto* a = fi.find_slot_impl3d(1);
        check(a && a->quivers3d[0].amp() == 0.5, "amp: as given");
        {
            V gx, gy, gz, gu, gv, gw;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) {
                    gx.push_back(j);
                    gy.push_back(i);
                    gz.push_back(0.0);
                    gu.push_back(0.48);
                    gv.push_back(0.64);
                    gw.push_back(0.6);
                }
            ax->quiver3d(gx, gy, gz, gu, gv, gw);
            // 9 vectors of length 1 over x in [0, 2]: 2 / (1.8 * 10).
            check(near(a->quivers3d[3].amp(), 2.0 / 18.0, 1e-12),
                  "amp: unset, the mean 3D arrow is the x extent / (1.8 * max(10, sqrt(N))) (" +
                      std::to_string(a->quivers3d[3].amp()) + ")");
            ax->quiver3d(V{1.0, 1.0}, V{2.0, 2.0}, V{0.0, 9.0}, V{0.0, 0.0}, V{0.0, 0.0}, V{3.0, 3.0});
            ax->quiver3d(V{1.0}, V{1.0}, V{1.0}, V{0.0}, V{0.0}, V{0.0});
            check(near(a->quivers3d[4].amp(), 9.0 / (1.8 * 10.0 * 3.0), 1e-12) && a->quivers3d[5].amp() == 1.0,
                  "amp: x and y extents 0 fall back to the z extent; no nonzero vector gives 1");
            ax->set_quiver3d_data(3, V{0.0, 100.0}, V{0.0, 0.0}, V{0.0, 0.0}, V{9.0, 9.0}, V{0.0, 0.0}, V{0.0, 0.0});
            check(near(a->quivers3d[3].amp(), 2.0 / 18.0, 1e-12) && ax->quiver3d_data(3).x.size() == 2,
                  "set_quiver3d_data(spans): replaces the arrows and keeps the amp");
        }
        // c: kept by the span form while the count holds, else dropped; QuiverData sets it.
        {
            ax->set_quiver3d_data(1, x, y, z, w, u, v);
            check(ax->quiver3d_data(1).c == V({-1.0, 1.0, 4.0}) && ax->quiver3d_data(1).u == w,
                  "set_quiver3d_data(spans): c stays while the count holds");
            ax->set_quiver3d_data(1, V{0.0}, V{0.0}, V{0.0}, V{1.0}, V{0.0}, V{0.0});
            check(ax->quiver3d_data(1).c.empty(), "set_quiver3d_data(spans): a new count drops c");
            ax->set_quiver3d_data(1, Quiver3DData{x, y, z, u, v, w, cpos});
            check(ax->quiver3d_data(1).c == cpos, "set_quiver3d_data(Quiver3DData): sets c");
            check(throws_invalid([&] { ax->set_quiver3d_data(1, Quiver3DData{x, y, z, u, v, w, V{1.0}}); }) &&
                      throws_invalid([&] { ax->set_quiver3d_data(1, x, V{1.0}, z, u, v, w); }) &&
                      ax->quiver3d_data(1).c == cpos,
                  "set_quiver3d_data: bad lengths throw and change nothing");
            check(throws_range([&] { (void)ax->quiver3d_data(99); }) &&
                      throws_range([&] { ax->set_quiver3d_data(99, x, y, z, u, v, w); }) &&
                      throws_range([&] { (void)ax->quiver3d_visible(99); }),
                  "read-back: an index past the count throws out_of_range");
        }
        // Visibility, find and remove by id.
        {
            ax->set_quiver3d_visible(1, false);
            check(!ax->quiver3d_visible(1) && ax->quiver3d_visible(0), "visible: set and read per field");
            const ObjectId id = a->quivers3d[1].id;
            const auto loc = ax->find_object(id);
            check(loc && loc->kind == PickKind::Quiver3D && loc->index == 1, "objects: found by id as PickKind::Quiver3D");
            const std::size_t n = ax->quiver3d_count();
            check(ax->remove(id) && ax->quiver3d_count() == n - 1 && !ax->find_object(id),
                  "objects: removed by id");
            ax->quiver3d(x, y, z, u, v, w);
            check(ax->last_object() == a->quivers3d.back().id, "objects: last_object() is the latest quiver3d");
        }
        // The cycle: flat takes the next colour, colormapped keeps one without using it up.
        {
            auto f2 = Figure::create();
            auto a2 = f2->add_subplot3d(1, 1, 1);
            a2->quiver3d(x, y, z, u, v, w, cpos).quiver3d(x, y, z, u, v, w).line3d(x, y, z);
            const FigureSnapshot s = detail::FigureAccess::impl(*f2).build_figure_snapshot();
            const RenderSnapshot3D& r = *s.axes[0].snap3d();
            const Color q = *r.quivers3d[1].opts.color;
            const Color l = *r.lines3d[0].opts.color;
            check(q.r == Color::Blue.r && q.g == Color::Blue.g && q.b == Color::Blue.b && !(l.r == q.r && l.g == q.g),
                  "color: a flat field takes the next cycle colour; the colormapped one before it took none");
        }
        ax->cla();
        check(ax->quiver3d_count() == 0, "cla(): clears 3D quivers");
    }

    // --- Arrows: which, where, shape, colour, limits ---
    static void quiver3d_geometry_checks() {
        // The outline in the box, by hand: along +x facing +z, k = 0.01 box per px.
        {
            Quiver3DOptions o; // width 2, head 7 x 8
            Vec3 out[7];
            const int n = quiver3d_outline({0, 0, 0}, {1, 0, 0}, {0, 0, 1}, 0.01, o, out);
            const bool shape = n == 7 && near(out[3].x, 1.0, 1e-12) && near(out[1].x, 0.92, 1e-12) &&
                               near(out[2].x, 0.92, 1e-12) && near(length(out[0] - out[6]), 0.02, 1e-12) &&
                               near(length(out[2] - out[4]), 0.07, 1e-12) && near(out[0].x, 0.0, 1e-12);
            bool flat = true;
            for (const Vec3& p: out) flat = flat && p.z == 0.0;
            check(shape && flat, "outline: 7 points, shaft `width` and head `head_width` x `head_length` pixels in "
                                 "box units, flat across the facing direction");
            const int m = quiver3d_outline({0, 0, 0}, {0.04, 0, 0}, {0, 0, 1}, 0.01, o, out);
            check(m == 3 && near(out[1].x, 0.04, 1e-12) && near(out[0].x, 0.0, 1e-12) &&
                      near(length(out[0] - out[2]), 0.035, 1e-12),
                  "outline: shorter than its head, all head, scaled to its length");
            check(quiver3d_outline({0, 0, 0}, {0, 0, 1}, {0, 0, 1}, 0.01, o, out) == 0 &&
                      quiver3d_outline({0, 0, 0}, {0, 0, 0}, {0, 0, 1}, 0.01, o, out) == 0,
                  "outline: an arrow along the facing direction, or of no length, has none");
        }

        // Which arrows, and the pivot.
        {
            Quiver3DPlot q = field(V{0, 1, kNaN, 3, 4}, V{0, 0, 0, 0, 0}, V{0, 0, 0, 0, 0}, V{2, 2, 2, 0, 2},
                                   V{0, 0, 0, 0, 0}, V{0, 0, 0, 0, kNaN});
            std::vector<std::size_t> seen;
            for_each_quiver3d_arrow(q, [&](std::size_t i, Vec3, Vec3) { seen.push_back(i); });
            check(seen == std::vector<std::size_t>({0, 1}), "arrows: a NaN anchor or vector and a zero vector are left out");
            q = field(V{1}, V{1}, V{1}, V{1}, V{2}, V{2});
            q.opts.amp = 2.0;
            Vec3 t{}, h{};
            auto ends = [&] { for_each_quiver3d_arrow(q, [&](std::size_t, Vec3 a, Vec3 b) { t = a; h = b; }); };
            ends();
            const bool tail = t.x == 1 && t.y == 1 && t.z == 1 && h.x == 3 && h.y == 5 && h.z == 5;
            q.opts.pivot = QuiverPivot::Middle;
            ends();
            const bool mid = t.x == 0 && t.y == -1 && h.x == 2 && h.z == 3;
            q.opts.pivot = QuiverPivot::Tip;
            ends();
            const bool tip = h.x == 1 && h.y == 1 && h.z == 1 && t.x == -1 && t.z == -3;
            check(tail && mid && tip, "arrows: amp * (u, v, w) from the anchor at the tail, centred on it, or ending at it");

            Quiver3DPlot m = field(V{0, 1, 2, 3}, V{0, 0, 0, 0}, V{0, 0, 0, 0}, V{1, 1, 1, 1}, V{0, 0, 0, 0},
                                   V{0, 0, 0, 0});
            m.c = V{kNaN, -1.0, 2.0, 8.0};
            m.opts.norm = ColorNorm::log();
            seen.clear();
            for_each_quiver3d_arrow(m, [&](std::size_t i, Vec3, Vec3) { seen.push_back(i); });
            const bool log_ok = seen == std::vector<std::size_t>({2, 3});
            m.opts.norm = ColorNorm::boundaries({0.0, 4.0, 6.0});
            seen.clear();
            for_each_quiver3d_arrow(m, [&](std::size_t i, Vec3, Vec3) { seen.push_back(i); });
            check(log_ok && seen == std::vector<std::size_t>({2}),
                  "arrows: a NaN c, a c <= 0 under log, a c outside the boundaries are left out");
        }

        // Colours.
        {
            Quiver3DPlot m = field(V{0, 1, 2}, V{0, 0, 0}, V{0, 0, 0}, V{1, 1, 1}, V{0, 0, 0}, V{0, 0, 0});
            m.c = V{10.0, 15.0, 20.0};
            const uint8_t* lut = colormaps::get(Colormap::Viridis);
            auto is = [&](Color c, double t) {
                const uint8_t* e = lut + colormaps::index(t, false) * 4;
                return near(c.r, e[0] / 255.0, 1e-6) && near(c.g, e[1] / 255.0, 1e-6) && near(c.b, e[2] / 255.0, 1e-6);
            };
            Quiver3DColoring k = quiver3d_coloring(m);
            check(k.vmin == 10.0 && k.vmax == 20.0 && is(quiver3d_arrow_color(m, k, 0), 0.0) &&
                      is(quiver3d_arrow_color(m, k, 1), 0.5) && is(quiver3d_arrow_color(m, k, 2), 1.0),
                  "colour: vmin == vmax maps c over its own range");
            m.opts.vmin = 0.0f;
            m.opts.vmax = 40.0f;
            k = quiver3d_coloring(m);
            check(is(quiver3d_arrow_color(m, k, 2), 0.5), "colour: a given vmin/vmax");
            m.opts.vmin = m.opts.vmax = 0.0f;
            m.c = V{-5.0, 1.0, 100.0};
            m.opts.norm = ColorNorm::log();
            k = quiver3d_coloring(m);
            check(k.vmin == 1.0 && k.vmax == 100.0 && k.kind == ColorNorm::Kind::Log &&
                      is(quiver3d_arrow_color(m, k, 2), 1.0) && is(quiver3d_arrow_color(m, k, 1), 0.0),
                  "colour: under log the own range starts at the smallest c above 0");
            m.c = V{0.5, 1.5, 2.5};
            m.opts.norm = ColorNorm::boundaries({0.0, 1.0, 2.0, 3.0});
            k = quiver3d_coloring(m);
            check(is(quiver3d_arrow_color(m, k, 0), 0.0) && is(quiver3d_arrow_color(m, k, 1), 0.5) &&
                      is(quiver3d_arrow_color(m, k, 2), 1.0),
                  "colour: under boundaries each band's colour");
            Quiver3DPlot f = field(V{0}, V{0}, V{0}, V{1}, V{0}, V{0}, Color{0.2f, 0.4f, 0.6f, 0.5f});
            f.opts.alpha = 0.5f;
            const Color fc = quiver3d_color(f, quiver3d_coloring(f), 0);
            check(fc.r == 0.2f && near(fc.a, 0.25, 1e-6) && quiver3d_translucent(f),
                  "colour: flat, faded by alpha; translucent below 1");
            f.opts.color = kPureRed;
            f.opts.alpha = 1.0f;
            check(!quiver3d_translucent(f) && !quiver3d_translucent(m), "colour: opaque at full alpha (a map's entries are opaque)");
        }

        // Limits: both ends of every arrow drawn; a hidden field adds nothing.
        {
            Quiver3DPlot q = field(V{0, 1}, V{0, 0}, V{0, 0}, V{2, 0}, V{0, -3}, V{0, 4});
            const DataBounds3D b = auto_scale3d({}, {}, {}, {}, {}, {}, 0.0, {}, {q});
            check(b.xmin == 0.0 && b.xmax == 2.0 && b.ymin == -3.0 && b.ymax == 0.0 && b.zmin == 0.0 && b.zmax == 4.0,
                  "limits: both ends of every arrow");
            auto fig = Figure::create();
            auto ax = fig->add_subplot3d(1, 1, 1);
            ax->quiver3d(V{0, 1}, V{0, 0}, V{0, 0}, V{20, 0}, V{0, 0}, V{0, 1}, {.amp = 1.0});
            const bool shown = ax->xlim().hi > 20.0;
            ax->set_quiver3d_visible(0, false);
            check(shown && ax->xlim().hi < 20.0, "limits: the axes' automatic limits take the tips; a hidden field none");
        }

        // Facing the eye under perspective: the arrow's plane holds the eye
        // direction at its midpoint.
        {
            FigureSnapshot fs = bare(Projection::Perspective);
            RenderSnapshot3D& s = *fs.axes[0].snap3d();
            s.camera.azimuth = 30.0;
            s.camera.elevation = 25.0;
            s.quivers3d.push_back(field(V{0.2}, V{0.3}, V{0.4}, V{0.5}, V{0.2}, V{0.3}));
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            std::vector<Quiver3DArrow> arrows;
            quiver3d_arrows(proj, s.quivers3d[0], 0, arrows);
            bool facing = arrows.size() == 1 && arrows[0].points == 7;
            if (facing) {
                const Quiver3DArrow& a = arrows[0];
                const Vec3 mid = (a.p[0] + a.p[6] + a.p[3] * 2.0) * 0.25;
                const Vec3 to_eye = normalize(proj.eye_point() - (a.p[0] + a.p[6] + a.p[3] * 2.0) * 0.25);
                const Vec3 across = normalize(a.p[0] - a.p[6]);
                facing = std::abs(dot(across, to_eye)) < 1e-6 && length(mid) >= 0.0;
            }
            check(facing, "shape: under perspective the arrow lies across the direction to the eye at its midpoint");
        }
    }

    // --- GL pixels, translucency, SVG pieces ---
    static void quiver3d_render_checks() {
        // One arrow across the screen (down +x, so along y is horizontal):
        // shaft 6 px, head 20 x 24 px, orthographic so pixels are exact.
        FigureSnapshot fs = bare(Projection::Orthographic);
        RenderSnapshot3D& s = *fs.axes[0].snap3d();
        Quiver3DPlot q = field(V{0.5}, V{0.2}, V{0.5}, V{0.0}, V{0.6}, V{0.0});
        q.opts.width = 6.0f;
        q.opts.head_width = 20.0f;
        q.opts.head_length = 24.0f;
        s.quivers3d.push_back(q);
        const Image im = render(fs, "quiver3d");
        const FigureLayout lay = compute_figure_layout(fs, W, H);
        const Projector3D& proj = lay.cells[0].box3d->proj;
        const Px3 tail = proj.project(0.5, 0.2, 0.5), tip = proj.project(0.5, 0.8, 0.5);
        const float len = std::hypot(tip.x - tail.x, tip.y - tail.y);
        const float ux = (tip.x - tail.x) / len, uy = (tip.y - tail.y) / len, nx = -uy, ny = ux;
        auto px = [&](float along, float across) {
            return im.at(tail.x + ux * along + nx * across, tail.y + uy * along + ny * across);
        };
        const float base = len - 24.0f;
        check(!im.px.empty() && red(px(base * 0.5f, 0.0f)) && !red(px(base * 0.5f, 6.0f)) && !red(px(base * 0.5f, -6.0f)),
              "gl: the shaft is drawn, `width` pixels across");
        check(red(px(base + 4.0f, 6.0f)) && red(px(base + 4.0f, -6.0f)) && red(px(len - 3.0f, 0.0f)) &&
                  !red(px(len + 4.0f, 0.0f)) && !red(px(-4.0f, 0.0f)),
              "gl: the head is wider than the shaft and ends at the tip");

        // SVG: two pieces, the shaft reaching half a pixel into the head.
        const std::vector<Quiver3DPolygon> plan = plan_quivers3d(proj, s.quivers3d);
        const bool pieces = plan.size() == 2 && !plan[0].head && plan[1].head && plan[0].xy.size() == 8 &&
                            plan[1].xy.size() == 6;
        float tip_err = 99.0f, reach = 0.0f;
        if (pieces) {
            tip_err = std::hypot(plan[1].xy[2] - tip.x, plan[1].xy[3] - tip.y);
            reach = (plan[0].xy[2] - tail.x) * ux + (plan[0].xy[3] - tail.y) * uy;
        }
        std::printf("  svg: tip off by %.2e px, shaft reaches %.3f px (base %.3f)\n", tip_err, reach, base);
        check(pieces && tip_err < 1e-3f && near(reach, base + 0.5f, 1e-3),
              "svg: a shaft and a head, the head's point at the tip, an opaque shaft half a pixel into the head");
        const std::string svg = read_file("quiver3d.svg");
        check(count_of(svg, "<polygon points=") == 2 && count_of(svg, "fill=\"rgb(255,0,0)\"") == 2,
              "svg: the two pieces written as filled polygons in the arrow's colour");

        // Translucent: blended once (the pieces don't overlap).
        {
            FigureSnapshot ft = fs;
            RenderSnapshot3D& st = *ft.axes[0].snap3d();
            st.quivers3d[0].opts.alpha = 0.5f;
            const Image it = render(ft, "quiver3d_alpha");
            const unsigned char* bg = it.at(tail.x + ux * base * 0.5f + nx * 12.0f, tail.y + uy * base * 0.5f + ny * 12.0f);
            const unsigned char* sh = it.at(tail.x + ux * base * 0.5f, tail.y + uy * base * 0.5f);
            const unsigned char* hd = it.at(tail.x + ux * (base + 6.0f), tail.y + uy * (base + 6.0f));
            bool blend = bg && sh && hd;
            for (int c = 0; blend && c < 3; ++c) {
                const double want = 0.5 * (c == 0 ? 255.0 : 0.0) + 0.5 * bg[c];
                blend = std::abs(sh[c] - want) <= 4.0 && std::abs(hd[c] - want) <= 4.0;
            }
            check(blend, "gl: a translucent arrow is blended once over the background, shaft and head alike");
            const std::vector<Quiver3DPolygon> tp = plan_quivers3d(proj, st.quivers3d);
            const float r2 = tp.size() == 2 ? (tp[0].xy[2] - tail.x) * ux + (tp[0].xy[3] - tail.y) * uy : 0.0f;
            check(near(r2, base, 1e-3), "svg: a translucent shaft stops at the head (no double blend)");
        }

        // A style edit on the render thread leaves the data generation alone;
        // the cached arrows still follow it (one renderer, two frames).
        {
            FigureSnapshot fa = fs;
            fa.generation = fa.data_generation = 5;
            FigureSnapshot fb = fa;
            fb.axes[0].snap3d()->quivers3d[0].opts.color = Color{0.0f, 0.0f, 1.0f, 1.0f};
            fb.axes[0].snap3d()->quivers3d[0].opts.width = 12.0f;
            {
                GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
                RenderDevice dev;
                DataRenderer data;
                export_figure_png(dev, data, fa, "quiver3d_cache_a.png", W, H, 1);
                export_figure_png(dev, data, fb, "quiver3d_cache_b.png", W, H, 1);
            }
            int w = 0, h = 0, comp = 0;
            unsigned char* p = stbi_load("quiver3d_cache_b.png", &w, &h, &comp, 4);
            bool follows = false;
            if (p) {
                auto at = [&](float along, float across) {
                    const int xi = static_cast<int>(std::lround(tail.x + ux * along + nx * across));
                    const int yi = static_cast<int>(std::lround(tail.y + uy * along + ny * across));
                    return p + (static_cast<std::size_t>(yi) * w + xi) * 4;
                };
                const unsigned char* mid = at(base * 0.5f, 0.0f);
                const unsigned char* edge = at(base * 0.5f, 5.0f);
                follows = mid[2] > 200 && mid[0] < 60 && edge[2] > 200 && edge[0] < 60;
                stbi_image_free(p);
            }
            check(follows, "gl: a style edit (colour, width) at the same data generation redraws the cached arrows");
        }

        // Depth shade: the far arrow darker; hidden fields draw nothing.
        {
            FigureSnapshot fd = bare(Projection::Orthographic);
            RenderSnapshot3D& sd = *fd.axes[0].snap3d();
            Quiver3DPlot two = field(V{0.9, 0.1}, V{0.2, 0.2}, V{0.7, 0.3}, V{0, 0}, V{0.6, 0.6}, V{0, 0});
            two.opts.depthshade = 1.0f;
            sd.quivers3d.push_back(two);
            const FigureLayout ld = compute_figure_layout(fd, W, H);
            const std::vector<Quiver3DPolygon> pd = plan_quivers3d(ld.cells[0].box3d->proj, sd.quivers3d);
            bool shaded = pd.size() == 4;
            if (shaded) {
                // Far to near: the last pieces are the near arrow's (x = 0.9, toward the eye).
                shaded = pd.front().arrow == 1 && pd.back().arrow == 0 && pd.front().color.r < pd.back().color.r;
            }
            check(shaded, "depth shade: pieces far to near, the far arrow darker");
            sd.quivers3d[0].opts.visible = false;
            check(plan_quivers3d(ld.cells[0].box3d->proj, sd.quivers3d).empty(), "visible: a hidden field has no pieces");
        }
    }

    // --- Colorbar, legend, hover, picks, panel, edits ---
    static void quiver3d_decor_checks() {
        FigureSnapshot fs = bare(Projection::Orthographic);
        RenderSnapshot3D& s = *fs.axes[0].snap3d();
        Quiver3DPlot q = field(V{0.5, 0.5}, V{0.2, 0.5}, V{0.3, 0.7}, V{0, 0}, V{0.2, 0.2}, V{0, 0});
        q.c = V{2.0, 6.0};
        q.opts.colorbar = true;
        q.opts.name = "flow";
        q.opts.hint_labels = {"first", "second"};
        s.quivers3d.push_back(q);

        // Colorbar and legend key.
        {
            const auto reqs = find_colorbar_requests(s);
            check(reqs.size() == 1 && reqs[0].vmin == 2.0f && reqs[0].vmax == 6.0f && reqs[0].name == "flow" &&
                      reqs[0].bounds.empty(),
                  "colorbar: a colormapped field's, over the resolved range");
            RenderSnapshot3D b = s;
            b.quivers3d[0].opts.norm = ColorNorm::boundaries({0.0, 3.0, 9.0});
            const auto br = find_colorbar_requests(b);
            check(br.size() == 1 && br[0].bounds.size() == 3 && br[0].vmin == 0.0f && br[0].vmax == 9.0f,
                  "colorbar: stepped under a boundary norm");
            RenderSnapshot3D f = s;
            f.quivers3d[0].c = V{};
            check(find_colorbar_requests(f).empty(), "colorbar: none for a flat field");
            const auto keys = collect_legend_entries(s);
            const auto fkeys = collect_legend_entries(f);
            check(keys.size() == 1 && keys[0].kind == LegendKind::Arrow && keys[0].color.r == 1.0f &&
                      keys[0].color.g == 1.0f && fkeys.size() == 1 && fkeys[0].kind == LegendKind::Arrow &&
                      fkeys[0].color.r == 1.0f && fkeys[0].color.g == 0.0f,
                  "legend: an arrow key, white for a colormapped field, its colour for a flat one");
        }

        // Hover and picks at the anchor.
        {
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            const Px3 a1 = proj.project(0.5, 0.5, 0.7);
            const auto hit = find_pick3d(s, proj, a1.x, a1.y, nullptr, 5.0f);
            const auto hint = find_hint3d(s, proj, a1.x, a1.y, nullptr, 5.0f);
            check(hit && hit->kind == PickKind::Quiver3D && hit->object == 0 && hit->element == 1 &&
                      pick_object_id(s, hit->kind, hit->object) == 7001,
                  "pick: the arrow at the cursor's anchor, by kind, element and id");
            check(hint && hint->text.find("z=0.7") != std::string::npos && hint->text.find("w=0") != std::string::npos &&
                      hint->text.find("c=6") != std::string::npos && hint->text.find("second") != std::string::npos,
                  "hover: x, y, z, then u, v, w and c, then the hint label (" + (hint ? hint->text : std::string()) + ")");
            const Px3 mid = proj.project(0.5, 0.35, 0.5);
            check(!find_pick3d(s, proj, mid.x, mid.y, nullptr, 5.0f), "pick: nothing away from an anchor");
            RenderSnapshot3D hidden = s;
            hidden.quivers3d[0].opts.visible = false;
            check(!find_pick3d(hidden, proj, a1.x, a1.y, nullptr, 5.0f), "pick: a hidden field is not hit");
        }

        // The Data panel: a table of x, y, z, u, v, w (and c), a tab without id conflicts.
        {
            const auto tables = collect_plot_data_tables(s);
            RenderSnapshot3D f = s;
            f.quivers3d[0].c = V{};
            const auto ftables = collect_plot_data_tables(f);
            check(tables.size() == 1 && tables[0].kind == PlotKind::Quiver3D && tables[0].label == "flow" &&
                      tables[0].columns.size() == 7 && tables[0].columns[5].name == std::string("w") &&
                      tables[0].columns[6].name == std::string("c") && ftables[0].columns.size() == 6,
                  "panel: a table per field, x y z u v w and c when colormapped");
            FigureSnapshot ps = fs;
            ps.generation = ps.data_generation = 1;
            const IdConflictScan sc = scan_panel_for_id_conflicts(ps, &draw_data_window, "Data", {});
            check(sc.conflicts == 0 && sc.probes > 0, "panel: a 3D quiver's tab draws without id conflicts");
        }

        // Edits: a style edit keeps the hint labels; a cell edit moves the number it names.
        {
            RenderSnapshot3D dst = s;
            AxesEdit3D e;
            Quiver3DOptions o = q.opts;
            o.width = 9.0f;
            o.pivot = QuiverPivot::Tip;
            o.hint_labels.clear();
            e.quivers3d.push_back({0, o});
            apply_axes3d_edit(dst, e);
            check(dst.quivers3d[0].opts.width == 9.0f && dst.quivers3d[0].opts.pivot == QuiverPivot::Tip &&
                      dst.quivers3d[0].opts.hint_labels.size() == 2,
                  "edit: a style edit reaches the field and keeps its hint labels");
            AxesEdit3D d2;
            d2.plot_ops.push_back(PlotCellEdit{PlotKind::Quiver3D, 0, 5, 1, 4.25, -1});
            d2.plot_ops.push_back(PlotCellEdit{PlotKind::Quiver3D, 0, 6, 0, -3.0, -1});
            apply_axes3d_edit(dst, d2);
            check(dst.quivers3d[0].w[1] == 4.25 && dst.quivers3d[0].c[0] == -3.0 && dst.quivers3d[0].w[0] == 0.0,
                  "edit: a cell edit moves the w or c it names");
        }
    }
} // namespace lt
