// Shapes: Axes::shape_poly()/shape_circle()/shape_wedge() as geometry
// (shape_geometry.h). The triangles' area, the outlines, the data-space ellipse
// and its bend on a log axis, pixel and fraction radii, wedges and annuli, ear
// clipping, the hit test and the autoscale box.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "shape_geometry.h"
#include "hint.h"
#include "reference_lines.h"
#include "figure_impl.h"
#include "plot_data_view.h"
#include <cmath>

namespace lt {
    using namespace sextant;

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        // A 200x100 frame at (50, 50) showing 0..10 both ways: 20 px per unit
        // across, 10 up, so a circle in data units is an ellipse.
        CoordTransform wide_tr() {
            CoordTransform tr{};
            tr.xmin = 0.0; tr.xmax = 10.0; tr.ymin = 0.0; tr.ymax = 10.0;
            tr.px = 50.0f; tr.py = 50.0f; tr.pw = 200.0f; tr.ph = 100.0f;
            return tr;
        }

        double tri_area(const ShapeGeom& g) {
            double a = 0.0;
            for (std::size_t i = 0; i + 2 < g.tris.size(); i += 3)
                a += std::fabs(shape_detail::cross(g.tris[i], g.tris[i + 1], g.tris[i + 2])) * 0.5;
            return a;
        }

        double ring_area(const std::vector<ShapePt>& r) {
            double a = 0.0;
            for (std::size_t i = 0; i < r.size(); ++i) {
                const ShapePt &p = r[i], &q = r[(i + 1) % r.size()];
                a += static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
            }
            return std::fabs(a) * 0.5;
        }

        struct Extent { float x0, x1, y0, y1; };
        Extent extent(const std::vector<ShapePt>& r) {
            Extent e{r[0].x, r[0].x, r[0].y, r[0].y};
            for (const ShapePt& p : r) {
                e.x0 = std::min(e.x0, p.x); e.x1 = std::max(e.x1, p.x);
                e.y0 = std::min(e.y0, p.y); e.y1 = std::max(e.y1, p.y);
            }
            return e;
        }

        bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
        bool within(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::fabs(b); }

