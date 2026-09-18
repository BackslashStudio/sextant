// The 3D coordinate chain, the box on screen, and a 3D cell rendered.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {

// ===========================================================================
// Phase 5 -- 3D (v1.0 step 1)
// ===========================================================================
//
// Projection is checked against arithmetic rather than against a picture. An
// orthographic camera is a rotation, one uniform scale and a y flip, so the
// pixel a named box corner lands on can be written down in the test and
// compared exactly -- and the render checks below can then assume the
// projection and test only what a renderer does with it. That is the same
// split step 0 used: exact equivalence for derived numbers, rendered bytes
// only for what the renderer alone decides.

void test_axes3d_projection() {
    std::printf("\n[3D: the coordinate chain]\n");

    using namespace sextant;

    // ---- Transform3D: data -> box, per axis, with the limits going away.
    Transform3D tf{ 0.0, 10.0, -5.0, 5.0, 100.0, 200.0, BoxAspect{} };
    check(tf.box_x(0.0)  == -0.5 && tf.box_x(10.0) == 0.5 && tf.box_x(5.0) == 0.0,
          "3D: an axis maps onto [-side/2, +side/2] whatever its own units");
    check(tf.box_y(-5.0) == -0.5 && tf.box_z(200.0) == 0.5,
          "3D: all three axes map the same way");
    check(std::fabs(tf.data_x(tf.box_x(3.7)) - 3.7) < 1e-12 &&
          std::fabs(tf.data_z(tf.box_z(137.0)) - 137.0) < 1e-12,
          "3D: data -> box -> data is the identity");

    Transform3D wide = tf;
    wide.aspect = BoxAspect{ 2.0, 1.0, 1.0 };
    check(wide.box_x(10.0) == 1.0 && wide.box_y(5.0) == 0.5,
          "3D: BoxAspect is the only thing that changes a picture's proportions");

    // A degenerate axis is rejected by the public setter, so this only has to
    // not produce an infinity when the struct is built directly.
    Transform3D degen{ 1.0, 1.0, 0.0, 1.0, 0.0, 1.0, BoxAspect{} };
    check(degen.box_x(1.0) == 0.0, "3D: a degenerate axis collapses rather than dividing by zero");

    // ---- The camera basis, at a camera whose answer is obvious by hand.
    // Azimuth 0, elevation 0 puts the eye on +x looking back at the origin
    // with world +z up, so screen-right is +y and screen-up is +z.
    const PlotRect frame{ 0.0f, 0.0f, 200.0f, 200.0f };
    Camera3D cam;
    cam.azimuth = 0.0; cam.elevation = 0.0;
    Projector3D p(Transform3D{}, cam, frame, 0.0f);

    check(near_px(static_cast<float>(p.eye_dir().x), 1.0f) &&
          near_px(static_cast<float>(p.eye_dir().y), 0.0f) &&
          near_px(static_cast<float>(p.eye_dir().z), 0.0f),
          "3D: azimuth 0 / elevation 0 puts the eye on +x");
    check(near_px(static_cast<float>(p.right().y), 1.0f) &&
          near_px(static_cast<float>(p.up().z), 1.0f),
          "3D: right/up form a right-handed basis with the view direction");

    // A unit cube with no margin fills a 200x200 frame: half a box unit
    // reaches 100 px, so the scale is exactly 200 px per box unit.
    check(near_px(static_cast<float>(p.pixels_per_box_unit()), 200.0f),
          "3D: the fit is exact -- a unit box in a 200 px frame is 200 px/unit");

    const Px3 origin = p.project_box({ 0.0, 0.0, 0.0 });
    check(near_px(origin.x, 100.0f) && near_px(origin.y, 100.0f),
          "3D: the box centre projects to the frame centre");

    const Px3 py = p.project_box({ 0.0, 0.5, 0.0 });
    check(near_px(py.x, 200.0f) && near_px(py.y, 100.0f),
          "3D: +y is screen-right at this camera, at the scale the fit chose");

    const Px3 pz = p.project_box({ 0.0, 0.0, 0.5 });
    check(near_px(pz.x, 100.0f) && near_px(pz.y, 0.0f),
          "3D: +z is screen-up, i.e. toward smaller pixel y");

    const Px3 near_corner = p.project_box({ 0.5, 0.0, 0.0 });
    const Px3 far_corner  = p.project_box({ -0.5, 0.0, 0.0 });
    check(near_px(near_corner.x, 100.0f) && near_px(near_corner.y, 100.0f),
          "3D: the view axis projects to a point -- it is orthographic, not perspective");
    check(near_corner.depth < far_corner.depth,
          "3D: depth orders the two ends of the view axis, nearer first");

    // ---- Margin and zoom both scale the fit, and nothing else.
    Projector3D pm(Transform3D{}, cam, frame, 0.1f);
    check(near_px(static_cast<float>(pm.pixels_per_box_unit()), 160.0f),
          "3D: the margin is a fraction of the frame taken off each side");

    Camera3D zoomed = cam; zoomed.zoom = 2.0;
    Projector3D pz2(Transform3D{}, zoomed, frame, 0.0f);
    check(near_px(static_cast<float>(pz2.pixels_per_box_unit()), 400.0f),
          "3D: zoom multiplies the fit");

    // ---- The fit answers to BoxAspect through the *projected* silhouette,
    // which is the point of fitting rather than fixing a distance: at this
    // camera a doubled x is edge-on and costs nothing, and turning 90 degrees
    // makes it the direction that runs out of room first.
    Transform3D wide_box{ 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, BoxAspect{ 2.0, 1.0, 1.0 } };
    Projector3D pw(wide_box, cam, frame, 0.0f);
    check(near_px(static_cast<float>(pw.pixels_per_box_unit()), 200.0f),
          "3D: a box stretched along the view axis does not shrink the picture");

    Camera3D side = cam; side.azimuth = 90.0;
    Projector3D ps(wide_box, side, frame, 0.0f);
    check(near_px(static_cast<float>(ps.pixels_per_box_unit()), 100.0f),
          "3D: turned side-on, that same stretch is what the fit has to accommodate");

    // ---- Elevation is clamped where the basis would collapse.
    Camera3D top = cam; top.elevation = 90.0;
    Projector3D pt(Transform3D{}, top, frame, 0.0f);
    check(std::isfinite(pt.pixels_per_box_unit()) &&
          near_px(static_cast<float>(length(pt.right())), 1.0f),
          "3D: a straight-down camera stays well conditioned (elevation clamps)");
}

