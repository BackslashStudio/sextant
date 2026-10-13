// Hiding an axis' tick marks and labels: AxesStyle::show_xticks /
// show_yticks / show_zticks. The layout stops reserving room, the grid stays,
// and set_xticks({}) still means "automatic". Part of sextant_layout_test.
#include "layout_test.h"

namespace lt {
    using namespace sextant;

    namespace {
        std::size_t count(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }

        // The plot frame: the first axes-background rect in an SVG.
        bool frame_of(const std::string& svg, PlotRect& out) {
            const std::size_t at = svg.find("\" fill=\"white\"/>");
            if (at == std::string::npos) return false;
            const std::size_t start = svg.rfind("<rect x=\"", at);
            if (start == std::string::npos) return false;
            return std::sscanf(svg.c_str() + start, "<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\"",
                               &out.x, &out.y, &out.w, &out.h) == 4;
        }

        std::string svg2d(AxesStyle st, bool grid) {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(std::vector<double>{0.0, 1.0, 2.0}, std::vector<double>{0.0, 3.0, 1.0});
            ax->set_axes_style(st);
            ax->grid(grid);
            return fig->render_svg().svg;
        }

        std::string svg3d(AxesStyle st) {
            auto fig = Figure::create();
            auto ax = fig->add_subplot3d(1, 1, 1);
            const std::vector<double> u{0.0, 1.0, 2.0}, v{0.0, 1.0};
            const std::vector<double> h{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
            ax->surface(PlaneOrientation::XY, u, v, h);
            ax->set_axes_style(st);
            return fig->render_svg().svg;
        }
    } // namespace

    void test_ticks_hidden() {
        std::printf("\n[ticks hidden: marks and labels per axis]\n");

        const AxesStyle shown;
        AxesStyle no_x, no_y, none;
        no_x.show_xticks = false;
        no_y.show_yticks = false;
        none.show_xticks = none.show_yticks = false;

        // Defaults draw both axes' ticks.
        const std::string all = svg2d(shown, false);
        const std::size_t labels_all = count(all, "<text"), marks_all = count(all, "<line");
        check(labels_all > 0 && marks_all > 0, "default: tick labels and marks are drawn");

        // One axis at a time.
        const std::string nx = svg2d(no_x, false), ny = svg2d(no_y, false);
        check(count(nx, "<text") > 0 && count(nx, "<text") < labels_all
                  && count(nx, "<line") > 0 && count(nx, "<line") < marks_all,
              "hiding x drops its labels and marks and keeps y's");
        check(count(ny, "<text") > 0 && count(ny, "<text") < labels_all
                  && count(ny, "<line") > 0 && count(ny, "<line") < marks_all,
              "hiding y drops its labels and marks and keeps x's");
        check(count(nx, "<text") + count(ny, "<text") == labels_all,
              "the two axes' labels add up to the default's");

        // Both: nothing is left, and the frame grows into the room.
        const std::string off = svg2d(none, false);
        check(count(off, "<text") == 0 && count(off, "<line") == 0, "hiding both draws no labels or marks");
        PlotRect fa, fx, fy, fo;
        check(frame_of(all, fa) && frame_of(nx, fx) && frame_of(ny, fy) && frame_of(off, fo),
              "the SVGs carry a frame rect");
        // x labels also overhang the frame's left and right ends, so the width may grow too.
        check(fx.h > fa.h && fx.w >= fa.w && fx.x == fa.x, "hiding x frees the bottom band, not the left one");
        check(fy.w > fa.w && fy.h >= fa.h, "hiding y frees the left band");
        check(fo.w > fa.w && fo.h > fa.h, "hiding both frees both");

        // The grid stays: its lines are the difference between with and without.
        const std::size_t grid_lines = count(svg2d(shown, true), "<line") - marks_all;
        check(grid_lines > 0, "the grid has lines");
        check(count(svg2d(none, true), "<line") == grid_lines && count(svg2d(none, true), "<text") == 0,
              "with both hidden, the grid lines stay and the labels are gone");

        // The raster path agrees: ink beside the frame is there by default and
        // gone with the axis hidden (marks and labels both live outside the frame).
        {
            auto ink = [](AxesStyle st, bool left_of_frame) {
                auto fig = Figure::create();
                auto ax = fig->add_subplot(1, 1, 1);
                ax->line(std::vector<double>{0.0, 1.0, 2.0}, std::vector<double>{0.0, 3.0, 1.0});
                ax->set_axes_style(st);
                PlotRect f;
                if (!frame_of(fig->render_svg().svg, f)) return std::size_t(-1);
                const RgbaImage img = fig->render_rgba();
                std::size_t n = 0;
                for (int y = 0; y < img.height; ++y)
                    for (int x = 0; x < img.width; ++x) {
                        const bool in_band = left_of_frame
                            ? (x < f.x - 3.0f && y >= f.y && y <= f.y + f.h)
                            : (y > f.y + f.h + 3.0f && x >= f.x && x <= f.x + f.w);
                        if (!in_band) continue;
                        const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
                        if (img.pixels[i] < 200) ++n;   // dark ink on the light figure background
                    }
                return n;
            };
            check(ink(shown, true) > 0 && ink(shown, false) > 0, "raster: y and x ticks put ink beside the frame");
            check(ink(no_y, true) == 0 && ink(no_y, false) > 0, "raster: hiding y clears the left band only");
            check(ink(no_x, false) == 0 && ink(no_x, true) > 0, "raster: hiding x clears the bottom band only");
        }

        // An empty tick list is still "automatic", not "none".
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(std::vector<double>{0.0, 1.0, 2.0}, std::vector<double>{0.0, 3.0, 1.0});
            ax->set_xticks(std::vector<double>{});
            check(count(fig->render_svg().svg, "<text") == labels_all, "set_xticks({}) still means automatic ticks");
        }