        const std::vector<double> kLx{0, 4, 4, 2, 2, 0}, kLy{0, 0, 2, 2, 4, 4};   // an L, area 12
    } // namespace

    void test_shape_geometry() {
        std::printf("\n[shapes: geometry]\n");
        const CoordTransform tr = wide_tr();
        const Pos c5 = Pos(5.0);

        // A circle with a pixel radius is round, and its area is the disc's.
        {
            const ShapeGeom g = circle_geometry(c5, c5, Pos::pixels(40.0), tr);
            check(!g.empty() && g.rings.size() == 1 && within(tri_area(g), kPi * 1600.0, 0.01),
                  "shapes: a circle of 40 px has about pi r^2 of fill");
            const Extent e = extent(g.rings[0]);
            check(near(e.x1 - e.x0, 80.0, 0.5) && near(e.y1 - e.y0, 80.0, 0.5),
                  "shapes: a pixel radius is round on a 2:1 frame");
            check(near((e.x0 + e.x1) * 0.5, 150.0, 0.5) && near((e.y0 + e.y1) * 0.5, 100.0, 0.5),
                  "shapes: its centre is the data point (5, 5)");
            check(within(ring_area(g.rings[0]), tri_area(g), 1e-4), "shapes: the outline bounds the fill");
        }

        // A data radius is an ellipse: 2 units are 40 px across and 20 px up.
        {
            const ShapeGeom g = circle_geometry(c5, c5, Pos(2.0), tr);
            const Extent e = extent(g.rings[0]);
            check(near(e.x1 - e.x0, 80.0, 0.5) && near(e.y1 - e.y0, 40.0, 0.5),
                  "shapes: a data-unit circle is an ellipse where the axes' scales differ");
            check(within(tri_area(g), kPi * 40.0 * 20.0, 0.01), "shapes: its area is pi rx ry");
        }

        // A fraction radius is a fraction of the frame's shorter side, round.
        {
            const ShapeGeom g = circle_geometry(Pos::fraction(0.5), Pos::fraction(0.5), Pos::fraction(0.25), tr);
            const Extent e = extent(g.rings[0]);
            check(near(e.x1 - e.x0, 50.0, 0.5) && near(e.y1 - e.y0, 50.0, 0.5)
                  && near((e.x0 + e.x1) * 0.5, 150.0, 0.5),
                  "shapes: a fraction radius is round, of min(w, h), around a fraction centre");
        }

        // Pixels positions are from the frame's left / bottom edge, y up.
        {
            const ShapeGeom g = circle_geometry(Pos::pixels(30.0), Pos::pixels(20.0), Pos::pixels(10.0), tr);
            const Extent e = extent(g.rings[0]);
            check(near((e.x0 + e.x1) * 0.5, 80.0, 0.5) && near((e.y0 + e.y1) * 0.5, 130.0, 0.5),
                  "shapes: a pixel centre is measured from the frame's left and bottom edge");
        }

        // A data circle bends on a log axis: x = 6..14 around 10 is not symmetric.
        {
            CoordTransform lg = tr;
            lg.xs.kind = Scale::Log;
            lg.xmin = 1.0; lg.xmax = 100.0;
            const ShapeGeom g = circle_geometry(Pos(10.0), c5, Pos(4.0), lg);
            const Extent e = extent(g.rings[0]);
            check(near(e.x0, lg.to_px(6.0), 0.5) && near(e.x1, lg.to_px(14.0), 0.5)
                  && (e.x1 - lg.to_px(10.0)) < (lg.to_px(10.0) - e.x0) - 5.0,
                  "shapes: a data circle is stretched towards the low end of a log axis");
            // Past 0 on a log axis the shape runs off the frame instead of vanishing.
            const ShapeGeom h = circle_geometry(Pos(2.0), c5, Pos(4.0), lg);
            check(!h.empty() && extent(h.rings[0]).x0 < lg.px - 100.0,
                  "shapes: a log axis's non-positive side runs past the frame's low edge");
        }

        // Wedges: a quarter, a wrap through 0, a full turn, no sweep.
        {
            const Pos r = Pos::pixels(40.0), z = Pos::pixels(0.0);
            const ShapeGeom q = wedge_geometry(c5, c5, r, 0.0, 90.0, z, tr);
            check(!q.empty() && within(tri_area(q), kPi * 1600.0 / 4.0, 0.01),
                  "shapes: a quarter wedge covers a quarter of the disc");
            check(q.rings[0].size() > 3 && near(q.rings[0][0].x, 150.0, 1e-3) && near(q.rings[0][0].y, 100.0, 1e-3),
                  "shapes: a sector's outline starts at its centre");
            const ShapePt end = q.rings[0].back();
            check(near(end.x, 150.0, 0.01) && near(end.y, 60.0, 0.01),
                  "shapes: the arc ends at theta2 (90 degrees is up on screen)");
            const ShapeGeom w = wedge_geometry(c5, c5, r, 270.0, 90.0, z, tr);
            check(within(tri_area(w), kPi * 1600.0 / 2.0, 0.01), "shapes: theta2 < theta1 wraps counter-clockwise");
            check(within(tri_area(wedge_geometry(c5, c5, r, -10.0, 500.0, z, tr)), kPi * 1600.0, 0.01),
                  "shapes: 360 degrees or more is the whole disc");
            check(wedge_geometry(c5, c5, r, 30.0, 30.0, z, tr).empty(), "shapes: no sweep draws nothing");
        }

        // Annulus: a full one has two rings, a partial one a single closed ring.
        {
            const Pos r = Pos::pixels(40.0), in = Pos::pixels(20.0);
            const ShapeGeom a = wedge_geometry(c5, c5, r, 0.0, 360.0, in, tr);
            check(a.rings.size() == 2 && within(tri_area(a), kPi * (1600.0 - 400.0), 0.01),
                  "shapes: a full annulus fills pi (R^2 - r^2) between two rings");
            check(!shape_contains(a, 150.0f, 100.0f) && shape_contains(a, 150.0f + 30.0f, 100.0f),
                  "shapes: the hole of an annulus is outside, the band inside");
            const ShapeGeom h = wedge_geometry(c5, c5, r, 0.0, 180.0, in, tr);
            check(h.rings.size() == 1 && within(tri_area(h), kPi * 1200.0 / 2.0, 0.01),
                  "shapes: half an annulus is one ring");
            check(wedge_geometry(c5, c5, r, 0.0, 90.0, Pos::pixels(40.0), tr).empty()
                  && wedge_geometry(c5, c5, Pos::pixels(0.0), 0.0, 90.0, Pos::pixels(0.0), tr).empty(),
                  "shapes: an inner radius not below the outer one, or a zero radius, draws nothing");
        }

        // Polygons: ear clipping covers the area exactly, either winding.
        {
            ShapeGeom g = poly_geometry(kLx, kLy, Coords::Data, tr);
            check(g.tris.size() == 4 * 3 && g.rings.size() == 1 && g.rings[0].size() == 6,
                  "shapes: an L of six vertices is four triangles");
            check(near(tri_area(g), ring_area(g.rings[0]), 1e-3) && near(ring_area(g.rings[0]), 12.0 * 200.0, 1e-2),
                  "shapes: the triangles cover the L exactly (12 square units, 200 px^2 each)");
            std::vector<double> rx(kLx.rbegin(), kLx.rend()), ry(kLy.rbegin(), kLy.rend());
            g = poly_geometry(rx, ry, Coords::Data, tr);
            check(near(tri_area(g), 12.0 * 200.0, 1e-2), "shapes: the other winding fills the same");
            // A repeated first point and duplicates are dropped.
            std::vector<double> dx = kLx, dy = kLy;
            dx.insert(dx.begin() + 2, dx[1]);
            dy.insert(dy.begin() + 2, dy[1]);
            dx.push_back(dx[0]);
            dy.push_back(dy[0]);
            g = poly_geometry(dx, dy, Coords::Data, tr);
            check(g.rings.size() == 1 && g.rings[0].size() == 6 && near(tri_area(g), 12.0 * 200.0, 1e-2),
                  "shapes: a repeated vertex and a closing duplicate are dropped");
            const ShapeGeom l = poly_geometry(kLx, kLy, Coords::Data, tr);
            check(shape_contains(l, tr.to_px(1.0), tr.to_py(3.0)) && !shape_contains(l, tr.to_px(3.0), tr.to_py(3.0)),
                  "shapes: the L's notch is outside");
            const std::vector<double> two{1, 2};
            check(poly_geometry(two, two, Coords::Data, tr).empty(), "shapes: fewer than three vertices fill nothing");
            const std::vector<double> col{0, 1, 2};
            check(poly_geometry(col, col, Coords::Data, tr).empty(), "shapes: collinear vertices fill nothing");
            // Spaces.
            const std::vector<double> px{10, 60, 10}, py{10, 10, 60};
            g = poly_geometry(px, py, Coords::Pixels, tr);
            const Extent e = extent(g.rings[0]);
            check(near(e.x0, 60.0, 1e-3) && near(e.x1, 110.0, 1e-3) && near(e.y0, 90.0, 1e-3) && near(e.y1, 140.0, 1e-3),
                  "shapes: pixel vertices are from the frame's bottom left corner, y up");
            const std::vector<double> fx{0, 1, 0}, fy{0, 0, 1};
            g = poly_geometry(fx, fy, Coords::Fraction, tr);
            check(near(tri_area(g), 200.0 * 100.0 / 2.0, 1e-2), "shapes: fraction vertices span the frame");
            // A bow tie (self-intersecting) terminates and gives whole triangles.
            const std::vector<double> bx{0, 2, 2, 0}, by{0, 2, 0, 2};
            g = poly_geometry(bx, by, Coords::Data, tr);
            check(g.tris.size() % 3 == 0, "shapes: a self-intersecting polygon still terminates");
            const std::vector<double> nan{0, std::nan(""), 1};
            check(poly_geometry(nan, nan, Coords::Data, tr).empty(), "shapes: a non-finite vertex gives nothing");
        }

        // The autoscale box of a data-space circle or wedge.
        {
            auto b = wedge_data_box(5, 5, 2, 0, 360, 0);
            check(b && near(b->xmin, 3, 1e-9) && near(b->xmax, 7, 1e-9) && near(b->ymin, 3, 1e-9) && near(b->ymax, 7, 1e-9),
                  "shapes: a circle's box is centre +- r");
            b = wedge_data_box(5, 5, 2, 0, 90, 0);
            check(b && near(b->xmin, 5, 1e-9) && near(b->xmax, 7, 1e-9) && near(b->ymin, 5, 1e-9) && near(b->ymax, 7, 1e-9),
                  "shapes: a quarter wedge's box is the quarter, with its centre");
            b = wedge_data_box(5, 5, 2, 45, 135, 1);
            check(b && near(b->ymax, 7, 1e-9) && near(b->xmin, 5 - 2 * std::sqrt(0.5), 1e-9),
                  "shapes: an annular sector's box takes the arc and its quarter-turn crossings");
            check(!wedge_data_box(5, 5, 0, 0, 90, 0) && !wedge_data_box(5, 5, 2, 10, 10, 0),
                  "shapes: a degenerate wedge has no box");
        }
    }
} // namespace lt

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;

        template <class F>
        bool throws_invalid(F&& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        template <class F>
        bool throws_range(F&& f) {
            try {
                f();
            } catch (const std::out_of_range&) {
                return true;
            }
            return false;
        }

        bool close(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
    } // namespace

    // The plotting calls, read-back, set_*_data(), visibility, ids, cla(), the
    // legend key and the autoscale limits; Axes and Plane2D.
    void test_shapes_api() {
        std::printf("\n[shapes: calls, read-back, limits, legend]\n");

        auto fig = Figure::create();
        auto ax = fig->add_subplot(1, 1, 1);

        // The calls and their read-back.
        const V px{0, 4, 4, 2, 2, 0}, py{0, 0, 2, 2, 4, 4};
        ax->shape_poly(px, py);
        const ObjectId pid = ax->last_object();
        ax->shape_circle(5.0, 6.0, 1.5, {.name = "disc"});
        const ObjectId cid = ax->last_object();
        ax->shape_wedge(Pos::fraction(0.5), Pos::pixels(20.0), Pos::pixels(30.0), 10.0, 100.0, {.inner = 12.0});
        const ObjectId wid = ax->last_object();
        check(ax->shape_poly_count() == 1 && ax->shape_circle_count() == 1 && ax->shape_wedge_count() == 1
                  && pid != 0 && cid != pid && wid != cid,
              "shapes: three kinds, each with its own count and id");
        check(ax->shape_poly_data(0).x == px && ax->shape_poly_data(0).y == py, "shapes: a polygon's vertices read back");
        const CircleData cd = ax->shape_circle_data(0);
        check(cd.cx.v == 5.0 && cd.cx.space == Coords::Data && cd.r.v == 1.5, "shapes: a circle reads back");
        const WedgeData wd = ax->shape_wedge_data(0);
        check(wd.cx.space == Coords::Fraction && wd.cy.space == Coords::Pixels && wd.r.space == Coords::Pixels
                  && wd.theta1 == 10.0 && wd.theta2 == 100.0 && wd.inner == 12.0,
              "shapes: a wedge reads back with each Pos's space and its hole");
        check(throws_range([&] { ax->shape_poly_data(1); }) && throws_range([&] { ax->shape_circle_data(3); })
                  && throws_range([&] { ax->shape_wedge_data(1); }),
              "shapes: read-back past the count throws std::out_of_range");

        // Validation: nothing is added when a call throws.
        check(throws_invalid([&] { ax->shape_poly(V{0, 1}, V{0, 1}); })
                  && throws_invalid([&] { ax->shape_poly(V{0, 1, 2}, V{0, 1}); })
                  && throws_invalid([&] { ax->shape_poly(V{0, 1, std::nan("")}, V{0, 1, 2}); })
                  && throws_invalid([&] { ax->shape_poly(px, py, {.zorder = std::nan("")}); }),
              "shapes: a polygon needs >= 3 finite vertices, one y per x, and a finite zorder");
        check(throws_invalid([&] { ax->shape_circle(1.0, 1.0, 0.0); })
                  && throws_invalid([&] { ax->shape_circle(1.0, 1.0, -2.0); })
                  && throws_invalid([&] { ax->shape_circle(std::nan(""), 1.0, 1.0); })
                  && throws_invalid([&] { ax->shape_circle(Pos::fraction(0.5), 1.0, 1.0); })
                  && throws_invalid([&] { ax->shape_circle(1.0, Pos::pixels(5.0), Pos(1.0)); }),
              "shapes: a circle needs a positive finite radius, and a Data radius a Data centre");
        ax->shape_circle(Pos::fraction(0.5), Pos::fraction(0.5), Pos::fraction(0.1));   // any centre with a Fraction radius
        ax->shape_circle(Pos::fraction(0.5), 2.0, Pos::pixels(4.0));                    // mixed centre, pixel radius
        check(throws_invalid([&] { ax->shape_wedge(1.0, 1.0, 1.0, 0.0, 90.0, {.inner = 1.0}); })
                  && throws_invalid([&] { ax->shape_wedge(1.0, 1.0, 1.0, 0.0, 90.0, {.inner = -0.1}); })
                  && throws_invalid([&] { ax->shape_wedge(1.0, 1.0, 1.0, std::nan(""), 90.0); })
                  && throws_invalid([&] { ax->shape_wedge(1.0, 1.0, 0.0, 0.0, 90.0); }),
              "shapes: a wedge needs finite angles and a hole in [0, r)");
        check(ax->shape_poly_count() == 1 && ax->shape_circle_count() == 3 && ax->shape_wedge_count() == 1,
              "shapes: a call that throws adds nothing, one that is valid adds one");

        // set_*_data() validates as the call, then replaces.
        ax->set_shape_poly_data(0, V{0, 1, 0}, V{0, 0, 1});
        ax->set_shape_circle_data(0, {Pos(2.0), Pos(2.0), Pos(0.5)});
        ax->set_shape_wedge_data(0, {Pos(1.0), Pos(1.0), Pos(2.0), 0.0, 45.0, 1.0});
        check(ax->shape_poly_data(0).x == V({0, 1, 0}) && ax->shape_circle_data(0).r.v == 0.5
                  && ax->shape_wedge_data(0).theta2 == 45.0,
              "shapes: set_shape_*_data() replaces the numbers");
        check(throws_invalid([&] { ax->set_shape_poly_data(0, V{0, 1}, V{0, 1}); })
                  && throws_invalid([&] { ax->set_shape_circle_data(0, {Pos(1.0), Pos(1.0), Pos(-1.0)}); })
                  && throws_invalid([&] { ax->set_shape_wedge_data(0, {Pos(1.0), Pos(1.0), Pos(1.0), 0.0, 90.0, 2.0}); })
                  && throws_range([&] { ax->set_shape_circle_data(9, {Pos(1.0), Pos(1.0), Pos(1.0)}); })
                  && ax->shape_poly_data(0).x == V({0, 1, 0}),
              "shapes: bad data throws and changes nothing");

        // Visibility and zorder go through the shared object calls.
        ax->set_shape_circle_visible(0, false);
        check(!ax->shape_circle_visible(0) && ax->shape_poly_visible(0) && ax->shape_wedge_visible(0),
              "shapes: set_shape_*_visible() hides one kind's object");
        ax->set_shape_circle_visible(0, true);
        check(ax->set_zorder(cid, 3.0) && ax->zorder(cid) == 3.0 && ax->zorder(pid) == 0.0,
              "shapes: set_zorder() takes a shape's id");
        const auto at = ax->find_object(wid);
        check(at && at->kind == PickKind::Wedge && at->index == 0 && ax->find_object(pid)->kind == PickKind::Poly
                  && ax->find_object(cid)->kind == PickKind::Circle,
              "shapes: find_object() names the kind and index");
        check(ax->remove(cid) && ax->shape_circle_count() == 2 && !ax->find_object(cid),
              "shapes: remove() takes a shape out by id");

        // Autoscale: data shapes count, Fraction / Pixels ones do not.
        {
            auto f = Figure::create();
            auto a = f->add_subplot(1, 1, 1);
            a->shape_poly(V{10, 20, 20}, V{0, 0, 5});
            check(a->xlim().lo < 10.0 && a->xlim().hi > 20.0 && a->ylim().hi > 5.0 && a->ylim().lo < 0.0,
                  "limits: a data polygon's vertices count");
            auto g = Figure::create();
            auto b = g->add_subplot(1, 1, 1);
            b->shape_circle(5.0, 5.0, 2.0);
            check(b->xlim().lo < 3.0 && b->xlim().hi > 7.0 && b->ylim().lo < 3.0 && b->ylim().hi > 7.0,
                  "limits: a data circle counts as its box");
            auto h = Figure::create();
            auto c = h->add_subplot(1, 1, 1);
            c->line(V{0, 1}, V{0, 1});
            const Range x0 = c->xlim(), y0 = c->ylim();
            c->shape_circle(Pos::fraction(0.5), Pos::fraction(0.5), Pos::pixels(100.0));
            c->shape_poly(V{1000, 2000, 2000}, V{1000, 1000, 2000}, {.space = Coords::Fraction});
            c->shape_wedge(Pos::pixels(10.0), Pos::pixels(10.0), Pos::fraction(0.2), 0.0, 90.0);
            check(c->xlim().lo == x0.lo && c->xlim().hi == x0.hi && c->ylim().lo == y0.lo && c->ylim().hi == y0.hi,
                  "limits: Fraction and Pixels shapes move nothing");
            c->shape_wedge(0.5, 0.5, 20.0, 0.0, 90.0);
            check(c->xlim().hi > 20.0, "limits: a data wedge counts");
            c->set_shape_wedge_visible(1, false);
            check(c->xlim().hi < 20.0, "limits: a hidden shape leaves them");
            // A twin: its own y and the shared x.
            auto tf = Figure::create();
            auto host = tf->add_subplot(1, 1, 1);
            host->line(V{0, 1}, V{0, 1});
            auto twin = host->twinx();
            twin->shape_poly(V{0, 30, 30}, V{0, 0, 70});
            check(twin->ylim().hi > 70.0 && host->ylim().hi < 2.0 && host->xlim().hi > 30.0,
                  "limits: a twin's shape widens its own y and the shared x");
        }

        // The legend: a patch at the shape's opacity, outlined when it is.
        {
            RenderSnapshot s;
            PolyPlot p;
            p.opts.name = "poly";
            p.opts.alpha = 0.4f;
            p.opts.color = Color::Red;
            s.polys.push_back(p);
            CirclePlot c;
            c.opts.name = "ring";
            c.opts.linewidth = 2.0f;
            c.opts.edgecolor = Color::Black;
            c.id = 1;
            s.circles.push_back(c);
            WedgePlot w;
            w.opts.name = "hidden";
            w.opts.show_legend = false;
            w.id = 2;
            s.wedges.push_back(w);
            const auto e = collect_legend_entries(s);
            check(e.size() == 2 && e[0].name == "poly" && e[0].kind == LegendKind::Bar && close(e[0].color.a, 0.4, 1e-6)
                      && e[0].edge.a == 0.0f && e[1].name == "ring" && e[1].edge.r == Color::Black.r && e[1].edge.a == 1.0f,
                  "legend: a shape is keyed as a patch at its opacity, edged when outlined; show_legend = false keys none");
        }

        // cla() clears them.
        ax->cla();
        check(ax->shape_poly_count() == 0 && ax->shape_circle_count() == 0 && ax->shape_wedge_count() == 0,
              "shapes: cla() clears them");

        // A plane: the same calls on its sheet.
        {
            auto f3 = Figure::create();
            auto a3 = f3->add_subplot3d(1, 1, 1);
            auto pl = a3->plane(PlaneOrientation::XY, 0.5);
            pl->shape_poly(px, py);
            pl->shape_circle(2.0, 2.0, 1.0);
            pl->shape_wedge(Pos::pixels(10.0), Pos::pixels(10.0), Pos::pixels(5.0), 0.0, 90.0);
            check(pl->shape_poly_count() == 1 && pl->shape_circle_count() == 1 && pl->shape_wedge_count() == 1
                      && pl->shape_circle_data(0).r.v == 1.0,
                  "plane: the three calls and their read-back");
            pl->set_shape_circle_data(0, {Pos(2.0), Pos(2.0), Pos(30.0)});
            check(pl->shape_circle_data(0).r.v == 30.0 && a3->xlim().hi > 30.0,
                  "plane: set_shape_circle_data(), and a data shape widens the parent's limits");
            pl->set_shape_poly_visible(0, false);
            check(!pl->shape_poly_visible(0), "plane: set_shape_poly_visible()");
            pl->cla();
            check(pl->shape_poly_count() == 0 && pl->shape_circle_count() == 0, "plane: cla() clears shapes");
        }
    }
} // namespace lt

