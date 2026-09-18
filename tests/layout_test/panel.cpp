// The panels themselves: Cosmetic's groups, duplicate ids, selection, tabs.
//
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

namespace lt {

// The Cosmetic panel, run through a real ImGui frame with no window and no
// backend -- the same null-backend arrangement the Data-panel check uses. What
// matters is the dispatch: a 3D slot must seed the panel's scratch from the 3D
// snapshot, which is observable in PanelState afterwards, and must not fall
// into the 2D path (whose sections would drive nothing).
void test_cosmetic_panel_3d() {
    std::printf("\n[3D: the Cosmetic panel]\n");

    using namespace sextant;

    FigureSnapshot fs;
    RenderSnapshot3D rs;
    rs.title  = "Box";
    rs.ztitle = "counts";
    rs.zmin = -4.0; rs.zmax = 9.0; rs.zlim_auto = false;
    rs.camera.azimuth = 17.0;
    rs.camera.zoom    = 1.5;
    rs.default_camera.azimuth = -60.0;
    rs.aspect = BoxAspect{ 2.0, 1.0, 1.0 };
    // Two planes with deliberately *different* placements, so the Planes rows
    // can be shown to be seeded per plane rather than all from the first one.
    rs.planes = two_plane_snapshot().planes;
    rs.planes[1].opts.alpha   = 0.25f;
    rs.planes[1].opts.visible = false;
    // A bar grid and a surface, likewise with distinct appearance, so the two
    // new sections' rows can be shown to be seeded per object (step 7d).
    {
        Bar3DPlot b = bar3d_grid();
        b.opts.color   = { 1.0f, 0.0f, 0.0f, 1.0f };
        b.opts.shading = 0.9f;
        rs.bars3d.push_back(std::move(b));
        SurfacePlot sp = ripple_surface();
        sp.opts.colormap = true;
        sp.opts.alpha    = 0.4f;
        sp.opts.edges    = true;
        rs.surfaces.push_back(std::move(sp));
    }
    fs.axes.push_back({ {1, 1, 1}, std::move(rs) });
    fs.generation = fs.data_generation = 1;

    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 900.0f);
    io.DeltaTime   = 1.0f / 60.0f;
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

    // The Planes rows, seeded positionally. A row that read its values from
    // the wrong plane would edit the wrong plane the moment it was dragged,
    // and would look entirely reasonable on screen -- which is why the two
    // planes here differ in every field the section shows.
    check(st.planes_local.size() == 2, "3D panel: one Planes row per plane");
    check(st.planes_local[0].orient == PlaneOrientation::XY &&
          st.planes_local[0].offset == 0.25 &&
          st.planes_local[0].opts.visible && st.planes_local[0].opts.alpha == 1.0f,
          "3D panel: the first row seeded from the first plane");
    check(st.planes_local[1].orient == PlaneOrientation::YZ &&
          st.planes_local[1].offset == 0.5 &&
          !st.planes_local[1].opts.visible && st.planes_local[1].opts.alpha == 0.25f,
          "3D panel: and the second from the second, not from the first again");

    // The two sections step 7d added, seeded the same way and on the same
    // rule -- and the bar3d one is the row step 6c recorded as missing.
    check(st.bars3d_local.size() == 1 && st.surfaces_local.size() == 1,
          "3D panel: one Bars row per bar3d grid, one Surfaces row per surface");
    check(st.bars3d_local[0].color.r == 1.0f && st.bars3d_local[0].shading == 0.9f,
          "3D panel: the Bars row is seeded from its own grid's options");
    check(st.surfaces_local[0].colormap && st.surfaces_local[0].alpha == 0.4f &&
          st.surfaces_local[0].edges,
          "3D panel: and the Surfaces row from its own surface's");

    check(!box.load_and_clear().has_value(),
          "3D panel: drawing it without touching anything pushes no edit");

    // An appearance edit round-trips, and does *not* carry `hint_labels` with
    // it. The panel republishes the whole options struct from a local copy
    // that is only re-seeded when the selection or the object count changes,
    // so a colour set after the Data panel added a grid line would otherwise
    // restore the labels to their old shape -- a control silently undoing a
    // different control.
    {
        FigureSnapshot target = fs;
        RenderSnapshot3D* t3 = target.axes[0].snap3d();
        t3->surfaces[0].opts.hint_labels.assign(t3->surfaces[0].count(), "kept");
        t3->bars3d[0].opts.hint_labels.assign(t3->bars3d[0].count(), "kept");

        AxesEdit3D e;
        SurfaceOptions so = t3->surfaces[0].opts;
        so.alpha = 0.15f;
        so.hint_labels.clear();               // as a stale panel copy would be
        e.surfaces.push_back({ 0, so });
        Bar3DOptions bo = t3->bars3d[0].opts;
        bo.shading = 0.1f;
        bo.hint_labels.clear();
        e.bars3d.push_back({ 0, bo });

        // The same call both threads make: the render thread patches the
        // snapshot with it and the caller thread patches Axes3D::Impl with it,
        // which is why it is one function in figure_edits.h rather than a
        // private of Figure::Impl.
        apply_axes3d_edit(*t3, e);

        check(t3->surfaces[0].opts.alpha == 0.15f && t3->bars3d[0].opts.shading == 0.1f,
              "3D panel: an appearance edit reaches the object it names");
        check(t3->surfaces[0].opts.hint_labels.size() == t3->surfaces[0].count() &&
              t3->bars3d[0].opts.hint_labels.size() == t3->bars3d[0].count(),
              "3D panel: and leaves hint_labels alone, because those are data and this lane is not");
    }

