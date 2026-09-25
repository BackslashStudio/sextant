// A plane as an address: the edit journal, Data panel, hints, visibility.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {
    // ---------------------------------------------------------------------------
    // A plane is addressable: the panel names a plot object on it, the journal
    // carries the address across threads, and the op lands on that plane only.
    // ---------------------------------------------------------------------------

    void test_plane2d_edit_journal() {
        std::printf("\n[3D: the edit journal addressing a plane]\n");

        using namespace sextant;

        // ---- `plane_index` decides where an op lands: the same op with the other
        // plane's index must change only the other plane.
        {
            RenderSnapshot3D s = two_plane_snapshot();
            apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Line, 0, 1, 2, 99.0, 1}});
            check(s.planes[1].sheet.lines[0].y[2] == 99.0,
                  "journal: a cell edit addressed at plane 1 lands on plane 1");
            check(s.planes[0].sheet.lines[0].y[2] == 2.0,
                  "journal: and does not touch plane 0's line of the same index");

            RenderSnapshot3D t = two_plane_snapshot();
            apply_plot_data_ops(t, {PlotCellEdit{PlotKind::Line, 0, 1, 2, 99.0, 0}});
            check(t.planes[0].sheet.lines[0].y[2] == 99.0 &&
                  t.planes[1].sheet.lines[0].y[2] == 12.0,
                  "journal: (fault injection) the same op at plane 0 moves the other one instead");
        }

        // ---- Every op kind routes.
        {
            RenderSnapshot3D s = two_plane_snapshot();
            apply_plot_data_ops(s, {
                                    PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Line, 0, 1, 1},
                                    BarWidthEdit{0, 0.125, 1},
                                    MatrixLineEdit{MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row, 0, 1, 1},
                                });
            check(s.planes[1].sheet.lines[0].x.size() == 4 &&
                  s.planes[0].sheet.lines[0].x.size() == 3,
                  "journal: a row insert routes to the named plane");
            check(s.planes[1].sheet.bars[0].bar_width == 0.125 &&
                  s.planes[0].sheet.bars[0].bar_width == 0.5,
                  "journal: so does a bar width, which is a plot scalar rather than a cell");
            check(s.planes[1].sheet.heatmaps[0].rows == 3 &&
                  s.planes[0].sheet.heatmaps[0].rows == 2,
                  "journal: and so does a matrix reshape");
        }

        // ---- Ops naming the wrong kind of target are skipped (a 2D axes has no
        // plane 0; a 3D axes has no 2D objects of its own).
        {
            RenderSnapshot snap2d;
            LinePlot lp;
            lp.x = std::vector<double>{0.0, 1.0};
            lp.y = std::vector<double>{5.0, 6.0};
            snap2d.lines.push_back(std::move(lp));
            apply_plot_data_ops(snap2d, {PlotCellEdit{PlotKind::Line, 0, 1, 1, 77.0, 0}});
            check(snap2d.lines[0].y[1] == 6.0,
                  "journal: a plane-addressed op does nothing to a 2D axes");
            apply_plot_data_ops(snap2d, {PlotCellEdit{PlotKind::Line, 0, 1, 1, 77.0, -1}});
            check(snap2d.lines[0].y[1] == 77.0,
                  "journal: (and the same op addressed at the axes still works, so that is "
                  "the address doing it and not the guard)");

            RenderSnapshot3D s = two_plane_snapshot();
            apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Line, 0, 1, 2, 77.0, -1}});
            check(s.planes[0].sheet.lines[0].y[2] == 2.0 &&
                  s.planes[1].sheet.lines[0].y[2] == 12.0,
                  "journal: an axes-addressed op of a 2D kind does nothing to a 3D axes, "
                  "which holds no line of its own");

            // Out of range is skipped, never clamped.
            apply_plot_data_ops(s, {PlotCellEdit{PlotKind::Line, 0, 1, 2, 77.0, 5}});
            check(s.planes[1].sheet.lines[0].y[2] == 12.0,
                  "journal: an out-of-range plane index is skipped, not clamped onto the last");
        }

        // ---- Through FigureEditBox: the drain sees 3D plot_ops and journals them.
        {
            FigureEditBox box;
            box.update3d(3, [](AxesEdit3D& e) {
                e.plot_ops.push_back(PlotCellEdit{PlotKind::Line, 0, 1, 2, 42.0, 1});
            });
            auto drained = box.load_and_clear_journaled();
            check(drained.has_value() && drained->per_axes3d.size() == 1,
                  "journal: a plot-ops-only 3D edit is not mistaken for an empty one");
            check(drained && drained->per_axes3d[0].second.plot_ops.size() == 1,
                  "journal: and arrives on the 3D lane");

            auto j = box.take_journal();
            check(j.has_value() && j->per_axes.size() == 1 && j->per_axes[0].first == 3,
                  "journal: the render thread's drain records it for the caller thread to replay");
            check(j && j->per_axes[0].second.size() == 1 &&
                  plot_op_plane(j->per_axes[0].second[0]) == 1,
                  "journal: with the plane it was addressed to still on it");
            check(!box.take_journal().has_value(), "journal: and the journal drain is destructive");

            // Replaying the journal reproduces what the render thread did.
            RenderSnapshot3D s = two_plane_snapshot();
            apply_plot_data_ops(s, j->per_axes[0].second);
            check(s.planes[1].sheet.lines[0].y[2] == 42.0,
                  "journal: and replaying it lands on the same plane it did the first time");
        }

        // ---- The placement lane: orientation, offset and options.
        {
            FigureEditBox box;
            box.update3d(1, [](AxesEdit3D& e) {
                e.planes.push_back({1, PlaneOrientation::ZX, 0.75, Plane2DOptions{0.5f, false}});
            });
            auto drained = box.load_and_clear();
            check(drained && drained->per_axes3d.size() == 1 &&
                  drained->per_axes3d[0].second.planes.size() == 1,
                  "journal: a placement-only plane edit survives the drain too");
            const auto& pe = drained->per_axes3d[0].second.planes[0];
            check(pe.plane_index == 1 && pe.orient && *pe.orient == PlaneOrientation::ZX &&
                  pe.offset && *pe.offset == 0.75 && pe.opts && !pe.opts->visible,
                  "journal: carrying the plane it names and every field of it");
        }
    }

    // The Data panel's tables for a 3D slot, each carrying its address.
    void test_plane2d_data_tables() {
        std::printf("\n[3D: the Data panel's plane groups]\n");

        using namespace sextant;

        RenderSnapshot3D s = two_plane_snapshot();
        // A bar3d grid first (RenderSnapshot3D member order).
        Bar3DPlot b3;
        b3.orient = PlaneOrientation::XY;
        b3.u = std::vector<double>{0.0, 1.0};
        b3.v = std::vector<double>{0.0, 1.0};
        b3.heights = std::vector<double>{1.0, 2.0, 3.0, 4.0};
        s.bars3d.push_back(std::move(b3));

        const auto tables = collect_plot_data_tables(s);
        check(tables.size() == 7,
              "tables: the axes' own bar3d grid, then three objects on each of two planes");

        // The bar3d grid is on the axes: plane -1.
        check(tables[0].kind == PlotKind::Bar3D && tables[0].plane_index == -1 &&
              tables[0].bars3d == &s.bars3d[0] && tables[0].group.empty(),
              "tables: a bar3d grid is the third shape, addressed at the axes");
        check(tables[0].columns.empty() && tables[0].heatmap == nullptr,
              "tables: and is neither of the other two shapes");

        // Grouped by plane, in order, each row knowing its plane.
        check(tables[1].plane_index == 0 && tables[3].plane_index == 0 &&
              tables[4].plane_index == 1 && tables[6].plane_index == 1,
              "tables: enumerated plane by plane, in the order the planes were added");
        check(tables[1].group == plane_group_label(s.planes[0], 0) &&
              tables[4].group == plane_group_label(s.planes[1], 1),
              "tables: each carrying the same plane label the plane's own tab shows");
        check(tables[1].group.find("XY") != std::string::npos &&
              tables[4].group.find("YZ") != std::string::npos &&
              tables[4].group.find("0.5") != std::string::npos,
              "tables: which names the orientation and the offset");

        // The 2D tables unchanged, aliasing each plane's own sheet.
        check(tables[4].kind == PlotKind::Line && tables[4].plot_index == 0 &&
              tables[4].columns.size() == 2 &&
              tables[4].columns[1].values == s.planes[1].sheet.lines[0].y.data(),
              "tables: a plane's line is the 2D table, pointing into that plane's sheet");
        check(tables[6].heatmap == &s.planes[1].sheet.heatmaps[0],
              "tables: and its heatmap is the 2D grid mode, likewise");

        // A 3D axes with bars and no planes enumerates its bars.
        RenderSnapshot3D bars_only;
        bars_only.bars3d = s.bars3d;
        const auto bars_tables = collect_plot_data_tables(bars_only);
        check(bars_tables.size() == 1 && bars_tables[0].bars3d == &bars_only.bars3d[0],
              "tables: a bar3d grid alone is one table, pointing at that grid");

        // ---- The panel really drawn (null-backend ImGui frame).
        auto panel_vertices = [](const FigureSnapshot& fs) {
            ImGuiContext* ctx = ImGui::CreateContext();
            ImGui::SetCurrentContext(ctx);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(940.0f, 940.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();

            PanelState st;
            FigureEditBox box;
            int n = 0;
            // Three frames: a tab bar selects its tab a frame after first seeing it.
            for (int f = 0; f < 3; ++f) {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(900.0f, 900.0f));
                draw_data_panel(fs, box, st);
                ImGui::Render();
                n = ImGui::GetDrawData()->TotalVtxCount;
            }
            ImGui::DestroyContext(ctx);
            ImGui::SetCurrentContext(nullptr);
            return n;
        };

        auto wrap = [](RenderSnapshot3D r) {
            FigureSnapshot fs;
            fs.axes.push_back({{1, 1, 1}, std::move(r)});
            fs.generation = fs.data_generation = 1;
            return fs;
        };
        const int with_planes = panel_vertices(wrap(s));
        const int bars_alone = panel_vertices(wrap(bars_only));
        RenderSnapshot3D nothing;
        const int empty_axes = panel_vertices(wrap(nothing));
        check(with_planes > bars_alone && bars_alone > empty_axes * 2,
              "tables: the Data panel draws a 3D slot's planes and its bar3d grids "
              "rather than the 'not editable here' lines it used to");
        check(empty_axes > 0,
              "tables: (and an axes with nothing on it still draws its own chrome, "
              "so those are comparisons of three drawn panels)");
    }

    // Hover on planes (a ray cast): it round-trips the forward projection, the
    // nearest plane wins, and hidden planes aren't hinted.
    void test_plane2d_hints() {
        std::printf("\n[3D: hover hints by ray/plane intersection]\n");

        using namespace sextant;

        Transform3D tf;
        tf.xmin = 0.0;
        tf.xmax = 10.0;
        tf.ymin = 0.0;
        tf.ymax = 10.0;
        tf.zmin = 0.0;
        tf.zmax = 10.0;

        const PlotRect frame{20.0f, 15.0f, 400.0f, 320.0f};
        Camera3D cam;
        cam.azimuth = -55.0;
        cam.elevation = 24.0;

        for (int mode = 0; mode < 2; ++mode) {
            cam.projection = mode ? Projection::Perspective : Projection::Orthographic;
            const char* what = mode ? "perspective" : "orthographic";
            const Projector3D proj(tf, cam, frame, 0.1f);

            // Round trip: project a point on the plane, then ask what that pixel
            // names (exact: one divide).
            const double u0 = 3.5, v0 = 6.25, off = 4.0;
            const Vec3 pd = plane_point(PlaneOrientation::XY, u0, v0, off);
            const Px3 px = proj.project(pd.x, pd.y, pd.z);

            double u = 0.0, v = 0.0;
            float depth = 0.0f;
            const bool hit = plane_ray_hit(proj, PlaneOrientation::XY, off, px.x, px.y,
                                           u, v, depth);
            check(hit && std::fabs(u - u0) < 1e-6 && std::fabs(v - v0) < 1e-6,
                  std::string("hints: a pixel casts back to the point it came from (")
                  + what + ")");

            // A different pixel gives a different answer.
            double u2 = 0.0, v2 = 0.0;
            float d2 = 0.0f;
            check(plane_ray_hit(proj, PlaneOrientation::XY, off, px.x + 40.0f, px.y, u2, v2, d2)
                  && (std::fabs(u2 - u0) > 1e-3 || std::fabs(v2 - v0) > 1e-3),
                  std::string("hints: and a different pixel casts back somewhere else (")
                  + what + ")");

            // The other orientations round-trip on their own axes.
            for (auto o: {PlaneOrientation::YZ, PlaneOrientation::ZX}) {
                const Vec3 q = plane_point(o, u0, v0, off);
                const Px3 qp = proj.project(q.x, q.y, q.z);
                double a = 0.0, b = 0.0;
                float dd = 0.0f;
                check(plane_ray_hit(proj, o, off, qp.x, qp.y, a, b, dd) &&
                      std::fabs(a - u0) < 1e-6 && std::fabs(b - v0) < 1e-6,
                      std::string("hints: every orientation round-trips on its own two axes (")
                      + what + ")");
            }
        }

        // ---- Two parallel planes with a point under the same pixel: the near one
        // is reported.
        cam.projection = Projection::Orthographic;
        const Projector3D proj(tf, cam, frame, 0.1f);

        auto with_point = [](double offset, double x, double y, const char* label) {
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            pl.offset = offset;
            ScatterPlot sp;
            sp.x = std::vector<double>{x};
            sp.y = std::vector<double>{y};
            sp.opts.hint_labels = {label};
            pl.sheet.scatters.push_back(std::move(sp));
            return pl;
        };

        // B's point is where A's pixel ray meets B, so both are within the radius.
        const double off_a = 2.0, off_b = 8.0;
        const Px3 at = proj.project(4.0, 5.0, off_a);
        double ub = 0.0, vb = 0.0;
        float db = 0.0f;
        check(plane_ray_hit(proj, PlaneOrientation::XY, off_b, at.x, at.y, ub, vb, db),
              "hints: (the pixel meets both planes, so the two points really do coincide)");
        const Px3 at_b = proj.project(ub, vb, off_b);
        check(std::fabs(at_b.x - at.x) < 1e-3 && std::fabs(at_b.y - at.y) < 1e-3,
              "hints: (and they project to the same pixel, to within a fraction of one)");

        RenderSnapshot3D s;
        s.planes.push_back(with_point(off_a, 4.0, 5.0, "planeA"));
        s.planes.push_back(with_point(off_b, ub, vb, "planeB"));

        // Which is nearer is asked of the camera.
        const char* nearer_label = (at_b.depth < at.depth) ? "planeB" : "planeA";

        auto h = find_hint3d(s, proj, at.x, at.y);
        check(h.has_value(), "hints: a point on a plane is found under its own pixel");
        check(h && h->text.find(nearer_label) != std::string::npos,
              "hints: and with two points on one pixel it is the nearer plane's that wins");

        // Same with the planes added in the other order.
        RenderSnapshot3D swapped;
        swapped.planes.push_back(s.planes[1]);
        swapped.planes.push_back(s.planes[0]);
        auto hs = find_hint3d(swapped, proj, at.x, at.y);
        check(hs && hs->text.find(nearer_label) != std::string::npos,
              "hints: and reversing the order they were added does not change it");

        // Far from anything: nothing.
        check(!find_hint3d(s, proj, frame.x + 2.0f, frame.y + 2.0f).has_value(),
              "hints: and a pixel with nothing near it gets no hint");

        // A hidden plane isn't hit-tested.
        RenderSnapshot3D hidden = s;
        for (auto& pl: hidden.planes) pl.opts.visible = false;
        check(!find_hint3d(hidden, proj, at.x, at.y).has_value(),
              "hints: a hidden plane is not hinted");

        // Heatmap cells fall back to containment, as in 2D.
        RenderSnapshot3D grid; {
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            pl.offset = 5.0;
            HeatmapPlot hp;
            hp.rows = 2;
            hp.cols = 2;
            hp.xrange = {0.0, 10.0};
            hp.yrange = {0.0, 10.0};
            hp.data = std::vector<float>{0.0f, 1.0f, 2.0f, 3.0f};
            pl.sheet.heatmaps.push_back(std::move(hp));
            grid.planes.push_back(std::move(pl));
        }
        const Px3 centre = proj.project(7.5, 7.5, 5.0);
        auto hg = find_hint3d(grid, proj, centre.x, centre.y);
        check(hg && hg->text.find("row=") != std::string::npos &&
              hg->text.find("col=1") != std::string::npos,
              "hints: and a heatmap on a plane answers by cell, as the 2D one does");

        // ---- find_hint_cell() recognizes a 3D cell (rect from the projector).
        std::vector<AxesLayout> layout;
        layout.push_back({AxesSlot{1, 1, 1}, CoordTransform{}, proj});
        check(find_hint_cell(layout, frame.x + 5.0f, frame.y + 5.0f) == &layout[0],
              "hints: a 3D cell is found by the cursor at all");
        check(find_hint_cell(layout, frame.x - 5.0f, frame.y + 5.0f) == nullptr,
              "hints: and only inside its own frame");
    }

    // Plane2DOptions::visible hides the ink only; the box and layout don't change.

    void test_plane2d_visibility() {
        std::printf("\n[3D: hiding a plane]\n");

        using namespace sextant;

        constexpr int W = 360, H = 300;
        auto build = [&](bool visible) {
            auto fig = Figure::create({.width = W, .height = H, .supersample = 1});
            auto ax = fig->add_subplot3d(1, 1, 1);
            std::vector<double> field(16);
            for (int i = 0; i < 16; ++i) field[i] = static_cast<double>(i) / 15.0;
            ax->plane(PlaneOrientation::XY, 6.0, {.alpha = 1.0f, .visible = visible})
                    ->heatmap(field, 4, 4, {0.0, 4.0}, {0.0, 4.0}, {.colorbar = true});
            return fig;
        };

        build(true)->savefig("plane_visible.png");
        build(false)->savefig("plane_hidden.png");

        auto slurp = [](const char* p) {
            std::ifstream f(p, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        };
        check(slurp("plane_visible.png") != slurp("plane_hidden.png"),
              "visible: switching a plane off changes the picture");

        // ...measured through the layout actually drawn.
        auto layout_of = [&](bool visible) {
            RenderSnapshot3D s;
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::XY;
            pl.offset = 6.0;
            pl.opts.visible = visible;
            HeatmapPlot hp;
            hp.rows = 4;
            hp.cols = 4;
            hp.xrange = {0.0, 4.0};
            hp.yrange = {0.0, 4.0};
            hp.data = std::vector<float>(16, 0.5f);
            hp.opts.colorbar = true;
            pl.sheet.heatmaps.push_back(std::move(hp));
            s.planes.push_back(std::move(pl));

            FigureSnapshot fs;
            fs.axes.push_back({{1, 1, 1}, std::move(s)});
            return compute_figure_layout(fs, W, H);
        };
        const FigureLayout on = layout_of(true);
        const FigureLayout off = layout_of(false);
        check(on.cells.size() == 1 && off.cells.size() == 1 &&
              on.cells[0].frame.x == off.cells[0].frame.x &&
              on.cells[0].frame.w == off.cells[0].frame.w &&
              on.cells[0].frame.h == off.cells[0].frame.h,
              "visible: and the frame does not move, so the colorbar is still carved");
        check(on.cells[0].box3d && off.cells[0].box3d,
              "visible: (both cells really are 3D, so the comparison is of two boxes)");
        check(on.cells[0].box3d->proj.project(0.0, 0.0, 0.0).x ==
              off.cells[0].box3d->proj.project(0.0, 0.0, 0.0).x,
              "visible: and the limits are unchanged, so nothing else in the box moves");
    }
} // namespace lt
