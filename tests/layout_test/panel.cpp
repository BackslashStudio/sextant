// The panels: Cosmetic's groups, duplicate ids, selection, tabs. Part of
// sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "window_link.h"

namespace lt {
    // The Cosmetic panel in a null-backend ImGui frame: a 3D slot must seed the
    // scratch state from the 3D snapshot and not take the 2D path.
    void test_cosmetic_panel_3d() {
        std::printf("\n[3D: the Cosmetic panel]\n");

        using namespace sextant;

        FigureSnapshot fs;
        RenderSnapshot3D rs;
        rs.title = "Box";
        rs.ztitle = "counts";
        rs.zmin = -4.0;
        rs.zmax = 9.0;
        rs.zlim_auto = false;
        rs.camera.azimuth = 17.0;
        rs.camera.zoom = 1.5;
        rs.default_camera.azimuth = -60.0;
        rs.aspect = BoxAspect{2.0, 1.0, 1.0};
        // Two planes with different placements, to show per-plane seeding.
        rs.planes = two_plane_snapshot().planes;
        rs.planes[1].opts.alpha = 0.25f;
        rs.planes[1].opts.visible = false;
        // A bar grid and a surface with distinct appearance, to show per-object
        // seeding.
        {
            Bar3DPlot b = bar3d_grid();
            b.opts.color = {1.0f, 0.0f, 0.0f, 1.0f};
            b.opts.shading = 0.9f;
            rs.bars3d.push_back(std::move(b));
            SurfacePlot sp = ripple_surface();
            sp.opts.colormap = true;
            sp.opts.alpha = 0.4f;
            sp.opts.edges = true;
            rs.surfaces.push_back(std::move(sp));
        }
        fs.axes.push_back({{1, 1, 1}, std::move(rs)});
        fs.generation = fs.data_generation = 1;

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0f, 900.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();

        PanelState st;
        FigureEditBox box;
        std::size_t vertices = 0;
        for (int f = 0; f < 3; ++f) {
            ImGui::NewFrame();
            ImGui::SetNextWindowSize(ImVec2(420.0f, 820.0f));
            draw_cosmetic_panel(fs, box, st);
            ImGui::Render();
            vertices = 0;
            const ImDrawData* dd = ImGui::GetDrawData();
            for (int n = 0; n < dd->CmdListsCount; ++n)
                vertices += static_cast<std::size_t>(dd->CmdLists[n]->VtxBuffer.Size);
        }

        check(vertices > 0, "3D panel: the panel draws for a 3D slot");
        check(st.last_synced_slot == 1, "3D panel: the slot was synced");
        check(st.camera_local.azimuth == 17.0 && st.camera_local.zoom == 1.5,
              "3D panel: the camera is seeded from the 3D snapshot, not left at its default");
        check(st.zmin_local == -4.0 && st.zmax_local == 9.0,
              "3D panel: so is the third axis, which the 2D sync has no field for");
        check(std::string(st.ztitle_buf) == "counts",
              "3D panel: and the z title");
        check(st.aspect_local.x == 2.0, "3D panel: and the box aspect");

        // Plane rows seeded positionally (the planes differ in every field shown).
        check(st.planes_local.size() == 2, "3D panel: one Planes row per plane");
        check(st.planes_local[0].orient == PlaneOrientation::XY &&
              st.planes_local[0].offset == 0.25 &&
              st.planes_local[0].opts.visible && st.planes_local[0].opts.alpha == 1.0f,
              "3D panel: the first row seeded from the first plane");
        check(st.planes_local[1].orient == PlaneOrientation::YZ &&
              st.planes_local[1].offset == 0.5 &&
              !st.planes_local[1].opts.visible && st.planes_local[1].opts.alpha == 0.25f,
              "3D panel: and the second from the second, not from the first again");

        // Bar and surface rows, seeded the same way.
        check(st.bars3d_local.size() == 1 && st.surfaces_local.size() == 1,
              "3D panel: one Bars row per bar3d grid, one Surfaces row per surface");
        check(st.bars3d_local[0].color.r == 1.0f && st.bars3d_local[0].shading == 0.9f,
              "3D panel: the Bars row is seeded from its own grid's options");
        check(st.surfaces_local[0].colormap && st.surfaces_local[0].alpha == 0.4f &&
              st.surfaces_local[0].edges,
              "3D panel: and the Surfaces row from its own surface's");

        check(!box.load_and_clear().has_value(),
              "3D panel: drawing it without touching anything pushes no edit");

        // An appearance edit round-trips without overwriting `hint_labels` (the
        // panel's options copy may have stale labels).
        {
            FigureSnapshot target = fs;
            RenderSnapshot3D* t3 = target.axes[0].snap3d();
            t3->surfaces[0].opts.hint_labels.assign(t3->surfaces[0].count(), "kept");
            t3->bars3d[0].opts.hint_labels.assign(t3->bars3d[0].count(), "kept");

            AxesEdit3D e;
            SurfaceOptions so = t3->surfaces[0].opts;
            so.alpha = 0.15f;
            so.hint_labels.clear(); // as a stale panel copy would be
            e.surfaces.push_back({0, so});
            Bar3DOptions bo = t3->bars3d[0].opts;
            bo.shading = 0.1f;
            bo.hint_labels.clear();
            e.bars3d.push_back({0, bo});

            // The same function both threads apply (figure_edits.h).
            apply_axes3d_edit(*t3, e);

            check(t3->surfaces[0].opts.alpha == 0.15f && t3->bars3d[0].opts.shading == 0.1f,
                  "3D panel: an appearance edit reaches the object it names");
            check(t3->surfaces[0].opts.hint_labels.size() == t3->surfaces[0].count() &&
                  t3->bars3d[0].opts.hint_labels.size() == t3->bars3d[0].count(),
                  "3D panel: and leaves hint_labels alone, because those are data and this lane is not");
        }

        // Limit fields show the resolved limits for axes on auto (the declared
        // ones stay 0..1). draw_plot_panel() stores them in PanelState; filled by
        // hand here since no frame runs.
        st.resolved.clear();
        st.resolved.push_back({1, true, 400.0, 600.0, -1.5, 1.5, 0.0, 21.0});
        st.last_synced_slot = -1; // force a re-seed, as a slot change would
        for (int f = 0; f < 2; ++f) {
            ImGui::NewFrame();
            ImGui::SetNextWindowSize(ImVec2(420.0f, 820.0f));
            draw_cosmetic_panel(fs, box, st);
            ImGui::Render();
        }
        check(st.xmin_local == 400.0 && st.xmax_local == 600.0,
              "3D panel: an automatic axis shows the limits it resolved to, not its declared ones");
        check(st.ymin_local == -1.5 && st.ymax_local == 1.5,
              "3D panel: on every automatic axis");

        // ...and only for auto axes: explicit z limits must show as declared.
        check(st.zmin_local == -4.0 && st.zmax_local == 9.0,
              "3D panel: while an axis with explicit limits keeps them");

        ImGui::DestroyContext(ctx);
        ImGui::SetCurrentContext(nullptr);
    }

