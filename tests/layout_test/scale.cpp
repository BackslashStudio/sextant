// Axis scales on a 2D Axes: the AxisScale map, the log and
// symlog locators, minor ticks, the tick format, limits in scale space, pan
// and zoom, the refusals, and a log/symlog render whose GL pixels sit where the
// layout's transform (and so the SVG) puts them. Part of sextant_layout_test.
#include "layout_test.h"
#include "axis_placement.h"
#include "ticker.h"

namespace lt {
    using namespace sextant;

    namespace {
        bool near(double a, double b, double rel = 1e-12) {
            return std::fabs(a - b) <= rel * std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
        }

        std::vector<double> values(const std::vector<Tick>& t) {
            std::vector<double> v;
            for (const Tick& k: t) v.push_back(k.value);
            return v;
        }

        std::vector<std::string> labels(const std::vector<Tick>& t) {
            std::vector<std::string> v;
            for (const Tick& k: t) v.push_back(k.label);
            return v;
        }

        bool same(const std::vector<double>& a, const std::vector<double>& b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (!near(a[i], b[i], 1e-9)) return false;
            return true;
        }

        AxisScale log10s() { return make_axis_scale(Scale::Log, {}); }

        AxisScale symlog(double linthresh = 1.0, double linscale = 1.0) {
            return make_axis_scale(Scale::Symlog, {.linthresh = linthresh, .linscale = linscale});
        }

        // One 2D axes with fixed limits and scales, built directly.
        FigureSnapshot one_axes(RenderSnapshot s) {
            FigureSnapshot fs;
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            fa.snap = std::move(s);
            fs.axes.push_back(std::move(fa));
            return fs;
        }

        RenderSnapshot limits(double x0, double x1, double y0, double y1, AxisScale xs, AxisScale ys) {
            RenderSnapshot s;
            s.xmin = x0;
            s.xmax = x1;
            s.ymin = y0;
            s.ymax = y1;
            s.xlim_auto = s.ylim_auto = false;
            s.xscale = xs;
            s.yscale = ys;
            return s;
        }

        std::size_t count(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
            return n;
        }