    // The Limits fields must show what the axis *reads*, not what was
    // declared. An axes on auto never uses its declared limits -- they stay at
    // 0..1 while the axis runs 400..600 -- and the panel was showing the
    // declared pair, so a bar3d over real units read "0 to 1" beside an axis
    // labelled in nanometres. The resolved numbers exist only in the layout,
    // on the render thread, so draw_plot_panel() stashes them in PanelState;
    // here that stash is filled by hand, since this panel runs with no frame
    // behind it.
    st.resolved.clear();
    st.resolved.push_back({ 1, true, 400.0, 600.0, -1.5, 1.5, 0.0, 21.0 });
    st.last_synced_slot = -1;             // force a re-seed, as a slot change would
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

    // ...and only those. The z limits on this snapshot were set explicitly, so
    // the declared pair is what the axis reads and the resolved one must be
    // ignored -- otherwise the field would fight whatever the caller pinned.
    check(st.zmin_local == -4.0 && st.zmax_local == 9.0,
          "3D panel: while an axis with explicit limits keeps them");

    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(nullptr);
}

// Step 10.4: the 3D Cosmetic panel is groups -- View, Figure, Axis, Ticks, and
// since step 11.2 Legend & colorbar -- each a CollapsingHeader over what used
// to be separate sections. Asserted
// through what opening a header *does* to the drawn panel, which is the only
// thing a header is: each group, opened alone, draws more than all of them
// closed, and each old section name, "opened" alone, changes nothing, because
// it is a SeparatorText sub-heading inside a group now and not a header.
//
// Step 10.5 gave the 2D panel the same treatment, so the body is shared: `tag`
// prefixes the check names, `groups` are the headers that must open onto
// contents, `old` the former section names that must no longer be headers.
void check_cosmetic_groups(const sextant::FigureSnapshot& fs, const char* tag,
                           std::initializer_list<const char*> groups,
                           std::initializer_list<const char*> old) {
    using namespace sextant;

    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(470.0f, 3040.0f);
    io.DeltaTime   = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();

    PanelState st;
    FigureEditBox box;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        // Tall enough that no group's contents are clipped away unsubmitted.
        ImGui::SetNextWindowSize(ImVec2(430.0f, 3000.0f));
        draw_cosmetic_panel(fs, box, st);
        ImGui::Render();
        return ImGui::GetDrawData()->TotalVtxCount;
    };
    frame(); frame();

    char name[160];
    auto label = [&](const char* what) {
        std::snprintf(name, sizeof(name), "%s groups: %s", tag, what);
        return name;
    };
    ImGuiWindow* w = ImGui::FindWindowByName("Cosmetic");
    check(w != nullptr, label("the panel window is called Cosmetic"));
    // Every name closed, then `name` alone open -- written into the window's
    // storage, which is where a CollapsingHeader keeps its open flag.
    auto only = [&](const char* name) {
        for (const char* g : groups) w->StateStorage.SetInt(w->GetID(g), 0);
        for (const char* o : old)    w->StateStorage.SetInt(w->GetID(o), 0);
        if (name) w->StateStorage.SetInt(w->GetID(name), 1);
        frame();
        return frame();
    };

    if (w) {
        const int closed = only(nullptr);
        bool groups_open = true;
        for (const char* g : groups)
            if (only(g) <= closed) {
                groups_open = false;
                std::printf("    group \"%s\" drew nothing when opened\n", g);
            }
        check(groups_open, label("each group is a header over contents"));

        bool old_gone = true;
        for (const char* o : old)
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
    r.title = "Box"; r.xtitle = "x"; r.ytitle = "y"; r.ztitle = "z";
    fs.axes.push_back({ {1, 1, 1}, std::move(r) });
    fs.generation = fs.data_generation = 1;

    // "Legend & colorbar" is the fifth, from step 11.2. It is the *same*
    // group the 2D panel draws -- one templated definition -- so "Legend" and
    // "Colorbar" have to be sub-headings here exactly as they are there.
    check_cosmetic_groups(fs, "3D", { "View", "Figure", "Axis", "Ticks",
                                      "Legend & colorbar" },
                          { "Camera", "Box", "Text", "Limits", "Layout",
                            "Ticks & labels", "Grid", "Axis frame",
                            "Legend", "Colorbar" });
}

// Step 10.5: the 2D panel in the 3D panel's groups, less View -- a 2D axes has
// no camera or box -- and plus one for the two keys 3D has no controls for.
void test_cosmetic_groups_2d() {
    std::printf("\n[2D Cosmetic panel: four groups]\n");

    const auto fs = one_line_snapshot({ 0.0, 1.0, 2.0 }, { 0.0, 1.0, 4.0 });
    check_cosmetic_groups(fs, "2D", { "Figure", "Axis", "Ticks", "Legend & colorbar" },
                          { "Text", "Layout", "Limits", "Ticks & labels",
                            "Grid", "Axis frame", "Legend", "Colorbar" });
}

// Duplicate ImGui ids, which are invisible in ordinary use and break the
// widget rather than the drawing: an id is hashed from a label, so two items
// sharing one means clicking either activates the same one. ImGui detects it,
// but only under two conditions that together are why the last instance
// reached a user rather than a test -- it reports the conflict only for the
// item *under the cursor*, and a closed CollapsingHeader never submits its
// contents at all. So this forces every section open and then sweeps the
// cursor down the whole panel.
//
// Two frames per cursor position on purpose: the count of items sharing the
// hovered id is gathered during one frame and inspected on the next.
struct IdConflictScan {
    int      probes    = 0;
    int      conflicts = 0;
    ImGuiID  first     = 0;
};

