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

    // -------------------------------------------------------------------------
    // dpi: output pixels scale, the layout does not (v1.0 step 22.4)
    // -------------------------------------------------------------------------
    void test_png_dpi() {
        std::printf("\n[PNG dpi]\n");

        auto make = [](float dpi) {
            auto fig = Figure::create({.width = W, .height = H, .title = "dpi", .dpi = dpi});
            std::vector<double> x(40), y(40);
            for (std::size_t i = 0; i < x.size(); ++i) {
                x[i] = static_cast<double>(i) * 0.25;
                y[i] = std::sin(x[i]);
            }
            fig->axes()->line(x, y, {.color = Color::Blue, .linewidth = 2.0f});
            fig->axes()->set_title("dpi");
            fig->axes()->set_xtitle("x");
            return fig;
        };
        auto dims = [](const std::string& p) {
            int w = 0, h = 0, comp = 0;
            if (!stbi_info(p.c_str(), &w, &h, &comp)) w = h = -1;
            return std::pair{w, h};
        };

        auto fig = make(96.0f);
        fig->savefig_png("dpi_96.png");
        fig->savefig_png("dpi_192.png", {.dpi = 192.0f});
        fig->savefig_png("dpi_144.png", {.dpi = 144.0f});
        check(dims("dpi_96.png") == std::pair{W, H},
              "dpi: the default writes the figure's own size");
        check(dims("dpi_192.png") == std::pair{2 * W, 2 * H},
              "dpi: 192 writes twice the pixels");
        check(dims("dpi_144.png") == std::pair{W * 3 / 2, H * 3 / 2},
              "dpi: 144 writes one and a half times, a fractional scale");

        auto fig2 = make(192.0f);
        fig2->savefig_png("dpi_fig192.png");
        check(dims("dpi_fig192.png") == std::pair{2 * W, 2 * H},
              "dpi: FigureOptions::dpi is the default for every PNG of that figure");

        // Same layout, more pixels: the 2x file box-filtered to 1x is the 1x
        // file, up to how text and edges rasterize. A layout that moved with the
        // dpi -- text twice the size, a margin in output pixels -- misses by far
        // more than that.
        {
            int w1 = 0, h1 = 0, w2 = 0, h2 = 0, comp = 0;
            unsigned char* a = stbi_load("dpi_96.png", &w1, &h1, &comp, 4);
            unsigned char* b = stbi_load("dpi_192.png", &w2, &h2, &comp, 4);
            double sum = 0.0;
            bool ok = a && b && w2 == 2 * w1 && h2 == 2 * h1;
            if (ok) {
                for (int y = 0; y < h1; ++y)
                    for (int x = 0; x < w1; ++x)
                        for (int k = 0; k < 3; ++k) {
                            int s = 0;
                            for (int dy = 0; dy < 2; ++dy)
                                for (int dx = 0; dx < 2; ++dx)
                                    s += b[((2 * y + dy) * w2 + 2 * x + dx) * 4 + k];
                            sum += std::abs(s / 4.0 - a[(y * w1 + x) * 4 + k]);
                        }
                sum /= static_cast<double>(w1) * h1 * 3;
            }
            if (a) stbi_image_free(a);
            if (b) stbi_image_free(b);
            std::printf("  2x filtered to 1x vs 1x: mean |delta| %.2f levels\n", sum);
            check(ok && sum < 3.0, "dpi: the 2x file is the 1x layout at twice the resolution");
        }

        auto throws = [](auto&& f) {
            try { f(); } catch (const std::invalid_argument&) { return true; }
            return false;
        };
        check(throws([&] { fig->savefig_png("dpi_bad.png", {.dpi = -1.0f}); }),
              "dpi: a negative PngExportOptions::dpi throws");
        check(throws([&] { (void) Figure::create({.dpi = 0.0f}); }),
              "dpi: FigureOptions::dpi of 0 throws (it has no 'default' to fall back to)");
    }
} // namespace lt
