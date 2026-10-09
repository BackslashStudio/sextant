// Figure and plot-area background colors (v1.1 step 33): the pixels of a PNG
// render, the SVG, set_background() after create(), and alpha 0 staying
// transparent. Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    using namespace sextant;

    namespace {
        struct Px { int r, g, b, a; };

        Px pixel(const RgbaImage& img, int x, int y) {
            const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
            return { img.pixels[i], img.pixels[i + 1], img.pixels[i + 2], img.pixels[i + 3] };
        }

        bool near(const Px& p, int r, int g, int b, int a, int tol = 2) {
            return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol
                && std::abs(p.b - b) <= tol && std::abs(p.a - a) <= tol;
        }

        // Pixels of the image that are `r,g,b` opaque (the plot area's fill).
        std::size_t count_color(const RgbaImage& img, int r, int g, int b) {
            std::size_t n = 0;
            for (int y = 0; y < img.height; ++y)
                for (int x = 0; x < img.width; ++x)
                    if (near(pixel(img, x, y), r, g, b, 255, 3)) ++n;
            return n;
        }

        std::shared_ptr<Figure> make(FigureOptions o) {
            auto fig = Figure::create(o);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0});
            return fig;
        }
    } // namespace

    void test_background() {
        std::printf("\n[background: figure and plot-area colors (step 33)]\n");

        // Default: the long-standing gray outside the plot, white inside.
        {
            auto fig = make({});
            const RgbaImage img = fig->render_rgba();
            check(near(pixel(img, 1, 1), 237, 237, 237, 255), "default figure background is gray 0.93");
            check(count_color(img, 255, 255, 255) > 1000, "default plot area is white");
            check(fig->render_svg().svg.find("fill=\"#ededed\"") != std::string::npos,
                  "default SVG background is #ededed");
        }

        // Through the options.
        {
            FigureOptions o;
            o.background = {0.0f, 0.0f, 1.0f, 1.0f};
            auto fig = make(o);
            const RgbaImage img = fig->render_rgba();
            check(near(pixel(img, 1, 1), 0, 0, 255, 255), "FigureOptions::background colors the figure");
            check(fig->render_svg().svg.find("fill=\"#0000ff\"") != std::string::npos,
                  "the SVG background follows");
        }

        // After create(), through set_background().
        {
            auto fig = make({});
            fig->render_rgba();
            fig->set_background({0.0f, 1.0f, 0.0f, 1.0f});
            const RgbaImage img = fig->render_rgba();
            check(near(pixel(img, 1, 1), 0, 255, 0, 255), "set_background() recolors the next render");
        }

        // The plot area.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(std::vector<double>{0.0, 1.0}, std::vector<double>{0.0, 1.0});
            ax->set_axes_style({.background = {1.0f, 0.0f, 0.0f, 1.0f}});
            const RgbaImage img = fig->render_rgba();
            check(count_color(img, 255, 0, 0) > 1000 && count_color(img, 255, 255, 255) < 100,
                  "AxesStyle::background fills the plot area");
            check(fig->render_svg().svg.find("fill=\"#ff0000\"") != std::string::npos,
                  "the SVG plot-area fill follows");
        }

        // Alpha 0 stays transparent, in PNG pixels and SVG.
        {
            FigureOptions o;
            o.background = {0.0f, 0.0f, 0.0f, 0.0f};
            auto fig = make(o);
            const RgbaImage img = fig->render_rgba();
            check(pixel(img, 1, 1).a == 0, "alpha 0 leaves the figure transparent in the PNG");
            check(fig->render_svg().svg.find("<rect width=") == std::string::npos,
                  "alpha 0 writes no background rect in the SVG");
        }

        // Supersampling keeps the color exact.
        {
            FigureOptions o;
            o.background = {1.0f, 0.5f, 0.0f, 1.0f};
            o.supersample = 3;
            const RgbaImage img = make(o)->render_rgba();
            check(near(pixel(img, 1, 1), 255, 128, 0, 255, 2), "supersampling keeps the background color");
        }
    }
} // namespace lt
