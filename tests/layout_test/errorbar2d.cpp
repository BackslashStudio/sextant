// 2D error bars: the ErrorBar parameter, offsets and symmetry, the chevron
// cap, and what each output draws. Part of sextant_layout_test; see
// layout_test.h.
#include "layout_test.h"
#include "renderer/error_bar_shape.h"
#include <type_traits>

namespace lt {
    using namespace sextant;

    namespace {
        template<class T>
        concept HistTakesErrorBar = requires(T& a, std::span<const double> d)
        {
            a.hist(d, 10, ErrorBar{});
        };

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

        // A one-point line (so no stroke of its own) carrying a y error bar.
        LinePlot err_point(std::vector<double> cap_lo, std::vector<double> cap_hi,
                           CapStyle style) {
            LinePlot lp;
            lp.x = std::vector<double>{5.0};
            lp.y = std::vector<double>{5.0};
            if (!cap_lo.empty()) lp.err.y_cap_lo = std::move(cap_lo);
            if (!cap_hi.empty()) lp.err.y_cap_hi = std::move(cap_hi);
            lp.opts.errorbar.color = Color{0.0f, 0.6f, 0.0f, 1.0f};
            lp.opts.errorbar.linewidth = 3.0f;
            lp.opts.errorbar.capsize = 24.0f;
            lp.opts.errorbar.capstyle = style;
            return lp;
        }

        struct Seg2 {
            double x0, y0, x1, y1;
        };

        std::vector<Seg2> segments(double cx, double cy, double lo, double hi, bool vertical,
                                   float capsize, CapStyle style,
                                   double unit_along = 1.0, double unit_across = 1.0) {
            std::vector<Seg2> out;
            whisker_segments(cx, cy, lo, hi, vertical, unit_along, unit_across, capsize, style,
                             [&](double a, double b, double c, double d) { out.push_back({a, b, c, d}); });
            return out;
        }

        bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

        bool has_seg(const std::vector<Seg2>& v, double x0, double y0, double x1, double y1,
                     double tol = 1e-9) {
            for (const Seg2& s: v)
                if ((near(s.x0, x0, tol) && near(s.y0, y0, tol) && near(s.x1, x1, tol) && near(s.y1, y1, tol)) ||
                    (near(s.x0, x1, tol) && near(s.y0, y1, tol) && near(s.x1, x0, tol) && near(s.y1, y0, tol)))
                    return true;
            return false;
        }

        std::string read_file(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        // Every <line> in an SVG stroked in `stroke`.
        std::vector<Seg2> svg_lines(const std::string& svg, const std::string& stroke) {
            std::vector<Seg2> out;
            const std::string want = "stroke=\"" + stroke + "\"";
            for (std::size_t p = svg.find("<line "); p != std::string::npos; p = svg.find("<line ", p + 1)) {
                const std::size_t end = svg.find("/>", p);
                const std::string el = svg.substr(p, end - p);
                if (el.find(want) == std::string::npos) continue;
                Seg2 s{};
                if (std::sscanf(el.c_str(), "<line x1=\"%lf\" y1=\"%lf\" x2=\"%lf\" y2=\"%lf\"",
                                &s.x0, &s.y0, &s.x1, &s.y1) == 4)
                    out.push_back(s);
            }
            return out;
        }
    } // namespace