void test_axes3d_box_plan() {
    std::printf("\n[3D: the box on screen]\n");

    using namespace sextant;

    RenderSnapshot3D snap;
    snap.xticks_override = std::vector<Tick>{ {0.0,"0"}, {0.5,"a"}, {1.0,"1"} };          // 3
    snap.yticks_override = std::vector<Tick>{ {0.0,"0"}, {1.0,"1"} };                     // 2
    snap.zticks_override = std::vector<Tick>{ {0.0,"0"}, {0.25,"q"}, {0.5,"h"}, {1.0,"1"} }; // 4
    snap.xtitle = "X"; snap.ytitle = "Y"; snap.ztitle = "Z";

    const PlotRect frame{ 50.0f, 40.0f, 300.0f, 240.0f };
    const Transform3D tf{ 0,1, 0,1, 0,1, BoxAspect{} };

    Projector3D proj(tf, snap.camera, frame, snap.box_style.margin);
    const Box3DPlan plan = plan_box3d(proj, snap, *snap.xticks_override,
                                      *snap.yticks_override, *snap.zticks_override);

    check(plan.panes.size() == 3,
          "3D: exactly three back panes are drawn, for any camera outside the box");
    // Each pane carries the ticks of the two axes lying in it, so every axis
    // appears on two panes: 2*(3+2+4).
    check(plan.grid.size() == 18, "3D: each pane is ruled by the two axes that lie in it");
    check(plan.axis_lines.size() == 3, "3D: one labelled edge per axis");
    check(plan.tick_marks.size() == 9,
          "3D: one tick mark per tick, across all three axes");
    check(plan.tick_labels.size() == 9,
          "3D: at this size every one of them also has room for its number");
    check(plan.axis_titles.size() == 3, "3D: an axis title for each axis that has one");

    // Each axis title carries its own size and colour. Until v1.0 step 2 the
    // z title borrowed the y title's fields, because AxesStyle is shared with
    // 2D and had only two slots -- which is invisible until someone tries to
    // style the two apart, and then cannot be worked around at all.
    RenderSnapshot3D styled = snap;
    styled.axes_style.xtitle_fontsize = 21.0f;
    styled.axes_style.ytitle_fontsize = 14.0f;
    styled.axes_style.ztitle_fontsize = 9.0f;
    styled.axes_style.xtitle_color = Color::Red;
    styled.axes_style.ytitle_color = Color::Green;
    styled.axes_style.ztitle_color = Color::Blue;
    const Box3DPlan pl_styled = plan_box3d(proj, styled, *snap.xticks_override,
                                           *snap.yticks_override, *snap.zticks_override);
    check(pl_styled.axis_titles.size() == 3 &&
          pl_styled.axis_titles[0].fontsize == 21.0f &&
          pl_styled.axis_titles[1].fontsize == 14.0f &&
          pl_styled.axis_titles[2].fontsize == 9.0f,
          "3D: x, y and z titles each take their own font size");
    check(pl_styled.axis_titles[0].color.r == Color::Red.r &&
          pl_styled.axis_titles[1].color.g == Color::Green.g &&
          pl_styled.axis_titles[2].color.b == Color::Blue.b,
          "3D: and their own colour -- z is not a second copy of y");

    // The back panes turn with the camera. Two cameras half a turn apart see
    // opposite walls, so no pane can be in both plans -- which is what the
    // whole back-face rule is for, and what a fixed choice of pane would fail.
    Camera3D flipped = snap.camera;
    flipped.azimuth += 180.0;
    RenderSnapshot3D snap_flipped = snap;
    snap_flipped.camera = flipped;
    Projector3D proj_f(tf, flipped, frame, snap.box_style.margin);
    const Box3DPlan plan_f = plan_box3d(proj_f, snap_flipped, *snap.xticks_override,
                                        *snap.yticks_override, *snap.zticks_override);
    bool any_shared = false;
    for (const auto& a : plan.panes)
        for (const auto& b : plan_f.panes)
            if (a.xy == b.xy) any_shared = true;
    check(!any_shared, "3D: turning the camera around swaps every pane for its opposite");

    // ...and it is the *far* wall of each pair that gets drawn. Which one that
    // is, is a fact about depth rather than about a sign convention, so the
    // test asks the projector and then requires the plan to have drawn that
    // one. Worth stating separately: getting the sign backwards draws the
    // three near walls instead, which still looks like a box from any angle,
    // still turns with the camera, and still fits its frame -- every weaker
    // check here passes on it.
    const Vec3 hx = Transform3D{}.half_extent();
    auto farther = [&](Vec3 a, Vec3 b) {
        return proj.project_box(a).depth > proj.project_box(b).depth ? a : b;
    };
    const Vec3 fx = farther({  hx.x, 0.0, 0.0 }, { -hx.x, 0.0, 0.0 });
    const Vec3 fy = farther({ 0.0,  hx.y, 0.0 }, { 0.0, -hx.y, 0.0 });
    const Vec3 fz = farther({ 0.0, 0.0,  hx.z }, { 0.0, 0.0, -hx.z });

    auto quad = [&](std::initializer_list<Vec3> pts) {
        std::vector<float> xy;
        for (const Vec3& p : pts) {
            const Px3 q = proj.project_box(p);
            xy.push_back(q.x); xy.push_back(q.y);
        }
        return xy;
    };
    check(plan.panes[0].xy == quad({ { fx.x, -hx.y, -hx.z }, { fx.x,  hx.y, -hx.z },
                                     { fx.x,  hx.y,  hx.z }, { fx.x, -hx.y,  hx.z } }),
          "3D: the drawn YZ pane is the one facing away from the camera");
    check(plan.panes[1].xy == quad({ { -hx.x, fy.y, -hx.z }, {  hx.x, fy.y, -hx.z },
                                     {  hx.x, fy.y,  hx.z }, { -hx.x, fy.y,  hx.z } }),
          "3D: and so is the ZX pane");
    check(plan.panes[2].xy == quad({ { -hx.x, -hx.y, fz.z }, {  hx.x, -hx.y, fz.z },
                                     {  hx.x,  hx.y, fz.z }, { -hx.x,  hx.y, fz.z } }),
          "3D: and the XY pane -- so nothing in the scene can ever be behind one");

    // Each labelled edge is a silhouette edge bounding the drawn panes, which
    // is what keeps its numbers outside the solid rather than across it. In
    // pixels that is exactly: both endpoints are corners of a drawn pane.
    bool edges_on_panes = true;
    for (const auto& line : plan.axis_lines) {
        for (int end = 0; end < 2; ++end) {
            const float ex = line.xy[end * 2], ey = line.xy[end * 2 + 1];
            bool found = false;
            for (const auto& pane : plan.panes)
                for (std::size_t i = 0; i + 1 < pane.xy.size(); i += 2)
                    if (near_px(pane.xy[i], ex, 0.01f) && near_px(pane.xy[i + 1], ey, 0.01f))
                        found = true;
            if (!found) edges_on_panes = false;
        }
    }
    check(edges_on_panes, "3D: every labelled edge bounds a drawn pane");

    // Grid lines lie on the panes, and the panes are behind everything, so the
    // whole box has to fit inside the fraction of the frame the fit reserved.
    // Swept over cameras, because which corner is extreme changes with the
    // angle and a fit that is right at one angle can be wrong at another.
    const float m  = snap.box_style.margin;
    const float ix = frame.x + frame.w * m, iy = frame.y + frame.h * m;
    const float iw = frame.w * (1.0f - 2.0f * m), ih = frame.h * (1.0f - 2.0f * m);

    bool inside = true, tight = false;
    for (int ai = 0; ai < 8; ++ai) {
        for (int ei = -2; ei <= 2; ++ei) {
            Camera3D c;
            c.azimuth = ai * 45.0;
            c.elevation = ei * 40.0;
            RenderSnapshot3D s = snap;
            s.camera = c;
            Projector3D pr(tf, c, frame, m);
            const Box3DPlan pl = plan_box3d(pr, s, *snap.xticks_override,
                                            *snap.yticks_override, *snap.zticks_override);
            float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
            for (const auto* g : { &pl.panes, &pl.grid, &pl.axis_lines })
                for (const auto& poly : *g)
                    for (std::size_t i = 0; i + 1 < poly.xy.size(); i += 2) {
                        lo_x = std::min(lo_x, poly.xy[i]);   hi_x = std::max(hi_x, poly.xy[i]);
                        lo_y = std::min(lo_y, poly.xy[i+1]); hi_y = std::max(hi_y, poly.xy[i+1]);
                    }
            if (lo_x < ix - 0.5f || hi_x > ix + iw + 0.5f ||
                lo_y < iy - 0.5f || hi_y > iy + ih + 0.5f) inside = false;
            // Tight in at least one direction, at least somewhere in the
            // sweep: a fit that always left room would be shrinking the
            // picture and no "inside" check would notice.
            if (std::fabs((hi_x - lo_x) - iw) < 0.5f || std::fabs((hi_y - lo_y) - ih) < 0.5f)
                tight = true;
        }
    }
    check(inside, "3D: the box stays inside its reserved area at every camera angle");
    check(tight,  "3D: and fills it -- the fit is tight, not merely safe");

    // Labels are pushed *outward* from the box, which is the property the
    // per-axis offset direction exists to give. Every tick label must be
    // further from the projected box centre than its own tick mark.
    const Px3 centre = proj.project_box({ 0.0, 0.0, 0.0 });
    bool pushed_out = true, named = true;
    for (const auto& lb : plan.tick_labels) {
        if (lb.tick < 0 || lb.tick >= static_cast<int>(plan.tick_marks.size())) {
            named = false;
            continue;
        }
        const auto& tm = plan.tick_marks[lb.tick].xy;
        const float d_label = std::hypot(lb.x - centre.x, lb.y - centre.y);
        const float d_tick  = std::hypot(tm[0] - centre.x, tm[1] - centre.y);
        if (d_label <= d_tick) pushed_out = false;
    }
    check(named, "3D: every tick label names the tick mark it belongs to");
    check(pushed_out, "3D: every tick label sits further out than the tick it names");

    // Labels are thinned when the edge they run along is too short for them,
    // and only the labels are -- the marks and grid still show every
    // subdivision. This is the case a 2D axis never has: the same tick list on
    // an edge foreshortened to a fraction of its length.
    RenderSnapshot3D dense = snap;
    dense.xticks_override.reset();   // generate_ticks over 0..1 gives eleven
    dense.yticks_override.reset();
    dense.zticks_override.reset();
    const PlotRect small{ 0.0f, 0.0f, 240.0f, 200.0f };
    Projector3D pd(tf, dense.camera, small, dense.box_style.margin);
    const std::vector<Tick> auto_x = generate_ticks(0.0, 1.0);
    const Box3DPlan pl_dense = plan_box3d(pd, dense, auto_x, auto_x, auto_x);
    check(pl_dense.tick_marks.size() == auto_x.size() * 3,
          "3D: thinning never removes a tick mark");
    check(pl_dense.tick_labels.size() < pl_dense.tick_marks.size(),
          "3D: but it does remove numbers there is no room for");
    check(pl_dense.tick_labels.size() >= 3,
          "3D: and never all of them -- each axis keeps at least its first");
}

