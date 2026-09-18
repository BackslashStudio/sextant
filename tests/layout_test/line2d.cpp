// The 2D line's `loop` (Axes::line() and Plane2D::line()): the stroke shader,
// the SVG element and plane strokes all use segment_count()/segment_ends().
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    using namespace sextant;

    namespace {
        // One 2D axes over fixed 0..10 limits holding `lp`.
        FigureSnapshot one_axes(LinePlot lp) {
            FigureSnapshot fs;
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            RenderSnapshot& s = *fa.snap2d();
            s.xmin = 0.0;
            s.xmax = 10.0;
            s.xlim_auto = false;
            s.ymin = 0.0;
            s.ymax = 10.0;
            s.ylim_auto = false;
            s.lines.push_back(std::move(lp));
            fs.axes.push_back(std::move(fa));
            fs.generation = fs.data_generation = 1;
            return fs;
        }

        // A thick triangle; the third side exists only if `loop` draws it.
        LinePlot triangle(bool loop) {
            LinePlot lp;
            lp.x = std::vector<double>{2.0, 8.0, 5.0};
            lp.y = std::vector<double>{2.0, 2.0, 8.0};
            lp.opts.color = Color{0.8f, 0.0f, 0.0f, 1.0f};
            lp.opts.linewidth = 6.0f;
            lp.opts.loop = loop;
            return lp;
        }

        std::string read_file(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        // The first `tag` element in the SVG, or "".
        std::string element(const std::string& svg, const std::string& tag) {
            const std::size_t p = svg.find("<" + tag + " ");
            if (p == std::string::npos) return {};
            return svg.substr(p, svg.find("/>", p) + 2 - p);
        }

        int point_count(const std::string& el) {
            const std::size_t p = el.find("points=\"");
            if (p == std::string::npos) return -1;
            const std::string pts = el.substr(p + 8, el.find('"', p + 8) - (p + 8));
            return static_cast<int>(std::count(pts.begin(), pts.end(), ','));
        }
    } // namespace

    // -------------------------------------------------------------------------
    // What `loop` means: the segment list, and what it deliberately does not touch
    // -------------------------------------------------------------------------
    void test_line_loop_segments() {
        std::printf("\n[2D line: the loop's segments]\n");

        LinePlot open = triangle(false), closed = triangle(true);

        check(open.segment_count() == 2 && closed.segment_count() == 3,
              "loop: a closed path has one segment more than the open one, and the "
              "same points");
        check(closed.count() == open.count() && closed.x.size() == 3,
              "loop: and adds no point");

        std::size_t a = 0, b = 0;
        closed.segment_ends(2, a, b);
        check(a == 2 && b == 0, "loop: the closing segment runs from the last point to the first");
        closed.segment_ends(0, a, b);
        const std::size_t oa = a, ob = b;
        open.segment_ends(0, a, b);
        check(oa == a && ob == b && a == 0 && b == 1,
              "loop: the segments an open path already had are unaffected");

        // A looped two-point path draws its segment back (as Line3DPlot).
        LinePlot pair;
        pair.x = std::vector<double>{0.0, 1.0};
        pair.y = std::vector<double>{0.0, 1.0};
        pair.opts.loop = true;
        pair.segment_ends(1, a, b);
        check(pair.segment_count() == 2 && a == 1 && b == 0,
              "loop: a two-point path closes onto itself rather than gaining a "
              "segment of its own");

        LinePlot one;
        one.x = std::vector<double>{4.0};
        one.y = std::vector<double>{4.0};
        one.opts.loop = true;
        check(one.segment_count() == 0,
              "loop: a one-point path has no segment to close");

        // `loop` adds no point, so auto limits don't move.
        {
            const std::vector<ScatterPlot> no_s;
            const std::vector<BarPlot> no_b;
            const std::vector<HeatmapPlot> no_h;
            const std::vector<ScatterZPlot> no_z;
            const std::vector<LinePlot> o{open}, c{closed};
            const DataBounds ob = auto_scale(AllPlotData{o, no_s, no_b, no_h, no_z}, 0.0);
            const DataBounds cb = auto_scale(AllPlotData{c, no_s, no_b, no_h, no_z}, 0.0);
            check(ob.xmin == cb.xmin && ob.xmax == cb.xmax &&
                  ob.ymin == cb.ymin && ob.ymax == cb.ymax,
                  "loop: closing the path cannot move an auto limit -- the closing "
                  "segment spans points the limits already covered");
        }
    }

    // -------------------------------------------------------------------------
    // Rendered: the seam in the raster, the SVG element, and a plane's strokes
    // -------------------------------------------------------------------------
    void test_line_loop_rendered() {
        std::printf("\n[2D line: the loop, rendered]\n");

        const int W = 400, H = 320; {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            NvgRenderer nvg(ctx.nvg());
            DataRenderer data;
            // One DataRenderer and the same data generation: the stroke cache must
            // key on `loop`, or the second render reuses the first's buffer.
            for (bool loop: {false, true}) {
                FigureSnapshot fs = one_axes(triangle(loop));
                export_figure_png(ctx, nvg, data, fs, loop ? "line_loop.png" : "line_open.png",
                                  W, H, 1);
                export_figure_svg(fs, loop ? "line_loop.svg" : "line_open.svg", W, H);
            }
            // Reference: an open path whose middle vertex is the triangle's first
            // point, with the same two directions (an ordinary bend).
            LinePlot bend = triangle(false);
            bend.x = std::vector<double>{5.0, 2.0, 8.0};
            bend.y = std::vector<double>{8.0, 2.0, 2.0};
            FigureSnapshot fs = one_axes(bend);
            fs.generation = fs.data_generation = 2;
            export_figure_png(ctx, nvg, data, fs, "line_bend.png", W, H, 1);
        }

        const CoordTransform tr = compute_figure_layout(one_axes(triangle(false)), W, H).cells[0].tr;

        // The midpoint of the closing side, (5,8) back to (2,2): only the closing
        // segment draws there.
        const float sx = tr.to_px(3.5), sy = tr.to_py(5.0);

        auto ink_at = [&](const char* png, float x, float y) {
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load(png, &w, &h, &comp, 4);
            check(px != nullptr && w == W && h == H, std::string("rendered: ") + png + " decoded");
            if (!px) return -1;
            int lit = 0;
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx) {
                    const int ix = static_cast<int>(x) + dx, iy = static_cast<int>(y) + dy;
                    if (ix < 0 || iy < 0 || ix >= w || iy >= h) continue;
                    const unsigned char* p = px + (iy * w + ix) * 4;
                    if (p[0] > 120 && p[1] < 100 && p[2] < 100) ++lit;
                }
            stbi_image_free(px);
            return lit;
        };

        const int open_ink = ink_at("line_open.png", sx, sy);
        const int loop_ink = ink_at("line_loop.png", sx, sy);
        std::printf("  seam: %d pixels open, %d looped\n", open_ink, loop_ink);
        check(open_ink == 0 && loop_ink > 0,
              "rendered: loop draws the segment from the last point back to the "
              "first, and an open path draws nothing there");

        // The seam is an ordinary bend: compared pixel-for-pixel with the same
        // bend in an open path (a half-done loop would butt-cap it, leaving a
        // notch).
        {
            const float cx = tr.to_px(2.0), cy = tr.to_py(2.0);
            const float ox = cx - 2.0f, oy = cy + 2.0f; // outside the turn, down-left
            const int seam = ink_at("line_loop.png", ox, oy);
            const int bend = ink_at("line_bend.png", ox, oy);
            std::printf("  corner ink: %d at the seam, %d at the same open bend\n", seam, bend);
            check(bend > 0 && seam == bend,
                  "rendered: the seam is mitered like any other interior point -- "
                  "the corner where the closing segment meets the first carries the "
                  "same ink as the identical bend in an open path (" +
                  std::to_string(seam) + " vs " + std::to_string(bend) + ")");
        }

        // The SVG: a closed element with the same points (not the first repeated).
        {
            const std::string closed = read_file("line_loop.svg"), open = read_file("line_open.svg");
            const std::string poly = element(closed, "polygon"), line = element(open, "polyline");
            check(!poly.empty() && element(closed, "polyline").empty(),
                  "rendered: a looped line is a <polygon>, and there is no <polyline> beside it");
            check(!line.empty() && element(open, "polygon").empty(),
                  "rendered: an open line is still a <polyline>");
            check(point_count(poly) == 3 && point_count(line) == 3,
                  "rendered: the polygon carries the caller's three points, not four -- "
                  "the element closes it, so the seam gets a join rather than two "
                  "butt caps crossing (" + std::to_string(point_count(poly)) + ")");
            check(poly.find("fill=\"none\"") != std::string::npos,
                  "rendered: and it is unfilled, which is the one thing a polygon "
                  "would otherwise change about the picture");
        }

        // A plane's strokes use the same accessors.
        {
            auto plane_segs = [](bool loop) {
                PlaneSnapshot p;
                p.orient = PlaneOrientation::XY;
                p.sheet.lines.push_back(triangle(loop));
                return plane_geometry(p).segs.size();
            };
            const std::size_t open_n = plane_segs(false), loop_n = plane_segs(true);
            std::printf("  plane: %zu segments open, %zu looped\n", open_n, loop_n);
            check(open_n == 2 && loop_n == 3,
                  "rendered: a plane strokes the closing segment too -- Plane2D::line() "
                  "honours `loop` on the same terms as a flat axes");
        }
    }
} // namespace lt
