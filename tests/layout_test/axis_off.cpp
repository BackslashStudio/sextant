// Axis off: Axes::set_axis_visible(false) hides the spines, ticks, tick
// labels, x/y titles, grid and background; the layout gives their room to the
// frame; on again draws what was set. Part of sextant_layout_test.
#include "layout_test.h"
#include "figure_impl.h"

namespace lt {
    using namespace sextant;

    namespace {
        std::size_t count(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }

        // The plot frame: axes 0's clip rect (drawn with or without the axis).
        bool frame_of(const std::string& svg, PlotRect& out) {
            const std::size_t at = svg.find("<clipPath id=\"plotArea0\"><rect x=\"");
            if (at == std::string::npos) return false;
            return std::sscanf(svg.c_str() + at, "<clipPath id=\"plotArea0\"><rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\"",
                               &out.x, &out.y, &out.w, &out.h) == 4;
        }

        const std::vector<double> kX{0.0, 1.0, 2.0}, kY{0.0, 3.0, 1.0};

        struct Plot { std::shared_ptr<Figure> fig; std::shared_ptr<Axes> ax; };
        Plot make(bool axis_on, bool toggled = false) {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(kX, kY, LineOptions{.name = "data"});
            ax->set_title("T").set_xtitle("x title").set_ytitle("y title").grid(true).legend();
            if (toggled) ax->set_axis_visible(false);
            ax->set_axis_visible(axis_on);
            return {fig, ax};
        }
        std::shared_ptr<Figure> plot(bool axis_on, bool toggled = false) { return make(axis_on, toggled).fig; }

        void svg_parts() {
            const std::string on = plot(true)->render_svg().svg;
            const std::string off = plot(false)->render_svg().svg;
            check(count(on, "<line") > 0 && on.find("x title") != std::string::npos
                      && on.find("y title") != std::string::npos,
                  "axis on: ticks and axis titles are drawn");
            // The one <line> left is the legend's sample.
            check(count(off, "<line") == 1, "axis off: no tick marks or grid lines");
            check(off.find("x title") == std::string::npos && off.find("y title") == std::string::npos,
                  "axis off: no x/y titles");
            check(off.find(">T<") != std::string::npos && off.find(">data<") != std::string::npos,
                  "axis off: the title and the legend stay");
            check(off.find("fill=\"white\"/>") == std::string::npos || count(off, "fill=\"white\"/>") < count(on, "fill=\"white\"/>"),
                  "axis off: no axes background rect");
            check(off.find("stroke-width") != std::string::npos, "axis off: the data line stays");
            PlotRect fon, foff;
            check(frame_of(on, fon) && frame_of(off, foff), "both SVGs carry the frame's clip rect");
            check(foff.w > fon.w && foff.h > fon.h, "axis off: the frame grows into the freed room");

            const std::string back = plot(true, true)->render_svg().svg;
            check(back == on, "axis off then on again: byte-identical to never off");
        }

        void raster_parts() {
            // Ink left of the frame: the y ticks and title by default, none with the axis off.
            auto ink = [](bool axis_on) {
                auto fig = plot(axis_on);
                PlotRect f;
                if (!frame_of(fig->render_svg().svg, f)) return std::size_t(-1);
                const RgbaImage img = fig->render_rgba();
                std::size_t n = 0;
                for (int y = static_cast<int>(f.y); y < static_cast<int>(f.y + f.h); ++y)
                    for (int x = 0; x < static_cast<int>(f.x) - 3; ++x) {
                        const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
                        if (img.pixels[i] < 200) ++n;
                    }
                return n;
            };
            check(ink(true) > 0, "raster: the y axis puts ink left of the frame");
            check(ink(false) == 0, "raster: axis off leaves no ink left of the frame");
        }

        void twin_layer() {
            auto fig = Figure::create();
            auto host = fig->add_subplot(1, 1, 1);
            host->line(kX, kY);
            auto twin = host->twinx();
            twin->line(kX, kY);
            twin->set_ytitle("twin title");
            const std::string both = fig->render_svg().svg;
            twin->set_axis_visible(false);
            check(!twin->axis_visible() && host->axis_visible(), "twin: the flag is the layer's own");
            const std::string off = fig->render_svg().svg;
            check(both.find("twin title") != std::string::npos && off.find("twin title") == std::string::npos,
                  "twin off: its y title is gone");
            check(count(off, "<line") > 0 && count(off, "<line") < count(both, "<line"),
                  "twin off: its ticks go, the host's stay");
        }

        void panel_edits() {
            auto [fig, ax] = make(true);
            auto& fi = detail::FigureAccess::impl(*fig);
            fi.apply_edits_and_publish();

            FigureEdits e;
            AxesEdit ae;
            ae.axis_on = false;
            e.per_axes.push_back({1, ae});
            const FigureEdits undo = fi.apply_edits(e);
            check(!fi.snapshot_box.load()->axes[0].snap2d()->axis_on, "axis edit: reaches the snapshot");
            fi.apply_edits_and_publish();
            check(!ax->axis_visible(), "axis edit: the caller's replay lands on the live axes");
            fi.apply_edits(undo);
            fi.apply_edits_and_publish();
            check(fi.snapshot_box.load()->axes[0].snap2d()->axis_on && ax->axis_visible(),
                  "axis edit: undo turns it back on");

            // A setter after the panel drew wins.
            FigureEdits late;
            AxesEdit le;
            le.axis_on = true;
            le.style_seen = fi.snapshot_box.load()->axes[0].snap2d()->style_stamps;
            ax->set_axis_visible(false);
            late.per_axes.push_back({1, le});
            fi.apply_edits(late);
            fi.apply_edits_and_publish();
            check(!ax->axis_visible(), "axis edit: a newer set_axis_visible() wins over an older edit");

            ax->cla();
            check(ax->axis_visible(), "cla() turns the axis back on");
        }
    } // namespace

    void test_axis_off() {
        std::printf("\n[axis off: set_axis_visible()]\n");
        svg_parts();
        raster_parts();
        twin_layer();
        panel_edits();
    }
} // namespace lt