    // -------------------------------------------------------------------------
    // The public API: the overloads, the length rule, and what does not throw
    // -------------------------------------------------------------------------
    void test_errorbar_api() {
        std::printf("\n[2D error bars: the ErrorBar parameter]\n");

        auto threw = [](auto&& fn) {
            try {
                fn();
                return false;
            } catch (const std::invalid_argument&) { return true; }
        };

        check(!HistTakesErrorBar<Axes>,
              "api: hist() has no ErrorBar overload -- a bin count carries no measured uncertainty");
        check(!ErrorBar{}.any(), "api: an empty ErrorBar sets nothing");

        const std::vector<double> x{1.0, 2.0, 3.0}, y{2.0, 4.0, 3.0}, z{0.1, 0.5, 0.9};
        const std::vector<double> e3{0.5, 0.25, 1.0}, e2{0.5, 0.25};

        auto fig = Figure::create({.width = 300, .height = 240});
        auto ax = fig->axes();

        // Every span, on every kind: one entry per point is fine, any other count throws.
        std::span<const double> ErrorBar::* const fields[] = {
            &ErrorBar::x_cap_lo, &ErrorBar::x_cap_hi, &ErrorBar::x_box_lo, &ErrorBar::x_box_hi,
            &ErrorBar::y_cap_lo, &ErrorBar::y_cap_hi, &ErrorBar::y_box_lo, &ErrorBar::y_box_hi,
        };
        int ok_calls = 0, bad_throws = 0, total = 0;
        for (auto f: fields) {
            ErrorBar good, bad;
            good.*f = e3;
            bad.*f = e2;
            const std::function<void(const ErrorBar &)> kinds[] = {
                [&](const ErrorBar& e) { ax->line(x, y, e); },
                [&](const ErrorBar& e) { ax->line(y, e); },
                [&](const ErrorBar& e) { ax->scatter(x, y, e); },
                [&](const ErrorBar& e) { ax->scatter_z(x, y, z, e); },
                [&](const ErrorBar& e) { ax->bar(x, y, e); },
            };
            for (const auto& k: kinds) {
                ++total;
                if (!threw([&] { k(good); })) ++ok_calls;
                if (threw([&] { k(bad); })) ++bad_throws;
            }
        }
        check(ok_calls == total,
              "api: every span on every kind accepts one entry per point -- x included on line() "
              "and bar(), which threw on an x field before step 16 (" +
              std::to_string(ok_calls) + "/" + std::to_string(total) + ")");
        check(bad_throws == total,
              "api: and every one of them throws on a count that is not one per point (" +
              std::to_string(bad_throws) + "/" + std::to_string(total) + ")");

        const std::vector<double> odd{
            -0.5, std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity()
        };
        check(!threw([&] { ax->scatter(x, y, {.x_cap_lo = odd, .y_box_hi = odd}); }),
              "api: negative and non-finite offsets do not throw -- a magnitude, and a masked end");

        auto fig3 = Figure::create({.width = 300, .height = 240});
        auto ax3 = fig3->add_subplot3d(1, 1, 1);
        auto plane = ax3->plane(PlaneOrientation::XY, 0.0);
        check(!threw([&] { plane->line(x, y, {.y_cap_lo = e3}); }) &&
              threw([&] { plane->bar(x, y, {.x_box_hi = e2}); }),
              "api: Plane2D takes the same ErrorBar on the same terms");

        // Through the public API: an empty ErrorBar equals the plain overload, and
        // real data reaches the writer.
        {
            auto a = Figure::create({.width = 300, .height = 240});
            a->axes()->line(x, y, {.color = Color::Red});
            a->savefig("errorbar_none.svg");
            auto b = Figure::create({.width = 300, .height = 240});
            b->axes()->line(x, y, ErrorBar{}, {.color = Color::Red});
            b->savefig("errorbar_empty.svg");
            check(read_file("errorbar_none.svg") == read_file("errorbar_empty.svg"),
                  "api: an empty ErrorBar writes exactly the file the overload without one does");

            auto c = Figure::create({.width = 300, .height = 240});
            c->axes()->line(x, y, {.y_cap_lo = e3},
                            {
                                .color = Color::Red,
                                .errorbar = {.color = Color{0.0f, 0.6f, 0.0f, 1.0f}}
                            });
            c->savefig("errorbar_public.svg");
            check(svg_lines(read_file("errorbar_public.svg"), "rgb(0,153,0)").size() == 9,
                  "api: three symmetric whiskers through the public API write 9 lines (stem + 2 caps each)");
        }
    }

