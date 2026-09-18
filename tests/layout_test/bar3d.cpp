// bar3d: ingest, faces, painter order, hints and the Data panel. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    // ---------------------------------------------------------------------------
    // bar3d
    // ---------------------------------------------------------------------------
    // The GPU and CPU must project alike: clip_matrix() is pushed through GL's
    // divide and viewport transform and must land on project_box()'s pixel.
    void test_bar3d_clip_matrix() {
        std::printf("\n[3D: the GPU and the CPU project alike]\n");

        using namespace sextant;

        constexpr float W = 240.0f, H = 200.0f;
        const PlotRect frame{0.0f, 0.0f, W, H};
        const Transform3D tf{0, 10, -5, 5, 100, 200, BoxAspect{1.4, 1.0, 0.8}};

        // GL's perspective divide and viewport transform, written out.
        auto through_matrix = [&](const std::array<float, 16>& m, Vec3 p) {
            double c[4] = {0, 0, 0, 0};
            const double v[4] = {p.x, p.y, p.z, 1.0};
            for (int r = 0; r < 4; ++r)
                for (int j = 0; j < 4; ++j)
                    c[r] += static_cast<double>(m[static_cast<std::size_t>(j * 4 + r)]) * v[j];
            const double iw = c[3] != 0.0 ? 1.0 / c[3] : 0.0;
            const double nx = c[0] * iw, ny = c[1] * iw, nz = c[2] * iw;
            return std::array < double, 4 > {
                (nx + 1.0) * 0.5 * W, (1.0 - ny) * 0.5 * H, nz, c[3]
            };
        };

        for (int mode = 0; mode < 2; ++mode) {
            Camera3D cam;
            cam.azimuth = -37.0;
            cam.elevation = 24.0;
            cam.zoom = 1.3;
            cam.target = Vec3{0.05, -0.08, 0.02};
            if (mode) {
                cam.projection = Projection::Perspective;
                cam.fov = 70.0;
            }
            const Projector3D proj(tf, cam, frame, 0.1f);
            const std::array<float, 16> m = proj.clip_matrix(W, H);

            double worst = 0.0;
            bool depth_ordered = true, in_range = true;
            double prev_nz = -2.0, prev_depth = -1e30;
            for (int i = 0; i < 27; ++i) {
                const Vec3 p{
                    ((i % 3) - 1) * 0.7 * tf.aspect.x * 0.5,
                    (((i / 3) % 3) - 1) * 0.7 * tf.aspect.y * 0.5,
                    ((i / 9) - 1) * 0.7 * tf.aspect.z * 0.5
                };
                const Px3 cpu = proj.project_box(p);
                const auto gpu = through_matrix(m, p);
                worst = std::max(worst, std::max(std::fabs(gpu[0] - cpu.x),
                                                 std::fabs(gpu[1] - cpu.y)));
                if (gpu[2] < -1.0 || gpu[2] > 1.0) in_range = false;
                // Depth must order as project_box()'s does.
                if (i && (cpu.depth > prev_depth) != (gpu[2] > prev_nz)) depth_ordered = false;
                prev_nz = gpu[2];
                prev_depth = cpu.depth;
            }
            const char* what = mode ? "perspective" : "orthographic";
            check(worst < 0.02,
                  mode
                      ? "bar3d: the matrix lands on project_box()'s pixel under perspective"
                      : "bar3d: the matrix lands on project_box()'s pixel under orthographic");
            check(in_range,
                  mode
                      ? "bar3d: and inside the depth range, so nothing in the box is clipped away (perspective)"
                      : "bar3d: and inside the depth range, so nothing in the box is clipped away (orthographic)");
            check(depth_ordered,
                  mode
                      ? "bar3d: ordering depth the same way the CPU does (perspective)"
                      : "bar3d: ordering depth the same way the CPU does (orthographic)");
            std::printf("  %-13s worst disagreement %.4f px over 27 points\n", what, worst);
        }

        // The data -> box half, checked against Transform3D (not the matrix).
        Camera3D cam;
        const Projector3D proj(tf, cam, frame, 0.1f);
        const Vec3 anchor{5.0, 0.0, 150.0};
        float scale[3], offset[3];
        proj.box_affine(anchor, scale, offset);
        bool affine_ok = true;
        for (const Vec3& d: {Vec3{0, -5, 100}, Vec3{10, 5, 200}, Vec3{3, 1, 175}}) {
            const Vec3 want = tf.to_box(d.x, d.y, d.z);
            const double got[3] = {
                scale[0] * (d.x - anchor.x) + offset[0],
                scale[1] * (d.y - anchor.y) + offset[1],
                scale[2] * (d.z - anchor.z) + offset[2]
            };
            if (std::fabs(got[0] - want.x) > 1e-5 || std::fabs(got[1] - want.y) > 1e-5 ||
                std::fabs(got[2] - want.z) > 1e-5)
                affine_ok = false;
        }
        check(affine_ok, "bar3d: and the data->box step the shader applies first is Transform3D exactly");
    }

    // Ingest resolves footprints to data units and bases per bar.
    void test_bar3d_ingest() {
        std::printf("\n[3D: bar3d ingest and auto-scale]\n");

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
        check(threw([&] { ax->bar3d(PlaneOrientation::XY, u, v, {h.data(), 5}); }),
              "bar3d: heights that are not |u| x |v| throw at ingest");
        check(threw([&] { ax->bar3d(PlaneOrientation::XY, {}, v, {}); }),
              "bar3d: an empty grid throws");
        const std::vector<double> bad{1, 2, 3, std::numeric_limits<double>::quiet_NaN(), 5, 6};
        check(threw([&] { ax->bar3d(PlaneOrientation::XY, u, v, bad); }),
              "bar3d: a non-finite height throws, once, rather than reaching the vertex buffer");
        check(threw([&] { ax->bar3d(PlaneOrientation::XY, u, v, h, std::span<const double>(h).first(3)); }),
              "bar3d: and a bottoms vector that does not match heights");

        // Footprints and the three orientations, via auto_scale3d.
        Bar3DPlot b;
        b.u = u;
        b.v = v;
        b.heights = h;
        b.u_width = 2.0 * 0.8; // spacing x width, as ingest resolves it
        b.v_width = 1.0 * 0.8;
        b.opts.bottom = 0.0;

        const DataBounds3D xy = auto_scale3d({b}, {}, {}, {}, {}, {}, 0.0);
        check(xy.xmin == -0.8 && xy.xmax == 4.8,
              "bar3d: auto-scale spans the bars' footprint, not just their centres");
        check(xy.ymin == -0.4 && xy.ymax == 1.4, "bar3d: on the other grid axis too");
        check(xy.zmin == 0.0 && xy.zmax == 6.0,
              "bar3d: and from base to tallest tip along the axis they stand on");

        Bar3DPlot yz = b;
        yz.orient = PlaneOrientation::YZ;
        const DataBounds3D r2 = auto_scale3d({yz}, {}, {}, {}, {}, {}, 0.0);
        check(r2.ymin == xy.xmin && r2.zmin == xy.ymin && r2.xmax == xy.zmax,
              "bar3d: the orientation rotates which axis is which, and nothing else");

        // A non-zero base moves the span (unlike the 2D bar, zero isn't forced in).
        Bar3DPlot raised = b;
        raised.opts.bottom = 100.0;
        const DataBounds3D r3 = auto_scale3d({raised}, {}, {}, {}, {}, {}, 0.0);
        check(r3.zmin == 100.0 && r3.zmax == 106.0,
              "bar3d: a raised base is where the bars stand, not a gap to the origin");

        // Per-bar bases, and a negative height hanging below its base.
        Bar3DPlot hung = b;
        hung.heights = std::vector<double>{-1, 2, 3, 4, 5, 6};
        const DataBounds3D r4 = auto_scale3d({hung}, {}, {}, {}, {}, {}, 0.0);
        check(r4.zmin == -1.0 && r4.zmax == 6.0,
              "bar3d: a negative height hangs below the base rather than inverting the box");

        // End to end: the public call resolves the footprint from the spacing.
        auto ax2 = Figure::create({.width = 300, .height = 240})->add_subplot3d(1, 1, 1);
        ax2->bar3d(PlaneOrientation::XY, u, v, h, {.width = 0.5f, .depth = 1.0f});
        ax2->set_zlim(0.0, 10.0);
        check(true, "bar3d: a well-formed call is accepted"); // the throw checks are above
    }

    // The geometry: which faces are drawn, and in what order.
    void test_bar3d_faces() {
        std::printf("\n[3D: bar3d faces and the painter sort]\n");

        using namespace sextant;

        Bar3DPlot b;
        b.u = std::vector<double>{1.0};
        b.v = std::vector<double>{2.0};
        b.heights = std::vector<double>{4.0};
        b.u_width = 1.0;
        b.v_width = 2.0;
        b.opts.bottom = 1.0;
        const Transform3D tf{0, 4, 0, 6, 0, 8, BoxAspect{}};

        Bar3DFace f[6];
        bar3d_faces(b, 0, tf, f);

        // The box the corners describe, read back as a bounding box.
        double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
        for (const Bar3DFace& fc: f)
            for (const Vec3& p: fc.p) {
                const double c[3] = {p.x, p.y, p.z};
                for (int a = 0; a < 3; ++a) {
                    lo[a] = std::min(lo[a], c[a]);
                    hi[a] = std::max(hi[a], c[a]);
                }
            }
        check(lo[0] == 0.5 && hi[0] == 1.5, "bar3d: the footprint is centred on the grid coordinate");
        check(lo[1] == 1.0 && hi[1] == 3.0, "bar3d: with its own width on each grid axis");
        check(lo[2] == 1.0 && hi[2] == 5.0, "bar3d: and the bar runs from its base to base + height");

        // Box-space shading: the three visible faces differ in brightness.
        check(f[5].shade > f[1].shade && f[1].shade > f[3].shade,
              "bar3d: top, +x and +y come out at three different brightnesses");
        check(f[4].shade < f[5].shade,
              "bar3d: and the underside is the darkest of the pair it belongs to");
        Bar3DPlot flat = b;
        flat.opts.shading = 0.0f;
        bar3d_faces(flat, 0, tf, f);
        check(f[0].shade == 1.0f && f[5].shade == 1.0f,
              "bar3d: shading 0 leaves every face the flat colour");

        // A reversed limit mirrors that axis, flipping which face is lit.
        const Transform3D mirrored{4, 0, 0, 6, 0, 8, BoxAspect{}};
        Bar3DFace g[6];
        bar3d_faces(b, 0, tf, f);
        bar3d_faces(b, 0, mirrored, g);
        check(f[0].shade == g[1].shade && f[1].shade == g[0].shade,
              "bar3d: a reversed limit swaps which of that axis's faces is lit");
        check(f[4].shade == g[4].shade,
              "bar3d: and leaves the axes it did not reverse alone");

        // ---- The painter sort ------------------------------------------------
        Bar3DPlot grid;
        grid.u = std::vector<double>{0.0, 1.0, 2.0};
        grid.v = std::vector<double>{0.0, 1.0, 2.0};
        grid.heights = std::vector<double>(9, 1.0);
        grid.u_width = grid.v_width = 0.8;
        const Transform3D gt{-0.5, 2.5, -0.5, 2.5, 0, 1.5, BoxAspect{}};
        const PlotRect frame{0.0f, 0.0f, 300.0f, 240.0f};

        for (int mode = 0; mode < 2; ++mode) {
            Camera3D cam;
            if (mode) {
                cam.projection = Projection::Perspective;
                cam.fov = 60.0;
            }
            const Projector3D proj(gt, cam, frame, 0.1f);
            const std::vector<Bar3DPolygon> polys = plan_bars3d(proj, {grid});

            check(polys.size() == 9 * 3,
                  mode
                      ? "bar3d: three faces per bar under perspective -- the other three face away"
                      : "bar3d: three faces per bar, for any camera outside them");

            // A bar's faces come out together (order between bars is checked in
            // test_bar3d_painter_order()).
            bool grouped = true;
            for (std::size_t i = 3; i < polys.size(); i += 3)
                if (polys[i].bar == polys[i - 1].bar) grouped = false;
            for (std::size_t i = 0; i + 2 < polys.size(); i += 3)
                if (polys[i].bar != polys[i + 1].bar || polys[i].bar != polys[i + 2].bar)
                    grouped = false;
            check(grouped, mode
                               ? "bar3d: a bar's faces are emitted together (perspective)"
                               : "bar3d: a bar's faces are emitted together, not interleaved");

            // A drawn face's centroid is nearer than its opposite face's (a depth
            // fact, independent of sign conventions).
            Bar3DFace ff[6];
            bar3d_faces(grid, 4, gt, ff); // the middle bar
            int drawn = 0;
            bool nearer = true;
            for (int a = 0; a < 3; ++a) {
                for (int e = 0; e < 2; ++e) {
                    const Bar3DFace& face = ff[a * 2 + e];
                    const Bar3DFace& opp = ff[a * 2 + (1 - e)];
                    auto centroid = [&](const Bar3DFace& x) {
                        Vec3 c{};
                        for (const Vec3& p: x.p) c = c + gt.to_box(p.x, p.y, p.z);
                        return c * 0.25;
                    };
                    const bool front = proj.faces_camera(centroid(face), face.normal);
                    if (!front) continue;
                    ++drawn;
                    if (proj.project_box(centroid(face)).depth >=
                        proj.project_box(centroid(opp)).depth)
                        nearer = false;
                }
            }
            check(drawn == 3 && nearer,
                  mode
                      ? "bar3d: and each is the nearer of its pair, by depth (perspective)"
                      : "bar3d: and each is the nearer of its pair, by depth");
        }

        // Edge width: pixels at the box centre, so under perspective a farther bar
        // is thinner; under orthographic every bar gets the requested width.
        Bar3DPlot edged = grid;
        edged.opts.edges = true;
        edged.opts.edge_linewidth = 2.0f;

        Camera3D ortho_cam;
        const std::vector<Bar3DPolygon> op =
                plan_bars3d(Projector3D(gt, ortho_cam, frame, 0.1f), {edged});
        bool all_two = !op.empty();
        for (const Bar3DPolygon& p: op) if (std::fabs(p.stroke_width - 2.0f) > 1e-4f) all_two = false;
        check(all_two, "bar3d: under orthographic every bar's outline is exactly the width asked for");

        Camera3D pc;
        pc.projection = Projection::Perspective;
        pc.fov = 90.0;
        const std::vector<Bar3DPolygon> pp = plan_bars3d(Projector3D(gt, pc, frame, 0.1f), {edged});
        float thinnest = 1e9f, thickest = 0.0f;
        for (const Bar3DPolygon& p: pp) {
            thinnest = std::min(thinnest, p.stroke_width);
            thickest = std::max(thickest, p.stroke_width);
        }
        check(thickest > thinnest * 1.05f,
              "bar3d: under perspective it thins with distance -- it is a width in the scene");
        check(thickest > 2.0f && thinnest < 2.0f,
              "bar3d: with the width asked for landing at the box centre, near bars over and far under");

        // Unlike the axis frame, which keeps a fixed screen width.
        RenderSnapshot3D snap;
        snap.camera = pc;
        snap.xticks_override = std::vector<Tick>{{0.0, "0"}, {1.0, "1"}};
        snap.yticks_override = snap.xticks_override;
        snap.zticks_override = snap.xticks_override;
        const Box3DPlan bplan = plan_box3d(Projector3D(gt, pc, frame, 0.1f), snap,
                                           *snap.xticks_override, *snap.yticks_override,
                                           *snap.zticks_override);
        bool marks_equal = bplan.tick_marks.size() > 1;
        for (std::size_t i = 1; i < bplan.tick_marks.size(); ++i) {
            const auto& a = bplan.tick_marks[i - 1].xy;
            const auto& c = bplan.tick_marks[i].xy;
            if (std::fabs(std::hypot(a[2] - a[0], a[3] - a[1]) -
                          std::hypot(c[2] - c[0], c[3] - c[1])) > 1e-3)
                marks_equal = false;
        }
        check(marks_equal,
              "bar3d: while under the same camera every tick mark is still the same pixel length");
    }

    // Painter order checked against separating planes, not the sort key (sorting
    // by face centroid depth was wrong once heights differ). For two bars on a
    // grid, the one on the far side of their separating grid line from the eye
    // must be emitted first.
    void test_bar3d_painter_order() {
        std::printf("\n[3D: bar3d painter order]\n");

        using namespace sextant;

        // Heights chosen so a wrong order is certain: low rows in front of high.
        constexpr std::size_t NU = 5, NV = 5;
        Bar3DPlot b;
        std::vector<double> u(NU), v(NV), h(NU * NV);
        for (std::size_t i = 0; i < NU; ++i) u[i] = static_cast<double>(i);
        for (std::size_t j = 0; j < NV; ++j) v[j] = static_cast<double>(j);
        for (std::size_t i = 0; i < NU; ++i)
            for (std::size_t j = 0; j < NV; ++j)
                h[i * NV + j] = ((i + j) % 2 == 0) ? 0.4 : 6.0;
        b.u = u;
        b.v = v;
        b.heights = h;
        b.u_width = b.v_width = 0.85;

        const Transform3D tf{-0.6, 4.6, -0.6, 4.6, 0.0, 6.5, BoxAspect{}};
        const PlotRect frame{0.0f, 0.0f, 420.0f, 340.0f};
        const Axis3Map m = axis_map(b.orient);

        for (int mode = 0; mode < 2; ++mode) {
            Camera3D cam; // the default view, -60 / 30
            if (mode) {
                cam.projection = Projection::Perspective;
                cam.fov = 75.0;
            }
            const Projector3D proj(tf, cam, frame, 0.12f);
            const std::vector<Bar3DPolygon> polys = plan_bars3d(proj, {b});

            // The eye on each box axis (a far point under orthographic).
            const Vec3 eye = proj.has_eye_point()
                                 ? proj.eye_point()
                                 : proj.eye_dir() * 1e4;
            auto axis_of = [](Vec3 p, int a) { return a == 0 ? p.x : a == 1 ? p.y : p.z; };
            auto cell_centre = [&](std::size_t k) {
                const std::size_t i = k / NV, j = k % NV;
                Vec3 d{};
                double c[3] = {0, 0, 0};
                c[m.u] = u[i];
                c[m.v] = v[j];
                c[m.h] = (b.h_lo(k) + b.h_hi(k)) * 0.5;
                d = {c[0], c[1], c[2]};
                return tf.to_box(d.x, d.y, d.z);
            };

            // First and last emission index of each bar.
            std::vector<int> first(NU * NV, -1), last(NU * NV, -1);
            for (std::size_t p = 0; p < polys.size(); ++p) {
                const std::size_t k = polys[p].bar;
                if (first[k] < 0) first[k] = static_cast<int>(p);
                last[k] = static_cast<int>(p);
            }
            bool all_drawn = true;
            for (std::size_t k = 0; k < NU * NV; ++k) if (first[k] < 0) all_drawn = false;
            check(all_drawn, mode
                                 ? "bar3d order: every bar contributes faces (perspective)"
                                 : "bar3d order: every bar contributes faces");

            // The pairwise constraint, for every pair.
            int violations = 0;
            for (std::size_t a = 0; a < NU * NV; ++a) {
                for (std::size_t c = a + 1; c < NU * NV; ++c) {
                    const Vec3 ca = cell_centre(a), cc = cell_centre(c);
                    // The separating plane: the u grid line between the columns,
                    // or the v one if they share a column.
                    int axis = (a / NV != c / NV) ? m.u : m.v;
                    const double pa = axis_of(ca, axis), pc = axis_of(cc, axis);
                    if (pa == pc) continue; // same cell on this axis
                    const double plane = (pa + pc) * 0.5;
                    const double eye_side = axis_of(eye, axis) - plane;
                    // The bar across the plane from the eye is behind and must be
                    // emitted first.
                    const bool a_behind = (pa - plane) * eye_side < 0.0;
                    const std::size_t behind = a_behind ? a : c;
                    const std::size_t front = a_behind ? c : a;
                    if (last[behind] > first[front]) ++violations;
                }
            }
            check(violations == 0,
                  mode
                      ? "bar3d order: no bar is drawn over one that is in front of it (perspective)"
                      : "bar3d order: no bar is drawn over one that is in front of it");
            if (violations)
                std::printf("    %d of %d pairs out of order\n",
                            violations, static_cast<int>(NU * NV * (NU * NV - 1) / 2));

            // Translucency: the same bar order, with all six faces per bar
            // (hidden three first).
            Bar3DPlot glassy = b;
            glassy.opts.alpha = 0.45f;
            const std::vector<Bar3DPolygon> clear_ = plan_bars3d(proj, {glassy});
            check(clear_.size() == polys.size() * 2,
                  mode
                      ? "bar3d alpha: a translucent bar draws all six faces (perspective)"
                      : "bar3d alpha: a translucent bar draws all six faces, not just the three seen");
            bool same_bar_order = clear_.size() == polys.size() * 2;
            for (std::size_t p = 0; same_bar_order && p < polys.size(); ++p)
                if (clear_[p * 2].bar != polys[p].bar) same_bar_order = false;
            check(same_bar_order,
                  mode
                      ? "bar3d alpha: in the same bar order (perspective)"
                      : "bar3d alpha: in the same bar order -- alpha changes what is drawn, not where");

            // Within a bar, faces turned away come first.
            bool back_first = true;
            for (std::size_t p = 0; p + 1 < clear_.size(); p += 6) {
                for (int f = 0; f < 6; ++f) {
                    const bool front = f >= 3;
                    // Asked of the projector: a face faces the camera exactly when
                    // its centroid is nearer than the opposite face's.
                    const float d = clear_[p + static_cast<std::size_t>(f)].depth;
                    const float opp = clear_[p + static_cast<std::size_t>((f + 3) % 6)].depth;
                    if (front != (d < opp)) back_first = false;
                }
            }
            check(back_first,
                  mode
                      ? "bar3d alpha: hidden faces before visible ones, within each bar (perspective)"
                      : "bar3d alpha: hidden faces before visible ones, within each bar");

            bool carries_alpha = !clear_.empty();
            for (const Bar3DPolygon& p: clear_)
                if (std::fabs(p.fill.a - 0.45f) > 1e-6f) carries_alpha = false;
            check(carries_alpha, mode
                                     ? "bar3d alpha: and every face carries it (perspective)"
                                     : "bar3d alpha: and every face carries it into the output");

            // Translucent bars draw all twelve edges as their own strokes (once
            // each); `edge_alpha` is independent of the face alpha (the two are
            // set differently here).
            Bar3DPlot outlined = glassy;
            outlined.opts.edges = true;
            outlined.opts.edgecolor = {0.0f, 0.0f, 0.0f, 0.8f};
            outlined.opts.edge_alpha = 0.5f;
            outlined.opts.edge_linewidth = 1.5f;
            const std::vector<Bar3DPolygon> caged = plan_bars3d(proj, {outlined});
            std::size_t faces_n = 0, lines_n = 0;
            bool edge_alpha_ok = true, faces_unstroked = true;
            for (const Bar3DPolygon& p: caged) {
                (p.filled ? faces_n : lines_n)++;
                if (p.filled && p.stroke_width != 0.0f) faces_unstroked = false;
                if (!p.filled && std::fabs(p.stroke.a - 0.8f * 0.5f) > 1e-6f)
                    edge_alpha_ok = false;
            }
            check(faces_n == clear_.size() && lines_n == clear_.size() / 6 * 12,
                  mode
                      ? "bar3d alpha: a translucent outline is twelve edges per bar (perspective)"
                      : "bar3d alpha: a translucent outline is all twelve edges of each bar, once each");
            check(faces_unstroked,
                  mode
                      ? "bar3d alpha: with the faces themselves left unstroked (perspective)"
                      : "bar3d alpha: with the faces themselves left unstroked, so no edge is drawn twice");
            check(edge_alpha_ok,
                  mode
                      ? "bar3d alpha: and the outline takes edgecolor.a * edge_alpha (perspective)"
                      : "bar3d alpha: and the outline takes edgecolor.a * edge_alpha, not the face's alpha");

            // By default the outline stays solid over translucent faces.
            Bar3DPlot default_edge = glassy;
            default_edge.opts.edges = true;
            default_edge.opts.edge_linewidth = 1.5f;
            bool solid_outline = false, any_line = false;
            for (const Bar3DPolygon& p: plan_bars3d(proj, {default_edge}))
                if (!p.filled) {
                    any_line = true;
                    solid_outline = p.stroke.a == 1.0f;
                }
            check(any_line && solid_outline,
                  mode
                      ? "bar3d alpha: and by default it stays fully opaque over the glass (perspective)"
                      : "bar3d alpha: and by default it stays fully opaque over the glass");

            // Opaque bars keep the stroke on their faces.
            Bar3DPlot solid = b;
            solid.opts.edges = true;
            solid.opts.edgecolor = {0.0f, 0.0f, 0.0f, 0.8f};
            const std::vector<Bar3DPolygon> solid_p = plan_bars3d(proj, {solid});
            bool all_faces = !solid_p.empty();
            for (const Bar3DPolygon& p: solid_p)
                if (!p.filled || p.stroke_width <= 0.0f || std::fabs(p.stroke.a - 0.8f) > 1e-6f)
                    all_faces = false;
            check(all_faces,
                  mode
                      ? "bar3d alpha: an opaque bar still strokes its visible faces (perspective)"
                      : "bar3d alpha: while an opaque bar still strokes its three visible faces");

            // ...and the old centroid-depth order would fail here (the grid tells
            // them apart).
            if (mode == 0) {
                std::vector<std::size_t> by_depth(polys.size());
                for (std::size_t p = 0; p < polys.size(); ++p) by_depth[p] = p;
                std::stable_sort(by_depth.begin(), by_depth.end(),
                                 [&](std::size_t x, std::size_t y) {
                                     return polys[x].depth > polys[y].depth;
                                 });
                bool same = true;
                for (std::size_t p = 0; p < by_depth.size(); ++p)
                    if (by_depth[p] != p) same = false;
                check(!same,
                      "bar3d order: and centroid depth would have put this scene in a different order");
            }
        }
    }

    // Hover hints over a bar3d grid: the ray meets the bar the picture shows, the
    // nearest surface (bar or plane) wins, and the tooltip names the bar in data
    // coordinates.
    void test_bar3d_hints() {
        std::printf("\n[3D: hover hints over bar3d]\n");

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

        const Bar3DPlot grid = bar3d_grid();

        for (int mode = 0; mode < 2; ++mode) {
            cam.projection = mode ? Projection::Perspective : Projection::Orthographic;
            const char* what = mode ? "perspective" : "orthographic";
            const Projector3D proj(tf, cam, frame, 0.1f);

            // ---- Every face: the ray through a face's projected centre meets
            // that bar (exact).
            int faces = 0, met = 0;
            for (std::size_t k = 0; k < grid.count(); ++k) {
                Bar3DFace f[6];
                bar3d_faces(grid, k, tf, f);
                for (const Bar3DFace& face: f) {
                    Vec3 c{0.0, 0.0, 0.0};
                    for (const Vec3& p: face.p) c = c + p * 0.25;
                    const Px3 px = proj.project(c.x, c.y, c.z);
                    if (!px.in_front()) continue;
                    ++faces;
                    float depth = 0.0f;
                    if (bar3d_ray_hit(grid, k, proj, px.x, px.y, depth)) ++met;
                }
            }
            check(faces > 0 && met == faces,
                  std::string("bar3d hints: the ray through a face's centre meets its own bar, "
                      "all ") + std::to_string(faces) + " of them (" + what + ")");

            // ---- A pixel far outside the silhouette meets nothing.
            int stray = 0;
            for (std::size_t k = 0; k < grid.count(); ++k) {
                float depth = 0.0f;
                if (bar3d_ray_hit(grid, k, proj, frame.x + 1.0f, frame.y + 1.0f, depth)) ++stray;
            }
            check(stray == 0,
                  std::string("bar3d hints: and a pixel in the frame's corner meets none of them (")
                  + what + ")");

            // ---- Height counts: a point above a short bar's top misses it.
            {
                Bar3DPlot low = grid;
                low.heights = std::vector<double>(9, 0.5);
                const Px3 above = proj.project(low.u[1], low.v[1], 6.0);
                float d0 = 0.0f, d1 = 0.0f;
                const bool over = bar3d_ray_hit(low, low.index_of(1, 1), proj, above.x, above.y, d0);
                const Px3 on = proj.project(low.u[1], low.v[1], 0.25);
                const bool onbar = bar3d_ray_hit(low, low.index_of(1, 1), proj, on.x, on.y, d1);
                check(!over && onbar,
                      std::string("bar3d hints: a pixel above a short bar misses it while one on "
                          "it does not (") + what + ")");
            }
        }

        // ---- The hint end to end, orthographic.
        cam.projection = Projection::Orthographic;
        const Projector3D proj(tf, cam, frame, 0.1f);

        RenderSnapshot3D s;
        s.bars3d.push_back(grid);
        // A label per bar (row-major), so a wrong bar quotes the wrong label.
        for (std::size_t k = 0; k < grid.count(); ++k)
            s.bars3d[0].opts.hint_labels.push_back("bar" + std::to_string(k));

        // The nearest bar at a pixel and how many bars it crosses, computed
        // independently for the checks below.
        auto nearest_bar = [&](const Bar3DPlot& b, float px, float py, int& hits) {
            std::size_t nearest = 0;
            float best = 0.0f;
            hits = 0;
            for (std::size_t k = 0; k < b.count(); ++k) {
                float depth = 0.0f;
                if (!bar3d_ray_hit(b, k, proj, px, py, depth)) continue;
                if (hits == 0 || depth < best) {
                    best = depth;
                    nearest = k;
                }
                ++hits;
            }
            return nearest;
        }; {
            // The corner bar nearest this eye (+x and -y toward it).
            const std::size_t k = grid.index_of(2, 0);
            const Px3 top = proj.project(grid.u[2], grid.v[0], grid.h_hi(k));
            int hits = 0;
            check(nearest_bar(grid, top.x, top.y, hits) == k,
                  "bar3d hints: (nothing is in front of this bar's top face, so it really is "
                  "the one under the cursor)");

            auto h = find_hint3d(s, proj, top.x, top.y);
            check(h.has_value(), "bar3d hints: a bar is found under its own top face");
            check(h && h->text.find("x=8") != std::string::npos &&
                  h->text.find("y=2") != std::string::npos &&
                  h->text.find("height=7") != std::string::npos,
                  "bar3d hints: reported in the parent's own coordinates, u and v named "
                  "by the axes the orientation maps them to");
            check(h && h->text.find("bar6") != std::string::npos,
                  "bar3d hints: with its own hint_label, which is index-aligned with heights");
            check(h && h->text.find("base=") == std::string::npos,
                  "bar3d hints: and no base line, since these bars stand on zero");
        }

        // A non-zero base is reported; a zero base is not.
        {
            RenderSnapshot3D based;
            based.bars3d.push_back(grid);
            based.bars3d[0].opts.bottom = 3.0;
            const std::size_t k = grid.index_of(2, 0);
            const Px3 top = proj.project(grid.u[2], grid.v[0],
                                         based.bars3d[0].bottom_at(k) + grid.height_at(k));
            auto h = find_hint3d(based, proj, top.x, top.y);
            check(h && h->text.find("base=3") != std::string::npos,
                  "bar3d hints: a bar standing somewhere other than zero says where");
        }

        // ---- Nearest wins, decided from the depths (not hard-coded).
        {
            const Px3 at = proj.project(grid.u[1], grid.v[1], grid.h_hi(grid.index_of(1, 1)));
            int hits = 0;
            const std::size_t nearest = nearest_bar(grid, at.x, at.y, hits);
            check(hits >= 2,
                  "bar3d hints: (this pixel really does run through more than one bar, so "
                  "the order below is doing work)");
            auto h = find_hint3d(s, proj, at.x, at.y);
            check(h && h->text.find("bar" + std::to_string(nearest)) != std::string::npos,
                  "bar3d hints: and the nearest of them is the one reported");
        }

        // ---- Bars and planes share one depth order: a plane behind the bars
        // doesn't answer where a bar covers it; in front, it does.
        {
            const std::size_t k = grid.index_of(1, 1);
            const Px3 at = proj.project(grid.u[1], grid.v[1], grid.h_hi(k));

            auto with_plane = [&](double offset) {
                RenderSnapshot3D r = s;
                PlaneSnapshot pl;
                pl.orient = PlaneOrientation::XY;
                pl.offset = offset;
                // The point is where this pixel meets the plane; only depth decides.
                double u = 0.0, v = 0.0;
                float d = 0.0f;
                plane_ray_hit(proj, PlaneOrientation::XY, offset, at.x, at.y, u, v, d);
                ScatterPlot sp;
                sp.x = std::vector<double>{u};
                sp.y = std::vector<double>{v};
                sp.opts.hint_labels = {"sheet"};
                pl.sheet.scatters.push_back(std::move(sp));
                r.planes.push_back(std::move(pl));
                return r;
            };

            // Bars are 0..9 tall: a plane at 9.5 is above all, at 0.05 under.
            const RenderSnapshot3D over = with_plane(9.5);
            const RenderSnapshot3D under = with_plane(0.05);
            float d_over = 0.0f, d_under = 0.0f, d_bar = 0.0f;
            double du = 0.0, dv = 0.0;
            plane_ray_hit(proj, PlaneOrientation::XY, 9.5, at.x, at.y, du, dv, d_over);
            plane_ray_hit(proj, PlaneOrientation::XY, 0.05, at.x, at.y, du, dv, d_under);
            bar3d_ray_hit(grid, k, proj, at.x, at.y, d_bar);
            check(d_over < d_bar && d_bar < d_under,
                  "bar3d hints: (the plane above really is nearer than the bar, and the one "
                  "below further -- so the two checks that follow are about the order)");

            auto ho = find_hint3d(over, proj, at.x, at.y);
            auto hu = find_hint3d(under, proj, at.x, at.y);
            check(ho && ho->text.find("sheet") != std::string::npos,
                  "bar3d hints: a plane in front of a bar answers over it");
            check(hu && hu->text.find("bar") != std::string::npos &&
                  hu->text.find("sheet") == std::string::npos,
                  "bar3d hints: and one behind it does not");
        }

        // ---- A reversed limit swaps near/far faces; box_ray_hit() orders the
        // slab itself.
        {
            Transform3D rev = tf;
            rev.xmin = 10.0;
            rev.xmax = 0.0;
            const Projector3D rp(rev, cam, frame, 0.1f);
            const std::size_t k = grid.index_of(2, 0);
            const Px3 top = rp.project(grid.u[2], grid.v[0], grid.h_hi(k));
            float depth = 0.0f;
            check(bar3d_ray_hit(grid, k, rp, top.x, top.y, depth),
                  "bar3d hints: a bar on a reversed axis is still hit");
        }

        // ---- An empty grid contributes nothing (not a hit at the origin).
        {
            RenderSnapshot3D empty;
            empty.bars3d.push_back(Bar3DPlot{});
            const Px3 mid = proj.project(5.0, 5.0, 5.0);
            check(!find_hint3d(empty, proj, mid.x, mid.y).has_value(),
                  "bar3d hints: a grid with no bars in it is not hinted");
        }
    }

    // Data panel for bar3d: the table shape and the ops it builds (addressed at
    // the axes, not a plane).
    void test_bar3d_data_panel() {
        std::printf("\n[3D: the Data panel's bar3d grid]\n");

        using namespace sextant;

        auto snap = [] {
            RenderSnapshot3D s;
            Bar3DPlot b = bar3d_grid();
            b.bottoms = std::vector<double>{0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8};
            for (std::size_t k = 0; k < 9; ++k) b.opts.hint_labels.push_back("L" + std::to_string(k));
            s.bars3d.push_back(std::move(b));
            return s;
        };

        // ---- The four cell-edit columns, each checked against the others
        // staying put.
        {
            RenderSnapshot3D s = snap();
            apply_plot_data_ops(s, {
                                    PlotCellEdit{PlotKind::Bar3D, 0, 0, 2, 9.5}, // u[2]
                                    PlotCellEdit{PlotKind::Bar3D, 0, 1, 0, -1.5}, // v[0]
                                    PlotCellEdit{PlotKind::Bar3D, 0, 2, 4, 42.0}, // heights[1][1]
                                    PlotCellEdit{PlotKind::Bar3D, 0, 3, 4, -7.0}, // bottoms[1][1]
                                });
            const Bar3DPlot& b = s.bars3d[0];
            check(b.u[2] == 9.5 && b.v[0] == -1.5,
                  "bar3d panel: a cell edit reaches the grid's own coordinates");
            check(b.heights[4] == 42.0 && b.bottoms[4] == -7.0,
                  "bar3d panel: and either of its two matrices, at the row-major index");
            check(b.u[0] == 2.0 && b.v[2] == 8.0 && b.heights[3] == 4.0 && b.bottoms[3] == 0.3,
                  "bar3d panel: leaving every neighbour alone");
        }

        // ---- Footprints (plot scalars).
        {
            RenderSnapshot3D s = snap();
            apply_plot_data_ops(s, {
                                    BarWidthEdit{0, 0.25, -1, PlotKind::Bar3D, 0},
                                    BarWidthEdit{0, 0.75, -1, PlotKind::Bar3D, 1},
                                });
            check(s.bars3d[0].u_width == 0.25 && s.bars3d[0].v_width == 0.75,
                  "bar3d panel: a width edit names which of the two footprints it sets");
        }

        // ---- Structural: a u line with its coordinate and every buffer.
        {
            RenderSnapshot3D s = snap();
            apply_plot_data_ops(s, {
                                    MatrixLineEdit{
                                        MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                                        0, 1, -1, PlotKind::Bar3D
                                    },
                                });
            const Bar3DPlot& b = s.bars3d[0];
            check(b.u.size() == 4 && b.v.size() == 3 && b.heights.size() == 12 &&
                  b.bottoms.size() == 12 && b.opts.hint_labels.size() == 12,
                  "bar3d panel: inserting a u line grows the coordinate and every matrix at once");
            check(b.u[1] == 3.5,
                  "bar3d panel: and the new line sits between its neighbours rather than on one "
                  "of them, which two bars in the same place is what copying would give");
            check(b.heights[3] == 1.0 && b.heights[4] == 2.0 && b.heights[5] == 3.0,
                  "bar3d panel: its values copy the line above, so the picture does not jump");
            check(b.opts.hint_labels[6] == "L3" && b.opts.hint_labels[11] == "L8",
                  "bar3d panel: and the labels re-stride with them rather than sliding one line");
        }

        // A v line, appended (no successor).
        {
            RenderSnapshot3D s = snap();
            apply_plot_data_ops(s, {
                                    MatrixLineEdit{
                                        MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col,
                                        0, 3, -1, PlotKind::Bar3D
                                    },
                                });
            const Bar3DPlot& b = s.bars3d[0];
            check(b.v.size() == 4 && b.v[3] == 11.0,
                  "bar3d panel: a v line appended past the end continues the grid's own spacing");
            check(b.heights.size() == 12 && b.heights[3] == 3.0 && b.heights[7] == 6.0,
                  "bar3d panel: with the matrix re-strided around it, not merely appended to");
        }

        // Removal, never below a 1x1 grid.
        {
            RenderSnapshot3D s = snap();
            apply_plot_data_ops(s, {
                                    MatrixLineEdit{
                                        MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                                        0, 0, -1, PlotKind::Bar3D
                                    },
                                    MatrixLineEdit{
                                        MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                                        0, 0, -1, PlotKind::Bar3D
                                    },
                                });
            check(s.bars3d[0].u.size() == 1 && s.bars3d[0].heights.size() == 3 &&
                  s.bars3d[0].heights[0] == 7.0,
                  "bar3d panel: removing u lines takes the coordinate and its rows together");
            apply_plot_data_ops(s, {
                                    MatrixLineEdit{
                                        MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                                        0, 0, -1, PlotKind::Bar3D
                                    },
                                });
            check(s.bars3d[0].u.size() == 1,
                  "bar3d panel: and the last one is refused, since bar3d() rejects an empty grid");
        }

        // No per-bar bases stays none.
        {
            RenderSnapshot3D s;
            s.bars3d.push_back(bar3d_grid());
            apply_plot_data_ops(s, {
                                    MatrixLineEdit{
                                        MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                                        0, 3, -1, PlotKind::Bar3D
                                    },
                                });
            check(s.bars3d[0].bottoms.empty() && s.bars3d[0].heights.size() == 12,
                  "bar3d panel: a grid with no per-bar bases still has none afterwards");
        }

        // ---- Addressing both ways: plane-addressed ops don't reach the bars, and
        // Bar3D ops don't reach a plane's 2D bar plot.
        {
            RenderSnapshot3D s = two_plane_snapshot();
            s.bars3d.push_back(bar3d_grid());
            apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Bar3D, 0, 2, 0, 99.0, 0}});
            check(s.bars3d[0].heights[0] == 1.0,
                  "bar3d panel: a Bar3D op addressed at a plane does not reach the axes' grid");
            apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Bar3D, 0, 2, 0, 99.0, -1}});
            check(s.bars3d[0].heights[0] == 99.0,
                  "bar3d panel: (and at the axes it does, so that is the address doing it)");
            check(s.planes[0].sheet.bars[0].heights[0] == 1.0 &&
                  s.planes[0].sheet.bars[0].centers[0] == 0.0,
                  "bar3d panel: and neither reaches a plane's 2D bar plot, which shares the index");

            // And a 2D bar's ops aren't diverted to bars3d.
            apply_plot_data_ops(s, {BarWidthEdit{0, 0.125, 0}});
            check(s.planes[0].sheet.bars[0].bar_width == 0.125 &&
                  s.bars3d[0].u_width == 2.0,
                  "bar3d panel: a Bar width edit is a 2D bar's, not a footprint");
        }

        // A Bar3D op against a 2D axes is dropped.
        {
            RenderSnapshot snap2d;
            BarPlot bp;
            bp.centers = std::vector<double>{0.0, 1.0};
            bp.heights = std::vector<double>{5.0, 6.0};
            bp.bar_width = 0.5;
            snap2d.bars.push_back(std::move(bp));
            apply_plot_data_ops(snap2d, {
                                    PlotCellEdit{PlotKind::Bar3D, 0, 1, 1, 77.0},
                                    BarWidthEdit{0, 0.25, -1, PlotKind::Bar3D, 0},
                                    MatrixLineEdit{
                                        MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                                        0, 0, -1, PlotKind::Bar3D
                                    },
                                });
            check(snap2d.bars[0].heights[1] == 6.0 && snap2d.bars[0].bar_width == 0.5,
                  "bar3d panel: a Bar3D op does nothing at all to a 2D axes");
        }

        // ---- Through the whole channel.
        {
            FigureEditBox box;
            box.update3d(2, [](AxesEdit3D& e) {
                e.plot_ops.push_back(PlotCellEdit{PlotKind::Bar3D, 0, 2, 8, 55.0, -1});
            });
            auto drained = box.load_and_clear_journaled();
            check(drained && drained->per_axes3d.size() == 1,
                  "bar3d panel: an axes-addressed 3D edit is not mistaken for an empty one");
            auto j = box.take_journal();
            check(j && j->per_axes.size() == 1 && j->per_axes[0].second.size() == 1 &&
                  plot_op_plane(j->per_axes[0].second[0]) == -1,
                  "bar3d panel: and is journaled with the address it was made at");
            RenderSnapshot3D s = snap();
            apply_plot_data_ops(s, j->per_axes[0].second);
            check(s.bars3d[0].heights[8] == 55.0,
                  "bar3d panel: replaying it lands on the same bar it did the first time");
        }

        // ---- The panel actually clicked: the lane depends on the slot's kind,
        // not the plane index.
        {
            FigureSnapshot fs;
            RenderSnapshot3D r;
            r.bars3d.push_back(bar3d_grid());
            fs.axes.push_back({{1, 1, 1}, std::move(r)});
            fs.generation = fs.data_generation = 1;

            ImGuiContext* ctx = ImGui::CreateContext();
            ImGui::SetCurrentContext(ctx);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(560.0f, 900.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();

            PanelState st;
            FigureEditBox box;
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(520.0f, 860.0f));
                draw_data_panel(fs, box, st);
                ImGui::Render();
            };
            frame();
            frame();
            frame();

            // Click over a grid of positions until something fires (only the
            // grid's +/- buttons emit without typing). Starts below the format
            // controls so a combo popup can't swallow clicks.
            int fired = 0;
            bool lane_ok = false, wrong_lane = false;
            for (float y = 120.0f; y < 840.0f && !fired; y += 5.0f) {
                for (float x = 8.0f; x < 512.0f && !fired; x += 5.0f) {
                    io.MousePos = ImVec2(x, y);
                    io.MouseDown[0] = true;
                    frame();
                    io.MouseDown[0] = false;
                    frame();
                    auto e = box.load_and_clear();
                    if (!e) continue;
                    if (!e->per_axes.empty()) wrong_lane = true;
                    if (e->per_axes3d.empty() || e->per_axes3d[0].second.plot_ops.empty())
                        continue;
                    ++fired;
                    const PlotDataOp& op = e->per_axes3d[0].second.plot_ops[0];
                    lane_ok = e->per_axes.empty() && plot_op_plane(op) == -1
                              && std::holds_alternative<MatrixLineEdit>(op)
                              && std::get<MatrixLineEdit>(op).kind == PlotKind::Bar3D;
                }
            }
            ImGui::DestroyContext(ctx);
            ImGui::SetCurrentContext(nullptr);

            check(fired > 0,
                  "bar3d panel: the drawn table has live controls (a click on one reaches "
                  "the edit box at all)");
            check(!wrong_lane, "bar3d panel: and nothing it builds goes down the 2D lane");
            check(lane_ok,
                  "bar3d panel: an op from it arrives on the 3D lane, addressed at the axes "
                  "and naming the bar3d kind");
        }
    }
} // namespace lt
