// Box zoom and navigation history: box_pixels()/box_limits()
// on linear, log, reversed, thin and clipped boxes and the aspect growth; the
// history's gestures, Back/Forward/Home, stale stamps, Home re-taken, the cap,
// twins and a 3D camera; and the whole path through PlotView::draw() with
// ImGui input and no window.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "render_frame.h"
#include "coord_transform.h"
#include "widgets/nav_history.h"
#include "figure_impl.h"

namespace lt {
    using namespace sextant;

    namespace {
        bool near(double a, double b, double tol = 1e-9) {
            return std::abs(a - b) <= tol * std::max(1.0, std::abs(b));
        }

        // A 400 x 200 frame at (50, 20) over x 0..10, y 0..100.
        CoordTransform frame_tr() {
            CoordTransform tr{};
            tr.xmin = 0.0; tr.xmax = 10.0; tr.ymin = 0.0; tr.ymax = 100.0;
            tr.px = 50.0f; tr.py = 20.0f; tr.pw = 400.0f; tr.ph = 200.0f;
            return tr;
        }

        // Applies what the box holds to the snapshot, as the render thread's
        // drain does (no share groups here).
        void apply_drained(FigureSnapshot& fs, FigureEditBox& box) {
            if (auto d = box.load_and_clear()) {
                for (const auto& [k, e] : d->per_axes)
                    if (RenderSnapshot* s = fs.find2d(k)) apply_axes_edit(*s, e);
                for (const auto& [k, e] : d->per_axes3d)
                    if (RenderSnapshot3D* s = fs.find3d(k)) apply_axes3d_edit(*s, e);
                ++fs.generation;
            }
        }

        // Slot 1: a 2D line with a twinx; slot 2: a 3D axes. All automatic.
        FigureSnapshot twin_and_box() {
            FigureSnapshot fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
            fs.axes[0].slot = AxesSlot{1, 2, 1};
            RenderSnapshot twin = *fs.axes[0].snap2d();
            fs.axes[0].twins.push_back({kLayerTwinX, std::move(twin)});
            RenderSnapshot3D r;
            r.camera.azimuth = 10.0;
            r.default_camera = r.camera;
            fs.axes.push_back({AxesSlot{1, 2, 2}, std::move(r)});
            fs.generation = fs.data_generation = 1;
            return fs;
        }

        // A pan as the plot view pushes it: explicit limits on both axes.
        void push_limits(FigureEditBox& box, int key, double x0, double x1, double y0, double y1,
                         const LimitStamps& seen) {
            box.update(key, [&](AxesEdit& e) {
                e.xmin = x0; e.xmax = x1; e.xlim_auto = false;
                e.ymin = y0; e.ymax = y1; e.ylim_auto = false;
                e.lim_seen = seen;
            });
        }
    } // namespace