    // -------------------------------------------------------------------------
    // ErrorBarData: the symmetric rule, magnitudes, masking, and auto limits
    // -------------------------------------------------------------------------
    void test_errorbar_data() {
        std::printf("\n[2D error bars: offsets, symmetry and limits]\n");

        const double nan = std::numeric_limits<double>::quiet_NaN();
        ErrorBarData d;
        d.y_cap_lo = std::vector<double>{0.5, -0.5, nan};
        d.y_box_hi = std::vector<double>{0.2, 0.3, 0.4};
        d.x_cap_lo = std::vector<double>{0.1, 0.1, 0.1};
        d.x_cap_hi = std::vector<double>{0.9, nan, 0.0};

        check(near(d.y_cap(0).lo, 0.5) && near(d.y_cap(0).hi, 0.5),
              "data: lo alone is symmetric");
        check(near(d.y_box(1).lo, 0.3) && near(d.y_box(1).hi, 0.3),
              "data: and so is hi alone");
        check(near(d.x_cap(0).lo, 0.1) && near(d.x_cap(0).hi, 0.9),
              "data: both given is asymmetric, each side its own");
        check(near(d.y_cap(1).lo, 0.5) && near(d.y_cap(1).hi, 0.5),
              "data: a negative offset is its magnitude, not an inverted bar");
        check(d.y_cap(2).lo == 0.0 && d.y_cap(2).hi == 0.0 && !d.y_cap(2).any(),
              "data: a non-finite entry reads as zero -- on both sides when it is the only end given");
        check(near(d.x_cap(1).lo, 0.1) && d.x_cap(1).hi == 0.0,
              "data: and on its own side only when the other end is given");
        check(d.has_y_box() && !d.has_x_box() && d.x_box(0).lo == 0.0,
              "data: an absent pair reads as zero");

        // Auto limits reach every drawn end, and a masked end reaches nothing.
        LinePlot lp;
        lp.x = std::vector<double>{0.0, 10.0};
        lp.y = std::vector<double>{0.0, 1.0};
        lp.err.y_cap_lo = std::vector<double>{2.0, 0.0};
        lp.err.y_cap_hi = std::vector<double>{0.0, nan};
        lp.err.x_box_hi = std::vector<double>{0.0, 3.0};
        const std::vector<LinePlot> ls{lp};
        const std::vector<ScatterPlot> no_s;
        const std::vector<BarPlot> no_b;
        const std::vector<HeatmapPlot> no_h;
        const std::vector<ScatterZPlot> no_z;
        const DataBounds b = auto_scale(AllPlotData{ls, no_s, no_b, no_h, no_z}, 0.0);
        check(b.ymin == -2.0 && b.ymax == 1.0 && b.xmin == 0.0 && b.xmax == 13.0,
              "data: auto limits take each end's own offset -- an asymmetric cap, a one-sided "
              "box and a masked end (got x " + std::to_string(b.xmin) + ".." + std::to_string(b.xmax) +
              ", y " + std::to_string(b.ymin) + ".." + std::to_string(b.ymax) + ")");
    }

