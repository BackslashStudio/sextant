// The 2D line's step drawstyle (LineOptions::drawstyle): step_vertices() per
// mode, NaN and loop; the GL stroke against the SVG element's vertices; plane
// strokes; limits unchanged.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "describe.h"
#include "line_steps.h"

namespace lt {
    using namespace sextant;

    namespace {
        using Pts = std::vector<std::pair<double, double>>;

        Pts expand(const std::vector<double>& x, const std::vector<double>& y, DrawStyle ds,
                   bool loop) {
            std::vector<double> ox, oy;
            step_vertices(x.data(), y.data(), x.size(), ds, loop, ox, oy);
            Pts out;
            for (std::size_t i = 0; i < ox.size(); ++i) out.emplace_back(ox[i], oy[i]);
            return out;
        }

        // One 2D axes over fixed 0..10 limits holding `lp`.
        FigureSnapshot one_axes(LinePlot lp, unsigned long long gen = 1) {
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
            fs.generation = fs.data_generation = gen;
            return fs;
        }

        // (2,2) (5,8) (8,4), thick red.
        LinePlot stairs(DrawStyle ds, LineStyle ls = LineStyle::Solid) {
            LinePlot lp;
            lp.x = std::vector<double>{2.0, 5.0, 8.0};
            lp.y = std::vector<double>{2.0, 8.0, 4.0};
            lp.opts.color = Color{0.8f, 0.0f, 0.0f, 1.0f};
            lp.opts.linewidth = 6.0f;
            lp.opts.linestyle = ls;
            lp.opts.drawstyle = ds;
            return lp;
        }

        std::string read_file(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        // The SVG's first <polyline> points, as pixel pairs.
        std::vector<std::pair<float, float>> polyline_points(const std::string& svg) {
            std::vector<std::pair<float, float>> out;
            const std::size_t p = svg.find("<polyline ");
            if (p == std::string::npos) return out;
            const std::size_t q = svg.find("points=\"", p);
            if (q == std::string::npos) return out;
            std::string pts = svg.substr(q + 8, svg.find('"', q + 8) - (q + 8));
            std::replace(pts.begin(), pts.end(), ',', ' ');
            std::istringstream in(pts);
            float a = 0.0f, b = 0.0f;
            while (in >> a >> b) out.emplace_back(a, b);
            return out;
        }

        struct Png {
            int w = 0, h = 0;
            std::vector<unsigned char> px;

            explicit Png(const char* path) {
                int comp = 0;
                unsigned char* p = stbi_load(path, &w, &h, &comp, 4);
                if (p) px.assign(p, p + static_cast<std::size_t>(w) * h * 4);
                stbi_image_free(p);
            }

            bool red(int x, int y) const {
                if (x < 0 || y < 0 || x >= w || y >= h || px.empty()) return false;
                const unsigned char* p = px.data() + (static_cast<std::size_t>(y) * w + x) * 4;
                return p[0] > 120 && p[1] < 100 && p[2] < 100;
            }

            // Red pixels in the 7x7 box around (x, y).
            int ink(float x, float y) const {
                int lit = 0;
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dx = -3; dx <= 3; ++dx)
                        lit += red(static_cast<int>(x) + dx, static_cast<int>(y) + dy);
                return lit;
            }
        };
    } // namespace