namespace lt {
    using namespace sextant;

    namespace {
        struct ShapePng {
            int w = 0, h = 0;
            std::vector<unsigned char> px;

            explicit ShapePng(const char* path) {
                int comp = 0;
                unsigned char* p = stbi_load(path, &w, &h, &comp, 4);
                if (p) px.assign(p, p + static_cast<std::size_t>(w) * h * 4);
                stbi_image_free(p);
            }

            // Within `tol` levels of (r, g, b) at pixel (x, y).
            bool is(float x, float y, int r, int g, int b, int tol = 40) const {
                const int ix = static_cast<int>(x), iy = static_cast<int>(y);
                if (ix < 0 || iy < 0 || ix >= w || iy >= h || px.empty()) return false;
                const unsigned char* p = px.data() + (static_cast<std::size_t>(iy) * w + ix) * 4;
                return std::abs(p[0] - r) < tol && std::abs(p[1] - g) < tol && std::abs(p[2] - b) < tol;
            }
        };

        std::string read_text(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        std::size_t count_sub(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
            return n;
        }
    } // namespace

    // PNG (GL) and SVG of the three shapes, in call order with a line over them.
    void test_shapes_rendered() {
        std::printf("\n[shapes: rendered]\n");

        const int W = 400, H = 320;
        const Color red{0.8f, 0.0f, 0.0f, 1.0f}, blue{0.0f, 0.0f, 0.8f, 1.0f}, green{0.0f, 0.6f, 0.0f, 1.0f};
        FigureSnapshot fs;
        {
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            RenderSnapshot& s = *fa.snap2d();
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            // An L, opaque red (id 1).
            PolyPlot l;
            l.x = std::vector<double>{0, 4, 4, 2, 2, 0};
            l.y = std::vector<double>{0, 0, 2, 2, 4, 4};
            l.opts.color = red;
            l.id = 1;
            s.polys.push_back(l);
            // A data-unit circle (an ellipse here), translucent blue with a green
            // dashed outline (id 2).
            CirclePlot c;
            c.cx = Pos(7.0);
            c.cy = Pos(7.0);
            c.r = Pos(2.0);
            c.opts.color = blue;
            c.opts.alpha = 0.5f;
            c.opts.linewidth = 2.0f;
            c.opts.edgecolor = green;
            c.opts.linestyle = LineStyle::Dashed;
            c.id = 2;
            s.circles.push_back(c);
            // A round green disc: a fraction centre, a 25 px radius (id 3).
            CirclePlot d;
            d.cx = Pos::fraction(0.75);
            d.cy = Pos::fraction(0.2);
            d.r = Pos::pixels(25.0);
            d.opts.color = green;
            d.id = 3;
            s.circles.push_back(d);
            // A quarter wedge, opaque red, 40 px, at (2, 7) (id 4).
            WedgePlot w;
            w.cx = Pos(2.0);
            w.cy = Pos(7.0);
            w.r = Pos::pixels(40.0);
            w.theta1 = 0.0;
            w.theta2 = 90.0;
            w.opts.color = red;
            w.id = 4;
            s.wedges.push_back(w);
            // A ring at (5, 5): pixel radii 30 / 15, blue (id 5).
            WedgePlot ring;
            ring.cx = Pos(5.0);
            ring.cy = Pos(5.0);
            ring.r = Pos::pixels(30.0);
            ring.inner = 15.0;
            ring.theta2 = 360.0;
            ring.opts.color = blue;
            ring.id = 5;
            s.wedges.push_back(ring);
            // A black line over everything (id 6).
            LinePlot lp;
            lp.x = std::vector<double>{0.0, 10.0};
            lp.y = std::vector<double>{9.5, 9.5};
            lp.opts.color = Color::Black;
            lp.opts.linewidth = 3.0f;
            lp.id = 6;
            s.lines.push_back(lp);
            fs.axes.push_back(std::move(fa));
            fs.generation = fs.data_generation = 1;
        }
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            export_figure_png(dev, data, fs, "shapes.png", W, H, 1);
            export_figure_svg(fs, "shapes.svg", W, H);
        }
        const CellLayout cell = compute_figure_layout(fs, W, H).cells[0];
        const CoordTransform& tr = cell.tr;
        const ShapePng png("shapes.png");
        const std::string svg = read_text("shapes.svg");

