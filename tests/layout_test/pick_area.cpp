// Picking by area: a bar by its body, a fill (and so a stack) by the
// region it covers, a heatmap by its cell, at the cursor's data position; a
// point within the pick radius before any area, whatever the draw order; the
// topmost area among areas, across twins too; a fill on a plane; the radius
// (Figure::set_pick_radius(), FigureOptions::pick_radius) through the search,
// 3D markers and the Pick event.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "figure_impl.h"
#include "fill_geometry.h"
#include "plot_events.h"
#include <cmath>
#include <limits>

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;

        // 0..10 on both axes over 400 px: 40 px a unit.
        const CoordTransform kTr{0.0, 10.0, 0.0, 10.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};

        RenderSnapshot snap_0_10() {
            RenderSnapshot s;
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            return s;
        }

        std::optional<PickHit> at(const RenderSnapshot& s, double x, double y,
                                  float radius = kHintHitRadiusPx, const CoordTransform& tr = kTr) {
            return find_pick(s, tr, tr.to_px(x), tr.to_py(y), nullptr, radius);
        }

        bool is(const std::optional<PickHit>& h, PickKind k, std::size_t object, std::size_t element) {
            return h && h->kind == k && h->object == object && h->element == element;
        }

        FillPlot fill_of(V x, V y1, V y2, ObjectId id = 0) {
            FillPlot f;
            f.x = std::move(x);
            f.y1 = std::move(y1);
            f.y2 = std::move(y2);
            f.id = id;
            return f;
        }

        template <class F>
        bool throws_invalid(F&& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }
    } // namespace

    void test_pick_areas() {
        std::printf("\n[pick: areas, priority, radius]\n");

        // ---- A bar by its body, in either orientation.
        {
            RenderSnapshot s = snap_0_10();
            BarPlot b;
            b.centers = V{2.0, 5.0, 8.0};
            b.heights = V{4.0, 6.0, 0.0};
            b.bar_width = 1.0;
            s.bars.push_back(b);

            const auto in = at(s, 5.0, 3.0);
            check(is(in, PickKind::Bar, 0, 1), "pick bar: inside the body, the bar's index");
            check(in && in->anchor_x == kTr.to_px(5.0) && in->anchor_y == kTr.to_py(3.0) &&
                      std::abs(in->data_x - 5.0) < 1e-4 && std::abs(in->data_y - 3.0) < 1e-4,
                  "pick bar: anchored at the cursor, with its data position");
            check(in && in->row == -1 && in->col == -1, "pick bar: no grid position");
            check(!at(s, 5.0, 6.5) && !at(s, 3.5, 3.0), "pick bar: above the tip, or between bars, nothing");
            check(is(at(s, 5.5 + 1.0 / 40.0, 3.0), PickKind::Bar, 0, 1) && !at(s, 5.5 + 3.0 / 40.0, 3.0),
                  "pick bar: 1 px outside an edge still hits, 3 px does not");
            check(is(at(s, 8.0, 1.0 / 40.0), PickKind::Bar, 0, 2), "pick bar: a zero-height bar, within the margin");
            check(format_pick(s, *in) == find_hint(s, kTr, kTr.to_px(5.0), kTr.to_py(3.0))->text &&
                      format_pick(s, *in).rfind("x=5, height=6", 0) == 0,
                  "pick bar: the hint is the bar's own, wherever the cursor is on it");

            RenderSnapshot based = s;
            based.bars[0].bottom = V{0.0, 2.0, 0.0};
            check(!at(based, 5.0, 1.0) && is(at(based, 5.0, 7.5), PickKind::Bar, 0, 1),
                  "pick bar: from its base to its tip");

            RenderSnapshot lying = s;
            lying.bars[0].opts.orientation = Orientation::Horizontal;
            check(is(at(lying, 3.0, 5.0), PickKind::Bar, 0, 1) && !at(lying, 5.0, 3.0),
                  "pick bar: a lying bar along x");
        }

        // ---- A fill by the region it covers; pick_index the segment.
        {
            RenderSnapshot s = snap_0_10();
            s.fills.push_back(fill_of(V{0.0, 2.0, 4.0, 6.0}, V{1.0, 1.0, 1.0, 1.0}, V{3.0, 5.0, 3.0, 5.0}));
            s.fills[0].opts.name = "band";
            check(is(at(s, 3.0, 2.0), PickKind::Fill, 0, 1) && is(at(s, 1.0, 2.0), PickKind::Fill, 0, 0) &&
                      is(at(s, 5.0, 3.5), PickKind::Fill, 0, 2),
                  "pick fill: the segment between the points the cursor is between");
            check(!at(s, 3.0, 0.5) && !at(s, 5.0, 4.5) && !at(s, 7.0, 2.0),
                  "pick fill: below, above the curve, or past the end, nothing");
            const auto h = at(s, 3.0, 2.0);
            check(h && format_pick(s, *h) == "x=3, y=2\nband" && h->row == -1 && h->col == -1,
                  "pick fill: the hint is the cursor's position and the fill's name");

            // The fill's `exclude` and its NaN break the region.
            RenderSnapshot ex = s;
            ex.fills[0].exclude = V{0.0, 0.0, 1.0, 0.0};
            check(!at(ex, 3.0, 2.0) && !at(ex, 5.0, 2.0) && is(at(ex, 1.0, 2.0), PickKind::Fill, 0, 0),
                  "pick fill: an excluded point leaves both its segments out");

            // Steps: the area drawn, not the straight one.
            RenderSnapshot st = snap_0_10();
            st.fills.push_back(fill_of(V{0.0, 2.0, 4.0}, V{0.0, 0.0, 0.0}, V{1.0, 3.0, 2.0}));
            check(!at(st, 3.0, 2.8), "pick fill: straight, 2.8 is above the curve at x=3");
            st.fills[0].opts.drawstyle = DrawStyle::StepsPost;
            check(is(at(st, 3.0, 2.8), PickKind::Fill, 0, 1) && !at(st, 1.0, 1.5),
                  "pick fill: steps-post holds each value to the next point");
            st.fills[0].opts.drawstyle = DrawStyle::StepsMid;
            check(is(at(st, 1.5, 2.8), PickKind::Fill, 0, 0) && !at(st, 0.5, 1.5),
                  "pick fill: steps-mid changes halfway, in the first point's segment");

            // interpolate: the reach to the crossing belongs to the segment it crosses in.
            RenderSnapshot ip = snap_0_10();
            ip.fills.push_back(fill_of(V{0.0, 2.0, 4.0, 6.0}, V{0.0, 0.0, 0.0, 0.0}, V{2.0, 2.0, -2.0, -2.0}));
            ip.fills[0].exclude = V{0.0, 0.0, 1.0, 1.0};
            check(!at(ip, 2.5, 0.3), "pick fill: without interpolate the region stops at its last point");
            ip.fills[0].opts.interpolate = true;
            check(is(at(ip, 2.5, 0.3), PickKind::Fill, 0, 1) && !at(ip, 3.5, -0.3),
                  "pick fill: with interpolate it reaches the crossing, as segment 1");

            // Vertical: along y, between x1(y) and x2(y).
            RenderSnapshot vt = snap_0_10();
            vt.fills.push_back(fill_of(V{0.0, 10.0}, V{1.0, 1.0}, V{3.0, 3.0}));
            vt.fills[0].opts.orientation = Orientation::Vertical;
            check(is(at(vt, 2.0, 5.0), PickKind::Fill, 0, 0) && !at(vt, 5.0, 2.0),
                  "pick fill: a vertical fill along y");

            // On a log axis, in scale space; a curve at 0 reaches below the view.
            CoordTransform lg{0.0, 10.0, 1.0, 1000.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
            lg.ys.kind = Scale::Log;
            RenderSnapshot ls = snap_0_10();
            ls.fills.push_back(fill_of(V{0.0, 10.0}, V{0.0, 0.0}, V{100.0, 100.0}));
            check(is(at(ls, 5.0, 10.0, kHintHitRadiusPx, lg), PickKind::Fill, 0, 0) &&
                      !at(ls, 5.0, 500.0, kHintHitRadiusPx, lg),
                  "pick fill: on a log axis, up to its curve");

            // A stack: the layer under the cursor; on a shared edge, the later one.
            RenderSnapshot sk = snap_0_10();
            sk.fills.push_back(fill_of(V{0.0, 10.0}, V{0.0, 0.0}, V{1.0, 1.0}, 1));
            sk.fills.push_back(fill_of(V{0.0, 10.0}, V{1.0, 1.0}, V{3.0, 3.0}, 2));
            check(is(at(sk, 5.0, 0.5), PickKind::Fill, 0, 0) && is(at(sk, 5.0, 2.0), PickKind::Fill, 1, 0),
                  "pick fill: a stack's layers, each its own band");

            check(fill_segment_at(s.fills[0], AxisScale{}, AxisScale{}, std::numeric_limits<double>::quiet_NaN(),
                                  2.0) == -1,
                  "pick fill: a NaN position is in no segment");
        }

        // ---- Priority: a point beats any area, whatever the draw order; areas
        // by draw order.
        {
            RenderSnapshot s = snap_0_10();
            s.fills.push_back(fill_of(V{0.0, 10.0}, V{0.0, 0.0}, V{8.0, 8.0}, 5));
            LinePlot lp;
            lp.x = V{2.0, 5.0};
            lp.y = V{2.0, 4.0};
            lp.id = 1;
            s.lines.push_back(lp);
            check(is(at(s, 5.0, 4.0 + 6.0 / 40.0), PickKind::Line, 0, 1),
                  "priority: a line vertex within the radius beats a fill made later");
            check(is(at(s, 3.5, 3.0), PickKind::Fill, 0, 0),
                  "priority: between the line's vertices, the fill under it");

            HeatmapPlot hp;
            hp.rows = 1;
            hp.cols = 1;
            hp.data = std::vector<float>{1.0f};
            hp.xrange = {0.0, 10.0};
            hp.yrange = {0.0, 10.0};
            RenderSnapshot hs = s;
            hs.heatmaps.push_back(hp);
            hs.heatmaps[0].id = 9;
            check(is(at(hs, 5.0, 4.0), PickKind::Line, 0, 1),
                  "priority: a heatmap made after the point does not hide it");
            check(is(at(hs, 3.5, 3.0), PickKind::Heatmap, 0, 0), "priority: the heatmap drawn over the fill");
            hs.heatmaps[0].id = 3;
            check(is(at(hs, 3.5, 3.0), PickKind::Fill, 0, 0), "priority: the fill drawn over the heatmap");

            BarPlot b;
            b.centers = V{7.0};
            b.heights = V{5.0};
            b.bar_width = 2.0;
            b.id = 7;
            RenderSnapshot bs = s;
            bs.bars.push_back(b);
            check(is(at(bs, 7.0, 3.0), PickKind::Bar, 0, 0), "priority: a bar drawn over the fill");
            bs.bars[0].id = 2;
            check(is(at(bs, 7.0, 3.0), PickKind::Fill, 0, 0), "priority: a fill drawn over the bar");
        }

        // ---- Twins: the same rules across layers.
        {
            RenderSnapshot host = snap_0_10(), twin = snap_0_10();
            BarPlot b;
            b.centers = V{5.0};
            b.heights = V{8.0};
            b.bar_width = 4.0;
            b.id = 1;
            host.bars.push_back(b);
            twin.fills.push_back(fill_of(V{0.0, 10.0}, V{0.0, 0.0}, V{6.0, 6.0}, 2));
            CellLayers l;
            l.snaps = {&host, &twin};
            l.trs = {&kTr, &kTr};
            l.keys = {1, axes_key(1, kLayerTwinX)};
            l.layers = {0, kLayerTwinX};
            std::size_t k = 9;
            auto hit = find_pick_layers(l, kTr.to_px(5.0), kTr.to_py(3.0), k);
            check(hit && hit->kind == PickKind::Fill && k == 1, "twins: of two areas, the later object's");
            hit = find_pick_layers(l, kTr.to_px(5.0), kTr.to_py(7.0), k);
            check(hit && hit->kind == PickKind::Bar && k == 0, "twins: the host's bar above the twin's fill");

            ScatterPlot sp;
            sp.x = V{5.0};
            sp.y = V{3.0};
            sp.id = 0;
            host.scatters.push_back(sp);
            hit = find_pick_layers(l, kTr.to_px(5.0) + 5.0f, kTr.to_py(3.0), k);
            check(hit && hit->kind == PickKind::Scatter && k == 0,
                  "twins: the host's point beats the twin's later fill");
            hit = find_pick_layers(l, kTr.to_px(5.0) + 20.0f, kTr.to_py(3.0), k, nullptr, 0, 25.0f);
            check(hit && hit->kind == PickKind::Scatter, "twins: the radius is passed to every layer");
        }

        // ---- The radius: points, plane sheets, 3D markers and vertices.
        {
            RenderSnapshot s = snap_0_10();
            ScatterPlot sp;
            sp.x = V{5.0};
            sp.y = V{5.0};
            s.scatters.push_back(sp);
            const float x = kTr.to_px(5.0) + 20.0f, y = kTr.to_py(5.0);
            check(!find_pick(s, kTr, x, y), "radius: 20 px away is past the default 12");
            check(is(find_pick(s, kTr, x, y, nullptr, 25.0f), PickKind::Scatter, 0, 0) &&
                      find_hint(s, kTr, x, y, nullptr, 25.0f).has_value(),
                  "radius: within 25, hint and pick alike");
            check(!find_pick(s, kTr, kTr.to_px(5.0) + 0.5f, y, nullptr, 0.0f) &&
                      is(find_pick(s, kTr, kTr.to_px(5.0), y, nullptr, 0.0f), PickKind::Scatter, 0, 0),
                  "radius: 0 hits only exactly");

            Transform3D tf;
            tf.xmin = tf.ymin = tf.zmin = 0.0;
            tf.xmax = tf.ymax = tf.zmax = 10.0;
            Camera3D cam;
            cam.azimuth = -55.0;
            cam.elevation = 24.0;
            cam.projection = Projection::Orthographic;
            const Projector3D proj(tf, cam, PlotRect{20.0f, 15.0f, 400.0f, 320.0f}, 0.1f);

            RenderSnapshot3D rs;
            Scatter3DPlot c;
            c.x = V{5.0};
            c.y = V{5.0};
            c.z = V{5.0};
            c.opts.size = 4.0f;
            rs.scatter3d.push_back(c);
            const Px3 q = proj.project(5.0, 5.0, 5.0);
            check(!find_pick3d(rs, proj, q.x + 20.0f, q.y) &&
                      find_pick3d(rs, proj, q.x + 20.0f, q.y, nullptr, 25.0f).has_value(),
                  "radius 3D: a cloud marker within the radius");

            // A fill on a plane, picked through the ray.
            RenderSnapshot3D ps;
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            pl.offset = 5.0;
            pl.sheet.fills.push_back(fill_of(V{2.0, 5.0, 8.0}, V{2.0, 2.0, 2.0}, V{8.0, 8.0, 8.0}));
            ps.planes.push_back(pl);
            const Px3 in = proj.project(6.0, 5.0, 5.0);
            const auto hit = find_pick3d(ps, proj, in.x, in.y);
            check(hit && hit->kind == PickKind::Fill && hit->plane == 0 && hit->element == 1 &&
                      std::abs(hit->data_x - 6.0) < 1e-3 && std::abs(hit->data_y - 5.0) < 1e-3,
                  "plane: a fill on a plane, its segment and the plane's own coordinates");
            check(hit && format_pick3d(ps, *hit) == find_hint3d(ps, proj, in.x, in.y)->text,
                  "plane: the hint is the pick's text");
            const Px3 out = proj.project(9.0, 5.0, 5.0);
            check(!find_pick3d(ps, proj, out.x, out.y), "plane: past the fill's end, nothing");
        }

        // ---- The Pick event: kind, segment, position; the figure's radius.
        {
            auto layout_of = [](const FigureSnapshot& fs) {
                const FigureLayout fl = compute_figure_layout(fs, 600, 400);
                std::vector<AxesLayout> layout;
                for (const CellLayout& c : fl.cells) layout.push_back({c.slot, c.tr, std::nullopt, c.cell});
                return layout;
            };
            auto press = [](const FigureSnapshot& fs, const std::vector<AxesLayout>& layout, float x, float y) {
                PlotEventTracker t;
                PlotInputFrame in;
                in.x = x;
                in.y = y;
                in.hovered = true;
                in.down[0] = true;
                std::vector<Event> out;
                HintIndexCache index;
                collect_plot_events(t, in, {}, fs, layout, event_bit(EventKind::Pick), out, &index);
                return out.empty() ? std::optional<Event>() : std::optional<Event>(out[0]);
            };

            RenderSnapshot rs = snap_0_10();
            rs.fills.push_back(fill_of(V{0.0, 4.0, 10.0}, V{0.0, 0.0, 0.0}, V{6.0, 6.0, 6.0}));
            rs.fills.back().id = 7;
            ScatterPlot sp;
            sp.x = V{8.0};
            sp.y = V{8.0};
            rs.scatters.push_back(sp);
            FigureSnapshot fs;
            fs.axes.push_back({AxesSlot{1, 1, 1}, std::move(rs)});
            fs.generation = fs.data_generation = 1;
            const auto layout = layout_of(fs);
            const CoordTransform& tr = layout[0].tr;

            const auto p = press(fs, layout, tr.to_px(6.0), tr.to_py(2.0));
            check(p && p->pick_kind == PickKind::Fill && p->pick_object == 7 && p->pick_index == 1 &&
                      p->pick_row == -1 && p->pick_col == -1 && p->has_data && std::abs(p->xdata - 6.0) < 0.05 &&
                      std::abs(p->ydata - 2.0) < 0.05,
                  "event: a press inside a fill is a Fill pick, its segment, at xdata/ydata");

            const float off = tr.to_px(8.0) + 20.0f;
            check(!press(fs, layout, off, tr.to_py(8.0)), "event: 20 px from the point, nothing at 12");
            fs.pick_radius = 25.0f;
            const auto far = press(fs, layout, off, tr.to_py(8.0));
            check(far && far->pick_kind == PickKind::Scatter, "event: the figure's radius reaches it");
        }

        // ---- The API: FigureOptions::pick_radius and set_pick_radius().
        {
            const auto fp = Figure::create();
            Figure& f = *fp;
            check(f.pick_radius() == 12.0f &&
                      detail::FigureAccess::impl(f).build_figure_snapshot().pick_radius == 12.0f,
                  "api: 12 by default");
            f.set_pick_radius(30.0f);
            check(f.pick_radius() == 30.0f && detail::FigureAccess::impl(f).build_figure_snapshot().pick_radius == 30.0f,
                  "api: set_pick_radius() reaches the snapshot");
            f.set_pick_radius(0.0f);
            check(f.pick_radius() == 0.0f, "api: 0 is allowed");
            check(throws_invalid([&] { f.set_pick_radius(-1.0f); }) &&
                      throws_invalid([&] { f.set_pick_radius(std::numeric_limits<float>::quiet_NaN()); }) &&
                      throws_invalid([&] { f.set_pick_radius(std::numeric_limits<float>::infinity()); }) &&
                      f.pick_radius() == 0.0f,
                  "api: a negative or non-finite radius throws and changes nothing");
            FigureOptions o;
            o.pick_radius = 20.0f;
            check(Figure::create(o)->pick_radius() == 20.0f, "api: FigureOptions::pick_radius");
            o.pick_radius = -2.0f;
            check(throws_invalid([&] { (void)Figure::create(o); }), "api: a negative FigureOptions::pick_radius throws");
        }
    }
} // namespace lt