// What a limit change is *for*, stated as an assertion because it is a
// reasonable thing to be unsure about from the outside: the limits say what
// range of data the box spans, not how big the box is. The box's size and
// proportions come from BoxAspect and the automatic fit, and deliberately do
// not move -- otherwise the picture would change shape whenever the data did,
// and three axes in incommensurable units could not be compared at all. So
// changing a limit must relabel the axis and remap the data, and must leave
// the drawn box exactly where it was.
void test_axes3d_limits_rescale_not_resize() {
    std::printf("\n[3D: what a limit change does]\n");

    using namespace sextant;

    const PlotRect frame{ 0.0f, 0.0f, 300.0f, 240.0f };

    auto plan_at = [&](double xhi) {
        RenderSnapshot3D s;
        s.xmax = xhi;
        const Transform3D tf{ s.xmin, s.xmax, s.ymin, s.ymax, s.zmin, s.zmax, s.aspect };
        Projector3D proj(tf, s.camera, frame, s.box_style.margin);
        const std::vector<Tick> xt = generate_ticks(s.xmin, s.xmax);
        const std::vector<Tick> yt = generate_ticks(s.ymin, s.ymax);
        const std::vector<Tick> zt = generate_ticks(s.zmin, s.zmax);
        return std::pair{ plan_box3d(proj, s, xt, yt, zt), xt };
    };

    const auto [near_plan, xt1] = plan_at(1.0);
    const auto [wide_plan, xt5] = plan_at(5.0);

    bool same_box = near_plan.panes.size() == wide_plan.panes.size() &&
                    near_plan.axis_lines.size() == wide_plan.axis_lines.size();
    for (std::size_t i = 0; same_box && i < near_plan.panes.size(); ++i)
        same_box = near_plan.panes[i].xy == wide_plan.panes[i].xy;
    for (std::size_t i = 0; same_box && i < near_plan.axis_lines.size(); ++i)
        same_box = near_plan.axis_lines[i].xy == wide_plan.axis_lines[i].xy;
    check(same_box,
          "3D limits: the drawn box does not move -- its size is BoxAspect and the fit, not the data range");

    check(xt1.back().value != xt5.back().value,
          "3D limits: but the ticks are generated over the new range");

    bool labels_differ = false;
    for (const auto& a : near_plan.tick_labels) {
        bool found = false;
        for (const auto& b : wide_plan.tick_labels)
            if (a.text == b.text && std::fabs(a.x - b.x) < 0.01f) found = true;
        if (!found) labels_differ = true;
    }
    check(labels_differ, "3D limits: and the numbers on the box change with them");

    // The mapping moved too, not just the labels: the same data value lands
    // somewhere else on the box.
    const Transform3D t1{ 0,1, 0,1, 0,1, BoxAspect{} };
    const Transform3D t5{ 0,5, 0,1, 0,1, BoxAspect{} };
    check(t1.box_x(1.0) != t5.box_x(1.0) && t5.box_x(5.0) == t1.box_x(1.0),
          "3D limits: x=1 sits at the box edge under 0..1 and a fifth of the way under 0..5");
}