    // -------------------------------------------------------------------------
    // box_pixels() / box_limits()
    // -------------------------------------------------------------------------
    void test_box_zoom() {
        std::printf("\n[box zoom: box_pixels / box_limits]\n");

        const CoordTransform tr = frame_tr();
        auto limits = [&](const CoordTransform& t, float ax, float ay, float bx, float by,
                          bool keep = false) -> std::optional<AxisLimits> {
            const auto b = box_pixels(t, ax, ay, bx, by, keep);
            if (!b) return std::nullopt;
            return box_limits(t, *b);
        };

        // x 150..250 is 0.25..0.5 of the frame; y 70..170 is 0.75..0.25 from the top.
        const auto a = limits(tr, 150.0f, 70.0f, 250.0f, 170.0f);
        check(a && near(a->xmin, 2.5) && near(a->xmax, 5.0) && near(a->ymin, 25.0) && near(a->ymax, 75.0),
              "box: the box's share of the frame becomes the limits");
        const auto a2 = limits(tr, 250.0f, 170.0f, 150.0f, 70.0f);
        check(a2 && a2->xmin == a->xmin && a2->xmax == a->xmax && a2->ymin == a->ymin && a2->ymax == a->ymax,
              "box: dragged either way, the same box");

        const auto c = limits(tr, 0.0f, 0.0f, 250.0f, 170.0f);
        check(c && near(c->xmin, 0.0) && near(c->xmax, 5.0) && near(c->ymax, 100.0) && near(c->ymin, 25.0),
              "box: clipped to the frame");

        const auto thin_x = limits(tr, 150.0f, 100.0f, 250.0f, 103.0f);
        check(thin_x && near(thin_x->xmin, 2.5) && near(thin_x->xmax, 5.0)
              && thin_x->ymin == 0.0 && thin_x->ymax == 100.0,
              "box: a thin horizontal box zooms x only");
        const auto thin_y = limits(tr, 150.0f, 70.0f, 153.0f, 170.0f);
        check(thin_y && thin_y->xmin == 0.0 && thin_y->xmax == 10.0
              && near(thin_y->ymin, 25.0) && near(thin_y->ymax, 75.0),
              "box: a thin vertical box zooms y only");
        check(!box_pixels(tr, 150.0f, 100.0f, 154.0f, 104.0f, false),
              "box: short on both sides is a click, no zoom");
        check(!box_pixels(tr, 150.0f, 100.0f, 154.0f, 104.0f, true),
              "box: (with a ratio too)");

        CoordTransform lg = tr;
        lg.xmin = 1.0; lg.xmax = 1.0e4; lg.xs.kind = Scale::Log;
        const auto l = limits(lg, 150.0f, 70.0f, 250.0f, 170.0f);
        check(l && near(l->xmin, 10.0) && near(l->xmax, 100.0),
              "box: on a log axis the box's share is in decades");

        CoordTransform sl = tr;
        sl.xmin = -100.0; sl.xmax = 100.0; sl.xs.kind = Scale::Symlog;
        const auto s = limits(sl, 50.0f, 70.0f, 250.0f, 170.0f);
        check(s && near(s->xmin, -100.0) && std::abs(s->xmax) < 1e-9,
              "box: symlog, the left half of the frame ends at 0");

        CoordTransform rv = tr;
        rv.xmin = 10.0; rv.xmax = 0.0;
        const auto r = limits(rv, 150.0f, 70.0f, 250.0f, 170.0f);
        check(r && near(r->xmin, 7.5) && near(r->xmax, 5.0),
              "box: reversed limits stay reversed");

        // The frame is 2:1; a square box grows to 200 x 100 about its centre.
        const auto k = limits(tr, 150.0f, 70.0f, 250.0f, 170.0f, true);
        check(k && near(k->xmin, 1.25) && near(k->xmax, 6.25) && near(k->ymin, 25.0) && near(k->ymax, 75.0),
              "box: with a ratio set, grown to the frame's shape");
        check(k && near((k->xmax - k->xmin) / 10.0, (k->ymax - k->ymin) / 100.0),
              "box: (both axes zoom by the same share, so the ratio holds)");
        const auto kt = limits(tr, 150.0f, 100.0f, 250.0f, 103.0f, true);
        check(kt && near((kt->xmax - kt->xmin) / 10.0, (kt->ymax - kt->ymin) / 100.0)
              && kt->ymax - kt->ymin < 100.0,
              "box: with a ratio a thin box zooms both axes");
    }