IdConflictScan scan_panel_for_id_conflicts(
        const sextant::FigureSnapshot& fsnap,
        void (*draw)(const sextant::FigureSnapshot&, sextant::FigureEditBox&, sextant::PanelState&),
        const char* window_name,
        std::initializer_list<const char*> sections)
{
    // Tall enough to submit every section at once. A clipped item is never
    // submitted, so a window shorter than the fully-expanded panel simply
    // hides the bottom sections from this scan -- which is the failure mode
    // that makes a check like this quietly stop covering what it claims to.
    constexpr float W = 430.0f, H = 3000.0f;

    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(W + 40.0f, H + 40.0f);
    io.DeltaTime   = 1.0f / 60.0f;
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

    // Settle, then open every section: a section left closed submits none of
    // its widgets, and so hides exactly the conflicts this is looking for.
    frame();
    frame();
    // Written straight into the window's own storage, which is where a
    // CollapsingHeader keeps its open flag. Not TreeNodeSetOpen(): that
    // reaches through g.CurrentWindow, which is null between frames.
    if (ImGuiWindow* w = ImGui::FindWindowByName(window_name))
        for (const char* s : sections)
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

    // Every section either panel can draw. Naming them here rather than
    // discovering them is deliberate: a section added without a line here is
    // a section this check silently stops covering.
    // The groups of both panels (steps 10.4 and 10.5); the old section names
    // now head SeparatorText sub-headings, which have no id and need no
    // opening. Figure, Axis and Ticks are in both.
    const std::initializer_list<const char*> sections = {
        "View", "Figure", "Axis", "Ticks", "Legend & colorbar",
    };

    FigureSnapshot fs3;
    RenderSnapshot3D r3;
    r3.title = "Box"; r3.xtitle = "x"; r3.ytitle = "y"; r3.ztitle = "z";
    // Two planes, because the Planes section draws nothing without them and
    // because its rows are what a per-plane id has to stay distinct across --
    // two "Visible" checkboxes under one header is exactly the shape this
    // scan exists to catch.
    r3.planes = two_plane_snapshot().planes;
    fs3.axes.push_back({ {1, 1, 1}, std::move(r3) });
    fs3.generation = fs3.data_generation = 1;

    const IdConflictScan s3 = scan_panel_for_id_conflicts(
        fs3, &draw_cosmetic_panel, "Cosmetic", sections);
    check(s3.conflicts == 0,
          "3D panel: no two widgets share an id (a checkbox labelled like its own header would)");
    if (s3.conflicts)
        std::printf("    3D: %d of %d cursor positions reported a conflict, first id %u\n",
                    s3.conflicts, s3.probes, static_cast<unsigned>(s3.first));

    // The 2D panel through the same scan: it is the older and larger of the
    // two, and nothing had ever checked it.
    const auto fs2 = one_line_snapshot({ 0.0, 1.0, 2.0 }, { 0.0, 1.0, 4.0 });
    const IdConflictScan s2 = scan_panel_for_id_conflicts(
        fs2, &draw_cosmetic_panel, "Cosmetic", sections);
    check(s2.conflicts == 0, "2D panel: likewise");
    if (s2.conflicts)
        std::printf("    2D: %d of %d cursor positions reported a conflict, first id %u\n",
                    s2.conflicts, s2.probes, static_cast<unsigned>(s2.first));

    // The Data panel over the same two-plane slot. Its tab ids come from the
    // plot labels, and two planes each holding a "line 0" is precisely the
    // collision the "P0 "/"P1 " prefix and the "##i" suffix are there to
    // prevent -- so it is worth sweeping rather than assuming.
    const IdConflictScan sd = scan_panel_for_id_conflicts(
        fs3, &draw_data_panel, "Data", {});
    check(sd.conflicts == 0,
          "Data panel: two planes holding objects of the same name keep distinct ids");
    if (sd.conflicts)
        std::printf("    Data: %d of %d cursor positions reported a conflict, first id %u\n",
                    sd.conflicts, sd.probes, static_cast<unsigned>(sd.first));

    // Again with a bar3d grid on the slot, which selects the first tab and so
    // draws the third table shape. Worth its own sweep: it has three frozen
    // header rows and a coordinate column in the gutter, all of them ids in a
    // table whose headers ImGui already pushes an id per column for.
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

    // A surface alone, so its tab is the one selected and its Appearance block
    // (step 10.3) is drawn open above its table.
    FigureSnapshot fss;
    {
        RenderSnapshot3D r;
        r.surfaces.push_back(ripple_surface());
        fss.axes.push_back({ {1, 1, 1}, std::move(r) });
        fss.generation = fss.data_generation = 1;
    }
    const IdConflictScan ss = scan_panel_for_id_conflicts(
        fss, &draw_data_panel, "Data", {});
    check(ss.conflicts == 0,
          "Data panel: and a surface's tab, with its Appearance block above the table");
    if (ss.conflicts)
        std::printf("    surface: %d of %d cursor positions reported a conflict, first id %u\n",
                    ss.conflicts, ss.probes, static_cast<unsigned>(ss.first));

    // A path alone, for the same reason (v1.0 step 13.5): its Appearance block
    // is a new set of ids, and without a sweep of its own the block would be
    // drawn by nothing this check ever opens.
    FigureSnapshot fsl;
    {
        RenderSnapshot3D r;
        Line3DPlot l;
        l.x = std::vector<double>{ 0.0, 1.0, 2.0 };
        l.y = std::vector<double>{ 0.0, 2.0, 1.0 };
        l.z = std::vector<double>{ 0.0, 1.0, 3.0 };
        l.colors = std::vector<double>{ 1.0, 2.0, 3.0 };   // so vmin/vmax are drawn
        r.lines3d.push_back(std::move(l));
        fsl.axes.push_back({ {1, 1, 1}, std::move(r) });
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


// Step 10.2: the subplot selector left the panels for the menu bar, and a
// click on a subplot selects it. Everything the click does is decided by
// update_plot_selection() off a PlotPointer, so the whole gesture vocabulary
// -- click, drag, click-then-double-click, press here and release there -- is
// assertable frame by frame with no window and no ImGui context at all.
void test_subplot_selection() {
    std::printf("\n[subplot selection: menu combo + click-to-select]\n");

    using namespace sextant;

    // Slot 1 is 2D, slot 2 is 3D with a camera nothing else has -- so a
    // camera_local seeded from the wrong slot, or not seeded at all, shows.
    FigureSnapshot fs = one_line_snapshot({ 0.0, 1.0, 2.0 }, { 0.0, 1.0, 4.0 });
    fs.axes[0].slot = AxesSlot{ 1, 2, 1 };
    {
        RenderSnapshot3D rs;
        rs.title = "box";
        rs.camera.azimuth = 71.0;
        rs.camera.zoom    = 2.5;
        rs.default_camera = rs.camera;
        fs.axes.push_back({ AxesSlot{ 1, 2, 2 }, std::move(rs) });
    }

    // ---- The cell rect: the grid's share, and the frame lies inside it.
    const FigureLayout fl = compute_figure_layout(fs, 800, 400);
    check(fl.cells.size() == 2, "select: one cell per axes");
    std::vector<AxesLayout> layout;
    for (const CellLayout& c : fl.cells) {
        const PlotRect& r = c.cell;
        check(c.frame.x >= r.x && c.frame.y >= r.y &&
              c.frame.x + c.frame.w <= r.x + r.w + 1e-3f &&
              c.frame.y + c.frame.h <= r.y + r.h + 1e-3f,
              "select: the frame lies inside the cell it was carved from");
        layout.push_back({ c.slot, c.tr,
                           c.box3d ? std::optional<Projector3D>(c.box3d->proj) : std::nullopt,
                           c.cell });
    }
    check(layout[0].cell.x + layout[0].cell.w <= layout[1].cell.x,
          "select: the two cells of a 1x2 grid do not overlap");

    const PlotRect c1 = layout[0].cell, c2 = layout[1].cell;
    // Just inside the cell but outside its frame -- where a tick label or
    // the title is. The whole cell answers, not only the frame.
    const float in1x = c1.x + 2.0f, in1y = c1.y + 2.0f;
    const float in2x = c2.x + c2.w - 2.0f, in2y = c2.y + c2.h - 2.0f;
    check(find_cell_at(layout, in1x, in1y) == &layout[0] &&
          find_cell_at(layout, in2x, in2y) == &layout[1],
          "select: a point in a cell's decorations finds that cell, not just its frame");
    check(find_cell_at(layout, c1.x - 1.0f, in1y) == nullptr &&
          find_cell_at(layout, in1x, c1.y + c1.h + 1.0f) == nullptr,
          "select: a margin belongs to no cell");

    // Cells never overlap -- a Figure has one grid and refuses a subplot on
    // an occupied cell -- so there is no tie for find_cell_at() to break.
    // Spans, whose cells take in gaps, are checked in test_subplot_spans().

    PanelState st;
    auto frame = [&](float x, float y, auto&& set) {
        PlotPointer in;
        in.x = x; in.y = y;
        in.hovered = true;
        set(in);
        return update_plot_selection(st, fs, layout, in);
    };
    auto idle    = [](PlotPointer&) {};
    auto press   = [](PlotPointer& p) { p.pressed = p.active = true; };
    auto dpress  = [](PlotPointer& p) { p.pressed = p.active = p.double_clicked = true; };
    auto hold    = [](PlotPointer& p) { p.active = true; };
    auto release = [](PlotPointer& p) { p.released = true; };
    auto drop    = [](PlotPointer& p) { p.released = p.dragged = true; };

    // ---- The first frame seeds the scratch fields from slot 1 with no
    // panel drawn at all -- the stale-camera bug began exactly there.
    frame(in1x, in1y, idle);
    check(st.selected_slot_index == 1 && st.last_synced_slot == 1,
          "select: the selection is synced before any panel draws");

    // ---- Hovering the other cell lets nothing through to navigation.
    PlotNavGate g = frame(in2x, in2y, idle);
    check(!g.wheel && !g.keys, "select: the wheel and keys ignore an unselected cell");
    g = frame(in1x, in1y, idle);
    check(g.wheel && g.keys, "select: and act over the selected one, decorations included");

    // ---- A drag that starts on an unselected cell neither navigates nor
    // selects.
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

    // ---- A click selects on release, and re-seeds the scratch state right
    // there: the 3D camera, not slot 1's default.
    frame(in2x, in2y, press);
    check(st.selected_slot_index == 1, "select: still not on the press");
    frame(in2x, in2y, release);
    check(st.selected_slot_index == 2, "select: a click selects on its release");
    check(st.last_synced_slot == 2 && st.camera_local.azimuth == 71.0 &&
          st.camera_local.zoom == 2.5,
          "select: and the camera navigation starts from is the new slot's own");

    // ---- The double-click that completes the selecting click is a way of
    // picking the cell, not of resetting it...
    g = frame(in2x, in2y, dpress);
    check(!g.reset, "select: a double-click whose first click selected does not reset");
    check(g.drag, "select: but the cell is selected now, so the press may navigate");
    frame(in2x, in2y, release);

    // ...while a double-click on a cell already selected is.
    frame(in2x, in2y, press);
    frame(in2x, in2y, release);
    g = frame(in2x, in2y, dpress);
    check(g.reset, "select: a double-click on the already-selected cell resets it");
    frame(in2x, in2y, release);

    // ---- A drag on the selected cell navigates, and keeps going off it.
    frame(in2x, in2y, press);
    g = frame(in1x, in1y, hold);
    check(g.drag && g.keys && !g.wheel,
          "select: a drag begun on the selected cell keeps it when the cursor leaves");
    frame(in1x, in1y, drop);
    check(st.selected_slot_index == 2, "select: and does not select where it ends");

    // ---- The menu's path: select_slot() re-seeds exactly as a click does.
    select_slot(st, fs, 1);
    check(st.selected_slot_index == 1 && st.last_synced_slot == 1 &&
          std::string(st.title_buf).empty(),
          "select: select_slot() moves the selection and re-seeds from that slot");
    select_slot(st, fs, 2);
    check(std::string(st.title_buf) == "box", "select: in either direction");

    // ---- A selection naming a slot that does not exist is normalized to
    // the first, as the panels always displayed -- File > Resize and the
    // Save dialog read the index too.
    PanelState st2;
    st2.selected_slot_index = 7;
    check(sync_selected_slot(st2, fs) == 1 && st2.selected_slot_index == 1,
          "select: a missing slot falls back to the first one");
    check(sync_selected_slot(st2, FigureSnapshot{}) == -1, "select: and no axes is no slot");
}

// Step 10.3: a 3D object's controls live in its own tab of the Data panel --
// planes, bar grids and surfaces all -- and the Cosmetic panel no longer has
// them. Three claims: every plane gets a tab even with nothing on it, in
// order; an edit made in each kind of tab reaches the appearance lane at the
// right index; and the scratch copies those tabs edit are re-seeded on an
// object-count change with no Cosmetic panel drawn at all.
void test_data_panel_object_tabs() {
    std::printf("\n[Data panel: a 3D object's controls are in its own tab]\n");

    using namespace sextant;

    // ---- The tab list, by hand: a bar grid on the axes, nothing on plane 0,
    // two objects on plane 1, nothing on plane 2.
    {
        std::vector<PlotDataTable> tables(3);
        tables[0].kind = PlotKind::Bar3D; tables[0].plot_index = 0; tables[0].plane_index = -1;
        tables[1].kind = PlotKind::Line;  tables[1].plot_index = 0; tables[1].plane_index = 1;
        tables[2].kind = PlotKind::Line;  tables[2].plot_index = 1; tables[2].plane_index = 1;
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

    // Drives the Data panel alone, with no Cosmetic panel at all, and sweeps
    // clicks down it until an appearance edit matching `want` arrives. Popups
    // (a colour picker, a combo) are closed as they open, or one would swallow
    // every click after it.
    auto sweep_for = [](const FigureSnapshot& fs, PanelState& st,
                        auto&& want) -> std::optional<AxesEdit3D> {
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(560.0f, 900.0f);
        io.DeltaTime   = 1.0f / 60.0f;
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
        frame(); frame(); frame();

        std::optional<AxesEdit3D> got;
        // Below the format row (whose combo would open a popup on every
        // click) and down through the tab's controls, which sit above its
        // table.
        for (float y = 90.0f; y < 600.0f && !got; y += 4.0f) {
            for (float x = 8.0f; x < 400.0f && !got; x += 6.0f) {
                io.MousePos = ImVec2(x, y);
                io.MouseDown[0] = true;  frame();
                io.MouseDown[0] = false; frame();
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
        fs.axes.push_back({ { 1, 1, 1 }, std::move(r) });
        fs.generation = fs.data_generation = 1;
        return fs;
    };

    // ---- An empty plane: its own tab is the only one, and reachable. It
    // used to contribute nothing to this panel at all.
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

    // ---- A bar grid's tab: its Appearance block, on the bar lane, index 0.
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

    // ---- A cloud's tab, likewise -- and it is the one 3D object whose tab
    // carries a *vector* table under its Appearance block rather than a grid.
    {
        RenderSnapshot3D r;
        Scatter3DPlot c;
        c.x = std::vector<double>{ 1.0, 2.0, 3.0 };
        c.y = std::vector<double>{ 4.0, 5.0, 6.0 };
        c.z = std::vector<double>{ 7.0, 8.0, 9.0 };
        r.scatter3d.push_back(std::move(c));
        const FigureSnapshot fs = wrap(std::move(r));
        PanelState st;
        const auto e = sweep_for(fs, st, [](const AxesEdit3D& a) { return !a.scatter3d.empty(); });
        check(e && e->scatter3d[0].plot_index == 0 && e->plot_ops.empty(),
              "object tabs: and a cloud's, on the scatter3d lane");
    }

    // ---- The scratch copies follow an object-count change with no Cosmetic
    // panel drawn: an edit from a tab would otherwise land on whichever
    // object now sits at the old index.
    {
        RenderSnapshot3D r;
        Bar3DPlot red = bar3d_grid();
        red.opts.color = { 1.0f, 0.0f, 0.0f, 1.0f };
        r.bars3d.push_back(red);
        FigureSnapshot fs = wrap(std::move(r));
        PanelState st;
        sync_selected_slot(st, fs);
        check(st.bars3d_local.size() == 1 && st.bars3d_local[0].color.r == 1.0f,
              "object tabs: the copies are seeded from the snapshot");

        Bar3DPlot blue = bar3d_grid();
        blue.opts.color = { 0.0f, 0.0f, 1.0f, 1.0f };
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

// HiDPI chrome scaling. The panel used to measure the monitor's content scale
// once, in ImGuiPanelContext's constructor, and bake the font at 13 * that --
// so a figure opened on the primary display (which is where the OS puts a new
// window, and which on the reporting machine is the 1080p 100% one) stayed at
// scale 1.0 forever, including after being dragged onto a 4K 150% display.
// That is the whole of "GUI elements are too small on a high dpi monitor".
//
// The scale is now re-derived every frame by sync_dpi_scale(), which means
// apply_panel_style() is called repeatedly rather than once -- and that is the
// part worth pinning here, because ImGuiStyle::ScaleAllSizes() *multiplies the
// style it is called on* (and accumulates into _MainScale), while
// StyleColorsX() writes only Colors[]. Rescaling the live style would compound
// 1.5 into 2.25 on the second frame and keep going. Asserted through
// behaviour, not by re-deriving imgui's own arithmetic: same scale twice is
// the same style, and scaling up then back down returns exactly to where it
// started -- which is what dragging a window across and back does.
void test_panel_dpi_scale() {
    std::printf("\n[panel: HiDPI chrome scaling]\n");

    using namespace sextant;

    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGui::GetIO().Fonts->AddFontDefault();

    // The size fields a cramped panel would show up in, sampled rather than
    // enumerated: enough that a wrong factor cannot hide, few enough to name.
    auto sample = [] {
        const ImGuiStyle& s = ImGui::GetStyle();
        return std::vector<float>{
            s.WindowPadding.x, s.WindowPadding.y, s.FramePadding.x, s.FramePadding.y,
            s.ItemSpacing.x,   s.ItemSpacing.y,   s.ItemInnerSpacing.x,
            s.IndentSpacing,   s.ScrollbarSize,   s.GrabMinSize,
            s.CellPadding.x,   s.CellPadding.y,   s.WindowMinSize.x,
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

    // Every size field is the truncated 1.5x of its own unscaled value.
    // Truncated, not scaled: ScaleAllSizes uses ImTrunc, so FramePadding.y
    // goes 3 -> 4 rather than to 4.5, and a check written as "1.5x" would
    // fail on the fields that lose a half.
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

    // The compounding guard. A second call at the same scale must change
    // nothing; against a rescale of the live style this is 2.25x and the panel
    // grows without bound, one frame at a time.
    apply_panel_style(PanelTheme::Light, 1.5f);
    check(sample() == at_15x,
          "dpi: applying the same scale twice is the same style (no compounding)");

    // And back down, which is the drag back onto the 100% monitor.
    apply_panel_style(PanelTheme::Light, 1.0f);
    check(sample() == at_1x,
          "dpi: 1.5x then 1.0x returns exactly to the unscaled style");

    // The rescale must not silently reset the theme -- it rebuilds the whole
    // style, so the colours have to be re-derived from the stored theme rather
    // than left at ImGuiStyle's own Dark default.
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
                static_cast<double>(at_1x[0]),  static_cast<double>(at_15x[0]),
                static_cast<double>(at_1x[8]),  static_cast<double>(at_15x[8]),
                static_cast<double>(at_1x[3]),  static_cast<double>(at_15x[3]));

    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(nullptr);
}

// The per-plot-object appearance lane (v1.0 step 11.6). The 3D kinds have had
// one since step 7d; the 2D ones had none at all, so a `LinePlot`'s or a
// `HeatmapPlot`'s options could not be edited from any panel -- and neither
// could a plane's, since `PlaneEdit` carries only the plane's placement. What
// forced it closed is that `colorbar` and `show_legend` are per-object flags
// with nowhere to be toggled from.
//
// The thing worth pinning hardest is that one body serves both sides of the
// thread boundary: the caller thread writes `Axes::Impl`, the render thread
// patches a published `RenderSnapshot`, and a lane that meant two different
// things there is a control that works until the next refresh().
void test_plot_style_lane() {
    std::printf("\n[the per-plot-object style lane]\n");

    using namespace sextant;

    // ---- A 2D target, addressed at the axes itself.
    {
        RenderSnapshot s;
        LinePlot lp;
        lp.x = CowVec<double>{ std::vector<double>{ 0.0, 1.0 } };
        lp.y = CowVec<double>{ std::vector<double>{ 0.0, 1.0 } };
        lp.opts.name = "series";
        lp.opts.hint_labels = { "a", "b" };
        s.lines.push_back(lp);

        HeatmapPlot hp;
        hp.rows = 2; hp.cols = 2;
        hp.data = CowVec<float>{ std::vector<float>(4, 0.5f) };
        s.heatmaps.push_back(hp);

        LineOptions lo = s.lines[0].opts;
        lo.show_legend = false;
        lo.hint_labels.clear();           // a stale copy, as the panel's would be
        HeatmapOptions ho = s.heatmaps[0].opts;
        ho.colorbar = true;

        apply_plot_style_edits(s, { { 0, -1, lo }, { 0, -1, ho } });

        check(!s.lines[0].opts.show_legend && s.heatmaps[0].opts.colorbar,
              "style lane: an edit reaches the object the variant names");
        check(s.lines[0].opts.name == "series",
              "style lane: carrying the rest of the struct with it");
        check(s.lines[0].opts.hint_labels.size() == 2,
              "style lane: but NOT hint_labels, which are data the table owns -- a stale "
              "copy riding an appearance edit would silently revert them");

        // A stale index is skipped rather than clamped onto its neighbour,
        // which is the rule every plot index on this channel follows.
        LineOptions gone = lo;
        gone.name = "should not appear";
        apply_plot_style_edits(s, { { 7, -1, gone } });
        check(s.lines[0].opts.name == "series",
              "style lane: an index naming no object is skipped, not clamped");

        // And a plane-addressed edit is not reachable from a 2D axes.
        LineOptions elsewhere = lo;
        elsewhere.name = "on a plane";
        apply_plot_style_edits(s, { { 0, 1, elsewhere } });
        // Asserted against the plane edit's own text rather than against the
        // original, so a fault in the check above cannot cascade into this one.
        check(s.lines[0].opts.name != "on a plane",
              "style lane: a 2D target skips a plane-addressed edit");
    }

    // ---- A 3D target: the same lane, addressed at a plane. Plane -1 is
    // skipped here, because what sits directly on a 3D axes is bar3d grids and
    // surfaces, which have lanes of their own.
    {
        RenderSnapshot3D r;
        PlaneSnapshot pl;
        LinePlot lp;
        lp.x = CowVec<double>{ std::vector<double>{ 0.0, 1.0 } };
        lp.y = CowVec<double>{ std::vector<double>{ 0.0, 1.0 } };
        lp.opts.name = "on the plane";
        pl.sheet.lines.push_back(lp);
        r.planes.push_back(std::move(pl));

        LineOptions lo = r.planes[0].sheet.lines[0].opts;
        lo.show_legend = false;
        AxesEdit3D e;
        e.plot_styles.push_back({ 0, 0, lo });
        apply_axes3d_edit(r, e);
        check(!r.planes[0].sheet.lines[0].opts.show_legend,
              "style lane: a plane-addressed edit reaches that plane's own object");

        // A plane that has gone is skipped, not clamped onto its neighbour.
        AxesEdit3D stale;
        LineOptions relabel = lo;
        relabel.name = "nowhere";
        stale.plot_styles.push_back({ 0, 4, relabel });
        apply_axes3d_edit(r, stale);
        check(r.planes[0].sheet.lines[0].opts.name == "on the plane",
              "style lane: and a plane index naming no plane is skipped");
    }

    // ---- One body, both sides of the thread boundary. The same edit goes
    // through a real FigureEditBox drain and onto a published RenderSnapshot,
    // which is the half that would silently revert on the next refresh() if
    // the two sides ever meant different things.
    {
        RenderSnapshot published;
        LinePlot lp;
        lp.x = CowVec<double>{ std::vector<double>{ 0.0, 1.0 } };
        lp.y = CowVec<double>{ std::vector<double>{ 0.0, 1.0 } };
        lp.opts.name = "series";
        published.lines.push_back(lp);

        LineOptions off;
        off.name = "series";
        off.show_legend = false;

        FigureEditBox box;
        box.update(1, [&](AxesEdit& e) { e.plot_styles.push_back({ 0, -1, off }); });
        auto drained = box.load_and_clear();
        check(drained && !drained->per_axes.empty(),
              "style lane: the edit survives a FigureEditBox drain");

        if (drained && !drained->per_axes.empty()) {
            apply_plot_style_edits(published, drained->per_axes[0].second.plot_styles);
            check(!published.lines[0].opts.show_legend,
                  "style lane: and the render thread's snapshot takes it");
        }
    }

    // ---- The Data panel actually emits one. Drives the panel with no
    // Cosmetic panel at all, sweeping clicks over the Appearance block above
    // the table, as test_data_panel_object_tabs() does for a 3D object.
    {
        FigureSnapshot fs = one_line_snapshot({ 0.0, 1.0, 2.0 }, { 0.0, 1.0, 4.0 });
        fs.axes[0].snap2d()->lines[0].opts.name = "series";

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(560.0f, 900.0f);
        io.DeltaTime   = 1.0f / 60.0f;
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
        frame(); frame(); frame();

        // Two things to find: the legend checkbox, and the Name field above it
        // -- typed into rather than clicked, since an InputText emits nothing
        // on a bare click.
        bool emitted = false, off = false, named = false;
        for (float y = 90.0f; y < 400.0f && !(emitted && named); y += 4.0f) {
            for (float x = 8.0f; x < 400.0f && !(emitted && named); x += 6.0f) {
                io.MousePos = ImVec2(x, y);
                io.MouseDown[0] = true;  frame();
                io.MouseDown[0] = false; frame();
                // A click that landed in a text field leaves it active; typing
                // is what makes it emit, so type into whatever is now active.
                io.AddInputCharacter(static_cast<unsigned>(0x5A));   // 'Z'
                frame();
                auto e = box.load_and_clear();
                if (!e || e->per_axes.empty()) continue;
                for (const auto& ps : e->per_axes[0].second.plot_styles)
                    if (const auto* o = std::get_if<LineOptions>(&ps.opts)) {
                        if (o->name.find('Z') != std::string::npos) named = true;
                        else { emitted = true; off = !o->show_legend; }
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

// The Cosmetic panel's axis-position controls (v1.0 step 19): the two combos,
// the two origin pins and the four spine checkboxes. That they carry no
// duplicate ids is already covered by test_panel_id_conflicts(), which forces
// every section open and sweeps the whole panel; what is left, and what is new
// state rather than new drawing, is the scratch the optional pins need and the
// wiring from each control to an emitted AxesStyle.
void test_panel_axis_position() {
    std::printf("\n[Cosmetic panel: axis position]\n");

    using namespace sextant;

    // The invariant axis_position_combo() rests on: its name list is written
    // in the enum's own order, so a name and a placement drift apart the
    // moment an enumerator is inserted rather than appended.
    check(static_cast<int>(AxisPosition::Auto) == 0 &&
          static_cast<int>(AxisPosition::Low)  == 1 &&
          static_cast<int>(AxisPosition::Mid)  == 2 &&
          static_cast<int>(AxisPosition::High) == 3,
          "panel: AxisPosition is in the order the combo names it");

    auto run = [](const AxesStyle& seed, PanelState& st) {
        FigureSnapshot fs = one_line_snapshot({ 0.0, 1.0, 2.0 }, { 0.0, 1.0, 4.0 });
        fs.axes[0].snap2d()->axes_style = seed;

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(480.0f, 3040.0f);
        io.DeltaTime   = 1.0f / 60.0f;
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
        frame(); frame();
        ImGui::DestroyContext(ctx);
        ImGui::SetCurrentContext(nullptr);
    };

    // Seeding: the panel reads the new fields, and the two pin scratches come
    // from the published optionals. An unset component seeds 0 rather than
    // being left at whatever the last selected subplot had.
    {
        AxesStyle seed;
        seed.xaxis_y = AxisPosition::Mid;
        seed.yaxis_x = AxisPosition::High;
        seed.origin_y = 3.5;
        seed.spine_top = false;

        PanelState st;
        st.origin_x_scratch = 99.0;   // a stale value the seed must overwrite
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

    // Wiring: sweep the Axis group and click, then read what the panel
    // emitted. Each control is looked for by its *effect* on the pushed
    // AxesStyle rather than by position, so the checks survive the section
    // being re-ordered.
    {
        FigureSnapshot fs = one_line_snapshot({ 0.0, 1.0, 2.0 }, { 0.0, 1.0, 4.0 });

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(480.0f, 3040.0f);
        io.DeltaTime   = 1.0f / 60.0f;
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
            // A click can land on a combo; leaving its popup open would cover
            // the rows below it for the rest of the sweep.
            if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                ImGui::ClosePopupToLevel(0, false);
            ImGui::Render();
        };
        frame(); frame();
        // Re-asserted before every probe, not once up front: a click that
        // lands on a CollapsingHeader closes it, and a closed group submits
        // none of its widgets -- so one stray hit would hide the rest of the
        // panel for the whole sweep, which is exactly what a coarser sweep
        // got away with by hitting the headers less often.
        auto open_sections = [&] {
            if (ImGuiWindow* w = ImGui::FindWindowByName("Cosmetic"))
                for (const char* s : { "Figure", "Axis", "Ticks", "Legend & colorbar" })
                    w->StateStorage.SetInt(w->GetID(s), 1);
        };
        open_sections();
        frame(); frame();

        bool bottom_off = false, top_off = false, left_off = false, right_off = false;
        bool pinned_x = false, pinned_y = false;
        auto all_found = [&] {
            return bottom_off && top_off && left_off && right_off && pinned_x && pinned_y;
        };
        // 6 px across, not 24: a checkbox is only about 19 px wide and sits at
        // the left of a table cell roughly 107 px wide, so a coarse step falls
        // between the four of them. The early exit is what keeps that
        // resolution affordable -- everything here is in the Axis group, near
        // the top of the panel.
        for (float y = 2.0f; y < 3000.0f && !all_found(); y += 4.0f) {
            for (float x = 8.0f; x < 420.0f && !all_found(); x += 6.0f) {
                open_sections();
                io.MousePos = ImVec2(x, y);
                io.MouseDown[0] = true;  frame();
                io.MouseDown[0] = false; frame();
                auto e = box.load_and_clear();
                if (!e || e->per_axes.empty()) continue;
                for (const auto& pa : e->per_axes) {
                    if (!pa.second.axes_style) continue;
                    const AxesStyle& s = *pa.second.axes_style;
                    if (!s.spine_bottom) bottom_off = true;
                    if (!s.spine_top)    top_off    = true;
                    if (!s.spine_left)   left_off   = true;
                    if (!s.spine_right)  right_off  = true;
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

// The 3D half of the same section (v1.0 step 20). Six placements rather than
// two, because a 3D axis's position is two numbers; three origin components
// rather than two, shared between the axes that measure against them.
void test_panel_axis_position_3d() {
    std::printf("\n[3D Cosmetic panel: axis position]\n");

    using namespace sextant;

    auto make = [] {
        FigureSnapshot fs;
        RenderSnapshot3D r;
        r.title = "Box"; r.xtitle = "x"; r.ytitle = "y"; r.ztitle = "z";
        fs.axes.push_back({ {1, 1, 1}, std::move(r) });
        fs.generation = fs.data_generation = 1;
        return fs;
    };

    // Seeding, including the component 2D has no use for. A stale scratch has
    // to be overwritten, or re-selecting a subplot would offer the last one's
    // number as this one's.
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
        io.DeltaTime   = 1.0f / 60.0f;
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
        frame(); frame();
        ImGui::DestroyContext(ctx);
        ImGui::SetCurrentContext(nullptr);

        check(st.axes_style_local.xaxis_z == AxisPosition::High &&
              st.axes_style_local.zaxis_y == AxisPosition::Mid,
              "3D panel: the z-involving placements reach the panel's scratch");
        check(near_px(static_cast<float>(st.origin_z_scratch), 2.5f),
              "3D panel: and origin_z seeds its drag box");
    }

    // Wiring: sweep the Axis group and read what the panel emitted. Found by
    // effect, not by position, and with the sections re-asserted open before
    // every probe for the reason the 2D sweep gives.
    {
        FigureSnapshot fs = make();

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(480.0f, 3040.0f);
        io.DeltaTime   = 1.0f / 60.0f;
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
        frame(); frame();
        auto open_sections = [&] {
            if (ImGuiWindow* w = ImGui::FindWindowByName("Cosmetic"))
                for (const char* s : { "View", "Figure", "Axis", "Ticks", "Legend & colorbar" })
                    w->StateStorage.SetInt(w->GetID(s), 1);
        };
        open_sections();
        frame(); frame();

        bool pin_x = false, pin_y = false, pin_z = false;
        auto all_found = [&] { return pin_x && pin_y && pin_z; };
        for (float y = 2.0f; y < 3000.0f && !all_found(); y += 4.0f) {
            for (float x = 8.0f; x < 420.0f && !all_found(); x += 6.0f) {
                open_sections();
                io.MousePos = ImVec2(x, y);
                io.MouseDown[0] = true;  frame();
                io.MouseDown[0] = false; frame();
                auto e = box.load_and_clear();
                // per_axes3d, not per_axes: a 3D axes' cosmetics travel in an
                // AxesEdit3D, so a test that read the 2D lane would find an
                // empty edit however well the panel worked.
                if (!e || e->per_axes3d.empty()) continue;
                for (const auto& pa : e->per_axes3d) {
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

}  // namespace lt
