// One alpha rule: every part of an object (fill, outline, error bars) is its own
// color's alpha times the object's `alpha`, in the SVG as in the raster; and a
// hollow marker (`filled` false). Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    using namespace sextant;

    namespace {
        bool has(const std::string& svg, const std::string& needle) {
            return svg.find(needle) != std::string::npos;
        }

        // Fill at 0.5 * 0.5 = 0.25, outline at 0.8 * 0.5 = 0.4.
        constexpr Color kFace{0.0f, 0.0f, 1.0f, 0.5f};
        constexpr Color kEdge{1.0f, 0.0f, 0.0f, 0.8f};

        const std::vector<double> kX{1.0, 2.0, 3.0}, kY{1.0, 2.0, 1.5}, kErr{0.3, 0.3, 0.3};
        const ErrorBar kBars{.y_cap_lo = kErr, .y_cap_hi = kErr};

        std::string svg_of(const auto& draw) {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 4).set_ylim(0, 4);
            draw(*ax);
            return fig->render_svg().svg;
        }
    } // namespace

    void test_alpha() {
        std::printf("\n[alpha: color.a times alpha, on every part]\n");

        const std::string line = svg_of([](Axes& ax) {
            ax.line(kX, kY, kBars, {.color = kFace, .alpha = 0.5f});
        });
        check(has(line, "stroke-opacity=\"0.25\" stroke-width"), "line: the stroke is color.a * alpha");
        check(has(line, "stroke=\"rgb(0,0,255)\" stroke-opacity=\"0.25\" stroke-width=\"1\""),
              "line: and its error bars too");

        const std::string scatter = svg_of([](Axes& ax) {
            ax.scatter(kX, kY, kBars,
                       {.color = kFace, .alpha = 0.5f, .edgecolor = kEdge, .edge_linewidth = 2.0f});
        });
        check(has(scatter, "fill-opacity=\"0.25\""), "scatter: the fill is color.a * alpha");
        check(has(scatter, "stroke=\"rgb(255,0,0)\" stroke-opacity=\"0.4\""),
              "scatter: the outline is edgecolor.a * alpha");
        check(has(scatter, "stroke=\"rgb(0,0,255)\" stroke-opacity=\"0.25\""),
              "scatter: the error bars are their color's alpha * alpha");

        const std::string hollow = svg_of([](Axes& ax) {
            ax.scatter(kX, kY, {.color = kFace, .alpha = 0.5f, .filled = false, .edge_linewidth = 2.0f});
        });
        check(has(hollow, "fill-opacity=\"0\"") && has(hollow, "stroke=\"rgb(0,0,255)\" stroke-opacity=\"0.25\""),
              "scatter: filled false is the outline alone, in the face's color");

        const std::string bar = svg_of([](Axes& ax) {
            ax.bar(kX, kY, kBars, {.color = kFace, .alpha = 0.5f, .edgecolor = kEdge, .linewidth = 2.0f});
        });
        check(has(bar, "fill-opacity=\"0.25\" stroke=\"rgb(255,0,0)\" stroke-opacity=\"0.4\""),
              "bar: fill and outline each take alpha");
        check(has(bar, "<line") && has(bar, "stroke=\"rgb(255,0,0)\" stroke-opacity=\"0.4\" stroke-width=\"1\""),
              "bar: its error bars (in edgecolor) too");

        const std::string span = svg_of([](Axes& ax) {
            ax.axspan(1.0, 2.0, {.color = kFace, .alpha = 0.5f, .edgecolor = kEdge, .linewidth = 2.0f});
        });
        check(has(span, "fill-opacity=\"0.25\"") && has(span, "stroke-opacity=\"0.4\""),
              "axspan: fill and outline each take alpha");

        const std::string fill = svg_of([](Axes& ax) {
            ax.fill(kX, kY, std::vector<double>{0.0, 0.0, 0.0},
                    {.color = kFace, .alpha = 0.5f, .edgecolor = kEdge, .linewidth = 2.0f});
        });
        check(has(fill, "fill-opacity=\"0.25\"") && has(fill, "stroke-opacity=\"0.4\""),
              "fill: fill and outline each take alpha");

        const std::string circle = svg_of([](Axes& ax) {
            ax.shape_circle(Pos(2.0), Pos(2.0), Pos(1.0),
                            {.color = kFace, .alpha = 0.5f, .edgecolor = kEdge, .linewidth = 2.0f});
        });
        check(has(circle, "fill-opacity=\"0.25\"") && has(circle, "stroke-opacity=\"0.4\""),
              "shape: fill and outline each take alpha");
    }
} // namespace lt
