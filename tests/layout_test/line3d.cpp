// line3d: ingest, the color range, segments, and auto-scale. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "renderer/line3d.h"
#include "plot_data_view.h"
#include "hint.h"
#include "widgets/data_panel.h"

namespace lt {
    // -------------------------------------------------------------------------
    // line3d(): overloads, throws, segments, auto-scale
    // -------------------------------------------------------------------------
    void test_line3d_ingest() {
        std::printf("\n[3D: line3d ingest, segments and auto-scale]\n");

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

        // A path needs two points (one would draw nothing).
        const std::vector<double> one{1.0};
        check(threw([&] { ax->line3d({}, {}, {}); }),
              "line3d: an empty series throws, as a cloud's does");
        check(threw([&] { ax->line3d(one, one, one); }),
              "line3d: and so does a path of one point, which has no segment to draw -- "
              "the one ingest rule a cloud does not share");
        const std::vector<double> two{1.0, 2.0};
        check(!threw([&] { ax->line3d(two, two, two); }),
              "line3d: two points are a path");

        check(threw([&] { ax->line3d(x, y, {z.data(), 2}); }),
              "line3d: three coordinate vectors of different lengths throw");
        check(threw([&] { ax->line3d(x, y, z, {c.data(), 2}); }),
              "line3d: a colors vector that is neither empty nor |x| long throws -- a value "
              "belongs to a point, not to a segment");
        const std::vector<double> nan_z{0.0, std::numeric_limits<double>::quiet_NaN(), 1.0};
        check(threw([&] { ax->line3d(x, y, nan_z); }),
              "line3d: a non-finite coordinate throws, once, rather than reaching the buffer");
        const std::vector<double> nan_c{1.0, 2.0, std::numeric_limits<double>::infinity()};
        check(threw([&] { ax->line3d(x, y, z, nan_c); }),
              "line3d: and a non-finite colour is rejected on the same terms as a coordinate");
        check(threw([&] {
                  Line3DOptions o;
                  o.vmax = std::numeric_limits<float>::quiet_NaN();
                  ax->line3d(x, y, z, c, o);
              }),
              "line3d: a non-finite vmin/vmax throws -- it is a divisor downstream");

        // Error bars: line3d uses the same length rule (spans are checked fully in
        // scatter3d).
        {
            std::vector<double> e{0.5, 0.5, 0.5}, e2{0.5, 0.5};
            ErrorBar3D err;
            err.z_cap_lo = e;
            check(!threw([&] { ax->line3d(x, y, z, err); }) &&
                  !threw([&] { ax->line3d(x, y, z, c, err); }),
                  "line3d: error-bar data of one entry per point is accepted, through both "
                  "overloads that take it");
            err.z_box_hi = e2;
            check(threw([&] { ax->line3d(x, y, z, err); }) &&
                  threw([&] { ax->line3d(x, y, z, c, err); }),
                  "line3d: and a span of any other length throws");
            Line3DOptions styled;
            styled.errorbar.linewidth = 2.0f;
            styled.errorbar.capstyle = CapStyle::Arrow;
            styled.errorbar.box_alpha = 0.5f;
            check(!threw([&] { ax->line3d(x, y, z, styled); }),
                  "line3d: and its style is not data, so none of it throws");
        }

        check(!threw([&] { ax->line3d(x, y, z); }),
              "line3d: three finite vectors of one length are accepted");
        check(!threw([&] { ax->line3d(x, y, z, c); }),
              "line3d: and so is a fourth colour dimension");

        // Only whether `colors` is non-empty distinguishes the overloads.
        Line3DPlot flat;
        flat.x = x;
        flat.y = y;
        flat.z = z;
        Line3DPlot mapped = flat;
        mapped.colors = c;
        check(!flat.colormapped() && mapped.colormapped() && flat.count() == 3,
              "line3d: a colors vector is the only thing that makes a series colormapped");

        // segment_count()/segment_ends(): the one definition of `loop`.
        {
            check(flat.segment_count() == 2,
                  "line3d: three points make two segments");
            Line3DPlot looped = flat;
            looped.opts.loop = true;
            check(looped.segment_count() == 3,
                  "line3d: loop adds exactly one segment, and no point");

            std::size_t a = 99, b = 99;
            flat.segment_ends(0, a, b);
            const bool first_ok = (a == 0 && b == 1);
            flat.segment_ends(1, a, b);
            const bool last_ok = (a == 1 && b == 2);
            looped.segment_ends(2, a, b);
            const bool wrap_ok = (a == 2 && b == 0);
            check(first_ok && last_ok && wrap_ok,
                  "line3d: the closing segment is the one that wraps to point 0, and the "
                  "open segments are unaffected by loop");

            // A looped two-point path draws its segment back over itself.
            Line3DPlot pair;
            pair.x = two;
            pair.y = two;
            pair.z = two;
            pair.opts.loop = true;
            pair.segment_ends(1, a, b);
            check(pair.segment_count() == 2 && a == 1 && b == 0,
                  "line3d: a looped two-point path closes onto itself rather than "
                  "degenerating");
        }

        // Color range: the series' own range when the interval is empty.
        {
            double lo = 0.0, hi = 0.0;
            line3d_value_range(mapped, lo, hi);
            check(lo == 10.0 && hi == 40.0,
                  "line3d: an empty vmin/vmax interval means the colors' own range");
            Line3DPlot fixed = mapped;
            fixed.opts.vmin = 0.0f;
            fixed.opts.vmax = 100.0f;
            line3d_value_range(fixed, lo, hi);
            check(lo == 0.0 && hi == 100.0, "line3d: and a stated one is used as stated");
            line3d_value_range(flat, lo, hi);
            check(lo == 0.0 && hi == 1.0,
                  "line3d: a series with no colors has no range to take, and says 0..1");
        }

        // Auto-scale: the points are the extent.
        {
            const DataBounds3D b = auto_scale3d({}, {}, {}, {}, {flat}, {}, 0.0);
            check(b.xmin == 0.0 && b.xmax == 2.0 && b.ymin == 5.0 && b.ymax == 7.0 &&
                  b.zmin == -1.0 && b.zmax == 3.0,
                  "line3d: its extent is exactly its points, on all three axes at once");

            // `loop` adds no point, so it can't move a limit (e.g. by appending a
            // copy of the first point).
            Line3DPlot looped = flat;
            looped.opts.loop = true;
            const DataBounds3D lb = auto_scale3d({}, {}, {}, {}, {looped}, {}, 0.0);
            check(lb.xmin == b.xmin && lb.xmax == b.xmax && lb.ymin == b.ymin &&
                  lb.ymax == b.ymax && lb.zmin == b.zmin && lb.zmax == b.zmax,
                  "line3d: loop closes the path without adding a point, so the limits "
                  "are exactly what they were");

            // It shares the box with the other kinds.
            const Bar3DPlot bars = bar3d_grid();
            const DataBounds3D both = auto_scale3d({bars}, {}, {}, {}, {flat}, {}, 0.0);
            const DataBounds3D bar_only = auto_scale3d({bars}, {}, {}, {}, {}, {}, 0.0);
            check(both.zmax >= flat.z[2] && both.zmax >= bar_only.zmax,
                  "line3d: a path and a bar grid in one axes give the union of their extents");
        }

        // End to end through the pipeline: a path spanning y 5..7 must move the
        // axis off the empty 0..1.
        {
            auto fig2 = Figure::create({.width = 400, .height = 320});
            auto ax2 = fig2->add_subplot3d(1, 1, 1);
            ax2->line3d(x, y, z);
            fig2->savefig("line3d_box.svg");
            std::ifstream f("line3d_box.svg");
            const std::string svg((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
            check(!svg.empty() && svg.find(">6<") != std::string::npos &&
                  svg.find(">0.2<") == std::string::npos,
                  "line3d: a path ingested through the public API reaches auto_scale3d and "
                  "moves the box -- the y axis is annotated over the path's own range, not "
                  "the empty axes' 0..1");
        }
    }

    // -------------------------------------------------------------------------
    // The ribbon: scene-space width, gradient, miter, loop, depth cue
    // -------------------------------------------------------------------------
    void test_line3d_render() {
        std::printf("\n[3D: line3d rendered -- a width in the scene, and the ramp]\n");

        using namespace sextant;

        constexpr int W = 420, H = 360;

        // Looking down +x at zero elevation: a constant-x segment is horizontal on
        // screen and its thickness is a vertical pixel run.
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

        auto red = [](const unsigned char* p) {
            return p[0] > p[1] + 20 && p[0] > p[2] + 20;
        };
        // The vertical run of path pixels through one column.
        auto thickness_at = [&](const unsigned char* px, int w, int h, Px3 q) {
            const int xi = static_cast<int>(std::lround(q.x));
            const int yc = static_cast<int>(std::lround(q.y));
            if (xi < 0 || xi >= w || yc < 0 || yc >= h) return 0;
            auto on = [&](int y) { return red(px + (y * w + xi) * 4); };
            if (!on(yc)) return 0;
            int lo = yc, hi = yc;
            while (lo > 0 && on(lo - 1)) --lo;
            while (hi + 1 < h && on(hi + 1)) ++hi;
            return hi - lo + 1;
        };

        // ---- Width is a scene length: two paths at different depths (and
        // different y, so neither hides the other), probed at their midpoints.
        // The inverse of the cloud's marker-size check.
        const double near_x = 0.92, far_x = 0.08;
        const double near_y = 0.75, far_y = 0.25;

        for (const Projection mode: {Projection::Orthographic, Projection::Perspective}) {
            const bool persp = mode == Projection::Perspective;
            FigureSnapshot fs = bare(mode);
            for (int k = 0; k < 2; ++k) {
                const double x = k == 0 ? near_x : far_x;
                const double y = k == 0 ? near_y : far_y;
                Line3DPlot l;
                l.x = std::vector<double>{x, x};
                l.y = std::vector<double>{y - 0.18, y + 0.18};
                l.z = std::vector<double>{0.5, 0.5};
                l.opts.color = Color::Red;
                l.opts.linewidth = 20.0f;
                fs.axes[0].snap3d()->lines3d.push_back(l);
            }
            render(fs, persp ? "line3d_persp" : "line3d_ortho");

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;

            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load(persp ? "line3d_persp.png" : "line3d_ortho.png",
                                          &w, &h, &comp, 4);
            int near_t = 0, far_t = 0;
            double near_depth = 0.0, far_depth = 0.0;
            if (px) {
                const Px3 n = proj.project(near_x, near_y, 0.5);
                const Px3 f = proj.project(far_x, far_y, 0.5);
                near_depth = n.depth;
                far_depth = f.depth;
                near_t = thickness_at(px, w, h, n);
                far_t = thickness_at(px, w, h, f);
                stbi_image_free(px);
            }
            std::printf("  %-13s depths %.3f / %.3f, ribbon thickness %d / %d px\n",
                        persp ? "perspective" : "orthographic",
                        near_depth, far_depth, near_t, far_t);

            if (persp) {
                check(far_depth > near_depth * 1.2,
                      "line3d: the perspective probe really does put its two paths at "
                      "different distances");
                // A pixel-width stroke would be equal here.
                check(near_t > far_t + 1,
                      "line3d: under perspective a near stretch of path is drawn thicker "
                      "than a far one -- the width is a length in the scene, not a count "
                      "of pixels (spec_3d.md §4's world-space ribbon)");
                // Thicknesses are in inverse ratio to depth.
                const double want = far_depth / near_depth;
                const double got = far_t > 0 ? static_cast<double>(near_t) / far_t : 0.0;
                check(got > want * 0.9 && got < want * 1.1,
                      "line3d: and it thins by the depth ratio, not merely in the right "
                      "direction");
            } else {
                check(near_t == far_t && near_t >= 19 && near_t <= 21,
                      "line3d: under an orthographic camera there is no foreshortening, so "
                      "one width is drawn everywhere -- and it is the pixel width asked "
                      "for, measured at the box centre");
            }
        }

        // ---- The miter join: a thick right angle; an unmitered ribbon leaves a
        // wedge of background at the outer corner.
        {
            FigureSnapshot fs = bare(Projection::Orthographic);
            Line3DPlot l;
            // An L in the screen plane: across, then up.
            l.x = std::vector<double>{0.5, 0.5, 0.5};
            l.y = std::vector<double>{0.20, 0.70, 0.70};
            l.z = std::vector<double>{0.30, 0.30, 0.80};
            l.opts.color = Color::Red;
            l.opts.linewidth = 24.0f;
            fs.axes[0].snap3d()->lines3d.push_back(l);
            render(fs, "line3d_miter");

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load("line3d_miter.png", &w, &h, &comp, 4);
            int gap = 0;
            if (px) {
                // The outer corner's quarter-disc: covered only by a miter.
                const Px3 c = proj.project(0.5, 0.70, 0.30);
                const int cx = static_cast<int>(std::lround(c.x));
                const int cy = static_cast<int>(std::lround(c.y));
                for (int dy = 1; dy <= 8; ++dy)
                    for (int dx = 1; dx <= 8; ++dx) {
                        if (dx * dx + dy * dy > 64) continue;
                        const int sx = cx + dx, sy = cy + dy;
                        if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
                        if (!red(px + (sy * w + sx) * 4)) ++gap;
                    }
                stbi_image_free(px);
            }
            std::printf("  miter: %d uncovered pixels in the outer corner\n", gap);
            check(px != nullptr && gap == 0,
                  "line3d: a bend is mitered, so the outer corner is filled -- an "
                  "unmitered ribbon leaves a wedge of background at every one of a "
                  "path's interior points");
        }

        // ---- `loop`: the closing segment is the only difference.
        {
            auto ink_between = [&](bool loop) {
                FigureSnapshot fs = bare(Projection::Orthographic);
                Line3DPlot l;
                l.x = std::vector<double>{0.5, 0.5, 0.5};
                l.y = std::vector<double>{0.20, 0.80, 0.50};
                l.z = std::vector<double>{0.20, 0.20, 0.80};
                l.opts.color = Color::Red;
                l.opts.linewidth = 6.0f;
                l.opts.loop = loop;
                fs.axes[0].snap3d()->lines3d.push_back(l);
                render(fs, loop ? "line3d_loop" : "line3d_open");

                const FigureLayout lay = compute_figure_layout(fs, W, H);
                const Projector3D& proj = lay.cells[0].box3d->proj;
                int w = 0, h = 0, comp = 0;
                unsigned char* px = stbi_load(loop ? "line3d_loop.png" : "line3d_open.png",
                                              &w, &h, &comp, 4);
                if (!px) return -1;
                // The midpoint of the closing segment.
                const Px3 m = proj.project(0.5, 0.35, 0.50);
                int lit = 0;
                const int mx = static_cast<int>(std::lround(m.x));
                const int my = static_cast<int>(std::lround(m.y));
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dx = -3; dx <= 3; ++dx) {
                        const int sx = mx + dx, sy = my + dy;
                        if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
                        if (red(px + (sy * w + sx) * 4)) ++lit;
                    }
                stbi_image_free(px);
                return lit;
            };
            const int open = ink_between(false);
            const int loop = ink_between(true);
            std::printf("  loop: %d closing-segment pixels open, %d looped\n", open, loop);
            check(open == 0 && loop > 0,
                  "line3d: loop draws the segment from the last point back to the first, "
                  "and an open path draws nothing there");
        }

        // ---- The gradient: the value ramps, not the color. Viridis 0 to 1 via
        // RGB passes through brown; the correct midpoint is colormaps::get(0.5).
        {
            FigureSnapshot fs = bare(Projection::Orthographic);
            Line3DPlot l;
            l.x = std::vector<double>{0.5, 0.5};
            l.y = std::vector<double>{0.20, 0.80};
            l.z = std::vector<double>{0.5, 0.5};
            l.colors = std::vector<double>{0.0, 1.0};
            l.opts.linewidth = 10.0f;
            fs.axes[0].snap3d()->lines3d.push_back(l);
            render(fs, "line3d_ramp");

            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& proj = lay.cells[0].box3d->proj;
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load("line3d_ramp.png", &w, &h, &comp, 4);
            bool ramped = false, ends_ok = false;
            if (px) {
                auto at = [&](double y) {
                    const Px3 q = proj.project(0.5, y, 0.5);
                    const int xi = static_cast<int>(std::lround(q.x));
                    const int yi = static_cast<int>(std::lround(q.y));
                    Color c{};
                    if (xi >= 0 && xi < w && yi >= 0 && yi < h) {
                        const unsigned char* p = px + (yi * w + xi) * 4;
                        c = {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f};
                    }
                    return c;
                };
                const Color a = at(0.22), m = at(0.50), b = at(0.78);
                // The RGB blend would be about (0.63, 0.45, 0.24): far off on every
                // channel.
                const uint8_t* lut = colormaps::get(Colormap::Viridis);
                const Color mid{
                    lut[128 * 4] / 255.0f, lut[128 * 4 + 1] / 255.0f,
                    lut[128 * 4 + 2] / 255.0f, 1.0f
                };
                ramped = std::fabs(m.r - mid.r) < 0.10f &&
                         std::fabs(m.g - mid.g) < 0.10f &&
                         std::fabs(m.b - mid.b) < 0.10f;
                // The ends are the colormap's ends.
                const Color v0 = line3d_point_color(l, 0, 0.0, 1.0);
                const Color v1 = line3d_point_color(l, 1, 0.0, 1.0);
                ends_ok = std::fabs(a.b - v0.b) < 0.25f && std::fabs(b.r - v1.r) < 0.25f;
                std::printf("  ramp mid: drawn (%.2f,%.2f,%.2f) vs colormap (%.2f,%.2f,%.2f)"
                            "; an RGB blend would be (0.63,0.45,0.24)\n",
                            m.r, m.g, m.b, mid.r, mid.g, mid.b);
                stbi_image_free(px);
            }
            check(ramped, "line3d: a colors vector ramps the *value* along the segment and "
                  "looks the colour up, so a midpoint is a colour the colorbar "
                  "beside it actually shows");
            check(ends_ok, "line3d: and the ramp's ends are the two points' own colours");
        } {
            // depthshade darkens the far stretch (off by default). Which end is
            // far is asked of the projector; probes sit inside the square caps.
            const double ax = 0.05, az = 0.25, bx = 0.95, bz = 0.75;
            auto probe = [&](float shade) {
                FigureSnapshot fs = bare(Projection::Perspective);
                Line3DPlot l;
                l.x = std::vector<double>{ax, bx};
                l.y = std::vector<double>{0.5, 0.5};
                l.z = std::vector<double>{az, bz};
                l.opts.color = Color::Red;
                l.opts.linewidth = 14.0f;
                l.opts.depthshade = shade;
                fs.axes[0].snap3d()->lines3d.push_back(l);
                render(fs, shade > 0.0f ? "line3d_shaded" : "line3d_flat");
                const FigureLayout lay = compute_figure_layout(fs, W, H);
                const Projector3D& proj = lay.cells[0].box3d->proj;
                int w = 0, h = 0, comp = 0;
                unsigned char* px = stbi_load(shade > 0.0f
                                                  ? "line3d_shaded.png"
                                                  : "line3d_flat.png",
                                              &w, &h, &comp, 4);
                // 15% and 85% along the path.
                auto lerp = [](double u, double v, double t) { return u + (v - u) * t; };
                const Px3 pa = proj.project(lerp(ax, bx, 0.15), 0.5, lerp(az, bz, 0.15));
                const Px3 pb = proj.project(lerp(ax, bx, 0.85), 0.5, lerp(az, bz, 0.85));
                auto red_at = [&](Px3 q) {
                    const int xi = static_cast<int>(std::lround(q.x));
                    const int yi = static_cast<int>(std::lround(q.y));
                    if (!px || xi < 0 || xi >= w || yi < 0 || yi >= h) return 0;
                    return static_cast<int>(px[(yi * w + xi) * 4]);
                };
                // Sorted by the projector's depth: `first` is nearer.
                const bool a_near = pa.depth < pb.depth;
                const std::pair<int, int> out{
                    red_at(a_near ? pa : pb),
                    red_at(a_near ? pb : pa)
                };
                if (px) stbi_image_free(px);
                return out;
            };
            const auto flat = probe(0.0f);
            const auto shaded = probe(1.0f);
            std::printf("  depthshade red: flat %d/%d, shaded %d/%d (near/far)\n",
                        flat.first, flat.second, shaded.first, shaded.second);
            check(std::abs(flat.first - flat.second) < 12,
                  "line3d: depthshade is off by default, so both ends draw in the "
                  "colour that was asked for");
            check(shaded.first > shaded.second + 40,
                  "line3d: and with it on the far end is darkened toward black, on "
                  "SurfaceOptions::shading's own terms");
        }
    }

    // -------------------------------------------------------------------------
    // A path through a sheet, ordered for the SVG, with the ramp surviving splits
    // -------------------------------------------------------------------------
    void test_line3d_svg_order() {
        std::printf("\n[3D: a path through a sheet, ordered for the SVG]\n");

        using namespace sextant;

        constexpr int W = 460, H = 400;

        // A sheet at x = 0.5 and a path crossing it repeatedly, so segments are
        // both in front and behind.
        auto build = [&](float sheet_alpha, bool colormapped) {
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

            Line3DPlot path;
            std::vector<double> px, py, pz, pc;
            // A zig-zag through the sheet eight times.
            for (int i = 0; i < 9; ++i) {
                px.push_back(i % 2 == 0 ? 0.15 : 0.85);
                py.push_back(0.12 + 0.09 * i);
                pz.push_back(0.20 + 0.07 * i);
                pc.push_back(static_cast<double>(i));
            }
            path.x = px;
            path.y = py;
            path.z = pz;
            if (colormapped) path.colors = pc;
            path.opts.color = Color::Red;
            path.opts.linewidth = 4.0f;
            s->lines3d.push_back(std::move(path));
            return fs;
        };

        bool newell_on = true;
        if (const char* env = std::getenv("SEXTANT_NEWELL"))
            newell_on = std::atoi(env) != 0;

        for (const float alpha: {0.45f, 1.0f}) {
            const FigureSnapshot fs = build(alpha, false);
            const RenderSnapshot3D* s = fs.axes[0].snap3d();
            const FigureLayout lay = compute_figure_layout(fs, W, H);
            const Projector3D& pj = lay.cells[0].box3d->proj;

            const std::vector<Surface3DPolygon> splan = plan_surfaces3d(pj, s->surfaces);
            const std::vector<Line3DSegment> lplan = plan_lines3d(pj, s->lines3d);
            PaintOrderStats st;
            const std::vector<ScenePaint> scene =
                    plan_scene3d(pj, {}, splan, {}, {}, lplan, {}, {}, &st);

            check(lplan.size() == 8,
                  "line3d/svg: every segment of the path is planned, and only the segments");
            check(!st.bailed, "line3d/svg: the scene stays inside the work bound");

            struct Slot {
                bool is_line;
                std::size_t index;
            };
            std::vector<Slot> slots;
            std::vector<std::vector<float>> rings(scene.size());
            std::vector<Vec3> plane_p0(scene.size()), plane_n(scene.size());
            std::vector<char> plane_ok(scene.size(), 0);
            // A split piece is probed over its own pixels (`sp.index` is the
            // parent's).
            std::vector<float> px_a(scene.size()), py_a(scene.size());
            std::vector<float> px_b(scene.size()), py_b(scene.size());
            for (std::size_t k = 0; k < scene.size(); ++k) {
                const ScenePaint& sp = scene[k];
                if (sp.kind == ScenePaint::Kind::Line) {
                    slots.push_back({true, sp.index});
                    if (sp.index < lplan.size()) {
                        if (sp.xy.size() >= 4) {
                            px_a[k] = sp.xy[0];
                            py_a[k] = sp.xy[1];
                            px_b[k] = sp.xy[2];
                            py_b[k] = sp.xy[3];
                        } else {
                            px_a[k] = lplan[sp.index].x0;
                            py_a[k] = lplan[sp.index].y0;
                            px_b[k] = lplan[sp.index].x1;
                            py_b[k] = lplan[sp.index].y1;
                        }
                    }
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

            // Inset 0.75 px: points on shared edges are ambiguous.
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

            // Probed along the piece's screen extent, with depth where each
            // pixel's ray passes the parent's 3D line (exact under perspective).
            auto depth_on_segment = [&](const Line3DSegment& g, float qx, float qy,
                                        float& out) {
                const Projector3D::Ray3 r = pj.ray_from_pixel(qx, qy);
                const Vec3 u = g.b - g.a;
                const Vec3 w = g.a - r.origin;
                const double a = dot(u, u), b = dot(u, r.dir), c = dot(r.dir, r.dir);
                const double den = a * c - b * b;
                if (std::fabs(den) < 1e-14 || a < 1e-14) return false;
                const double t = std::clamp((b * dot(r.dir, w) - c * dot(u, w)) / den,
                                            0.0, 1.0);
                out = pj.project_box(g.a + u * t).depth;
                return true;
            };

            int covered = 0, wrong = 0, in_front = 0, behind = 0;
            for (std::size_t i = 0; i < slots.size(); ++i) {
                if (!slots[i].is_line || slots[i].index >= lplan.size()) continue;
                const Line3DSegment& g = lplan[slots[i].index];
                for (int t = 1; t <= 9; ++t) {
                    const float f = t / 10.0f;
                    const float qx = px_a[i] + (px_b[i] - px_a[i]) * f;
                    const float qy = py_a[i] + (py_b[i] - py_a[i]) * f;
                    float qd = 0.0f;
                    if (!depth_on_segment(g, qx, qy, qd)) continue;
                    for (std::size_t j = 0; j < slots.size(); ++j) {
                        if (slots[j].is_line || !plane_ok[j] || !inside(rings[j], qx, qy))
                            continue;
                        const Projector3D::Ray3 r = pj.ray_from_pixel(qx, qy);
                        const double den = dot(plane_n[j], r.dir);
                        if (std::fabs(den) < 1e-12) continue;
                        const double tt = dot(plane_n[j], plane_p0[j] - r.origin) / den;
                        const float d = pj.project_box(r.origin + r.dir * tt).depth;
                        // Near the crossing both are at the same depth; the
                        // tolerance separates that from a real misordering.
                        if (std::fabs(d - qd) < 3e-3f) continue;
                        ++covered;
                        const bool line_nearer = qd < d;
                        ++(line_nearer ? in_front : behind);
                        if (line_nearer ? (i < j) : (i > j)) ++wrong;
                    }
                }
            }
            std::printf("  %s sheet: %zu polys -> %zu, %d splits, %d line points under a "
                        "polygon, %d in front / %d behind, %d misordered\n",
                        alpha < 1.0f ? "translucent" : "opaque     ",
                        st.input, st.output, static_cast<int>(st.splits),
                        covered, in_front, behind, wrong);
            check(in_front > 5 && behind > 5,
                  "line3d/svg: the sheet covers the path on both sides of itself, so the "
                  "order below is asked in both directions");
            if (newell_on)
                check(wrong == 0,
                      "line3d/svg: every stretch of path a polygon covers is emitted on the "
                      "right side of it -- stroke_vs_polygon() answers a segment exactly");
            else
                check(wrong > 0,
                      "line3d/svg (control): the whole-object order gets the path against a "
                      "sheet wrong, which is what makes the check above about the painter");
        }

        // ---- The file: strokes with their own width; colormapped ones carry
        // gradients with colormap stops.
        {
            const FigureSnapshot fs = build(1.0f, true);
            export_figure_svg(fs, "line3d_scene.svg", W, H);
            std::ifstream f("line3d_scene.svg");
            const std::string svg((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());

            std::size_t lines = 0, at = 0;
            while ((at = svg.find("<line ", at)) != std::string::npos) {
                ++lines;
                at += 6;
            }
            std::size_t grads = 0;
            at = 0;
            while ((at = svg.find("<linearGradient", at)) != std::string::npos) {
                ++grads;
                at += 15;
            }
            std::printf("  svg: %zu <line> elements, %zu gradients\n", lines, grads);
            check(lines >= 8 && grads >= 8,
                  "line3d/svg: the file carries a stroke per segment, each with its own "
                  "gradient -- a two-stop ramp between the ends would be the RGB chord the "
                  "raster path rejects");
            check(svg.find("stroke-linecap=\"round\"") != std::string::npos,
                  "line3d/svg: and a round cap, which is what joins the path at a bend when "
                  "each segment is its own stroke");

            // The middle stop is the colormap's own value (compared with
            // colormaps::get()).
            const std::size_t g0 = svg.find("<linearGradient");
            const std::size_t gend = svg.find("</linearGradient>", g0);
            bool stops_ok = false;
            if (g0 != std::string::npos && gend != std::string::npos) {
                const std::string block = svg.substr(g0, gend - g0);
                std::size_t n = 0, p = 0;
                while ((p = block.find("<stop ", p)) != std::string::npos) {
                    ++n;
                    p += 6;
                }
                stops_ok = n >= 5; // more than a two-stop chord, by a wide margin
            }
            check(stops_ok,
                  "line3d/svg: a gradient has stops along it rather than only at its two "
                  "ends, which is what keeps every drawn colour on the colormap");
        }
    }

    // -------------------------------------------------------------------------
    // The legend key and the colorbar
    // -------------------------------------------------------------------------
    void test_line3d_legend_and_colorbar() {
        std::printf("\n[3D: a path's legend key and colorbar]\n");

        using namespace sextant;

        // Values away from 0..1, so resolved and declared ranges differ.
        Line3DPlot l;
        l.x = std::vector<double>{0.1, 0.5, 0.9};
        l.y = std::vector<double>{0.2, 0.5, 0.8};
        l.z = std::vector<double>{0.3, 0.5, 0.7};
        l.colors = std::vector<double>{20.0, 50.0, 80.0};
        l.opts.colorbar = true;
        l.opts.name = "path";

        auto with_path = [&](Line3DPlot lp) {
            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            fs.axes[0].snap3d()->lines3d.push_back(std::move(lp));
            return fs;
        };

        // ---- The bar
        {
            const auto reqs = find_colorbar_requests(*with_path(l).axes[0].snap3d());
            check(reqs.size() == 1 && reqs[0].vmin == 20.0f && reqs[0].vmax == 80.0f,
                  "line3d cb: a colormapped path that asks for a bar gets one, spanning its "
                  "own colors");
            check(reqs[0].name == "path",
                  "line3d cb: and the series' label names the scale on it");

            Line3DPlot fixed = l;
            fixed.opts.vmin = 0.0f;
            fixed.opts.vmax = 100.0f;
            const auto fr = find_colorbar_requests(*with_path(fixed).axes[0].snap3d());
            check(fr.size() == 1 && fr[0].vmin == 0.0f && fr[0].vmax == 100.0f,
                  "line3d cb: a declared range is taken as declared");

            Line3DPlot flat = l;
            flat.colors = CowVec<double>{};
            check(find_colorbar_requests(*with_path(flat).axes[0].snap3d()).empty(),
                  "line3d cb: a flat path asking for a bar gets none -- there is no mapping "
                  "for it to explain");
        }

        // ---- The key
        {
            const auto e = collect_legend_entries(*with_path(l).axes[0].snap3d());
            check(e.size() == 1 && e[0].kind == LegendKind::Line && e[0].name == "path",
                  "line3d legend: a path is keyed by a stroke, and by its own label");
            check(e[0].swept && e[0].cmap == Colormap::Viridis,
                  "line3d legend: a colormapped path's key is the map swept along the "
                  "stroke -- a flat swatch has no one colour it could honestly show, and "
                  "every line's swatch is the same shape, so shape cannot tell two apart");

            Line3DPlot flat = l;
            flat.colors = CowVec<double>{};
            flat.opts.color = Color::Orange;
            const auto fe = collect_legend_entries(*with_path(flat).axes[0].snap3d());
            check(fe.size() == 1 && !fe[0].swept &&
                  fe[0].color.r == Color::Orange.r && fe[0].color.g == Color::Orange.g,
                  "line3d legend: and a flat path keys by a stroke of its own colour");

            // A colormapped path is keyed (unlike a colormapped surface).
            SurfacePlot sheet;
            sheet.orient = PlaneOrientation::XY;
            sheet.u = std::vector<double>{0.0, 0.5, 1.0};
            sheet.v = std::vector<double>{0.0, 0.5, 1.0};
            sheet.heights = std::vector<double>(9, 0.5);
            sheet.opts.colormap = true;
            sheet.opts.name = "sheet";
            FigureSnapshot both = with_path(l);
            both.axes[0].snap3d()->surfaces.push_back(std::move(sheet));
            const auto be = collect_legend_entries(*both.axes[0].snap3d());
            check(be.size() == 1 && be[0].name == "path",
                  "line3d legend: a colormapped path is keyed where a colormapped surface "
                  "is not");

            Line3DPlot quiet = l;
            quiet.opts.show_legend = false;
            check(collect_legend_entries(*with_path(quiet).axes[0].snap3d()).empty(),
                  "line3d legend: show_legend is the second gate, as it is for every kind "
                  "that can be keyed");
        }

        // ---- The swatch in the file: its gradient stops, and that it references
        // the gradient.
        {
            constexpr int W = 420, H = 340;
            FigureSnapshot fs = with_path(l);
            fs.axes[0].snap3d()->legend_enabled = true;
            export_figure_svg(fs, "line3d_legend.svg", W, H);
            std::ifstream f("line3d_legend.svg");
            const std::string svg((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
            const std::size_t g = svg.find("legendGrad");
            bool referenced = false, stops_ok = false;
            if (g != std::string::npos) {
                const std::size_t end = svg.find("</linearGradient>", g);
                const std::string block = svg.substr(g, end - g);
                int n = 0;
                std::size_t p = 0;
                while ((p = block.find("<stop ", p)) != std::string::npos) {
                    ++n;
                    p += 6;
                }
                stops_ok = n >= 5;
                referenced = svg.find("stroke=\"url(#legendGrad", g) != std::string::npos;
            }
            check(g != std::string::npos && stops_ok,
                  "line3d legend/svg: the key's gradient is written with stops sampled from "
                  "the colormap");
            check(referenced,
                  "line3d legend/svg: and the swatch stroke actually references it, rather "
                  "than a gradient being emitted beside a flat line");
        }
    }

    // -------------------------------------------------------------------------
    // Hover at vertices, the Data-panel tab, and the edit lanes
    // -------------------------------------------------------------------------
    void test_line3d_hints_and_panel() {
        std::printf("\n[3D: a path under the pointer, and in the panels]\n");

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

        Line3DPlot path;
        path.x = std::vector<double>{2.0, 5.0, 8.0};
        path.y = std::vector<double>{2.0, 6.0, 3.0};
        path.z = std::vector<double>{3.0, 5.0, 7.0};
        path.opts.linewidth = 10.0f;

        for (int mode = 0; mode < 2; ++mode) {
            cam.projection = mode ? Projection::Perspective : Projection::Orthographic;
            const char* what = mode ? "perspective" : "orthographic";
            const Projector3D proj(tf, cam, frame, 0.1f);

            RenderSnapshot3D snap;
            snap.lines3d.push_back(path);

            // ---- The cursor on a vertex names it.
            int named = 0;
            for (std::size_t i = 0; i < path.count(); ++i) {
                const Px3 q = proj.project(path.x[i], path.y[i], path.z[i]);
                const auto r = find_hint3d(snap, proj, q.x, q.y);
                if (!r) continue;
                char want[64];
                std::snprintf(want, sizeof(want), "x=%.4g, y=%.4g, z=%.4g",
                              path.x[i], path.y[i], path.z[i]);
                if (r->text == want) ++named;
            }
            check(named == 3,
                  std::string("line3d hint (") + what + "): the pointer on a vertex names "
                  "that vertex, by its three coordinates");

            // ---- A path is hovered at vertices only: a segment's midpoint answers
            // nothing.
            {
                const double mx = (path.x[0] + path.x[1]) * 0.5;
                const double my = (path.y[0] + path.y[1]) * 0.5;
                const double mz = (path.z[0] + path.z[1]) * 0.5;
                const Px3 q = proj.project(mx, my, mz);
                check(!find_hint3d(snap, proj, q.x, q.y).has_value(),
                      std::string("line3d hint (") + what + "): and the middle of a segment "
                      "answers nothing -- a path is hovered where its data is");
            }

            // ---- A colormapped path also reports its value.
            {
                RenderSnapshot3D cs;
                Line3DPlot c = path;
                c.colors = std::vector<double>{11.0, 22.0, 33.0};
                cs.lines3d.push_back(c);
                const Px3 q = proj.project(c.x[1], c.y[1], c.z[1]);
                const auto r = find_hint3d(cs, proj, q.x, q.y);
                check(r && r->text.find("c=22") != std::string::npos,
                      std::string("line3d hint (") + what + "): a colormapped path adds the "
                      "c value, which is the one thing position does not show");
            }
        }

        // ---- Hit radius = max(drawn half width, 12 px floor). Tested with a
        // 40 px line, so the two radii differ (20 vs 12).
        {
            const Projector3D proj(tf, cam, frame, 0.1f);
            RenderSnapshot3D thin, thick;
            Line3DPlot t = path;
            t.opts.linewidth = 1.0f;
            Line3DPlot w = path;
            w.opts.linewidth = 40.0f;
            thin.lines3d.push_back(t);
            thick.lines3d.push_back(w);
            const Px3 q = proj.project(path.x[0], path.y[0], path.z[0]);
            check(find_hint3d(thick, proj, q.x + 16.0f, q.y).has_value(),
                  "line3d hint: a path wider than the floor is hoverable over the ink it "
                  "actually draws");
            check(!find_hint3d(thin, proj, q.x + 16.0f, q.y).has_value(),
                  "line3d hint: and a thin one is not -- the radius follows the width where "
                  "the width is the larger, rather than always");
            check(find_hint3d(thin, proj, q.x + 8.0f, q.y).has_value(),
                  "line3d hint: while the floor still makes a hairline a target rather than "
                  "a test of aim");
        }

        // ---- The Data panel's table and tab.
        {
            RenderSnapshot3D snap;
            Line3DPlot c = path;
            c.colors = std::vector<double>{11.0, 22.0, 33.0};
            c.opts.name = "trajectory";
            snap.lines3d.push_back(c);
            snap.lines3d.push_back(path); // flat: three columns

            const auto tables = collect_plot_data_tables(snap);
            check(tables.size() == 2 && tables[0].kind == PlotKind::Line3D &&
                  tables[0].label == "trajectory",
                  "line3d panel: a path gets a table of its own, named by its label");
            check(tables[0].columns.size() == 4 && tables[0].columns[3].name == std::string("c"),
                  "line3d panel: a colormapped path shows the fourth column");
            check(tables[1].columns.size() == 3,
                  "line3d panel: and a flat one shows three -- a column of nothing is not "
                  "the same statement as no column");
            check(tables[0].columns[0].count == 3,
                  "line3d panel: the rows are the path's *points*, not its segments, which "
                  "have no values of their own");

            const auto tabs = data_panel_tabs(tables, 0);
            check(tabs.size() == 2 && tabs[0].table == 0 && tabs[1].table == 1,
                  "line3d panel: each path gets its own tab");
        }

        // ---- Edit lanes: appearance and a data op.
        {
            RenderSnapshot3D dst;
            dst.lines3d.push_back(path);
            dst.lines3d[0].opts.hint_labels = {"a", "b", "c"};

            AxesEdit3D e;
            Line3DOptions o = path.opts;
            o.color = Color::Green;
            o.loop = true;
            e.lines3d.push_back({0, o});
            apply_axes3d_edit(dst, e);
            check(dst.lines3d[0].opts.loop &&
                  dst.lines3d[0].opts.color.g == Color::Green.g,
                  "line3d lane: an appearance edit reaches the path it names");
            check(dst.lines3d[0].opts.hint_labels.size() == 3,
                  "line3d lane: and does not carry a stale copy of the hint labels back "
                  "over the ones the Data panel edits");

            AxesEdit3D d2;
            d2.plot_ops.push_back(PlotCellEdit{PlotKind::Line3D, 0, 2, 1, 4.25, -1});
            apply_axes3d_edit(dst, d2);
            check(dst.lines3d[0].z[1] == 4.25,
                  "line3d lane: a data op moves the vertex it names, in the column it names");

            // Addressed by kind: a cloud and a path at index 0 are different
            // objects.
            RenderSnapshot3D both;
            both.lines3d.push_back(path);
            Scatter3DPlot cloud;
            cloud.x = path.x;
            cloud.y = path.y;
            cloud.z = path.z;
            both.scatter3d.push_back(cloud);
            AxesEdit3D d3;
            d3.plot_ops.push_back(PlotCellEdit{PlotKind::Line3D, 0, 0, 0, 9.5, -1});
            apply_axes3d_edit(both, d3);
            check(both.lines3d[0].x[0] == 9.5 && both.scatter3d[0].x[0] == 2.0,
                  "line3d lane: an op naming a path reaches the path and not the cloud at "
                  "the same index");
        }
    }
} // namespace lt
