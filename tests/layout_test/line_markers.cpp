// Markers on a line (LineOptions::marker / markersize): drawn in the line's
// color at its finite points, on the GPU, in the SVG, on a Plane2D, keyed in the
// legend; markersize checked; set_line_options() turns them on and off. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lt {
    using namespace sextant;

    namespace {
        const std::vector<double> kX{2.0, 5.0, 8.0}, kY{5.0, 5.0, 5.0};

        // Pixels within `tol` of an opaque RGB.
        std::size_t ink(const RgbaImage& img, int r, int g, int b, int tol) {
            std::size_t n = 0;
            for (std::size_t i = 0; i + 3 < img.pixels.size(); i += 4)
                if (std::abs(img.pixels[i] - r) <= tol && std::abs(img.pixels[i + 1] - g) <= tol &&
                    std::abs(img.pixels[i + 2] - b) <= tol)
                    ++n;
            return n;
        }

        std::size_t count_of(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }

        LineOptions blue(MarkerStyle m, LineStyle ls = LineStyle::None) {
            LineOptions o;
            o.color = Color{0.0f, 0.0f, 1.0f, 1.0f};
            o.linestyle = ls;
            o.marker = m;
            o.markersize = 30.0f;
            return o;
        }

        std::shared_ptr<Figure> one(const std::vector<double>& x, const std::vector<double>& y,
                                    const LineOptions& o, bool legend = false) {
            FigureOptions fo;
            fo.width = 400;
            fo.height = 300;
            fo.supersample = 1;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 10).set_ylim(0, 10);
            ax->line(x, y, o);
            if (legend) {
                // Inside, so the plot area is the same with and without it.
                LegendOptions lo;
                lo.anchor = LegendAnchor::InsideTL;
                ax->legend(lo);
            }
            return fig;
        }
    } // namespace

    void test_line_markers() {
        std::printf("\n[line markers]\n");

        // --- GPU ---------------------------------------------------------------
        {
            const std::size_t none = ink(one(kX, kY, blue(MarkerStyle::None))->render_rgba(), 0, 0, 255, 40);
            const std::size_t sq = ink(one(kX, kY, blue(MarkerStyle::Square))->render_rgba(), 0, 0, 255, 40);
            check(none == 0, "no marker, no stroke: nothing drawn (today's look)");
            // A 30 px square is ~900 px; three of them.
            check(sq > 3 * 700 && sq < 3 * 1100,
                  "Square markers alone (LineStyle::None): three ~30 px squares, " + std::to_string(sq) + " px");

            std::vector<double> gap = kY;
            gap[1] = std::numeric_limits<double>::quiet_NaN();
            const std::size_t two = ink(one(kX, gap, blue(MarkerStyle::Square))->render_rgba(), 0, 0, 255, 40);
            check(two > 2 * 700 && two < 2 * 1100, "a NaN point gets no marker: two squares, " +
                                                       std::to_string(two) + " px");

            const std::size_t stroke =
                    ink(one(kX, kY, blue(MarkerStyle::None, LineStyle::Solid))->render_rgba(), 0, 0, 255, 40);
            const std::size_t both =
                    ink(one(kX, kY, blue(MarkerStyle::Square, LineStyle::Solid))->render_rgba(), 0, 0, 255, 40);
            check(both > stroke + 3 * 500, "markers over the stroke, in the line's color");

            LineOptions half = blue(MarkerStyle::Square);
            half.alpha = 0.0f;
            check(ink(one(kX, kY, half)->render_rgba(), 0, 0, 255, 40) == 0, "markers take the line's alpha");
        }

        // --- SVG ---------------------------------------------------------------
        {
            const std::string none = one(kX, kY, blue(MarkerStyle::None))->render_svg().svg;
            const std::string circ = one(kX, kY, blue(MarkerStyle::Circle))->render_svg().svg;
            check(count_of(circ, "<circle") == count_of(none, "<circle") + 3, "SVG: three circle markers");
            std::vector<double> gap = kY;
            gap[2] = std::numeric_limits<double>::quiet_NaN();
            const std::string g = one(kX, gap, blue(MarkerStyle::Circle))->render_svg().svg;
            check(count_of(g, "<circle") == count_of(none, "<circle") + 2, "SVG: a NaN point gets no marker");
        }

        // --- Legend ------------------------------------------------------------
        // The data sits low and the legend top left inside, so they don't overlap and a
        // keyed image minus its unkeyed twin is the key's own ink.
        {
            const std::vector<double> low{2.0, 5.0, 8.0}, ylow{1.0, 1.0, 1.0};
            auto key_ink = [&](LineOptions o) {
                const std::size_t without = ink(one(low, ylow, o)->render_rgba(), 0, 0, 255, 40);
                o.name = "key";
                const std::size_t with = ink(one(low, ylow, o, true)->render_rgba(), 0, 0, 255, 40);
                return static_cast<long>(with) - static_cast<long>(without);
            };
            LineOptions o = blue(MarkerStyle::None);
            o.name = "key";
            check(ink(one(low, ylow, o, true)->render_rgba(), 0, 0, 255, 40) == 0,
                  "legend: a LineStyle::None line without markers has no key");
            const long alone = key_ink(blue(MarkerStyle::Square));
            check(alone > 20, "legend: with markers alone, the key is the marker (" + std::to_string(alone) + ")");
            const long plain = key_ink(blue(MarkerStyle::None, LineStyle::Solid));
            const long marked = key_ink(blue(MarkerStyle::Square, LineStyle::Solid));
            check(plain > 5 && marked > plain + 20,
                  "legend: a stroke with markers keys as the stroke with the marker on it (" +
                      std::to_string(marked) + " vs " + std::to_string(plain) + ")");
            LineOptions s = blue(MarkerStyle::Square, LineStyle::Solid);
            s.name = "key";
            LineOptions su = blue(MarkerStyle::Square, LineStyle::Solid);
            const std::string svg = one(kX, kY, s, true)->render_svg().svg;
            const std::string svg_plain = one(kX, kY, su)->render_svg().svg;
            check(count_of(svg, "<rect") > count_of(svg_plain, "<rect") + 1 ||
                      count_of(svg, "<polygon") > count_of(svg_plain, "<polygon"),
                  "legend SVG: the key carries the marker");
        }

        // --- Validation and set_line_options ---------------------------------
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            LineOptions bad;
            bad.markersize = -1.0f;
            bool threw = false;
            try { ax->line(kX, kY, bad); } catch (const std::invalid_argument&) { threw = true; }
            check(threw && ax->line_count() == 0, "markersize < 0 throws, nothing plotted");
            bad.markersize = std::numeric_limits<float>::quiet_NaN();
            threw = false;
            try { ax->line(kX, kY, bad); } catch (const std::invalid_argument&) { threw = true; }
            check(threw, "markersize NaN throws");

            ax->line(kX, kY);
            LineOptions o = ax->line_options(0);
            check(o.marker == MarkerStyle::None && o.markersize == 20.0f, "defaults: no marker, 20 px");
            o.marker = MarkerStyle::Diamond;
            o.markersize = 12.0f;
            ax->set_line_options(0, o);
            const LineOptions r = ax->line_options(0);
            check(r.marker == MarkerStyle::Diamond && r.markersize == 12.0f, "set_line_options: markers on");
            o.markersize = -2.0f;
            threw = false;
            try { ax->set_line_options(0, o); } catch (const std::invalid_argument&) { threw = true; }
            check(threw && ax->line_options(0).markersize == 12.0f, "set_line_options: a bad markersize throws");
        }

        // --- Plane2D ------------------------------------------------------------
        {
            FigureOptions fo;
            fo.width = 400;
            fo.height = 300;
            fo.supersample = 1;
            auto fig = Figure::create(fo);
            auto ax3 = fig->add_subplot3d(1, 1, 1);
            auto pl = ax3->plane(PlaneOrientation::XY, 0.5);
            const std::vector<double> x{0.2, 0.5, 0.8}, y{0.5, 0.5, 0.5};
            pl->line(x, y, blue(MarkerStyle::None));
            const std::string none = fig->render_svg().svg;
            const std::size_t ink_none = ink(fig->render_rgba(), 0, 0, 255, 60);
            LineOptions o = pl->line_options(0);
            o.marker = MarkerStyle::Circle;
            pl->set_line_options(0, o);
            const std::size_t ink_marked = ink(fig->render_rgba(), 0, 0, 255, 60);
            check(ink_marked > ink_none + 3 * 20, "Plane2D: markers drawn on the GPU (" + std::to_string(ink_none) + " -> " +
                                                        std::to_string(ink_marked) + ")");
            check(fig->render_svg().svg.size() > none.size(), "Plane2D: markers in the SVG");
        }
    }
} // namespace lt
