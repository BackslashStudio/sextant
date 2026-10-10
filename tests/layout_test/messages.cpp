// Diagnostics through Figure::set_message_handler(): what
// reaches the handler, and the thread contract -- any thread, no sextant lock
// held, a throwing handler ignored.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "messages.h"
#include "window_broker.h"

#include <mutex>

namespace lt {
    using namespace sextant;

    namespace {
        // Messages seen by the handler, from any thread.
        struct Inbox {
            std::mutex m;
            std::vector<std::string> msgs;
            std::vector<std::thread::id> threads;

            MessageHandler handler() {
                return [this](std::string_view s) {
                    std::lock_guard<std::mutex> lock(m);
                    msgs.emplace_back(s);
                    threads.push_back(std::this_thread::get_id());
                };
            }
            std::size_t size() {
                std::lock_guard<std::mutex> lock(m);
                return msgs.size();
            }
        };

        // A 3D scene whose SVG order is inexact at max_splits = 1: a plane
        // cutting through a bar grid.
        std::shared_ptr<Figure> inexact_scene() {
            auto fig = Figure::create({.width = 320, .height = 240, .title = "messages"});
            auto ax = fig->add_subplot3d(1, 1, 1);
            const std::vector<double> u = {0, 1, 2, 3}, v = {0, 1, 2};
            std::vector<double> h(u.size() * v.size());
            for (std::size_t i = 0; i < h.size(); ++i) h[i] = 1.0 + static_cast<double>(i % 5);
            ax->bar3d(PlaneOrientation::XY, u, v, h);
            ax->plane(PlaneOrientation::ZX, 1.0)
               ->imshow(std::vector<double>{0.1, 0.4, 0.7, 0.9, 0.3, 0.6}, 2, 3);
            return fig;
        }

        bool starts_with(const std::string& s, std::string_view p) {
            return s.compare(0, p.size(), p) == 0;
        }
    } // namespace

    void test_message_handler() {
        std::printf("\n[message handler]\n");

        const MessageHandler before = Figure::set_message_handler(nullptr);
        check(!before, "messages: the default handler reads back as empty");

        // An export's warning, once per call, prefix-free.
        Inbox in;
        Figure::set_message_handler(in.handler());
        auto fig = inexact_scene();
        const SvgSaveReport rep = fig->savefig_svg("messages_bound.svg", {.max_splits = 1});
        const SvgRender mem = fig->render_svg({.max_splits = 1});
        (void) fig->render_svg();   // exact: nothing to say
        check(!rep.scene_order_exact && in.size() == 2,
              "messages: an inexact SVG export is one message per call, an exact one none");
        if (in.size() == 2) {
            check(in.msgs[0] == "messages_bound.svg: " + rep.warning,
                  "messages: savefig_svg() names its path, then the report's warning");
            check(in.msgs[1] == "render_svg: " + mem.report.warning,
                  "messages: render_svg() names itself");
            check(!starts_with(in.msgs[0], "sextant:") && in.msgs[0].back() != '\n',
                  "messages: without the stderr prefix or a newline");
            check(in.threads[0] == std::this_thread::get_id(),
                  "messages: on the thread that made the call");
        }

        // The previous handler comes back, and is the one installed.
        MessageHandler prev = Figure::set_message_handler([](std::string_view) {});
        const std::size_t n0 = in.size();
        if (prev) prev("via the returned handler");
        check(in.size() == n0 + 1, "messages: set_message_handler() returns the previous handler");

        // A throwing handler does not fail the export.
        Figure::set_message_handler([](std::string_view) { throw std::runtime_error("no"); });
        bool threw = false;
        try {
            (void) fig->render_svg({.max_splits = 1});
        } catch (...) {
            threw = true;
        }
        check(!threw, "messages: a handler that throws does not fail the call");

        // Re-entrant: the handler replaces itself mid-call.
        Inbox second;
        std::atomic<int> calls{0};
        Figure::set_message_handler([&](std::string_view) {
            ++calls;
            Figure::set_message_handler(second.handler());
        });
        emit_message("one");
        emit_message("two");
        check(calls.load() == 1 && second.size() == 1,
              "messages: a handler may call set_message_handler() (no lock held)");

        // Any thread, several at once, nothing lost.
        Inbox many;
        Figure::set_message_handler(many.handler());
        {
            std::vector<std::thread> ts;
            for (int t = 0; t < 8; ++t)
                ts.emplace_back([] { for (int i = 0; i < 100; ++i) emit_message("x"); });
            for (auto& t : ts) t.join();
        }
        check(many.size() == 800, "messages: 8 threads x 100 messages, all delivered");

        // A blocked handler holds up no other thread: while it waits, another
        // thread can still replace it and emit.
        {
            std::atomic<bool> inside{false}, released{false}, waited{false};
            Figure::set_message_handler([&](std::string_view) {
                inside.store(true);
                const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (!released.load() && std::chrono::steady_clock::now() < end)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                waited.store(released.load());
            });
            std::thread blocked([] { emit_message("slow"); });
            while (!inside.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            Inbox after;
            Figure::set_message_handler(after.handler());
            emit_message("fast");
            released.store(true);
            blocked.join();
            check(after.size() == 1 && waited.load(),
                  "messages: a blocked handler blocks neither set_message_handler() nor another emit");
        }

        Figure::set_message_handler(nullptr);
    }

    // macOS only, where windows belong to the main thread: the "still waiting
    // for a window" message is raised while a worker waits on the broker. The
    // handler blocks on a lock the main thread holds -- a Python handler taking
    // the GIL -- and the main thread must still be able to pump. With the broker
    // lock held across the handler, this pump would hang.
    void test_message_handler_broker() {
        std::printf("\n[message handler: the broker's warning]\n");
        if (!pump_runs_here()) {
            std::printf("  skipped: this thread does not own the windows here\n");
            return;
        }

        std::timed_mutex gil;
        gil.lock();
        std::atomic<bool> inside{false}, got_gil{false}, done{false};
        Figure::set_message_handler([&](std::string_view m) {
            if (m.find("still waiting for a window") == std::string_view::npos) return;
            inside.store(true);
            if (gil.try_lock_for(std::chrono::seconds(15))) {
                got_gil.store(true);
                gil.unlock();
            }
        });

        std::thread worker([&] {
            {
                GLContext ctx({.width = 64, .height = 48, .title = "messages", .visible = false});
            }
            done.store(true);
        });

        // Not pumping: the worker's request waits, and after ~2 s it says so.
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!inside.load() && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(inside.load(), "messages broker: a worker waiting for the pump raises the message");

        // The handler is now blocked on `gil`. Pump while holding it.
        const auto t0 = std::chrono::steady_clock::now();
        pump_until([] { return false; }, 0.5);
        const double pumped = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(pumped < 3.0, "messages broker: the pump runs while the handler blocks ("
                                + std::to_string(pumped) + " s)");
        gil.unlock();

        pump_until([&done] { return done.load(); }, 30.0);
        worker.join();
        check(got_gil.load() && done.load(),
              "messages broker: and the worker gets its window once the handler returns");
        Figure::set_message_handler(nullptr);
    }
} // namespace lt