        // GL: the L, with its notch empty.
        check(png.is(tr.to_px(1.0), tr.to_py(1.0), 204, 0, 0) && png.is(tr.to_px(3.0), tr.to_py(1.0), 204, 0, 0)
                  && png.is(tr.to_px(1.0), tr.to_py(3.0), 204, 0, 0),
              "gl: the L is filled");
        check(!png.is(tr.to_px(3.0), tr.to_py(3.0), 204, 0, 0), "gl: the L's notch is empty");

        // The data circle is an ellipse (2 units across and up are different pixels).
        const float cx = tr.to_px(7.0), cy = tr.to_py(7.0);
        const float rx = tr.to_px(9.0) - cx, ry = cy - tr.to_py(9.0);
        check(png.is(cx, cy, 128, 128, 230, 60) && png.is(cx + rx * 0.8f, cy, 128, 128, 230, 60)
                  && png.is(cx, cy - ry * 0.8f, 128, 128, 230, 60),
              "gl: the data-unit circle is filled, translucent blue");
        check(!png.is(cx + rx * 1.2f, cy, 128, 128, 230, 60) && !png.is(cx, cy - ry * 1.2f, 128, 128, 230, 60),
              "gl: and ends at its radius along each axis (" + std::to_string(rx) + " x " + std::to_string(ry) + " px)");

