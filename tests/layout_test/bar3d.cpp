// bar3d: ingest, the six faces, the painter sort, hints and the Data panel.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {

// ---------------------------------------------------------------------------
// bar3d (v1.0 step 4)
// ---------------------------------------------------------------------------
// The claim that costs the most if it is wrong: the GPU and the CPU project
// alike. Everything drawn inside a 3D box goes through Projector3D::
// clip_matrix() on the GPU and through project_box() on the CPU -- the axis
// frame, the SVG scene, every 3D check in this suite -- so a matrix that is merely
// plausible would put the bars somewhere other than the box that measures
// them, in a way no count and no pixel diff would name.
//
// So the matrix is not trusted, it is checked: pushed through the same divide
// and viewport transform GL applies, and required to land on project_box()'s
// own pixel.
void test_bar3d_clip_matrix() {
    std::printf("\n[3D: the GPU and the CPU project alike]\n");

    using namespace sextant;

    constexpr float W = 240.0f, H = 200.0f;
    const PlotRect frame{ 0.0f, 0.0f, W, H };
    const Transform3D tf{ 0, 10, -5, 5, 100, 200, BoxAspect{ 1.4, 1.0, 0.8 } };

    // What GL does with gl_Position, written out: the perspective divide and
    // then the viewport transform onto the figure's own pixels.
    auto through_matrix = [&](const std::array<float, 16>& m, Vec3 p) {
        double c[4] = { 0, 0, 0, 0 };
        const double v[4] = { p.x, p.y, p.z, 1.0 };
        for (int r = 0; r < 4; ++r)
            for (int j = 0; j < 4; ++j)
                c[r] += static_cast<double>(m[static_cast<std::size_t>(j * 4 + r)]) * v[j];
        const double iw = c[3] != 0.0 ? 1.0 / c[3] : 0.0;
        const double nx = c[0] * iw, ny = c[1] * iw, nz = c[2] * iw;
        return std::array<double, 4>{ (nx + 1.0) * 0.5 * W, (1.0 - ny) * 0.5 * H, nz, c[3] };
    };

    for (int mode = 0; mode < 2; ++mode) {
        Camera3D cam;
        cam.azimuth = -37.0; cam.elevation = 24.0;
        cam.zoom = 1.3;
        cam.target = Vec3{ 0.05, -0.08, 0.02 };
        if (mode) { cam.projection = Projection::Perspective; cam.fov = 70.0; }
        const Projector3D proj(tf, cam, frame, 0.1f);
        const std::array<float, 16> m = proj.clip_matrix(W, H);

        double worst = 0.0;
        bool depth_ordered = true, in_range = true;
        double prev_nz = -2.0, prev_depth = -1e30;
        for (int i = 0; i < 27; ++i) {
            const Vec3 p{ ((i % 3) - 1) * 0.7 * tf.aspect.x * 0.5,
                          (((i / 3) % 3) - 1) * 0.7 * tf.aspect.y * 0.5,
                          ((i / 9) - 1) * 0.7 * tf.aspect.z * 0.5 };
            const Px3 cpu = proj.project_box(p);
            const auto gpu = through_matrix(m, p);
            worst = std::max(worst, std::max(std::fabs(gpu[0] - cpu.x),
                                             std::fabs(gpu[1] - cpu.y)));
            if (gpu[2] < -1.0 || gpu[2] > 1.0) in_range = false;
            // Depth must order the same way project_box()'s does, or the depth
            // buffer would resolve a scene the annotation does not describe.
            if (i && (cpu.depth > prev_depth) != (gpu[2] > prev_nz)) depth_ordered = false;
            prev_nz = gpu[2]; prev_depth = cpu.depth;
        }
        const char* what = mode ? "perspective" : "orthographic";
        check(worst < 0.02,
              mode ? "bar3d: the matrix lands on project_box()'s pixel under perspective"
                   : "bar3d: the matrix lands on project_box()'s pixel under orthographic");
        check(in_range,
              mode ? "bar3d: and inside the depth range, so nothing in the box is clipped away (perspective)"
                   : "bar3d: and inside the depth range, so nothing in the box is clipped away (orthographic)");
        check(depth_ordered,
              mode ? "bar3d: ordering depth the same way the CPU does (perspective)"
                   : "bar3d: ordering depth the same way the CPU does (orthographic)");
        std::printf("  %-13s worst disagreement %.4f px over 27 points\n", what, worst);
    }

    // The data -> box half, which the matrix deliberately leaves to the
    // shader. Checked against Transform3D itself rather than against the
    // matrix, so the two halves cannot agree with each other and both be wrong.
    Camera3D cam;
    const Projector3D proj(tf, cam, frame, 0.1f);
    const Vec3 anchor{ 5.0, 0.0, 150.0 };
    float scale[3], offset[3];
    proj.box_affine(anchor, scale, offset);
    bool affine_ok = true;
    for (const Vec3& d : { Vec3{ 0, -5, 100 }, Vec3{ 10, 5, 200 }, Vec3{ 3, 1, 175 } }) {
        const Vec3 want = tf.to_box(d.x, d.y, d.z);
        const double got[3] = {
            scale[0] * (d.x - anchor.x) + offset[0],
            scale[1] * (d.y - anchor.y) + offset[1],
            scale[2] * (d.z - anchor.z) + offset[2] };
        if (std::fabs(got[0] - want.x) > 1e-5 || std::fabs(got[1] - want.y) > 1e-5 ||
            std::fabs(got[2] - want.z) > 1e-5) affine_ok = false;
    }
    check(affine_ok, "bar3d: and the data->box step the shader applies first is Transform3D exactly");
}