    // -------------------------------------------------------------------------
    // step_vertices(): each mode, loop, NaN
    // -------------------------------------------------------------------------
    void test_line_steps() {
        std::printf("\n[2D line: step drawstyle, the vertices]\n");

        const std::vector<double> x{0.0, 2.0, 4.0}, y{0.0, 4.0, 1.0};
        check(expand(x, y, DrawStyle::Default, false) == Pts{{0, 0}, {2, 4}, {4, 1}},
              "steps: Default strokes the points themselves");
        check(expand(x, y, DrawStyle::StepsPre, false) ==
              Pts{{0, 0}, {0, 4}, {2, 4}, {2, 1}, {4, 1}},
              "steps: pre rises at each point's x to the next value, then runs flat");
        check(expand(x, y, DrawStyle::StepsPost, false) ==
              Pts{{0, 0}, {2, 0}, {2, 4}, {4, 4}, {4, 1}},
              "steps: post runs flat at each value to the next x, then rises");
        check(expand(x, y, DrawStyle::StepsMid, false) ==
              Pts{{0, 0}, {1, 0}, {1, 4}, {2, 4}, {3, 4}, {3, 1}, {4, 1}},
              "steps: mid rises halfway between the two x values");

        // The midpoint is taken in data units, so on a log axis it is not the
        // visual middle (matplotlib's choice).
        {
            const auto p = expand({1.0, 100.0}, {0.0, 1.0}, DrawStyle::StepsMid, false);
            check(p.size() == 4 && p[1].first == 50.5 && p[2].first == 50.5,
                  "steps: mid uses the data-space mean of x (50.5 between 1 and 100)");
        }

        // A loop steps the closing pair and ends on its corner; the closing
        // segment runs from there to the first point.
        {
            const auto p = expand(x, y, DrawStyle::StepsPre, true);
            check(p == Pts{{0, 0}, {0, 4}, {2, 4}, {2, 1}, {4, 1}, {4, 0}},
                  "steps: a looped pre path steps the closing pair (4,1) -> (0,0) as "
                  "(4,0), and does not repeat the first point");
            StrokePath sp;
            sp.build(x.data(), y.data(), x.size(), DrawStyle::StepsPre, true);
            check(sp.n == 6 && sp.segment_count() == 6,
                  "steps: and the stroked path has 6 vertices and 6 segments, the "
                  "last one closing");
            StrokePath open;
            open.build(x.data(), y.data(), x.size(), DrawStyle::StepsPost, false);
            check(open.n == 5 && open.segment_count() == 4, "steps: an open path does not close");
        }

        // NaN: a pair with a non-finite end draws none of its steps; its
        // neighbours draw theirs.
        {
            const double nan = std::numeric_limits<double>::quiet_NaN();
            const std::vector<double> nx{0.0, 1.0, 2.0, 3.0}, ny{0.0, nan, 2.0, 3.0};
            for (DrawStyle ds: {DrawStyle::StepsPre, DrawStyle::StepsMid, DrawStyle::StepsPost}) {
                std::vector<double> ox, oy;
                step_vertices(nx.data(), ny.data(), nx.size(), ds, false, ox, oy);
                auto finite = [&](std::size_t i) {
                    return std::isfinite(ox[i]) && std::isfinite(oy[i]);
                };
                int drawn = 0;
                double drawn_from = 0.0;
                for (std::size_t i = 0; i + 1 < ox.size(); ++i)
                    if (finite(i) && finite(i + 1)) {
                        if (drawn++ == 0) drawn_from = ox[i];
                    }
                const int want = ds == DrawStyle::StepsMid ? 3 : 2;
                check(drawn == want && drawn_from == 2.0,
                      std::string("steps: ") + std::string(enum_name(ds)) +
                      " with a NaN at point 1 draws only the (2,2) -> (3,3) step (" +
                      std::to_string(drawn) + " segments)");
            }
        }

        // Fewer than two points: nothing to step.
        {
            const auto p = expand({4.0}, {4.0}, DrawStyle::StepsMid, true);
            StrokePath sp;
            const double one = 4.0;
            sp.build(&one, &one, 1, DrawStyle::StepsMid, true);
            check(p.size() == 1 && sp.segment_count() == 0,
                  "steps: a one-point line has no step and no segment");
        }

        // Limits: every corner lies in the points' box, so the steps cannot move one.
        {
            const std::vector<ScatterPlot> no_s;
            const std::vector<BarPlot> no_b;
            const std::vector<HeatmapPlot> no_h;
            const std::vector<ScatterZPlot> no_z;
            const std::vector<LinePlot> d{stairs(DrawStyle::Default)};
            const DataBounds db = auto_scale(AllPlotData{d, no_s, no_b, no_h, no_z}, 0.0);
            bool same = true;
            for (DrawStyle ds: {DrawStyle::StepsPre, DrawStyle::StepsMid, DrawStyle::StepsPost}) {
                const std::vector<LinePlot> s{stairs(ds)};
                const DataBounds sb = auto_scale(AllPlotData{s, no_s, no_b, no_h, no_z}, 0.0);
                same = same && sb.xmin == db.xmin && sb.xmax == db.xmax &&
                       sb.ymin == db.ymin && sb.ymax == db.ymax;
            }
            check(same, "steps: the drawstyle does not move an auto limit");
        }
    }

    // -------------------------------------------------------------------------
    // Rendered: GL ink along the SVG's vertices, the cache key, dashes, planes
    // -------------------------------------------------------------------------
    void test_line_steps_rendered() {
        std::printf("\n[2D line: step drawstyle, rendered]\n");

        const int W = 400, H = 320;
        const DrawStyle modes[] = {DrawStyle::Default, DrawStyle::StepsPre, DrawStyle::StepsMid,
                                   DrawStyle::StepsPost};
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            // One DataRenderer and the same data generation: the stroke cache must
            // key on the drawstyle, or every render reuses the first's buffer.
            for (DrawStyle ds: modes) {
                const std::string base = "line_steps_" + std::string(enum_name(ds));
                FigureSnapshot fs = one_axes(stairs(ds));
                export_figure_png(dev, data, fs, base + ".png", W, H, 1);
                export_figure_svg(fs, base + ".svg", W, H);
            }
            FigureSnapshot fs = one_axes(stairs(DrawStyle::StepsPost, LineStyle::Dashed), 2);
            export_figure_png(dev, data, fs, "line_steps_dashed.png", W, H, 1);
        }

