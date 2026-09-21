// sextant_window_probe: the library driven the way the threading design says a
// portable program drives it -- figures shown from the main thread and from a
// worker, the main thread pumping, windows closed out of order, an export
// asked for from somewhere else again -- with a check on everything that comes
// back. It links sextant and uses nothing but the public header.
//
// The same script runs on every platform. On Windows and Linux each window
// pumps itself and Figure::poll_events() does nothing, so what is really under
// test there is that a portable loop stays correct; where windows belong to the
// main thread it is the broker, the queued creation and the deferred destroy.
//
//   sextant_window_probe            scripted. Exit 0 when every check passes,
//                                   1 when one fails, 2 on the watchdog (120 s,
//                                   most likely a deadlock). Writes
//                                   window_probe.png.
//   sextant_window_probe --manual   two live windows left to a person:
//                                   drag-resize them, click and type in the
//                                   panels, copy and paste, and watch the
//                                   moving trace for flicker or tearing.
#include <sextant/sextant.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
    using Clock = std::chrono::steady_clock;
    using sextant::Figure;

    int g_checks = 0;
    int g_failures = 0;

    void check(bool ok, const char* what) {
        ++g_checks;
        if (!ok) ++g_failures;
        std::printf("%s: %s\n", ok ? "  ok  " : "  FAIL", what);
        std::fflush(stdout);
    }

    // Every wait in this file goes through the pump, which is what makes the
    // script portable: a no-op where windows pump themselves, the only thing
    // keeping them alive where they do not.
    void pump_for(std::chrono::milliseconds ms) {
        const auto until = Clock::now() + ms;
        while (Clock::now() < until) {
            Figure::poll_events();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }

    bool pump_until(const std::function<bool()>& done, double seconds) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(
                                  static_cast<int>(seconds * 1000));
        while (!done()) {
            if (Clock::now() >= deadline) return false;
            Figure::poll_events();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        return true;
    }

    std::vector<double> ramp(int n) {
        std::vector<double> v(n);
        for (int i = 0; i < n; ++i) v[i] = i * 0.25;
        return v;
    }

    std::shared_ptr<Figure> make_figure(const char* title, int w = 480, int h = 360) {
        auto fig = Figure::create({.width = w, .height = h, .title = title});
        const std::vector<double> x = ramp(40);
        std::vector<double> y(x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = std::sin(x[i]);
        fig->axes()->line(x, y, {.color = sextant::Color::Blue, .linewidth = 2.0f});
        fig->axes()->set_title(title);
        return fig;
    }

    // -------------------------------------------------------------------------
    // The script
    // -------------------------------------------------------------------------
    void run_checks() {
        // --- a file, with nothing open and nobody pumping ----------------------
        // An export needs a GL context, not a window. This is first, before
        // anything has asked for a window at all, and the main thread does
        // nothing for it but wait: where a window belongs to the main thread,
        // asking for one here would queue a request to a thread that is about
        // to block on the join, and the watchdog would be what ended the run.
        std::printf("[probe] savefig() from a worker thread, nothing open\n");
        {
            auto h = make_figure("probe headless", 400, 300);
            std::atomic<bool> saved{false};
            std::string err;
            std::thread saver([&h, &saved, &err] {
                try {
                    h->savefig_png("window_probe_headless.png", {}, 400, 300);
                } catch (const std::exception& e) {
                    err = e.what();
                }
                saved.store(true);
            });
            const auto until = Clock::now() + std::chrono::seconds(30);
            while (!saved.load() && Clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const bool unassisted = saved.load();
            if (!unassisted) pump_until([&saved] { return saved.load(); }, 30.0);
            saver.join();
            check(unassisted && err.empty(),
                  "savefig() with nothing open is served on the thread that asked, "
                  "whichever thread that is");
            std::error_code ec;
            check(std::filesystem::exists("window_probe_headless.png", ec) &&
                  std::filesystem::file_size("window_probe_headless.png", ec) > 1000,
                  "and wrote a file with something in it");
            check(!h->is_open(), "without opening a window");
        }

        std::printf("[probe] nothing open yet\n");
        const auto t0 = Clock::now();
        Figure::run();
        check(std::chrono::duration<double>(Clock::now() - t0).count() < 1.0,
              "run() returns at once with no window open");
        Figure::poll_events();
        check(true, "poll_events() on the main thread is allowed with nothing open");

        // --- a window from the main thread ------------------------------------
        std::printf("[probe] showing A from the main thread\n");
        auto a = make_figure("probe A");
        a->show(false);
        check(a->is_open(), "show() from the main thread opens a window");

        // Waited for, not timed: a software renderer's first frame takes as long
        // as it takes (shader compiles, the font atlas), and the claim here is
        // that the frame arrives at all.
        check(pump_until([&a] { return a->frame_stats().frames > 0; }, 60.0),
              "and its own thread is rendering frames");

        // --- a window from a worker thread ------------------------------------
        // The case the whole broker exists for: the caller is not the thread
        // that may make a window, so the request crosses over and the main
        // thread's pump serves it.
        std::printf("[probe] showing B from a worker thread\n");
        auto b = make_figure("probe B", 420, 300);
        std::atomic<bool> shown{false};
        std::atomic<bool> show_failed{false};
        std::thread shower([&b, &shown, &show_failed] {
            try {
                b->show(false);
            } catch (...) {
                show_failed.store(true);
            }
            shown.store(true);
        });
        const bool arrived = pump_until([&shown] { return shown.load(); }, 30.0);
        shower.join();
        check(arrived && !show_failed.load(),
              "show() from a worker thread opens a window while the main thread pumps");
        check(b->is_open(), "and the figure it was called on is open");

        // poll_events() is the main thread's job where there is a pump at all.
        std::atomic<bool> polled{false}, refused{false};
        std::thread poller([&polled, &refused] {
            try {
                Figure::poll_events();
                polled.store(true);
            } catch (const std::logic_error&) {
                refused.store(true);
            }
        });
        poller.join();
        check(polled.load() != refused.load(),
              "poll_events() off the main thread either does nothing or refuses, "
              "and says which");

        // --- work asked for from elsewhere while both are up -------------------
        pump_for(std::chrono::milliseconds(300));
        std::atomic<bool> saved{false}, save_failed{false};
        std::thread saver([&a, &saved, &save_failed] {
            try {
                a->savefig_png("window_probe.png", {}, 400, 300);
            } catch (...) {
                save_failed.store(true);
            }
            saved.store(true);
        });
        const bool save_done = pump_until([&saved] { return saved.load(); }, 60.0);
        saver.join();
        check(save_done && !save_failed.load(),
              "savefig() from a third thread is serviced while both windows are up");
        std::error_code ec;
        check(std::filesystem::exists("window_probe.png", ec) &&
              std::filesystem::file_size("window_probe.png", ec) > 1000,
              "and wrote a file with something in it");

        // A live edit from the main thread, the loop a program actually runs.
        const std::vector<double> x = ramp(40);
        for (int i = 0; i < 12; ++i) {
            pump_for(std::chrono::milliseconds(40));
            auto ax = a->axes();
            ax->cla();
            std::vector<double> y(x.size());
            for (size_t j = 0; j < x.size(); ++j) y[j] = std::sin(x[j] + i * 0.2);
            ax->line(x, y, {.color = sextant::Color::Red, .linewidth = 2.0f});
            a->refresh();
        }
        check(a->is_open() && a->frame_stats().frames > 0,
              "refresh() in a pumping loop leaves the window up and drawing");

        // --- closed out of order ----------------------------------------------
        std::printf("[probe] closing B, then A from a worker thread\n");
        b->close();
        check(!b->is_open() && a->is_open(),
              "closing one window leaves the other one open");
        const unsigned long long before = a->frame_stats().frames;
        check(pump_until([&a, before] { return a->frame_stats().frames > before; }, 60.0),
              "and the survivor is still rendering");

        // The close comes from a thread that is not the pump, so its window is
        // handed back and destroyed at whatever poll comes next -- which is the
        // one wait_closed() is doing.
        std::thread closer([&a] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            a->close();
        });
        const auto t1 = Clock::now();
        const bool closed = a->wait_closed(30.0);
        const double waited = std::chrono::duration<double>(Clock::now() - t1).count();
        closer.join();
        check(closed, "wait_closed() on the main thread returns when another thread closes it");
        check(waited >= 0.2 && waited < 20.0, "having waited for it, and not much longer");

        // --- run() to the end --------------------------------------------------
        auto c = make_figure("probe C", 360, 260);
        c->show(false);
        check(c->is_open(), "a third window opens after the first two are gone");
        std::thread last([&c] {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            c->close();
        });
        const auto t2 = Clock::now();
        Figure::run();
        const double ran = std::chrono::duration<double>(Clock::now() - t2).count();
        last.join();
        check(!c->is_open(), "run() returns with the last window closed");
        check(ran >= 0.3 && ran < 20.0, "and not before it was");

        // Nothing left behind: another round still works.
        auto d = make_figure("probe D", 300, 220);
        d->show(false);
        pump_for(std::chrono::milliseconds(200));
        check(d->is_open(), "a window opens again after run() has returned");
        d->close();
        check(!d->is_open(), "and closes again");
    }

    // -------------------------------------------------------------------------
    // The half no script reaches
    // -------------------------------------------------------------------------
    void run_manual() {
        std::printf(
            "[probe] two live windows. Drag-resize both (watch for flicker or a\n"
            "        torn frame), click and type in the panels, copy and paste,\n"
            "        move the pointer in and out. Close both to finish.\n");

        auto a = make_figure("probe A -- drag me", 640, 480);
        auto b = make_figure("probe B -- and me", 560, 420);
        a->show(false);
        b->show(false);

        // A trace that moves every frame: a still picture hides a torn one.
        const std::vector<double> x = ramp(80);
        for (int i = 0; a->is_open() || b->is_open(); ++i) {
            pump_for(std::chrono::milliseconds(16));
            std::vector<double> y(x.size());
            for (size_t j = 0; j < x.size(); ++j) y[j] = std::sin(x[j] + i * 0.08);
            for (const auto& fig: {a, b}) {
                if (!fig->is_open()) continue;
                auto ax = fig->axes();
                ax->cla();
                ax->line(x, y, {.color = sextant::Color::Red, .linewidth = 2.0f});
                fig->refresh();
            }
        }
        std::printf("[probe] both windows closed\n");
    }
} // namespace

int main(int argc, char** argv) {
    const bool manual = argc > 1 && std::strcmp(argv[1], "--manual") == 0;

    // A deadlock is the failure this probe is really looking for, so it is the
    // one failure that must not hang the CI job.
    std::thread watchdog;
    if (!manual) {
        watchdog = std::thread([] {
            const auto deadline = Clock::now() + std::chrono::seconds(120);
            while (Clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::fprintf(stderr, "TIMEOUT: window_probe is stuck\n");
            std::fflush(nullptr);
            std::_Exit(2);
        });
        watchdog.detach();
    }

    try {
        if (manual) run_manual();
        else run_checks();
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw %s\n", e.what());
        ++g_failures;
        ++g_checks;
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