        // The pixel-radius disc: round, 25 px, at the frame fraction (0.75, 0.2).
        const float dx = tr.px + 0.75f * tr.pw, dy = tr.py + tr.ph - 0.2f * tr.ph;
        check(png.is(dx, dy, 0, 153, 0) && png.is(dx + 20.0f, dy, 0, 153, 0) && png.is(dx, dy - 20.0f, 0, 153, 0)
                  && !png.is(dx + 30.0f, dy, 0, 153, 0) && !png.is(dx, dy + 30.0f, 0, 153, 0),
              "gl: a pixel-radius disc at a fraction centre is round");

        // The quarter wedge: up and to the right of its centre only.
        const float wx = tr.to_px(2.0), wy = tr.to_py(7.0);
        check(png.is(wx + 20.0f, wy - 20.0f, 204, 0, 0) && !png.is(wx - 20.0f, wy - 20.0f, 204, 0, 0)
                  && !png.is(wx + 20.0f, wy + 20.0f, 204, 0, 0) && !png.is(wx + 38.0f, wy - 38.0f, 204, 0, 0),
              "gl: a 0-90 degree wedge fills the up-right quarter, to its radius");

        // The ring: band filled, centre and outside empty.
        const float rxc = tr.to_px(5.0), ryc = tr.to_py(5.0);
        check(png.is(rxc + 22.0f, ryc, 0, 0, 204) && png.is(rxc, ryc + 22.0f, 0, 0, 204)
                  && !png.is(rxc, ryc, 0, 0, 204) && !png.is(rxc + 36.0f, ryc, 0, 0, 204),
              "gl: the ring keeps its hole");

        // The line is drawn after the shapes.
        check(png.is(tr.to_px(5.0), tr.to_py(9.5), 0, 0, 0), "gl: the line drawn last shows over the shapes");

        // The dashed outline: some of its pixels green, some gaps.
        int on = 0, off = 0;
        for (float px = cx - rx * 0.9f; px < cx + rx * 0.9f; px += 1.0f) {
            const float t = (px - cx) / rx;
            const float py = cy - ry * std::sqrt(std::max(0.0f, 1.0f - t * t));
            if (png.is(px, py, 0, 153, 0, 70)) ++on;
            else ++off;
        }
        check(on > 5 && off > 5, "gl: the circle's outline is dashed (" + std::to_string(on) + " on, " +
                                     std::to_string(off) + " off)");

        // SVG: one fill path per shape, in call order, the L's first; the ring's holds two subpaths.
        const std::size_t paths = count_sub(svg, "fill-rule=\"evenodd\"");
        check(paths == 5, "svg: one even-odd path per shape (" + std::to_string(paths) + ")");
        const std::size_t at_l = svg.find("fill=\"rgb(204,0,0)\" fill-rule"), at_ring = svg.find("fill=\"rgb(0,0,204)\" fill-rule"),
                          at_line = svg.find("<polyline");
        check(at_l != std::string::npos && at_l < at_ring && at_ring < at_line, "svg: in call order, the line last");
        check(svg.find("fill-opacity=\"0.5\"") != std::string::npos && svg.find("stroke-dasharray") != std::string::npos,
              "svg: the circle at half opacity, its outline dashed");
        std::size_t ring_m = 0;
        {
            const std::size_t b = at_ring;
            const std::size_t e = svg.find("/>", b);
            ring_m = b == std::string::npos ? 0 : count_sub(svg.substr(b, e - b), "M");
        }
        check(ring_m == 2, "svg: a ring is two subpaths (" + std::to_string(ring_m) + ")");
    }
} // namespace lt

namespace lt {
    using namespace sextant;

    // Shapes on a plane: drawn by the raster (PNG) and the scene geometry (SVG).
    void test_shapes_plane_rendered() {
        std::printf("\n[shapes: on a plane, rendered]\n");

        const Color red{0.8f, 0.0f, 0.0f, 1.0f}, blue{0.0f, 0.0f, 0.8f, 1.0f};
        FigureOptions fo;
        fo.width = 400;
        fo.height = 320;
        auto fig = Figure::create(fo);
        auto a3 = fig->add_subplot3d(1, 1, 1);
        a3->set_xlim(0, 1).set_ylim(0, 1).set_zlim(0, 1);
        auto pl = a3->plane(PlaneOrientation::XY, 0.0);
        pl->shape_poly(std::vector<double>{0.0, 1.0, 1.0, 0.0}, std::vector<double>{0.0, 0.0, 1.0, 1.0}, {.color = red});
        pl->shape_circle(0.5, 0.5, 0.25, {.color = blue});
        fig->savefig("shapes_plane.png");
        fig->savefig_svg("shapes_plane.svg");

        const ShapePng png("shapes_plane.png");
        std::size_t nred = 0, nblue = 0;
        for (int y = 0; y < png.h; ++y)
            for (int x = 0; x < png.w; ++x) {
                nred += png.is(static_cast<float>(x), static_cast<float>(y), 204, 0, 0);
                nblue += png.is(static_cast<float>(x), static_cast<float>(y), 0, 0, 204);
            }
        check(nred > 2000 && nblue > 500, "plane gl: the polygon and the circle are drawn (" + std::to_string(nred) +
                                              " red, " + std::to_string(nblue) + " blue px)");
        const std::string svg = read_text("shapes_plane.svg");
        check(svg.find("rgb(204,0,0)") != std::string::npos && svg.find("rgb(0,0,204)") != std::string::npos,
              "plane svg: both shapes are written");
        // A fraction circle: laid out on the plane's own sheet, in the raster and in the SVG scene.
        auto g = Figure::create(fo);
        auto b3 = g->add_subplot3d(1, 1, 1);
        b3->set_xlim(0, 1).set_ylim(0, 1).set_zlim(0, 1);
        auto p2 = b3->plane(PlaneOrientation::XY, 0.0);
        p2->shape_circle(Pos::fraction(0.5), Pos::fraction(0.5), Pos::fraction(0.3), {.color = red});
        g->savefig("shapes_plane_frac.png");
        g->savefig_svg("shapes_plane_frac.svg");
        const ShapePng png2("shapes_plane_frac.png");
        std::size_t nred2 = 0;
        for (int y = 0; y < png2.h; ++y)
            for (int x = 0; x < png2.w; ++x) nred2 += png2.is(static_cast<float>(x), static_cast<float>(y), 204, 0, 0);
        check(nred2 > 500, "plane gl: a fraction circle is drawn on the plane's sheet (" + std::to_string(nred2) + " px)");
        check(read_text("shapes_plane_frac.svg").find("rgb(204,0,0)") != std::string::npos,
              "plane svg: and the fraction circle is written too");
    }
} // namespace lt