    // -------------------------------------------------------------------------
    // NavHistory
    // -------------------------------------------------------------------------
    void test_nav_history() {
        std::printf("\n[navigation history: back / forward / home]\n");

        // ---- Gestures.
        {
            NavHistory h;
            check(!nav_gesture_starts(h, NavInput{.drag = true}),
                  "gesture: a held press that has not moved records nothing");
            check(nav_gesture_starts(h, NavInput{.drag = true, .moved = true}),
                  "gesture: a drag starts at its first moving frame");
            check(!nav_gesture_starts(h, NavInput{.drag = true, .moved = true}),
                  "gesture: and records once");
            nav_gesture_starts(h, NavInput{});
            check(nav_gesture_starts(h, NavInput{.drag = true, .moved = true}),
                  "gesture: the next drag is another");
            nav_gesture_starts(h, NavInput{});

            check(nav_gesture_starts(h, NavInput{.wheel = true, .slot = 1, .time = 10.0}),
                  "gesture: a first notch starts a wheel run");
            check(!nav_gesture_starts(h, NavInput{.wheel = true, .slot = 1, .time = 10.4}),
                  "gesture: a notch within 0.5 s continues it");
            check(!nav_gesture_starts(h, NavInput{.slot = 1, .time = 10.6}),
                  "gesture: (frames without a notch change nothing)");
            check(!nav_gesture_starts(h, NavInput{.wheel = true, .slot = 1, .time = 10.8}),
                  "gesture: the gap counts from the last notch");
            check(nav_gesture_starts(h, NavInput{.wheel = true, .slot = 1, .time = 11.5}),
                  "gesture: a notch after 0.5 s starts another");
            check(nav_gesture_starts(h, NavInput{.wheel = true, .slot = 2, .time = 11.6}),
                  "gesture: a notch over another cell starts another");

            check(nav_gesture_starts(h, NavInput{.fly = true}), "gesture: a fly key starts a run");
            check(!nav_gesture_starts(h, NavInput{.fly = true}), "gesture: held, it continues");
            nav_gesture_starts(h, NavInput{});
            check(nav_gesture_starts(h, NavInput{.fly = true}), "gesture: let go and pressed, another");
            nav_gesture_starts(h, NavInput{});
            check(nav_gesture_starts(h, NavInput{.box_done = true}), "gesture: a box records");
        }

        FigureSnapshot fs = twin_and_box();
        FigureEditBox box;
        FigureWindowState st;
        FigureContext ctx = panel_ctx(fs, box, st);
        NavHistory& h = st.plot.nav;
        const int twin = axes_key(1, kLayerTwinX);
        const auto lim = [&](int key) { return fs.find2d(key); };

        // ---- Home, then a pan and Back.
        nav_track_home(h, fs);
        check(h.have_home && at_home(h, fs) && !can_back(h) && !can_forward(h),
              "history: Home is the first view; nothing to go back or forward to");
        check(!nav_home(h, ctx), "history: Home at Home pushes nothing");

        nav_begin_gesture(h, fs);
        push_limits(box, 1, 1.0, 2.0, 3.0, 4.0, lim(1)->limit_stamps);
        push_limits(box, twin, 1.0, 2.0, 30.0, 40.0, lim(twin)->limit_stamps);
        apply_drained(fs, box);
        check(can_back(h) && !at_home(h, fs) && !lim(1)->xlim_auto,
              "history: a gesture records the view before it");

        check(nav_back(h, ctx), "history: Back pushes edits");
        check(st.slot_view.xauto_local && st.slot_view.yauto_local,
              "history: the slot view's locals follow at once");
        apply_drained(fs, box);
        check(lim(1)->xlim_auto && lim(1)->ylim_auto && lim(twin)->xlim_auto && lim(twin)->ylim_auto,
              "history: Back restores every layer, twins included");
        check(at_home(h, fs) && !can_back(h) && can_forward(h),
              "history: back at Home, Forward available");

        check(nav_forward(h, ctx), "history: Forward pushes edits");
        apply_drained(fs, box);
        check(!lim(1)->xlim_auto && lim(1)->xmin == 1.0 && lim(1)->ymax == 4.0 && lim(twin)->ymin == 30.0,
              "history: Forward puts the panned view back");
        check(can_back(h) && !can_forward(h), "history: and the stacks swap");

        // ---- Home records itself, so Back undoes it.
        check(nav_home(h, ctx), "history: Home pushes edits");
        apply_drained(fs, box);
        check(lim(1)->xlim_auto && at_home(h, fs) && h.back.size() == 2 && !can_forward(h),
              "history: Home restores the first view and is recorded");
        nav_back(h, ctx);
        apply_drained(fs, box);
        check(!lim(1)->xlim_auto && lim(1)->xmin == 1.0, "history: Back undoes Home");

        // ---- A new gesture clears Forward.
        nav_back(h, ctx);
        apply_drained(fs, box);
        check(can_forward(h), "history: (Forward is available)");
        nav_begin_gesture(h, fs);
        check(!can_forward(h), "history: a new gesture clears Forward");

        // ---- The program sets x: Home takes it, and Back leaves x alone.
        {
            FigureSnapshot f2 = twin_and_box();
            FigureEditBox b2;
            FigureWindowState s2;
            FigureContext c2 = panel_ctx(f2, b2, s2);
            NavHistory& n2 = s2.plot.nav;
            nav_track_home(n2, f2);
            nav_begin_gesture(n2, f2);
            push_limits(b2, 1, 1.0, 2.0, 3.0, 4.0, f2.find2d(1)->limit_stamps);
            apply_drained(f2, b2);

            // set_xlim(100, 200) as a refresh() publishes it: a new stamp.
            RenderSnapshot& s = *f2.find2d(1);
            s.xmin = 100.0; s.xmax = 200.0; s.xlim_auto = false; s.limit_stamps.x = 5;
            nav_track_home(n2, f2);
            const NavLimits* hx = nullptr;
            for (const NavLimits& l : n2.home.limits) if (l.key == 1) hx = &l;
            check(hx && !hx->xauto && hx->xmin == 100.0 && hx->xmax == 200.0 && hx->yauto,
                  "history: an axis the program sets is re-taken into Home; the other stays");

            nav_back(n2, c2);
            apply_drained(f2, b2);
            check(s.xmin == 100.0 && s.xmax == 200.0 && !s.xlim_auto && s.ylim_auto,
                  "history: Back skips an axis the program set since; the other is restored");

            // The user pans away again; Home goes to the program's x.
            push_limits(b2, 1, 1.0, 2.0, 3.0, 4.0, s.limit_stamps);
            apply_drained(f2, b2);
            nav_home(n2, c2);
            apply_drained(f2, b2);
            check(s.xmin == 100.0 && s.xmax == 200.0 && !s.xlim_auto && s.ylim_auto,
                  "history: Home is what the program last asked for");
        }

        // ---- The 3D camera, and Home for one cell.
        {
            FigureSnapshot f3 = twin_and_box();
            FigureEditBox b3;
            FigureWindowState s3;
            FigureContext c3 = panel_ctx(f3, b3, s3);
            NavHistory& n3 = s3.plot.nav;
            nav_track_home(n3, f3);
            nav_begin_gesture(n3, f3);
            b3.update3d(2, [&](AxesEdit3D& e) {
                Camera3D cam = f3.find3d(2)->camera;
                cam.azimuth = 80.0;
                e.camera = cam;
                e.camera_seen = f3.find3d(2)->camera_stamp;
            });
            push_limits(b3, 1, 1.0, 2.0, 3.0, 4.0, f3.find2d(1)->limit_stamps);
            apply_drained(f3, b3);
            check(f3.find3d(2)->camera.azimuth == 80.0 && !at_home(n3, f3, 2) && !at_home(n3, f3, 1),
                  "history: (an orbit and a pan, in one gesture)");

            nav_home(n3, c3, 2);
            apply_drained(f3, b3);
            check(f3.find3d(2)->camera.azimuth == 10.0 && !f3.find2d(1)->xlim_auto,
                  "history: Home for one cell restores only that cell");
            check(at_home(n3, f3, 2) && !at_home(n3, f3), "history: (that cell is at Home)");
            nav_back(n3, c3);
            apply_drained(f3, b3);
            check(f3.find3d(2)->camera.azimuth == 80.0, "history: Back undoes it: the camera");

            // A camera the program set since is the program's.
            f3.find3d(2)->camera.azimuth = 45.0;
            f3.find3d(2)->camera_stamp = 9;
            nav_back(n3, c3);
            apply_drained(f3, b3);
            check(f3.find3d(2)->camera.azimuth == 45.0 && f3.find2d(1)->xlim_auto,
                  "history: Back skips a camera the program set since");
        }

        // ---- set_default_camera() names Home's camera, whatever set_camera() said.
        {
            FigureSnapshot f4 = twin_and_box();
            RenderSnapshot3D& r = *f4.find3d(2);
            r.default_camera.azimuth = 120.0;
            r.default_camera_set = true;
            FigureEditBox b4;
            FigureWindowState s4;
            FigureContext c4 = panel_ctx(f4, b4, s4);
            NavHistory& n4 = s4.plot.nav;
            check(!at_home(n4, f4, 2), "history: with a default camera, the first view is not Home");
            nav_home(n4, c4, 2);
            apply_drained(f4, b4);
            check(r.camera.azimuth == 120.0 && at_home(n4, f4, 2),
                  "history: Home goes to set_default_camera()'s camera");
            // The program's set_view() after it does not move Home.
            r.camera.azimuth = 33.0;
            r.camera_stamp = 4;
            check(!at_home(n4, f4, 2), "history: a later set_camera() is not Home");
            nav_home(n4, c4, 2);
            apply_drained(f4, b4);
            check(r.camera.azimuth == 120.0, "history: Home is still the default camera");
            nav_back(n4, c4);
            apply_drained(f4, b4);
            check(r.camera.azimuth == 33.0, "history: and Back returns to the program's camera");
        }
        {
            auto fig = Figure::create({.width = 200, .height = 200});
            auto a = fig->add_subplot3d(1, 1, 1);
            a->set_default_camera(Camera3D{.azimuth = 50.0});
            auto& fi = detail::FigureAccess::impl(*fig);
            const RenderSnapshot3D sn = fi.find_slot_impl3d(1)->build_snapshot();
            check(sn.default_camera_set && sn.default_camera.azimuth == 50.0,
                  "history: set_default_camera() reaches the snapshot, flagged");
            a->cla();
            check(!fi.find_slot_impl3d(1)->build_snapshot().default_camera_set, "history: cla() clears the flag");
        }

        // ---- The cap.
        {
            NavHistory n;
            for (int i = 0; i < 150; ++i) nav_begin_gesture(n, fs);
            check(n.back.size() == NavHistory::kMaxBack, "history: Back keeps at most 100 views");
        }
    }

