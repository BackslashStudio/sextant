// The wait/pump API: Figure::wait_closed(), Figure::run() and
// Figure::poll_events(), over the process-wide open-window registry. These
// checks open real windows on their own threads, as show() always has.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "window_registry.h"

namespace lt {
    using namespace sextant;

    namespace {
        // Opening a window means show() creating one on its own thread, which is
        // what does not hold on macOS until window creation moves to the main
        // thread. Until then the checks that need a window are skipped there; the
        // registry ones above them are not.
#if defined(__APPLE__)
        constexpr bool kCanOpenWindows = false;
#else
        constexpr bool kCanOpenWindows = true;
#endif

        using Clock = std::chrono::steady_clock;

        double ms_since(Clock::time_point t0) {
            return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        }

        // A small window with one line in it; the picture is beside the point.
        std::shared_ptr<Figure> window_figure(const char* title) {
            auto fig = Figure::create({.width = 320, .height = 240, .title = title});
            const std::vector<double> x{0.0, 1.0, 2.0, 3.0};
            const std::vector<double> y{0.0, 1.0, 0.5, 1.5};
            fig->axes()->line(x, y);
            return fig;
        }

        // Wakes every waiter without changing the count. The blocking checks below
        // arm it so a lost wakeup fails a timing check instead of hanging the suite.
        struct Watchdog {
            std::thread       t;
            std::atomic<bool> done{false};

            explicit Watchdog(double after_s) {
                t = std::thread([this, after_s] {
                    const auto deadline = Clock::now() +
                        std::chrono::milliseconds(static_cast<int>(after_s * 1000));
                    while (!done.load() && Clock::now() < deadline)
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    if (!done.load()) {          // nothing woke the waiter -- prod it
                        register_open_window();
                        unregister_open_window();
                    }
                });
            }

            ~Watchdog() { done.store(true); t.join(); }
        };
    } // namespace

    // -------------------------------------------------------------------------
    // wait_closed(): the timeout, the wait, and the figure that has no window
    // -------------------------------------------------------------------------
    void test_wait_closed() {
        std::printf("\n[window wait: wait_closed()]\n");

        check(open_window_count() == 0, "wait_closed: no window is open before the first show()");
        check(Figure::create()->wait_closed(), "wait_closed: a figure never shown is already closed");

        if (!kCanOpenWindows) {
            std::printf("  (skipped: show() opens its window off the main thread)\n");
            return;
        }

        auto fig = window_figure("wait_closed");
        fig->show(false);
        check(fig->is_open() && open_window_count() == 1,
              "wait_closed: show() registers the window it opened");

        // The timeout: the window is still up, so the wait gives up and says so.
        auto t0 = Clock::now();
        const bool timed_out = !fig->wait_closed(0.2);
        const double waited_out = ms_since(t0);
        check(timed_out, "wait_closed: returns false while the window is still open");
        check(waited_out >= 150.0, "wait_closed: and only after the timeout it was given");
        check(fig->is_open(), "wait_closed: a timeout leaves the window open");

        // Closed from another thread: the wait returns once it happens, not before.
        {
            Watchdog wd(10.0);
            std::thread closer([&fig] {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                fig->close();
            });
            t0 = Clock::now();
            const bool closed = fig->wait_closed(30.0);
            const double waited = ms_since(t0);
            closer.join();
            check(closed, "wait_closed: returns true once close() runs on another thread");
            check(waited >= 150.0 && waited < 5000.0,
                  "wait_closed: having blocked until then, and no longer");
        }
        check(!fig->is_open() && open_window_count() == 0,
              "wait_closed: the closed window leaves the registry");

        t0 = Clock::now();
        check(fig->wait_closed() && ms_since(t0) < 200.0,
              "wait_closed: an already-closed figure returns at once, with no timeout given");

        // The destructor closes too, so it must retire the registration as well.
        auto doomed = window_figure("wait_closed dtor");
        doomed->show(false);
        check(open_window_count() == 1, "wait_closed: a second figure registers in its turn");
        doomed.reset();
        check(open_window_count() == 0, "wait_closed: ~Figure() unregisters what show() registered");
    }

    // -------------------------------------------------------------------------
    // run(), poll_events(), and the registry's arithmetic across two windows
    // -------------------------------------------------------------------------
    void test_run_until_closed() {
        std::printf("\n[window wait: run() and poll_events()]\n");

        auto t0 = Clock::now();
        Figure::run();
        check(ms_since(t0) < 200.0, "run: returns at once when no figure is open");

        // A no-op on this platform, from a thread that is not the main one.
        bool polled = false;
        std::thread poller([&polled] { Figure::poll_events(); polled = true; });
        poller.join();
        check(polled, "poll_events: a no-op on Windows and Linux, from any thread");

        if (!kCanOpenWindows) {
            std::printf("  (skipped: show() opens its window off the main thread)\n");
            return;
        }

        auto a = window_figure("run A");
        auto b = window_figure("run B");
        a->show(false);
        b->show(false);
        check(open_window_count() == 2, "run: two windows, two registrations");

        // show() again on the open figure replaces its window rather than
        // counting a second one, and leaves the figure open.
        a->show(false);
        check(a->is_open() && open_window_count() == 2,
              "run: a second show() on the same figure replaces its window");

        {
            Watchdog wd(10.0);
            std::thread closer([&a, &b] {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                a->close();
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                b->close();
            });
            t0 = Clock::now();
            Figure::run();
            const double waited = ms_since(t0);
            closer.join();
            check(waited >= 250.0 && waited < 5000.0,
                  "run: returns when the last figure closes, not the first");
        }
        check(!a->is_open() && !b->is_open() && open_window_count() == 0,
              "run: both figures are closed and the registry is empty");
    }
} // namespace lt