// The plot object, from the API down. Ingest resolves what the rest of the
// pipeline is written against -- a footprint in data units and a base per bar
// -- so nothing downstream ever sees a fraction, the same bargain
// BarPlot::bar_width struck.
void test_bar3d_ingest() {
    std::printf("\n[3D: bar3d ingest and auto-scale]\n");

    using namespace sextant;

    auto threw = [](auto&& fn) {
        try { fn(); return false; } catch (const std::invalid_argument&) { return true; }
    };

    const std::vector<double> u{ 0.0, 2.0, 4.0 };
    const std::vector<double> v{ 0.0, 1.0 };
    const std::vector<double> h{ 1, 2, 3, 4, 5, 6 };

    auto fig = Figure::create({ .width = 300, .height = 240 });
    auto ax  = fig->add_subplot3d(1, 1, 1);
    check(threw([&]{ ax->bar3d(PlaneOrientation::XY, u, v, { h.data(), 5 }); }),
          "bar3d: heights that are not |u| x |v| throw at ingest");
    check(threw([&]{ ax->bar3d(PlaneOrientation::XY, {}, v, {}); }),
          "bar3d: an empty grid throws");
    const std::vector<double> bad{ 1, 2, 3, std::numeric_limits<double>::quiet_NaN(), 5, 6 };
    check(threw([&]{ ax->bar3d(PlaneOrientation::XY, u, v, bad); }),
          "bar3d: a non-finite height throws, once, rather than reaching the vertex buffer");
    check(threw([&]{ ax->bar3d(PlaneOrientation::XY, u, v, h, std::span<const double>(h).first(3)); }),
          "bar3d: and a bottoms vector that does not match heights");

    // Footprints, and the three orientations, checked through auto_scale3d --
    // which is where the resolved widths become visible from outside.
    Bar3DPlot b;
    b.u = u; b.v = v; b.heights = h;
    b.u_width = 2.0 * 0.8;   // spacing x width, as ingest resolves it
    b.v_width = 1.0 * 0.8;
    b.opts.bottom = 0.0;

    const DataBounds3D xy = auto_scale3d({ b }, {}, {}, {}, {}, {}, 0.0);
    check(xy.xmin == -0.8 && xy.xmax == 4.8,
          "bar3d: auto-scale spans the bars' footprint, not just their centres");
    check(xy.ymin == -0.4 && xy.ymax == 1.4, "bar3d: on the other grid axis too");
    check(xy.zmin == 0.0 && xy.zmax == 6.0,
          "bar3d: and from base to tallest tip along the axis they stand on");

    Bar3DPlot yz = b; yz.orient = PlaneOrientation::YZ;
    const DataBounds3D r2 = auto_scale3d({ yz }, {}, {}, {}, {}, {}, 0.0);
    check(r2.ymin == xy.xmin && r2.zmin == xy.ymin && r2.xmax == xy.zmax,
          "bar3d: the orientation rotates which axis is which, and nothing else");

    // A non-zero base moves the whole span rather than stretching it from the
    // origin, which is the one place this deliberately differs from the 2D bar.
    Bar3DPlot raised = b;
    raised.opts.bottom = 100.0;
    const DataBounds3D r3 = auto_scale3d({ raised }, {}, {}, {}, {}, {}, 0.0);
    check(r3.zmin == 100.0 && r3.zmax == 106.0,
          "bar3d: a raised base is where the bars stand, not a gap to the origin");

    // Per-bar bases, and a negative height hanging below its base.
    Bar3DPlot hung = b;
    hung.heights = std::vector<double>{ -1, 2, 3, 4, 5, 6 };
    const DataBounds3D r4 = auto_scale3d({ hung }, {}, {}, {}, {}, {}, 0.0);
    check(r4.zmin == -1.0 && r4.zmax == 6.0,
          "bar3d: a negative height hangs below the base rather than inverting the box");

    // End to end: the public call resolves the footprint from the spacing.
    auto ax2 = Figure::create({ .width = 300, .height = 240 })->add_subplot3d(1, 1, 1);
    ax2->bar3d(PlaneOrientation::XY, u, v, h, { .width = 0.5f, .depth = 1.0f });
    ax2->set_zlim(0.0, 10.0);
    check(true, "bar3d: a well-formed call is accepted");   // the throw checks are above
}