    // -------------------------------------------------------------------------
    // whisker_segments(): the one definition of a whisker and its caps
    // -------------------------------------------------------------------------
    void test_errorbar_whisker_shape() {
        std::printf("\n[2D error bars: the whisker and its caps]\n");

        // Screen frame: y grows downward, so the "hi" end is the smaller y.
        const double cx = 100.0, cy = 200.0; {
            const auto s = segments(cx, cy, 240.0, 150.0, true, 10.0f, CapStyle::Flat);
            check(s.size() == 3 && has_seg(s, cx, 240.0, cx, 150.0) &&
                  has_seg(s, 95.0, 240.0, 105.0, 240.0) && has_seg(s, 95.0, 150.0, 105.0, 150.0),
                  "shape: flat -- one stem end to end, and a capsize-long crossbar at each end");
        } {
            const auto s = segments(cx, cy, cy, 150.0, true, 10.0f, CapStyle::Flat);
            check(s.size() == 2 && has_seg(s, cx, cy, cx, 150.0) && has_seg(s, 95.0, 150.0, 105.0, 150.0),
                  "shape: a zero side draws neither stem nor cap there");
            check(segments(cx, cy, cy, cy, true, 10.0f, CapStyle::Arrow).empty(),
                  "shape: and zero on both sides draws nothing at all");
            check(segments(cx, cy, 240.0, 150.0, true, 0.0f, CapStyle::Arrow).size() == 1,
                  "shape: capsize 0 is the stem alone, in either style");
        } {
            const double L = kArrowLengthRatio * 10.0;
            const auto s = segments(cx, cy, 240.0, 150.0, true, 10.0f, CapStyle::Arrow);
            check(s.size() == 5 &&
                  has_seg(s, 95.0, 150.0 + L, cx, 150.0, 1e-9) && has_seg(s, 105.0, 150.0 + L, cx, 150.0, 1e-9) &&
                  has_seg(s, 95.0, 240.0 - L, cx, 240.0, 1e-9) && has_seg(s, 105.0, 240.0 - L, cx, 240.0, 1e-9),
                  "shape: arrow -- an open chevron, tip on the end, capsize wide and 0.87 x capsize long, "
                  "pointing away from the point on both sides");
        } {
            // A stem of 4 px under a head 8.66 px long: the head scales down whole.
            const auto s = segments(cx, cy, cy, cy - 4.0, true, 10.0f, CapStyle::Arrow);
            const double k = 4.0 / (kArrowLengthRatio * 10.0);
            check(s.size() == 3 && has_seg(s, cx - 5.0 * k, cy, cx, cy - 4.0, 1e-9) &&
                  has_seg(s, cx + 5.0 * k, cy, cx, cy - 4.0, 1e-9),
                  "shape: a head longer than its stem shrinks whole, its base landing on the point");
        } {
            // A horizontal whisker in a plane frame whose axes have different scales.
            const auto s = segments(0.0, 0.0, -1.0, 2.0, false, 10.0f, CapStyle::Flat, 0.01, 0.5);
            check(s.size() == 3 && has_seg(s, 2.0, -2.5, 2.0, 2.5) && has_seg(s, -1.0, -2.5, -1.0, 2.5),
                  "shape: a horizontal whisker's caps run along y, measured in the across axis' units");
            const auto a = segments(0.0, 0.0, 0.0, 2.0, false, 10.0f, CapStyle::Arrow, 0.01, 0.5);
            const double L = kArrowLengthRatio * 10.0 * 0.01;
            check(a.size() == 3 && has_seg(a, 2.0 - L, -2.5, 2.0, 0.0, 1e-12),
                  "shape: and an arrow's length in the along axis' units");
        }
    }