void test_axes3d_coexistence() {
    std::printf("\n[3D: coexisting with 2D in one grid]\n");

    using namespace sextant;

    // A 1x2 grid: a 2D line plot beside an empty 3D box.
    FigureSnapshot fs = make_snapshot(1, 2, 1);
    fs.axes[0].slot = { 1, 2, 1 };
    RenderSnapshot3D r3;
    r3.title = "3D";
    fs.axes.push_back({ {1, 2, 2}, std::move(r3) });

    const FigureLayout lay = compute_figure_layout(fs, 800, 400);
    check(lay.cells.size() == 2, "3D: a mixed grid lays out both cells");
    check(!lay.cells[0].is_3d() && lay.cells[1].is_3d(),
          "3D: each cell carries the geometry its own kind needs, and only that");
    check(lay.cells[0].box3d == std::nullopt && lay.cells[1].box3d.has_value(),
          "3D: a projector exists exactly for the 3D cell");
    check(lay.cells[1].xticks.empty() && lay.cells[1].yticks.empty(),
          "3D: a 3D cell leaves the 2D tick lists empty -- its three live in box3d");
    check(lay.cells[1].box3d->xticks.size() > 1 &&
          lay.cells[1].box3d->zticks.size() > 1,
          "3D: all three axes get ticks, from the same generate_ticks() 2D uses");

    check(near_px(lay.cells[0].frame.y, lay.cells[1].frame.y, 0.5f) &&
          near_px(lay.cells[0].frame.h, lay.cells[1].frame.h, 0.5f),
          "3D: the two cells' frames still line up along their row, so the grid is a grid");
    check(lay.cells[1].frame.w > lay.cells[0].frame.w,
          "3D: and the 3D cell does not pay for the 2D cell's tick labels (step 15.1)");

    // The projector is built from the *carved* frame, so the box is centred on
    // what is left rather than on where the frame started.
    const Px3 c = lay.cells[1].box3d->proj.project_box({ 0.0, 0.0, 0.0 });
    const PlotRect& f = lay.cells[1].frame;
    check(near_px(c.x, f.x + f.w * 0.5f, 0.5f) && near_px(c.y, f.y + f.h * 0.5f, 0.5f),
          "3D: the box is centred on the frame it was actually given");

    // A 3D cell gives up only its title band, since where its labels land
    // depends on the camera and cannot be measured before the frame exists.
    FigureSnapshot only3d = make_snapshot3d(1, 1, 1);
    const PlotInsets bare = compute_figure_layout(only3d, 800, 600).cells[0].reserved;
    check(bare.left == 0.0f && bare.right == 0.0f &&
          bare.bottom == 0.0f && bare.top == 0.0f,
          "3D: an untitled 3D cell reserves no inset at all");
    only3d.axes[0].snap3d()->title = "T";
    check(compute_figure_layout(only3d, 800, 600).cells[0].reserved.top > 0.0f,
          "3D: a title still gets a measured band, as in 2D");
}

void test_axes3d_public_api() {
    std::printf("\n[3D: the public API]\n");

    using namespace sextant;

    auto threw = [](auto&& fn) {
        try { fn(); return false; } catch (const std::invalid_argument&) { return true; }
    };

    auto fig = Figure::create({ .width = 400, .height = 300 });
    auto ax2 = fig->add_subplot(1, 2, 1);
    auto ax3 = fig->add_subplot3d(1, 2, 2);
    check(ax2 && ax3, "3D: add_subplot and add_subplot3d share one grid");
    check(fig->add_subplot3d(1, 2, 2) == ax3,
          "3D: re-requesting a 3D cell returns the same axes");
    check(threw([&]{ fig->add_subplot(1, 2, 2); }),
          "3D: asking for a 2D axes in a 3D cell throws rather than replacing it");
    check(threw([&]{ fig->add_subplot3d(1, 2, 1); }),
          "3D: and the other way round");

    auto fig3 = Figure::create({ .width = 400, .height = 300 });
    fig3->add_subplot3d(1, 1, 1);
    check(threw([&]{ fig3->axes(); }),
          "3D: axes() will not hand back a 3D cell as an Axes");

    check(threw([&]{ ax3->set_xlim(1.0, 1.0); }),
          "3D: a degenerate limit throws at ingest, once, not at each conversion");
    check(threw([&]{ ax3->set_zlim(0.0, std::numeric_limits<double>::infinity()); }),
          "3D: a non-finite limit likewise");
    check(threw([&]{ ax3->set_box_aspect({ 0.0, 1.0, 1.0 }); }),
          "3D: a zero box side throws");

    ax3->set_view(30.0, 180.0);
    check(ax3->camera().elevation == 89.0 && ax3->camera().azimuth == 30.0,
          "3D: elevation is clamped where the camera basis would collapse");

    Camera3D bad; bad.zoom = 0.0;
    ax3->set_camera(bad);
    check(ax3->camera().zoom == 1.0, "3D: a zero zoom falls back rather than dividing by zero");

    // Projection mode and field of view (v1.0 step 3). Both are camera state,
    // so both round-trip through camera() and both go through the same clamp
    // every other path ends in -- the public setters are not a second opinion
    // about what a valid camera is.
    check(ax3->camera().projection == Projection::Orthographic,
          "3D: the default camera is orthographic -- a plot is read as much as looked at");
    ax3->set_projection(Projection::Perspective);
    check(ax3->camera().projection == Projection::Perspective,
          "3D: set_projection() switches it and changes nothing else");
    ax3->set_fov(200.0);
    check(ax3->camera().fov == kMaxFov, "3D: set_fov() clamps rather than throwing");
    ax3->set_fov(60.0);
    check(ax3->camera().fov == 60.0 && ax3->camera().projection == Projection::Perspective,
          "3D: and a fov in range is taken as given");
    ax3->set_view(10.0, 20.0);
    check(ax3->camera().fov == 60.0 && ax3->camera().projection == Projection::Perspective,
          "3D: set_view() moves the eye without resetting how it projects");
}


