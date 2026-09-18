// Axis position: AxesStyle's placement enums and origin components, their
// effect on insets, and what each output draws. Part of sextant_layout_test;
// see layout_test.h.
#include "layout_test.h"
#include "axis_placement.h"

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 400, H = 320;

        // One 2D axes over fixed 0..10 limits with a two-point line (auto-limit checks
        // turn the fixed limits off).
        FigureSnapshot one_axes(AxesStyle st = {}) {
            FigureSnapshot fs;
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            RenderSnapshot& s = *fa.snap2d();
            s.xmin = 0.0;
            s.xmax = 10.0;
            s.xlim_auto = false;
            s.ymin = 0.0;
            s.ymax = 10.0;
            s.ylim_auto = false;
            s.axes_style = std::move(st);
            LinePlot lp;
            lp.x = CowVec<double>(std::vector<double>{1.0, 9.0});
            lp.y = CowVec<double>(std::vector<double>{1.0, 9.0});
            s.lines.push_back(std::move(lp));
            fs.axes.push_back(std::move(fa));
            fs.generation = fs.data_generation = 1;
            return fs;
        }

        CellLayout lay(const AxesStyle& st) {
            return compute_figure_layout(one_axes(st), W, H).cells[0];
        }
    } // namespace

    // -------------------------------------------------------------------------
    // place_axis(), shared by 2D and 3D
    // -------------------------------------------------------------------------
    void test_axis_placement() {
        std::printf("\n[axis position: where the line goes]\n");

        const std::optional<double> none;

        const auto lo = place_axis(AxisPosition::Low, none, 0.0, 10.0);
        const auto autp = place_axis(AxisPosition::Auto, none, 0.0, 10.0);
        const auto mid = place_axis(AxisPosition::Mid, none, 0.0, 10.0);
        const auto hi = place_axis(AxisPosition::High, none, 0.0, 10.0);

        check(lo.pos == 0.0 && !lo.interior && !lo.high, "Low sits at the minimum, on the edge");
        check(autp.pos == lo.pos && autp.interior == lo.interior && autp.high == lo.high,
              "Auto and Low are the same placement in 2D");
        check(mid.pos == 5.0 && mid.interior && !mid.high, "Mid sits at the midpoint, interior");
        check(hi.pos == 10.0 && !hi.interior && hi.high, "High sits at the maximum, on the far edge");

        // Mid uses the resolved range, so an asymmetric one still lands between.
        const auto mid2 = place_axis(AxisPosition::Mid, none, -3.0, 7.0);
        check(mid2.pos == 2.0 && mid2.interior, "Mid is the middle of the range, not zero");

        // A pin supersedes the enum, whatever the enum said.
        const auto pinned = place_axis(AxisPosition::High, std::optional<double>{2.5}, 0.0, 10.0);
        check(pinned.pos == 2.5 && pinned.interior && !pinned.high,
              "an origin component supersedes the enum");

        // ...and clamps to the frame in both directions.
        const auto below = place_axis(AxisPosition::Mid, std::optional<double>{-5.0}, 0.0, 10.0);
        const auto above = place_axis(AxisPosition::Mid, std::optional<double>{99.0}, 0.0, 10.0);
        check(below.pos == 0.0 && !below.interior && !below.high,
              "a pin below the range clamps to the low edge");
        check(above.pos == 10.0 && !above.interior && above.high,
              "a pin above the range clamps to the high edge, and takes its tick side");

        // A degenerate range collapses onto Low.
        const auto flat = place_axis(AxisPosition::Mid, none, 4.0, 4.0);
        check(!flat.interior && !flat.high, "a degenerate range leaves the axis on the edge");

        // widen_for_origin only touches automatic bounds.
        double lo2 = 5.0, hi2 = 10.0;
        widen_for_origin(std::optional<double>{0.0}, true, lo2, hi2);
        check(lo2 == 0.0 && hi2 == 10.0, "an automatic bound widens to take the origin in");
        double lo3 = 5.0, hi3 = 10.0;
        widen_for_origin(std::optional<double>{0.0}, false, lo3, hi3);
        check(lo3 == 5.0 && hi3 == 10.0, "an explicit bound does not");
    }

    // -------------------------------------------------------------------------
    // What the cell layout makes of it
    // -------------------------------------------------------------------------
    void test_axis_position_layout() {
        std::printf("\n[axis position: the layout]\n");

        const CellLayout def = lay({});
        check(near_px(def.xaxis_y, def.frame.y + def.frame.h) && near_px(def.yaxis_x, def.frame.x),
              "default: the axes are the frame's bottom and left edges");
        check(!def.xaxis_interior && !def.yaxis_interior,
              "default: neither axis needs a stroke of its own");
        check(def.xtick_dir > 0.0f && def.ytick_dir < 0.0f,
              "default: ticks hang below and to the left");
        check(def.ylabel_align == HAlign::Right, "default: y labels end at the axis");

        // Mid lands on the transform's midpoint as its own line.
        AxesStyle mid_st;
        mid_st.xaxis_y = mid_st.yaxis_x = AxisPosition::Mid;
        const CellLayout mid = lay(mid_st);
        check(near_px(mid.xaxis_y, mid.tr.to_py(5.0)) && near_px(mid.yaxis_x, mid.tr.to_px(5.0)),
              "Mid: both lines sit where the transform puts the middle value");
        check(mid.xaxis_interior && mid.yaxis_interior, "Mid: both lines are interior");
        check(mid.xtick_dir > 0.0f && mid.ytick_dir < 0.0f,
              "Mid: ticks keep the low side's direction");

        // An interior axis reserves nothing; the frame gains the band's size.
        check(mid.reserved.bottom < def.reserved.bottom && mid.reserved.left < def.reserved.left,
              "Mid: the tick bands stop being reserved");
        check(mid.frame.h > def.frame.h && mid.frame.w > def.frame.w,
              "Mid: and the frame grows by what they took");

        // High moves the band to the far side and flips the ticks.
        AxesStyle hi_st;
        hi_st.xaxis_y = hi_st.yaxis_x = AxisPosition::High;
        const CellLayout hi = lay(hi_st);
        check(near_px(hi.xaxis_y, hi.frame.y) && near_px(hi.yaxis_x, hi.frame.x + hi.frame.w),
              "High: the axes are the top and right edges");
        check(!hi.xaxis_interior && !hi.yaxis_interior,
              "High: they are still frame edges, so still drawn by a spine");
        check(hi.xtick_dir < 0.0f && hi.ytick_dir > 0.0f, "High: the ticks point the other way");
        check(hi.ylabel_align == HAlign::Left, "High: and the y numbers start at the axis");
        check(near_px(hi.reserved.top, def.reserved.bottom) &&
              near_px(hi.reserved.right, def.reserved.left),
              "High: the two bands swap sides, whole");
        check(near_px(hi.frame.h, def.frame.h),
              "High: so the frame keeps its height exactly");
        // Not the same width, correctly: the side insets also hold half of the end
        // x labels ("0" vs "10"). What matters is the band moved.
        check(hi.frame.w < mid.frame.w, "High: and gives up a band's worth of width, unlike Mid");

        // Label anchors follow the ticks: below the line at Low, above at High.
        check(def.xlabel_top > def.xaxis_y, "default: the x numbers hang below the line");
        check(hi.xlabel_top < hi.xaxis_y, "High: the x numbers sit above it");
        check(def.ylabel_x < def.yaxis_x && hi.ylabel_x > hi.yaxis_x,
              "the y numbers sit on the side the ticks point");

        // Titles stay in the outer band.
        AxesStyle t_st = mid_st;
        FigureSnapshot fs = one_axes(t_st);
        fs.axes[0].snap2d()->xtitle = "x";
        fs.axes[0].snap2d()->ytitle = "y";
        const CellLayout tc = compute_figure_layout(fs, W, H).cells[0];
        check(tc.xtitle_y > tc.frame.y + tc.frame.h && tc.ytitle_x < tc.frame.x,
              "the axis titles stay outside the frame when the axes move inside it");
    }

    // -------------------------------------------------------------------------
    // The origin components, and what they do to limits
    // -------------------------------------------------------------------------
    void test_axis_origin_and_limits() {
        std::printf("\n[axis position: the origin]\n");

        // Auto limits over data far from zero: pinning to zero widens the view.
        auto fs = one_axes({});
        auto& s = *fs.axes[0].snap2d();
        s.xlim_auto = s.ylim_auto = true;
        s.lines.clear();
        LinePlot lp;
        lp.x = CowVec<double>(std::vector<double>{5.0, 10.0});
        lp.y = CowVec<double>(std::vector<double>{5.0, 10.0});
        s.lines.push_back(std::move(lp));

        const CellLayout plain = compute_figure_layout(fs, W, H).cells[0];
        check(plain.tr.xmin > 0.0 && plain.tr.ymin > 0.0,
              "without a pin, automatic limits stay around the data");

        s.axes_style.origin_x = 0.0;
        s.axes_style.origin_y = 0.0;
        const CellLayout wide = compute_figure_layout(fs, W, H).cells[0];
        check(wide.tr.xmin < 0.0 && wide.tr.ymin < 0.0,
              "a pin outside the data widens automatic limits past it");
        check(wide.xaxis_interior && wide.yaxis_interior,
              "and past it far enough that the axes are inside the frame, not on its edge");
        check(near_px(wide.xaxis_y, wide.tr.to_py(0.0)) && near_px(wide.yaxis_x, wide.tr.to_px(0.0)),
              "the lines land on zero itself");

        // With auto_scale's padding (no separate rule for pins).
        check(std::abs((wide.tr.xmax - wide.tr.xmin) - (10.0 - 0.0) * (1.0 + 2 * kAutoScalePad)) < 1e-9,
              "a pinned bound gets exactly auto_scale's padding, not its own");

        // Explicit limits win; the axis clamps onto the edge.
        s.xlim_auto = s.ylim_auto = false;
        s.xmin = s.ymin = 5.0;
        s.xmax = s.ymax = 10.0;
        const CellLayout clamped = compute_figure_layout(fs, W, H).cells[0];
        check(clamped.tr.xmin == 5.0 && clamped.tr.ymin == 5.0,
              "explicit limits are not widened for a pin");
        check(near_px(clamped.xaxis_y, clamped.frame.y + clamped.frame.h) &&
              near_px(clamped.yaxis_x, clamped.frame.x),
              "the axes clamp to the frame edge they went off");
        check(!clamped.xaxis_interior && !clamped.yaxis_interior,
              "a clamped axis is an edge again, so a spine draws it");

        // One component alone moves only the axis measured across it.
        AxesStyle one;
        one.origin_y = 5.0;
        const CellLayout part = lay(one);
        check(part.xaxis_interior && !part.yaxis_interior,
              "origin_y alone moves the x axis and leaves the y axis on its edge");
        check(near_px(part.xaxis_y, part.tr.to_py(5.0)),
              "and puts it on the value it names");
    }

    // -------------------------------------------------------------------------
    // What each output actually draws
    // -------------------------------------------------------------------------
    void test_axis_position_rendered() {
        std::printf("\n[axis position: rendered]\n");

        AxesStyle mid_st;
        mid_st.xaxis_y = mid_st.yaxis_x = AxisPosition::Mid;
        // No box, so the only dark ink is the axis lines and the data (kept away
        // from the middle).
        mid_st.spine_bottom = mid_st.spine_left = false;
        mid_st.spine_top = mid_st.spine_right = false;

        AxesStyle box_st; // the default placement, for the spine checks

        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            NvgRenderer nvg(ctx.nvg());
            DataRenderer data;
            unsigned long long gen = 0;
            for (auto [name, st]: {std::pair{"ax_mid", mid_st}, std::pair{"ax_box", box_st}}) {
                FigureSnapshot fs = one_axes(st);
                fs.generation = fs.data_generation = ++gen;
                export_figure_png(ctx, nvg, data, fs, std::string(name) + ".png", W, H, 1);
                export_figure_svg(fs, std::string(name) + ".svg", W, H);
            }
        }

        const CellLayout mid = lay(mid_st);
        const CellLayout box = lay(box_st);

        // Dark grey ink: a spine or axis line (not white ground or the blue data).
        auto load = [&](const char* name, const std::function<void(std::function < bool(float, float) >)>& body) {
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load((std::string(name) + ".png").c_str(), &w, &h, &comp, 4);
            check(px != nullptr && w == W && h == H, std::string("rendered: ") + name + ".png decoded");
            if (!px) return;
            // A 1 px line off the pixel grid spans two rows, so probe a one-pixel
            // neighbourhood.
            body([&](float x, float y) {
                const int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int ix = cx + dx, iy = cy + dy;
                        if (ix < 0 || iy < 0 || ix >= w || iy >= h) continue;
                        const unsigned char* p = px + (iy * w + ix) * 4;
                        // 200, not the spine's 77: a half-coverage blend. Grey channels
                        // move together; the blue data line's don't.
                        if (p[0] < 200 && p[1] < 200 && p[2] < 200 &&
                            std::abs(int(p[0]) - int(p[2])) < 40)
                            return true;
                    }
                return false;
            });
            stbi_image_free(px);
        };

        std::printf("  mid frame (%.1f,%.1f %.1fx%.1f) axes x@%.2f y@%.2f\n",
                    mid.frame.x, mid.frame.y, mid.frame.w, mid.frame.h, mid.xaxis_y, mid.yaxis_x);
        load("ax_mid", [&](auto dark) {
            // The lines are at a quarter and three quarters of the frame.
            const float x25 = mid.frame.x + mid.frame.w * 0.25f;
            const float x75 = mid.frame.x + mid.frame.w * 0.75f;
            const float y25 = mid.frame.y + mid.frame.h * 0.25f;
            const float y75 = mid.frame.y + mid.frame.h * 0.75f;
            check(dark(x25, mid.xaxis_y) && dark(x75, mid.xaxis_y),
                  "rendered: the x axis is a line across the middle of the frame");
            check(dark(mid.yaxis_x, y25) && dark(mid.yaxis_x, y75),
                  "rendered: and the y axis runs down it");
            // With every spine off, the frame edges carry no ink.
            check(!dark(x25, mid.frame.y) && !dark(x25, mid.frame.y + mid.frame.h),
                  "rendered: spine_top/bottom off leaves the horizontal edges bare");
            check(!dark(mid.frame.x, y75) && !dark(mid.frame.x + mid.frame.w, y75),
                  "rendered: spine_left/right off leaves the vertical edges bare");
            // Ticks hang below the interior line: 3 px down is inside the 5 px mark
            // at tick 3.0.
            check(dark(mid.tr.to_px(3.0), mid.xaxis_y + 3.0f),
                  "rendered: the x ticks hang off the interior line, into the frame");
        });

        load("ax_box", [&](auto dark) {
            const float x50 = box.frame.x + box.frame.w * 0.5f;
            const float y50 = box.frame.y + box.frame.h * 0.5f;
            check(dark(x50, box.frame.y) && dark(x50, box.frame.y + box.frame.h) &&
                  dark(box.frame.x, y50) && dark(box.frame.x + box.frame.w, y50),
                  "rendered: by default all four spines draw, as the rect did");
            check(!dark(x50, y50), "rendered: and nothing crosses the middle");
        });

        // The SVG agrees: interior axes are one <path>.
        auto svg_of = [](const char* name) {
            std::ifstream f(std::string(name) + ".svg");
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        };

        const std::string mid_svg = svg_of("ax_mid");
        char want[128];
        std::snprintf(want, sizeof(want), "M%g %gH%g", mid.frame.x, mid.xaxis_y,
                      mid.frame.x + mid.frame.w);
        check(mid_svg.find(want) != std::string::npos,
              "svg: the interior x axis is emitted at the layout's own y");
        std::snprintf(want, sizeof(want), "M%g %gV%g", mid.yaxis_x, mid.frame.y,
                      mid.frame.y + mid.frame.h);
        check(mid_svg.find(want) != std::string::npos,
              "svg: and the interior y axis at its x");
        check(mid_svg.find("fill=\"none\" stroke=\"rgb(76,76,76)\"") != std::string::npos,
              "svg: drawn in the spine colour, being a spine that moved");

        // Every spine off: no outline path at all.
        const std::size_t mid_paths = [&] {
            std::size_t n = 0, at = 0;
            while ((at = mid_svg.find("<path d=\"M", at)) != std::string::npos) {
                ++n;
                at += 4;
            }
            return n;
        }();
        const std::string box_svg = svg_of("ax_box");
        check(box_svg.find("<path d=\"M") != std::string::npos,
              "svg: the default box is emitted as a path of its four edges");
        check(mid_paths >= 1, "svg: the interior axes are emitted even with no box");
        check(box_svg.find("<rect x=\"" + std::to_string(box.frame.x)) == std::string::npos,
              "svg: and the old frame <rect> is gone");
    }

    // -------------------------------------------------------------------------
    // 3D: two coordinates per axis; Auto means the camera's edge.
    // -------------------------------------------------------------------------
    void test_axis_placement3d() {
        std::printf("\n[axis position 3D: where the line goes]\n");

        const std::optional<double> none;

        const auto a = place_axis3d(AxisPosition::Auto, none, 0.0, 10.0);
        check(a.camera, "3D Auto with no pin defers to the camera's silhouette edge");

        const auto lo = place_axis3d(AxisPosition::Low, none, 0.0, 10.0);
        const auto hi = place_axis3d(AxisPosition::High, none, 0.0, 10.0);
        check(!lo.camera && lo.at.pos == 0.0 && !hi.camera && hi.at.pos == 10.0,
              "3D Low and High are absolute -- that coordinate's data min and max");

        // A pin beats Auto.
        const auto pinned = place_axis3d(AxisPosition::Auto, std::optional<double>{2.5}, 0.0, 10.0);
        check(!pinned.camera && pinned.at.pos == 2.5 && pinned.at.interior,
              "3D an origin component supersedes Auto, camera included");

        // Clamping is per coordinate: two out of range put the axis on a box edge,
        // one on a face.
        const auto off = place_axis3d(AxisPosition::Mid, std::optional<double>{-4.0}, 0.0, 10.0);
        check(!off.camera && off.at.pos == 0.0 && !off.at.interior,
              "3D a pin outside the range clamps onto that coordinate's face");
    }

    void test_axis_position_3d_plan() {
        std::printf("\n[axis position 3D: the box plan]\n");

        RenderSnapshot3D snap;
        snap.xticks_override = std::vector<Tick>{{0.0, "0"}, {0.5, "h"}, {1.0, "1"}};
        snap.yticks_override = snap.xticks_override;
        snap.zticks_override = snap.xticks_override;
        snap.xtitle = "X";
        snap.ytitle = "Y";
        snap.ztitle = "Z";

        const PlotRect frame{50.0f, 40.0f, 300.0f, 240.0f};
        const Transform3D tf{0, 1, 0, 1, 0, 1, BoxAspect{}};
        const Vec3 h = tf.half_extent();

        Projector3D proj(tf, snap.camera, frame, snap.box_style.margin);
        auto plan_of = [&](const AxesStyle& st) {
            RenderSnapshot3D s = snap;
            s.axes_style = st;
            return plan_box3d(proj, s, *snap.xticks_override, *snap.yticks_override,
                              *snap.zticks_override);
        };
        auto ends = [](const Box3DPlan::Poly& p) {
            return std::pair{
                std::pair{p.xy[0], p.xy[1]},
                std::pair{p.xy[2], p.xy[3]}
            };
        };

        const Box3DPlan def = plan_of({});
        check(def.axis_lines.size() == 3 && def.axis_titles.size() == 3,
              "3D position: the default plan is still three edges and three titles");

        // Every axis pinned to the middle: all three cross at the box centre.
        AxesStyle cross;
        cross.origin_x = cross.origin_y = cross.origin_z = 0.5;
        const Box3DPlan xh = plan_of(cross);
        const Px3 centre = proj.project_box({0.0, 0.0, 0.0});
        check(xh.axis_lines.size() == 3, "3D position: a crosshair is still three lines");
        bool all_through_centre = true;
        for (const auto& ln: xh.axis_lines) {
            const auto [p, q] = ends(ln);
            all_through_centre = all_through_centre &&
                                 near_px((p.first + q.first) * 0.5f, centre.x, 0.5f) &&
                                 near_px((p.second + q.second) * 0.5f, centre.y, 0.5f);
        }
        check(all_through_centre,
              "3D position: three origin components put all three axes through the box centre");

        // The lines moved; the titles stay on the silhouette edges.
        bool titles_fixed = true;
        for (std::size_t i = 0; i < 3; ++i)
            titles_fixed = titles_fixed &&
                           near_px(xh.axis_titles[i].x, def.axis_titles[i].x, 0.01f) &&
                           near_px(xh.axis_titles[i].y, def.axis_titles[i].y, 0.01f);
        check(titles_fixed, "3D position: the titles stay on the Auto edge when the lines move in");

        // So does the label push direction (the line through the centre has none).
        bool same_push = true;
        for (std::size_t i = 0; i < xh.tick_marks.size() && i < def.tick_marks.size(); ++i) {
            const auto [p0, q0] = ends(def.tick_marks[i]);
            const auto [p1, q1] = ends(xh.tick_marks[i]);
            same_push = same_push &&
                        near_px(q0.first - p0.first, q1.first - p1.first, 0.01f) &&
                        near_px(q0.second - p0.second, q1.second - p1.second, 0.01f);
        }
        check(same_push && !xh.tick_marks.empty(),
              "3D position: the tick marks keep the Auto edge's outward direction");

        // origin_z alone drops the x and y axes to z = 0; z stays on the camera's
        // edge.
        AxesStyle zonly;
        zonly.origin_z = 0.5;
        const Box3DPlan pz = plan_of(zonly);
        const auto [zp, zq] = ends(pz.axis_lines[2]);
        const auto [dp, dq] = ends(def.axis_lines[2]);
        check(near_px(zp.first, dp.first, 0.01f) && near_px(zq.second, dq.second, 0.01f),
              "3D position: origin_z alone leaves the z axis on its camera edge");
        const auto [xp, xq] = ends(pz.axis_lines[0]);
        const Px3 want_a = proj.project_box({-h.x, tf.box_y(0.0), 0.0});
        const Px3 want_b = proj.project_box({h.x, tf.box_y(0.0), 0.0});
        // box_y(0.0) is -h.y: the unset component is still the camera's.
        check(near_px(xp.first, want_a.x, 0.01f) && near_px(xq.first, want_b.x, 0.01f) &&
              near_px(xp.second, want_a.y, 0.01f),
              "3D position: while the x axis takes the new z and keeps its camera-chosen y");

        // Low/High are absolute: turning the camera moves an Auto edge, not a
        // High one.
        Camera3D turned = snap.camera;
        turned.azimuth += 180.0;
        // The x axis' endpoints in box space.
        auto x_ends_box = [&](const Camera3D& cam, const AxesStyle& st) {
            RenderSnapshot3D s = snap;
            s.camera = cam;
            s.axes_style = st;
            const Projector3D pr(tf, cam, frame, snap.box_style.margin);
            const Box3DPlan p = plan_box3d(pr, s, *snap.xticks_override, *snap.yticks_override,
                                           *snap.zticks_override);
            // Back out of pixels: axis-aligned endpoints give the two coordinates.
            return ends(p.axis_lines[0]);
        };
        AxesStyle fixed;
        fixed.xaxis_y = fixed.xaxis_z = AxisPosition::High;
        const auto [fa, fb] = x_ends_box(snap.camera, fixed);
        const Px3 hi_a = proj.project_box({-h.x, h.y, h.z});
        const Px3 hi_b = proj.project_box({h.x, h.y, h.z});
        check(near_px(fa.first, hi_a.x, 0.01f) && near_px(fa.second, hi_a.y, 0.01f) &&
              near_px(fb.first, hi_b.x, 0.01f) && near_px(fb.second, hi_b.y, 0.01f),
              "3D position: High puts the x axis on the +y +z edge, not where the camera looks");

        // Half a turn: a High edge stays, an Auto one moves.
        Projector3D proj_t(tf, turned, frame, snap.box_style.margin);
        auto box_of = [&](std::pair<float, float> px, const Projector3D& pr, Vec3 cand_a, Vec3 cand_b) {
            const Px3 a = pr.project_box(cand_a);
            return near_px(px.first, a.x, 0.5f) && near_px(px.second, a.y, 0.5f)
                       ? cand_a
                       : cand_b;
        };
        const auto [ta, tb] = x_ends_box(turned, fixed);
        check(near_px(ta.first, proj_t.project_box({-h.x, h.y, h.z}).x, 0.01f) &&
              near_px(tb.first, proj_t.project_box({h.x, h.y, h.z}).x, 0.01f),
              "3D position: turning the camera leaves a High axis on that same edge");
        const auto [aa, ab] = x_ends_box(snap.camera, AxesStyle{});
        const auto [ba, bb] = x_ends_box(turned, AxesStyle{});
        const Vec3 seen_a = box_of(aa, proj, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z});
        const Vec3 seen_b = box_of(ba, proj_t, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z});
        check(seen_a.y != seen_b.y,
              "3D position: while an Auto axis swaps to the opposite edge, as it always has");
        (void) ab;
        (void) bb;
    }

    void test_axis_origin_3d_limits() {
        std::printf("\n[axis position 3D: the origin and the limits]\n");

        // A point far from the origin, all auto: pinning to zero widens the box.
        auto figure = [](const AxesStyle& st) {
            FigureSnapshot fs;
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            fa.snap = RenderSnapshot3D{};
            RenderSnapshot3D& s = *fa.snap3d();
            s.axes_style = st;
            Scatter3DPlot sc;
            sc.x = CowVec<double>(std::vector<double>{5.0, 10.0});
            sc.y = CowVec<double>(std::vector<double>{5.0, 10.0});
            sc.z = CowVec<double>(std::vector<double>{5.0, 10.0});
            s.scatter3d.push_back(std::move(sc));
            fs.axes.push_back(std::move(fa));
            fs.generation = fs.data_generation = 1;
            return fs;
        };
        auto limits = [&](const AxesStyle& st) {
            const FigureLayout fl = compute_figure_layout(figure(st), W, H);
            return fl.cells[0].box3d->proj.transform();
        };

        const Transform3D plain = limits({});
        check(plain.xmin > 0.0 && plain.zmin > 0.0,
              "3D origin: without a pin, automatic limits stay around the data");
        // The unpinned path is unchanged.
        check(std::abs(plain.zmin - (5.0 - 5.0 * kAutoScalePad)) < 1e-9,
              "3D origin: and are exactly auto_scale3d's, padding included");

        AxesStyle zpin;
        zpin.origin_z = 0.0;
        const Transform3D pinned = limits(zpin);
        check(pinned.zmin < 0.0, "3D origin: a pin outside the data widens that axis past it");
        check(std::abs(pinned.zmin - (0.0 - 10.0 * kAutoScalePad)) < 1e-9,
              "3D origin: by exactly the padding a data point at zero would have got");
        check(std::abs(pinned.xmin - plain.xmin) < 1e-9,
              "3D origin: and the two axes with no component of their own do not move");

        // An explicit limit is never widened; the pin clamps into it.
        AxesStyle zpin2 = zpin;
        FigureSnapshot fs = figure(zpin2);
        RenderSnapshot3D& s = *fs.axes[0].snap3d();
        s.zlim_auto = false;
        s.zmin = 5.0;
        s.zmax = 10.0;
        const FigureLayout fl = compute_figure_layout(fs, W, H);
        const Transform3D fixed = fl.cells[0].box3d->proj.transform();
        check(fixed.zmin == 5.0 && fixed.zmax == 10.0,
              "3D origin: an explicit limit is not widened for a pin");
    }
} // namespace lt
