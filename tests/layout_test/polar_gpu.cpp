// Polar axes on the GPU: the polar shaders against the frame's own geometry
// (PolarMap), kind by kind -- fills, polygons, bars, lines, markers -- over a
// full disc, a sector, rmin > 0, data below rmin, a 1e6 band and a 20π spiral;
// a turn, zoom or resize uploads nothing; dashes exact after a drag. Part of
// sextant_layout_test.
#include "layout_test.h"
#include "figure_impl.h"
#include "figure_export.h"
#include "renderer/polar_renderer.h"

#include <cmath>
#include <numbers>

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;
        constexpr double pi = std::numbers::pi;
        constexpr int W = 400, H = 400;

        FigureSnapshot snapshot(Figure& fig) { return detail::FigureAccess::impl(fig).build_figure_snapshot(); }

        // The rendered figure and its polar frame.
        struct Shot {
            RgbaImage img;
            PolarPlan plan;

            const std::uint8_t* at(int x, int y) const {
                return &img.pixels[(static_cast<std::size_t>(y) * img.width + x) * 4];
            }
            bool red(int x, int y) const {
                if (x < 0 || y < 0 || x >= img.width || y >= img.height) return false;
                const std::uint8_t* p = at(x, y);
                return p[0] > 180 && p[1] < 120 && p[2] < 120;
            }
            bool red(double x, double y) const { return red(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))); }
        };

        Shot shoot(RenderDevice& dev, DataRenderer& data, Figure& fig) {
            const FigureSnapshot fs = snapshot(fig);
            Shot s;
            s.img = render_figure_rgba(dev, data, fs, W, H);
            s.plan = *compute_figure_layout(fs, W, H).cells[0].polar;
            return s;
        }

        // Pixels a sweep of the frame found right/wrong; `skip` ones sit within
        // the margin of an edge, where a pixel may fall either way.
        struct Tally {
            int n = 0, bad = 0, skip = 0;
            bool ok() const { return n > 200 && bad * 200 <= n; }
        };

        double wrap_pi(double d) { return std::remainder(d, 2.0 * pi); }

        // Compares every third pixel inside the frame against `expect(theta, r)`
        // (the region that should be red), skipping pixels whose neighbours
        // 1.5 px away disagree and those near the rim.
        template <class F>
        Tally sweep(const Shot& s, F&& expect) {
            const PolarPlan& pl = s.plan;
            const auto want = [&](double x, double y) -> int {   // 1, 0, or -1 outside the frame
                const double ux = (x - pl.cx) / pl.radius, uy = (pl.cy - y) / pl.radius;
                if (!pl.map.inside(ux, uy)) return -1;
                double t, r;
                pl.map.from_unit(ux, uy, t, r);
                return expect(t, r) ? 1 : 0;
            };
            Tally tl;
            for (int y = 0; y < H; y += 3)
                for (int x = 0; x < W; x += 3) {
                    const double px = x + 0.5, py = y + 0.5;
                    const double ux = (px - pl.cx) / pl.radius, uy = (pl.cy - py) / pl.radius;
                    if (std::hypot(ux, uy) * pl.radius > pl.radius - 3.0) continue;   // the outline
                    const int w = want(px, py);
                    if (w < 0) continue;
                    bool edge = false;
                    for (const double d: {-1.5, 1.5}) {
                        if (want(px + d, py) != w || want(px, py + d) != w || want(px + d, py + d) != w) edge = true;
                    }
                    if (edge) {
                        ++tl.skip;
                        continue;
                    }
                    ++tl.n;
                    if (s.red(x + 1, y + 1) != (w == 1)) ++tl.bad;
                }
            return tl;
        }

        struct Scene {
            const char* name;
            double rmin, rmax;
            double tmin, tmax;   // degrees
            double offset;       // degrees
            double dir;
            double r0, r1;       // the region's r
            double a, b;         // the region's θ, radians
        };

        void apply(PolarAxes& pa, const Scene& sc) {
            pa.grid(false);
            pa.set_rlim(sc.rmin, sc.rmax);
            pa.set_thetalim(sc.tmin, sc.tmax);
            pa.set_theta_offset(sc.offset);
            pa.set_theta_direction(static_cast<int>(sc.dir));
        }

        bool in_region(const Scene& sc, double t, double r) {
            return r >= sc.r0 && r <= sc.r1 && std::fabs(wrap_pi(t - 0.5 * (sc.a + sc.b))) <= 0.5 * (sc.b - sc.a);
        }

        void areas(RenderDevice& dev, DataRenderer& data) {
            const Scene scenes[] = {
                {"full disc", 0.0, 1.0, 0.0, 360.0, 0.0, 1, 0.3, 0.8, 0.4, 2.2},
                {"sector", 0.0, 1.0, 0.0, 180.0, 0.0, 1, 0.2, 0.7, 0.5, 2.0},
                {"sector, clockwise from north", 0.0, 1.0, -90.0, 90.0, 90.0, -1, 0.1, 0.9, -1.0, 1.0},
                {"rmin > 0", 0.4, 1.0, 0.0, 360.0, 0.0, 1, 0.6, 0.9, 1.0, 3.0},
                {"data below rmin", 0.4, 1.0, 0.0, 360.0, 0.0, 1, 0.0, 0.7, 1.0, 3.0},
                {"data past rmax", 0.0, 1.0, 0.0, 360.0, 30.0, 1, 0.5, 1.6, 4.0, 5.5},
                {"a 1e6 band", 1e6, 1e6 + 1.0, 0.0, 360.0, 0.0, 1, 1e6 + 0.25, 1e6 + 0.75, 0.2, 2.5},
            };
            const auto lin = [](double a, double b, int n) {
                V v;
                for (int i = 0; i < n; ++i) v.push_back(a + (b - a) * i / (n - 1));
                return v;
            };
            for (const Scene& sc: scenes) {
                for (const char* kind: {"fill", "polygon", "bar"}) {
                    auto fig = Figure::create();
                    fig->resize(W, H);
                    auto pa = fig->add_subplot_polar(1, 1, 1);
                    apply(*pa, sc);
                    const std::string k = kind;
                    Scene eff = sc;
                    if (k == "fill") {
                        const V th = lin(sc.a, sc.b, 9);
                        pa->fill(th, V(9, sc.r0), V(9, sc.r1), {.color = Color::Red});
                    } else if (k == "polygon") {
                        pa->shape_poly(V{sc.a, sc.b, sc.b, sc.a}, V{sc.r0, sc.r0, sc.r1, sc.r1}, {.color = Color::Red});
                    } else {
                        // Eight touching bars (width 1 = the full θ spacing) cover [a, b].
                        const double d = (sc.b - sc.a) / 8.0;
                        V c;
                        for (int i = 0; i < 8; ++i) c.push_back(sc.a + (i + 0.5) * d);
                        pa->bar(c, V(8, sc.r1 - sc.r0),
                                {.color = Color::Red, .bottom = V{sc.r0}, .width = 1.0f, .linewidth = 0.0f});
                    }
                    const Shot s = shoot(dev, data, *fig);
                    const Tally t = sweep(s, [&](double th, double r) { return in_region(eff, th, r); });
                    std::printf("  %s %s: %d samples, %d wrong, %d at edges\n", sc.name, kind, t.n, t.bad, t.skip);
                    check(t.ok(), std::string("gl: ") + kind + " matches the frame's geometry (" + sc.name + ")");
                }
            }
        }

        void strokes_and_markers(RenderDevice& dev, DataRenderer& data) {
            const auto lin = [](double a, double b, int n) {
                V v;
                for (int i = 0; i < n; ++i) v.push_back(a + (b - a) * i / (n - 1));
                return v;
            };
            // A line along an arc: red within its half width, not beyond it.
            for (const bool sector: {false, true}) {
                auto fig = Figure::create();
                fig->resize(W, H);
                auto pa = fig->add_subplot_polar(1, 1, 1);
                pa->grid(false);
                pa->set_rlim(0.0, 1.0);
                if (sector) pa->set_thetalim(0.0, 180.0);
                pa->plot(lin(0.4, 2.6, 200), V(200, 0.6), {.color = Color::Red, .linewidth = 6.0f});
                const Shot s = shoot(dev, data, *fig);
                const PolarPlan& pl = s.plan;
                int n = 0, bad = 0;
                for (int y = 0; y < H; y += 2)
                    for (int x = 0; x < W; x += 2) {
                        const double px = x + 0.5, py = y + 0.5;
                        const double ux = (px - pl.cx) / pl.radius, uy = (pl.cy - py) / pl.radius;
                        const double t = std::atan2(uy, ux);
                        if (t < 0.55 || t > 2.45) continue;   // the ends and their caps
                        const double d = std::fabs(std::hypot(ux, uy) - 0.6) * pl.radius;
                        if (d > 1.5 && d < 4.5) continue;
                        ++n;
                        if (s.red(x, y) != (d <= 1.5)) ++bad;
                    }
                std::printf("  arc line (%s): %d samples, %d wrong\n", sector ? "sector" : "disc", n, bad);
                check(n > 200 && bad * 100 <= n, std::string("gl: a line along an arc keeps its width (") +
                                                     (sector ? "sector" : "disc") + ")");
            }

            // A line on the rim is cut at it.
            {
                auto fig = Figure::create();
                fig->resize(W, H);
                auto pa = fig->add_subplot_polar(1, 1, 1);
                pa->grid(false);
                pa->set_rlim(0.0, 1.0);
                pa->plot(lin(0.0, 2.0 * pi, 400), V(400, 1.0), {.color = Color::Red, .linewidth = 10.0f});
                const Shot s = shoot(dev, data, *fig);
                const PolarPlan& pl = s.plan;
                int outside = 0, inside = 0;
                for (int q = 0; q < 24; ++q) {
                    const double a = 0.1 + q * 0.26;
                    outside += s.red(pl.cx + (pl.radius + 3.0) * std::cos(a), pl.cy - (pl.radius + 3.0) * std::sin(a));
                    inside += s.red(pl.cx + (pl.radius - 2.5) * std::cos(a), pl.cy - (pl.radius - 2.5) * std::sin(a));
                }
                check(outside == 0 && inside >= 22, "gl: a line on the rim is cut there (nothing beyond, the inner half drawn)");
            }

            // Markers: shown inside, culled outside the r limits and the sector.
            {
                auto fig = Figure::create();
                fig->resize(W, H);
                auto pa = fig->add_subplot_polar(1, 1, 1);
                pa->grid(false);
                pa->set_rlim(0.4, 1.0);
                pa->set_thetalim(0.0, 180.0);
                const V th{0.5, 1.5, 2.5, 4.0, 1.0, 1.0}, r{0.6, 0.8, 0.9, 0.7, 0.2, 1.4};
                pa->scatter(th, r, {.color = Color::Red, .size = 14.0f, .alpha = 1.0f});
                const Shot s = shoot(dev, data, *fig);
                const PolarPlan& pl = s.plan;
                const bool want[] = {true, true, true, false, false, false};
                bool all = true;
                for (int i = 0; i < 6; ++i) {
                    const double x = pl.px(th[i], r[i]), y = pl.py(th[i], r[i]);
                    all &= s.red(x, y) == want[i];
                }
                check(all, "gl: markers inside the frame drawn at their points, those past the limits or the sector culled");
            }

            // A 20π spiral keeps its float precision: every point of the curve is red.
            {
                auto fig = Figure::create();
                fig->resize(W, H);
                auto pa = fig->add_subplot_polar(1, 1, 1);
                pa->grid(false);
                pa->set_rlim(0.0, 1.0);
                const int N = 8000;
                const V th = lin(0.0, 20.0 * pi, N), r = lin(0.02, 1.0, N);
                pa->plot(th, r, {.color = Color::Red, .linewidth = 3.0f});
                const Shot s = shoot(dev, data, *fig);
                int n = 0, bad = 0;
                for (int i = 0; i < N; i += 7) {
                    if (r[i] > 0.97) continue;
                    ++n;
                    if (!s.red(s.plan.px(th[i], r[i]), s.plan.py(th[i], r[i]))) ++bad;
                }
                std::printf("  20π spiral: %d points, %d not drawn\n", n, bad);
                check(n > 500 && bad * 100 <= n, "gl: a 20π spiral is drawn where it should be (θ wrapped in double)");
            }
        }

        bool same_pixels(const RgbaImage& a, const RgbaImage& b) {
            return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
        }

        void caches(RenderDevice& dev, DataRenderer& data) {
            auto fig = Figure::create();
            fig->resize(W, H);
            auto pa = fig->add_subplot_polar(1, 1, 1);
            V th, r;
            for (int i = 0; i < 2000; ++i) {
                th.push_back(i * 0.01);
                r.push_back(1.0 + 0.4 * std::sin(i * 0.05));
            }
            pa->plot(th, r, {.color = Color::Red, .linewidth = 2.0f, .linestyle = LineStyle::Dashed});
            pa->scatter(V{0.5, 1.5, 2.5}, V{1.0, 1.2, 0.8}, {.size = 8.0f});
            pa->fill(V{0.0, 1.0, 2.0}, V{0.2, 0.3, 0.2}, V{0.6, 0.8, 0.6});
            pa->bar(V{3.0}, V{0.5}, {.width = 0.5f});
            (void)shoot(dev, data, *fig);
            const auto before = PolarRenderer::uploads();
            check(before > 0, "polar cache: the first frame built the buffers");
            (void)shoot(dev, data, *fig);
            check(PolarRenderer::uploads() == before, "polar cache: the same figure again uploads nothing");
            for (const double deg: {30.0, 75.0, 140.0}) {
                pa->set_theta_offset(deg);
                (void)shoot(dev, data, *fig);
            }
            pa->set_theta_direction(-1);
            (void)shoot(dev, data, *fig);
            check(PolarRenderer::uploads() == before, "polar cache: turning the axes uploads nothing (dashes included)");
            fig->resize(W, H);
            pa->set_thetalim(0.0, 270.0);
            (void)shoot(dev, data, *fig);
            check(PolarRenderer::uploads() == before, "polar cache: a new θ span uploads nothing");

            // An r zoom re-measures the dashes only, and once; a drag keeps them.
            pa->set_rlim(0.0, 2.0);
            (void)shoot(dev, data, *fig);
            const auto after_zoom = PolarRenderer::uploads();
            std::printf("  uploads: first %llu, after r zoom %llu\n", before, after_zoom);
            check(after_zoom == before + 1, "polar cache: an r zoom re-measures the dash buffer, nothing else");
            data.polar().set_nav_active(true);
            pa->set_rlim(0.0, 1.2);
            (void)shoot(dev, data, *fig);
            pa->set_rlim(0.0, 1.0);
            (void)shoot(dev, data, *fig);
            check(PolarRenderer::uploads() == after_zoom, "polar cache: during a drag the dashes are not re-measured");
            data.polar().set_nav_active(false);
            const Shot released = shoot(dev, data, *fig);
            check(PolarRenderer::uploads() == after_zoom + 1, "polar cache: the first frame after the drag re-measures them once");

            // ... and then they are what a fresh renderer draws.
            DataRenderer fresh;
            const Shot exact = shoot(dev, fresh, *fig);
            check(same_pixels(released.img, exact.img), "polar dashes: exact again after the drag (same pixels as a fresh renderer)");

            // A changed point set rebuilds the buffers.
            const auto n0 = PolarRenderer::uploads();
            pa->scatter(V{0.1}, V{0.5}, {.size = 8.0f});
            (void)shoot(dev, data, *fig);
            check(PolarRenderer::uploads() > n0, "polar cache: a new object uploads its own buffers");
        }
    } // namespace

    void test_polar_gpu() {
        std::printf("\n=== polar_gpu ===\n");
        GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
        RenderDevice dev;
        DataRenderer data;
        areas(dev, data);
        strokes_and_markers(dev, data);
        caches(dev, data);
    }
} // namespace lt