        const CoordTransform tr = compute_figure_layout(one_axes(stairs(DrawStyle::Default)), W, H)
                                      .cells[0].tr;

        // Each SVG carries the expansion, and the GL raster has ink at the middle
        // of every segment the SVG draws.
        for (DrawStyle ds: modes) {
            const std::string base = "line_steps_" + std::string(enum_name(ds));
            const auto pts = polyline_points(read_file(base + ".svg"));
            const std::size_t want = ds == DrawStyle::Default ? 3 : ds == DrawStyle::StepsMid ? 7 : 5;
            check(pts.size() == want,
                  "steps rendered: " + base + ".svg carries " + std::to_string(want) +
                  " vertices (" + std::to_string(pts.size()) + ")");
            const Png png((base + ".png").c_str());
            check(png.w == W && png.h == H, "steps rendered: " + base + ".png decoded");
            int missing = 0;
            for (std::size_t i = 0; i + 1 < pts.size(); ++i)
                if (png.ink(0.5f * (pts[i].first + pts[i + 1].first),
                            0.5f * (pts[i].second + pts[i + 1].second)) == 0)
                    ++missing;
            check(!pts.empty() && missing == 0,
                  "steps rendered: the GL stroke has ink at the middle of each SVG segment (" +
                  base + ", " + std::to_string(missing) + " missing)");
        }

        // Where only one style draws: the post step's flat run along y = 2, and
        // the straight line's first segment through (3.5, 5).
        {
            const Png straight("line_steps_default.png"), post("line_steps_steps_post.png"),
                      pre("line_steps_steps_pre.png");
            const float rx = tr.to_px(3.5), ry = tr.to_py(2.0);
            const float dx = tr.to_px(3.5), dy = tr.to_py(5.0);
            std::printf("  (3.5,2): straight %d, post %d; (3.5,5): straight %d, post %d\n",
                        straight.ink(rx, ry), post.ink(rx, ry), straight.ink(dx, dy), post.ink(dx, dy));
            check(post.ink(rx, ry) > 0 && straight.ink(rx, ry) == 0 && pre.ink(rx, ry) == 0,
                  "steps rendered: post runs flat at the first value, where neither the "
                  "straight line nor pre draws");
            check(straight.ink(dx, dy) > 0 && post.ink(dx, dy) == 0,
                  "steps rendered: and the straight segment is gone");
        }

        // Dashes run along the expanded path: the flat run (2,2) -> (5,2) of the
        // dashed post line has both dash and gap.
        {
            const Png png("line_steps_dashed.png");
            int on = 0, off = 0;
            const int y = static_cast<int>(tr.to_py(2.0));
            for (int x = static_cast<int>(tr.to_px(2.3)); x <= static_cast<int>(tr.to_px(4.7)); ++x)
                (png.red(x, y) ? on : off) += 1;
            std::printf("  dashed flat run: %d on, %d off\n", on, off);
            check(on > 0 && off > 0, "steps rendered: a dashed step line is dashed along its runs");
        }

        // NaN: SVG has no break in a polyline, so a line is cut into one
        // element per finite run, and no `nan` reaches the file.
        for (DrawStyle ds: modes) {
            const double nan = std::numeric_limits<double>::quiet_NaN();
            LinePlot lp = stairs(ds);
            lp.x = std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0};
            lp.y = std::vector<double>{1.0, 3.0, nan, 4.0, 6.0, 5.0, 2.0};
            const std::string base = "line_steps_nan_" + std::string(enum_name(ds));
            export_figure_svg(one_axes(lp), base + ".svg", W, H);
            const std::string svg = read_file(base + ".svg");
            std::size_t runs = 0;
            for (std::size_t p = svg.find("<polyline "); p != std::string::npos;
                 p = svg.find("<polyline ", p + 1))
                ++runs;
            check(svg.find("nan") == std::string::npos && runs == 2,
                  "steps rendered: a NaN cuts the SVG line into two polylines, none with nan (" +
                  base + ", " + std::to_string(runs) + " polylines)");
        }

        // A plane strokes the same expansion.
        {
            auto plane_segs = [](DrawStyle ds, bool loop) {
                PlaneSnapshot p;
                p.orient = PlaneOrientation::XY;
                LinePlot lp = stairs(ds);
                lp.opts.loop = loop;
                p.sheet.lines.push_back(lp);
                return plane_geometry(p).segs.size();
            };
            const std::size_t d = plane_segs(DrawStyle::Default, false);
            const std::size_t m = plane_segs(DrawStyle::StepsMid, false);
            const std::size_t ml = plane_segs(DrawStyle::StepsMid, true);
            std::printf("  plane: %zu straight, %zu mid, %zu mid looped\n", d, m, ml);
            check(d == 2 && m == 6 && ml == 9,
                  "steps rendered: a plane strokes the steps too, the closing pair included");
        }
    }
} // namespace lt