namespace lt {
    using namespace sextant;

    namespace {
        // 0..10 on both axes over 400 px: 40 px a unit.
        const CoordTransform kShapeTr{0.0, 10.0, 0.0, 10.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};

        std::optional<PickHit> pick_at(const RenderSnapshot& s, double x, double y) {
            return find_pick(s, kShapeTr, kShapeTr.to_px(x), kShapeTr.to_py(y));
        }
    } // namespace

    // Shapes are hovered and picked where they cover, by the same geometry the
    // outputs draw: the L's notch is a miss, a pixel circle is round, a ring has
    // its hole, a later shape wins, a zorder lifts one.
    void test_shapes_pick() {
        std::printf("\n[shapes: pick and hover]\n");

        RenderSnapshot s;
        s.xmin = s.ymin = 0.0;
        s.xmax = s.ymax = 10.0;
        s.xlim_auto = s.ylim_auto = false;
        PolyPlot l;
        l.x = std::vector<double>{0, 4, 4, 2, 2, 0};
        l.y = std::vector<double>{0, 0, 2, 2, 4, 4};
        l.opts.name = "ell";
        l.id = 1;
        s.polys.push_back(l);
        CirclePlot c;
        c.cx = Pos(7.0);
        c.cy = Pos(7.0);
        c.r = Pos(1.0);
        c.id = 2;
        s.circles.push_back(c);
        WedgePlot ring;
        ring.cx = Pos(5.0);
        ring.cy = Pos(5.0);
        ring.r = Pos::pixels(60.0);
        ring.inner = 30.0;
        ring.theta2 = 360.0;
        ring.opts.name = "ring";
        ring.id = 3;
        s.wedges.push_back(ring);

        auto is = [](const std::optional<PickHit>& h, PickKind k, std::size_t o) {
            return h && h->kind == k && h->object == o;
        };
        check(is(pick_at(s, 1.0, 1.0), PickKind::Poly, 0) && is(pick_at(s, 3.0, 1.0), PickKind::Poly, 0),
              "pick: inside the L");
        check(!pick_at(s, 3.0, 3.0), "pick: the L's notch is a miss");
        check(is(pick_at(s, 7.0, 7.0), PickKind::Circle, 0) && is(pick_at(s, 7.9, 7.0), PickKind::Circle, 0)
                  && !pick_at(s, 8.2, 7.0),
              "pick: a data circle, to its radius");
        check(is(pick_at(s, 5.0 + 1.1, 5.0), PickKind::Wedge, 0) && !pick_at(s, 5.0, 5.0) && !pick_at(s, 5.0 + 1.8, 5.0),
              "pick: the ring's band, not its hole or past it (45 px of 60/30)");

        const auto h = pick_at(s, 1.0, 1.0);
        check(h && pick_is_area(h->kind) && format_pick(s, *h) == "x=1, y=1\nell" && h->row == -1,
              "pick: the hint is the cursor's data position, then the name");
        check(format_pick(s, *pick_at(s, 5.0 + 1.1, 5.0)) == "x=6.1, y=5\nring", "pick: and a ring's name");

        // A later object wins where two overlap; a higher zorder lifts the earlier.
        s.circles[0].cx = Pos(1.0);
        s.circles[0].cy = Pos(1.0);
        check(is(pick_at(s, 1.0, 1.0), PickKind::Circle, 0), "pick: the later object wins where shapes overlap");
        s.polys[0].opts.zorder = 5.0;
        check(is(pick_at(s, 1.0, 1.0), PickKind::Poly, 0), "pick: a higher zorder wins over a later call");
        s.polys[0].opts.visible = false;
        check(is(pick_at(s, 1.0, 1.0), PickKind::Circle, 0), "pick: a hidden shape is not picked");

        // A fraction circle is picked where it is drawn.
        {
            RenderSnapshot f = s;
            f.polys.clear();
            f.wedges.clear();
            f.circles.clear();
            CirclePlot d;
            d.cx = Pos::fraction(0.5);
            d.cy = Pos::fraction(0.5);
            d.r = Pos::fraction(0.1);
            d.id = 9;
            f.circles.push_back(d);
            check(is(pick_at(f, 5.0, 5.0), PickKind::Circle, 0) && is(pick_at(f, 5.0 + 0.9, 5.0), PickKind::Circle, 0)
                      && !pick_at(f, 5.0 + 1.2, 5.0),
                  "pick: a fraction circle, round at 0.1 of the frame");
        }

        // A shape on a plane, picked through the ray in the plane's coordinates.
        {
            Transform3D tf;
            tf.xmin = tf.ymin = tf.zmin = 0.0;
            tf.xmax = tf.ymax = tf.zmax = 10.0;
            Camera3D cam;
            cam.azimuth = -55.0;
            cam.elevation = 24.0;
            cam.projection = Projection::Orthographic;
            const Projector3D proj(tf, cam, PlotRect{20.0f, 15.0f, 400.0f, 320.0f}, 0.1f);
            RenderSnapshot3D ps;
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            pl.offset = 5.0;
            CirclePlot pc;
            pc.cx = Pos(5.0);
            pc.cy = Pos(5.0);
            pc.r = Pos(2.0);
            pc.id = 4;
            pc.opts.name = "disc";
            pl.sheet.circles.push_back(pc);
            ps.planes.push_back(pl);
            const Px3 in = proj.project(6.0, 5.0, 5.0);
            const auto hit = find_pick3d(ps, proj, in.x, in.y);
            check(hit && hit->kind == PickKind::Circle && hit->plane == 0 && hit->element == 0
                      && std::abs(hit->data_x - 6.0) < 1e-3 && std::abs(hit->data_y - 5.0) < 1e-3,
                  "plane: a circle on a plane, picked in the plane's own coordinates");
            const Px3 out = proj.project(9.0, 5.0, 5.0);
            check(!find_pick3d(ps, proj, out.x, out.y), "plane: past the circle, nothing");
        }
    }
} // namespace lt

namespace lt {
    using namespace sextant;