    // -------------------------------------------------------------------------
    // What each output draws
    // -------------------------------------------------------------------------
    void test_errorbar_rendered() {
        std::printf("\n[2D error bars: rendered]\n");

        const int W = 400, H = 320;
        const double nan = std::numeric_limits<double>::quiet_NaN();

        struct Case {
            const char* name;
            LinePlot lp;
        };
        std::vector<Case> cases;
        cases.push_back({"eb_flat", err_point({2.0}, {}, CapStyle::Flat)});
        cases.push_back({"eb_arrow", err_point({2.0}, {}, CapStyle::Arrow)});
        cases.push_back({"eb_onesided", err_point({0.0}, {2.0}, CapStyle::Arrow)});
        cases.push_back({"eb_masked", err_point({2.0}, {nan}, CapStyle::Flat)}); {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            NvgRenderer nvg(ctx.nvg());
            DataRenderer data;
            // One DataRenderer for all four, so each needs its own generation.
            unsigned long long gen = 0;
            for (const Case& c: cases) {
                FigureSnapshot fs = one_axes(c.lp);
                fs.generation = fs.data_generation = ++gen;
                export_figure_png(ctx, nvg, data, fs, std::string(c.name) + ".png", W, H, 1);
                export_figure_svg(fs, std::string(c.name) + ".svg", W, H);
            }
        }

        const CoordTransform tr = compute_figure_layout(one_axes(cases[0].lp), W, H).cells[0].tr;
        const float cx = tr.to_px(5.0), cy = tr.to_py(5.0);
        const float top = tr.to_py(7.0), bottom = tr.to_py(3.0);
        const float half = 12.0f, len = static_cast<float>(kArrowLengthRatio * 24.0);

        auto load = [&](const char* name, std::function<void(std::function < bool(float, float) >)> body) {
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load((std::string(name) + ".png").c_str(), &w, &h, &comp, 4);
            check(px != nullptr && w == W && h == H, std::string("rendered: ") + name + ".png decoded");
            if (!px) return;
            body([&](float x, float y) {
                const int ix = static_cast<int>(x), iy = static_cast<int>(y);
                if (ix < 0 || iy < 0 || ix >= w || iy >= h) return false;
                const unsigned char* p = px + (iy * w + ix) * 4;
                return p[1] > 100 && p[0] < 90 && p[2] < 90;
            });
            stbi_image_free(px);
        };

        load("eb_flat", [&](auto green) {
            check(green(cx, (cy + top) * 0.5f) && green(cx, (cy + bottom) * 0.5f),
                  "rendered: flat -- the stem runs both ways from the point");
            check(green(cx - 0.8f * half, top) && green(cx + 0.8f * half, bottom),
                  "rendered: flat -- a crossbar at each end");
        });
        load("eb_arrow", [&](auto green) {
            check(green(cx - 0.5f * half, top + 0.5f * len) && green(cx + 0.5f * half, bottom - 0.5f * len),
                  "rendered: arrow -- ink along each chevron arm, opening back toward the point");
            check(!green(cx - 0.8f * half, top) && !green(cx + 0.8f * half, bottom),
                  "rendered: arrow -- and none where a flat crossbar would be");
        });
        load("eb_onesided", [&](auto green) {
            check(green(cx, (cy + top) * 0.5f) && !green(cx, (cy + bottom) * 0.5f) &&
                  !green(cx - 0.5f * half, bottom - 0.5f * len),
                  "rendered: a zero lo draws a one-sided bar -- no stem and no head below the point");
        });
        load("eb_masked", [&](auto green) {
            check(green(cx, (cy + bottom) * 0.5f) && !green(cx, (cy + top) * 0.5f),
                  "rendered: a NaN hi masks that side only");
        });

        // The SVG draws the same segments, at the same pixels.
        {
            const auto s = svg_lines(read_file("eb_arrow.svg"), "rgb(0,153,0)");
            const double tol = 0.01;
            check(s.size() == 5 && has_seg(s, cx, bottom, cx, top, tol) &&
                  has_seg(s, cx - half, top + len, cx, top, tol) &&
                  has_seg(s, cx + half, bottom - len, cx, bottom, tol),
                  "rendered: the SVG writes the chevron as lines at the raster's positions (" +
                  std::to_string(s.size()) + " lines)");
            check(svg_lines(read_file("eb_flat.svg"), "rgb(0,153,0)").size() == 3 &&
                  svg_lines(read_file("eb_onesided.svg"), "rgb(0,153,0)").size() == 3 &&
                  svg_lines(read_file("eb_masked.svg"), "rgb(0,153,0)").size() == 2,
                  "rendered: and flat, one-sided and masked bars write 3, 3 and 2");
        }

        // A box: asymmetric in y, the boxwidth fallback in x.
        {
            LinePlot lp = err_point({}, {}, CapStyle::Flat);
            lp.err.y_box_lo = std::vector<double>{1.0};
            lp.err.y_box_hi = std::vector<double>{3.0};
            lp.opts.errorbar.boxwidth = 20.0f;
            lp.opts.errorbar.linewidth = 1.0f;
            FigureSnapshot fs = one_axes(lp);
            export_figure_svg(fs, "eb_box.svg", W, H);
            const std::string svg = read_file("eb_box.svg");
            std::ostringstream want;
            want << "<rect x=\"" << cx - 10.0f << "\" y=\"" << tr.to_py(8.0)
                    << "\" width=\"" << 20.0f << "\" height=\"" << (tr.to_py(4.0) - tr.to_py(8.0)) << "\"";
            check(svg.find(want.str()) != std::string::npos &&
                  svg_lines(svg, "rgb(0,153,0)").empty(),
                  "rendered: box data alone draws the box -- y from p - box_lo to p + box_hi, "
                  "boxwidth across -- and no whisker");
        }

        // A plane draws the same shapes through its own units.
        {
            auto plane_segs = [](CapStyle style) {
                PlaneSnapshot p;
                p.orient = PlaneOrientation::XY;
                LinePlot lp;
                lp.x = std::vector<double>{2.0, 8.0};
                lp.y = std::vector<double>{5.0, 5.0};
                lp.err.y_cap_lo = std::vector<double>{2.0, 2.0};
                lp.opts.errorbar.capsize = 20.0f;
                lp.opts.errorbar.capstyle = style;
                p.sheet.lines.push_back(std::move(lp));
                return plane_geometry(p).segs;
            };
            const auto flat = plane_segs(CapStyle::Flat), arrow = plane_segs(CapStyle::Arrow);
            const Vec3 tip = plane_point(PlaneOrientation::XY, 2.0, 7.0, 0.0);
            int ends_at_tip = 0;
            for (const auto& s: arrow)
                if (length(s.b - tip) < 1e-9 || length(s.a - tip) < 1e-9) ++ends_at_tip;
            check(flat.size() == 7 && arrow.size() == 11,
                  "rendered: a plane gets the same segments -- 1 line + 2 x (stem + 2 caps) flat, "
                  "2 x (stem + 4 arms) arrow (" + std::to_string(flat.size()) + ", " +
                  std::to_string(arrow.size()) + ")");
            check(ends_at_tip == 3,
                  "rendered: and a chevron's two arms and the stem meet on the plane at the end");
        }
    }

