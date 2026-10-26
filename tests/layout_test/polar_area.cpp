// Polar axes' areas and texts: PolarAxes::fill(), shape_poly(), text() and
// annotate() -- automatic r limits over the areas, the area pipeline (clipped
// triangles, rings and strokes), the sheet's mapping (a polygon as a fill
// keeping id and zorder, texts at unit points), a translucent fill drawn once
// in GL, SVG structure, hover and picks, read-back, set_*_data, the checks and
// cla. Part of sextant_layout_test.
#include "layout_test.h"
#include "figure_impl.h"
#include "polar_geometry.h"
#include "plot_data_view.h"
#include "render_frame.h"

#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;
        constexpr double pi = std::numbers::pi;

        bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

        bool same(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

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

        FigureSnapshot snapshot(Figure& fig) { return detail::FigureAccess::impl(fig).build_figure_snapshot(); }

        std::string read_file(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }

        // The (θ, r) rectangle [t0, t1] x [r0, r1] as two triangles.
        V rect_tris(double t0, double t1, double r0, double r1) {
            return {t0, r0, t1, r0, t1, r1, t0, r0, t1, r1, t0, r1};
        }

        double tris_area(const std::vector<float>& t) {
            double a = 0.0;
            for (std::size_t i = 0; i + 6 <= t.size(); i += 6)
                a += 0.5 * std::abs((t[i + 2] - t[i]) * (t[i + 5] - t[i + 1]) - (t[i + 4] - t[i]) * (t[i + 3] - t[i + 1]));
            return a;
        }

        double ring_area(const PolarArea::Path& p) {
            double a = 0.0;
            const std::size_t n = p.x.size();
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = (i + 1) % n;
                a += p.x[i] * p.y[j] - p.x[j] * p.y[i];
            }
            return 0.5 * std::abs(a);
        }

        void auto_limits() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->fill(V{0.0, 1.0, 2.0}, V{1.0, 2.3, 1.5});
            check(near(pa->rlim().lo, 0) && near(pa->rlim().hi, 2.5), "rlim: a fill's r counts (r2 = 0 too)");
            pa->set_fill_visible(0, false);
            pa->fill(V{1.0, 3.6}, V{0.0, 1.0}, V{1.0, 2.0}, {.orientation = Orientation::Vertical});
            check(near(pa->rlim().hi, 4), "rlim: a Vertical fill's r is along it");
            pa->set_fill_visible(1, false);
            pa->shape_poly(V{0.0, 1.0, 2.0}, V{-1.0, 3.2, 1.0});
            check(near(pa->rlim().lo, -1) && pa->rlim().hi >= 3.2 && pa->rlim().hi <= 4,
                  "rlim: a polygon's vertices count");
            const Range before = pa->rlim();
            pa->text("far", 0.0, 50.0);
            check(near(pa->rlim().hi, before.hi), "rlim: texts never count");
        }

        void area_pipeline() {
            PolarMap m;
            std::vector<float> out;
            polar_area_tris(m, rect_tris(0.0, 2 * pi, 0.5, 1.0), out);
            check(near(tris_area(out), 0.75 * pi, 1e-3), "area: a full annulus is π(r2² − r1²)");
            check(out.size() / 6 >= 360, "area: cut into strips of at most 1°");

            out.clear();
            m.tmax = pi / 2;
            polar_area_tris(m, rect_tris(-1.0, 3.0, 0.5, 1.0), out);
            check(near(tris_area(out), 0.75 * pi / 4, 1e-3), "area: cut to the sector's raw θ");

            out.clear();
            m.tmax = 2 * pi;
            polar_area_tris(m, rect_tris(0.0, pi / 2, 0.5, 1.5), out);
            check(near(tris_area(out), 0.75 * pi / 4, 1e-3), "area: cut at rmax");
            out.clear();
            m.rmin = 0.25;
            polar_area_tris(m, rect_tris(0.0, pi / 2, 0.0, 1.0), out);
            // Unit radius (r − 0.25) / 0.75 over r in [0.25, 1]: the quarter disc.
            check(near(tris_area(out), pi / 4, 1e-3), "area: cut at rmin");
            m.rmin = 0.0;

            out.clear();
            polar_area_tris(m, rect_tris(0.0, 1.0, 2.0, 3.0), out);
            check(out.empty(), "area: nothing outside the frame");

            const PolarArea::Path ring = polar_area_ring(m, V{0.0, pi / 2, pi / 2, 0.0}, V{0.5, 0.5, 1.0, 1.0});
            check(ring.closed && near(ring_area(ring), 0.75 * pi / 4, 1e-3) && ring.x.size() >= 180,
                  "ring: the band's outline, chorded, encloses its area");
            const PolarArea::Path cut = polar_area_ring(m, V{0.0, pi / 2, pi / 2, 0.0}, V{0.5, 0.5, 1.5, 1.5});
            check(near(ring_area(cut), 0.75 * pi / 4, 1e-3), "ring: clipped at rmax as the triangles");

            const PolarArea::Path whole = polar_area_stroke(m, V{0.0, pi / 2, pi / 2, 0.0}, V{0.5, 0.5, 0.9, 0.9});
            bool broken = false;
            const PolarArea::Path rim = polar_area_stroke(m, V{0.0, pi / 2, pi / 2, 0.0}, V{0.5, 0.5, 1.5, 1.5});
            for (double x: rim.x) broken |= std::isnan(x);
            check(whole.closed && !rim.closed && broken, "stroke: closed when uncut, broken where the rim cuts it");
        }

        void sheet_mapping() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->set_rlim(0, 2);
            pa->fill(V{0.0, 1.0, 2.0}, V{1.0, 1.5, 1.0}, 0.0, {.color = Color::Red, .zorder = 2.0});
            pa->shape_poly(V{3.0, 4.0, 3.5}, V{0.5, 0.5, 1.5}, {.color = Color::Blue, .zorder = 1.0});
            pa->text("a", 0.0, 1.0);
            pa->text("out", 0.0, 3.0);
            pa->text("kept", 0.0, 3.0, {.clip_to_frame = true});
            pa->text("frac", Pos::fraction(0.5), Pos::fraction(0.9));
            pa->annotate(pi / 2, 1.0, "arrow", Pos::fraction(0.1), Pos::fraction(0.1));
            const FigureSnapshot fs = snapshot(*fig);
            const RenderSnapshot& s = *fs.axes[0].snap2d();
            const PolarMap m = polar_map(*fs.axes[0].snappolar());
            const RenderSnapshot sheet = polar_sheet(s, m);
            check(sheet.polys.empty() && sheet.fills.size() == 2, "sheet: the polygon becomes a fill");
            check(sheet.fills.size() == 2 && sheet.fills[0].polar_area && sheet.fills[1].polar_area
                      && sheet.fills[1].id == s.polys[0].id && near(sheet.fills[1].opts.zorder, 1.0)
                      && same(*sheet.fills[1].opts.color, Color::Blue) && same(*sheet.fills[0].opts.color, Color::Red),
                  "sheet: each area carries id, zorder and colour");
            check(s.fills[0].polar_area == nullptr, "sheet: the snapshot's own fill untouched");
            const std::vector<ObjectRef> order = draw_order(sheet);
            check(!order.empty() && order.front().kind == PlotKind::Fill && order.front().index == 1,
                  "sheet: draw order still by zorder (the polygon under)");

            const std::vector<TextPlot> ts = polar_texts(s, m);
            std::vector<std::string> names;
            for (const TextPlot& t: ts) names.push_back(t.content.text);
            check(names == std::vector<std::string>{"a", "kept", "frac", "arrow"},
                  "texts: one outside the r limits dropped, clip_to_frame kept");
            check(ts.size() == 4 && near(ts[0].content.x.v, 0.5) && near(ts[0].content.y.v, 0.0),
                  "texts: a Data text at its unit point");
            check(ts.size() == 4 && ts[2].content.x.space == Coords::Fraction && near(ts[2].content.x.v, 0.5),
                  "texts: a Fraction text unchanged");
            check(ts.size() == 4 && near(ts[3].content.px, 0.0, 1e-12) && near(ts[3].content.py, 0.5),
                  "texts: an arrow's point at its unit point");

            pa->set_thetalim(0, 90);
            const RenderSnapshotPolar s2 = *snapshot(*fig).axes[0].snappolar();
            bool arrow = false;
            for (const TextPlot& t: polar_texts(s2.objects, polar_map(s2))) arrow |= t.content.text == "arrow";
            check(arrow, "texts: an arrow to a point on the sector's edge kept");
            pa->set_thetalim(0, 45);
            const RenderSnapshotPolar s3 = *snapshot(*fig).axes[0].snappolar();
            arrow = false;
            for (const TextPlot& t: polar_texts(s3.objects, polar_map(s3))) arrow |= t.content.text == "arrow";
            check(!arrow, "texts: an arrow to a point off the sector dropped");
        }

        void gl_translucent() {
            constexpr int W = 400, H = 400;
            auto fig = Figure::create();
            fig->resize(W, H);
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->set_rlim(0, 1);
            pa->grid(false);
            V th, r;
            for (int i = 0; i < 6; ++i) {
                th.push_back(i * pi / 3);
                r.push_back(0.8);
            }
            th.push_back(2 * pi);
            r.push_back(0.8);
            pa->fill(th, r, 0.0, {.color = Color::Red, .alpha = 0.5f});
            const FigureSnapshot fs = snapshot(*fig);
            const FigureLayout fl = compute_figure_layout(fs, W, H);
            const PolarPlan& pl = *fl.cells[0].polar;
            const RgbaImage img = fig->render_rgba({}, W, H);
            const float k = img.width / static_cast<float>(W);
            const auto px = [&](float x, float y) {
                const int ix = static_cast<int>(x * k), iy = static_cast<int>(y * k);
                return &img.pixels[(static_cast<std::size_t>(iy) * img.width + ix) * 4];
            };
            // Off the strips' seams and the fan's centre.
            const std::uint8_t* in = px(pl.cx + 0.37f * pl.radius, pl.cy - 0.11f * pl.radius);
            check(in[0] > 230 && in[1] > 110 && in[1] < 150, "gl: a translucent fill's pixel blended once");
            const std::uint8_t* out = px(pl.cx + 0.95f * pl.radius, pl.cy - 0.02f * pl.radius);
            check(out[1] > 230, "gl: outside the fill's r, not filled");
        }

        void svg_structure() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->fill(V{0.0, 2.0, 4.0, 2 * pi}, V{1.0, 2.0, 1.5, 1.0}, 0.0,
                     {.color = Color::Red, .linewidth = 2.0f});
            pa->shape_poly(V{0.5, 1.5, 1.0}, V{0.5, 0.5, 1.5}, {.color = Color::Blue});
            pa->text("label here", 1.0, 1.0);
            fig->savefig_svg("polar_area.svg");
            fig->savefig_png("polar_area.png");
            const std::string svg = read_file("polar_area.svg");
            const std::size_t clip = svg.find("clip-path=\"url(#plotArea0)\"");
            const std::size_t g_end = svg.find("</g>", clip);
            const std::size_t fill = svg.find("<path fill=", clip);
            const std::size_t stroke = svg.find("<polyline fill=\"none\"", clip);
            check(clip != std::string::npos && fill < g_end && stroke < g_end,
                  "svg: the areas' paths and the fill's edge inside the polar clip");
            check(count(svg.substr(clip, g_end - clip), "<path fill=") == 2, "svg: one path per area");
            const std::size_t text = svg.find("label here");
            check(text != std::string::npos && text > g_end, "svg: the text after the plots");
        }

        void hover_and_picks() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            pa->set_rlim(0, 4);
            pa->fill(V{-0.5, 0.5}, V{1.0, 1.0}, V{2.0, 2.0}, {.name = "band"});
            pa->shape_poly(V{pi - 0.3, pi + 0.3, pi}, V{1.0, 1.0, 3.0}, {.name = "tri"});
            const FigureSnapshot fs = snapshot(*fig);
            const FigureLayout fl = compute_figure_layout(fs, 500, 500);
            const std::vector<AxesLayout> layout = axes_layouts(fl);
            const RenderSnapshotPolar& s = *fs.axes[0].snappolar();
            const PolarMap m = polar_map(s);
            const CoordTransform& tr = layout[0].tr;
            const auto at = [&](double t, double r, float& x, float& y) {
                x = tr.to_px(m.unit_x(t, r));
                y = tr.to_py(m.unit_y(t, r));
            };
            float x, y;
            at(-0.2, 1.5, x, y);
            auto hit = find_pick(s, tr, x, y);
            std::string text = hit ? format_pick(s, *hit) : "";
            check(hit && hit->kind == PickKind::Fill && hit->object == 0 && near(hit->data_x, -0.2, 0.02),
                  "pick: a fill across θ 0 (θ taken a turn back)");
            check(text.find("theta=") == 0 && text.find("\nband") != std::string::npos,
                  "hover: a polar fill (" + text + ")");
            at(0.2, 0.5, x, y);
            check(!find_pick(s, tr, x, y), "pick: inside the band's hole, nothing");
            at(pi, 1.5, x, y);
            hit = find_pick(s, tr, x, y);
            text = hit ? format_pick(s, *hit) : "";
            check(hit && hit->kind == PickKind::Poly && text.find("\ntri") != std::string::npos,
                  "pick: inside the polygon (" + text + ")");
            at(pi + 0.28, 2.8, x, y);
            check(!find_pick(s, tr, x, y), "pick: beside the polygon's tip, nothing");
        }

        void read_back() {
            auto fig = Figure::create();
            auto pa = fig->add_subplot_polar(1, 1, 1);
            check(throws_invalid([&] { pa->shape_poly(V{0.0, 1.0, 2.0}, V{1.0, 1.0, 1.0},
                                                      {.space = Coords::Fraction}); }),
                  "checks: a non-Data polygon throws");
            check(throws_invalid([&] { pa->text("x", 1.0, Pos::fraction(0.5)); }), "checks: a mixed-space text throws");
            check(throws_invalid([&] { pa->annotate(0.0, 1.0, "x", Pos::fraction(0.5), 1.0); }),
                  "checks: a mixed-space annotation throws");
            check(pa->text_count() == 0 && pa->shape_poly_count() == 0, "checks: nothing added on a throw");

            pa->fill(V{0.0, 1.0}, V{1.0, 2.0}, V{0.5, 0.5});
            pa->shape_poly(V{0.0, 1.0, 2.0}, V{1.0, 2.0, 1.0});
            pa->text("t", 0.5, 1.5);
            check(pa->fill_count() == 1 && pa->fill_data(0).x == V({0.0, 1.0}) && pa->fill_data(0).y2 == V({0.5, 0.5}),
                  "read-back: fill_data is θ, r1, r2");
            check(pa->shape_poly_data(0).y == V({1.0, 2.0, 1.0}), "read-back: shape_poly_data is θ, r");
            const TextData td = pa->text_data(0);
            check(td.text == "t" && near(td.x.v, 0.5) && near(td.y.v, 1.5), "read-back: text_data is θ, r");
            pa->set_fill_data(0, V{0.0, 2.0, 3.0}, V{1.0, 1.0, 1.0}, V{2.0, 2.0, 2.0});
            check(pa->fill_data(0).x.size() == 3, "read-back: set_fill_data");
            pa->set_shape_poly_data(0, V{0.0, 1.0, 2.0, 3.0}, V{1.0, 1.0, 1.0, 1.0});
            check(pa->shape_poly_data(0).x.size() == 4, "read-back: set_shape_poly_data");
            pa->set_text_data(0, "u", 1.0, 2.0);
            check(pa->text_data(0).text == "u" && near(pa->text_data(0).y.v, 2.0), "read-back: set_text_data");
            check(throws_invalid([&] { pa->set_text_data(0, "v", Pos::fraction(0.1), 1.0); }),
                  "checks: set_text_data with mixed spaces throws");
            pa->set_fill_visible(0, false);
            pa->set_shape_poly_visible(0, false);
            pa->set_text_visible(0, false);
            check(!pa->fill_visible(0) && !pa->shape_poly_visible(0) && !pa->text_visible(0), "read-back: visible");

            const std::vector<PlotDataTable> t = collect_plot_data_tables(*snapshot(*fig).axes[0].snappolar());
            bool fill_named = false, poly_named = false, flagged = !t.empty();
            for (const PlotDataTable& d: t) {
                flagged &= d.polar;
                if (d.kind == PlotKind::Fill)
                    fill_named = d.columns[0].name == std::string("theta") && d.columns[1].name == std::string("r1");
                if (d.kind == PlotKind::Poly) poly_named = d.columns[0].name == std::string("theta");
            }
            check(fill_named && poly_named && flagged, "data panel: polar columns read theta and r");

            pa->cla();
            check(pa->fill_count() == 0 && pa->shape_poly_count() == 0 && pa->text_count() == 0,
                  "cla: clears areas and texts");
        }
    } // namespace

    void test_polar_area() {
        auto_limits();
        area_pipeline();
        sheet_mapping();
        gl_translucent();
        svg_structure();
        hover_and_picks();
        read_back();
    }
} // namespace lt