        template <class F>
        bool throws(F f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        void test_map() {
            const AxisScale lin{}, lg = log10s();
            check(lin.fwd(-3.5) == -3.5 && lin.inv(7.25) == 7.25, "map: linear is the identity");
            check(lg.fwd(1000.0) == 3.0 && lg.fwd(0.01) == -2.0, "map: log10 of a power of ten is exact");
            check(lg.fwd(0.0) == -300.0 && lg.fwd(-5.0) == -300.0, "map: log clips v <= 0 to 1e-300");
            check(std::isnan(lg.fwd_strict(0.0)) && std::isnan(lg.fwd_strict(-1.0)) && lg.fwd_strict(10.0) == 1.0,
                  "map: fwd_strict has no value for v <= 0 on log");
            check(std::isnan(lg.fwd(std::nan(""))), "map: NaN stays NaN (a gap)");
            check(!lg.valid(0.0) && lg.valid(1e-300) && lin.valid(-1.0) && symlog().valid(-1.0),
                  "map: only log refuses v <= 0");

            bool round = true;
            const AxisScale b2 = make_axis_scale(Scale::Log, {.base = 2.0});
            const AxisScale be = make_axis_scale(Scale::Log, {.base = std::exp(1.0)});
            for (double v: {1e-8, 0.3, 1.0, 2.0, 7.5, 1024.0, 3e9}) {
                round = round && near(lg.inv(lg.fwd(v)), v, 1e-12) && near(b2.inv(b2.fwd(v)), v, 1e-12)
                        && near(be.inv(be.fwd(v)), v, 1e-12);
            }
            check(round, "map: log inv(fwd(v)) == v at bases 10, 2 and e");
            check(b2.fwd(64.0) == 6.0, "map: log2 of a power of two is exact");

            for (const AxisScale& s: {symlog(), symlog(2.0), symlog(1.0, 0.5), symlog(0.1, 2.0)}) {
                bool ok = true, mono = true;
                double prev = -1e300;
                for (double v: {-1e5, -30.0, -3.0, -1.0, -0.5, -0.01, 0.0, 0.01, 0.5, 1.0, 3.0, 30.0, 1e5}) {
                    ok = ok && near(s.inv(s.fwd(v)), v, 1e-12);
                    mono = mono && s.fwd(v) > prev;
                    prev = s.fwd(v);
                }
                check(ok && mono, "map: symlog round-trips and increases (linthresh "
                                  + std::to_string(s.linthresh) + ", linscale " + std::to_string(s.linscale) + ")");
                // Continuous at the threshold, odd, and linear inside.
                const double t = s.linthresh;
                check(near(s.fwd(t * (1.0 + 1e-12)), s.fwd(t), 1e-9) && s.fwd(-t) == -s.fwd(t)
                          && near(s.fwd(t * 0.5), s.fwd(t) * 0.5, 1e-12),
                      "map: symlog is continuous at linthresh, odd, and linear inside");
            }
            // matplotlib: linscale_adj = linscale / (1 - 1/base); fwd(linthresh) = linthresh * adj.
            check(near(symlog().fwd(1.0), 1.0 / 0.9) && near(symlog().fwd(10.0), 1.0 / 0.9 + 1.0),
                  "map: symlog matches matplotlib's transform (one decade past linthresh adds 1)");

            // CoordTransform: limits in data, pixels through the scale.
            const CoordTransform tr{1.0, 1000.0, 0.0, 1.0, 0.0f, 0.0f, 300.0f, 100.0f, 300.0f, 100.0f, lg, lin};
            check(near_px(tr.to_px(10.0), 100.0f) && near_px(tr.to_px(100.0), 200.0f) && near_px(tr.to_px(1.0), 0.0f),
                  "transform: decades are evenly spaced on a log x");
            check(near(tr.to_data_x(150.0f), std::pow(10.0, 1.5), 1e-6), "transform: to_data_x inverts through the scale");
            check(tr.to_px(0.0) < -1e4f && std::isfinite(tr.to_px(0.0)), "transform: 0 on log lands far left, finite");

            // Zoom keeps the data value under the cursor; pan moves whole decades.
            const AxisLimits z = zoom_limits(tr, 100.0f, 50.0f, 0.5f);
            // Pixel arithmetic is float, so ~1e-7 relative.
            check(near(z.xmin, std::pow(10.0, 0.5), 1e-6) && near(z.xmax, 100.0, 1e-6),
                  "zoom: halves the decades around the cursor on log");
            CoordTransform zt = tr;
            zt.xmin = z.xmin;
            zt.xmax = z.xmax;
            check(near_px(zt.to_px(10.0), 100.0f, 1e-3f), "zoom: the data value under the cursor stays put");
            const AxisLimits p = pan_limits(tr, 100.0f, 0.0f);
            check(near(p.xmin, 0.1, 1e-6) && near(p.xmax, 100.0, 1e-6), "pan: a 100 px drag moves one decade on log");
            check(near(p.ymin, 0.0) && near(p.ymax, 1.0), "pan: a linear y is untouched by an x drag");

            check(near(place_axis(AxisPosition::Mid, std::nullopt, 1.0, 100.0, lg).pos, 10.0, 1e-12),
                  "placement: Mid on a log axis is the frame's middle (10 on 1..100)");
            check(place_axis(AxisPosition::Mid, std::nullopt, 0.0, 10.0).pos == 5.0,
                  "placement: Mid on linear unchanged");
        }

        void test_locators() {
            const AxisScale lg = log10s();
            auto t = scale_ticks(1.0, 1e4, lg, "", true);
            check(same(values(t), {1.0, 10.0, 100.0, 1e3, 1e4}), "log: a tick per decade on 1..1e4");
            check(labels(t) == std::vector<std::string>{"$10^{0}$", "$10^{1}$", "$10^{2}$", "$10^{3}$", "$10^{4}$"},
                  "log: labels are powers in math");
            check(labels(scale_ticks(1.0, 1e4, lg, "", false))
                      == std::vector<std::string>{"1", "10", "100", "1000", "10000"},
                  "log: with math off, %g");
            check(labels(scale_ticks(0.001, 0.1, lg, "", true))
                      == std::vector<std::string>{"$10^{-3}$", "$10^{-2}$", "$10^{-1}$"},
                  "log: negative exponents");

            auto wide = scale_ticks(1e-10, 1e10, lg, "", true);
            check(same(values(wide), {1e-9, 1e-6, 1e-3, 1.0, 1e3, 1e6, 1e9}),
                  "log: 21 decades thin to every 3rd, aligned so 10^0 stays");
            check(scale_ticks(1.0, 1e8, lg, "", true).size() == 9, "log: 9 decades are all kept");

            check(same(values(scale_ticks(2.0, 30.0, lg, "", true)), {2.0, 5.0, 10.0, 20.0}),
                  "log: under two decades, 1-2-5 per decade");
            check(labels(scale_ticks(2.0, 30.0, lg, "", true)) == std::vector<std::string>{"2", "5", "10", "20"},
                  "log: 1-2-5 ticks are labelled %g");
            check(same(values(scale_ticks(1.1, 1.5, lg, "", true)), values(generate_ticks(1.1, 1.5))),
                  "log: with no 1-2-5 tick in view, linear ticks");
            const AxisScale b2 = make_axis_scale(Scale::Log, {.base = 2.0});
            auto t2 = scale_ticks(1.0, 64.0, b2, "", true);
            check(same(values(t2), {1, 2, 4, 8, 16, 32, 64}) && t2[3].label == "$2^{3}$", "log: base 2 powers");
            const AxisScale be = make_axis_scale(Scale::Log, {.base = std::exp(1.0)});
            check(scale_ticks(1.0, 100.0, be, "", true)[1].label == "$e^{1}$", "log: base e is written e");

            auto rev = scale_ticks(1e4, 1.0, lg, "", true);
            check(same(values(rev), {1e4, 1e3, 100.0, 10.0, 1.0}), "log: a reversed axis gets the ticks reversed");
            check(scale_ticks(0.0, 10.0, lg, "", true).empty(), "log: limits reaching 0 make no ticks");

            check(labels(scale_ticks(1.0, 100.0, lg, "%.1f", true))
                      == std::vector<std::string>{"1.0", "10.0", "100.0"},
                  "format: replaces the power labels");
            check(labels(scale_ticks(0.0, 1.0, AxisScale{}, "%.0f%%", true)).front() == "0%",
                  "format: on linear, with %% and text");
            check(values(scale_ticks(0.0, 1.0, AxisScale{}, "%.2f", true)) == values(generate_ticks(0.0, 1.0)),
                  "format: on linear, the positions are generate_ticks()'s");

            auto sy = scale_ticks(-100.0, 100.0, symlog(), "", true);
            check(same(values(sy), {-100, -10, -1, 0, 1, 10, 100}), "symlog: 0 and +-decades from linthresh");
            check(labels(sy) == std::vector<std::string>{"$-10^{2}$", "$-10^{1}$", "$-10^{0}$", "0",
                                                         "$10^{0}$", "$10^{1}$", "$10^{2}$"},
                  "symlog: power labels, signed, and 0");
            check(same(values(scale_ticks(-0.5, 0.5, symlog(), "", true)), values(generate_ticks(-0.5, 0.5))),
                  "symlog: inside the linear part, linear ticks");
            check(same(values(scale_ticks(0.0, 1000.0, symlog(10.0), "", true)), {0, 10, 100, 1000}),
                  "symlog: decades start at linthresh");
            check(scale_ticks(-1e12, 1e12, symlog(), "", true).size() <= 13,
                  "symlog: many decades are thinned");

            // Minors.
            auto m = scale_minor_ticks(1.0, 100.0, lg, false);
            check(m.size() == 16 && near(m.front(), 2.0) && near(m.back(), 90.0),
                  "minor: log 2..9 and 20..90 per decade");
            auto mw = scale_minor_ticks(1e-10, 1e10, lg, false);
            check(mw.size() == 14, "minor: thinned log majors leave the skipped decades as minors");
            check(scale_minor_ticks(1.0, 64.0, b2, false).empty(), "minor: base 2 has no in-decade minors");
            auto m125 = scale_minor_ticks(2.0, 30.0, lg, false);
            check(std::find_if(m125.begin(), m125.end(), [](double v) { return near(v, 3.0); }) != m125.end()
                      && std::find_if(m125.begin(), m125.end(), [](double v) { return near(v, 5.0); }) == m125.end(),
                  "minor: under 1-2-5 majors, the other multiples");
            check(scale_minor_ticks(0.0, 10.0, AxisScale{}, false).size() == 40,
                  "minor: linear step 1 splits in 5");
            check(scale_minor_ticks(0.0, 20.0, AxisScale{}, false).size() == 24,
                  "minor: linear step 2.5 splits in 4");
            check(scale_minor_ticks(0.0, 10.0, AxisScale{}, true).empty(),
                  "minor: linear with set_xticks() positions has none");
            check(scale_minor_ticks(1.0, 100.0, lg, true).size() == 16,
                  "minor: log keeps its decade minors with set_xticks() positions");
            auto ms = scale_minor_ticks(-100.0, 100.0, symlog(), false);
            check(ms.size() == 32 && std::is_sorted(ms.begin(), ms.end())
                      && std::none_of(ms.begin(), ms.end(), [](double v) { return std::fabs(v) < 1.0; }),
                  "minor: symlog per decade outside the linear part, none inside");

            // The format check.
            bool ok = true;
            for (const char* f: {"%g", "%.2f", "%.0f%%", "$%.1f$ ms", "%+08.3e", "%G", "x=%5.f", "%%%E%%"})
                ok = ok && tick_format_ok(f);
            check(ok, "format: printf conversions for one double are accepted");
            bool bad = false;
            for (const char* f: {"", "%d", "%s", "%n", "%*.2f", "%.2f %.2f", "%", "abc", "%lf", "%%", "%.*f", "%5"})
                bad = bad || tick_format_ok(f);
            check(!bad, "format: anything but one %f/%e/%g conversion is refused");
            check(format_tick("%.0f%%", 42.0) == "42%", "format: formats the value");

            // set_xticks() positions without a label take the format; given labels stay.
            const double pos[] = {1.0, 2.0};
            std::vector<Tick> ov = make_tick_override(pos, {"one"});
            ov = relabel_ticks(ov, "%.1f");
            check(ov[0].label == "one" && ov[1].label == "2.0", "format: relabels only set_xticks() positions it labelled");
        }

        void test_limits_and_api() {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const std::vector<double> x{1, 2, 3, 4}, y{0.0, 1.0, 10.0, 100.0};
            ax->line(x, y);
            ax->set_yscale(Scale::Log);
            check(ax->yscale() == Scale::Log && ax->xscale() == Scale::Linear, "api: xscale()/yscale() read back");
            const Range yl = ax->ylim();
            check(near(yl.lo, std::pow(10.0, -0.1), 1e-9) && near(yl.hi, std::pow(10.0, 2.1), 1e-9),
                  "limits: log auto limits pad 5% of the decades and skip 0");

            auto bars = Figure::create()->add_subplot(1, 1, 1);
            bars->bar(std::vector<double>{1, 2, 3}, std::vector<double>{1, 10, 100});
            bars->set_yscale(Scale::Log);
            check(near(bars->ylim().lo, std::pow(10.0, -0.1), 1e-9), "limits: a bar's baseline counts for nothing on log");

            auto empty = Figure::create()->add_subplot(1, 1, 1);
            const Range lin = empty->ylim();
            empty->set_yscale(Scale::Log);
            check(near(empty->ylim().lo, std::pow(10.0, lin.lo), 1e-12) && near(empty->ylim().hi, std::pow(10.0, lin.hi), 1e-12),
                  "limits: an empty log axis reads linear's empty view as decades (1..10, padded)");

            auto pin = Figure::create()->add_subplot(1, 1, 1);
            pin->line(x, std::vector<double>{1, 10, 100, 1000});
            pin->set_yscale(Scale::Log);
            const Range before = pin->ylim();
            AxesStyle st;
            st.origin_y = 0.0;
            pin->set_axes_style(st);
            check(pin->ylim().lo == before.lo, "limits: an origin pin at 0 widens nothing on log");

            auto sx = Figure::create()->add_subplot(1, 1, 1);
            sx->line(std::vector<double>{-100.0, 100.0}, std::vector<double>{0.0, 1.0});
            sx->set_xscale(Scale::Symlog);
            const AxisScale s = symlog();
            const double pad = (s.fwd(100.0) - s.fwd(-100.0)) * kAutoScalePad;
            check(near(sx->xlim().hi, s.inv(s.fwd(100.0) + pad), 1e-9), "limits: symlog pads in scale space");

            // Set limits.
            check(throws([&] { ax->set_ylim(0.0, 10.0); }) && throws([&] { ax->set_ylim(-1.0, 10.0); }),
                  "api: set_ylim() with an end <= 0 throws on log");
            ax->set_ylim(2.0, 50.0);
            check(ax->ylim().lo == 2.0 && ax->ylim().hi == 50.0, "api: positive limits on log are kept");
            auto keep = Figure::create()->add_subplot(1, 1, 1);
            keep->line(x, std::vector<double>{1, 10, 100, 1000});
            keep->set_ylim(-1.0, 5.0);
            keep->set_yscale(Scale::Log);
            check(keep->ylim().lo > 0.0 && near(keep->ylim().hi, std::pow(10.0, 3.15), 1e-9),
                  "api: switching to log drops set limits reaching 0 back to automatic");
            keep->set_yscale(Scale::Linear);
            keep->set_ylim(-1.0, 5.0);
            check(keep->ylim().lo == -1.0, "api: back to linear, any limits again");

            // Options.
            auto o = Figure::create()->add_subplot(1, 1, 1);
            check(throws([&] { o->set_xscale(Scale::Log, {.base = 1.0}); })
                      && throws([&] { o->set_xscale(Scale::Log, {.base = std::nan("")}); })
                      && throws([&] { o->set_xscale(Scale::Symlog, {.linthresh = 0.0}); })
                      && throws([&] { o->set_xscale(Scale::Symlog, {.linscale = -1.0}); }),
                  "api: options a scale reads are checked");
            o->set_xscale(Scale::Linear, {.base = 0.0});
            o->set_xscale(Scale::Log, {.linthresh = -5.0});
            check(o->xscale() == Scale::Log, "api: options a scale does not read are not");

            // Heatmaps take any scale (heatmap_scale.cpp draws them).
            const std::vector<double> cells{1, 2, 3, 4};
            auto h = Figure::create()->add_subplot(1, 1, 1);
            h->set_xscale(Scale::Log);
            h->imshow(cells, 2, 2);
            check(h->heatmap_count() == 1, "api: heatmap() on a log axes is added");
            auto h2 = Figure::create()->add_subplot(1, 1, 1);
            h2->imshow(cells, 2, 2);
            h2->set_yscale(Scale::Symlog);
            check(h2->yscale() == Scale::Symlog, "api: an axes holding a heatmap takes a scale");

            // Formats.
            check(throws([&] { o->set_xtick_format("%d"); }) && throws([&] { o->set_ytick_format("%s"); }),
                  "api: a bad tick format throws");
            o->set_xtick_format("%.1f");
            o->set_xtick_format("");

            // cla() resets.
            ax->cla();
            check(ax->yscale() == Scale::Linear, "api: cla() resets the scales");
        }

        void test_layout() {
            const int W = 400, H = 300;
            RenderSnapshot s = limits(1.0, 1e4, 0.0, 10.0, log10s(), AxisScale{});
            auto fs = one_axes(s);
            const CellLayout c = compute_figure_layout(fs, W, H).cells[0];
            check(same(values(c.xticks), {1, 10, 100, 1e3, 1e4}) && c.xticks[2].label == "$10^{2}$",
                  "layout: a log x gets decade ticks with power labels");
            check(c.xminor.size() == 32 && c.yminor.empty(), "layout: minors on log by default, not on linear");
            fs.mathtext = false;
            check(compute_figure_layout(fs, W, H).cells[0].xticks[2].label == "100",
                  "layout: with math off the labels are %g");

            RenderSnapshot f = s;
            f.xtick_format = "%.0e";
            f.axes_style.minor_xticks = false;
            f.axes_style.minor_yticks = true;
            const CellLayout cf = compute_figure_layout(one_axes(f), W, H).cells[0];
            check(cf.xticks[1].label == "1e+01" && cf.xminor.empty() && !cf.yminor.empty(),
                  "layout: the format and AxesStyle::minor_* are honoured");

            RenderSnapshot o = s;
            const double pos[] = {3.0, 30.0};
            o.xticks_override = make_tick_override(pos, {});
            o.xtick_format = "%.1f";
            check(labels(compute_figure_layout(one_axes(o), W, H).cells[0].xticks)
                      == std::vector<std::string>{"3.0", "30.0"},
                  "layout: set_xticks() positions take the format");

            // The SVG draws minors, and a minor grid when asked.
            const std::string with = render_figure_svg(one_axes(s), W, H);
            RenderSnapshot nm = s;
            nm.axes_style.minor_xticks = false;
            const std::string without = render_figure_svg(one_axes(nm), W, H);
            check(count(with, "<line") == count(without, "<line") + 32, "svg: one mark per minor tick");
            RenderSnapshot g = s;
            g.grid_enabled = true;
            g.grid_opts.minor = true;
            g.grid_opts.minor_color = {0.0f, 0.0f, 1.0f, 1.0f};
            const std::string grid = render_figure_svg(one_axes(g), W, H);
            check(count(grid, "stroke=\"rgb(0,0,255)\"") == 1 && count(grid, "<line") == count(with, "<line") + 32 + 5 + 11,
                  "svg: a minor grid line per minor tick, in its own colour, under the major grid");
        }

        // GL pixels where the transform puts them: scatter (instanced, data
        // space), bars (filled in data space) and a line (segments), on log
        // and symlog axes.
        void test_render() {
            const int W = 400, H = 300;
            auto red = [](const unsigned char* p) { return p[0] > 180 && p[1] < 80 && p[2] < 80; };
            auto green = [](const unsigned char* p) { return p[1] > 100 && p[0] < 90 && p[2] < 90; };
            auto blue = [](const unsigned char* p) { return p[2] > 150 && p[0] < 90 && p[1] < 120; };

            RenderSnapshot sc = limits(0.5, 2000.0, 0.0, 10.0, log10s(), AxisScale{});
            {
                ScatterPlot sp;
                sp.x = CowVec<double>(std::vector<double>{1.0, 10.0, 100.0, 1000.0});
                sp.y = CowVec<double>(std::vector<double>{5.0, 5.0, 5.0, 5.0});
                sp.opts.color = Color{1.0f, 0.0f, 0.0f, 1.0f};
                sp.opts.marker = MarkerStyle::Square;
                sp.opts.size = 8.0f;
                sc.scatters.push_back(std::move(sp));
            }
            RenderSnapshot br = limits(0.0, 3.0, 1.0, 1e4, AxisScale{}, log10s());
            {
                BarPlot bp;
                bp.centers = CowVec<double>(std::vector<double>{1.0, 2.0});
                bp.heights = CowVec<double>(std::vector<double>{10.0, 1000.0});
                bp.bar_width = 0.5;
                bp.opts.color = Color{0.0f, 0.6f, 0.0f, 1.0f};
                bp.opts.linewidth = 0.0f;
                br.bars.push_back(std::move(bp));
            }
            RenderSnapshot ln = limits(-200.0, 200.0, -200.0, 200.0, symlog(), symlog());
            {
                LinePlot lp;
                lp.x = CowVec<double>(std::vector<double>{-100.0, -1.0, 0.0, 1.0, 100.0});
                lp.y = lp.x;
                lp.opts.color = Color{0.0f, 0.0f, 1.0f, 1.0f};
                lp.opts.linewidth = 3.0f;
                ln.lines.push_back(std::move(lp));
            }
            const std::vector<std::pair<const char*, RenderSnapshot*>> figs{
                {"scale_scatter", &sc}, {"scale_bars", &br}, {"scale_line", &ln}};
            {
                GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
                RenderDevice dev;
                DataRenderer data;
                unsigned long long gen = 0;
                for (const auto& [name, snap]: figs) {
                    FigureSnapshot fs = one_axes(*snap);
                    fs.generation = fs.data_generation = ++gen;
                    export_figure_png(dev, data, fs, std::string(name) + ".png", W, H, 1);
                    export_figure_svg(fs, std::string(name) + ".svg", W, H);
                }
            }

            auto probe = [&](const char* name, const RenderSnapshot& snap, auto body) {
                const CoordTransform tr = compute_figure_layout(one_axes(snap), W, H).cells[0].tr;
                int w = 0, h = 0, comp = 0;
                unsigned char* px = stbi_load((std::string(name) + ".png").c_str(), &w, &h, &comp, 4);
                check(px != nullptr && w == W && h == H, std::string("render: ") + name + ".png decoded");
                if (!px) return;
                auto at = [&](float x, float y) -> const unsigned char* {
                    static const unsigned char none[4] = {255, 255, 255, 255};
                    const int ix = static_cast<int>(x), iy = static_cast<int>(y);
                    if (ix < 0 || iy < 0 || ix >= w || iy >= h) return none;
                    return px + (iy * w + ix) * 4;
                };
                body(tr, at);
                stbi_image_free(px);
            };

            probe("scale_scatter", sc, [&](const CoordTransform& tr, auto at) {
                bool hit = true, gap = true;
                for (double v: {1.0, 10.0, 100.0, 1000.0}) hit = hit && red(at(tr.to_px(v), tr.to_py(5.0)));
                for (double v: {3.1623, 31.623, 316.23}) gap = gap && !red(at(tr.to_px(v), tr.to_py(5.0)));
                check(hit, "render: log x scatter markers sit at the decades the transform places");
                check(gap, "render: and nothing between them");
                check(near_px(tr.to_px(10.0) - tr.to_px(1.0), tr.to_px(100.0) - tr.to_px(10.0), 0.01f),
                      "render: the decades are evenly spaced");
            });
            probe("scale_bars", br, [&](const CoordTransform& tr, auto at) {
                const float x1 = tr.to_px(1.0), x2 = tr.to_px(2.0);
                check(green(at(x1, tr.to_py(10.0) + 3.0f)) && !green(at(x1, tr.to_py(10.0) - 3.0f)),
                      "render: a log y bar's top is at its height");
                check(green(at(x2, tr.to_py(1000.0) + 3.0f)) && !green(at(x2, tr.to_py(1000.0) - 3.0f)),
                      "render: and the taller one's too");
                check(green(at(x1, tr.to_py(1.0) - 2.0f)), "render: a bar from 0 stands on the frame's bottom");
            });
            probe("scale_line", ln, [&](const CoordTransform& tr, auto at) {
                bool on = true;
                for (double v: {-50.0, -10.0, -0.5, 0.5, 10.0, 50.0}) on = on && blue(at(tr.to_px(v), tr.to_py(v)));
                check(on, "render: a symlog line runs through the points the transform places");
                check(!blue(at(tr.to_px(10.0), tr.to_py(-10.0))), "render: and not elsewhere");
                const std::string svg = render_figure_svg(one_axes(ln), W, H);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%g,%g ", tr.to_px(100.0), tr.to_py(100.0));
                check(svg.find(buf) != std::string::npos, "render: the SVG polyline uses the same transform");
            });
        }

        // The panel's scale and format edits: applied in the ticks group,
        // also over a heatmap, skipped under a newer setter, limits dropped and
        // restored.
        void test_edits() {
            RenderSnapshot s = limits(-1.0, 5.0, 0.0, 1.0, AxisScale{}, AxisScale{});
            AxesEdit e;
            e.xscale = log10s();
            e.ytick_format = std::string("%.1f");
            AxesEdit inv;
            apply_axes_edit(s, e, &inv);
            check(s.xscale == log10s() && s.ytick_format == "%.1f", "edit: a scale and a format apply");
            check(s.xlim_auto && !s.ylim_auto, "edit: set limits the new scale can't place go automatic");
            check(inv.xscale && inv.xscale->linear() && inv.ytick_format && inv.ytick_format->empty()
                      && inv.xlim_auto && !*inv.xlim_auto && inv.xmin && *inv.xmin == -1.0 && inv.xmax && *inv.xmax == 5.0,
                  "edit: the inverse holds the old scale, format and limits");
            apply_axes_edit(s, inv);
            check(s.xscale.linear() && !s.xlim_auto && s.xmin == -1.0 && s.xmax == 5.0 && s.ytick_format.empty(),
                  "edit: applying the inverse restores them");

            RenderSnapshot ok = limits(1.0, 5.0, 0.0, 1.0, AxisScale{}, AxisScale{});
            apply_axes_edit(ok, e);
            check(!ok.xlim_auto && ok.xmin == 1.0, "edit: limits the scale can place are kept");

            RenderSnapshot h = limits(1.0, 5.0, 0.0, 1.0, AxisScale{}, AxisScale{});
            h.heatmaps.emplace_back();
            apply_axes_edit(h, e);
            check(h.xscale == log10s() && h.ytick_format == "%.1f", "edit: a scale over a heatmap applies");

            RenderSnapshot newer = limits(1.0, 5.0, 0.0, 1.0, AxisScale{}, AxisScale{});
            newer.style_stamps.xticks = 10;
            AxesEdit stale = e;
            stale.style_seen.xticks = 5;
            apply_axes_edit(newer, stale);
            check(newer.xscale.linear(), "edit: a set_xscale() since the panel drew wins");

            AxesEdit a, b;
            a.xscale = log10s();
            b.xscale = symlog();
            b.xtick_format = std::string("%g");
            merge_style_edits(a, b);
            check(a.xscale == symlog() && a.xtick_format == "%g", "edit: the latest scale and format merge in");
            check(!style_edits_empty(b) && style_edits_empty(AxesEdit{}), "edit: a scale edit is not empty");

            FigureEdits f;
            f.per_axes.push_back({1, b});
            check(!is_navigation_only(f), "edit: a scale edit re-measures the layout (not navigation)");
        }
    } // namespace

    void test_scale() {
        std::printf("\n[axis scales: log, symlog, minor ticks, tick format]\n");
        test_map();
        test_locators();
        test_limits_and_api();
        test_layout();
        test_render();
        test_edits();
    }
} // namespace lt