// The requirement the whole step exists for: under perspective, annotation
// must not scale with distance. It is satisfied by construction -- annotation
// is NanoVG text and pixel-space polylines, and neither renderer ever sees a
// camera -- so what is asserted here is that the construction holds, at two
// cameras a dolly apart where the scene geometry demonstrably does scale.
void test_annotation_invariance() {
    std::printf("\n[3D: annotation does not scale with distance]\n");

    using namespace sextant;

    const PlotRect frame{ 0.0f, 0.0f, 300.0f, 240.0f };
    const Transform3D tf{ 0,1, 0,1, 0,1, BoxAspect{} };

    RenderSnapshot3D snap;
    snap.xticks_override = std::vector<Tick>{ {0.0,"0"}, {0.5,"a"}, {1.0,"1"} };
    snap.yticks_override = snap.xticks_override;
    snap.zticks_override = snap.xticks_override;
    snap.xtitle = "X"; snap.ytitle = "Y"; snap.ztitle = "Z";
    snap.camera.projection = Projection::Perspective;
    snap.camera.fov = 70.0;

    auto plan_at = [&](double dolly) {
        RenderSnapshot3D s = snap;
        const Projector3D probe(tf, s.camera, frame, s.box_style.margin);
        s.camera.target = probe.forward() * dolly;
        const Projector3D pr(tf, s.camera, frame, s.box_style.margin);
        return std::pair{ plan_box3d(pr, s, *s.xticks_override, *s.yticks_override,
                                     *s.zticks_override), pr };
    };

    const auto [far_plan, far_proj] = plan_at(0.0);
    const auto [near_plan, near_proj] = plan_at(0.35);

    // The scene did move, which is what makes the rest of this a statement
    // about annotation rather than about a camera that did nothing.
    auto edge_len = [](const Box3DPlan& pl, std::size_t i) {
        const auto& xy = pl.axis_lines[i].xy;
        return std::hypot(xy[2] - xy[0], xy[3] - xy[1]);
    };
    check(far_plan.axis_lines.size() == 3 && near_plan.axis_lines.size() == 3,
          "3D invariance: both cameras draw all three labelled edges");
    bool geometry_moved = false;
    for (std::size_t i = 0; i < 3; ++i)
        if (std::fabs(edge_len(near_plan, i) - edge_len(far_plan, i)) > 1.0)
            geometry_moved = true;
    check(geometry_moved, "3D invariance: the dolly really did change the picture");

    // Text size is a font size in pixel space on both output paths, and a tick
    // mark is a fixed pixel length along a pixel-space direction. Neither can
    // pick up the divide, because neither is ever expressed in box space --
    // that is the rule, and its corollary is that the box frame must never
    // become geometry in the scene.
    bool same_size = far_plan.tick_labels.size() == near_plan.tick_labels.size();
    for (std::size_t i = 0; same_size && i < far_plan.tick_labels.size(); ++i)
        if (far_plan.tick_labels[i].fontsize != near_plan.tick_labels[i].fontsize)
            same_size = false;
    check(same_size, "3D invariance: every tick label is the same size at both distances");

    auto mark_len = [](const Box3DPlan& pl, std::size_t i) {
        const auto& xy = pl.tick_marks[i].xy;
        return std::hypot(xy[2] - xy[0], xy[3] - xy[1]);
    };
    bool marks_equal = !far_plan.tick_marks.empty() &&
                       far_plan.tick_marks.size() == near_plan.tick_marks.size();
    for (std::size_t i = 0; marks_equal && i < far_plan.tick_marks.size(); ++i)
        if (std::fabs(mark_len(far_plan, i) - mark_len(near_plan, i)) > 1e-3)
            marks_equal = false;
    check(marks_equal, "3D invariance: and every tick mark the same length, in pixels");

    bool titles_equal = far_plan.axis_titles.size() == near_plan.axis_titles.size();
    for (std::size_t i = 0; titles_equal && i < far_plan.axis_titles.size(); ++i)
        if (far_plan.axis_titles[i].fontsize != near_plan.axis_titles[i].fontsize)
            titles_equal = false;
    check(titles_equal, "3D invariance: the axis titles likewise");
}

