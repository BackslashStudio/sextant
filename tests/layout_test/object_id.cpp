// Plot-object identity: every plot object and plane has an
// ObjectId; edits carry it beside their index, so an edit made over one order
// of objects lands on its own object after the caller removed or reordered
// others -- or drops with it.
#include "layout_test.h"
#include "figure_impl.h"
#include "widgets/figure_context.h"
#include "widgets/panel_state.h"

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace lt {
    using namespace sextant;

    namespace {
        using AxesImpl   = detail::FigureAccess::Impl::AxesImpl;
        using Axes3DImpl = detail::FigureAccess::Impl::Axes3DImpl;

        const std::vector<double> kX{ 0.0, 1.0, 2.0 };
        const std::vector<double> kY{ 1.0, 2.0, 3.0 };

        LineOptions width(float w) {
            LineOptions o;
            o.linewidth = w;
            return o;
        }
    } // namespace

    // Assignment: at every ingest, kept by set_*_data(), gone with cla().
    static void ids_assigned() {
        auto fig = Figure::create();
        auto ax  = fig->add_subplot(1, 2, 1);
        auto ax3 = fig->add_subplot3d(1, 2, 2);
        const std::vector<double> grid{ 1.0, 2.0, 3.0, 4.0 };
        ax->line(kX, kY).line(kY).scatter(kX, kY).scatter_z(kX, kY, kY).bar(kX, kY)
          .hist(kY).heatmap(grid, 2, 2, {0.0, 1.0}, {0.0, 1.0});
        const std::vector<double> u{ 0.0, 1.0 };
        const std::vector<std::uint32_t> tri{ 0, 1, 2 };
        ax3->bar3d(PlaneOrientation::XY, u, u, grid).surface(PlaneOrientation::XY, u, u, grid)
            .scatter3d(kX, kY, kY).line3d(kX, kY, kY).surface_tri(kX, kY, kY, tri);
        auto p0 = ax3->plane(PlaneOrientation::XY, 0.0);
        auto p1 = ax3->plane(PlaneOrientation::YZ, 0.5);
        p0->line(kX, kY);
        p1->scatter(kX, kY);

        auto& fi = detail::FigureAccess::impl(*fig);
        AxesImpl* a2 = fi.find_slot_impl(1);
        Axes3DImpl* a3 = fi.find_slot_impl3d(2);

        std::set<ObjectId> seen;
        bool all = true;
        auto take = [&](ObjectId id) {
            if (id == 0 || id == kNoObject || !seen.insert(id).second) all = false;
        };
        for (PlotKind k : kPlotKinds2D)
            with_kind(*a2, k, [&](const auto& v) { for (const auto& p : v) take(p.id); return 0; }, 0);
        for (PlotKind k : kPlotKinds3D)
            with_kind(*a3, k, [&](const auto& v) { for (const auto& p : v) take(p.id); return 0; }, 0);
        for (std::size_t i = 0; i < a3->plane_count(); ++i) {
            take(a3->plane_at(i).id);
            for (const auto& p : a3->plane_at(i).sheet.lines) take(p.id);
            for (const auto& p : a3->plane_at(i).sheet.scatters) take(p.id);
        }
        check(all && seen.size() == 16,
              "ids: every 2D and 3D object (hist and heatmap included), plane and plane object has its own");

        const ObjectId line0 = a2->lines[0].id;
        const auto stamp0 = a2->lines[0].data_stamp;
        ax->set_line_data(0, kY, kX);
        check(a2->lines[0].id == line0 && a2->lines[0].data_stamp != stamp0,
              "ids: set_*_data() keeps the id and renews the data stamp");

        fi.apply_edits_and_publish();
        auto snap = fi.snapshot_box.load();
        const RenderSnapshot3D* s3 = snap->axes[1].snap3d();
        check(snap->axes[0].snap2d()->lines[0].id == line0 && s3
              && s3->planes[1].id == a3->plane_at(1).id
              && s3->planes[1].sheet.scatters[0].id == a3->plane_at(1).sheet.scatters[0].id,
              "ids: the snapshot carries them, planes and their objects included");

        const ObjectId plane0 = a3->plane_at(0).id;
        ax->cla();
        ax3->cla();
        ax->line(kY);
        ax3->plane(PlaneOrientation::XY, 0.0);
        check(a2->lines.size() == 1 && seen.count(a2->lines[0].id) == 0
              && a3->plane_at(0).id != plane0 && seen.count(a3->plane_at(0).id) == 0,
              "ids: after cla(), new objects and planes get new ids, never reused");
    }

    // find / remove / move on both Impls.
    static void remove_and_move() {
        auto fig = Figure::create();
        auto ax  = fig->add_subplot(1, 2, 1);
        auto ax3 = fig->add_subplot3d(1, 2, 2);
        ax->line(kY).line(kY).line(kY).scatter(kX, kY);
        ax3->scatter3d(kX, kY, kY).scatter3d(kX, kY, kY);
        ax3->plane(PlaneOrientation::XY, 0.0);
        ax3->plane(PlaneOrientation::XY, 1.0);
        ax3->plane(PlaneOrientation::XY, 2.0);
        auto& fi = detail::FigureAccess::impl(*fig);
        AxesImpl* a2 = fi.find_slot_impl(1);
        Axes3DImpl* a3 = fi.find_slot_impl3d(2);

        const ObjectId l0 = a2->lines[0].id, l1 = a2->lines[1].id, l2 = a2->lines[2].id;
        const ObjectId sc = a2->scatters[0].id;
        auto ref = a2->find_object(l2);
        check(ref && ref->kind == PlotKind::Line && ref->index == 2
              && a2->find_object(sc)->kind == PlotKind::Scatter,
              "find_object: the kind and index of an id");
        check(!a2->find_object(0) && !a2->find_object(kNoObject) && !a2->find_object(a3->scatter3d[0].id),
              "find_object: none for 0, kNoObject, or an object of another axes");
        check(object_id_at(*a2, PlotKind::Line, 1) == l1 && object_id_at(*a2, PlotKind::Line, 3) == kNoObject
              && object_id_at(*a2, PlotKind::Bar3D, 0) == kNoObject,
              "object_id_at: the id at an index, kNoObject past the end or for a kind the axes lacks");

        check(a2->move_object(l2, 0) && a2->lines[0].id == l2 && a2->lines[1].id == l0
              && a2->lines[2].id == l1, "move_object: to the front, the others keep their order");
        check(a2->move_object(l2, 99) && a2->lines[2].id == l2 && a2->lines[0].id == l0,
              "move_object: past the end is clamped to the last");
        check(a2->remove_object(l1) && a2->lines.size() == 2 && a2->lines[0].id == l0
              && a2->lines[1].id == l2 && a2->scatters.size() == 1,
              "remove_object: only that object goes");
        check(!a2->remove_object(l1) && !a2->move_object(l1, 0) && a2->lines.size() == 2,
              "remove/move: false for an id no longer there");

        const ObjectId s0 = a3->scatter3d[0].id, s1 = a3->scatter3d[1].id;
        check(a3->find_object(s1)->index == 1 && a3->move_object(s1, 0) && a3->scatter3d[0].id == s1
              && a3->remove_object(s0) && a3->scatter3d.size() == 1,
              "3D: find, move and remove its own objects");
        const ObjectId p0 = a3->plane_at(0).id, p1 = a3->plane_at(1).id, p2 = a3->plane_at(2).id;
        auto handle = ax3->plane_at(0);
        check(a3->find_plane(p2) == 2u && a3->move_plane(p2, 0) && a3->plane_at(0).id == p2
              && a3->plane_at(1).id == p0 && a3->remove_plane(p0) && a3->plane_count() == 2
              && a3->plane_at(1).id == p1 && !a3->remove_plane(p0),
              "3D: find, move and remove planes");
        handle->line(kY);
        check(a3->plane_at(0).sheet.lines.empty() && a3->plane_at(1).sheet.lines.empty(),
              "3D: a removed plane's handle keeps working, detached");
    }

    // The journal replays panel edits after the caller removed or reordered
    // objects: each lands on its own object, on both sides of the publish.
    static void edits_follow_objects() {
        auto fig = Figure::create();
        auto ax  = fig->add_subplot(1, 1, 1);
        ax->line(kY).line(kY).line(kY);
        auto& fi = detail::FigureAccess::impl(*fig);
        AxesImpl* a2 = fi.find_slot_impl(1);
        const ObjectId a = a2->lines[0].id, b = a2->lines[1].id, c = a2->lines[2].id;

        // Over the drawn order A B C: B's data and style, C's style, and A's.
        auto s = fi.host_frame();
        const auto b_stamp = s->axes[0].snap2d()->lines[1].data_stamp;
        fi.edit_box.update(1, [&](AxesEdit& e) {
            PlotCellEdit cell{PlotKind::Line, 1, 1, 0, 9.0};
            cell.seen = b_stamp;
            e.plot_ops.push_back(cell);
            e.plot_styles.push_back({1, -1, width(7.0f)});
            e.plot_styles.push_back({2, -1, width(5.0f)});
            e.plot_styles.push_back({0, -1, width(3.0f)});
        });
        auto patched = fi.host_frame();
        const auto& pl = patched->axes[0].snap2d()->lines;
        check(pl[1].y[0] == 9.0 && pl[1].opts.linewidth == 7.0f && pl[2].opts.linewidth == 5.0f,
              "follow: the render-side patch lands as before (nothing has moved yet)");

        // The caller removes A and moves C to the front: C B.
        a2->remove_object(a);
        a2->move_object(c, 0);
        fi.apply_edits_and_publish();
        check(a2->lines.size() == 2 && a2->lines[0].id == c && a2->lines[1].id == b,
              "follow: the caller's order is C B");
        check(a2->lines[1].y[0] == 9.0 && a2->lines[0].y[0] == 1.0,
              "follow: B's data op reaches B, now at index 1");
        check(a2->lines[1].opts.linewidth == 7.0f && a2->lines[0].opts.linewidth == 5.0f,
              "follow: B's and C's style edits reach them at their new indices; A's dropped with A");
        auto after = fi.host_frame();
        check(after->axes[0].snap2d()->lines[0].id == c && after->axes[0].snap2d()->lines[1].opts.linewidth == 7.0f,
              "follow: the rebuilt snapshot draws the new order");

        // An edit over the new order, on the object then removed: dropped.
        fi.edit_box.update(1, [](AxesEdit& e) { e.plot_styles.push_back({0, -1, width(9.0f)}); });
        fi.host_frame();
        a2->remove_object(c);
        fi.apply_edits_and_publish();
        check(a2->lines.size() == 1 && a2->lines[0].opts.linewidth == 7.0f,
              "follow: an edit on a removed object drops, not landing on the one now at its index");
    }

    static void edits_follow_3d() {
        auto fig = Figure::create();
        auto ax3 = fig->add_subplot3d(1, 1, 1);
        ax3->scatter3d(kX, kY, kY).scatter3d(kX, kY, kY);
        auto p0 = ax3->plane(PlaneOrientation::XY, 0.0);
        auto p1 = ax3->plane(PlaneOrientation::XY, 1.0);
        p0->line(kX, kY);
        p1->line(kX, kY);
        auto& fi = detail::FigureAccess::impl(*fig);
        Axes3DImpl* a3 = fi.find_slot_impl3d(1);
        const ObjectId s0 = a3->scatter3d[0].id;
        const ObjectId pl0 = a3->plane_at(0).id;

        auto s = fi.host_frame();
        const auto line_stamp = s->axes[0].snap3d()->planes[1].sheet.lines[0].data_stamp;
        fi.edit_box.update3d(1, [&](AxesEdit3D& e) {
            Scatter3DOptions big;
            big.size = 21.0f;
            e.scatter3d.push_back({1, big});
            e.planes.push_back({1, std::nullopt, 0.7, std::nullopt});
            e.plot_styles.push_back({0, 1, width(5.0f)});
            PlotCellEdit cell{PlotKind::Line, 0, 1, 0, 8.0, 1};
            cell.seen = line_stamp;
            e.plot_ops.push_back(cell);
        });
        fi.host_frame();
        a3->remove_object(s0);
        a3->remove_plane(pl0);
        fi.apply_edits_and_publish();
        const auto& plane = a3->plane_at(0);
        check(a3->scatter3d.size() == 1 && a3->scatter3d[0].opts.size == 21.0f,
              "follow 3D: an object edit reaches its object after one before it is removed");
        check(a3->plane_count() == 1 && plane.offset == 0.7,
              "follow 3D: a plane edit reaches its plane after one before it is removed");
        check(plane.sheet.lines[0].opts.linewidth == 5.0f && plane.sheet.lines[0].y[0] == 8.0,
              "follow 3D: a style edit and a data op on that plane's sheet reach it too");
    }

    // The journal merges two edits of one object made at different indices.
    static void merge_by_id() {
        AxesEdit dst, src;
        PlotStyleEdit first{2, -1, width(2.0f)};
        first.id = 40;
        PlotStyleEdit second{0, -1, width(6.0f)};
        second.id = 40;
        PlotStyleEdit other{2, -1, width(1.0f)};
        other.id = 41;
        src.plot_styles = {first};
        merge_style_edits(dst, src);
        src.plot_styles = {second, other};
        merge_style_edits(dst, src);
        check(dst.plot_styles.size() == 2 && dst.plot_styles[0].id == 40
              && dst.plot_styles[0].plot_index == 0
              && std::get<LineOptions>(dst.plot_styles[0].opts).linewidth == 6.0f,
              "merge: one entry per object id, at its latest index and value");
    }

    // The Data panel's scratch re-seeds when the objects change, even at an
    // unchanged count.
    static void scratch_follows_ids() {
        RenderSnapshot sn;
        sn.lines.resize(2);
        sn.lines[0].id = 1;
        sn.lines[0].opts.linewidth = 1.0f;
        sn.lines[1].id = 2;
        sn.lines[1].opts.linewidth = 2.0f;
        FigureAxesSnapshot fa{{1, 1, 1}, sn};
        Selection sel;
        DataPanelState d;
        pull_data_panel(d, sel, fa);
        d.sheet_local.lines[1].linewidth = 4.0f;   // a scratch value being edited
        pull_data_panel(d, sel, fa);
        check(d.sheet_local.lines[1].linewidth == 4.0f, "scratch: the same objects keep it");

        // Line 1 removed and line 3 added: still two lines.
        RenderSnapshot& s2 = *fa.snap2d();
        s2.lines[0] = s2.lines[1];
        s2.lines[1].id = 3;
        s2.lines[1].opts.linewidth = 3.0f;
        pull_data_panel(d, sel, fa);
        check(d.sheet_local.lines[0].linewidth == 2.0f && d.sheet_local.lines[1].linewidth == 3.0f,
              "scratch: a remove and an add at the same count re-seed it");

        RenderSnapshot3D s3;
        s3.planes.resize(2);
        s3.planes[0].id = 7;
        s3.planes[0].offset = 0.0;
        s3.planes[1].id = 8;
        s3.planes[1].offset = 1.0;
        s3.scatter3d.resize(2);
        s3.scatter3d[0].id = 11;
        s3.scatter3d[1].id = 12;
        s3.scatter3d[1].opts.size = 5.0f;
        FigureAxesSnapshot fa3{{1, 1, 1}, s3};
        Selection sel3;
        DataPanelState d3;
        pull_data_panel(d3, sel3, fa3);
        std::swap(fa3.snap3d()->planes[0], fa3.snap3d()->planes[1]);
        std::swap(fa3.snap3d()->scatter3d[0], fa3.snap3d()->scatter3d[1]);
        pull_data_panel(d3, sel3, fa3);
        check(d3.planes_local[0].offset == 1.0 && d3.scatter3d_local[0].size == 5.0f,
              "scratch: reordered planes and 3D objects re-seed it");
    }

    void test_object_ids() {
        std::printf("\n[object ids: identity, removal, reorder]\n");
        ids_assigned();
        remove_and_move();
        edits_follow_objects();
        edits_follow_3d();
        merge_by_id();
        scratch_follows_ids();
    }
} // namespace lt