    // The Cosmetic panel's groups are CollapsingHeaders: each opened alone draws
    // more than all closed, and the old section names (now SeparatorText
    // sub-headings) change nothing when "opened". Shared by 2D and 3D: `tag`
    // prefixes check names, `groups` must open onto contents, `old` must not be
    // headers.
    void check_cosmetic_groups(const sextant::FigureSnapshot& fs, const char* tag,
                               std::initializer_list<const char *> groups,
                               std::initializer_list<const char *> old) {
        using namespace sextant;

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(470.0f, 3040.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();

        PanelState st;
        FigureEditBox box;
        auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            // Tall enough that no group's contents are clipped.
            ImGui::SetNextWindowSize(ImVec2(430.0f, 3000.0f));
            draw_cosmetic_panel(fs, box, st);
            ImGui::Render();
            return ImGui::GetDrawData()->TotalVtxCount;
        };
        frame();
        frame();

        char name[160];
        auto label = [&](const char* what) {
            std::snprintf(name, sizeof(name), "%s groups: %s", tag, what);
            return name;
        };
        ImGuiWindow* w = ImGui::FindWindowByName("Cosmetic");
        check(w != nullptr, label("the panel window is called Cosmetic"));
        // All names closed, then `name` alone open (in the window's storage).
        auto only = [&](const char* name) {
            for (const char* g: groups) w->StateStorage.SetInt(w->GetID(g), 0);
            for (const char* o: old) w->StateStorage.SetInt(w->GetID(o), 0);
            if (name) w->StateStorage.SetInt(w->GetID(name), 1);
            frame();
            return frame();
        };

        if (w) {
            const int closed = only(nullptr);
            bool groups_open = true;
            for (const char* g: groups)
                if (only(g) <= closed) {
                    groups_open = false;
                    std::printf("    group \"%s\" drew nothing when opened\n", g);
                }
            check(groups_open, label("each group is a header over contents"));

            bool old_gone = true;
            for (const char* o: old)
                if (only(o) != closed) {
                    old_gone = false;
                    std::printf("    \"%s\" is still a header of its own\n", o);
                }
            check(old_gone, label("and none of the old section names is a header any more"));
        }

        ImGui::DestroyContext(ctx);
        ImGui::SetCurrentContext(nullptr);
    }

    void test_cosmetic_groups_3d() {
        std::printf("\n[3D Cosmetic panel: five groups]\n");

        using namespace sextant;

        FigureSnapshot fs;
        RenderSnapshot3D r;
        r.title = "Box";
        r.xtitle = "x";
        r.ytitle = "y";
        r.ztitle = "z";
        fs.axes.push_back({{1, 1, 1}, std::move(r)});
        fs.generation = fs.data_generation = 1;

        // "Legend & colorbar" is shared with 2D; "Legend" and "Colorbar" are
        // sub-headings.
        check_cosmetic_groups(fs, "3D", {
                                  "View", "Figure", "Axis", "Ticks",
                                  "Legend & colorbar"
                              },
                              {
                                  "Camera", "Box", "Text", "Limits", "Layout",
                                  "Ticks & labels", "Grid", "Axis frame",
                                  "Legend", "Colorbar"
                              });
    }

    // The 2D panel's groups: the 3D ones minus View, plus Legend & colorbar.
    void test_cosmetic_groups_2d() {
        std::printf("\n[2D Cosmetic panel: four groups]\n");

        const auto fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
        check_cosmetic_groups(fs, "2D", {"Figure", "Axis", "Ticks", "Legend & colorbar"},
                              {
                                  "Text", "Layout", "Limits", "Ticks & labels",
                                  "Grid", "Axis frame", "Legend", "Colorbar"
                              });
    }

    // Duplicate ImGui ids (two items sharing one activate together). ImGui reports
    // a conflict only for the hovered item, and closed headers submit nothing, so
    // open every section and sweep the cursor down the panel. Two frames per
    // position: the count is gathered in one frame and read in the next.
    struct IdConflictScan {
        int probes = 0;
        int conflicts = 0;
        ImGuiID first = 0;
    };

    IdConflictScan scan_panel_for_id_conflicts(
        const sextant::FigureSnapshot& fsnap,
        void (*draw)(const sextant::FigureSnapshot&, sextant::FigureEditBox&, sextant::PanelState&),
        const char* window_name,
        std::initializer_list<const char *> sections) {
        // Tall enough to submit every section (clipped items aren't checked).
        constexpr float W = 430.0f, H = 3000.0f;

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(W + 40.0f, H + 40.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.ConfigDebugHighlightIdConflicts = true;
        io.Fonts->AddFontDefault();

        sextant::PanelState st;
        sextant::FigureEditBox box;

        auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(W, H));
            draw(fsnap, box, st);
            ImGui::Render();
        };

        // Settle, then open every section (closed ones hide their widgets).
        frame();
        frame();
        // Written into the window's storage (TreeNodeSetOpen() needs a current
        // window, which is null between frames).
        if (ImGuiWindow* w = ImGui::FindWindowByName(window_name))
            for (const char* s: sections)
                w->StateStorage.SetInt(w->GetID(s), 1);

        IdConflictScan out;
        for (float y = 2.0f; y < H; y += 3.0f) {
            io.MousePos = ImVec2(W * 0.5f, y);
            frame();
            frame();
            ++out.probes;
            const ImGuiID id = ImGui::GetCurrentContext()->DebugDrawIdConflictsId;
            if (id != 0) {
                ++out.conflicts;
                if (out.first == 0) out.first = id;
            }
        }