    // The Data panel and the journal for shapes and sloped reference lines: the
    // tables and forms, cell / row / whole-number ops with their inverses, style
    // edits, the journal's replay onto the live axes, and the panel drawing a tab
    // of each without widget-id conflicts.
    void test_shapes_panel() {
        std::printf("\n[shapes: Data panel, edits, journal]\n");
        using V = std::vector<double>;

        RenderSnapshot s;
        PolyPlot pp;
        pp.x = V{0, 4, 4, 2};
        pp.y = V{0, 0, 2, 2};
        pp.opts.color = Color::Red;
        pp.id = 1;
        s.polys.push_back(pp);
        CirclePlot cp;
        cp.cx = Pos(5.0); cp.cy = Pos(5.0); cp.r = Pos(2.0);
        cp.opts.color = Color::Green;
        cp.id = 2;
        s.circles.push_back(cp);
        WedgePlot wp;
        wp.cx = Pos(1.0); wp.cy = Pos(1.0); wp.r = Pos::pixels(40.0);
        wp.theta1 = 10.0; wp.theta2 = 100.0; wp.inner = 10.0;
        wp.opts.color = Color::Blue;
        wp.id = 3;
        s.wedges.push_back(wp);
        AxLinePlot al;
        al.mode = AxLineMode::PointSlope;
        al.x = Pos(1.0); al.y = Pos(2.0); al.slope = 0.5;
        al.opts.color = Color::Black;
        al.id = 4;
        s.axlines.push_back(al);
        AxLinePlot av;
        av.value = 3.0;
        av.opts.color = Color::Black;
        av.id = 5;
        s.axlines.push_back(av);

        // The tables: a polygon's vertices, forms for the rest; a value line keeps its row.
        {
            const auto t = collect_plot_data_tables(s);
            const PlotDataTable *poly = nullptr, *circle = nullptr, *wedge = nullptr, *sloped = nullptr, *value = nullptr;
            for (const auto& x : t) {
                if (x.kind == PlotKind::Poly) poly = &x;
                else if (x.kind == PlotKind::Circle) circle = &x;
                else if (x.kind == PlotKind::Wedge) wedge = &x;
                else if (x.kind == PlotKind::AxLine && x.sloped_axline) sloped = &x;
                else if (x.kind == PlotKind::AxLine) value = &x;
            }
            check(poly && poly->columns.size() == 2 && std::string(poly->columns[0].name) == "x" && poly->columns[0].count == 4
                      && !poly->rows_fixed && poly->label == "poly 0",
                  "panel: a polygon is a table of x and y with addable rows");
            check(circle && circle->circle && !circle->wedge && wedge && wedge->wedge && sloped && sloped->sloped_axline
                      && sloped->columns.empty() && value && !value->sloped_axline && value->columns.size() == 1,
                  "panel: a circle, a wedge and a sloped line are forms; a value line keeps its row");
        }

        // Cells and rows of a polygon, with their inverses.
        {
            RenderSnapshot c = s;
            std::vector<PlotDataOp> undo;
            apply_plot_data_ops(c, {PlotCellEdit{PlotKind::Poly, 0, 1, 2, 9.0}, PlotCellEdit{PlotKind::Poly, 0, 0, 0, -1.0}}, &undo);
            check(c.polys[0].y[2] == 9.0 && c.polys[0].x[0] == -1.0, "edits: a polygon's cells");
            apply_plot_data_ops(c, undo);
            check(c.polys[0].y[2] == 2.0 && c.polys[0].x[0] == 0.0, "edits: and undone");
            std::vector<PlotDataOp> rows;
            apply_plot_data_ops(c, {PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Poly, 0, 2},
                                    PlotRowEdit{PlotRowEdit::Op::Remove, PlotKind::Poly, 0, 0}}, &rows);
            check(c.polys[0].x.size() == 4 && c.polys[0].x[0] == 4.0, "edits: a vertex inserted, one removed");
            apply_plot_data_ops(c, rows);
            check(c.polys[0].x.size() == 4 && c.polys[0].x[0] == 0.0 && c.polys[0].y[3] == 2.0, "edits: rows undone");
        }

        // A circle's and a wedge's numbers, replaced whole; invalid ones change nothing.
        {
            RenderSnapshot c = s;
            std::vector<PlotDataOp> undo;
            ShapeDataEdit ce;
            ce.plot_index = 0;
            ce.kind = PlotKind::Circle;
            ce.data = WedgeData{Pos::fraction(0.25), Pos::pixels(30.0), Pos::pixels(12.0)};
            ShapeDataEdit we;
            we.plot_index = 0;
            we.kind = PlotKind::Wedge;
            we.data = WedgeData{Pos(2.0), Pos(3.0), Pos::fraction(0.1), 0.0, 270.0, 0.02};
            apply_plot_data_ops(c, {ce, we}, &undo);
            check(c.circles[0].cx.space == Coords::Fraction && c.circles[0].cy.v == 30.0 && c.circles[0].r.v == 12.0
                      && c.wedges[0].theta2 == 270.0 && c.wedges[0].r.space == Coords::Fraction && c.wedges[0].inner == 0.02,
                  "edits: a circle's and a wedge's numbers, spaces included");
            apply_plot_data_ops(c, undo);
            check(c.circles[0].cx.v == 5.0 && c.circles[0].cx.space == Coords::Data && c.circles[0].r.v == 2.0
                      && c.wedges[0].theta1 == 10.0 && c.wedges[0].theta2 == 100.0 && c.wedges[0].inner == 10.0
                      && c.wedges[0].r.space == Coords::Pixels,
                  "edits: undone, every number and space back");
            ShapeDataEdit bad = ce;
            bad.data.r = Pos(-1.0);
            ShapeDataEdit bad2 = ce;
            bad2.data.r = Pos(2.0);   // a data radius, a fraction centre
            ShapeDataEdit bad3 = we;
            bad3.data.inner = 99.0;
            apply_plot_data_ops(c, {bad, bad2, bad3});
            check(c.circles[0].r.v == 2.0 && c.wedges[0].inner == 10.0,
                  "edits: a radius <= 0, a data radius off a data centre, a hole past the radius are refused");
        }

        // A sloped line's numbers, replaced whole, its mode too.
        {
            RenderSnapshot c = s;
            std::vector<PlotDataOp> undo;
            AxLineDataEdit e;
            e.plot_index = 0;
            e.data.mode = AxLineMode::TwoPoints;
            e.data.x = Pos(0.0); e.data.y = Pos(0.0); e.data.x2 = Pos(4.0); e.data.y2 = Pos(2.0);
            apply_plot_data_ops(c, {e}, &undo);
            check(c.axlines[0].mode == AxLineMode::TwoPoints && c.axlines[0].x2.v == 4.0, "edits: a sloped line to two points");
            apply_plot_data_ops(c, undo);
            check(c.axlines[0].mode == AxLineMode::PointSlope && c.axlines[0].slope == 0.5 && c.axlines[0].x.v == 1.0,
                  "edits: and back");
            AxLineDataEdit same = e;
            same.data.x2 = same.data.x;
            same.data.y2 = same.data.y;
            apply_plot_data_ops(c, {same});
            check(c.axlines[0].mode == AxLineMode::PointSlope, "edits: two equal points are refused");
        }

        // Style edits, and undo.
        {
            RenderSnapshot o = s;
            std::vector<PlotStyleEdit> inv;
            PolyOptions po = o.polys[0].opts;
            po.alpha = 0.4f;
            po.space = Coords::Fraction;
            CircleOptions co = o.circles[0].opts;
            co.linewidth = 2.5f;
            WedgeOptions wo = o.wedges[0].opts;
            wo.edgecolor = Color::Red;
            apply_plot_style_edits(o, {PlotStyleEdit{0, -1, po}, PlotStyleEdit{0, -1, co}, PlotStyleEdit{0, -1, wo}}, &inv);
            check(o.polys[0].opts.alpha == 0.4f && o.polys[0].opts.space == Coords::Fraction && o.circles[0].opts.linewidth == 2.5f
                      && o.wedges[0].opts.edgecolor && o.wedges[0].opts.edgecolor->r == Color::Red.r,
                  "style: each shape kind's options apply");
            apply_plot_style_edits(o, inv);
            check(o.polys[0].opts.alpha == 1.0f && o.polys[0].opts.space == Coords::Data && o.circles[0].opts.linewidth == 0.0f
                      && !o.wedges[0].opts.edgecolor,
                  "style: undone");
        }

