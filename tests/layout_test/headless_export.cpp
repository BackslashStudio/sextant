// A PNG export with no window: the offscreen GL context savefig() draws into
// when nothing is open. On Windows and Linux that is still a hidden window,
// because any thread may make one; on macOS it is a context of the platform's
// own, which is what lets an export be asked for from a thread that may not own
// a window and without the main thread pumping for it.
//
// The gate is here: the same figure through both paths is the same picture.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "platform/platform.h"
#include "renderer/fbo_readback.h"
#include "window_broker.h"

#include <glad/glad.h>

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 420, H = 320;

        void render_png(const FigureSnapshot& fs, bool headless, const std::string& path) {
            GLContext ctx({
                .width = W, .height = H, .title = "layout_test",
                .visible = false, .resizable = false, .headless = headless
            });
            NvgRenderer nvg(ctx.nvg());
            DataRenderer data;
            export_figure_png(ctx, nvg, data, fs, path, W, H, 1);
        }

        long long file_size(const std::string& p) {
            std::error_code ec;
            const auto n = std::filesystem::file_size(p, ec);
            return ec ? -1 : static_cast<long long>(n);
        }
    } // namespace

    // -------------------------------------------------------------------------
    // The context itself
    // -------------------------------------------------------------------------
    void test_headless_context() {
        std::printf("\n[headless export: the context]\n");

        // Two platforms, two answers; each check states both and asserts the one
        // its own owes, so neither column is merely assumed.
        const bool offscreen = platform::has_offscreen_gl;
        const int base = live_window_count();

        {
            GLContext ctx({
                .width = W, .height = H, .title = "layout_test",
                .visible = false, .resizable = false, .headless = true
            });
            check(ctx.is_headless() == offscreen,
                  "headless: no window where the platform gives a context without one, "
                  "a hidden window where it does not");
            check(live_window_count() == base + (offscreen ? 0 : 1),
                  "headless: so the broker is holding one window fewer there");
            check(ctx.nvg() != nullptr,
                  "headless: and a NanoVG context either way");
            check(offscreen
                      ? (ctx.width() == W && ctx.height() == H)
                      : (ctx.width() > 0 && ctx.height() > 0),
                  "headless: with the size it was asked for, which nothing can resize");
            check(ctx.should_close() == offscreen,
                  "headless: and nothing to keep open where there is no window, "
                  "a window to keep open where there is");

            // The export never draws to the default framebuffer, which is just
            // as well: a headless context has no drawable behind one.
            FboReadback fbo(32, 24, 1);
            fbo.bind();
            check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                  "headless: an FboReadback on it is a complete framebuffer");
            fbo.unbind();
        }
        check(live_window_count() == base,
              "headless: and the context leaves nothing behind");

        // A headless context asked for where there is none falls back rather
        // than failing, which is what keeps one export path for every platform.
        {
            GLContext ctx({.width = 64, .height = 48, .title = "layout_test", .visible = false});
            check(!ctx.is_headless(),
                  "headless: a context that did not ask for it still gets its window");
        }
    }

    // -------------------------------------------------------------------------
    // The gate: both paths, one picture
    // -------------------------------------------------------------------------
    void test_headless_export() {
        std::printf("\n[headless export: the picture]\n");

        const FigureSnapshot fs = make_snapshot(1, 1, 1);
        render_png(fs, false, "headless_window.png");
        render_png(fs, true, "headless_offscreen.png");
        check(same_picture("headless_window.png", "headless_offscreen.png"),
              "headless: the export is the picture the windowed path draws, "
              "pixel for pixel");

        // The public route, on a figure that was never shown: what a program
        // that only wants a file does.
        auto fig = Figure::create({.width = W, .height = H, .title = "headless"});
        std::vector<double> x(40), y(40);
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = static_cast<double>(i) * 0.25;
            y[i] = std::sin(x[i]);
        }
        fig->axes()->line(x, y, {.color = Color::Blue, .linewidth = 2.0f});
        fig->axes()->set_title("headless");
        fig->savefig_png("headless_main.png", {}, W, H);
        check(file_size("headless_main.png") > 1000,
              "headless: savefig() with no window open writes a file");

        // The same call from a thread that may not own a window, with the main
        // thread doing nothing for it -- no pump, no poll. Where a window has to
        // come from the main thread, this can only work without one.
        std::atomic<bool> done{false};
        std::string err;
        std::thread saver([&fig, &done, &err] {
            try {
                fig->savefig_png("headless_worker.png", {}, W, H);
            } catch (const std::exception& e) {
                err = e.what();
            }
            done.store(true);
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!done.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const bool unassisted = done.load();
        check(unassisted,
              "headless: savefig() from a worker thread asks nothing of the main thread");
        // Whatever the answer, the thread has to end: if it is waiting on the
        // pump, pump, rather than hanging the suite on the join.
        if (!unassisted) pump_until([&done] { return done.load(); }, 60.0);
        saver.join();
        check(err.empty(), "headless: and without throwing (" + err + ")");
        check(same_picture("headless_main.png", "headless_worker.png"),
              "headless: for the same picture whichever thread asked for it");
    }
} // namespace lt