        ImGui::DestroyContext(ctx);
        ImGui::SetCurrentContext(nullptr);
        return out;
    }

    void test_panel_id_conflicts() {
        std::printf("\n[Cosmetic panel: no duplicate widget ids]\n");

        using namespace sextant;

        // Every group of both panels, listed explicitly so a new one without a
        // line here is noticed.
        const std::initializer_list<const char *> sections = {
            "View", "Figure", "Axis", "Ticks", "Legend & colorbar",
        };

        FigureSnapshot fs3;
        RenderSnapshot3D r3;
        r3.title = "Box";
        r3.xtitle = "x";
        r3.ytitle = "y";
        r3.ztitle = "z";
        // Two planes, so per-plane rows (e.g. two "Visible" checkboxes) are
        // covered.
        r3.planes = two_plane_snapshot().planes;
        fs3.axes.push_back({{1, 1, 1}, std::move(r3)});
        fs3.generation = fs3.data_generation = 1;

        const IdConflictScan s3 = scan_panel_for_id_conflicts(
            fs3, &draw_cosmetic_panel, "Cosmetic", sections);
        check(s3.conflicts == 0,
              "3D panel: no two widgets share an id (a checkbox labelled like its own header would)");
        if (s3.conflicts)
            std::printf("    3D: %d of %d cursor positions reported a conflict, first id %u\n",
                        s3.conflicts, s3.probes, static_cast<unsigned>(s3.first));

        // The 2D panel through the same scan.
        const auto fs2 = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
        const IdConflictScan s2 = scan_panel_for_id_conflicts(
            fs2, &draw_cosmetic_panel, "Cosmetic", sections);
        check(s2.conflicts == 0, "2D panel: likewise");
        if (s2.conflicts)
            std::printf("    2D: %d of %d cursor positions reported a conflict, first id %u\n",
                        s2.conflicts, s2.probes, static_cast<unsigned>(s2.first));

        // The Data panel over two planes that each hold a "line 0" (the "P0 "/
        // "P1 " prefixes and "##i" must keep tabs distinct).
        const IdConflictScan sd = scan_panel_for_id_conflicts(
            fs3, &draw_data_panel, "Data", {});
        check(sd.conflicts == 0,
              "Data panel: two planes holding objects of the same name keep distinct ids");
        if (sd.conflicts)
            std::printf("    Data: %d of %d cursor positions reported a conflict, first id %u\n",
                        sd.conflicts, sd.probes, static_cast<unsigned>(sd.first));

        // With a bar3d grid selected: the grid table's frozen header rows and
        // gutter coordinates.
        FigureSnapshot fsb = fs3;
        fsb.axes[0].snap3d()->bars3d.push_back(bar3d_grid());
        const IdConflictScan sb = scan_panel_for_id_conflicts(
            fsb, &draw_data_panel, "Data", {});
        check(sb.conflicts == 0,
              "Data panel: and a bar3d grid's own table, whose gutter and header rows are "
              "all new ids");
        if (sb.conflicts)
            std::printf("    bar3d: %d of %d cursor positions reported a conflict, first id %u\n",
                        sb.conflicts, sb.probes, static_cast<unsigned>(sb.first));

        // A surface alone, so its Appearance block is drawn.
        FigureSnapshot fss; {
            RenderSnapshot3D r;
            r.surfaces.push_back(ripple_surface());
            fss.axes.push_back({{1, 1, 1}, std::move(r)});
            fss.generation = fss.data_generation = 1;
        }
        const IdConflictScan ss = scan_panel_for_id_conflicts(
            fss, &draw_data_panel, "Data", {});
        check(ss.conflicts == 0,
              "Data panel: and a surface's tab, with its Appearance block above the table");
        if (ss.conflicts)
            std::printf("    surface: %d of %d cursor positions reported a conflict, first id %u\n",
                        ss.conflicts, ss.probes, static_cast<unsigned>(ss.first));

        // A path alone, so its Appearance block is drawn and swept.
        FigureSnapshot fsl; {
            RenderSnapshot3D r;
            Line3DPlot l;
            l.x = std::vector<double>{0.0, 1.0, 2.0};
            l.y = std::vector<double>{0.0, 2.0, 1.0};
            l.z = std::vector<double>{0.0, 1.0, 3.0};
            l.colors = std::vector<double>{1.0, 2.0, 3.0}; // so vmin/vmax are drawn
            r.lines3d.push_back(std::move(l));
            fsl.axes.push_back({{1, 1, 1}, std::move(r)});
            fsl.generation = fsl.data_generation = 1;
        }
        const IdConflictScan sl = scan_panel_for_id_conflicts(
            fsl, &draw_data_panel, "Data", {});
        check(sl.conflicts == 0,
              "Data panel: and a path's tab, whose Appearance block is a stroke's controls "
              "rather than a marker's");
        if (sl.conflicts)
            std::printf("    line3d: %d of %d cursor positions reported a conflict, first id %u\n",
                        sl.conflicts, sl.probes, static_cast<unsigned>(sl.first));

        std::printf("  swept %d cursor positions per panel\n", s3.probes);
    }

    // Click-to-select and the navigation gate, driven through
    // update_plot_selection() frame by frame (no window, no ImGui).
    void test_subplot_selection() {
        std::printf("\n[subplot selection: menu combo + click-to-select]\n");

        using namespace sextant;

        // Slot 1 is 2D, slot 2 is 3D with a distinct camera, so wrong seeding shows.
        FigureSnapshot fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
        fs.axes[0].slot = AxesSlot{1, 2, 1}; {
            RenderSnapshot3D rs;
            rs.title = "box";
            rs.camera.azimuth = 71.0;
            rs.camera.zoom = 2.5;
            rs.default_camera = rs.camera;
            fs.axes.push_back({AxesSlot{1, 2, 2}, std::move(rs)});
        }

        // ---- The cell rect contains the frame.
        const FigureLayout fl = compute_figure_layout(fs, 800, 400);
        check(fl.cells.size() == 2, "select: one cell per axes");
        std::vector<AxesLayout> layout;
        for (const CellLayout& c: fl.cells) {
            const PlotRect& r = c.cell;
            check(c.frame.x >= r.x && c.frame.y >= r.y &&
                  c.frame.x + c.frame.w <= r.x + r.w + 1e-3f &&
                  c.frame.y + c.frame.h <= r.y + r.h + 1e-3f,
                  "select: the frame lies inside the cell it was carved from");
            layout.push_back({
                c.slot, c.tr,
                c.box3d ? std::optional<Projector3D>(c.box3d->proj) : std::nullopt,
                c.cell
            });
        }
        check(layout[0].cell.x + layout[0].cell.w <= layout[1].cell.x,
              "select: the two cells of a 1x2 grid do not overlap");

        const PlotRect c1 = layout[0].cell, c2 = layout[1].cell;
        // Inside the cell but outside the frame (tick label/title area) still
        // counts.
        const float in1x = c1.x + 2.0f, in1y = c1.y + 2.0f;
        const float in2x = c2.x + c2.w - 2.0f, in2y = c2.y + c2.h - 2.0f;
        check(find_cell_at(layout, in1x, in1y) == &layout[0] &&
              find_cell_at(layout, in2x, in2y) == &layout[1],
              "select: a point in a cell's decorations finds that cell, not just its frame");
        check(find_cell_at(layout, c1.x - 1.0f, in1y) == nullptr &&
              find_cell_at(layout, in1x, c1.y + c1.h + 1.0f) == nullptr,
              "select: a margin belongs to no cell");

        // Cells never overlap (spans are checked in test_subplot_spans()).

        PanelState st;
        auto frame = [&](float x, float y, auto&& set) {
            PlotPointer in;
            in.x = x;
            in.y = y;
            in.hovered = true;
            set(in);
            return update_plot_selection(st, fs, layout, in);
        };
        auto idle = [](PlotPointer&) {
        };
        auto press = [](PlotPointer& p) { p.pressed = p.active = true; };
        auto dpress = [](PlotPointer& p) { p.pressed = p.active = p.double_clicked = true; };
        auto hold = [](PlotPointer& p) { p.active = true; };
        auto release = [](PlotPointer& p) { p.released = true; };
        auto drop = [](PlotPointer& p) { p.released = p.dragged = true; };

        // ---- The first frame seeds the scratch from slot 1 with no panel drawn.
        frame(in1x, in1y, idle);
        check(st.selected_slot_index == 1 && st.last_synced_slot == 1,
              "select: the selection is synced before any panel draws");

        // ---- Hovering the other cell lets nothing through to navigation.
        PlotNavGate g = frame(in2x, in2y, idle);
        check(!g.wheel && !g.keys, "select: the wheel and keys ignore an unselected cell");
        g = frame(in1x, in1y, idle);
        check(g.wheel && g.keys, "select: and act over the selected one, decorations included");

        // ---- A drag starting on an unselected cell neither navigates nor selects.
        g = frame(in2x, in2y, press);
        check(!g.drag && st.selected_slot_index == 1,
              "select: a press on an unselected cell does not select it yet");
        g = frame(in2x - 30.0f, in2y, hold);
        check(!g.drag, "select: nor does the drag after it navigate anything");
        frame(in2x - 30.0f, in2y, drop);
        check(st.selected_slot_index == 1, "select: and ending a drag is not a click");

        // ---- Pressed on one cell, released on the other: nothing.
        frame(in2x, in2y, press);
        frame(in1x, in1y, release);
        check(st.selected_slot_index == 1,
              "select: a press released over a different cell selects neither");

        // ---- A click selects on release and re-seeds (the 3D camera).
        frame(in2x, in2y, press);
        check(st.selected_slot_index == 1, "select: still not on the press");
        frame(in2x, in2y, release);
        check(st.selected_slot_index == 2, "select: a click selects on its release");
        check(st.last_synced_slot == 2 && st.camera_local.azimuth == 71.0 &&
              st.camera_local.zoom == 2.5,
              "select: and the camera navigation starts from is the new slot's own");

        // ---- The double-click completing a selecting click doesn't reset...
        g = frame(in2x, in2y, dpress);
        check(!g.reset, "select: a double-click whose first click selected does not reset");
        check(g.drag, "select: but the cell is selected now, so the press may navigate");
        frame(in2x, in2y, release);

        // ...but one on an already-selected cell does.
        frame(in2x, in2y, press);
        frame(in2x, in2y, release);
        g = frame(in2x, in2y, dpress);
        check(g.reset, "select: a double-click on the already-selected cell resets it");
        frame(in2x, in2y, release);

        // ---- A drag on the selected cell navigates, even leaving it.
        frame(in2x, in2y, press);
        g = frame(in1x, in1y, hold);
        check(g.drag && g.keys && !g.wheel,
              "select: a drag begun on the selected cell keeps it when the cursor leaves");
        frame(in1x, in1y, drop);
        check(st.selected_slot_index == 2, "select: and does not select where it ends");

        // ---- select_slot() (the menu) re-seeds as a click does.
        select_slot(st, fs, 1);
        check(st.selected_slot_index == 1 && st.last_synced_slot == 1 &&
              std::string(st.title_buf).empty(),
              "select: select_slot() moves the selection and re-seeds from that slot");
        select_slot(st, fs, 2);
        check(std::string(st.title_buf) == "box", "select: in either direction");

        // ---- A nonexistent selection is normalized to the first slot.
        PanelState st2;
        st2.selected_slot_index = 7;
        check(sync_selected_slot(st2, fs) == 1 && st2.selected_slot_index == 1,
              "select: a missing slot falls back to the first one");
        check(sync_selected_slot(st2, FigureSnapshot{}) == -1, "select: and no axes is no slot");
    }

    // A 3D object's controls live in its Data-panel tab: every plane gets a tab
    // (even empty, in order), each tab's edit reaches the right lane and index,
    // and scratch copies re-seed on an object-count change without the Cosmetic
    // panel.
    void test_data_panel_object_tabs() {
        std::printf("\n[Data panel: a 3D object's controls are in its own tab]\n");

        using namespace sextant;

        // ---- The tab list: a bar grid on the axes, nothing on plane 0, two
        // objects on plane 1, nothing on plane 2.
        {
            std::vector<PlotDataTable> tables(3);
            tables[0].kind = PlotKind::Bar3D;
            tables[0].plot_index = 0;
            tables[0].plane_index = -1;
            tables[1].kind = PlotKind::Line;
            tables[1].plot_index = 0;
            tables[1].plane_index = 1;
            tables[2].kind = PlotKind::Line;
            tables[2].plot_index = 1;
            tables[2].plane_index = 1;
            const auto tabs = data_panel_tabs(tables, 3);
            const bool ok = tabs.size() == 6
                            && tabs[0].table == 0 && tabs[0].plane == -1
                            && tabs[1].table == -1 && tabs[1].plane == 0
                            && tabs[2].table == -1 && tabs[2].plane == 1
                            && tabs[3].table == 1 && tabs[4].table == 2
                            && tabs[5].table == -1 && tabs[5].plane == 2;
            check(ok, "tabs: every plane has a tab, empty ones included, just before its own objects");
            check(data_panel_tabs(tables, 0).size() == 3 &&
                  data_panel_tabs({}, 0).empty(),
                  "tabs: and with no planes the list is the tables alone");
        }

        // Drives the Data panel alone, sweeping clicks until an appearance edit
        // matching `want` arrives. Popups are closed as they open.
        auto sweep_for = [](const FigureSnapshot& fs, PanelState& st,
                            auto&& want) -> std::optional<AxesEdit3D> {
            ImGuiContext* ctx = ImGui::CreateContext();
            ImGui::SetCurrentContext(ctx);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(560.0f, 900.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();

            FigureEditBox box;
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(520.0f, 860.0f));
                draw_data_panel(fs, box, st);
                if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                    ImGui::ClosePopupToLevel(0, false);
                ImGui::Render();
            };
            frame();
            frame();
            frame();

            std::optional<AxesEdit3D> got;
            // Below the format row (its combo opens a popup) and down through the
            // tab's controls.
            for (float y = 90.0f; y < 600.0f && !got; y += 4.0f) {
                for (float x = 8.0f; x < 400.0f && !got; x += 6.0f) {
                    io.MousePos = ImVec2(x, y);
                    io.MouseDown[0] = true;
                    frame();
                    io.MouseDown[0] = false;
                    frame();
                    auto e = box.load_and_clear();
                    if (!e || e->per_axes3d.empty()) continue;
                    if (want(e->per_axes3d[0].second)) got = e->per_axes3d[0].second;
                }
            }
            ImGui::DestroyContext(ctx);
            ImGui::SetCurrentContext(nullptr);
            return got;
        };
        auto wrap = [](RenderSnapshot3D r) {
            FigureSnapshot fs;
            fs.axes.push_back({{1, 1, 1}, std::move(r)});
            fs.generation = fs.data_generation = 1;
            return fs;
        };

        // ---- An empty plane: its own tab is the only one.
        {
            RenderSnapshot3D r;
            PlaneSnapshot pl;
            pl.orient = PlaneOrientation::YZ;
            pl.offset = 0.5;
            r.planes.push_back(std::move(pl));
            const FigureSnapshot fs = wrap(std::move(r));
            PanelState st;
            const auto e = sweep_for(fs, st, [](const AxesEdit3D& a) { return !a.planes.empty(); });
            check(e.has_value(), "object tabs: an empty plane's tab has controls that edit it");
            check(e && e->planes[0].plane_index == 0 &&
                  e->planes[0].opts && !e->planes[0].opts->visible &&
                  e->planes[0].orient == PlaneOrientation::YZ && e->planes[0].offset == 0.5,
                  "object tabs: its Visible box reaches the plane lane, carrying the placement it had");
        }

        // ---- A bar grid's tab: Appearance on the bar lane, index 0.
        {
            RenderSnapshot3D r;
            r.bars3d.push_back(bar3d_grid());
            const FigureSnapshot fs = wrap(std::move(r));
            PanelState st;
            const auto e = sweep_for(fs, st, [](const AxesEdit3D& a) { return !a.bars3d.empty(); });
            check(e && e->bars3d[0].plot_index == 0 && e->plot_ops.empty(),
                  "object tabs: a bar grid's Appearance block edits that grid on the appearance lane");
        }

        // ---- A surface's tab, likewise.
        {
            RenderSnapshot3D r;
            r.surfaces.push_back(ripple_surface());
            const FigureSnapshot fs = wrap(std::move(r));
            PanelState st;
            const auto e = sweep_for(fs, st, [](const AxesEdit3D& a) { return !a.surfaces.empty(); });
            check(e && e->surfaces[0].plot_index == 0 && e->plot_ops.empty(),
                  "object tabs: and a surface's, on the surface lane");
        }

        // ---- A cloud's tab (Appearance over a vector table).
        {
            RenderSnapshot3D r;
            Scatter3DPlot c;
            c.x = std::vector<double>{1.0, 2.0, 3.0};
            c.y = std::vector<double>{4.0, 5.0, 6.0};
            c.z = std::vector<double>{7.0, 8.0, 9.0};
            r.scatter3d.push_back(std::move(c));
            const FigureSnapshot fs = wrap(std::move(r));
            PanelState st;
            const auto e = sweep_for(fs, st, [](const AxesEdit3D& a) { return !a.scatter3d.empty(); });
            check(e && e->scatter3d[0].plot_index == 0 && e->plot_ops.empty(),
                  "object tabs: and a cloud's, on the scatter3d lane");
        }

        // ---- Scratch copies follow an object-count change without the Cosmetic
        // panel.
        {
            RenderSnapshot3D r;
            Bar3DPlot red = bar3d_grid();
            red.opts.color = {1.0f, 0.0f, 0.0f, 1.0f};
            r.bars3d.push_back(red);
            FigureSnapshot fs = wrap(std::move(r));
            PanelState st;
            sync_selected_slot(st, fs);
            check(st.bars3d_local.size() == 1 && st.bars3d_local[0].color.r == 1.0f,
                  "object tabs: the copies are seeded from the snapshot");

            Bar3DPlot blue = bar3d_grid();
            blue.opts.color = {0.0f, 0.0f, 1.0f, 1.0f};
            RenderSnapshot3D* s3 = fs.axes[0].snap3d();
            s3->bars3d.insert(s3->bars3d.begin(), blue);
            PlaneSnapshot pl;
            s3->planes.push_back(pl);
            sync_selected_slot(st, fs);
            check(st.bars3d_local.size() == 2 && st.bars3d_local[0].color.b == 1.0f &&
                  st.bars3d_local[1].color.r == 1.0f,
                  "object tabs: a grid inserted ahead re-seeds them, so index 0 is the new grid");
            check(st.planes_local.size() == 1,
                  "object tabs: and a plane added is picked up the same way");
        }
    }

    // HiDPI chrome scaling: apply_panel_style() runs every frame, and
    // ScaleAllSizes() multiplies in place, so it must rebuild from a default style.
    // Asserted by behaviour: the same scale twice gives the same style, and scaling
    // up then down returns exactly.
    void test_panel_dpi_scale() {
        std::printf("\n[panel: HiDPI chrome scaling]\n");

        using namespace sextant;

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGui::GetIO().Fonts->AddFontDefault();

        // A sample of size fields.
        auto sample = [] {
            const ImGuiStyle& s = ImGui::GetStyle();
            return std::vector<float>{
                s.WindowPadding.x, s.WindowPadding.y, s.FramePadding.x, s.FramePadding.y,
                s.ItemSpacing.x, s.ItemSpacing.y, s.ItemInnerSpacing.x,
                s.IndentSpacing, s.ScrollbarSize, s.GrabMinSize,
                s.CellPadding.x, s.CellPadding.y, s.WindowMinSize.x,
                s.SeparatorTextPadding.x, s.FontScaleDpi,
            };
        };

        apply_panel_style(PanelTheme::Light, 1.0f);
        const std::vector<float> at_1x = sample();
        const ImVec4 light_bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

        apply_panel_style(PanelTheme::Light, 1.5f);
        const std::vector<float> at_15x = sample();

        check(ImGui::GetStyle().FontScaleDpi == 1.5f,
              "dpi: FontScaleDpi carries the scale exactly (this is what resizes the text)");
        check(at_1x.back() == 1.0f, "dpi: and is 1.0 at 100%");

        // Each is the truncated 1.5x of its unscaled value (ScaleAllSizes uses
        // ImTrunc: 3 -> 4, not 4.5).
        bool all_scaled = true;
        for (std::size_t i = 0; i + 1 < at_1x.size(); ++i)
            if (at_15x[i] != std::trunc(at_1x[i] * 1.5f)) {
                all_scaled = false;
                std::printf("    field %zu: %g at 1x -> %g at 1.5x, wanted %g\n",
                            i, static_cast<double>(at_1x[i]),
                            static_cast<double>(at_15x[i]),
                            static_cast<double>(std::trunc(at_1x[i] * 1.5f)));
            }
        check(all_scaled, "dpi: every sampled size field is trunc(1.5x) of its unscaled value");

        // A second call at the same scale changes nothing (no compounding).
        apply_panel_style(PanelTheme::Light, 1.5f);
        check(sample() == at_15x,
              "dpi: applying the same scale twice is the same style (no compounding)");

        // And back down.
        apply_panel_style(PanelTheme::Light, 1.0f);
        check(sample() == at_1x,
              "dpi: 1.5x then 1.0x returns exactly to the unscaled style");

        // Rescaling keeps the theme's colors.
        apply_panel_style(PanelTheme::Light, 1.5f);
        const ImVec4 light_bg_scaled = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        apply_panel_style(PanelTheme::Dark, 1.5f);
        const ImVec4 dark_bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        check(light_bg_scaled.x == light_bg.x && light_bg_scaled.y == light_bg.y &&
              light_bg_scaled.z == light_bg.z,
              "dpi: a rescale keeps the theme's own colours");
        check(dark_bg.x != light_bg.x || dark_bg.y != light_bg.y || dark_bg.z != light_bg.z,
              "dpi: and the theme still decides them (Dark differs from Light)");

        std::printf("  window padding %g -> %g, scrollbar %g -> %g, frame pad y %g -> %g\n",
                    static_cast<double>(at_1x[0]), static_cast<double>(at_15x[0]),
                    static_cast<double>(at_1x[8]), static_cast<double>(at_15x[8]),
                    static_cast<double>(at_1x[3]), static_cast<double>(at_15x[3]));

        // ---- Which scale the chrome is styled at (v1.0 step 21.6).
        // Two platforms put the display's scale in different places, and the
        // panel must be scaled by it exactly once. As arithmetic, so the macOS
        // answer is checked here rather than assumed.
        check(chrome_scale_from(1.0f, 1.0f) == 1.0f,
              "dpi: a 100% display with no framebuffer scaling styles at 1.0");
        check(chrome_scale_from(1.5f, 1.0f) == 1.5f,
              "dpi: a 150% Windows display scales the window, so the chrome takes all of it");
        check(chrome_scale_from(2.0f, 2.0f) == 1.0f,
              "dpi: a Retina Mac scales the framebuffer instead, so the chrome takes none "
              "of it -- scaling by the content scale as well drew the panel 2x too big");
        check(chrome_scale_from(2.0f, 1.0f) == 2.0f,
              "dpi: and a 200% display that does not scale its framebuffer still takes all of it");
        check(chrome_scale_from(0.0f, 1.0f) == 1.0f && chrome_scale_from(1.5f, 0.0f) == 1.5f,
              "dpi: a scale nobody reported reads as 1");

        // The live link, and the half this platform owes. Both answers stated:
        // where the framebuffer carries no scaling the chrome scale *is* the
        // content scale, and where it carries all of it the chrome scale is 1.
        {
            GLContext gl({.width = 200, .height = 150, .title = "layout_test", .visible = false});
            const WindowLink& link = gl.link();
            check(link.chrome_scale() ==
                  chrome_scale_from(link.content_scale(), link.framebuffer_scale()),
                  "dpi: a window's chrome scale is that arithmetic over its own mirror");
            const bool fb_scales = link.framebuffer_scale() != 1.0f;
            check(fb_scales
                      ? link.chrome_scale() < link.content_scale()
                      : link.chrome_scale() == link.content_scale(),
                  "dpi: which is the content scale where the framebuffer is not scaled, "
                  "and less than it where it is");
            std::printf("  content %g, framebuffer %g -> chrome %g\n",
                        static_cast<double>(link.content_scale()),
                        static_cast<double>(link.framebuffer_scale()),
                        static_cast<double>(link.chrome_scale()));
        }

        ImGui::DestroyContext(ctx);
        ImGui::SetCurrentContext(nullptr);
    }

    // The per-object appearance lane (PlotStyleEdit) for 2D kinds, including on
    // planes. One body serves both thread sides (Axes::Impl and RenderSnapshot).
    void test_plot_style_lane() {
        std::printf("\n[the per-plot-object style lane]\n");

        using namespace sextant;

        // ---- A 2D target, addressed at the axes.
        {
            RenderSnapshot s;
            LinePlot lp;
            lp.x = CowVec<double>{std::vector<double>{0.0, 1.0}};
            lp.y = CowVec<double>{std::vector<double>{0.0, 1.0}};
            lp.opts.name = "series";
            lp.opts.hint_labels = {"a", "b"};
            s.lines.push_back(lp);

            HeatmapPlot hp;
            hp.rows = 2;
            hp.cols = 2;
            hp.data = CowVec<float>{std::vector<float>(4, 0.5f)};
            s.heatmaps.push_back(hp);

            LineOptions lo = s.lines[0].opts;
            lo.show_legend = false;
            lo.hint_labels.clear(); // a stale copy, as the panel's would be
            HeatmapOptions ho = s.heatmaps[0].opts;
            ho.colorbar = true;

            apply_plot_style_edits(s, {{0, -1, lo}, {0, -1, ho}});

            check(!s.lines[0].opts.show_legend && s.heatmaps[0].opts.colorbar,
                  "style lane: an edit reaches the object the variant names");
            check(s.lines[0].opts.name == "series",
                  "style lane: carrying the rest of the struct with it");
            check(s.lines[0].opts.hint_labels.size() == 2,
                  "style lane: but NOT hint_labels, which are data the table owns -- a stale "
                  "copy riding an appearance edit would silently revert them");

            // A stale index is skipped, not clamped.
            LineOptions gone = lo;
            gone.name = "should not appear";
            apply_plot_style_edits(s, {{7, -1, gone}});
            check(s.lines[0].opts.name == "series",
                  "style lane: an index naming no object is skipped, not clamped");

            // A plane-addressed edit doesn't apply to a 2D axes.
            LineOptions elsewhere = lo;
            elsewhere.name = "on a plane";
            apply_plot_style_edits(s, {{0, 1, elsewhere}});
            // Checked against the plane edit's own text, so failures don't cascade.
            check(s.lines[0].opts.name != "on a plane",
                  "style lane: a 2D target skips a plane-addressed edit");
        }

        // ---- A 3D target, addressed at a plane (plane -1 is skipped: 3D objects
        // have their own lanes).
        {
            RenderSnapshot3D r;
            PlaneSnapshot pl;
            LinePlot lp;
            lp.x = CowVec<double>{std::vector<double>{0.0, 1.0}};
            lp.y = CowVec<double>{std::vector<double>{0.0, 1.0}};
            lp.opts.name = "on the plane";
            pl.sheet.lines.push_back(lp);
            r.planes.push_back(std::move(pl));

            LineOptions lo = r.planes[0].sheet.lines[0].opts;
            lo.show_legend = false;
            AxesEdit3D e;
            e.plot_styles.push_back({0, 0, lo});
            apply_axes3d_edit(r, e);
            check(!r.planes[0].sheet.lines[0].opts.show_legend,
                  "style lane: a plane-addressed edit reaches that plane's own object");

            // A plane that no longer exists is skipped, not clamped.
            AxesEdit3D stale;
            LineOptions relabel = lo;
            relabel.name = "nowhere";
            stale.plot_styles.push_back({0, 4, relabel});
            apply_axes3d_edit(r, stale);
            check(r.planes[0].sheet.lines[0].opts.name == "on the plane",
                  "style lane: and a plane index naming no plane is skipped");
        }

        // ---- Through a real FigureEditBox drain onto a published RenderSnapshot.
        {
            RenderSnapshot published;
            LinePlot lp;
            lp.x = CowVec<double>{std::vector<double>{0.0, 1.0}};
            lp.y = CowVec<double>{std::vector<double>{0.0, 1.0}};
            lp.opts.name = "series";
            published.lines.push_back(lp);

            LineOptions off;
            off.name = "series";
            off.show_legend = false;

            FigureEditBox box;
            box.update(1, [&](AxesEdit& e) { e.plot_styles.push_back({0, -1, off}); });
            auto drained = box.load_and_clear();
            check(drained && !drained->per_axes.empty(),
                  "style lane: the edit survives a FigureEditBox drain");

            if (drained && !drained->per_axes.empty()) {
                apply_plot_style_edits(published, drained->per_axes[0].second.plot_styles);
                check(!published.lines[0].opts.show_legend,
                      "style lane: and the render thread's snapshot takes it");
            }
        }

        // ---- The Data panel emits one: sweep clicks over the Appearance block.
        {
            FigureSnapshot fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
            fs.axes[0].snap2d()->lines[0].opts.name = "series";

            ImGuiContext* ctx = ImGui::CreateContext();
            ImGui::SetCurrentContext(ctx);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(560.0f, 900.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();

            PanelState st;
            FigureEditBox box;
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(520.0f, 860.0f));
                draw_data_panel(fs, box, st);
                if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                    ImGui::ClosePopupToLevel(0, false);
                ImGui::Render();
            };
            frame();
            frame();
            frame();

            // Find the legend checkbox and the Name field (typed into; a click
            // alone emits nothing).
            bool emitted = false, off = false, named = false;
            for (float y = 90.0f; y < 400.0f && !(emitted && named); y += 4.0f) {
                for (float x = 8.0f; x < 400.0f && !(emitted && named); x += 6.0f) {
                    io.MousePos = ImVec2(x, y);
                    io.MouseDown[0] = true;
                    frame();
                    io.MouseDown[0] = false;
                    frame();
                    // A click on a text field leaves it active; type into it.
                    io.AddInputCharacter(static_cast<unsigned>(0x5A)); // 'Z'
                    frame();
                    auto e = box.load_and_clear();
                    if (!e || e->per_axes.empty()) continue;
                    for (const auto& ps: e->per_axes[0].second.plot_styles)
                        if (const auto* o = std::get_if<LineOptions>(&ps.opts)) {
                            if (o->name.find('Z') != std::string::npos) named = true;
                            else {
                                emitted = true;
                                off = !o->show_legend;
                            }
                        }
                }
            }
            ImGui::DestroyContext(ctx);
            ImGui::SetCurrentContext(nullptr);

            check(emitted, "style lane: the Data panel's Appearance block emits one");
            check(off, "style lane: and the legend checkbox is what it carries");
            check(named, "style lane: the Name field emits one too, carrying the typed label");
        }
    }

    // The Cosmetic panel's axis-position controls: scratch seeding for the
    // optional pins and each control's effect on the emitted AxesStyle. (Id
    // conflicts are covered by test_panel_id_conflicts().)
    void test_panel_axis_position() {
        std::printf("\n[Cosmetic panel: axis position]\n");

        using namespace sextant;

        // axis_position_combo()'s names must be in enum order.
        check(static_cast<int>(AxisPosition::Auto) == 0 &&
              static_cast<int>(AxisPosition::Low) == 1 &&
              static_cast<int>(AxisPosition::Mid) == 2 &&
              static_cast<int>(AxisPosition::High) == 3,
              "panel: AxisPosition is in the order the combo names it");

        auto run = [](const AxesStyle& seed, PanelState& st) {
            FigureSnapshot fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});
            fs.axes[0].snap2d()->axes_style = seed;

            ImGuiContext* ctx = ImGui::CreateContext();
            ImGui::SetCurrentContext(ctx);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(480.0f, 3040.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();

            FigureEditBox box;
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(430.0f, 3000.0f));
                draw_cosmetic_panel(fs, box, st);
                ImGui::Render();
            };
            frame();
            frame();
            ImGui::DestroyContext(ctx);
            ImGui::SetCurrentContext(nullptr);
        };

        // Seeding: an unset pin component seeds 0, not the previous subplot's.
        {
            AxesStyle seed;
            seed.xaxis_y = AxisPosition::Mid;
            seed.yaxis_x = AxisPosition::High;
            seed.origin_y = 3.5;
            seed.spine_top = false;

            PanelState st;
            st.origin_x_scratch = 99.0; // a stale value the seed must overwrite
            run(seed, st);

            check(st.axes_style_local.xaxis_y == AxisPosition::Mid &&
                  st.axes_style_local.yaxis_x == AxisPosition::High &&
                  !st.axes_style_local.spine_top,
                  "panel: the placements and spine flags reach the panel's scratch");
            check(st.axes_style_local.origin_y.has_value() &&
                  *st.axes_style_local.origin_y == 3.5,
                  "panel: and so does a pinned origin component");
            check(near_px(static_cast<float>(st.origin_y_scratch), 3.5f),
                  "panel: the pin's drag box is seeded with the pinned value");
            check(st.origin_x_scratch == 0.0,
                  "panel: an unset component seeds 0, not the last subplot's number");
        }

        // Wiring: sweep the Axis group and identify each control by its effect on
        // the pushed AxesStyle.
        {
            FigureSnapshot fs = one_line_snapshot({0.0, 1.0, 2.0}, {0.0, 1.0, 4.0});

            ImGuiContext* ctx = ImGui::CreateContext();
            ImGui::SetCurrentContext(ctx);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(480.0f, 3040.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();

            PanelState st;
            FigureEditBox box;
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(430.0f, 3000.0f));
                draw_cosmetic_panel(fs, box, st);
                // Close any combo popup so it doesn't cover later rows.
                if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                    ImGui::ClosePopupToLevel(0, false);
                ImGui::Render();
            };
            frame();
            frame();
            // Re-open the sections before every probe (a click on a header closes
            // it).
            auto open_sections = [&] {
                if (ImGuiWindow* w = ImGui::FindWindowByName("Cosmetic"))
                    for (const char* s: {"Figure", "Axis", "Ticks", "Legend & colorbar"})
                        w->StateStorage.SetInt(w->GetID(s), 1);
            };
            open_sections();
            frame();
            frame();

            bool bottom_off = false, top_off = false, left_off = false, right_off = false;
            bool pinned_x = false, pinned_y = false;
            auto all_found = [&] {
                return bottom_off && top_off && left_off && right_off && pinned_x && pinned_y;
            };
            // 6 px steps across: checkboxes are ~19 px in ~107 px cells. The early
            // exit keeps it cheap.
            for (float y = 2.0f; y < 3000.0f && !all_found(); y += 4.0f) {
                for (float x = 8.0f; x < 420.0f && !all_found(); x += 6.0f) {
                    open_sections();
                    io.MousePos = ImVec2(x, y);
                    io.MouseDown[0] = true;
                    frame();
                    io.MouseDown[0] = false;
                    frame();
                    auto e = box.load_and_clear();
                    if (!e || e->per_axes.empty()) continue;
                    for (const auto& pa: e->per_axes) {
                        if (!pa.second.axes_style) continue;
                        const AxesStyle& s = *pa.second.axes_style;
                        if (!s.spine_bottom) bottom_off = true;
                        if (!s.spine_top) top_off = true;
                        if (!s.spine_left) left_off = true;
                        if (!s.spine_right) right_off = true;
                        if (s.origin_x) pinned_x = true;
                        if (s.origin_y) pinned_y = true;
                    }
                }
            }
            if (!all_found())
                std::printf("    reached: bottom=%d top=%d left=%d right=%d pin_x=%d pin_y=%d\n",
                            bottom_off, top_off, left_off, right_off, pinned_x, pinned_y);
            ImGui::DestroyContext(ctx);
            ImGui::SetCurrentContext(nullptr);

            check(bottom_off && top_off && left_off && right_off,
                  "panel: all four spine checkboxes are reachable and each clears its own edge");
            check(pinned_x && pinned_y,
                  "panel: both origin pins are reachable and each engages its own component");
        }
    }

    // The 3D half: six placements and three origin components.
    void test_panel_axis_position_3d() {
        std::printf("\n[3D Cosmetic panel: axis position]\n");

        using namespace sextant;

        auto make = [] {
            FigureSnapshot fs;
            RenderSnapshot3D r;
            r.title = "Box";
            r.xtitle = "x";
            r.ytitle = "y";
            r.ztitle = "z";
            fs.axes.push_back({{1, 1, 1}, std::move(r)});
            fs.generation = fs.data_generation = 1;
            return fs;
        };

        // Seeding, including z; stale scratch values are overwritten.
        {
            FigureSnapshot fs = make();
            AxesStyle& seed = fs.axes[0].snap3d()->axes_style;
            seed.xaxis_z = AxisPosition::High;
            seed.zaxis_y = AxisPosition::Mid;
            seed.origin_z = 2.5;

            ImGuiContext* ctx = ImGui::CreateContext();
            ImGui::SetCurrentContext(ctx);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(480.0f, 3040.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();

            PanelState st;
            st.origin_z_scratch = 99.0;
            FigureEditBox box;
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(430.0f, 3000.0f));
                draw_cosmetic_panel(fs, box, st);
                ImGui::Render();
            };
            frame();
            frame();
            ImGui::DestroyContext(ctx);
            ImGui::SetCurrentContext(nullptr);

            check(st.axes_style_local.xaxis_z == AxisPosition::High &&
                  st.axes_style_local.zaxis_y == AxisPosition::Mid,
                  "3D panel: the z-involving placements reach the panel's scratch");
            check(near_px(static_cast<float>(st.origin_z_scratch), 2.5f),
                  "3D panel: and origin_z seeds its drag box");
        }

        // Wiring: sweep and identify by effect, re-opening sections per probe.
        {
            FigureSnapshot fs = make();

            ImGuiContext* ctx = ImGui::CreateContext();
            ImGui::SetCurrentContext(ctx);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(480.0f, 3040.0f);
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.Fonts->AddFontDefault();

            PanelState st;
            FigureEditBox box;
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(430.0f, 3000.0f));
                draw_cosmetic_panel(fs, box, st);
                if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                    ImGui::ClosePopupToLevel(0, false);
                ImGui::Render();
            };
            frame();
            frame();
            auto open_sections = [&] {
                if (ImGuiWindow* w = ImGui::FindWindowByName("Cosmetic"))
                    for (const char* s: {"View", "Figure", "Axis", "Ticks", "Legend & colorbar"})
                        w->StateStorage.SetInt(w->GetID(s), 1);
            };
            open_sections();
            frame();
            frame();

            bool pin_x = false, pin_y = false, pin_z = false;
            auto all_found = [&] { return pin_x && pin_y && pin_z; };
            for (float y = 2.0f; y < 3000.0f && !all_found(); y += 4.0f) {
                for (float x = 8.0f; x < 420.0f && !all_found(); x += 6.0f) {
                    open_sections();
                    io.MousePos = ImVec2(x, y);
                    io.MouseDown[0] = true;
                    frame();
                    io.MouseDown[0] = false;
                    frame();
                    auto e = box.load_and_clear();
                    // 3D cosmetics travel in per_axes3d.
                    if (!e || e->per_axes3d.empty()) continue;
                    for (const auto& pa: e->per_axes3d) {
                        if (!pa.second.axes_style) continue;
                        const AxesStyle& s = *pa.second.axes_style;
                        if (s.origin_x) pin_x = true;
                        if (s.origin_y) pin_y = true;
                        if (s.origin_z) pin_z = true;
                    }
                }
            }
            if (!all_found())
                std::printf("    reached: pin_x=%d pin_y=%d pin_z=%d\n", pin_x, pin_y, pin_z);
            ImGui::DestroyContext(ctx);
            ImGui::SetCurrentContext(nullptr);

            check(pin_x && pin_y && pin_z,
                  "3D panel: all three origin pins are reachable and each engages its own component");
        }
    }
} // namespace lt
