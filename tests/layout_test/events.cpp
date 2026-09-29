// Interactive events (Step 29): the per-figure channel that carries them from
// the window thread to whoever drains it, the GL-free translation of a frame of
// pointer/key input into events, and delivery through a real figure.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "event_channel.h"
#include "plot_events.h"
#include "key_names.h"
#include "window_link.h"
#include "platform/platform.h"
#include "renderer/plot_fbo.h"
#include "widgets/imgui_context.h"
#include <GLFW/glfw3.h>

namespace lt {
    using namespace sextant;

    namespace {
        Event ev(EventKind k, float x = 0.0f) {
            Event e;
            e.kind = k;
            e.x = x;
            return e;
        }

        // Every kind, as a mask.
        constexpr std::uint32_t kAll = (1u << 8) - 1;
    } // namespace

    // -------------------------------------------------------------------------
    // The channel
    // -------------------------------------------------------------------------
    void test_event_channel() {
        std::printf("\n[events: the channel]\n");

        // ---- What is wanted decides what is queued at all.
        {
            auto ch = EventChannel::create();
            check(ch->wanted_mask() == 0 && !ch->wants(EventKind::Scroll),
                  "events: nothing is wanted before a callback is connected");
            ch->push(ev(EventKind::Scroll));
            check(!ch->has_pending(), "events: an event nobody wants is not queued");

            const int id = ch->connect(EventKind::Scroll, [](const Event&) {});
            check(ch->wants(EventKind::Scroll) && !ch->wants(EventKind::KeyDown),
                  "events: connect() wants exactly its kind");
            ch->push(ev(EventKind::Scroll));
            check(ch->has_pending(), "events: a wanted event is queued");
            ch->disconnect(id);
            check(!ch->wants(EventKind::Scroll), "events: disconnect() stops wanting it");
            check(ch->dispatch() == 1 && !ch->has_pending(),
                  "events: what was queued before the disconnect is still taken");
            ch->disconnect(9999); // unknown id: ignored
        }

        // ---- Delivery is in order, per kind in connection order, on this thread.
        {
            auto ch = EventChannel::create();
            std::vector<std::string> log;
            std::thread::id where{};
            ch->connect(EventKind::MouseDown, [&](const Event& e) {
                log.push_back("down" + std::to_string(static_cast<int>(e.x)));
                where = std::this_thread::get_id();
            });
            ch->connect(EventKind::MouseDown, [&](const Event& e) {
                log.push_back("also" + std::to_string(static_cast<int>(e.x)));
            });
            ch->connect(EventKind::KeyDown, [&](const Event& e) { log.push_back("key" + e.key); });

            Event k = ev(EventKind::KeyDown);
            k.key = "a";
            ch->push(ev(EventKind::MouseDown, 1));
            ch->push(k);
            ch->push(ev(EventKind::MouseDown, 2));
            check(ch->dispatch() == 3, "events: dispatch() returns how many events it took");
            const std::vector<std::string> want{"down1", "also1", "keya", "down2", "also2"};
            check(log == want, "events: delivered in queue order, handlers in connection order");
            check(where == std::this_thread::get_id(), "events: on the draining thread");
            check(ch->dispatch() == 0, "events: nothing is delivered twice");
        }

        // ---- Coalescing.
        {
            auto ch = EventChannel::create();
            std::vector<Event> got;
            for (EventKind k : {EventKind::MouseMove, EventKind::Scroll, EventKind::Resize,
                                EventKind::MouseDown})
                ch->connect(k, [&](const Event& e) { got.push_back(e); });

            ch->push(ev(EventKind::MouseMove, 1));
            ch->push(ev(EventKind::MouseMove, 2));
            ch->push(ev(EventKind::MouseMove, 3));
            check(ch->dispatch() == 1 && got.size() == 1 && got[0].x == 3.0f,
                  "events: back-to-back moves collapse to the latest");

            got.clear();
            Event s1 = ev(EventKind::Scroll, 1);
            s1.scroll_y = 1.0;
            Event s2 = ev(EventKind::Scroll, 2);
            s2.scroll_y = 2.5;
            s2.scroll_x = 0.5;
            ch->push(s1);
            ch->push(s2);
            ch->dispatch();
            check(got.size() == 1 && got[0].scroll_y == 3.5 && got[0].scroll_x == 0.5 && got[0].x == 2.0f,
                  "events: back-to-back scrolls add their offsets and keep the latest position");

            got.clear();
            ch->push(ev(EventKind::MouseMove, 1));
            ch->push(ev(EventKind::MouseDown, 5));
            ch->push(ev(EventKind::MouseMove, 2));
            ch->dispatch();
            check(got.size() == 3 && got[0].kind == EventKind::MouseMove &&
                  got[1].kind == EventKind::MouseDown && got[2].kind == EventKind::MouseMove,
                  "events: a move does not merge across a click, which would reorder them");

            got.clear();
            Event r1 = ev(EventKind::Resize);
            r1.width = 10;
            Event r2 = ev(EventKind::Resize);
            r2.width = 20;
            ch->push(r1);
            ch->push(r2);
            ch->dispatch();
            check(got.size() == 1 && got[0].width == 20, "events: resizes collapse to the latest");
        }

        // ---- disconnect() from a callback, and a drain from inside one.
        {
            auto ch = EventChannel::create();
            int a_calls = 0, b_calls = 0, nested = -1;
            int b_id = 0;
            ch->connect(EventKind::KeyDown, [&](const Event&) {
                ++a_calls;
                ch->disconnect(b_id);
                nested = ch->dispatch();
            });
            b_id = ch->connect(EventKind::KeyDown, [&](const Event&) { ++b_calls; });
            ch->push(ev(EventKind::KeyDown));
            ch->push(ev(EventKind::KeyDown));
            ch->dispatch();
            check(a_calls == 2 && b_calls == 0,
                  "events: a handler disconnected by an earlier one is not called, even in the same batch");
            check(nested == 0, "events: a drain from inside a callback returns 0");
        }

        // ---- A throwing callback is reported and does not stop delivery.
        {
            std::vector<std::string> messages;
            const MessageHandler old = Figure::set_message_handler(
                [&](std::string_view m) { messages.emplace_back(m); });
            auto ch = EventChannel::create();
            int after = 0;
            ch->connect(EventKind::KeyDown, [](const Event&) { throw std::runtime_error("boom"); });
            ch->connect(EventKind::KeyDown, [&](const Event&) { ++after; });
            ch->push(ev(EventKind::KeyDown));
            ch->push(ev(EventKind::KeyDown));
            ch->dispatch();
            Figure::set_message_handler(old);
            check(after == 2, "events: a throwing callback does not stop the ones after it");
            check(messages.size() == 2 && messages[0].find("boom") != std::string::npos,
                  "events: and its message goes to the message handler");
        }

        // ---- Bounds: coalescible events go first, clicks and keys are kept.
        {
            auto ch = EventChannel::create();
            ch->connect(EventKind::MouseMove, [](const Event&) {});
            ch->connect(EventKind::KeyDown, [](const Event&) {});
            // Alternating, so nothing merges.
            for (std::size_t i = 0; i < EventChannel::kSoftCapacity; ++i)
                ch->push(ev(i % 2 == 0 ? EventKind::MouseMove : EventKind::KeyDown));
            check(ch->dropped() == 0, "events: nothing is dropped up to the soft bound");
            ch->push(ev(EventKind::KeyDown));
            check(ch->dropped() == 1, "events: past it, a queued move is dropped to make room for a key");
            check(ch->dispatch() == static_cast<int>(EventChannel::kSoftCapacity),
                  "events: the queue held exactly the bound");

            // Now a queue of keys only: a move has nothing to displace and is dropped.
            auto ch2 = EventChannel::create();
            ch2->connect(EventKind::MouseMove, [](const Event&) {});
            ch2->connect(EventKind::KeyDown, [](const Event&) {});
            for (std::size_t i = 0; i < EventChannel::kSoftCapacity; ++i)
                ch2->push(ev(EventKind::KeyDown));
            ch2->push(ev(EventKind::MouseMove));
            check(ch2->dropped() == 1, "events: a move is dropped when the queue is full of keys");
            ch2->push(ev(EventKind::KeyDown));
            check(ch2->dropped() == 1, "events: a key is still kept past the soft bound");
        }

        // ---- Pushing from another thread while this one drains.
        {
            auto ch = EventChannel::create();
            std::vector<int> seen;
            ch->connect(EventKind::MouseDown, [&](const Event& e) { seen.push_back(static_cast<int>(e.x)); });
            constexpr int kN = 3000;
            std::thread producer([&] {
                for (int i = 0; i < kN; ++i) ch->push(ev(EventKind::MouseDown, static_cast<float>(i)));
            });
            const auto t0 = std::chrono::steady_clock::now();
            while (static_cast<int>(seen.size()) < kN &&
                   std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10))
                ch->dispatch();
            producer.join();
            ch->dispatch();
            bool ordered = static_cast<int>(seen.size()) == kN;
            for (int i = 0; ordered && i < kN; ++i) ordered = seen[i] == i;
            check(ordered, "events: everything pushed from another thread arrives, in order");
        }
    }

    // -------------------------------------------------------------------------
    // Key names
    // -------------------------------------------------------------------------
    void test_event_key_names() {
        std::printf("\n[events: key names]\n");
        check(key_event_name(GLFW_KEY_A, 0) == "a", "key names: a letter is lower case");
        check(key_event_name(GLFW_KEY_A, ModShift) == "A", "key names: upper case with shift");
        check(key_event_name(GLFW_KEY_A, ModCtrl) == "ctrl+a", "key names: ctrl+a");
        check(key_event_name(GLFW_KEY_A, ModCtrl | ModShift) == "ctrl+A", "key names: ctrl+A");
        check(key_event_name(GLFW_KEY_LEFT, ModShift | ModAlt) == "alt+shift+left",
              "key names: modifiers in a fixed order, shift on a non-letter is spelled out");
        check(key_event_name(GLFW_KEY_ESCAPE, 0) == "escape", "key names: escape");
        check(key_event_name(GLFW_KEY_F5, 0) == "f5", "key names: a function key");
        check(key_event_name(GLFW_KEY_5, 0) == "5" && key_event_name(GLFW_KEY_KP_5, 0) == "5",
              "key names: a digit, keypad or not");
        check(key_event_name(GLFW_KEY_SPACE, 0) == "space", "key names: space");
        check(key_event_name(GLFW_KEY_LEFT_CONTROL, ModCtrl) == "ctrl",
              "key names: a modifier key's own event is the bare name, not ctrl+ctrl");
        check(key_event_name(GLFW_KEY_UNKNOWN, 0).empty(), "key names: an unknown key has none");

        Event e;
        check(make_key_event(GLFW_KEY_B, ModCtrl, true, e) && e.kind == EventKind::KeyDown &&
              e.key == "ctrl+b" && e.mods == kModCtrl,
              "key names: a press becomes a KeyDown carrying the name and mods");
        check(make_key_event(GLFW_KEY_B, 0, false, e) && e.kind == EventKind::KeyUp,
              "key names: a release becomes a KeyUp");
        check(!make_key_event(GLFW_KEY_UNKNOWN, 0, true, e), "key names: an unnamed key makes no event");
        check(static_cast<int>(kModCtrl) == static_cast<int>(ModCtrl) &&
              static_cast<int>(kModShift) == static_cast<int>(ModShift) &&
              static_cast<int>(kModAlt) == static_cast<int>(ModAlt) &&
              static_cast<int>(kModSuper) == static_cast<int>(ModSuper),
              "key names: the public modifier bits are the window link's");
    }

    // -------------------------------------------------------------------------
    // Frames of input to events
    // -------------------------------------------------------------------------
    void test_plot_events() {
        std::printf("\n[events: pointer input over the plot]\n");

        // Slot 1 is 2D, slot 2 is 3D with two planes.
        FigureSnapshot fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
        fs.axes[0].slot = AxesSlot{1, 2, 1};
        fs.axes.push_back({AxesSlot{1, 2, 2}, two_plane_snapshot()});

        const int W = 800, H = 400;
        const FigureLayout fl = compute_figure_layout(fs, W, H);
        std::vector<AxesLayout> layout;
        for (const CellLayout& c: fl.cells)
            layout.push_back({c.slot, c.tr,
                              c.box3d ? std::optional<Projector3D>(c.box3d->proj) : std::nullopt,
                              c.cell});
        check(layout.size() == 2, "plot events: one layout per axes");

        const CoordTransform& tr = layout[0].tr;
        const float fx = tr.px + tr.pw * 0.5f, fy = tr.py + tr.ph * 0.5f;       // frame of slot 1
        const PlotRect& c1 = layout[0].cell;
        const float ox = c1.x + 1.0f, oy = c1.y + 1.0f;                          // cell 1, outside its frame

        PlotEventTracker t;
        std::vector<Event> out;
        auto frame = [&](float x, float y, bool hovered, auto&& set,
                         PlotEventInfo what = {}, std::uint32_t wanted = kAll) {
            PlotInputFrame in;
            in.x = x;
            in.y = y;
            in.hovered = hovered;
            in.width = W;
            in.height = H;
            set(in);
            out.clear();
            collect_plot_events(t, in, what, fs, layout, wanted, out);
        };
        auto idle = [](PlotInputFrame&) {};

        // ---- Hover: a move, with the subplot and its data.
        frame(fx, fy, true, idle);
        check(out.size() == 1 && out[0].kind == EventKind::MouseMove,
              "plot events: the first hovered frame reports a move");
        check(out[0].axes == 1 && out[0].has_data &&
              std::fabs(out[0].xdata - tr.to_data_x(fx)) < 1e-9 &&
              std::fabs(out[0].ydata - tr.to_data_y(fy)) < 1e-9 && std::isnan(out[0].zdata),
              "plot events: over a 2D frame: its index, and x/y data, z NaN");
        check(out[0].x == fx && out[0].y == fy, "plot events: at the figure pixel");
        frame(fx, fy, true, idle);
        check(out.empty(), "plot events: a still cursor reports nothing");
        frame(fx + 5, fy, false, idle);
        check(out.empty(), "plot events: motion with nothing under it reports nothing");

        frame(ox, oy, true, idle);
        check(out.size() == 1 && out[0].axes == -1 && !out[0].has_data,
              "plot events: in a cell's margin: no frame, no data");

        // ---- A press begins on the plot, and is followed off it.
        frame(fx, fy, true, [](PlotInputFrame& in) { in.down[0] = true; });
        check(out.size() == 2 && out[0].kind == EventKind::MouseDown && out[0].button == 0 &&
              !out[0].double_click && out[0].axes == 1 && out[1].kind == EventKind::MouseMove,
              "plot events: a press reports MouseDown (and the move that came with it)");
        frame(fx + 40, fy + 40, false, [](PlotInputFrame& in) { in.down[0] = true; });
        check(out.size() == 1 && out[0].kind == EventKind::MouseMove,
              "plot events: a drag that left the image is still followed");
        frame(fx + 60, fy + 60, false, [](PlotInputFrame& in) { in.down[0] = false; });
        check(out.size() == 1 && out[0].kind == EventKind::MouseUp && out[0].button == 0 &&
              out[0].x == fx + 60,
              "plot events: and its release is reported off the plot too");

        // ---- A press that began elsewhere is not ours, wherever it ends.
        frame(fx, fy, false, [](PlotInputFrame& in) { in.down[0] = true; });
        check(out.empty(), "plot events: a press with something on top reports nothing");
        frame(fx, fy, true, [](PlotInputFrame& in) { in.down[0] = true; });
        check(out.empty(), "plot events: dragging onto the image from a panel reports nothing");
        frame(fx, fy, true, idle);
        check(out.empty(), "plot events: nor does that press's release");

        // ---- Other buttons, double click, modifiers.
        frame(fx, fy, true, [](PlotInputFrame& in) {
            in.down[1] = true;
            in.mods = kModCtrl;
        });
        check(!out.empty() && out[0].kind == EventKind::MouseDown && out[0].button == 1 &&
              out[0].mods == kModCtrl, "plot events: the right button, with the modifiers held");
        frame(fx, fy, true, idle);
        check(out.size() == 1 && out[0].kind == EventKind::MouseUp && out[0].button == 1,
              "plot events: and its release");
        frame(fx, fy, true, [](PlotInputFrame& in) {
            in.down[0] = true;
            in.double_click[0] = true;
        });
        check(!out.empty() && out[0].double_click, "plot events: a double click says so on its MouseDown");
        frame(fx, fy, true, idle);

        // ---- Scroll.
        frame(fx, fy, true, [](PlotInputFrame& in) { in.wheel_y = -2.0f; in.wheel_x = 0.5f; });
        check(out.size() == 1 && out[0].kind == EventKind::Scroll && out[0].scroll_y == -2.0 &&
              out[0].scroll_x == 0.5 && out[0].axes == 1,
              "plot events: the wheel, with its offsets and the subplot under it");
        frame(fx, fy, false, [](PlotInputFrame& in) { in.wheel_y = 1.0f; });
        check(out.empty(), "plot events: the wheel over a panel is not ours");

        // ---- What sextant did with it.
        {
            PlotEventInfo nav;
            nav.nav_drag = true;
            frame(fx, fy, true, [](PlotInputFrame& in) { in.down[0] = true; }, nav);
            check(out[0].consumed == EventConsumed::Navigate,
                  "plot events: a press that starts a pan is Navigate");
            frame(fx + 3, fy, true, [](PlotInputFrame& in) { in.down[0] = true; }, nav);
            check(out[0].kind == EventKind::MouseMove && out[0].consumed == EventConsumed::Navigate,
                  "plot events: and so are the moves of that pan");
            frame(fx + 3, fy, true, idle, {});
            check(out[0].kind == EventKind::MouseUp && out[0].consumed == EventConsumed::Navigate,
                  "plot events: and its release");

            PlotEventInfo grid;
            grid.grid_owns = true;
            frame(fx, fy, true, [](PlotInputFrame& in) { in.down[0] = true; }, grid);
            check(out[0].consumed == EventConsumed::GridDrag,
                  "plot events: a press on a grid boundary is GridDrag");
            frame(fx, fy, true, idle, grid);
            check(out[0].kind == EventKind::MouseUp && out[0].consumed == EventConsumed::GridDrag,
                  "plot events: through the release");

            frame(fx, fy, true, [](PlotInputFrame& in) { in.down[0] = true; });
            check(out[0].consumed == EventConsumed::None,
                  "plot events: a press that did nothing is None");
            PlotEventInfo sel;
            sel.selected_now = true;
            frame(fx, fy, true, idle, sel);
            check(out[0].kind == EventKind::MouseUp && out[0].consumed == EventConsumed::Select,
                  "plot events: a release that selected the subplot is Select");

            PlotEventInfo zoom;
            zoom.nav_wheel = true;
            frame(fx, fy, true, [](PlotInputFrame& in) { in.wheel_y = 1.0f; }, zoom);
            check(out[0].kind == EventKind::Scroll && out[0].consumed == EventConsumed::Navigate,
                  "plot events: a wheel that zooms is Navigate");
        }

        // ---- Only what is wanted is built; the state moves on regardless.
        frame(fx, fy, true, [](PlotInputFrame& in) { in.down[0] = true; in.wheel_y = 1.0f; },
              {}, event_bit(EventKind::Scroll));
        check(out.size() == 1 && out[0].kind == EventKind::Scroll,
              "plot events: kinds nobody wants are not produced");
        frame(fx, fy, true, idle, {}, event_bit(EventKind::MouseUp));
        check(out.size() == 1 && out[0].kind == EventKind::MouseUp,
              "plot events: a press that was not reported still pairs with its release");

        // ---- Resize: not the first size, only a change.
        PlotEventTracker rt;
        auto sized = [&](int w, int h) {
            PlotInputFrame in;
            in.width = w;
            in.height = h;
            out.clear();
            collect_plot_events(rt, in, {}, fs, layout, kAll, out);
        };
        sized(800, 400);
        check(out.empty(), "plot events: the initial size is not a resize");
        sized(800, 400);
        check(out.empty(), "plot events: an unchanged size is not one either");
        sized(640, 400);
        check(out.size() == 1 && out[0].kind == EventKind::Resize && out[0].width == 640 &&
              out[0].height == 400, "plot events: a changed size is, with the new one");

        // ---- 3D: the subplot always, data where the ray meets a plane.
        {
            const AxesLayout& a3 = layout[1];
            check(a3.proj3d.has_value(), "plot events: slot 2 is 3D");
            const PlotRect f = a3.proj3d->frame();
            int hits = 0, misses = 0;
            bool on_plane = true, axes_ok = true;
            for (int j = 1; j < 12; ++j)
                for (int i = 1; i < 12; ++i) {
                    const float x = f.x + f.w * i / 12.0f, y = f.y + f.h * j / 12.0f;
                    const PointLocation p = locate_point(fs, layout, x, y);
                    axes_ok = axes_ok && p.axes == 2;
                    if (!p.has_data) { ++misses; continue; }
                    ++hits;
                    // two_plane_snapshot(): XY at z = 0.25, YZ at x = 0.5.
                    on_plane = on_plane && (std::fabs(p.z - 0.25) < 1e-6 || std::fabs(p.x - 0.5) < 1e-6);
                }
            check(axes_ok, "plot events: every point in the 3D frame names its subplot");
            check(hits > 0 && on_plane,
                  "plot events: a ray that meets a plane reports the point on it");
            (void) misses;

            PlotEventTracker t3;
            out.clear();
            PlotInputFrame in;
            in.x = f.x + f.w * 0.5f;
            in.y = f.y + f.h * 0.5f;
            in.hovered = true;
            collect_plot_events(t3, in, {}, fs, layout, kAll, out);
            check(!out.empty() && out[0].axes == 2, "plot events: a 3D hover names its subplot");
        }

        // ---- A snapshot with no plane: no data, but still the subplot.
        {
            FigureSnapshot bare = make_snapshot3d(1, 1, 1, Camera3D{});
            const FigureLayout bl = compute_figure_layout(bare, 400, 400);
            std::vector<AxesLayout> bl3;
            for (const CellLayout& c: bl.cells)
                bl3.push_back({c.slot, c.tr,
                               c.box3d ? std::optional<Projector3D>(c.box3d->proj) : std::nullopt,
                               c.cell});
            const PlotRect f = bl3[0].proj3d->frame();
            const PointLocation p = locate_point(bare, bl3, f.x + f.w * 0.5f, f.y + f.h * 0.5f);
            check(p.axes == 1 && !p.has_data, "plot events: a 3D axes with no plane has no data point");
        }
    }

    // -------------------------------------------------------------------------
    // Through the panel: synthetic window input -> WindowLink -> ImGui backend ->
    // draw_widget_panel() -> the channel. Everything a click takes except GLFW's
    // own callbacks, on a hidden window.
    // -------------------------------------------------------------------------
    void test_plot_events_in_panel() {
        std::printf("\n[events: through the widget panel]\n");

        FigureOptions opts;
        opts.width = 800;
        opts.height = 600;
        GLContext ctx({.width = 800, .height = 600, .title = "events_panel", .visible = false});
        NvgRenderer nvg(ctx.nvg());
        DataRenderer data;
        PlotFbo plot_fbo;
        ImGuiPanelContext imgui_ctx(ctx, opts);

        FigureSnapshot fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
        FigureEditBox edit_box;
        PanelState st;
        auto ch = EventChannel::create();
        st.events = ch.get();

        std::vector<Event> log;
        for (int k = 0; k <= static_cast<int>(EventKind::Resize); ++k)
            ch->connect(static_cast<EventKind>(k), [&log](const Event& e) { log.push_back(e); });

        auto frame = [&] {
            imgui_ctx.make_current();
            imgui_ctx.sync_dpi_scale(ctx);
            ctx.link().sync_state();
            platform::GLContextLock gl_lock;
            glViewport(0, 0, ctx.width(), ctx.height());
            draw_widget_panel(ctx, nvg, data, plot_fbo, fs, opts, edit_box, st);
            ctx.link().service_requests();
        };
        auto drain = [&] {
            log.clear();
            ch->dispatch();
        };
        // Runs a frame and returns what it produced.
        auto step = [&]() -> std::vector<Event>& {
            frame();
            drain();
            return log;
        };
        auto pos = [&](double x, double y) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::MousePos;
            e.x = x;
            e.y = y;
            ctx.link().post_event(e);
        };
        auto button = [&](int b, bool down) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::MouseButton;
            e.key = b;
            e.down = down;
            ctx.link().post_event(e);
        };
        auto key = [&](int k, bool down, int mods = 0) {
            WindowEvent e;
            e.kind = WindowEvent::Kind::Key;
            e.key = k;
            e.down = down;
            e.mods = mods;
            ctx.link().post_event(e);
        };

        // Let the dock layout settle, then find the plot image.
        for (int i = 0; i < 6; ++i) frame();
        drain();
        ImGuiWindow* plot = ImGui::FindWindowByName("Plot");
        check(plot != nullptr && st.live_plot_w.load() > 0, "panel events: the Plot panel is drawn");
        if (!plot) return;
        const ImVec2 mid((plot->ContentRegionRect.Min.x + plot->ContentRegionRect.Max.x) * 0.5f,
                         (plot->ContentRegionRect.Min.y + plot->ContentRegionRect.Max.y) * 0.5f);

        // ---- Hover, click, wheel.
        pos(mid.x, mid.y);
        {
            auto& got = step();
            check(!got.empty() && got[0].kind == EventKind::MouseMove && got[0].axes == 1 &&
                  got[0].has_data,
                  "panel events: moving over the plot reports a move with its subplot and data");
        }
        button(0, true);
        {
            auto& got = step();
            check(!got.empty() && got[0].kind == EventKind::MouseDown && got[0].button == 0 &&
                  got[0].axes == 1,
                  "panel events: a press reports MouseDown");
        }
        button(0, false);
        {
            auto& got = step();
            check(!got.empty() && got[0].kind == EventKind::MouseUp && got[0].button == 0,
                  "panel events: and the release reports MouseUp");
        }
        button(1, true);
        {
            auto& got = step();
            check(!got.empty() && got[0].kind == EventKind::MouseDown && got[0].button == 1,
                  "panel events: the right button too");
        }
        button(1, false);
        step();

        {
            WindowEvent w;
            w.kind = WindowEvent::Kind::Scroll;
            w.y = 1.0;
            ctx.link().post_event(w);
            auto& got = step();
            bool scrolled = false;
            for (const Event& e : got)
                scrolled = scrolled || (e.kind == EventKind::Scroll && e.scroll_y == 1.0);
            check(scrolled, "panel events: the wheel reports a Scroll");
        }

        // ---- Keys.
        key(GLFW_KEY_A, true, ModCtrl);
        {
            auto& got = step();
            check(!got.empty() && got[0].kind == EventKind::KeyDown && got[0].key == "ctrl+a",
                  "panel events: a key press reports KeyDown with its name and modifiers");
        }
        key(GLFW_KEY_A, false);
        {
            auto& got = step();
            check(!got.empty() && got[0].kind == EventKind::KeyUp, "panel events: and KeyUp");
        }

        // ---- What is not the plot's: the side panel.
        {
            const ImVec2 side(static_cast<float>(ctx.width()) - 12.0f, 300.0f);
            pos(side.x, side.y);
            step();
            button(0, true);
            auto& down = step();
            check(down.empty(), "panel events: a press over a side panel is not reported");
            pos(mid.x, mid.y);
            auto& drag = step();
            bool any_mouse = false;
            for (const Event& e : drag)
                any_mouse = any_mouse || e.kind == EventKind::MouseMove ||
                            e.kind == EventKind::MouseDown;
            check(!any_mouse, "panel events: dragging from a panel onto the plot is not either");
            button(0, false);
            auto& up = step();
            check(up.empty(), "panel events: nor is that release");
        }

        // ---- What sextant did with it.
        {
            st.navigate_enabled = true;
            pos(mid.x, mid.y);
            step();
            button(0, true);
            auto& got = step();
            check(!got.empty() && got[0].kind == EventKind::MouseDown &&
                  got[0].consumed == EventConsumed::Navigate,
                  "panel events: with Navigate on, a press on the selected subplot says Navigate");
            button(0, false);
            step();
            st.navigate_enabled = false;
        }

        // ---- Nothing is queued for a kind nobody wants.
        {
            auto quiet = EventChannel::create();
            st.events = quiet.get();
            pos(mid.x + 3, mid.y);
            frame();
            button(0, true);
            frame();
            button(0, false);
            frame();
            check(!quiet->has_pending(), "panel events: no callback, nothing queued");
            st.events = ch.get();
        }
    }

    // -------------------------------------------------------------------------
    // Through a real figure
    // -------------------------------------------------------------------------
    namespace {
        using Clock = std::chrono::steady_clock;

        std::shared_ptr<Figure> event_figure(const char* title) {
            auto fig = Figure::create({.width = 320, .height = 240, .title = title});
            const std::vector<double> x{0.0, 1.0, 2.0};
            const std::vector<double> y{0.0, 1.0, 0.5};
            fig->axes()->line(x, y);
            return fig;
        }
    } // namespace

    void test_event_delivery() {
        std::printf("\n[events: delivery through a figure]\n");

        // ---- Never shown: nothing to deliver, and connect() validates.
        {
            auto fig = Figure::create();
            bool threw = false;
            try { fig->connect(EventKind::Close, {}); } catch (const std::invalid_argument&) { threw = true; }
            check(threw, "delivery: an empty callback is refused");
            fig->connect(EventKind::Close, [](const Event&) {});
            check(fig->dispatch_events() == 0, "delivery: nothing is queued before show()");
        }

        // ---- close() on another thread: the Close event reaches wait_closed()'s caller.
        {
            auto fig = event_figure("events: close");
            std::atomic<int> closes{0};
            std::thread::id cb_thread{};
            fig->connect(EventKind::Close, [&](const Event& e) {
                closes.fetch_add(1);
                cb_thread = std::this_thread::get_id();
                (void) e;
            });
            fig->show(false);
            std::thread closer([&fig] {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                fig->close();
            });
            const bool closed = fig->wait_closed(30.0);
            closer.join();
            check(closed, "delivery: wait_closed() returns once the window is closed");
            check(closes.load() == 1, "delivery: and the Close callback ran before it returned");
            check(cb_thread == std::this_thread::get_id(),
                  "delivery: on the thread that was waiting, not the closer's or the window's");
            check(fig->dispatch_events() == 0 && closes.load() == 1,
                  "delivery: exactly once");
        }

        // ---- close() from inside a callback does not deadlock.
        {
            auto fig = event_figure("events: close in callback");
            std::atomic<int> closes{0};
            fig->connect(EventKind::Close, [&](const Event&) { closes.fetch_add(1); });
            fig->show(false);
            fig->close();                // queues Close; nothing has drained yet
            check(!fig->is_open(), "delivery: close() closes the window");
            check(closes.load() == 0, "delivery: close() itself runs no callback");
            check(fig->dispatch_events() == 1 && closes.load() == 1,
                  "delivery: the next drain delivers the Close it queued");

            // A callback that closes another figure, from the draining thread.
            auto a = event_figure("events: a"), b = event_figure("events: b");
            a->connect(EventKind::Close, [&](const Event&) { b->close(); });
            std::atomic<int> b_closes{0};
            b->connect(EventKind::Close, [&](const Event&) { b_closes.fetch_add(1); });
            a->show(false);
            b->show(false);
            a->close();
            Figure::poll_events();
            check(!b->is_open(), "delivery: a callback may close() a figure; no deadlock");
            Figure::poll_events();
            check(b_closes.load() == 1, "delivery: and that figure's own Close is delivered once");
        }

        // ---- run() drains figures as they close and returns after the last.
        {
            auto a = event_figure("events: run a"), b = event_figure("events: run b");
            std::atomic<int> closed{0};
            a->connect(EventKind::Close, [&](const Event&) { closed.fetch_add(1); });
            b->connect(EventKind::Close, [&](const Event&) { closed.fetch_add(1); });
            a->show(false);
            b->show(false);
            std::thread closer([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                a->close();
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                b->close();
            });
            Figure::run();
            closer.join();
            check(closed.load() == 2, "delivery: run() delivered both Close events before returning");
        }

        // ---- Events pushed by the window while the caller is busy wait for a drain.
        {
            auto fig = event_figure("events: waiting");
            std::atomic<int> closes{0};
            fig->connect(EventKind::Close, [&](const Event&) { closes.fetch_add(1); });
            fig->show(false);
            fig->close();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            check(closes.load() == 0, "delivery: an event no one drains is not delivered");
            check(fig->wait_closed(0.0) && closes.load() == 1,
                  "delivery: a wait on a closed figure drains before it returns");
        }

        // ---- Every window that closes reports it, a replaced one included.
        {
            auto fig = event_figure("events: reshow");
            std::atomic<int> closes{0};
            fig->connect(EventKind::Close, [&](const Event&) { closes.fetch_add(1); });
            fig->show(false);
            fig->show(false);   // replaces the window: the first one closes
            check(fig->is_open(), "delivery: show() again leaves a window up");
            fig->close();
            fig->dispatch_events();
            check(closes.load() == 2, "delivery: one Close per window, the replaced one too");
        }
    }
} // namespace lt