        // 3D: per axis, no room reserved there, labels and marks gone.
        AxesStyle no_z, none3;
        no_z.show_zticks = false;
        none3.show_xticks = none3.show_yticks = none3.show_zticks = false;
        const std::size_t t3_all = count(svg3d(shown), "<text");
        check(t3_all > 0, "3D default: tick labels are drawn");
        const std::size_t t3_noz = count(svg3d(no_z), "<text");
        check(t3_noz > 0 && t3_noz < t3_all, "3D: hiding z drops its labels only");
        check(count(svg3d(none3), "<text") == 0, "3D: hiding all three draws no tick labels");

        // In a grid (36g): a hidden axis gives its side back. Unshared frames are
        // each their own fit; a sharex column keeps one x span, its widest
        // member's (the 2D layout retrofit).
        {
            constexpr int W = 900, H = 600;
            auto grid = [](int rows, int cols) {
                FigureSnapshot fs = make_snapshot(rows, cols, rows * cols);
                for (auto& fa: fs.axes) {
                    RenderSnapshot& s = *fa.snap2d();
                    s.xtitle = "x";
                    s.ytitle = "y";
                }
                fs.generation = fs.data_generation = fs.layout_generation = 1;
                return fs;
            };

            // Two rows: the top cell's y labels are wide, the bottom's narrow.
            auto column = [&](bool hide_top, bool shared) {
                FigureSnapshot fs = grid(2, 1);
                RenderSnapshot& top = *fs.axes[0].snap2d();
                top.ymin = 0.0; top.ymax = 1.0e6; top.ylim_auto = false;
                top.axes_style.show_yticks = !hide_top;
                if (shared) top.xshare = fs.axes[1].snap2d()->xshare = 7;
                return compute_figure_layout(fs, W, H);
            };
            const FigureLayout shown2 = column(false, false), hidden2 = column(true, false);
            check(shown2.cells[0].frame.x > shown2.cells[1].frame.x + 1.0f,
                  "grid: unshared, the wide labels push only their own frame in");
            check(hidden2.cells[0].frame.x < hidden2.cells[1].frame.x,
                  "grid: the hidden cell's frame takes its left band back");
            const PlotRect& was = shown2.cells[1].frame;
            const PlotRect& now = hidden2.cells[1].frame;
            check(now.x == was.x && now.w == was.w && now.y == was.y && now.h == was.h,
                  "grid: the other cell of its column does not move");
            check(hidden2.cells[0].ytitle_x > hidden2.cells[0].cell.x
                      && hidden2.cells[0].ytitle_x < hidden2.cells[0].frame.x,
                  "grid: its y title sits in its own band, beside its frame");

            const FigureLayout shared2 = column(false, true), shared_hidden = column(true, true);
            check(near_px(shared2.cells[0].frame.x, shared2.cells[1].frame.x, 1e-3f)
                      && near_px(shared2.cells[0].frame.w, shared2.cells[1].frame.w, 1e-3f)
                      && near_px(shared2.cells[0].frame.x, shown2.cells[0].frame.x, 1e-3f),
                  "grid: sharex, the column's frames line up on the widest member's labels");
            check(near_px(shared_hidden.cells[0].frame.x, shared_hidden.cells[1].frame.x, 1e-3f)
                      && near_px(shared_hidden.cells[1].frame.x, shown2.cells[1].frame.x, 1e-3f),
                  "grid: sharex, hiding them lines both up on the other member's");

            // Two columns: the left cell hides x.
            FigureSnapshot fs = grid(1, 2);
            fs.axes[0].snap2d()->axes_style.show_xticks = false;
            const FigureLayout row = compute_figure_layout(fs, W, H);
            const PlotRect& l = row.cells[0].frame;
            const PlotRect& r = row.cells[1].frame;
            check(l.y + l.h > r.y + r.h + 1.0f && near_px(l.y, r.y, 1e-3f),
                  "grid: hiding x in one cell of a row frees its bottom band only");
            const FigureLayout row0 = compute_figure_layout(grid(1, 2), W, H);
            const PlotRect& r0 = row0.cells[1].frame;
            check(r.x == r0.x && r.w == r0.w && r.y == r0.y && r.h == r0.h,
                  "grid: and its neighbour does not move");
            check(row.cells[0].xtitle_y > l.y + l.h
                      && near_px(row.cells[0].xtitle_y, row.cells[1].xtitle_y, 1e-3f),
                  "grid: its x title stays below its frame, level with its neighbour's");

            // The inverse agrees: the hidden cell's frame comes out as asked.
            const LayoutSize sz = figure_size_for_frame(fs, 1, 300.0f, 200.0f);
            const FigureLayout inv = compute_figure_layout(fs, int(std::lround(sz.width)),
                                                           int(std::lround(sz.height)));
            check(near_px(inv.cells[0].frame.w, 300.0f, 1.0f) && near_px(inv.cells[0].frame.h, 200.0f, 1.0f),
                  "grid: figure_size_for_frame() gives the hidden cell the frame asked for");
        }
    }
} // namespace lt
