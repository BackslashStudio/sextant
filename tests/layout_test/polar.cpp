// Polar axes: PolarAxes -- the map and its round trip, curve densifying and
// clipping, automatic r limits, the θ and r labels, the sector frame, SVG
// structure, GL against SVG, the calls' checks, read-back, the journal and
// Home, hover and picks, and polar cells beside 2D ones. Part of
// sextant_layout_test.
#include "layout_test.h"
#include "figure_impl.h"
#include "polar_geometry.h"
#include "plot_events.h"
#include "render_frame.h"
#include "widgets/nav_history.h"
#include "widgets/panel_state.h"

#include <cmath>
#include <numbers>

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;
        constexpr double pi = std::numbers::pi;

        bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

        template <class F>
        bool throws_invalid(F&& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        std::size_t count(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }

        V spiral_theta(int n, double turns) {
            V t(n);
            for (int i = 0; i < n; ++i) t[i] = 2.0 * pi * turns * i / (n - 1);
            return t;
        }
        V spiral_r(int n) {
            V r(n);
            for (int i = 0; i < n; ++i) r[i] = 0.1 + 0.9 * i / (n - 1);
            return r;
        }

        void map_round_trip() {
            PolarMap m;
            m.offset = 90 * kPolarDeg;
            m.dir = -1.0;
            m.rmin = 1.0;
            m.rmax = 3.0;
            check(near(m.unit_x(0, 3), 0.0, 1e-12) && near(m.unit_y(0, 3), 1.0),
                  "map: θ 0 at north with offset 90");
            check(near(m.unit_x(pi / 2, 3), 1.0) && near(m.unit_y(pi / 2, 3), 0.0, 1e-12),
                  "map: clockwise puts 90° east");
            check(near(m.unit_x(0, 2), 0.0, 1e-12) && near(m.unit_y(0, 2), 0.5), "map: r maps from rmin");
            double t, r;
            m.from_unit(m.unit_x(1.0, 2.5), m.unit_y(1.0, 2.5), t, r);
            check(near(t, 1.0) && near(r, 2.5), "map: from_unit inverts");
            check(m.inside(0.3, 0.3) && !m.inside(0.9, 0.9), "map: inside the disc");

            PolarMap s;
            s.tmin = 0;
            s.tmax = pi / 2;
            check(s.inside(0.5, 0.5) && !s.inside(-0.5, 0.5) && !s.inside(0.5, -0.1), "map: inside a sector");
            const PolarBox b = polar_box(s);
            check(near(b.xmin, 0) && near(b.xmax, 1) && near(b.ymin, 0) && near(b.ymax, 1),
                  "box: a quarter's box is the quadrant");
            PolarMap h;
            h.tmin = 0;
            h.tmax = pi;
            const PolarBox hb = polar_box(h);
            check(near(hb.xmin, -1) && near(hb.xmax, 1) && near(hb.ymin, 0) && near(hb.ymax, 1),
                  "box: the upper half");
        }

        void densify_and_clip() {
            PolarMap m;
            std::vector<double> ox, oy;
            const V th{0.0, pi}, r{1.0, 1.0};
            polar_polyline(m, th.data(), r.data(), 2, DrawStyle::Default, false, ox, oy);
            check(ox.size() == 181, "densify: half a turn in 1° chords (" + std::to_string(ox.size()) + ")");
            bool on_circle = true;
            for (std::size_t i = 0; i < ox.size(); ++i) on_circle &= near(std::hypot(ox[i], oy[i]), 1.0, 1e-12);
            check(on_circle, "densify: a constant r stays on its ring");

            // A radial segment is not split.
            const V th2{0.5, 0.5}, r2{0.0, 1.0};
            polar_polyline(m, th2.data(), r2.data(), 2, DrawStyle::Default, false, ox, oy);
            check(ox.size() == 2, "densify: a radial segment stays one chord");

            // Clipped at rmax and rmin.
            m.rmin = 0.5;
            m.rmax = 1.0;
            const V th3{0.0, 0.0}, r3{0.0, 2.0};
            polar_polyline(m, th3.data(), r3.data(), 2, DrawStyle::Default, false, ox, oy);
            check(ox.size() == 2 && near(ox[0], 0.0) && near(ox[1], 1.0), "clip: r cut to [rmin, rmax]");

            // NaN breaks.
            m = PolarMap{};
            const V th4{0.0, 0.1, NAN, 0.3, 0.4}, r4{0.5, 0.5, 0.5, 0.5, 0.5};
            polar_polyline(m, th4.data(), r4.data(), 5, DrawStyle::Default, false, ox, oy);
            std::size_t nans = 0;
            for (double v: ox) nans += std::isnan(v);
            check(nans == 1, "densify: a NaN point breaks the curve once");

            // Sector clip.
            m.tmin = 0.0;
            m.tmax = pi / 2;
            const V th5{-0.5, 2.0}, r5{0.5, 0.5};
            polar_polyline(m, th5.data(), r5.data(), 2, DrawStyle::Default, false, ox, oy);
            double tt, rr;
            m.from_unit(ox.front(), oy.front(), tt, rr);
            const bool lo_ok = near(tt, 0.0, 1e-9);
            m.from_unit(ox.back(), oy.back(), tt, rr);
            check(lo_ok && near(tt, pi / 2, 1e-9), "clip: θ cut to the sector");
        }

        void auto_limits_and_labels() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            check(near(pa->rlim().lo, 0) && near(pa->rlim().hi, 1), "rlim: no data is [0, 1]");
            pa->plot(V{0, 1, 2}, V{0.3, 1.7, 2.6});
            check(near(pa->rlim().lo, 0) && near(pa->rlim().hi, 3), "rlim: from 0 up to the next tick");
            pa->set_rmax(5);
            check(near(pa->rlim().lo, 0) && near(pa->rlim().hi, 5), "rlim: set_rmax");
            pa->set_rmin(1);
            check(near(pa->rlim().lo, 1) && near(pa->rlim().hi, 5), "rlim: set_rmin");

            const RenderSnapshot s = *detail::FigureAccess::impl(*fig).build_figure_snapshot().axes[0].snap2d();
            const auto tt = polar_theta_ticks(s);
            check(tt.size() == 8 && tt[0].label == "0\xC2\xB0" && tt[2].label == "90\xC2\xB0",
                  "θ labels: every 45°, 0 to 315");
            const auto rt = polar_r_ticks(s, pa->rlim());
            bool none_at_rmin = true;
            for (const Tick& t: rt) none_at_rmin &= !near(t.value, 1.0);
            check(!rt.empty() && none_at_rmin && near(rt.back().value, 5.0), "r labels: none at rmin, up to rmax");
        }

        void render_files() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 2, 1);
            pa->plot(spiral_theta(200, 2), spiral_r(200), {.name = "spiral"});
            pa->set_title("spiral");
            pa->legend();
            auto pb = fig->add_subplot_polar(1, 2, 2);
            V th, r;
            for (int i = 0; i <= 180; ++i) {
                th.push_back(i * pi / 180);
                r.push_back(1 + std::sin(4 * i * pi / 180));
            }
            pb->plot(th, r, {.linestyle = LineStyle::Dashed});
            pb->set_theta_zero_location(ThetaZero::N);
            pb->set_theta_direction(-1);
            pb->set_thetalim(0, 180);
            pb->set_title("sector");
            fig->savefig_png("polar.png");
            fig->savefig_svg("polar.svg");
        }
        FigureSnapshot snapshot(Figure& fig) { return detail::FigureAccess::impl(fig).build_figure_snapshot(); }

        void checks_and_read_back() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 2, 2);
            const V th{0.0, 1.0, 2.0}, r{1.0, 2.0, 3.0};
            check(throws_invalid([&] { pa->set_theta_direction(0); }), "checks: direction 0 throws");
            check(throws_invalid([&] { pa->set_thetalim(10, 10); }), "checks: an empty θ span throws");
            check(throws_invalid([&] { pa->set_thetalim(0, 400); }), "checks: a θ span over 360 throws");
            check(throws_invalid([&] { pa->set_thetamax(NAN); }), "checks: a NaN θ limit throws");
            check(throws_invalid([&] { pa->set_rlim(1, 1); }), "checks: equal r limits throw");
            check(throws_invalid([&] { pa->set_rlim(0, INFINITY); }), "checks: an infinite r limit throws");
            bool threw = false;
            try {
                fig->add_subplot(1, 2, 2);
            } catch (const std::exception&) {
                threw = true;
            }
            check(threw, "checks: add_subplot on a polar cell throws");
            check(fig->add_subplot_polar(1, 2, 2) == pa, "add_subplot_polar: the same cell returns the same axes");

            pa->plot(th, r, {.name = "a"});
            check(pa->line_count() == 1 && pa->line_data(0).x == th && pa->line_data(0).y == r,
                  "read-back: line_data is θ, r");
            pa->set_line_data(0, V{0.5, 1.5}, V{2.0, 2.5});
            check(pa->line_data(0).x == V({0.5, 1.5}), "read-back: set_line_data");
            pa->plot(V{1.0, 2.0});
            check(pa->line_data(1).x == V({0.0, 1.0}), "read-back: plot(r) puts θ at 0, 1, ...");

            pa->set_theta_zero_location(ThetaZero::N, 10);
            check(near(pa->theta_offset(), 100), "read-back: zero at N plus 10 is offset 100");
            pa->set_theta_offset(-30);
            check(near(pa->theta_offset(), -30), "read-back: set_theta_offset");
            pa->set_theta_direction(-1);
            check(pa->theta_direction() == -1, "read-back: theta_direction");
            pa->set_thetalim(-90, 90);
            check(near(pa->thetalim().lo, -90) && near(pa->thetalim().hi, 90), "read-back: thetalim");
            pa->set_thetamin(-45);
            check(near(pa->thetalim().lo, -45) && near(pa->thetalim().hi, 90), "read-back: set_thetamin");
            pa->set_rlabel_position(60);
            check(near(pa->rlabel_position(), 60), "read-back: rlabel_position");
            pa->set_title("t");
            check(pa->title() == "t", "read-back: title");

            pa->cla();
            check(pa->line_count() == 0 && near(pa->theta_offset(), 0) && pa->theta_direction() == 1
                      && near(pa->thetalim().lo, 0) && near(pa->thetalim().hi, 360)
                      && near(pa->rlabel_position(), 22.5),
                  "cla: back to a new polar axes' defaults");

            // Mixed cells: a 2D axes beside the polar one, both drawn.
            auto ax = fig->add_subplot(1, 2, 1);
            ax->line(V{0, 1}, V{0, 1});
            pa->plot(th, r);
            const FigureSnapshot fs = snapshot(*fig);
            int polar = 0, flat = 0;
            for (const auto& a: fs.axes)
                if (const RenderSnapshot* s = a.snap2d()) (s->polar ? polar : flat)++;
            check(polar == 1 && flat == 1, "mixed: one polar and one 2D snapshot");
            const FigureLayout fl = compute_figure_layout(fs, 800, 400);
            int polar_cells = 0;
            for (const CellLayout& c: fl.cells) polar_cells += c.polar.has_value();
            check(polar_cells == 1, "mixed: the polar cell alone gets a polar plan");
            fig->savefig_png("polar_mixed.png");
        }

        void edits_and_journal() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->plot(V{0, 1}, V{1, 2});
            pa->set_theta_offset(0);   // a setter's stamp, for one to be newer than
            FigureSnapshot fs = snapshot(*fig);
            RenderSnapshot& s = *fs.axes[0].snap2d();
            AxesEdit e;
            PolarState p = *s.polar;
            p.theta_offset = 45;
            e.polar = p;
            e.style_seen.polar = s.style_stamps.polar;
            AxesEdit inv;
            apply_axes_edit(s, e, &inv);
            check(near(s.polar->theta_offset, 45), "edit: a turn applies");
            apply_axes_edit(s, inv);
            check(near(s.polar->theta_offset, 0), "edit: its inverse turns back");

            e.style_seen.polar = s.style_stamps.polar - 1;
            apply_axes_edit(s, e);
            check(near(s.polar->theta_offset, 0), "edit: a turn over a newer setter is skipped");

            FigureEdits f;
            f.per_axes.push_back({0, e});
            check(!is_navigation_only(f), "edit: a turn re-lays the figure (labels move)");

            // A plain 2D axes ignores it.
            RenderSnapshot flat;
            e.style_seen = StyleStamps::any();
            apply_axes_edit(flat, e);
            check(!flat.polar, "edit: a 2D axes ignores a polar edit");
        }

        void hover_pick_events() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->plot(V{0.0, pi / 2, pi}, V{1.0, 2.0, 1.5}, {.name = "p"});
            pa->set_rlim(0, 2);
            const FigureSnapshot fs = snapshot(*fig);
            const FigureLayout fl = compute_figure_layout(fs, 500, 500);
            const std::vector<AxesLayout> layout = axes_layouts(fl);
            const RenderSnapshot& s = *fs.axes[0].snap2d();
            const PolarMap m = polar_map(s);
            const CoordTransform& tr = layout[0].tr;
            const float x = tr.to_px(m.unit_x(pi / 2, 2.0)), y = tr.to_py(m.unit_y(pi / 2, 2.0));
            const auto hit = find_pick(s, tr, x + 2.0f, y + 1.0f);
            check(hit && hit->kind == PickKind::Line && hit->object == 0 && hit->element == 1
                      && std::abs(hit->anchor_x - x) < 0.01f && std::abs(hit->anchor_y - y) < 0.01f,
                  "pick: the point at θ 90°, r 2");
            const std::string text = hit ? format_pick(s, *hit) : "";
            check(text.rfind("theta=90\xC2\xB0, r=2", 0) == 0, "hover: reads theta and r (" + text + ")");
            check(!find_pick(s, tr, tr.to_px(0.0), tr.to_py(0.0)), "pick: nothing at the empty centre");

            const float qx = tr.to_px(m.unit_x(1.0, 0.5)), qy = tr.to_py(m.unit_y(1.0, 0.5));
            const PointLocation loc = locate_point(fs, layout, qx, qy);
            check(loc.has_data && near(loc.x, 1.0, 1e-3) && near(loc.y, 0.5, 1e-3),
                  "events: xdata is θ (radians), ydata r");
            const PointLocation out = locate_point(fs, layout, tr.to_px(0.99), tr.to_py(0.99));
            check(!out.has_data, "events: outside the disc has no data point");
        }

        void navigation_history() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->plot(V{0, 1}, V{1, 2});
            FigureSnapshot fs = snapshot(*fig);
            FigureEditBox box;
            FigureWindowState st;
            FigureContext ctx = panel_ctx(fs, box, st);
            NavHistory& h = st.plot.nav;
            const auto apply = [&] {
                if (auto d = box.load_and_clear()) {
                    for (const auto& [k, e]: d->per_axes)
                        if (RenderSnapshot* s = fs.find2d(k)) apply_axes_edit(*s, e);
                    ++fs.generation;
                }
            };
            const auto offset = [&] { return fs.find2d(fs.axes[0].slot.index)->polar->theta_offset; };
            const int key = fs.axes[0].slot.index;
            nav_track_home(h, fs);
            nav_begin_gesture(h, fs);
            box.update(key, [&](AxesEdit& e) {
                PolarState p = *fs.find2d(key)->polar;
                p.theta_offset = 30;
                e.polar = p;
                e.style_seen.polar = fs.find2d(key)->style_stamps.polar;
            });
            apply();
            check(near(offset(), 30) && !at_home(h, fs), "history: a turn leaves Home");
            check(nav_back(h, ctx), "history: Back after a turn pushes edits");
            apply();
            check(near(offset(), 0) && at_home(h, fs), "history: Back turns the frame back");
            check(nav_forward(h, ctx), "history: Forward pushes edits");
            apply();
            check(near(offset(), 30), "history: Forward turns it again");
            check(nav_home(h, ctx), "history: Home pushes edits");
            apply();
            check(near(offset(), 0), "history: Home turns it home");
        }
    } // namespace

    void test_polar() {
        map_round_trip();
        densify_and_clip();
        auto_limits_and_labels();
        render_files();
        checks_and_read_back();
        edits_and_journal();
        hover_pick_events();
        navigation_history();
    }
} // namespace lt