    // -------------------------------------------------------------------------
    // Hover text and the Data panel's row edits
    // -------------------------------------------------------------------------
    void test_errorbar_hint_and_rows() {
        std::printf("\n[2D error bars: hover text and row edits]\n");

        RenderSnapshot snap;
        snap.xmin = 0.0;
        snap.xmax = 10.0;
        snap.ymin = 0.0;
        snap.ymax = 10.0;
        snap.xlim_auto = snap.ylim_auto = false;
        LinePlot lp;
        lp.x = std::vector<double>{2.0, 6.0};
        lp.y = std::vector<double>{2.5, 7.0};
        lp.err.y_box_lo = std::vector<double>{0.3, 0.0};
        lp.err.y_cap_lo = std::vector<double>{0.5, 0.2};
        lp.err.y_cap_hi = std::vector<double>{0.6, 0.2};
        lp.err.x_cap_hi = std::vector<double>{0.0, 1.5};
        snap.lines.push_back(lp);

        const CoordTransform tr{0.0, 10.0, 0.0, 10.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
        const auto h0 = find_hint(snap, tr, tr.to_px(2.0), tr.to_py(2.5));
        const auto h1 = find_hint(snap, tr, tr.to_px(6.0), tr.to_py(7.0));
        check(h0 && h0->text == "x=2, y=2.5 box \xC2\xB1" "0.3 cap +0.6/-0.5",
              "hint: offsets as given -- '±' when symmetric, '+hi/-lo' when not, box before cap (got '" +
              (h0 ? h0->text : std::string("none")) + "')");
        check(h1 && h1->text == "x=6 cap \xC2\xB1" "1.5, y=7 cap \xC2\xB1" "0.2",
              "hint: a part that is zero at this point is left out, and x sits next to x (got '" +
              (h1 ? h1->text : std::string("none")) + "')");

        // A row insert copies the neighbour's error data in every stored column,
        // and leaves the absent columns absent.
        RenderSnapshot t = snap;
        apply_plot_data_ops(t, {PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Line, 0, 2, -1}});
        const ErrorBarData& e = t.lines[0].err;
        check(e.y_cap_lo.size() == 3 && e.y_cap_hi.size() == 3 && e.y_box_lo.size() == 3 &&
              e.x_cap_hi.size() == 3 && e.y_cap_hi[2] == 0.2 && e.x_cap_hi[2] == 1.5 &&
              e.x_cap_lo.empty() && e.y_box_hi.empty() && e.x_box_lo.empty(),
              "rows: an insert copies all eight columns' neighbour where stored, and leaves absent ones empty");
        apply_plot_data_ops(t, {PlotRowEdit{PlotRowEdit::Op::Remove, PlotKind::Line, 0, 0, -1}});
        check(t.lines[0].err.y_cap_hi.size() == 2 && t.lines[0].err.y_cap_hi[0] == 0.2,
              "rows: and a remove takes the error data out with its point");
    }
} // namespace lt