// The geometry, and the two rules that are invisible when wrong: which faces
// are drawn, and in what order.
void test_bar3d_faces() {
    std::printf("\n[3D: bar3d faces and the painter sort]\n");

    using namespace sextant;

    Bar3DPlot b;
    b.u = std::vector<double>{ 1.0 };
    b.v = std::vector<double>{ 2.0 };
    b.heights = std::vector<double>{ 4.0 };
    b.u_width = 1.0; b.v_width = 2.0;
    b.opts.bottom = 1.0;
    const Transform3D tf{ 0, 4, 0, 6, 0, 8, BoxAspect{} };

    Bar3DFace f[6];
    bar3d_faces(b, 0, tf, f);

    // The box the corners describe, read back as its own bounding box.
    double lo[3] = { 1e30, 1e30, 1e30 }, hi[3] = { -1e30, -1e30, -1e30 };
    for (const Bar3DFace& fc : f)
        for (const Vec3& p : fc.p) {
            const double c[3] = { p.x, p.y, p.z };
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], c[a]); hi[a] = std::max(hi[a], c[a]);
            }
        }
    check(lo[0] == 0.5 && hi[0] == 1.5, "bar3d: the footprint is centred on the grid coordinate");
    check(lo[1] == 1.0 && hi[1] == 3.0, "bar3d: with its own width on each grid axis");
    check(lo[2] == 1.0 && hi[2] == 5.0, "bar3d: and the bar runs from its base to base + height");

    // Shading is fixed in *box* space, so the three faces a camera sees are
    // three different brightnesses and stay so however the box is orbited --
    // which is what lets the shade be baked into a buffer with no camera in
    // its key.
    check(f[5].shade > f[1].shade && f[1].shade > f[3].shade,
          "bar3d: top, +x and +y come out at three different brightnesses");
    check(f[4].shade < f[5].shade,
          "bar3d: and the underside is the darkest of the pair it belongs to");
    Bar3DPlot flat = b;
    flat.opts.shading = 0.0f;
    bar3d_faces(flat, 0, tf, f);
    check(f[0].shade == 1.0f && f[5].shade == 1.0f,
          "bar3d: shading 0 leaves every face the flat colour");

    // A reversed limit mirrors that axis, so the face that was lit is now the
    // one facing away. Cosmetic, but wrong in a way that reads as a lighting
    // bug rather than a limits one, so it is taken from the transform.
    const Transform3D mirrored{ 4, 0, 0, 6, 0, 8, BoxAspect{} };
    Bar3DFace g[6];
    bar3d_faces(b, 0, tf, f);
    bar3d_faces(b, 0, mirrored, g);
    check(f[0].shade == g[1].shade && f[1].shade == g[0].shade,
          "bar3d: a reversed limit swaps which of that axis's faces is lit");
    check(f[4].shade == g[4].shade,
          "bar3d: and leaves the axes it did not reverse alone");

    // ---- The painter sort ------------------------------------------------
    Bar3DPlot grid;
    grid.u = std::vector<double>{ 0.0, 1.0, 2.0 };
    grid.v = std::vector<double>{ 0.0, 1.0, 2.0 };
    grid.heights = std::vector<double>(9, 1.0);
    grid.u_width = grid.v_width = 0.8;
    const Transform3D gt{ -0.5, 2.5, -0.5, 2.5, 0, 1.5, BoxAspect{} };
    const PlotRect frame{ 0.0f, 0.0f, 300.0f, 240.0f };

    for (int mode = 0; mode < 2; ++mode) {
        Camera3D cam;
        if (mode) { cam.projection = Projection::Perspective; cam.fov = 60.0; }
        const Projector3D proj(gt, cam, frame, 0.1f);
        const std::vector<Bar3DPolygon> polys = plan_bars3d(proj, { grid });

        check(polys.size() == 9 * 3,
              mode ? "bar3d: three faces per bar under perspective -- the other three face away"
                   : "bar3d: three faces per bar, for any camera outside them");

        // A bar's three faces come out together rather than interleaved with
        // another bar's, which is what makes the pairwise order in
        // test_bar3d_painter_order() a statement about bars at all. (The
        // order *between* bars is checked there, against the separating-plane
        // rule; this grid is uniform and so cannot tell a right ordering from
        // a plausible one.)
        bool grouped = true;
        for (std::size_t i = 3; i < polys.size(); i += 3)
            if (polys[i].bar == polys[i - 1].bar) grouped = false;
        for (std::size_t i = 0; i + 2 < polys.size(); i += 3)
            if (polys[i].bar != polys[i + 1].bar || polys[i].bar != polys[i + 2].bar)
                grouped = false;
        check(grouped, mode ? "bar3d: a bar's faces are emitted together (perspective)"
                            : "bar3d: a bar's faces are emitted together, not interleaved");

        // Which three, asked a second way: a drawn face's centroid must be
        // nearer than the centroid of the opposite face of the same bar. That
        // is a fact about depth rather than about the sign convention under
        // test -- the same trap the back-pane rule set, where inverting the
        // sign still produced a picture that looked like a box.
        Bar3DFace ff[6];
        bar3d_faces(grid, 4, gt, ff);   // the middle bar
        int drawn = 0;
        bool nearer = true;
        for (int a = 0; a < 3; ++a) {
            for (int e = 0; e < 2; ++e) {
                const Bar3DFace& face = ff[a * 2 + e];
                const Bar3DFace& opp  = ff[a * 2 + (1 - e)];
                auto centroid = [&](const Bar3DFace& x) {
                    Vec3 c{};
                    for (const Vec3& p : x.p) c = c + gt.to_box(p.x, p.y, p.z);
                    return c * 0.25;
                };
                const bool front = proj.faces_camera(centroid(face), face.normal);
                if (!front) continue;
                ++drawn;
                if (proj.project_box(centroid(face)).depth >=
                    proj.project_box(centroid(opp)).depth) nearer = false;
            }
        }
        check(drawn == 3 && nearer,
              mode ? "bar3d: and each is the nearer of its pair, by depth (perspective)"
                   : "bar3d: and each is the nearer of its pair, by depth");
    }

    // Edge width is the world-space half of the stroke fork: a pixel width at
    // the box centre, so under perspective a bar further from the eye gets a
    // proportionally thinner stroke, while under orthographic every bar gets
    // exactly the width that was asked for.
    Bar3DPlot edged = grid;
    edged.opts.edges = true;
    edged.opts.edge_linewidth = 2.0f;

    Camera3D ortho_cam;
    const std::vector<Bar3DPolygon> op =
        plan_bars3d(Projector3D(gt, ortho_cam, frame, 0.1f), { edged });
    bool all_two = !op.empty();
    for (const Bar3DPolygon& p : op) if (std::fabs(p.stroke_width - 2.0f) > 1e-4f) all_two = false;
    check(all_two, "bar3d: under orthographic every bar's outline is exactly the width asked for");

    Camera3D pc;
    pc.projection = Projection::Perspective;
    pc.fov = 90.0;
    const std::vector<Bar3DPolygon> pp = plan_bars3d(Projector3D(gt, pc, frame, 0.1f), { edged });
    float thinnest = 1e9f, thickest = 0.0f;
    for (const Bar3DPolygon& p : pp) {
        thinnest = std::min(thinnest, p.stroke_width);
        thickest = std::max(thickest, p.stroke_width);
    }
    check(thickest > thinnest * 1.05f,
          "bar3d: under perspective it thins with distance -- it is a width in the scene");
    check(thickest > 2.0f && thinnest < 2.0f,
          "bar3d: with the width asked for landing at the box centre, near bars over and far under");

    // Which is exactly what the axis frame must *not* do (spec_3d.md §4): the
    // two kinds of stroke are held apart here, in one check, because the whole
    // fork exists to keep them apart.
    RenderSnapshot3D snap;
    snap.camera = pc;
    snap.xticks_override = std::vector<Tick>{ {0.0,"0"}, {1.0,"1"} };
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
                      std::hypot(c[2] - c[0], c[3] - c[1])) > 1e-3) marks_equal = false;
    }
    check(marks_equal,
          "bar3d: while under the same camera every tick mark is still the same pixel length");
}

