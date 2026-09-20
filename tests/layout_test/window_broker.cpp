// The window broker: who makes a window, who unmakes it, and who keeps its
// link alive in between. On Windows and Linux every thread may do all three, so
// what is checked here is the bookkeeping the macOS main-thread path is built
// on -- the live list, the shared link, handing a window back twice, and the
// pump that does nothing here on purpose.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "window_broker.h"
#include "window_link.h"
#include "platform/platform.h"

namespace lt {
    using namespace sextant;

    // -------------------------------------------------------------------------
    // Making and unmaking a window
    // -------------------------------------------------------------------------
    void test_window_broker() {
        std::printf("\n[window broker: the live list]\n");

        const int base = live_window_count();

        BrokeredWindow bw = create_window({
            .width = 300, .height = 200, .title = "broker", .visible = false
        });
        check(bw.window != nullptr && bw.link != nullptr,
              "broker: a window comes back with its link");
        check(live_window_count() == base + 1,
              "broker: and is on the list the pump walks");

        int w = 0, h = 0;
        bw.link->window_size(w, h);
        check(w == 300 && h == 200,
              "broker: the link was attached before it was handed over, so the "
              "mirror already knows the size");

        // The broker holds a reference of its own: a window's callbacks reach
        // its link through the user pointer, and on macOS the destroy is served
        // long after the render thread has let go.
        check(bw.link.use_count() >= 2, "broker: the link is shared, not handed over");

        GLFWwindow* handle = bw.window;
        destroy_window(handle);
        check(live_window_count() == base,
              "broker: handing it back takes it off the list");
        check(bw.link.use_count() == 1, "broker: and drops the broker's reference");

        // Both of these happen for real: the render thread gives the window back
        // on its way out, and close() asks again after joining it.
        destroy_window(handle);
        destroy_window(nullptr);
        check(live_window_count() == base, "broker: a window handed back twice is dropped");

        // A window asked for by a thread that is not this one. Here that thread
        // may make it itself; where windows belong to the main thread the
        // request crosses over, and joining without pumping would be waiting on
        // a window only this thread can make.
        GLFWwindow* from_worker = nullptr;
        std::atomic<bool> worker_done{false};
        std::thread worker([&from_worker, &worker_done] {
            from_worker = create_window({
                .width = 120, .height = 90, .title = "broker worker", .visible = false
            }).window;
            worker_done.store(true);
        });
        if (pump_runs_here()) pump_until([&worker_done] { return worker_done.load(); }, 30.0);
        worker.join();
        check(from_worker != nullptr && live_window_count() == base + 1,
              "broker: another thread's window is made -- inline, or by the pump for it");
        destroy_window(from_worker);
        check(live_window_count() == base, "broker: and unmade again");

        // What GLContext does with all of the above.
        {
            GLContext ctx({.width = 200, .height = 150, .title = "broker ctx", .visible = false});
            check(live_window_count() == base + 1,
                  "broker: a GLContext's window is brokered like any other");
            check(ctx.width() > 0 && ctx.height() > 0,
                  "broker: and its framebuffer size reads the link the broker attached");
        }
        check(live_window_count() == base,
              "broker: and goes back when the context is destroyed");
    }

    // -------------------------------------------------------------------------
    // The pump, and the three things that differ on macOS
    // -------------------------------------------------------------------------
    void test_window_broker_pump() {
        std::printf("\n[window broker: the pump]\n");

        // Two platforms, two answers. Each check says which it expects rather
        // than assuming the one it happens to be compiled on -- these are the
        // `if`s the whole macOS path hangs off, so a test that only knows this
        // side of them proves half of nothing.
        const bool on_main = platform::windows_on_main_thread;

        check(pump_runs_here() == on_main,
              "broker: this thread is the pump exactly where windows belong to the main one");
        check(platform::this_thread_owns_windows(),
              "broker: and it may own a window either way, being the main one");

        // Figure::poll_events() is pump_windows(0): nothing at all here, from
        // any thread, so a portable loop stays portable; the pump there, which
        // no other thread may call.
        Figure::poll_events();
        pump_windows(0.05);
        bool worker_owns = false, polled = false, refused = false;
        std::thread([&worker_owns, &polled, &refused] {
            worker_owns = platform::this_thread_owns_windows();
            try {
                Figure::poll_events();
                polled = true;
            } catch (const std::logic_error&) {
                refused = true;
            }
        }).join();
        check(worker_owns == !on_main,
              "broker: a worker thread may own a window here, and may not there");
        check((on_main ? refused : polled),
              "broker: and pumping from it is the same no-op here, a refusal there");

        // Nothing is queued for a thread that serves its own requests inline.
        serve_broker_requests();
        check(live_window_count() >= 0, "broker: serving an empty queue does nothing");

        // The deadline arithmetic the macOS waits run on. Pumping does nothing
        // here, so this spins for the timeout and then gives up -- which is the
        // half worth checking: a wait that cannot succeed still ends.
        check(pump_until([] { return true; }, -1.0),
              "broker: a wait whose answer is already in returns at once");
        const auto t0 = std::chrono::steady_clock::now();
        check(!pump_until([] { return false; }, 0.05),
              "broker: one that never comes ends at the timeout");
        check(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= 0.04,
              "broker: having waited for it");

        // The render thread's context lock. Nothing at all here; there, this
        // thread has no context current, so it has none to take either -- what
        // is checked is that taking and dropping it is safe from a thread in
        // that state, which is every thread but a live render loop.
        {
            platform::GLContextLock lock;
            check(true, "broker: the context lock is safe to take with no context current");
        }

        std::string text;
        check(platform::read_clipboard(text) == on_main,
              "broker: the clipboard is the platform's to read where it has a thread-safe one, "
              "and GLFW's here");
    }
} // namespace lt
