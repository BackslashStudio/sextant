// Headless export with no display (v1.1 step 27).
//
//   sextant_headless_probe <out_dir> [--window]
//
// Renders gallery figures (2D and 3D) to PNG, SVG and raw RGBA, from the main
// thread and from worker threads, through the public API. By default headless
// exports take the platform's windowless context -- EGL on Linux, CGL on macOS
// -- and on Linux CI this runs with no X or Wayland display at all. --window
// turns that off (a test switch, not API) so they take the hidden-window path,
// and CI compares the two output directories byte for byte. With no DISPLAY on
// Linux it also checks that show() fails by throwing, not by ending the process.
//
// Exit 0 when every check passed, 1 otherwise.
#include <sextant/sextant.h>
#include "platform/platform.h"
#include "renderer/gl_context.h"
#include "window_broker.h"
#include <glad/glad.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
    int g_failures = 0;

    void check(bool ok, const std::string& what) {
        std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++g_failures;
    }

    std::string slurp(const std::filesystem::path& p) {
        std::ifstream f(p, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    }

    const char* env(const char* name) {
        const char* v = std::getenv(name);
        return v && *v ? v : "(unset)";
    }

    // Line, scatter, bars with error bars, a heatmap with labelled contours and
    // a colorbar, a colormapped scatter, a suptitle.
    std::shared_ptr<sextant::Figure> figure_2d() {
        using namespace sextant;
        auto fig = Figure::create({.width = 640, .height = 480, .title = "probe 2d"});
        fig->suptitle("headless probe");
        std::vector<double> x(80), s(80), c(80);
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = static_cast<double>(i) * 0.08;
            s[i] = std::sin(x[i]);
            c[i] = std::cos(x[i]);
        }
        fig->add_subplot(2, 2, 1)->line(x, s, {.color = Color::Blue, .name = "sin"})
            .scatter(x, c, {.color = Color::Red, .size = 10.0f, .name = "cos"})
            .set_title("line & scatter").legend();

        const std::vector<double> bx{1, 2, 3, 4, 5}, bh{3, 5, 2, 6, 4}, be{0.4, 0.6, 0.3, 0.8, 0.5};
        fig->add_subplot(2, 2, 2)->bar(bx, bh, ErrorBar{.y_cap_lo = be, .y_cap_hi = be},
                                       {.color = Color::Orange})
            .set_title("bars");

        constexpr int R = 30, C = 40;
        std::vector<double> cells(R * C);
        for (int r = 0; r < R; ++r)
            for (int k = 0; k < C; ++k)
                cells[r * C + k] = 0.5 + 0.5 * std::sin(k * 0.25) * std::cos(r * 0.2);
        fig->add_subplot(2, 2, 3)->heatmap(cells, R, C, {0, 4}, {0, 3},
                                           {.colorbar = true, .name = "level",
                                            .contours = {0.3, 0.5, 0.7}, .contour_labels = true})
            .set_title("heatmap");

        fig->add_subplot(2, 2, 4)->scatter_z(x, s, c, {.vmin = -1, .vmax = 1, .colorbar = true})
            .set_title("scatter_z");
        return fig;
    }

    // Edged bars crossed by a plane, a translucent colormapped surface (depth
    // peeling), and a scatter/line pair.
    std::shared_ptr<sextant::Figure> figure_3d() {
        using namespace sextant;
        auto fig = Figure::create({.width = 640, .height = 360, .title = "probe 3d"});
        auto a = fig->add_subplot3d(1, 2, 1);
        const std::vector<double> u{0, 1, 2, 3}, v{0, 1, 2};
        std::vector<double> h(u.size() * v.size());
        for (std::size_t i = 0; i < h.size(); ++i) h[i] = 1.0 + static_cast<double>(i % 5);
        a->bar3d(PlaneOrientation::XY, u, v, h, {.color = Color::Green, .edges = true, .name = "bars"});
        a->plane(PlaneOrientation::ZX, 1.0)
            ->imshow(std::vector<double>{0.1, 0.4, 0.7, 0.9, 0.3, 0.6}, 2, 3);
        a->set_title("bar3d + plane").legend();

        auto b = fig->add_subplot3d(1, 2, 2);
        std::vector<double> gu(14), gv(12), z(gu.size() * gv.size());
        for (std::size_t i = 0; i < gu.size(); ++i) gu[i] = static_cast<double>(i) * 0.4;
        for (std::size_t j = 0; j < gv.size(); ++j) gv[j] = static_cast<double>(j) * 0.4;
        for (std::size_t i = 0; i < gu.size(); ++i)
            for (std::size_t j = 0; j < gv.size(); ++j)
                z[i * gv.size() + j] = std::sin(gu[i]) * std::cos(gv[j]);
        b->surface(PlaneOrientation::XY, gu, gv, z, {.colormap = true, .colorbar = true, .alpha = 0.7f});
        std::vector<double> px(30), py(30), pz(30);
        for (std::size_t i = 0; i < px.size(); ++i) {
            const double t = static_cast<double>(i) * 0.2;
            px[i] = 2.6 + std::cos(t);
            py[i] = 2.2 + std::sin(t);
            pz[i] = -1.0 + 0.07 * static_cast<double>(i);
        }
        b->line3d(px, py, pz, {.color = Color::Red, .linewidth = 2.0f});
        b->scatter3d(px, py, pz, {.color = Color::Black, .size = 6.0f});
        b->set_title("surface + path");
        return fig;
    }
} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::fprintf(stderr, "usage: sextant_headless_probe <out_dir> [--window]\n");
        return 2;
    }
    const std::filesystem::path out = argv[1];
    const bool window = argc > 2 && std::string(argv[2]) == "--window";
    std::filesystem::create_directories(out);

    sextant::platform::set_offscreen_gl_enabled(!window);
    std::printf("=== sextant_headless_probe (%s) ===\n",
                window ? "hidden-window path" : "windowless path");
    std::printf("DISPLAY=%s WAYLAND_DISPLAY=%s\n", env("DISPLAY"), env("WAYLAND_DISPLAY"));

    try {
        // Which context a headless export gets, and what it runs on.
        {
            sextant::GLContext ctx({.width = 16, .height = 16, .title = "probe", .visible = false,
                                    .headless = true});
            const auto* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const auto* v = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            std::printf("context: %s, GL_RENDERER %s, GL_VERSION %s\n",
                        ctx.is_headless() ? "windowless" : "hidden window", r ? r : "?", v ? v : "?");
            if (!window)
                check(ctx.is_headless() == sextant::platform::has_offscreen_gl,
                      "the export context is windowless where the platform has one");
            else
                check(!ctx.is_headless(), "--window: the export context is a hidden window");
        }

        // The main thread.
        auto f2 = figure_2d();
        auto f3 = figure_3d();
        f2->savefig((out / "fig2d.png").string());
        f2->savefig((out / "fig2d.svg").string());
        f2->savefig_png((out / "fig2d_192.png").string(), {.dpi = 192.0f});
        f3->savefig((out / "fig3d.png").string());
        f3->savefig((out / "fig3d.svg").string());
        f3->savefig_png((out / "fig3d_peel16.png").string(), {.peel_layers = 16});
        {
            const sextant::RgbaImage img = f3->render_rgba();
            std::ofstream(out / "fig3d.rgba", std::ios::binary)
                .write(reinterpret_cast<const char*>(img.pixels.data()),
                       static_cast<std::streamsize>(img.pixels.size()));
            check(img.width == 640 && img.height == 360
                      && img.pixels.size() == std::size_t(640) * 360 * 4,
                  "render_rgba() is the figure's size, RGBA8");
        }
        check(std::filesystem::file_size(out / "fig2d.png") > 10000
                  && std::filesystem::file_size(out / "fig3d.png") > 10000,
              "the PNGs were written");

        // Worker threads, each with its own figure (a figure's calls must not
        // overlap), all at once: no main thread involved, no window.
        constexpr int kWorkers = 4;
        std::vector<std::string> errors(kWorkers);
        {
            std::vector<std::thread> ts;
            for (int i = 0; i < kWorkers; ++i)
                ts.emplace_back([i, &out, &errors] {
                    try {
                        figure_3d()->savefig((out / ("worker" + std::to_string(i) + ".png")).string());
                    } catch (const std::exception& e) {
                        errors[i] = e.what();
                    }
                });
            for (auto& t : ts) t.join();
        }
        bool workers_ok = true, workers_same = true;
        const std::string ref = slurp(out / "fig3d.png");
        for (int i = 0; i < kWorkers; ++i) {
            if (!errors[i].empty()) {
                std::printf("  worker %d: %s\n", i, errors[i].c_str());
                workers_ok = false;
            }
            workers_same = workers_same && slurp(out / ("worker" + std::to_string(i) + ".png")) == ref;
        }
        check(workers_ok, "4 worker threads export at once, none throws");
        check(workers_same, "each worker's PNG is the main thread's, byte for byte");

#if defined(__linux__)
        // No display: show() cannot make a window. It must throw to its caller
        // -- the failure used to escape on the window thread and std::terminate
        // the process -- and leave the figure as if never shown.
        if (!window && !std::getenv("DISPLAY")) {
            std::string what;
            try {
                f2->show(false);
            } catch (const std::runtime_error& e) {
                what = e.what();
            }
            std::printf("  show() with no display: %s\n", what.c_str());
            check(what.find("DISPLAY") != std::string::npos,
                  "show() with no display throws runtime_error, naming DISPLAY");
            check(!f2->is_open() && f2->wait_closed(0), "the figure is not open afterwards");
            bool again = false;
            try { f2->show(false); } catch (const std::runtime_error&) { again = true; }
            check(again, "a second show() throws again");
            // A leaked registration would make run() wait for a window that
            // does not exist.
            auto ran = std::async(std::launch::async, [] { sextant::Figure::run(); });
            const bool returned = ran.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
            check(returned, "Figure::run() returns: no open window is registered");
            if (!returned) {
                std::printf("FAILED\n");
                std::fflush(stdout);
                std::_Exit(1);
            }
            f2->savefig((out / "after_show.png").string());
            check(slurp(out / "after_show.png") == slurp(out / "fig2d.png"),
                  "savefig() after the failed show() writes the same PNG");
        }
#endif

        if (!window)
            check(sextant::live_window_count() == 0, "no window is left behind");
    } catch (const std::exception& e) {
        std::printf("  FAIL: exception: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%s\n", g_failures ? "FAILED" : "OK");
    return g_failures ? 1 : 0;
}