// The painter's order, checked against the physics rather than against the
// sort key -- because the obvious sort key is wrong and passed every other
// check in this file.
//
// What went wrong: faces were ordered by their own centroid depth, which is
// right for equal bars and wrong as soon as heights differ. A short bar in
// front has a low, and so far, top face; a tall bar behind it has a high, and
// so near, front face. The tall one sorted last and painted over the short one
// standing in front of it -- which in the SVG reads as bars missing their
// front faces. No count, no coordinate check and no orthographic-camera
// comparison noticed, because every face was present and in the right place.
//
// The claim asserted here is the separating-plane one, derived from the eye
// and the grid and not from any sort key: two bars on a grid are separated by
// an axis-aligned plane, and the bar on the far side of that plane from the
// eye cannot occlude the other, so it must be emitted first.
void test_bar3d_painter_order() {
    std::printf("\n[3D: bar3d painter order]\n");

    using namespace sextant;

    // Heights chosen to make the failure certain rather than likely: a low
    // row in front of a high one, across the whole grid.
    constexpr std::size_t NU = 5, NV = 5;
    Bar3DPlot b;
    std::vector<double> u(NU), v(NV), h(NU * NV);
    for (std::size_t i = 0; i < NU; ++i) u[i] = static_cast<double>(i);
    for (std::size_t j = 0; j < NV; ++j) v[j] = static_cast<double>(j);
    for (std::size_t i = 0; i < NU; ++i)
        for (std::size_t j = 0; j < NV; ++j)
            h[i * NV + j] = ((i + j) % 2 == 0) ? 0.4 : 6.0;
    b.u = u; b.v = v; b.heights = h;
    b.u_width = b.v_width = 0.85;

    const Transform3D tf{ -0.6, 4.6, -0.6, 4.6, 0.0, 6.5, BoxAspect{} };
    const PlotRect frame{ 0.0f, 0.0f, 420.0f, 340.0f };
    const Axis3Map m = axis_map(b.orient);

    for (int mode = 0; mode < 2; ++mode) {
        Camera3D cam;                       // the default view, -60 / 30
        if (mode) { cam.projection = Projection::Perspective; cam.fov = 75.0; }
        const Projector3D proj(tf, cam, frame, 0.12f);
        const std::vector<Bar3DPolygon> polys = plan_bars3d(proj, { b });

        // Where the eye is on each box axis. Under perspective it has a
        // position; under orthographic it is at infinity along the view
        // direction, which for a comparison of sides is a point far enough out.
        const Vec3 eye = proj.has_eye_point() ? proj.eye_point()
                                              : proj.eye_dir() * 1e4;
        auto axis_of = [](Vec3 p, int a) { return a == 0 ? p.x : a == 1 ? p.y : p.z; };
        auto cell_centre = [&](std::size_t k) {
            const std::size_t i = k / NV, j = k % NV;
            Vec3 d{};
            double c[3] = { 0, 0, 0 };
            c[m.u] = u[i];
            c[m.v] = v[j];
            c[m.h] = (b.h_lo(k) + b.h_hi(k)) * 0.5;
            d = { c[0], c[1], c[2] };
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
        check(all_drawn, mode ? "bar3d order: every bar contributes faces (perspective)"
                              : "bar3d order: every bar contributes faces");

        // The pairwise constraint, for every pair.
        int violations = 0;
        for (std::size_t a = 0; a < NU * NV; ++a) {
            for (std::size_t c = a + 1; c < NU * NV; ++c) {
                const Vec3 ca = cell_centre(a), cc = cell_centre(c);
                // The separating plane: the u grid line between the two
                // columns, or -- if they share a column -- the v one.
                int axis = (a / NV != c / NV) ? m.u : m.v;
                const double pa = axis_of(ca, axis), pc = axis_of(cc, axis);
                if (pa == pc) continue;                 // same cell on this axis
                const double plane = (pa + pc) * 0.5;
                const double eye_side = axis_of(eye, axis) - plane;
                // The bar on the opposite side of the plane from the eye is
                // behind, and must be emitted first: nothing on the far side
                // of a separating plane can occlude anything on the near side.
                const bool a_behind = (pa - plane) * eye_side < 0.0;
                const std::size_t behind = a_behind ? a : c;
                const std::size_t front  = a_behind ? c : a;
                if (last[behind] > first[front]) ++violations;
            }
        }
        check(violations == 0,
              mode ? "bar3d order: no bar is drawn over one that is in front of it (perspective)"
                   : "bar3d order: no bar is drawn over one that is in front of it");
        if (violations)
            std::printf("    %d of %d pairs out of order\n",
                        violations, static_cast<int>(NU * NV * (NU * NV - 1) / 2));

        // Translucency is the same order plus the faces that were being
        // dropped: all six per bar, the hidden three first, since that is what
        // showing through means. The bar order itself must not change -- alpha
        // says what is drawn, not where it is.
        Bar3DPlot glassy = b;
        glassy.opts.alpha = 0.45f;
        const std::vector<Bar3DPolygon> clear_ = plan_bars3d(proj, { glassy });
        check(clear_.size() == polys.size() * 2,
              mode ? "bar3d alpha: a translucent bar draws all six faces (perspective)"
                   : "bar3d alpha: a translucent bar draws all six faces, not just the three seen");
        bool same_bar_order = clear_.size() == polys.size() * 2;
        for (std::size_t p = 0; same_bar_order && p < polys.size(); ++p)
            if (clear_[p * 2].bar != polys[p].bar) same_bar_order = false;
        check(same_bar_order,
              mode ? "bar3d alpha: in the same bar order (perspective)"
                   : "bar3d alpha: in the same bar order -- alpha changes what is drawn, not where");

        // Within a bar, the faces turned away come first: a translucent bar
        // shows its own back through its front, not the other way round.
        bool back_first = true;
        for (std::size_t p = 0; p + 1 < clear_.size(); p += 6) {
            for (int f = 0; f < 6; ++f) {
                const bool front = f >= 3;
                // Asked of the projector, not of the emission order: a face is
                // turned toward the camera exactly when its own centroid is
                // nearer than the opposite face's, which is the same
                // independently-derived quantity the back-face rule uses.
                const float d = clear_[p + static_cast<std::size_t>(f)].depth;
                const float opp = clear_[p + static_cast<std::size_t>((f + 3) % 6)].depth;
                if (front != (d < opp)) back_first = false;
            }
        }
        check(back_first,
              mode ? "bar3d alpha: hidden faces before visible ones, within each bar (perspective)"
                   : "bar3d alpha: hidden faces before visible ones, within each bar");

        bool carries_alpha = !clear_.empty();
        for (const Bar3DPolygon& p : clear_)
            if (std::fabs(p.fill.a - 0.45f) > 1e-6f) carries_alpha = false;
        check(carries_alpha, mode ? "bar3d alpha: and every face carries it (perspective)"
                                  : "bar3d alpha: and every face carries it into the output");

        // On a translucent bar all twelve box edges are drawn as their own
        // strokes rather than as an outline on the faces. Both follow from
        // there being nothing left to hide behind: a solid bar occludes six of
        // its own edges, and stroking its three visible faces draws exactly
        // the nine that show; a translucent one occludes none, and stroking
        // six faces would draw each edge twice -- invisible while the stroke
        // was opaque, and twice as dark the moment it is not.
        //
        // The outline's *opacity*, though, is its own: an edge faded to match
        // the glass it sits on stops saying where one box ends and the next
        // begins, which is the whole job left to it once the faces are
        // see-through. So `edge_alpha` is a separate field and the face's
        // alpha does not enter it -- checked here with the two set to
        // different values, so neither could be standing in for the other.
        Bar3DPlot outlined = glassy;
        outlined.opts.edges = true;
        outlined.opts.edgecolor = { 0.0f, 0.0f, 0.0f, 0.8f };
        outlined.opts.edge_alpha = 0.5f;
        outlined.opts.edge_linewidth = 1.5f;
        const std::vector<Bar3DPolygon> caged = plan_bars3d(proj, { outlined });
        std::size_t faces_n = 0, lines_n = 0;
        bool edge_alpha_ok = true, faces_unstroked = true;
        for (const Bar3DPolygon& p : caged) {
            (p.filled ? faces_n : lines_n)++;
            if (p.filled && p.stroke_width != 0.0f) faces_unstroked = false;
            if (!p.filled && std::fabs(p.stroke.a - 0.8f * 0.5f) > 1e-6f)
                edge_alpha_ok = false;
        }
        check(faces_n == clear_.size() && lines_n == clear_.size() / 6 * 12,
              mode ? "bar3d alpha: a translucent outline is twelve edges per bar (perspective)"
                   : "bar3d alpha: a translucent outline is all twelve edges of each bar, once each");
        check(faces_unstroked,
              mode ? "bar3d alpha: with the faces themselves left unstroked (perspective)"
                   : "bar3d alpha: with the faces themselves left unstroked, so no edge is drawn twice");
        check(edge_alpha_ok,
              mode ? "bar3d alpha: and the outline takes edgecolor.a * edge_alpha (perspective)"
                   : "bar3d alpha: and the outline takes edgecolor.a * edge_alpha, not the face's alpha");

        // The default is what matters most here: an outline stays solid over
        // glass unless it is asked not to. Sharing the face's alpha is what
        // this replaced, and it made translucent bars unreadable.
        Bar3DPlot default_edge = glassy;
        default_edge.opts.edges = true;
        default_edge.opts.edge_linewidth = 1.5f;
        bool solid_outline = false, any_line = false;
        for (const Bar3DPolygon& p : plan_bars3d(proj, { default_edge }))
            if (!p.filled) { any_line = true; solid_outline = p.stroke.a == 1.0f; }
        check(any_line && solid_outline,
              mode ? "bar3d alpha: and by default it stays fully opaque over the glass (perspective)"
                   : "bar3d alpha: and by default it stays fully opaque over the glass");

        // The opaque case keeps the stroke on its faces, which is where the
        // byte-identity of every existing output comes from.
        Bar3DPlot solid = b;
        solid.opts.edges = true;
        solid.opts.edgecolor = { 0.0f, 0.0f, 0.0f, 0.8f };
        const std::vector<Bar3DPolygon> solid_p = plan_bars3d(proj, { solid });
        bool all_faces = !solid_p.empty();
        for (const Bar3DPolygon& p : solid_p)
            if (!p.filled || p.stroke_width <= 0.0f || std::fabs(p.stroke.a - 0.8f) > 1e-6f)
                all_faces = false;
        check(all_faces,
              mode ? "bar3d alpha: an opaque bar still strokes its visible faces (perspective)"
                   : "bar3d alpha: while an opaque bar still strokes its three visible faces");

        // ...and the ordering this replaced would have failed it: on this grid
        // the centroid-depth order and the correct one genuinely disagree, so
        // the check above is not passing by accident on data that cannot tell
        // them apart.
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

// Hover hints over a bar3d grid -- what step 6b deferred and 6c closes. The
// question is the same one planes answered: a pixel names a line in the scene,
// so the checks are that the line meets the bar the picture puts there, that
// the nearest surface wins whether it is a bar or a plane, and that the
// tooltip says which bar in the reader's own coordinates.
void test_bar3d_hints() {
    std::printf("\n[3D: hover hints over bar3d]\n");

    using namespace sextant;

    Transform3D tf;
    tf.xmin = 0.0; tf.xmax = 10.0;
    tf.ymin = 0.0; tf.ymax = 10.0;
    tf.zmin = 0.0; tf.zmax = 10.0;

    const PlotRect frame{ 20.0f, 15.0f, 400.0f, 320.0f };
    Camera3D cam;
    cam.azimuth = -55.0; cam.elevation = 24.0;

    const Bar3DPlot grid = bar3d_grid();

    for (int mode = 0; mode < 2; ++mode) {
        cam.projection = mode ? Projection::Perspective : Projection::Orthographic;
        const char* what = mode ? "perspective" : "orthographic";
        const Projector3D proj(tf, cam, frame, 0.1f);

        // ---- Every face of every bar: the ray through a face's own projected
        // centre must meet that bar. This is the whole claim of box_ray_hit()
        // stated 54 times over, and it is exact -- a face centre is on the box.
        int faces = 0, met = 0;
        for (std::size_t k = 0; k < grid.count(); ++k) {
            Bar3DFace f[6];
            bar3d_faces(grid, k, tf, f);
            for (const Bar3DFace& face : f) {
                Vec3 c{ 0.0, 0.0, 0.0 };
                for (const Vec3& p : face.p) c = c + p * 0.25;
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

        // ---- And misses what it should. A pixel far outside the box's
        // silhouette meets no bar at all; without this the check above would
        // pass for a test that always said yes.
        int stray = 0;
        for (std::size_t k = 0; k < grid.count(); ++k) {
            float depth = 0.0f;
            if (bar3d_ray_hit(grid, k, proj, frame.x + 1.0f, frame.y + 1.0f, depth)) ++stray;
        }
        check(stray == 0,
              std::string("bar3d hints: and a pixel in the frame's corner meets none of them (")
                  + what + ")");

        // ---- A bar's height is part of the box. The pixel of a point well
        // above a short bar's top must miss it, or the hit test would be a
        // test of the footprint alone -- which would answer for every bar in
        // the column under the cursor.
        {
            Bar3DPlot low = grid;
            low.heights = std::vector<double>(9, 0.5);
            const Px3 above = proj.project(low.u[1], low.v[1], 6.0);
            float d0 = 0.0f, d1 = 0.0f;
            const bool over  = bar3d_ray_hit(low, low.index_of(1, 1), proj, above.x, above.y, d0);
            const Px3 on     = proj.project(low.u[1], low.v[1], 0.25);
            const bool onbar = bar3d_ray_hit(low, low.index_of(1, 1), proj, on.x, on.y, d1);
            check(!over && onbar,
                  std::string("bar3d hints: a pixel above a short bar misses it while one on "
                              "it does not (") + what + ")");
        }
    }

    // ---- The hint end to end, orthographic so the geometry is easy to state.
    cam.projection = Projection::Orthographic;
    const Projector3D proj(tf, cam, frame, 0.1f);

    RenderSnapshot3D s;
    s.bars3d.push_back(grid);
    // A label per bar, in the heights' own row-major order, so a tooltip that
    // found the wrong bar quotes the wrong label.
    for (std::size_t k = 0; k < grid.count(); ++k)
        s.bars3d[0].opts.hint_labels.push_back("bar" + std::to_string(k));

    // Which bar of the grid a given pixel's nearest hit is, and how many bars
    // that pixel runs through at all -- the independent answer every ordering
    // check below is stated against, since which bar is in front is the
    // camera's business and a hard-coded one would be a check about this
    // azimuth rather than about the search.
    auto nearest_bar = [&](const Bar3DPlot& b, float px, float py, int& hits) {
        std::size_t nearest = 0;
        float best = 0.0f;
        hits = 0;
        for (std::size_t k = 0; k < b.count(); ++k) {
            float depth = 0.0f;
            if (!bar3d_ray_hit(b, k, proj, px, py, depth)) continue;
            if (hits == 0 || depth < best) { best = depth; nearest = k; }
            ++hits;
        }
        return nearest;
    };

    {
        // The corner bar at the near end of both grid axes: +x and -y are
        // toward this eye, so nothing of the grid stands in front of its top.
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

    // A base worth reporting is reported; one that is zero is not, which is
    // the pair that makes the omission a rule rather than an accident.
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

    // ---- Nearest wins. The answer is asked of the depths rather than
    // assumed, exactly as the plane pair is: which bar is in front depends on
    // the camera, and a check that hard-codes one would be a check about this
    // azimuth.
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

    // ---- Bars and planes are ordered against each other, in one quantity.
    // A plane behind the bars must not answer where a bar covers it, and the
    // same plane in front must.
    {
        const std::size_t k = grid.index_of(1, 1);
        const Px3 at = proj.project(grid.u[1], grid.v[1], grid.h_hi(k));

        auto with_plane = [&](double offset) {
            RenderSnapshot3D r = s;
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            pl.offset = offset;
            // The point is placed where this very pixel meets the plane, so it
            // is exactly under the cursor and only the depth can decide.
            double u = 0.0, v = 0.0; float d = 0.0f;
            plane_ray_hit(proj, PlaneOrientation::XY, offset, at.x, at.y, u, v, d);
            ScatterPlot sp;
            sp.x = std::vector<double>{ u };
            sp.y = std::vector<double>{ v };
            sp.opts.hint_labels = { "sheet" };
            pl.sheet.scatters.push_back(std::move(sp));
            r.planes.push_back(std::move(pl));
            return r;
        };

        // The bars stand from 0 up to at most 9, so a plane at 9.5 is above
        // every one of them and a plane at 0.05 is under the grid.
        const RenderSnapshot3D over  = with_plane(9.5);
        const RenderSnapshot3D under = with_plane(0.05);
        float d_over = 0.0f, d_under = 0.0f, d_bar = 0.0f;
        double du = 0.0, dv = 0.0;
        plane_ray_hit(proj, PlaneOrientation::XY, 9.5,  at.x, at.y, du, dv, d_over);
        plane_ray_hit(proj, PlaneOrientation::XY, 0.05, at.x, at.y, du, dv, d_under);
        bar3d_ray_hit(grid, k, proj, at.x, at.y, d_bar);
        check(d_over < d_bar && d_bar < d_under,
              "bar3d hints: (the plane above really is nearer than the bar, and the one "
              "below further -- so the two checks that follow are about the order)");

        auto ho = find_hint3d(over,  proj, at.x, at.y);
        auto hu = find_hint3d(under, proj, at.x, at.y);
        check(ho && ho->text.find("sheet") != std::string::npos,
              "bar3d hints: a plane in front of a bar answers over it");
        check(hu && hu->text.find("bar") != std::string::npos &&
              hu->text.find("sheet") == std::string::npos,
              "bar3d hints: and one behind it does not");
    }

    // ---- A reversed limit mirrors its axis, which swaps which face of a bar
    // is the near one. box_ray_hit() orders the slab itself so that nothing
    // above it has to; without that the whole grid would be unhittable.
    {
        Transform3D rev = tf;
        rev.xmin = 10.0; rev.xmax = 0.0;
        const Projector3D rp(rev, cam, frame, 0.1f);
        const std::size_t k = grid.index_of(2, 0);
        const Px3 top = rp.project(grid.u[2], grid.v[0], grid.h_hi(k));
        float depth = 0.0f;
        check(bar3d_ray_hit(grid, k, rp, top.x, top.y, depth),
              "bar3d hints: a bar on a reversed axis is still hit");
    }

    // ---- An empty grid contributes nothing rather than a hit at the origin.
    {
        RenderSnapshot3D empty;
        empty.bars3d.push_back(Bar3DPlot{});
        const Px3 mid = proj.project(5.0, 5.0, 5.0);
        check(!find_hint3d(empty, proj, mid.x, mid.y).has_value(),
              "bar3d hints: a grid with no bars in it is not hinted");
    }
}

// The Data panel's bar3d half: the table shape, and the ops it builds. A
// bar3d grid is the one plot object addressed at a 3D *axes* rather than at a
// plane, so the address it rides is the one every other check here is about.
void test_bar3d_data_panel() {
    std::printf("\n[3D: the Data panel's bar3d grid]\n");

    using namespace sextant;

    auto snap = [] {
        RenderSnapshot3D s;
        Bar3DPlot b = bar3d_grid();
        b.bottoms = std::vector<double>{ 0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8 };
        for (std::size_t k = 0; k < 9; ++k) b.opts.hint_labels.push_back("L" + std::to_string(k));
        s.bars3d.push_back(std::move(b));
        return s;
    };

    // ---- The four columns of a cell edit: two coordinate vectors and two
    // row-major matrices. Each is checked against the others staying put, so
    // a column that wrote the wrong array fails rather than passes twice.
    {
        RenderSnapshot3D s = snap();
        apply_plot_data_ops(s, {
            PlotCellEdit{ PlotKind::Bar3D, 0, 0, 2, 9.5 },     // u[2]
            PlotCellEdit{ PlotKind::Bar3D, 0, 1, 0, -1.5 },    // v[0]
            PlotCellEdit{ PlotKind::Bar3D, 0, 2, 4, 42.0 },    // heights[1][1]
            PlotCellEdit{ PlotKind::Bar3D, 0, 3, 4, -7.0 },    // bottoms[1][1]
        });
        const Bar3DPlot& b = s.bars3d[0];
        check(b.u[2] == 9.5 && b.v[0] == -1.5,
              "bar3d panel: a cell edit reaches the grid's own coordinates");
        check(b.heights[4] == 42.0 && b.bottoms[4] == -7.0,
              "bar3d panel: and either of its two matrices, at the row-major index");
        check(b.u[0] == 2.0 && b.v[2] == 8.0 && b.heights[3] == 4.0 && b.bottoms[3] == 0.3,
              "bar3d panel: leaving every neighbour alone");
    }

    // ---- The footprints, which are plot scalars rather than cells -- the
    // same reason BarPlot::bar_width has its own op.
    {
        RenderSnapshot3D s = snap();
        apply_plot_data_ops(s, {
            BarWidthEdit{ 0, 0.25, -1, PlotKind::Bar3D, 0 },
            BarWidthEdit{ 0, 0.75, -1, PlotKind::Bar3D, 1 },
        });
        check(s.bars3d[0].u_width == 0.25 && s.bars3d[0].v_width == 0.75,
              "bar3d panel: a width edit names which of the two footprints it sets");
    }

    // ---- Structural: a whole u line, coordinate and every buffer that
    // strides against it, in one step.
    {
        RenderSnapshot3D s = snap();
        apply_plot_data_ops(s, {
            MatrixLineEdit{ MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                            0, 1, -1, PlotKind::Bar3D },
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

    // A v line is the other stride, and appending is the case with no
    // successor to sit between.
    {
        RenderSnapshot3D s = snap();
        apply_plot_data_ops(s, {
            MatrixLineEdit{ MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col,
                            0, 3, -1, PlotKind::Bar3D },
        });
        const Bar3DPlot& b = s.bars3d[0];
        check(b.v.size() == 4 && b.v[3] == 11.0,
              "bar3d panel: a v line appended past the end continues the grid's own spacing");
        check(b.heights.size() == 12 && b.heights[3] == 3.0 && b.heights[7] == 6.0,
              "bar3d panel: with the matrix re-strided around it, not merely appended to");
    }

    // Removal, and the floor under it: Axes3D::bar3d() refuses an empty grid,
    // so the panel must not be able to manufacture one.
    {
        RenderSnapshot3D s = snap();
        apply_plot_data_ops(s, {
            MatrixLineEdit{ MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                            0, 0, -1, PlotKind::Bar3D },
            MatrixLineEdit{ MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                            0, 0, -1, PlotKind::Bar3D },
        });
        check(s.bars3d[0].u.size() == 1 && s.bars3d[0].heights.size() == 3 &&
              s.bars3d[0].heights[0] == 7.0,
              "bar3d panel: removing u lines takes the coordinate and its rows together");
        apply_plot_data_ops(s, {
            MatrixLineEdit{ MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                            0, 0, -1, PlotKind::Bar3D },
        });
        check(s.bars3d[0].u.size() == 1,
              "bar3d panel: and the last one is refused, since bar3d() rejects an empty grid");
    }

    // A grid with no per-bar bases keeps none: an empty buffer means "this
    // plot has none at all", and growing one here would invent a base.
    {
        RenderSnapshot3D s;
        s.bars3d.push_back(bar3d_grid());
        apply_plot_data_ops(s, {
            MatrixLineEdit{ MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                            0, 3, -1, PlotKind::Bar3D },
        });
        check(s.bars3d[0].bottoms.empty() && s.bars3d[0].heights.size() == 12,
              "bar3d panel: a grid with no per-bar bases still has none afterwards");
    }

    // ---- The address, in both directions. A bar3d op is addressed at the
    // axes; a plane-addressed one must not reach the bars, and a Bar3D op must
    // not reach a plane's own bar plot, which shares the index and the word.
    {
        RenderSnapshot3D s = two_plane_snapshot();
        s.bars3d.push_back(bar3d_grid());
        apply_plot_data_ops(s, { PlotCellEdit{ PlotKind::Bar3D, 0, 2, 0, 99.0, 0 } });
        check(s.bars3d[0].heights[0] == 1.0,
              "bar3d panel: a Bar3D op addressed at a plane does not reach the axes' grid");
        apply_plot_data_ops(s, { PlotCellEdit{ PlotKind::Bar3D, 0, 2, 0, 99.0, -1 } });
        check(s.bars3d[0].heights[0] == 99.0,
              "bar3d panel: (and at the axes it does, so that is the address doing it)");
        check(s.planes[0].sheet.bars[0].heights[0] == 1.0 &&
              s.planes[0].sheet.bars[0].centers[0] == 0.0,
              "bar3d panel: and neither reaches a plane's 2D bar plot, which shares the index");

        // The other way: a 2D bar's own ops must not be diverted to bars3d
        // now that both are addressed at plane -1 in some slot.
        apply_plot_data_ops(s, { BarWidthEdit{ 0, 0.125, 0 } });
        check(s.planes[0].sheet.bars[0].bar_width == 0.125 &&
              s.bars3d[0].u_width == 2.0,
              "bar3d panel: a Bar width edit is a 2D bar's, not a footprint");
    }

    // A Bar3D op against a 2D axes has nothing to address and is dropped,
    // rather than falling through onto one of the five 2D vectors.
    {
        RenderSnapshot snap2d;
        BarPlot bp;
        bp.centers = std::vector<double>{ 0.0, 1.0 };
        bp.heights = std::vector<double>{ 5.0, 6.0 };
        bp.bar_width = 0.5;
        snap2d.bars.push_back(std::move(bp));
        apply_plot_data_ops(snap2d, {
            PlotCellEdit{ PlotKind::Bar3D, 0, 1, 1, 77.0 },
            BarWidthEdit{ 0, 0.25, -1, PlotKind::Bar3D, 0 },
            MatrixLineEdit{ MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                            0, 0, -1, PlotKind::Bar3D },
        });
        check(snap2d.bars[0].heights[1] == 6.0 && snap2d.bars[0].bar_width == 0.5,
              "bar3d panel: a Bar3D op does nothing at all to a 2D axes");
    }

    // ---- And the whole channel, since a bar3d edit is the first thing the
    // 3D lane carries that is not addressed at a plane.
    {
        FigureEditBox box;
        box.update3d(2, [](AxesEdit3D& e) {
            e.plot_ops.push_back(PlotCellEdit{ PlotKind::Bar3D, 0, 2, 8, 55.0, -1 });
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

    // ---- The panel actually clicked, because which lane a table's ops go
    // down is decided by the *slot's* kind and not by the plane index -- and
    // routing on the index is the obvious way to write it, was how the plane
    // sink was written, and would send every bar3d edit down the 2D lane to be
    // dropped with nothing about the drawn panel looking wrong.
    {
        FigureSnapshot fs;
        RenderSnapshot3D r;
        r.bars3d.push_back(bar3d_grid());
        fs.axes.push_back({ { 1, 1, 1 }, std::move(r) });
        fs.generation = fs.data_generation = 1;

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(560.0f, 900.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();

        PanelState    st;
        FigureEditBox box;
        auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(520.0f, 860.0f));
            draw_data_panel(fs, box, st);
            ImGui::Render();
        };
        frame(); frame(); frame();

        // Press and release over a grid of positions until something fires.
        // The only widgets on this tab that produce an op without typing are
        // the grid's own +/- buttons, so whatever fires is a bar3d structural
        // edit -- and the check is about where it went, so any of them will do.
        // The sweep starts below the format controls so a combo popup can
        // never swallow the clicks that follow.
        int  fired = 0;
        bool lane_ok = false, wrong_lane = false;
        for (float y = 120.0f; y < 840.0f && !fired; y += 5.0f) {
            for (float x = 8.0f; x < 512.0f && !fired; x += 5.0f) {
                io.MousePos = ImVec2(x, y);
                io.MouseDown[0] = true;  frame();
                io.MouseDown[0] = false; frame();
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

}  // namespace lt