    // -------------------------------------------------------------------------
    // Through PlotView::draw(): Shift-drag, Esc, a pan, a double-click
    // -------------------------------------------------------------------------
    void test_box_zoom_view() {
        std::printf("\n[box zoom: through the plot view]\n");

        GLContext gl({.width = 64, .height = 64, .title = "layout_test", .visible = false});
        RenderDevice dev;
        PlotView view;

        FigureSnapshot fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
        fs.generation = fs.data_generation = 1;
        FigureEditBox box;
        FigureWindowState st;
        st.plot.navigate_enabled = true;

        ImGuiContext* ictx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ictx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(900.0f, 700.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();
        io.AddMousePosEvent(-1000.0f, -1000.0f);

        FigureContext ctx = panel_ctx(fs, box, st);
        ImVec2 origin;
        auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f));
            ImGui::Begin("Host");
            push_figure_id(ctx.figure_id);
            st.plot_info = view.draw(ctx, dev, st.plot, PlotViewParams{ .size = ImVec2(420.0f, 300.0f) });
            origin = ImGui::GetItemRectMin();
            ImGui::PopID();
            ImGui::End();
            ImGui::Render();
        };
        // One input change per frame, as ImGui's queue trickles them anyway.
        auto move = [&](float x, float y) { io.AddMousePosEvent(origin.x + x, origin.y + y); frame(); };
        auto button = [&](bool down) { io.AddMouseButtonEvent(0, down); frame(); };
        // Past the double-click time, so the next press is a single click.
        auto idle = [&] { for (int i = 0; i < 40; ++i) frame(); };
        auto shift = [&](bool down) {
            io.AddKeyEvent(ImGuiMod_Shift, down);
            io.AddKeyEvent(ImGuiKey_LeftShift, down);
            frame();
        };
        auto drained = [&]() -> std::optional<AxesEdit> {
            auto d = box.load_and_clear();
            if (!d || d->per_axes.empty()) return std::nullopt;
            AxesEdit e = d->per_axes[0].second;
            apply_axes_edit(*fs.find2d(1), e);
            ++fs.generation;
            return e;
        };

        frame();
        frame();
        const CoordTransform tr =
            axes_layouts(compute_figure_layout(fs, *st.plot_info.measure, 420, 300))[0].tr;
        const float ax = tr.px + 0.2f * tr.pw, ay = tr.py + 0.3f * tr.ph;
        const float bx = tr.px + 0.6f * tr.pw, by = tr.py + 0.8f * tr.ph;
        drained();

        // ---- Shift-drag: the box's limits, one history entry.
        move(ax, ay);
        shift(true);
        button(true);
        move(0.5f * (ax + bx), 0.5f * (ay + by));
        check(st.plot.box.active, "box view: a Shift press on the selected 2D cell starts a box");
        move(bx, by);
        button(false);
        shift(false);
        // ImGui floors the mouse position: the corners the view saw are within a
        // pixel of the intended ones, and the limits are those corners'.
        const PlotViewState::BoxDrag seen = st.plot.box;
        check(std::abs(seen.x0 - ax) <= 1.0f && std::abs(seen.y0 - ay) <= 1.0f &&
              std::abs(seen.x1 - bx) <= 1.0f && std::abs(seen.y1 - by) <= 1.0f,
              "box view: the corners are the press and the release");
        const auto want = box_limits(tr, *box_pixels(tr, seen.x0, seen.y0, seen.x1, seen.y1, false));
        const auto e = drained();
        check(e && e->xmin && e->xmax && e->ymin && e->ymax
              && near(*e->xmin, want.xmin, 1e-6) && near(*e->xmax, want.xmax, 1e-6)
              && near(*e->ymin, want.ymin, 1e-6) && near(*e->ymax, want.ymax, 1e-6)
              && e->xlim_auto == false && e->ylim_auto == false,
              "box view: the release applies box_limits() of the dragged box");
        check(st.plot.nav.back.size() == 1 && !st.plot.box.active,
              "box view: one history entry; the box is over");
        check(st.slot_view.xmin_local == want.xmin && !st.slot_view.xauto_local,
              "box view: the Limits fields follow");

        // ---- Esc cancels.
        idle();
        const CoordTransform tr2 =
            axes_layouts(compute_figure_layout(fs, *st.plot_info.measure, 420, 300))[0].tr;
        move(ax, ay);
        shift(true);
        button(true);
        move(bx, by);
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        frame();
        move(ax, by);
        button(false);
        shift(false);
        const auto none = drained();
        check(!(none && none->xmin), "box view: Esc cancels the box");
        check(st.plot.nav.back.size() == 1 && !st.plot.box.active,
              "box view: (the rest of the drag pans nothing, and nothing is recorded)");

        // ---- Without Shift the same drag pans.
        idle();
        move(ax, ay);
        button(true);
        move(ax + 10.0f, ay);
        move(bx, by);
        button(false);
        const auto pan = drained();
        check(pan && pan->xmin && pan->xmax
              && near(*pan->xmax - *pan->xmin, tr2.xmax - tr2.xmin, 1e-6) && !st.plot.box.active,
              "box view: without Shift it pans (the span holds)");
        check(st.plot.nav.back.size() == 2, "box view: the pan is a second entry");

        // ---- A double-click goes Home: automatic limits again.
        idle();
        move(ax, ay);
        button(true);
        button(false);
        button(true);
        button(false);
        const auto home = drained();
        check(home && home->xlim_auto == true && home->ylim_auto == true,
              "box view: a double-click goes Home (here, automatic)");
        check(st.plot.nav.back.size() == 3, "box view: recorded, so Back undoes it");

        ImGui::DestroyContext(ictx);
        ImGui::SetCurrentContext(nullptr);
    }
} // namespace lt