// What the box actually draws, in both outputs. The checks above are all on
// derived numbers; these are on what a renderer does with them -- and in
// particular on the two renderers agreeing, which they are made to do by
// consuming the same Box3DPlan rather than by each projecting the box.
void test_axes3d_render() {
    std::printf("\n[3D: rendered]\n");

    using namespace sextant;

    constexpr int W = 320, H = 260;

    auto snapshot = [](Camera3D cam, const char* title) {
        FigureSnapshot fs = make_snapshot3d(1, 1, 1, cam);
        RenderSnapshot3D* s = fs.axes[0].snap3d();
        s->title = title;
        s->xtitle = "x"; s->ytitle = "y"; s->ztitle = "z";
        return fs;
    };

    auto render = [&](const FigureSnapshot& fs, const std::string& stem) {
        {
            GLContext ctx({ .width = W, .height = H,
                            .title = "layout_test", .visible = false });
            NvgRenderer  nvg(ctx.nvg());
            DataRenderer data;
            export_figure_png(ctx, nvg, data, fs, stem + ".png", W, H, 1);
        }
        export_figure_svg(fs, stem + ".svg", W, H);
    };

    auto read_file = [](const std::string& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    };
    auto count_of = [](const std::string& hay, const std::string& needle) {
        int n = 0;
        for (std::size_t i = hay.find(needle); i != std::string::npos;
             i = hay.find(needle, i + 1)) ++n;
        return n;
    };

    Camera3D def;
    render(snapshot(def, "Box"), "box3d_a");

    // The SVG must contain exactly what the plan holds -- that is the check
    // that the vector path draws the box the raster path drew, and it works
    // precisely because neither writer projects anything itself.
    RenderSnapshot3D probe;
    probe.title = "Box"; probe.xtitle = "x"; probe.ytitle = "y"; probe.ztitle = "z";
    const FigureSnapshot fs_probe = snapshot(def, "Box");
    const FigureLayout   lay      = compute_figure_layout(fs_probe, W, H);
    const Box3DLayout&   b        = *lay.cells[0].box3d;
    const Box3DPlan      plan     = plan_box3d(b.proj, *fs_probe.axes[0].snap3d(),
                                               b.xticks, b.yticks, b.zticks);

    const std::string svg = read_file("box3d_a.svg");
    check(!svg.empty(), "3D: the SVG path produces a file");
    check(count_of(svg, "<polygon") == static_cast<int>(plan.panes.size() +
                                                        plan.pane_edges.size()),
          "3D: every pane and pane outline reaches the SVG");
    check(count_of(svg, "<polyline") == static_cast<int>(plan.grid.size() +
                                                         plan.axis_lines.size() +
                                                         plan.tick_marks.size()),
          "3D: every grid line, axis line and tick mark reaches the SVG");
    // Plan text plus the axes title, which a 3D cell positions the same way a
    // 2D one does and so comes out of the shared path.
    check(count_of(svg, "<text") == static_cast<int>(plan.tick_labels.size() +
                                                     plan.axis_titles.size()) + 1,
          "3D: every label and axis title reaches the SVG, plus the axes title");

    // Counts alone would pass on a writer that projected the box itself and
    // got it wrong. The coordinates are the check that the two outputs cannot
    // disagree about where the box is -- and they cannot, because only one of
    // them projects it.
    auto points_of = [](const Box3DPlan::Poly& p) {
        std::ostringstream o;
        for (std::size_t i = 0; i + 1 < p.xy.size(); i += 2) {
            if (i) o << ' ';
            o << p.xy[i] << ',' << p.xy[i + 1];
        }
        return o.str();
    };
    bool geometry_matches = !plan.panes.empty() && !plan.axis_lines.empty();
    for (const auto& poly : plan.panes)
        if (svg.find(points_of(poly)) == std::string::npos) geometry_matches = false;
    for (const auto& poly : plan.axis_lines)
        if (svg.find(points_of(poly)) == std::string::npos) geometry_matches = false;
    check(geometry_matches,
          "3D: the SVG carries the plan's own pixels, not its own projection of the box");

    bool labels_placed = !plan.tick_labels.empty();
    for (const auto& lb : plan.tick_labels) {
        std::ostringstream o;
        o << "<text x=\"" << lb.x << "\"";
        if (svg.find(o.str()) == std::string::npos) labels_placed = false;
    }
    check(labels_placed, "3D: and the labels land on the plan's own anchors");

    // The raster path drew something, and drew *the box* rather than a frame:
    // an empty 3D axes with its panes turned off is a different picture.
    FigureSnapshot no_panes = snapshot(def, "Box");
    no_panes.axes[0].snap3d()->box_style.panes = false;
    no_panes.axes[0].snap3d()->grid_enabled    = false;
    render(no_panes, "box3d_bare");

    auto png_diff = [](const std::string& a, const std::string& b) {
        int aw = 0, ah = 0, bw = 0, bh = 0, comp = 0;
        unsigned char* pa = stbi_load((a + ".png").c_str(), &aw, &ah, &comp, 4);
        unsigned char* pb = stbi_load((b + ".png").c_str(), &bw, &bh, &comp, 4);
        int px = -1;
        if (pa && pb && aw == bw && ah == bh) {
            px = 0;
            for (int i = 0; i < aw * ah * 4; ++i)
                if (pa[i] != pb[i]) { ++px; }
        }
        if (pa) stbi_image_free(pa);
        if (pb) stbi_image_free(pb);
        return px;
    };

    check(png_diff("box3d_a", "box3d_bare") > 1000,
          "3D: the panes and grid are actually rasterized, not just planned");

    // Turning the camera turns the picture. Without this every check above
    // would pass on a renderer that ignored the camera entirely.
    Camera3D turned = def;
    turned.azimuth += 90.0;
    render(snapshot(turned, "Box"), "box3d_b");
    check(png_diff("box3d_a", "box3d_b") > 1000,
          "3D: the camera reaches the pixels");

    // ...and the annotation keeps its size while doing so, which is the whole
    // of the invariance rule. Text size is a font size in pixel space on both
    // paths, so the assertion is that the SVG says the same number at two
    // very different cameras -- there is no code path by which it could not,
    // and that is the point being recorded.
    const std::string svg_b = read_file("box3d_b.svg");
    const std::string size_attr = "font-size=\"11\"";
    check(count_of(svg, size_attr) == count_of(svg_b, size_attr) &&
          count_of(svg, size_attr) > 0,
          "3D: tick labels are the same size at every camera angle");

    // ---- The same box under a perspective camera (v1.0 step 3) -----------
    // Both output paths again, and for the same reason: the SVG writer never
    // projects anything, so the only way it can carry perspective geometry is
    // by carrying the plan's own pixels. That property is what makes the
    // raster/vector agreement structural rather than a thing to re-check per
    // projection mode -- but it is worth showing that it survived the mode
    // that could have broken it.
    Camera3D persp = def;
    persp.projection = Projection::Perspective;
    persp.fov = 80.0;
    render(snapshot(persp, "Box"), "box3d_persp");

    const FigureSnapshot fs_persp = snapshot(persp, "Box");
    const FigureLayout   lay_p    = compute_figure_layout(fs_persp, W, H);
    const Box3DLayout&   bp       = *lay_p.cells[0].box3d;
    check(bp.proj.is_perspective(),
          "3D perspective: the projector the layout builds is the one the camera asked for");
    const Box3DPlan plan_p = plan_box3d(bp.proj, *fs_persp.axes[0].snap3d(),
                                        bp.xticks, bp.yticks, bp.zticks);

    const std::string svg_p = read_file("box3d_persp.svg");
    bool persp_geometry_matches = !plan_p.panes.empty() && !plan_p.axis_lines.empty();
    for (const auto& poly : plan_p.panes)
        if (svg_p.find(points_of(poly)) == std::string::npos) persp_geometry_matches = false;
    for (const auto& poly : plan_p.axis_lines)
        if (svg_p.find(points_of(poly)) == std::string::npos) persp_geometry_matches = false;
    check(persp_geometry_matches,
          "3D perspective: the SVG carries the perspective plan's own pixels, as under ortho");
    check(png_diff("box3d_a", "box3d_persp") > 1000,
          "3D perspective: and the raster path draws a visibly different box");

    // ---- bar3d, in both outputs (v1.0 step 4) ----------------------------
    auto bar_snapshot = [&](Camera3D cam, bool edges) {
        FigureSnapshot fs = snapshot(cam, "Bars");
        RenderSnapshot3D* s = fs.axes[0].snap3d();
        Bar3DPlot bp;
        bp.u = std::vector<double>{ 0.0, 1.0, 2.0 };
        bp.v = std::vector<double>{ 0.0, 1.0 };
        bp.heights = std::vector<double>{ 3.0, 1.0, 2.0, 4.0, 1.5, 2.5 };
        bp.u_width = bp.v_width = 0.8;
        bp.opts.color = Color::Blue;
        bp.opts.edges = edges;
        s->bars3d.push_back(std::move(bp));
        return fs;
    };

    const FigureSnapshot fs_bars = bar_snapshot(def, false);
    render(fs_bars, "bar3d_a");

    const FigureLayout lay_b = compute_figure_layout(fs_bars, W, H);
    const Box3DLayout& bb    = *lay_b.cells[0].box3d;
    const std::vector<Bar3DPolygon> faces =
        plan_bars3d(bb.proj, fs_bars.axes[0].snap3d()->bars3d);

    const std::string svg_bars = read_file("bar3d_a.svg");
    check(count_of(svg_bars, "<polygon") ==
              static_cast<int>(plan_box3d(bb.proj, *fs_bars.axes[0].snap3d(),
                                          bb.xticks, bb.yticks, bb.zticks).panes.size() * 2
                               + faces.size()),
          "bar3d: every planned face reaches the SVG, and only those");
    bool bar_pixels_match = !faces.empty();
    for (const Bar3DPolygon& f : faces) {
        std::ostringstream pts;
        for (std::size_t i = 0; i + 1 < f.xy.size(); i += 2) {
            if (i) pts << ' ';
            pts << f.xy[i] << ',' << f.xy[i + 1];
        }
        if (svg_bars.find(pts.str()) == std::string::npos) bar_pixels_match = false;
    }
    check(bar_pixels_match,
          "bar3d: with the planner's own pixels, not the writer's own projection");
    check(png_diff("box3d_a", "bar3d_a") > 1000,
          "bar3d: and the raster path really draws them");

    // Edges on, under a perspective camera -- the one combination where the
    // world-space stroke fork is visible at all, since under orthographic a
    // world-space width and a screen-space one differ by a constant factor.
    Camera3D bar_persp = def;
    bar_persp.projection = Projection::Perspective;
    bar_persp.fov = 85.0;
    render(bar_snapshot(bar_persp, true), "bar3d_persp_edges");
    check(png_diff("bar3d_a", "bar3d_persp_edges") > 1000,
          "bar3d: outlines and a perspective camera both reach the pixels");

    // Translucent bars, in both outputs. The GPU path stops being a depth-
    // buffer resolve here and becomes an ordered draw, so this is the one
    // combination where the raster path uses the painter order at all.
    FigureSnapshot fs_glass = bar_snapshot(def, false);
    fs_glass.axes[0].snap3d()->bars3d[0].opts.alpha = 0.45f;
    render(fs_glass, "bar3d_alpha");
    check(png_diff("bar3d_a", "bar3d_alpha") > 1000,
          "bar3d alpha: translucent bars are a different picture from opaque ones");

    // Translucent *and* outlined, which is the combination the two paths had
    // to be brought into line on: all twelve edges of every bar, once each,
    // at the bar's own alpha.
    FigureSnapshot fs_caged = bar_snapshot(def, true);
    {
        Bar3DPlot& bp = fs_caged.axes[0].snap3d()->bars3d[0];
        bp.opts.alpha = 0.4f;
        bp.opts.edgecolor = { 0.0f, 0.0f, 0.0f, 1.0f };
        // Deliberately not 0.4: the two opacities are separate fields, and a
        // check that used the same number for both could not tell them apart.
        bp.opts.edge_alpha = 0.6f;
    }
    render(fs_caged, "bar3d_alpha_edges");
    const std::string svg_caged = read_file("bar3d_alpha_edges.svg");
    const std::vector<Bar3DPolygon> caged_faces =
        plan_bars3d(bb.proj, fs_caged.axes[0].snap3d()->bars3d);
    std::size_t caged_lines = 0;
    for (const Bar3DPolygon& p : caged_faces) if (!p.filled) ++caged_lines;
    check(count_of(svg_caged, "<polyline") ==
              static_cast<int>(caged_lines +
                  plan_box3d(bb.proj, *fs_caged.axes[0].snap3d(),
                             bb.xticks, bb.yticks, bb.zticks).grid.size() +
                  plan_box3d(bb.proj, *fs_caged.axes[0].snap3d(),
                             bb.xticks, bb.yticks, bb.zticks).axis_lines.size() +
                  plan_box3d(bb.proj, *fs_caged.axes[0].snap3d(),
                             bb.xticks, bb.yticks, bb.zticks).tick_marks.size()),
          "bar3d alpha: a translucent outline reaches the SVG as its own twelve polylines");
    check(count_of(svg_caged, "stroke-opacity=\"0.6\"") == static_cast<int>(caged_lines),
          "bar3d alpha: at its own edge_alpha, on every one of them");
    check(count_of(svg_caged, "fill-opacity=\"0.4\"") ==
              static_cast<int>(caged_faces.size() - caged_lines),
          "bar3d alpha: while the faces underneath keep theirs, which is a different number");

    const std::string svg_glass = read_file("bar3d_alpha.svg");
    const std::vector<Bar3DPolygon> glass_faces =
        plan_bars3d(bb.proj, fs_glass.axes[0].snap3d()->bars3d);
    check(count_of(svg_glass, "fill-opacity=\"0.45\"") ==
              static_cast<int>(glass_faces.size()),
          "bar3d alpha: and the SVG carries it on every face, all six of each bar");
    check(glass_faces.size() == faces.size() * 2,
          "bar3d alpha: which is twice what an opaque plot emits");

    // That the raster path composites rather than merely tinting. An opaque
    // scene has one colour per visible face and nothing else; a translucent one
    // has a colour for every *stack* of faces over whatever is behind them, so
    // if the bars behind really are showing through there must be markedly more
    // distinct colours on screen. Counting them is the cheapest statement of
    // "you can see through it" that does not depend on where any one bar landed.
    auto distinct_colours = [](const std::string& path) {
        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
        std::set<unsigned int> seen;
        if (px) {
            for (int i = 0; i < w * h; ++i) {
                const unsigned char* p = px + i * 4;
                seen.insert((static_cast<unsigned>(p[0]) << 16) |
                            (static_cast<unsigned>(p[1]) << 8) | p[2]);
            }
            stbi_image_free(px);
        }
        return seen.size();
    };
    const std::size_t opaque_colours = distinct_colours("bar3d_a.png");
    const std::size_t glass_colours  = distinct_colours("bar3d_alpha.png");
    // The bound is the face count rather than a round number: the two images
    // share every antialiased edge of the box and its text, so the extra
    // colours can only come from bars stacked over one another, and there being
    // more of them than there are faces in the whole plot is not something a
    // flat tint could produce.
    check(glass_colours > opaque_colours + glass_faces.size(),
          "bar3d alpha: and the raster path composites the bars behind, not just tints the front");
    std::printf("  distinct colours: %zu opaque, %zu translucent (%zu faces)\n",
                opaque_colours, glass_colours, glass_faces.size());

    // Where the GPU actually put them, against where the projector says they
    // go. The matrix is checked as arithmetic elsewhere; this is the other
    // half -- that the matrix is the one the shader is fed, with the buffer,
    // the anchor, the affine and the viewport all wired the way it assumes.
    // A single bar filling the whole box makes the statement exact: its
    // silhouette must be the box's own, to the pixel.
    FigureSnapshot fs_fill = snapshot(def, "");
    {
        RenderSnapshot3D* s = fs_fill.axes[0].snap3d();
        s->xmin = 0; s->xmax = 1; s->xlim_auto = false;
        s->ymin = 0; s->ymax = 1; s->ylim_auto = false;
        s->zmin = 0; s->zmax = 1; s->zlim_auto = false;
        s->box_style.panes = false;
        s->grid_enabled = false;
        s->xticks_override = std::vector<Tick>{};
        s->yticks_override = std::vector<Tick>{};
        s->zticks_override = std::vector<Tick>{};
        s->xtitle.clear(); s->ytitle.clear(); s->ztitle.clear();
        Bar3DPlot bp;
        bp.u = std::vector<double>{ 0.5 };
        bp.v = std::vector<double>{ 0.5 };
        bp.heights = std::vector<double>{ 1.0 };
        bp.u_width = bp.v_width = 1.0;
        bp.opts.color = { 1.0f, 0.0f, 0.0f, 1.0f };
        bp.opts.shading = 0.0f;      // one flat colour, so the pixels are findable
        s->bars3d.push_back(std::move(bp));
    }
    render(fs_fill, "bar3d_fill");

    {
        const FigureLayout lf = compute_figure_layout(fs_fill, W, H);
        const Projector3D& pr = lf.cells[0].box3d->proj;
        float ex0 = 1e9f, ey0 = 1e9f, ex1 = -1e9f, ey1 = -1e9f;
        for (int i = 0; i < 8; ++i) {
            const Px3 q = pr.project_box({ (i & 1) ? 0.5 : -0.5, (i & 2) ? 0.5 : -0.5,
                                           (i & 4) ? 0.5 : -0.5 });
            ex0 = std::min(ex0, q.x); ex1 = std::max(ex1, q.x);
            ey0 = std::min(ey0, q.y); ey1 = std::max(ey1, q.y);
        }
        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load("bar3d_fill.png", &w, &h, &comp, 4);
        int gx0 = 1 << 30, gy0 = 1 << 30, gx1 = -1, gy1 = -1;
        if (px) {
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const unsigned char* p = px + (y * w + x) * 4;
                    if (p[0] > 200 && p[1] < 60 && p[2] < 60) {
                        gx0 = std::min(gx0, x); gx1 = std::max(gx1, x);
                        gy0 = std::min(gy0, y); gy1 = std::max(gy1, y);
                    }
                }
            stbi_image_free(px);
        }
        check(gx1 >= gx0, "bar3d: the bar is on screen at all");
        // Two-sided, with the two sides deliberately different. Filled pixels
        // may never fall *outside* the projected silhouette (a pixel there
        // would mean the GPU is drawing somewhere the projector does not
        // describe), while falling a little short of it is what rasterizing an
        // acute corner does -- the extreme pixel of a box corner covers almost
        // none of its own area, so the last two never light up.
        check(gx0 >= ex0 - 1.0f && gx1 <= ex1 + 1.0f &&
              gy0 >= ey0 - 1.0f && gy1 <= ey1 + 1.0f,
              "bar3d: the GPU draws nothing outside the silhouette the projector describes");
        const float tol = 2.5f;
        check(gx0 - ex0 <= tol && ex1 - gx1 <= tol &&
              gy0 - ey0 <= tol && ey1 - gy1 <= tol,
              "bar3d: and fills it to within a corner's worth of it, so it is the same box");
        std::printf("  bar silhouette px [%d,%d]x[%d,%d] vs projected [%.1f,%.1f]x[%.1f,%.1f]\n",
                    gx0, gx1, gy0, gy1, ex0, ex1, ey0, ey1);
    }

    // The invariance rule at the level the user sees it: every number is at
    // exactly the orthographic size, under a projection that scales everything
    // else. Against the plan's own count rather than against the orthographic
    // file's, because the two do *not* agree on how many labels there are --
    // thinning is per projected edge length, so a foreshortened perspective
    // edge legitimately keeps fewer numbers than the same edge under ortho.
    check(count_of(svg_p, size_attr) == static_cast<int>(plan_p.tick_labels.size()) &&
          !plan_p.tick_labels.empty(),
          "3D perspective: with every tick label still at exactly the orthographic size");
}

}  // namespace lt
