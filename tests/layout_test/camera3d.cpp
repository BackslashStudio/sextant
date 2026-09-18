// Camera3D: navigation, the fit, perspective, and the edit lane. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    // Navigation as arithmetic (coord_transform3d.h is window-free).
    void test_camera_navigation() {
        std::printf("\n[3D: navigation]\n");

        using namespace sextant;

        // ---- Orbit: the content follows the cursor, so the camera moves the
        // other way.
        const Camera3D base; // azimuth -60, elevation 30
        const Camera3D right_drag = orbit_camera(base, 10.0f, 0.0f);
        const Camera3D down_drag = orbit_camera(base, 0.0f, 10.0f);
        check(right_drag.azimuth < base.azimuth,
              "3D nav: dragging right turns the box with the cursor, not against it");
        check(down_drag.elevation > base.elevation,
              "3D nav: dragging down lifts the camera, so the top face swings down");
        check(std::fabs(right_drag.azimuth - (base.azimuth - 10.0 * kOrbitSensitivity)) < 1e-12,
              "3D nav: and by exactly the sensitivity, per pixel");
        check(right_drag.elevation == base.elevation && down_drag.azimuth == base.azimuth,
              "3D nav: the two axes of the drag are independent");

        // Elevation clamps before the basis collapses.
        Camera3D far_up = base;
        for (int i = 0; i < 200; ++i) far_up = orbit_camera(far_up, 0.0f, 10.0f);
        check(far_up.elevation == 89.0, "3D nav: a long upward drag stops at the pole");

        // ---- Scroll zooms per notch, bounded.
        check(std::fabs(zoom_camera(base, 1.0f).zoom - base.zoom * kZoomPerNotch) < 1e-12 &&
              std::fabs(zoom_camera(base, -1.0f).zoom - base.zoom / kZoomPerNotch) < 1e-12,
              "3D nav: one wheel notch is one multiplier, either way");
        Camera3D spun = base;
        for (int i = 0; i < 500; ++i) spun = zoom_camera(spun, 1.0f);
        check(spun.zoom == 100.0, "3D nav: and it clamps, so a slipped wheel is recoverable");

        // ---- Fly: A/D strafe along the camera's right vector; Q/E move along
        // world z.
        const Vec3 right{0.0, 1.0, 0.0};
        const Vec3 fwd{-1.0, 0.0, 0.0};
        FlyInput in;
        in.dt = 0.5;

        in.right = true;
        const Camera3D strafed = fly_camera(base, right, fwd, in);
        check(std::fabs(strafed.target.y - kFlySpeed * 0.5) < 1e-12 &&
              strafed.target.x == 0.0 && strafed.target.z == 0.0,
              "3D nav: D moves the target along the camera's right, by speed x dt");
        in.right = false;

        in.up = true;
        const Camera3D lifted = fly_camera(base, right, fwd, in);
        check(std::fabs(lifted.target.z - kFlySpeed * 0.5) < 1e-12 && lifted.target.y == 0.0,
              "3D nav: E moves along world up, not camera up");
        in.up = false;

        // W/S under orthographic drive zoom (a dolly would do nothing).
        in.forward = true;
        const Camera3D dollied = fly_camera(base, right, fwd, in);
        check(dollied.zoom > base.zoom && dollied.target.x == 0.0 &&
              dollied.target.y == 0.0 && dollied.target.z == 0.0,
              "3D nav: W zooms under an orthographic camera rather than doing nothing");
        in.back = true;
        check(fly_camera(base, right, fwd, in).zoom == base.zoom,
              "3D nav: W and S together cancel");
        in.forward = in.back = false;

        check(fly_camera(base, right, fwd, in).target.x == base.target.x &&
              fly_camera(base, right, fwd, in).zoom == base.zoom,
              "3D nav: no keys held is no movement");

        // Movement is per second, not per frame.
        FlyInput slow = in, fast = in;
        slow.right = fast.right = true;
        slow.dt = 0.1;
        fast.dt = 0.2;
        check(std::fabs(fly_camera(base, right, fwd, fast).target.y
                        - 2.0 * fly_camera(base, right, fwd, slow).target.y) < 1e-12,
              "3D nav: fly speed is per second, so it does not depend on the frame rate");

        // ---- The clamp every path shares.
        Camera3D bad;
        bad.zoom = 0.0;
        check(clamp_camera(bad).zoom == 1.0, "3D nav: a zero zoom falls back rather than dividing by zero");
        bad.zoom = std::numeric_limits<double>::quiet_NaN();
        check(clamp_camera(bad).zoom == 1.0, "3D nav: and so does a non-finite one");
    }

    // The fit depends on the camera angle only; the target just translates
    // (panning must not zoom out).
    void test_camera_fit_and_pan() {
        std::printf("\n[3D: the fit under a moved target]\n");

        using namespace sextant;

        const PlotRect frame{0.0f, 0.0f, 240.0f, 200.0f};
        const Transform3D tf{0, 1, 0, 1, 0, 1, BoxAspect{}};

        Camera3D at_rest;
        Projector3D p0(tf, at_rest, frame, 0.1f);

        Camera3D panned = at_rest;
        const Vec3 right = p0.right();
        const double delta = 0.25;
        panned.target = right * delta;
        Projector3D p1(tf, panned, frame, 0.1f);

        check(std::fabs(p0.pixels_per_box_unit() - p1.pixels_per_box_unit()) < 1e-9,
              "3D fit: moving the target does not change the scale");

        // ...and translates the picture by the same amount.
        const double k = p0.pixels_per_box_unit();
        bool translated = true;
        for (int i = 0; i < 8; ++i) {
            const Vec3 c{(i & 1) ? 0.5 : -0.5, (i & 2) ? 0.5 : -0.5, (i & 4) ? 0.5 : -0.5};
            const Px3 a = p0.project_box(c), b = p1.project_box(c);
            if (std::fabs((a.x - b.x) - delta * k) > 1e-3 || std::fabs(a.y - b.y) > 1e-3)
                translated = false;
        }
        check(translated, "3D fit: it translates the picture by the distance moved, and only in x");

        // The angle does change the fit (so the check above isn't vacuous).
        Camera3D turned = at_rest;
        turned.azimuth += 45.0;
        Transform3D oblong = tf;
        oblong.aspect = BoxAspect{3.0, 1.0, 1.0};
        check(std::fabs(Projector3D(oblong, at_rest, frame, 0.1f).pixels_per_box_unit()
                        - Projector3D(oblong, turned, frame, 0.1f).pixels_per_box_unit()) > 1.0,
              "3D fit: but it does still answer to the camera angle");
    }

    // ---------------------------------------------------------------------------
    // Perspective, checked against hand arithmetic:
    //
    //   unit box, azimuth 0 / elevation 0  ->  eye on +x, right = +y, up = +z
    //   200x200 frame, no margin, fov 90   ->  half-angle 45 deg, tan = 1
    //   the binding corner is the near one: |v| / tan(45) - t = 0.5 + 0.5 = 1
    // ---------------------------------------------------------------------------
    void test_perspective_projection() {
        std::printf("\n[3D: perspective]\n");

        using namespace sextant;

        const PlotRect frame{0.0f, 0.0f, 200.0f, 200.0f};
        const Transform3D tf{0, 1, 0, 1, 0, 1, BoxAspect{}};
        constexpr float kCx = 100.0f, kCy = 100.0f;

        Camera3D cam;
        cam.azimuth = 0.0;
        cam.elevation = 0.0;
        cam.projection = Projection::Perspective;
        cam.fov = 90.0;
        const Projector3D p(tf, cam, frame, 0.0f);

        check(p.is_perspective(), "3D perspective: the projector knows which mode it is in");
        check(std::fabs(p.eye_distance() - 1.0) < 1e-9,
              "3D perspective: the eye distance is derived from the fov and the box, never stored");
        check(near_px(p.project_box({0.0, 0.0, 0.0}).x, kCx) &&
              near_px(p.project_box({0.0, 0.0, 0.0}).y, kCy),
              "3D perspective: the target still projects to the frame centre");

        // Tight at the binding (near) corner, which lands on the frame corner;
        // others are pulled in by their eye depth. Both written out explicitly.
        const Px3 near_c = p.project_box({0.5, 0.5, 0.5});
        const Px3 far_c = p.project_box({-0.5, 0.5, 0.5});
        check(near_px(near_c.x, 200.0f, 0.01f) && near_px(near_c.y, 0.0f, 0.01f),
              "3D perspective: at fov 90 the near corner lands exactly on the frame corner");
        check(near_px(far_c.x, kCx + 100.0f * 0.5f / 1.5f, 0.01f),
              "3D perspective: and the far one is pulled in by exactly the ratio of their depths");
        check(near_px(near_c.w, 0.5f, 1e-4f) && near_px(far_c.w, 1.5f, 1e-4f),
              "3D perspective: w is eye-space depth, so the two faces are one box apart");

        // Foreshortening: face width is inversely proportional to depth.
        auto face_width = [](const Projector3D& pr, double x) {
            return pr.project_box({x, 0.5, 0.0}).x - pr.project_box({x, -0.5, 0.0}).x;
        };
        check(near_px(face_width(p, 0.5) / face_width(p, -0.5), 3.0f, 1e-3f),
              "3D perspective: the two faces are in the ratio of their eye depths, 1.5 : 0.5");

        // Orthographic control: w is constant (clips are no-ops).
        Camera3D ocam = cam;
        ocam.projection = Projection::Orthographic;
        const Projector3D po(tf, ocam, frame, 0.0f);
        check(po.project_box({0.5, 0.5, 0.5}).w == 1.0f &&
              po.project_box({-0.5, 0.5, 0.5}).w == 1.0f,
              "3D: an orthographic camera has no divisor, so w is 1 and nothing is behind the eye");
        check(near_px(face_width(po, 0.5), face_width(po, -0.5)),
              "3D: and no foreshortening -- the near and far faces are the same width");
        check(po.eye_distance() == 0.0 && p.pixels_per_box_unit() == 0.0,
              "3D: neither mode reports the number only the other one has");

        // Zoom magnifies the finished picture: offsets scale, perspective doesn't.
        Camera3D zoomed = cam;
        zoomed.zoom = 2.0;
        const Projector3D pz(tf, zoomed, frame, 0.0f);
        bool magnified = true;
        for (int i = 0; i < 8; ++i) {
            const Vec3 c{(i & 1) ? 0.5 : -0.5, (i & 2) ? 0.5 : -0.5, (i & 4) ? 0.5 : -0.5};
            const Px3 a = p.project_box(c), b = pz.project_box(c);
            if (!near_px(b.x - kCx, 2.0f * (a.x - kCx), 0.01f) ||
                !near_px(b.y - kCy, 2.0f * (a.y - kCy), 0.01f))
                magnified = false;
        }
        check(magnified, "3D perspective: zoom scales every offset from the frame centre, exactly");
        check(near_px(face_width(pz, 0.5) / face_width(pz, -0.5), 3.0f, 1e-3f),
              "3D perspective: and leaves the amount of perspective alone -- it is not a dolly");

        // A dolly moves the target along the view; the derived distance doesn't
        // change, so the approach isn't undone.
        Camera3D dollied = cam;
        dollied.target = p.forward() * 0.25;
        const Projector3D pd(tf, dollied, frame, 0.0f);
        check(std::fabs(pd.eye_distance() - p.eye_distance()) < 1e-9,
              "3D perspective: a dolly does not move the derived distance -- the fit sees only the angle");
        check(near_px(face_width(pd, 0.5) / face_width(pd, -0.5), 5.0f, 1e-3f),
              "3D perspective: so it is a real approach -- a quarter box closer takes the ratio 3 to 5");

        // ...and W/S is wired to the dolly under perspective.
        const Vec3 right{0.0, 1.0, 0.0}, fwd{-1.0, 0.0, 0.0};
        FlyInput in;
        in.dt = 0.5;
        in.forward = true;
        const Camera3D flown = fly_camera(cam, right, fwd, in);
        check(flown.zoom == cam.zoom &&
              std::fabs(flown.target.x - fwd.x * kFlySpeed * 0.5) < 1e-12,
              "3D nav: under perspective W dollies along the view direction and leaves zoom alone");
        Camera3D ortho_cam = cam;
        ortho_cam.projection = Projection::Orthographic;
        const Camera3D ortho_flown = fly_camera(ortho_cam, right, fwd, in);
        check(ortho_flown.zoom > ortho_cam.zoom && ortho_flown.target.x == 0.0,
              "3D nav: and under orthographic it still zooms, where a dolly would be invisible");

        // FOV is bounded on both sides.
        Camera3D wide = cam, narrow = cam;
        wide.fov = 110.0;
        narrow.fov = 15.0;
        const Projector3D pw(tf, wide, frame, 0.0f), pn(tf, narrow, frame, 0.0f);
        check(pw.eye_distance() < p.eye_distance() && pn.eye_distance() > p.eye_distance(),
              "3D perspective: a wider fov is a closer camera, a narrower one further away");
        check(face_width(pw, 0.5) / face_width(pw, -0.5) >
              face_width(pn, 0.5) / face_width(pn, -0.5),
              "3D perspective: so fov is how much perspective there is, and nothing else");
        Camera3D absurd = cam;
        absurd.fov = 400.0;
        check(clamp_camera(absurd).fov == kMaxFov, "3D perspective: fov clamps at the top");
        absurd.fov = 0.0;
        check(clamp_camera(absurd).fov == kMinFov, "3D perspective: and at the bottom");
        absurd.fov = std::numeric_limits<double>::quiet_NaN();
        check(clamp_camera(absurd).fov == 45.0, "3D perspective: a non-finite fov falls back");
        // ...and the projector clamps a directly built Camera3D too (no NaN).
        const Projector3D pnan(tf, absurd, frame, 0.0f);
        check(std::isfinite(pnan.project_box({0.25, 0.25, 0.25}).x) &&
              std::isfinite(pnan.eye_distance()),
              "3D perspective: and a projector built from one still projects finite pixels");

        // The box fits its cell under perspective at every angle and fov (an
        // oblong box, so the binding corner varies).
        const Transform3D oblong{0, 1, 0, 1, 0, 1, BoxAspect{1.6, 1.0, 0.7}};
        const PlotRect wide_frame{20.0f, 10.0f, 260.0f, 180.0f};
        const float m = 0.1f;
        const float ix = wide_frame.x + wide_frame.w * m, iy = wide_frame.y + wide_frame.h * m;
        const float iw = wide_frame.w * (1.0f - 2.0f * m), ih = wide_frame.h * (1.0f - 2.0f * m);
        bool inside = true, tight = false;
        for (const double fov: {20.0, 45.0, 90.0, 115.0}) {
            for (int ai = 0; ai < 8; ++ai) {
                for (int ei = -2; ei <= 2; ++ei) {
                    Camera3D c;
                    c.projection = Projection::Perspective;
                    c.fov = fov;
                    c.azimuth = ai * 45.0;
                    c.elevation = ei * 40.0;
                    const Projector3D pr(oblong, c, wide_frame, m);
                    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
                    for (int i = 0; i < 8; ++i) {
                        const Vec3 corner{
                            (i & 1) ? 0.8 : -0.8, (i & 2) ? 0.5 : -0.5,
                            (i & 4) ? 0.35 : -0.35
                        };
                        const Px3 q = pr.project_box(corner);
                        lo_x = std::min(lo_x, q.x);
                        hi_x = std::max(hi_x, q.x);
                        lo_y = std::min(lo_y, q.y);
                        hi_y = std::max(hi_y, q.y);
                    }
                    if (lo_x < ix - 0.5f || hi_x > ix + iw + 0.5f ||
                        lo_y < iy - 0.5f || hi_y > iy + ih + 0.5f)
                        inside = false;
                    if (std::fabs((hi_x - lo_x) - iw) < 0.5f ||
                        std::fabs((hi_y - lo_y) - ih) < 0.5f)
                        tight = true;
                }
            }
        }
        check(inside, "3D perspective: the box stays inside its reserved area at every angle and fov");
        check(tight, "3D perspective: and fills it -- the derived distance is tight, not merely safe");
    }

    // The near plane: a point behind the eye projects to the wrong side of the
    // picture, so geometry must be clipped.
    void test_perspective_near_clipping() {
        std::printf("\n[3D: behind the eye]\n");

        using namespace sextant;

        const PlotRect frame{0.0f, 0.0f, 200.0f, 200.0f};
        const Transform3D tf{0, 1, 0, 1, 0, 1, BoxAspect{}};
        constexpr float kCx = 100.0f;

        // The eye ends at x = -0.2 inside a -0.5..0.5 box: the +x half is behind.
        Camera3D cam;
        cam.azimuth = 0.0;
        cam.elevation = 0.0;
        cam.projection = Projection::Perspective;
        cam.fov = 90.0;
        const Projector3D p0(tf, cam, frame, 0.0f);
        cam.target = p0.forward() * 1.2;
        const Projector3D p(tf, cam, frame, 0.0f);

        check(p.in_front({-0.5, 0.0, 0.0}) && !p.in_front({0.5, 0.0, 0.0}),
              "3D near plane: the eye is inside the box, so half of it is behind the camera");
        check(p.project_box({0.5, 0.0, 0.0}).w < 0.0f,
              "3D near plane: and w says so -- that is what a consumer is meant to test");

        // A straddling segment is cut, keeping the front end.
        const Vec3 a{-0.5, 0.4, 0.0}; // in front
        const Vec3 b{0.5, 0.4, 0.0}; // behind
        Px3 ca{}, cb{};
        check(p.project_segment(a, b, ca, cb), "3D near plane: a straddling segment survives");
        check(ca.in_front() && cb.in_front(),
              "3D near plane: with both ends in front of the eye once it has been cut");
        check(cb.w < 0.01f && cb.w > 0.0f,
              "3D near plane: the cut end sits on the plane itself, not merely somewhere in front");

        // Unclipped, the behind-the-eye end would flip to screen left and the
        // line would cross the whole picture.
        const Px3 raw = p.project_box(b);
        check(ca.x > kCx && cb.x > kCx,
              "3D near plane: the clipped segment stays on the side of the picture it belongs to");
        check(raw.x < kCx,
              "3D near plane: where the unclipped point would have crossed to the other side");

        Px3 da{}, db{};
        check(!p.project_segment({0.4, 0.0, 0.0}, {0.5, 0.0, 0.0}, da, db),
              "3D near plane: a segment entirely behind the eye is dropped, not drawn");

        // Polygons: one Sutherland-Hodgman pass.
        std::vector<Px3> poly;
        p.project_polygon({
                              {0.5, -0.5, -0.5}, {0.5, 0.5, -0.5},
                              {0.5, 0.5, 0.5}, {0.5, -0.5, 0.5}
                          }, poly);
        check(poly.empty(), "3D near plane: a pane entirely behind the eye contributes nothing");

        p.project_polygon({
                              {-0.5, -0.5, -0.5}, {0.5, -0.5, -0.5},
                              {0.5, -0.5, 0.5}, {-0.5, -0.5, 0.5}
                          }, poly);
        bool all_front = poly.size() >= 3;
        for (const Px3& q: poly) if (!q.in_front()) all_front = false;
        check(all_front, "3D near plane: and a straddling one is cut down to the part that is");

        // The whole box plan: finite everywhere, with the ticks behind the eye
        // dropped (marks and numbers).
        RenderSnapshot3D snap;
        snap.xticks_override = std::vector<Tick>{
            {0.0, "0"}, {0.25, "a"}, {0.5, "b"},
            {0.75, "c"}, {1.0, "1"}
        };
        snap.yticks_override = snap.xticks_override;
        snap.zticks_override = snap.xticks_override;
        snap.xtitle = "X";
        snap.ytitle = "Y";
        snap.ztitle = "Z";

        RenderSnapshot3D outside = snap;
        outside.camera = Camera3D{};
        outside.camera.projection = Projection::Perspective;
        outside.camera.fov = 90.0;
        outside.camera.azimuth = 0.0;
        outside.camera.elevation = 0.0;
        snap.camera = cam;

        const Projector3D p_out(tf, outside.camera, frame, 0.0f);
        const Box3DPlan plan_out = plan_box3d(p_out, outside, *snap.xticks_override,
                                              *snap.yticks_override, *snap.zticks_override);
        const Box3DPlan plan_in = plan_box3d(p, snap, *snap.xticks_override,
                                             *snap.yticks_override, *snap.zticks_override);

        bool finite = true;
        for (const auto* g: {
                 &plan_in.panes, &plan_in.grid, &plan_in.axis_lines,
                 &plan_in.tick_marks
             })
            for (const auto& poly_in: *g)
                for (const float v: poly_in.xy)
                    if (!std::isfinite(v) || std::fabs(v) > 1e6f) finite = false;
        check(finite, "3D near plane: every coordinate the plan emits from inside the box is finite");

        // The x and z labelled edges survive (in front); the y edge is entirely
        // behind and dropped whole.
        check(plan_out.axis_lines.size() == 3 && plan_in.axis_lines.size() == 2,
              "3D near plane: the axis entirely behind the eye is dropped whole");
        check(plan_in.axis_titles.size() == 2,
              "3D near plane: and takes its title with it, rather than labelling nothing");

        // Surviving ticks counted independently from the projector (the dropped
        // y axis alone would make "fewer ticks" pass).
        std::size_t expect = 0;
        for (const Tick& t: *snap.xticks_override)
            if (p.in_front({tf.box_x(t.value), 0.5, -0.5})) ++expect;
        for (const Tick& t: *snap.zticks_override)
            if (p.in_front({-0.5, 0.5, tf.box_z(t.value)})) ++expect;
        check(expect > 0 && expect < plan_out.tick_marks.size(),
              "3D near plane: the camera really does put some of the ticks behind the eye");
        check(plan_in.tick_marks.size() == expect,
              "3D near plane: and the plan draws exactly the ones in front of it, no more");
        check(plan_in.tick_labels.size() <= plan_in.tick_marks.size(),
              "3D near plane: with no label outliving the mark it names");
    }

    // The camera's edit lane: it exists, merges, and the drain sees it.

    void test_camera_edit_lane() {
        std::printf("\n[3D: the camera on the edit channel]\n");

        using namespace sextant;

        FigureEditBox box;
        Camera3D cam;
        cam.azimuth = 12.0;
        box.update3d(2, [&](AxesEdit3D& e) { e.camera = cam; });

        // The drain tests FigureEdits::empty(), which must include the camera.
        auto drained = box.load_and_clear();
        check(drained.has_value(), "3D edits: a camera-only edit is not mistaken for an empty one");
        check(drained && drained->per_axes.empty() && drained->per_axes3d.size() == 1,
              "3D edits: and it arrives on the 3D lane, not the 2D one");
        check(drained && drained->per_axes3d[0].first == 2 &&
              drained->per_axes3d[0].second.camera &&
              drained->per_axes3d[0].second.camera->azimuth == 12.0,
              "3D edits: carrying the slot it was addressed to and the camera itself");
        check(!box.load_and_clear().has_value(), "3D edits: the drain is destructive");

        // Merging: a camera and a box style in the same drain both survive.
        box.update3d(1, [&](AxesEdit3D& e) { e.camera = cam; });
        box.update3d(1, [&](AxesEdit3D& e) { e.box_style = Box3DStyle{}; });
        box.update3d(3, [&](AxesEdit3D& e) { e.ztitle = "counts"; });
        auto merged = box.load_and_clear();
        check(merged && merged->per_axes3d.size() == 2,
              "3D edits: two slots stay two entries");
        check(merged && merged->per_axes3d[0].second.camera &&
              merged->per_axes3d[0].second.box_style,
              "3D edits: two fields of one slot accumulate rather than replacing each other");

        // Through the public API to the picture.
        auto fig = Figure::create({.width = 260, .height = 220, .supersample = 1});
        auto ax = fig->add_subplot3d(1, 1, 1);
        ax->set_view(-60.0, 30.0);
        fig->savefig("cam_a.png");
        ax->set_view(30.0, 30.0);
        fig->savefig("cam_b.png");
        ax->set_view(-60.0, 30.0);
        fig->savefig("cam_c.png");

        auto slurp = [](const char* p) {
            std::ifstream f(p, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        };
        const std::string a = slurp("cam_a.png"), b = slurp("cam_b.png"), c = slurp("cam_c.png");
        check(!a.empty() && a != b, "3D edits: set_view() reaches the rendered pixels");
        check(a == c, "3D edits: and returning to a camera returns to its picture exactly");
        check(ax->camera().azimuth == -60.0 && ax->camera().elevation == 30.0,
              "3D edits: camera() reads back what was set");
    }
} // namespace lt