        // The journal: edits reach the snapshot, replay onto the live axes, and undo.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->shape_circle(5.0, 5.0, 2.0);
            ax->axline(1.0, 2.0, 0.5);
            auto& fi = detail::FigureAccess::impl(*fig);
            AxesEdit ae;
            ShapeDataEdit ce;
            ce.plot_index = 0;
            ce.kind = PlotKind::Circle;
            ce.data = WedgeData{Pos(6.0), Pos(5.0), Pos(1.0)};
            ae.plot_ops.push_back(ce);
            AxLineDataEdit le;
            le.plot_index = 0;
            le.data.mode = AxLineMode::PointSlope;
            le.data.x = Pos(0.0); le.data.y = Pos(0.0); le.data.slope = 2.0;
            ae.plot_ops.push_back(le);
            ae.plot_styles.push_back(PlotStyleEdit{0, -1, CircleOptions{.color = Color::Red, .linewidth = 3.0f}});
            FigureEdits e;
            e.per_axes.push_back({1, ae});
            const FigureEdits undo = fi.apply_edits(e);
            const auto snap = fi.snapshot_box.load();
            const RenderSnapshot& r = *snap->axes[0].snap2d();
            check(r.circles[0].cx.v == 6.0 && r.circles[0].opts.linewidth == 3.0f && r.axlines[0].slope == 2.0,
                  "journal: the edits reach the snapshot");
            fi.apply_edits_and_publish();
            check(ax->shape_circle_data(0).cx.v == 6.0 && ax->axline_data(0).slope == 2.0 && ax->axline_data(0).x.v == 0.0,
                  "journal: and replay onto the live axes");
            fi.apply_edits(undo);
            const auto u = fi.snapshot_box.load();
            check(u->axes[0].snap2d()->circles[0].cx.v == 5.0 && u->axes[0].snap2d()->axlines[0].slope == 0.5
                      && u->axes[0].snap2d()->circles[0].opts.linewidth == 0.0f,
                  "journal: undo restores the numbers and the style");
        }

        // On a plane: addressed through the plane, applied to its sheet.
        {
            RenderSnapshot3D rs;
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            CirclePlot pc;
            pc.cx = Pos(5.0); pc.cy = Pos(5.0); pc.r = Pos(1.0);
            pc.opts.color = Color::Red;
            pc.id = 7;
            pl.sheet.circles.push_back(pc);
            pl.id = 11;
            rs.planes.push_back(pl);
            ShapeDataEdit e;
            e.plot_index = 0;
            e.plane_index = 0;
            e.plane_id = 11;
            e.kind = PlotKind::Circle;
            e.data = WedgeData{Pos(7.0), Pos(5.0), Pos(2.0)};
            std::vector<PlotDataOp> undo;
            apply_plot_data_ops(rs, {e}, &undo);
            check(rs.planes[0].sheet.circles[0].cx.v == 7.0 && rs.planes[0].sheet.circles[0].r.v == 2.0,
                  "plane: a circle's numbers through the plane");
            apply_plot_data_ops(rs, undo);
            check(rs.planes[0].sheet.circles[0].cx.v == 5.0, "plane: undone");
        }

        // The panel draws a tab of each, no duplicate widget ids.
        {
            FigureSnapshot fs;
            RenderSnapshot r = s;
            r.polys[0].opts.linewidth = 2.0f;
            r.circles[0].opts.linewidth = 2.0f;
            r.circles[0].opts.edgecolor = Color::Red;
            r.wedges[0].opts.linewidth = 2.0f;
            fs.axes.push_back({{1, 1, 1}, std::move(r)});
            fs.generation = fs.data_generation = 1;
            const IdConflictScan sc = scan_panel_for_id_conflicts(fs, &draw_data_window, "Data", {});
            check(sc.conflicts == 0 && sc.probes > 0, "panel: the shapes' and the sloped line's tabs draw without id conflicts");
        }
    }
} // namespace lt

namespace lt {
    using namespace sextant;

    // Review fixes: an extreme slope, a stale value-cell op on a sloped line, a
    // Data radius with a frame centre in the limits.
    void test_shapes_review_fixes() {
        std::printf("\n[shapes: review fixes]\n");
        CoordTransform tr{};
        tr.xmin = 0.0; tr.xmax = 10.0; tr.ymin = 0.0; tr.ymax = 10.0;
        tr.px = 50.0f; tr.py = 40.0f; tr.pw = 400.0f; tr.ph = 400.0f;
        AxLinePlot p;
        p.mode = AxLineMode::PointSlope;
        p.x = Pos(5.0); p.y = Pos(5.0); p.slope = 1e300;
        const auto runs = axline_paths(p, tr);
        check(runs.size() == 1 && runs[0].size() == 2 && std::fabs(runs[0][0].x - runs[0][1].x) < 1e-3,
              "axline: a huge slope on linear axes is a vertical line, not nothing");

        RenderSnapshot s;
        AxLinePlot al;
        al.mode = AxLineMode::PointSlope;
        al.x = Pos(1.0); al.y = Pos(2.0); al.slope = 0.5;
        al.value = 7.0;
        s.axlines.push_back(al);
        std::vector<PlotDataOp> undo;
        apply_plot_data_ops(s, {PlotCellEdit{PlotKind::AxLine, 0, 0, 0, 9.0}}, &undo);
        check(s.axlines[0].value == 7.0 && undo.empty(), "edits: a value-cell op on a sloped line changes nothing and has no inverse");

        CirclePlot c;
        c.cx = Pos::fraction(0.5); c.cy = Pos(3.0); c.r = Pos(2.0);
        WedgePlot w;
        w.cx = Pos(1.0); w.cy = Pos::pixels(4.0); w.r = Pos(2.0);
        int n = 0;
        shape_limit_points(c, [&](double, double) { ++n; });
        shape_limit_points(w, [&](double, double) { ++n; });
        check(n == 0, "limits: a Data radius with a non-Data centre adds no limit points");

        // The ear-clipping cache never changes the answer: same, panned, edited, bent.
        {
            const std::vector<double> x{0, 4, 4, 2, 2, 0}, y{0, 0, 2, 2, 4, 4};
            PolyTriCache cache;
            const auto same = [](const ShapeGeom& a, const ShapeGeom& b) {
                return a.tris.size() == b.tris.size()
                       && std::equal(a.tris.begin(), a.tris.end(), b.tris.begin(),
                                     [](const ShapePt& p, const ShapePt& q) { return p.x == q.x && p.y == q.y; });
            };
            const auto viewed = [&](const CoordTransform& t, std::span<const double> xs) {
                return same(poly_geometry(xs, y, Coords::Data, t, &cache), poly_geometry(xs, y, Coords::Data, t));
            };
            CoordTransform t = tr;
            bool ok = viewed(t, x) && viewed(t, x);                   // cold, then hit
            t.xmin = -3.0; t.xmax = 7.0; t.ymin = 1.0; t.ymax = 9.0;
            ok = ok && viewed(t, x);                                  // panned and zoomed: affine, reused
            std::vector<double> x2 = x;
            x2[3] = 3.0;
            ok = ok && viewed(t, x2);                                 // an edited vertex re-clips
            t.xs.kind = Scale::Log; t.xmin = 0.5; t.xmax = 8.0;
            std::vector<double> x3{1, 4, 4, 2, 2, 1};
            ok = ok && viewed(t, x3) && viewed(t, x3);                // a bent axis
            t.xmin = 0.2;
            ok = ok && viewed(t, x3);                                 // a bent axis, new view
            check(ok, "shapes: the polygon's ear-clip cache agrees with an uncached clip");
            check(!cache.tris.empty() && cache.raw.size() == 2 * x3.size(), "shapes: the cache holds the last clipping");
        }
    }
} // namespace lt
