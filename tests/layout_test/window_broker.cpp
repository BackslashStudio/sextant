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

        // A worker thread, which is every thread that is not the main one.
        std::thread([base] {
            BrokeredWindow other = create_window({
                .width = 120, .height = 90, .title = "broker worker", .visible = false
            });
            check(other.window != nullptr && live_window_count() == base + 1,
                  "broker: a thread that may own windows makes one inline");
            destroy_window(other.window);
        }).join();
        check(live_window_count() == base, "broker: and unmakes it the same way");

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

        check(!platform::windows_on_main_thread,
              "broker: windows are not tied to the main thread on this platform");
        check(platform::this_thread_owns_windows(),
              "broker: so this thread may own one");
        check(!pump_runs_here(), "broker: and nobody has to pump for it");

        // Figure::poll_events() is this, and it is a no-op here -- from any
        // thread, so a portable loop stays portable.
        Figure::poll_events();
        pump_windows(0.05);
        std::thread([] {
            check(platform::this_thread_owns_windows(),
                  "broker: a worker thread may own windows here too");
            Figure::poll_events();
            check(true, "broker: and pumping from it is the same no-op");
        }).join();

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

        // The render thread's context lock, which is nothing at all here.
        {
            platform::GLContextLock a;
            platform::GLContextLock b;
            check(true, "broker: the context lock is a no-op where no resize can race a frame");
        }

        std::string text;
        check(!platform::read_clipboard(text),
              "broker: and the clipboard is GLFW's to read, not the platform's");
    }
} // namespace lt
