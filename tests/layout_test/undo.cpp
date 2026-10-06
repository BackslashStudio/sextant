// Undo-capable apply (GUI-kit R10): every apply records what it replaced, an
// inverse of the same type, and applying that inverse puts the figure back --
// on the snapshot and, through the journal, on the live axes.
#include "layout_test.h"
#include "figure_impl.h"
#include "figure_export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lt {
    using namespace sextant;

    namespace {
        using AxesImpl   = detail::FigureAccess::Impl::AxesImpl;
        using Axes3DImpl = detail::FigureAccess::Impl::Axes3DImpl;

        const std::vector<double> kX{ 0.0, 1.0, 2.0 };
        const std::vector<double> kY{ 1.0, 3.0, 2.0 };

        LineOptions width(float w) {
            LineOptions o;
            o.linewidth = w;
            return o;
        }

        template <class T>
        std::vector<T> values(const CowVec<T>& c) {
            std::vector<T> out;
            for (std::size_t i = 0; i < c.size(); ++i) out.push_back(c[i]);
            return out;
        }

        // What the figure draws: equal SVGs = equal pictures.
        std::string svg(const std::shared_ptr<const FigureSnapshot>& s) {
            return render_figure_svg(*s, 640, 360);
        }

        // A 2D cell with every 2D kind, error bars and hint labels, and a 3D
        // cell with grid kinds (bar3d with bottoms), a scatter3d and a plane.
        struct Scene {
            std::shared_ptr<Figure> fig = Figure::create();
            std::shared_ptr<Axes>   ax;
            std::shared_ptr<Axes3D> ax3;
            std::shared_ptr<Plane2D> plane;
            detail::FigureAccess::Impl* fi = nullptr;

            Scene() {
                ax  = fig->add_subplot(1, 2, 1);
                ax3 = fig->add_subplot3d(1, 2, 2);
                const std::vector<double> lo{ 0.1, 0.2, 0.3 };
                LineOptions lo_opts;
                lo_opts.hint_labels = { "a", "b", "c" };
                ax->line(kX, kY, ErrorBar{ .y_cap_lo = lo }, lo_opts);
                ax->scatter(kX, kY).bar(kX, kY);
                HeatmapOptions h;
                h.hint_labels = { "h0" };   // shorter than the grid: padded by line ops
                const std::vector<double> grid{ 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 };
                ax->heatmap(grid, 2, 3, {0.0, 3.0}, {0.0, 2.0}, h);
                const std::vector<double> u{ 0.0, 1.0, 2.0 }, v{ 0.0, 1.0 };
                const std::vector<double> hb{ 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 };
                const std::vector<double> bt{ 0.0, 0.5, 0.0, 0.5, 0.0, 0.5 };
                ax3->bar3d(PlaneOrientation::XY, u, v, hb, bt);
                ax3->surface(PlaneOrientation::XY, u, v, hb);
                ax3->scatter3d(kX, kY, kY);
                plane = ax3->plane(PlaneOrientation::XY, 0.5);
                plane->line(kX, kY);
                fi = &detail::FigureAccess::impl(*fig);
            }
            AxesImpl*   a2() { return fi->find_slot_impl(1); }
            Axes3DImpl* a3() { return fi->find_slot_impl3d(2); }
            std::shared_ptr<const FigureSnapshot> snap() { return fi->snapshot_box.load(); }
        };
    } // namespace

    // Panel edits in every 2D lane and the figure's, drained by host_frame():
    // the inverse puts the picture back, its own inverse re-does it, and both
    // reach the live axes through the journal.
    static void round_trip_2d() {
        Scene sc;
        auto s0 = sc.fi->host_frame();
        const std::string before = svg(s0);

        sc.fi->edit_box.update(1, [](AxesEdit& e) {
            e.title = "edited";
            e.xmin = -1.0;
            e.xmax = 5.0;
            e.xlim_auto = false;
            e.grid_enabled = true;
            GridOptions g;
            g.linewidth = 3.0f;
            e.grid_opts = g;
            e.legend_enabled = true;
            e.xticks_override = std::vector<Tick>{ {0.5, "half"} };
            e.plot_styles.push_back({0, -1, width(6.0f)});
            e.plot_ops.push_back(PlotCellEdit{PlotKind::Line, 0, 1, 2, 9.0});
            e.plot_ops.push_back(PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Line, 0, 1});
            e.plot_ops.push_back(PlotRowEdit{PlotRowEdit::Op::Remove, PlotKind::Scatter, 0, 0});
            e.plot_ops.push_back(BarWidthEdit{0, 0.25});
            MatrixLineEdit rm;
            rm.op = MatrixLineEdit::Op::Remove;
            rm.index = 0;
            e.plot_ops.push_back(rm);
            MatrixLineEdit add;
            add.axis = MatrixLineEdit::Axis::Col;
            add.index = 3;
            e.plot_ops.push_back(add);
        });
        sc.fi->edit_box.update_figure([](FigureEdits& f) {
            f.suptitle = "sup";
            FigureMargins m;
            m.left = 40.0f;
            f.margins = m;
            f.col_ratios = std::vector<float>{ 2.0f, 1.0f };
        });
        FigureEdits inv;
        auto s1 = sc.fi->host_frame(&inv);
        const std::string edited = svg(s1);
        check(edited != before, "2D: the edits change the picture");
        check(inv.per_axes.size() == 1 && inv.per_axes[0].second.plot_ops.size() == 6
              && inv.suptitle && inv.margins && inv.col_ratios,
              "2D: one inverse per lane written, every data op with its own");
        const AxesEdit& ie = inv.per_axes[0].second;
        check(ie.title == std::optional<std::string>("") && ie.xlim_auto == std::optional(true)
              && ie.xticks_override && ie.xticks_override->empty()
              && ie.plot_styles.size() == 1
              && std::get<LineOptions>(ie.plot_styles[0].opts).linewidth == 1.5f,
              "2D: the inverse holds the old values (auto ticks = an empty override)");
        check(std::holds_alternative<MatrixLineEdit>(ie.plot_ops[0])
              && std::get<MatrixLineEdit>(ie.plot_ops[0]).op == MatrixLineEdit::Op::Remove,
              "2D: data ops undo last-first");

        const FigureEdits redo = sc.fi->apply_edits(inv);
        auto s2 = sc.snap();
        check(svg(s2) == before, "2D: applying the inverse restores the picture");
        const RenderSnapshot& r2 = *s2->axes[0].snap2d();
        const RenderSnapshot& r0 = *s0->axes[0].snap2d();
        check(values(r2.lines[0].x) == values(r0.lines[0].x)
              && values(r2.lines[0].y) == values(r0.lines[0].y)
              && values(r2.lines[0].err.y_cap_lo) == values(r0.lines[0].err.y_cap_lo)
              && r2.lines[0].opts.hint_labels == r0.lines[0].opts.hint_labels,
              "2D: the line's data, error bars and labels are back exactly");
        check(values(r2.scatters[0].x) == values(r0.scatters[0].x)
              && values(r2.heatmaps[0].data) == values(r0.heatmaps[0].data)
              && r2.heatmaps[0].rows == 2 && r2.heatmaps[0].cols == 3
              && r2.heatmaps[0].opts.hint_labels == r0.heatmaps[0].opts.hint_labels
              && r2.bars[0].bar_width == r0.bars[0].bar_width,
              "2D: a removed point, the heatmap (labels unpadded) and the bar width are back");
        check(r2.title.empty() && r2.xlim_auto && !r2.xticks_override && s2->suptitle.empty()
              && s2->col_ratios.empty(),
              "2D: titles, limits, ticks, suptitle and ratios are back");

        sc.fi->apply_edits(redo);
        check(svg(sc.snap()) == edited, "2D: the inverse's inverse re-does the edits");

        // Through the journal: after a publish the live axes draw the same.
        sc.fi->apply_edits_and_publish();
        check(svg(sc.snap()) == edited, "2D: the journal replays the edit, undo and redo onto the live axes");
        check(sc.fi->apply_edits(FigureEdits{}).empty(), "2D: an empty edit has an empty inverse");
        FigureEdits idle;
        sc.fi->host_frame(&idle);
        check(idle.empty(), "2D: a frame that drains nothing has an empty inverse");
    }

    // The 3D lanes: camera, box, aspect, a plane's placement, its sheet's
    // styles and ops, the axes' own objects and grid-line ops.
    static void round_trip_3d() {
        Scene sc;
        auto s0 = sc.fi->host_frame();
        const std::string before = svg(s0);
        sc.fi->edit_box.update3d(2, [](AxesEdit3D& e) {
            Camera3D c;
            c.azimuth = 10.0;
            c.elevation = 60.0;
            e.camera = c;
            Box3DStyle b;
            b.panes = false;
            e.box_style = b;
            e.aspect = BoxAspect{ 2.0, 1.0, 1.0 };
            e.ztitle = "z";
            Plane2DOptions po;
            po.alpha = 0.5f;
            e.planes.push_back({0, std::nullopt, 0.9, po});
            e.plot_styles.push_back({0, 0, width(4.0f)});
            PlotRowEdit pr{PlotRowEdit::Op::Remove, PlotKind::Line, 0, 2, 0};
            e.plot_ops.push_back(pr);
            Scatter3DOptions big;
            big.size = 20.0f;
            e.scatter3d.push_back({0, big});
            e.plot_ops.push_back(PlotCellEdit{PlotKind::Scatter3D, 0, 2, 1, 7.0});
            MatrixLineEdit rm;
            rm.op = MatrixLineEdit::Op::Remove;
            rm.index = 1;
            rm.kind = PlotKind::Bar3D;
            e.plot_ops.push_back(rm);
            MatrixLineEdit add;
            add.axis = MatrixLineEdit::Axis::Col;
            add.index = 0;
            add.kind = PlotKind::Surface;
            e.plot_ops.push_back(add);
            e.plot_ops.push_back(BarWidthEdit{0, 0.3, -1, PlotKind::Bar3D, 1});
        });
        FigureEdits inv;
        auto s1 = sc.fi->host_frame(&inv);
        const std::string edited = svg(s1);
        check(edited != before && inv.per_axes3d.size() == 1
              && inv.per_axes3d[0].second.camera && inv.per_axes3d[0].second.planes.size() == 1
              && inv.per_axes3d[0].second.plot_ops.size() == 5,
              "3D: the edits change the picture and every lane has its inverse");
        const FigureEdits redo = sc.fi->apply_edits(inv);
        auto s2 = sc.snap();
        const RenderSnapshot3D& r2 = *s2->axes[1].snap3d();
        const RenderSnapshot3D& r0 = *s0->axes[1].snap3d();
        check(svg(s2) == before, "3D: applying the inverse restores the picture");
        check(values(r2.bars3d[0].u) == values(r0.bars3d[0].u)
              && values(r2.bars3d[0].heights) == values(r0.bars3d[0].heights)
              && values(r2.bars3d[0].bottoms) == values(r0.bars3d[0].bottoms)
              && r2.bars3d[0].v_width == r0.bars3d[0].v_width
              && values(r2.surfaces[0].v) == values(r0.surfaces[0].v)
              && values(r2.planes[0].sheet.lines[0].x) == values(r0.planes[0].sheet.lines[0].x)
              && r2.planes[0].offset == r0.planes[0].offset && r2.camera.azimuth == -60.0,
              "3D: grid lines (coordinates, bottoms), the plane's data and placement, the camera are back");
        sc.fi->apply_edits(redo);
        check(svg(sc.snap()) == edited, "3D: the inverse's inverse re-does the edits");
        sc.fi->apply_edits_and_publish();
        check(svg(sc.snap()) == edited, "3D: the journal replays all of it onto the live axes");
    }

    // A field a newer setter protects, and an edit whose object is gone, leave
    // nothing in the inverse.
    static void skipped_and_dropped() {
        Scene sc;
        sc.fi->host_frame();
        sc.ax->grid(true);                   // newer than the snapshot the edit is made over
        sc.fi->apply_edits_and_publish();
        sc.fi->edit_box.update(1, [](AxesEdit& e) {
            e.grid_enabled = false;
            e.plot_styles.push_back({7, -1, width(3.0f)});   // no line 7
        });
        FigureEdits inv;
        sc.fi->host_frame(&inv);
        check(inv.empty(), "skip: a stamp-skipped field and a dropped edit record nothing");
        check(sc.snap()->axes[0].snap2d()->grid_enabled, "skip: and the setter's value stays");
    }

    // Several ops on one plot in one drain undo in reverse; removing the last
    // point empties its optional columns, and the undo fills them again.
    static void op_sequences() {
        RenderSnapshot s;
        s.lines.resize(1);
        LinePlot& l = s.lines[0];
        l.x = CowVec<double>(std::vector<double>{ 1.0, 2.0 });
        l.y = CowVec<double>(std::vector<double>{ 3.0, 4.0 });
        l.err.y_cap_lo = CowVec<double>(std::vector<double>{ 0.1, 0.2 });
        l.opts.hint_labels = { "p", "q" };
        const RenderSnapshot orig = s;

        std::vector<PlotDataOp> inv;
        apply_plot_data_ops(s, {
            PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Line, 0, 1},
            PlotCellEdit{PlotKind::Line, 0, 1, 1, 8.0},
            PlotRowEdit{PlotRowEdit::Op::Remove, PlotKind::Line, 0, 0},
        }, &inv);
        check(inv.size() == 3 && values(s.lines[0].y) == std::vector<double>{ 8.0, 4.0 },
              "ops: insert, edit the new point, remove the first");
        std::vector<PlotDataOp> redo;
        apply_plot_data_ops(s, inv, &redo);
        check(values(s.lines[0].x) == values(orig.lines[0].x)
              && values(s.lines[0].y) == values(orig.lines[0].y)
              && values(s.lines[0].err.y_cap_lo) == values(orig.lines[0].err.y_cap_lo)
              && s.lines[0].opts.hint_labels == orig.lines[0].opts.hint_labels,
              "ops: the inverse list, run in its order, undoes all three");
        apply_plot_data_ops(s, redo);
        check(values(s.lines[0].y) == std::vector<double>{ 8.0, 4.0 }, "ops: and the redo list re-does them");

        RenderSnapshot one;
        one.lines.resize(1);
        one.lines[0].x = CowVec<double>(std::vector<double>{ 5.0 });
        one.lines[0].y = CowVec<double>(std::vector<double>{ 6.0 });
        one.lines[0].err.y_cap_lo = CowVec<double>(std::vector<double>{ 0.5 });
        one.lines[0].opts.hint_labels = { "only" };
        std::vector<PlotDataOp> back;
        apply_plot_data_ops(one, {PlotRowEdit{PlotRowEdit::Op::Remove, PlotKind::Line, 0, 0}}, &back);
        check(one.lines[0].x.empty() && one.lines[0].opts.hint_labels.empty()
              && one.lines[0].err.y_cap_lo.empty(), "last point: removing it empties every column");
        apply_plot_data_ops(one, back);
        check(values(one.lines[0].x) == std::vector<double>{ 5.0 }
              && values(one.lines[0].err.y_cap_lo) == std::vector<double>{ 0.5 }
              && one.lines[0].opts.hint_labels == std::vector<std::string>{ "only" },
              "last point: the undo puts the point, its error bar and its label back");

        // An op whose data was re-set since its inverse was taken: the undo drops.
        std::vector<PlotDataOp> stale;
        apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Line, 0, 1, 0, 1.0}}, &stale);
        s.lines[0].data_stamp = next_snapshot_generation();   // set_line_data() since
        apply_plot_data_ops(s, stale);
        check(s.lines[0].y[0] == 1.0, "stale: an undo over newer data drops");
    }

    // Undo addresses its object by id, so it lands after a reorder.
    static void undo_after_reorder() {
        auto fig = Figure::create();
        auto ax = fig->add_subplot(1, 1, 1);
        ax->line(kY).line(kY);
        auto& fi = detail::FigureAccess::impl(*fig);
        AxesImpl* a2 = fi.find_slot_impl(1);
        const ObjectId b = a2->lines[1].id;
        fi.host_frame();
        fi.edit_box.update(1, [](AxesEdit& e) { e.plot_styles.push_back({1, -1, width(8.0f)}); });
        FigureEdits inv;
        fi.host_frame(&inv);
        fi.apply_edits_and_publish();
        check(a2->move_object(b, 0) == std::optional<std::size_t>(1) && a2->lines[0].id == b,
              "reorder: move_object() returns where the object was");
        fi.apply_edits_and_publish();
        fi.host_frame();
        fi.apply_edits(inv);
        fi.apply_edits_and_publish();
        check(a2->lines[0].id == b && a2->lines[0].opts.linewidth == 1.5f
              && a2->lines[1].opts.linewidth == 1.5f,
              "reorder: the undo finds its object at its new index");
    }

    // remove_object()/remove_plane() hand back what restore_*() puts back,
    // ids included.
    static void restore_removed() {
        Scene sc;
        AxesImpl* a2 = sc.a2();
        Axes3DImpl* a3 = sc.a3();
        const ObjectId line = a2->lines[0].id;
        const ObjectId scat3 = a3->scatter3d[0].id;
        const ObjectId pl = a3->plane_at(0).id;
        sc.fi->apply_edits_and_publish();
        const std::string before = svg(sc.snap());

        auto rl = a2->remove_object(line);
        auto rs = a3->remove_object(scat3);
        auto rp = a3->remove_plane(pl);
        check(rl && rl->index == 0 && rs && rp && rp->index == 0 && a2->lines.empty()
              && a3->plane_count() == 0, "restore: removal hands back the objects and the plane");
        sc.fi->apply_edits_and_publish();
        check(svg(sc.snap()) != before, "restore: the removal changes the picture");

        check(a2->restore_object(*rl) && a3->restore_object(*rs) && a3->restore_plane(*rp),
              "restore: each goes back");
        check(!a2->restore_object(*rl) && !a3->restore_plane(*rp),
              "restore: twice is refused (the id is there already)");
        sc.fi->apply_edits_and_publish();
        check(svg(sc.snap()) == before && a2->lines[0].id == line && a3->scatter3d[0].id == scat3
              && a3->plane_at(0).id == pl, "restore: the picture and the ids are back");

        // The plane's handle is attached again, and edits find the restored ids.
        sc.plane->line(kY);
        sc.fi->host_frame();
        sc.fi->edit_box.update(1, [](AxesEdit& e) { e.plot_styles.push_back({0, -1, width(9.0f)}); });
        sc.fi->host_frame();
        sc.fi->apply_edits_and_publish();
        check(a3->plane_at(0).sheet.lines.size() == 2 && a2->lines[0].opts.linewidth == 9.0f,
              "restore: the plane's handle draws again, and an edit reaches the restored line");
    }

    // A gesture over three frames, folded with compose_inverse(), undoes in one.
    static void composed_gesture() {
        Scene sc;
        auto s0 = sc.fi->host_frame();
        const std::string before = svg(s0);
        FigureEdits gesture;
        const float widths[] = { 2.0f, 3.0f, 4.0f };
        for (int f = 0; f < 3; ++f) {
            sc.fi->edit_box.update(1, [&](AxesEdit& e) {
                e.plot_styles.push_back({0, -1, width(widths[f])});
                e.xmin = -1.0 - f;
                e.xlim_auto = false;
                if (f == 0) e.plot_ops.push_back(PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Line, 0, 3});
                if (f == 1) e.plot_ops.push_back(PlotCellEdit{PlotKind::Line, 0, 1, 3, 9.0});
            });
            FigureEdits frame;
            sc.fi->host_frame(&frame);
            compose_inverse(gesture, frame);
        }
        check(gesture.per_axes.size() == 1 && gesture.per_axes[0].second.plot_styles.size() == 1
              && gesture.per_axes[0].second.plot_ops.size() == 2,
              "compose: one entry per object, every data op kept");
        sc.fi->apply_edits(gesture);
        check(svg(sc.snap()) == before, "compose: the gesture's inverse undoes all three frames");
    }

    void test_undo() {
        std::printf("\n[undo: inverses of every edit (GUI-kit R10)]\n");
        round_trip_2d();
        round_trip_3d();
        skipped_and_dropped();
        op_sequences();
        undo_after_reorder();
        restore_removed();
        composed_gesture();
    }
} // namespace lt
