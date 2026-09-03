#include "panel.h"
#include "data_panel.h"
#include "panel_state.h"
#include "panel_widgets.h"
#include "../figure_edits.h"
#include "../edit_box.h"
#include "../plot_objects.h"
#include "../render_frame.h"
#include "../figure_export.h"
#include "../hint.h"
#include "../renderer/gl_context.h"
#include "../renderer/nvg_renderer.h"
#include "../renderer/data_renderer.h"
#include "../renderer/figure_layout.h"
#include "../renderer/plot_fbo.h"
#include "../font_discovery.h"
#include "sextant/figure.h"
#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder* — not part of ImGui's stable public API
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace sextant {

namespace {

void sync_from_snapshot(PanelState& st, const FigureAxesSnapshot& fa) {
    std::snprintf(st.title_buf,  sizeof(st.title_buf),  "%s", fa.snap.title.c_str());
    std::snprintf(st.xtitle_buf, sizeof(st.xtitle_buf), "%s", fa.snap.xtitle.c_str());
    std::snprintf(st.ytitle_buf, sizeof(st.ytitle_buf), "%s", fa.snap.ytitle.c_str());
    st.grid_local  = fa.snap.grid_enabled;
    st.xauto_local = fa.snap.xlim_auto; st.xmin_local = fa.snap.xmin; st.xmax_local = fa.snap.xmax;
    st.yauto_local = fa.snap.ylim_auto; st.ymin_local = fa.snap.ymin; st.ymax_local = fa.snap.ymax;
    st.xticks_scratch = fa.snap.xticks_override.value_or(std::vector<Tick>{});
    st.yticks_scratch = fa.snap.yticks_override.value_or(std::vector<Tick>{});
    st.axes_style_local = fa.snap.axes_style;
    st.grid_opts_local  = fa.snap.grid_opts;
    st.legend_enabled_local = fa.snap.legend_enabled;
    st.legend_local     = fa.snap.legend_opts;
    st.colorbar_local   = fa.snap.colorbar_opts;
    st.last_synced_slot = fa.slot.index;
}

// Figure-level, so seeded separately from sync_from_snapshot() above — the
// suptitle does not belong to any axes slot and must not be re-seeded when
// the selected slot changes (that would discard an in-progress edit).
void sync_figure_from_snapshot(PanelState& st, const FigureSnapshot& fsnap) {
    if (st.suptitle_synced) return;
    std::snprintf(st.suptitle_buf, sizeof(st.suptitle_buf), "%s", fsnap.suptitle.c_str());
    st.suptitle_local  = fsnap.suptitle_opts;
    st.suptitle_synced = true;
}

// Same once-only rule, tracked separately from the suptitle's flag so the
// two seed independently of each other.
void sync_layout_from_snapshot(PanelState& st, const FigureSnapshot& fsnap) {
    if (st.layout_synced) return;
    st.margins_local = fsnap.margins;
    st.col_gap_local = fsnap.col_gap;
    st.row_gap_local = fsnap.row_gap;
    st.layout_synced = true;
}

// One "position | label | remove" table for an X or Y tick override.
// Returns true if the scratch vector changed this frame.
bool draw_tick_table(const char* table_id, std::vector<Tick>& scratch) {
    bool changed = false;
    int remove_i = -1;
    if (ImGui::BeginTable(table_id, 3, ImGuiTableFlags_SizingStretchProp)) {
        for (int i = 0; i < static_cast<int>(scratch.size()); ++i) {
            ImGui::PushID(i);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputDouble("##pos", &scratch[i].value, 0.0, 0.0, "%.4g"))
                changed = true;

            ImGui::TableNextColumn();
            char lbuf[64];
            std::snprintf(lbuf, sizeof(lbuf), "%s", scratch[i].label.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##label", lbuf, sizeof(lbuf))) {
                scratch[i].label = lbuf;
                changed = true;
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) remove_i = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove_i >= 0) {
        scratch.erase(scratch.begin() + remove_i);
        changed = true;
    }
    ImGui::PushID(table_id);
    if (ImGui::SmallButton("+ tick")) { scratch.push_back({0.0, ""}); changed = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear (auto)") && !scratch.empty()) { scratch.clear(); changed = true; }
    ImGui::PopID();
    return changed;
}

// Lays out the dockspace: "Plot" alone, or split into "Plot" and a right-hand
// column (opts.panel_width wide) holding "Controls" and/or "Data". Both side
// panels dock into the *same* node, so when both are visible ImGui gives them
// a shared tab bar.
//
// Rebuilt on the first frame and whenever either visibility toggles; skipped
// otherwise, which is what leaves the user's live-dragged split alone.
// Toggling a side panel off and back on re-establishes the default split
// fraction rather than restoring what the user had dragged it to.
void ensure_layout(ImGuiID dockspace_id, float panel_width, PanelState& st) {
    const bool first_build = ImGui::DockBuilderGetNode(dockspace_id) == nullptr;
    if (!first_build && st.layout_controls_visible == st.controls_visible
                     && st.layout_data_visible == st.data_visible) return;
    st.layout_controls_visible = st.controls_visible;
    st.layout_data_visible     = st.data_visible;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    // Dock/window sizes live in ImGui's own logical/screen-coordinate space
    // (see draw_plot_panel()'s comment) — use the viewport's own size here
    // rather than ctx.width()/height() (physical framebuffer pixels), since
    // panel_width is meant as a plain window-creation-scale pixel count.
    const ImVec2 size = ImGui::GetMainViewport()->Size;
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    if (st.controls_visible || st.data_visible) {
        const float side_frac = std::clamp(panel_width / size.x, 0.05f, 0.6f);
        ImGuiID dock_side, dock_plot;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, side_frac,
                                    &dock_side, &dock_plot);
        ImGui::DockBuilderDockWindow("Plot", dock_plot);
        if (st.controls_visible) ImGui::DockBuilderDockWindow("Controls", dock_side);
        if (st.data_visible)     ImGui::DockBuilderDockWindow("Data",     dock_side);

        // Seed the shared tab bar's selection explicitly. DockBuilderRemoveNode
        // destroyed the old node along with its SelectedTabId, and ImGui's
        // fallback is to select whichever tab was added last, so toggling
        // Controls off/on while Data is visible would re-select Data. A
        // window's TabId is GetID("#TAB") seeded by the window id.
        if (st.controls_visible && st.data_visible) {
            if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(dock_side)) {
                const char* want = st.focus_data_on_rebuild ? "Data" : "Controls";
                n->SelectedTabId = ImHashStr("#TAB", 0, ImHashStr(want));
            }
        }
    } else {
        ImGui::DockBuilderDockWindow("Plot", dockspace_id);
    }
    st.focus_data_on_rebuild = false;
    ImGui::DockBuilderFinish(dockspace_id);
}

// Renders the 3-pass plot into plot_fbo at the "Plot" panel's *live*
// content-region size (so dragging the dock splitter live-resizes it) and
// displays the result via ImGui::Image — this is what makes the plot itself
// a dockable/resizable panel rather than a fixed region of the window.
void draw_plot_panel(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                     PlotFbo& plot_fbo, const FigureSnapshot& fsnap,
                     FigureEditBox& edit_box, PanelState& st, int supersample) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Plot", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    // GetContentRegionAvail() is in ImGui's logical/screen-coordinate space,
    // NOT physical framebuffer pixels, and on a DPI-scaled display those
    // differ. Rendering plot_fbo at the logical size under-resolves it
    // relative to the pixels it covers, and ImGui then applies its own
    // logical->physical scaling on top -- the combination is what looked
    // stretched while resizing. So: render at avail scaled up to physical
    // pixels, but keep the on-screen display size at the logical avail.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 fb_scale = ImGui::GetIO().DisplayFramebufferScale;
    const int render_w = std::max(1, static_cast<int>(avail.x * fb_scale.x));
    const int render_h = std::max(1, static_cast<int>(avail.y * fb_scale.y));
    st.live_plot_w.store(render_w, std::memory_order_relaxed);
    st.live_plot_h.store(render_h, std::memory_order_relaxed);
    // The FBO is allocated supersample times larger in
    // each axis, but render_w/render_h stay the display size — layout (and so
    // the `layout` vector used below for hint/navigate hit-testing) stays in
    // those same coordinates, and only rasterization is enlarged.
    plot_fbo.ensure_size(render_w, render_h, supersample);

    std::vector<AxesLayout> layout;
    plot_fbo.bind();
    render_frame(ctx, nvg, data, fsnap, render_w, render_h,
                 plot_fbo.supersample(), &layout);
    plot_fbo.unbind();

    // GL textures are bottom-up (origin at bottom-left) but ImGui's default
    // UVs assume top-down image data — flip v (uv0=(0,1), uv1=(1,0)) or the
    // plot renders upside down.
    const ImVec2 image_pos = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(plot_fbo.color_texture()), avail,
                ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

    // Image() itself doesn't participate in ImGui's active-item tracking, so
    // an invisible button laid exactly on top is what gives correct
    // IsItemHovered()/IsItemActive() — including keeping a drag "active"
    // (and thus still delivering io.MouseDelta) even if the cursor slides
    // off the image mid-drag.
    ImGui::SetCursorScreenPos(image_pos);
    ImGui::InvisibleButton("##plot_nav", avail);

    // Hit-tests whichever axes cell is under the cursor, independent of
    // navigate_enabled/selected_slot_index: unlike Navigate, hints must work
    // over any subplot regardless of which is selected for pan/zoom. Drawn
    // into plot_fbo's already-rendered texture in a fresh NanoVG bracket,
    // which is safe because the texture is sampled later.
    if (st.hints_enabled && ImGui::IsItemHovered()) {
        const ImGuiIO& io = ImGui::GetIO();
        const float cursor_x = (io.MousePos.x - image_pos.x) * fb_scale.x;
        const float cursor_y = (io.MousePos.y - image_pos.y) * fb_scale.y;
        if (const AxesLayout* cell = find_hint_cell(layout, cursor_x, cursor_y)) {
            const FigureAxesSnapshot* fa = nullptr;
            for (const auto& a : fsnap.axes)
                if (a.slot.index == cell->slot.index) { fa = &a; break; }
            if (fa) {
                st.hint_index.set_frame_key(fsnap.data_generation, cell->slot.index);
                if (auto hint = find_hint(fa->snap, cell->tr, cursor_x, cursor_y,
                                          &st.hint_index)) {
                    plot_fbo.bind();
                    glViewport(0, 0, plot_fbo.render_width(), plot_fbo.render_height());
                    ctx.begin_nvg_frame(render_w, render_h,
                                        static_cast<float>(plot_fbo.supersample()));
                    nvg.draw_hint(render_w, render_h, hint->anchor_x, hint->anchor_y, hint->text);
                    ctx.end_nvg_frame();
                    plot_fbo.unbind();
                }
            }
        }
    }

    if (st.navigate_enabled) {
        const AxesLayout* cur = nullptr;
        for (const auto& al : layout)
            if (al.slot.index == st.selected_slot_index) { cur = &al; break; }

        if (cur) {
            const ImGuiIO& io = ImGui::GetIO();
            const int idx = cur->slot.index;

            if (ImGui::IsItemActive() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
                const auto lim = pan_limits(cur->tr,
                    io.MouseDelta.x * fb_scale.x, io.MouseDelta.y * fb_scale.y);
                st.xmin_local = lim.xmin; st.xmax_local = lim.xmax;
                st.ymin_local = lim.ymin; st.ymax_local = lim.ymax;
                st.xauto_local = st.yauto_local = false;
                edit_box.update(idx, [&](AxesEdit& e) {
                    e.xmin = lim.xmin; e.xmax = lim.xmax; e.xlim_auto = false;
                    e.ymin = lim.ymin; e.ymax = lim.ymax; e.ylim_auto = false;
                });
            }

            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
                const float cursor_x = (io.MousePos.x - image_pos.x) * fb_scale.x;
                const float cursor_y = (io.MousePos.y - image_pos.y) * fb_scale.y;
                const float factor = std::pow(0.9f, io.MouseWheel);
                const auto lim = zoom_limits(cur->tr, cursor_x, cursor_y, factor);
                st.xmin_local = lim.xmin; st.xmax_local = lim.xmax;
                st.ymin_local = lim.ymin; st.ymax_local = lim.ymax;
                st.xauto_local = st.yauto_local = false;
                edit_box.update(idx, [&](AxesEdit& e) {
                    e.xmin = lim.xmin; e.xmax = lim.xmax; e.xlim_auto = false;
                    e.ymin = lim.ymin; e.ymax = lim.ymax; e.ylim_auto = false;
                });
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                st.xauto_local = st.yauto_local = true;
                edit_box.update(idx, [&](AxesEdit& e) {
                    e.xlim_auto = true; e.ylim_auto = true;
                });
            }
        }
    }

    // Everything that draws into plot_fbo is done -- filter the supersampled
    // target down into the display-size texture ImGui::Image() referenced
    // earlier. Safe after that Image() call because the texture is not
    // sampled until ImGui_ImplOpenGL3_RenderDrawData(), later still.
    plot_fbo.resolve();

    ImGui::End();
}

void draw_controls_panel(const FigureSnapshot& fsnap, FigureEditBox& edit_box, PanelState& st) {
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoCollapse);

    // Navigate and Hints used to head this panel. They live in the menu bar's
    // Edit menu now: neither is a property of the figure or of the selected
    // axes — they say what the mouse does over the plot — and hiding Controls
    // used to take the only way of reaching them with it. Everything below
    // this point does edit the selected axes, which is what this panel is for.
    if (fsnap.axes.empty()) {
        ImGui::TextDisabled("No axes yet.");
        ImGui::End();
        return;
    }

    // --- Axes selector (only shown when there's more than one subplot).
    if (fsnap.axes.size() > 1)
        axes_selector("##axessel", fsnap, st.selected_slot_index);

    const FigureAxesSnapshot* cur = nullptr;
    for (const auto& fa : fsnap.axes)
        if (fa.slot.index == st.selected_slot_index) { cur = &fa; break; }
    if (!cur) cur = &fsnap.axes.front();

    if (st.last_synced_slot != cur->slot.index)
        sync_from_snapshot(st, *cur);
    sync_figure_from_snapshot(st, fsnap);
    sync_layout_from_snapshot(st, fsnap);

    const int idx = cur->slot.index;

    // Each group republishes its whole options struct on any change, the
    // pattern axes_style has always used — AxesEdit carries one optional per
    // struct rather than per field.
    auto& sty = st.axes_style_local;
    auto push_style    = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.axes_style     = sty; }); };
    auto push_grid     = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.grid_opts      = st.grid_opts_local; }); };
    auto push_legend   = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.legend_opts    = st.legend_local; }); };
    auto push_colorbar = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.colorbar_opts  = st.colorbar_local; }); };
    auto push_suptitle = [&]{ edit_box.update_figure([&](FigureEdits& f){ f.suptitle_opts = st.suptitle_local; }); };
    auto push_margins  = [&]{ edit_box.update_figure([&](FigureEdits& f){ f.margins = st.margins_local; }); };
    auto push_gaps     = [&]{ edit_box.update_figure([&](FigureEdits& f){
                                  f.col_gap = st.col_gap_local;
                                  f.row_gap = st.row_gap_local; }); };

    // ---- Text -----------------------------------------------------------
    if (section("Text", true)) {
        if (begin_field_table("txt")) {
            field_row("Title");
            if (ImGui::InputText("##title", st.title_buf, sizeof(st.title_buf)))
                edit_box.update(idx, [&](AxesEdit& e){ e.title = st.title_buf; });
            field_row("size");
            if (drag_float("##titlesz", &sty.title_fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_style();
            field_row("color");
            if (color_swatch("##titlecol", sty.title_color)) push_style();

            field_row("X title");
            if (ImGui::InputText("##xtitle", st.xtitle_buf, sizeof(st.xtitle_buf)))
                edit_box.update(idx, [&](AxesEdit& e){ e.xtitle = st.xtitle_buf; });
            field_row("size");
            if (drag_float("##xtitlesz", &sty.xtitle_fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_style();
            field_row("color");
            if (color_swatch("##xtitlecol", sty.xtitle_color)) push_style();

            field_row("Y title");
            if (ImGui::InputText("##ytitle", st.ytitle_buf, sizeof(st.ytitle_buf)))
                edit_box.update(idx, [&](AxesEdit& e){ e.ytitle = st.ytitle_buf; });
            field_row("size");
            if (drag_float("##ytitlesz", &sty.ytitle_fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_style();
            field_row("color");
            if (color_swatch("##ytitlecol", sty.ytitle_color)) push_style();

            field_row("Font");
            if (font_combo("##axesfont", sty.font_path)) push_style();
            end_field_table();
        }

        // Figure-wide, not per-axes — flagged so it isn't mistaken for a
        // property of the axes selected above.
        ImGui::Spacing();
        ImGui::TextDisabled("Figure-wide");
        if (begin_field_table("suptxt")) {
            field_row("Suptitle");
            if (ImGui::InputText("##suptitle", st.suptitle_buf, sizeof(st.suptitle_buf)))
                edit_box.update_figure([&](FigureEdits& f){ f.suptitle = st.suptitle_buf; });
            field_row("size");
            if (drag_float("##supsz", &st.suptitle_local.fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_suptitle();
            field_row("color");
            if (color_swatch("##supcol", st.suptitle_local.color)) push_suptitle();
            field_row("Font");
            if (font_combo("##supfont", st.suptitle_local.font_path)) push_suptitle();
            field_row("Align");
            if (halign_combo("##supalign", st.suptitle_local.align)) push_suptitle();
            field_row("Offset x");
            if (drag_float("##supox", &st.suptitle_local.offset_x, -2000.0f, 2000.0f, 0.5f, "%.0f px")) push_suptitle();
            field_row("Offset y");
            if (drag_float("##supoy", &st.suptitle_local.offset_y, -2000.0f, 2000.0f, 0.5f, "%.0f px")) push_suptitle();
            end_field_table();
        }
    }

    // ---- Layout ---------------------------------------------------------
    // Figure-level, like the suptitle above: the margins border the whole grid
    // and the gaps sit between subplots, so neither belongs to the selected
    // axes. There is deliberately no control for the space a tick label or
    // axis title occupies -- that is measured from the text, not chosen.
    if (section("Layout")) {
        if (begin_field_table("margins")) {
            field_row("Margin L");
            if (drag_float("##marl", &st.margins_local.left, 0.0f, 2000.0f, 0.5f, "%.0f px")) push_margins();
            field_row("R");
            if (drag_float("##marr", &st.margins_local.right, 0.0f, 2000.0f, 0.5f, "%.0f px")) push_margins();
            field_row("T");
            if (drag_float("##mart", &st.margins_local.top, 0.0f, 2000.0f, 0.5f, "%.0f px")) push_margins();
            field_row("B");
            if (drag_float("##marb", &st.margins_local.bottom, 0.0f, 2000.0f, 0.5f, "%.0f px")) push_margins();
            end_field_table();
        }

        // Gaps separate subplots from each other, so on a single-axes figure
        // there is nothing for them to separate.
        ImGui::BeginDisabled(fsnap.axes.size() <= 1);
        if (begin_field_table("gaps")) {
            field_row("Gap col");
            if (drag_float("##gapc", &st.col_gap_local, 0.0f, 2000.0f, 0.5f, "%.0f px")) push_gaps();
            field_row("row");
            if (drag_float("##gapr", &st.row_gap_local, 0.0f, 2000.0f, 0.5f, "%.0f px")) push_gaps();
            end_field_table();
        }
        ImGui::EndDisabled();

        // Read-only: the plot frame the current settings actually produce,
        // for the selected axes. Recomputed here rather than reported back
        // from the render pass, because it must reflect the values in this
        // panel on the frame they are dragged — and layout is cheap enough
        // (single-digit microseconds) to just run again.
        const int live_w = st.live_plot_w.load(std::memory_order_relaxed);
        const int live_h = st.live_plot_h.load(std::memory_order_relaxed);
        if (live_w > 0 && live_h > 0) {
            const FigureLayout fl = compute_figure_layout(fsnap, live_w, live_h);
            for (const auto& c : fl.cells) {
                if (c.slot.index != idx) continue;
                ImGui::TextDisabled("Frame %.0f x %.0f at (%.0f, %.0f)",
                                    static_cast<double>(c.frame.w), static_cast<double>(c.frame.h),
                                    static_cast<double>(c.frame.x), static_cast<double>(c.frame.y));
                ImGui::TextDisabled("Reserved L%.0f R%.0f T%.0f B%.0f",
                                    static_cast<double>(fl.insets.left),
                                    static_cast<double>(fl.insets.right),
                                    static_cast<double>(fl.insets.top),
                                    static_cast<double>(fl.insets.bottom));
                break;
            }
        }
    }

    // ---- Limits ---------------------------------------------------------
    if (section("Limits", true)) {
        if (begin_field_table("lim")) {
            field_row("X");
            if (ImGui::Checkbox("Auto##x", &st.xauto_local))
                edit_box.update(idx, [&](AxesEdit& e){ e.xlim_auto = st.xauto_local; });
            ImGui::BeginDisabled(st.xauto_local);
            const float xspeed = limit_drag_speed(st.xmin_local, st.xmax_local);
            field_row("min");
            if (drag_double("##xmin", &st.xmin_local, xspeed))
                edit_box.update(idx, [&](AxesEdit& e){ e.xmin = st.xmin_local; e.xlim_auto = false; });
            field_row("max");
            if (drag_double("##xmax", &st.xmax_local, xspeed))
                edit_box.update(idx, [&](AxesEdit& e){ e.xmax = st.xmax_local; e.xlim_auto = false; });
            ImGui::EndDisabled();

            field_row("Y");
            if (ImGui::Checkbox("Auto##y", &st.yauto_local))
                edit_box.update(idx, [&](AxesEdit& e){ e.ylim_auto = st.yauto_local; });
            ImGui::BeginDisabled(st.yauto_local);
            const float yspeed = limit_drag_speed(st.ymin_local, st.ymax_local);
            field_row("min");
            if (drag_double("##ymin", &st.ymin_local, yspeed))
                edit_box.update(idx, [&](AxesEdit& e){ e.ymin = st.ymin_local; e.ylim_auto = false; });
            field_row("max");
            if (drag_double("##ymax", &st.ymax_local, yspeed))
                edit_box.update(idx, [&](AxesEdit& e){ e.ymax = st.ymax_local; e.ylim_auto = false; });
            ImGui::EndDisabled();
            end_field_table();
        }
    }

    // ---- Ticks & labels -------------------------------------------------
    if (section("Ticks & labels")) {
        if (begin_field_table("tick")) {
            field_row("Marks");
            if (color_swatch("##tickcol", sty.tick_color)) push_style();
            field_row("length");
            if (drag_float("##ticklen", &sty.tick_length, 0.0f, 20.0f, 0.1f)) push_style();
            field_row("width");
            if (drag_float("##tickw", &sty.tick_linewidth, 0.5f, 6.0f, 0.02f)) push_style();

            field_row("Labels");
            if (color_swatch("##labelcol", sty.label_color)) push_style();
            field_row("size");
            if (drag_float("##labelsz", &sty.label_fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_style();
            end_field_table();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("X override");
        if (draw_tick_table("xticks", st.xticks_scratch))
            edit_box.update(idx, [&](AxesEdit& e){ e.xticks_override = st.xticks_scratch; });
        ImGui::TextDisabled("Y override");
        if (draw_tick_table("yticks", st.yticks_scratch))
            edit_box.update(idx, [&](AxesEdit& e){ e.yticks_override = st.yticks_scratch; });
    }

    // ---- Grid -----------------------------------------------------------
    if (section("Grid")) {
        if (ImGui::Checkbox("Enabled##grid", &st.grid_local))
            edit_box.update(idx, [&](AxesEdit& e){ e.grid_enabled = st.grid_local; });
        ImGui::BeginDisabled(!st.grid_local);
        if (begin_field_table("grid")) {
            field_row("Color");
            if (color_swatch("##gridcol", st.grid_opts_local.color)) push_grid();
            field_row("Width");
            if (drag_float("##gridw", &st.grid_opts_local.linewidth, 0.1f, 6.0f, 0.02f)) push_grid();
            field_row("Style");
            if (linestyle_combo("##gridls", st.grid_opts_local.linestyle)) push_grid();
            end_field_table();
        }
        ImGui::EndDisabled();
    }

    // ---- Axis frame -----------------------------------------------------
    if (section("Axis frame")) {
        if (begin_field_table("spine")) {
            field_row("Color");
            if (color_swatch("##spinecol", sty.spine_color)) push_style();
            field_row("Width");
            if (drag_float("##spinew", &sty.spine_linewidth, 0.5f, 6.0f, 0.02f)) push_style();
            end_field_table();
        }
    }

    // ---- Legend ---------------------------------------------------------
    if (section("Legend")) {
        if (ImGui::Checkbox("Show##legend", &st.legend_enabled_local))
            edit_box.update(idx, [&](AxesEdit& e){ e.legend_enabled = st.legend_enabled_local; });
        ImGui::BeginDisabled(!st.legend_enabled_local);
        if (begin_field_table("legend")) {
            field_row("Text");
            if (color_swatch("##legtextcol", st.legend_local.text_color)) push_legend();
            field_row("size");
            if (drag_float("##legsz", &st.legend_local.fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_legend();
            field_row("Font");
            if (font_combo("##legfont", st.legend_local.font_path)) push_legend();
            field_row("Offset x");
            if (drag_float("##legox", &st.legend_local.offset_x, -400.0f, 400.0f, 0.5f, "%.0f px")) push_legend();
            field_row("Offset y");
            if (drag_float("##legoy", &st.legend_local.offset_y, -400.0f, 400.0f, 0.5f, "%.0f px")) push_legend();
            end_field_table();
        }
        if (ImGui::Checkbox("Frame##legendframe", &st.legend_local.frameon)) push_legend();
        ImGui::BeginDisabled(!st.legend_local.frameon);
        if (begin_field_table("legendframe")) {
            field_row("Fill");
            if (color_swatch("##legfill", st.legend_local.frame_color)) push_legend();
            field_row("Border");
            if (color_swatch("##legborder", st.legend_local.border_color)) push_legend();
            field_row("width");
            if (drag_float("##legbw", &st.legend_local.border_linewidth, 0.0f, 6.0f, 0.02f)) push_legend();
            end_field_table();
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }

    // ---- Colorbar -------------------------------------------------------
    if (section("Colorbar")) {
        // Whether a colorbar exists at all is a per-plot-object flag
        // (HeatmapOptions/ScatterZOptions::colorbar), not something this panel
        // can toggle — so say so rather than showing dead controls.
        if (!find_colorbar_request(cur->snap))
            ImGui::TextDisabled("No colorbar on this axis.");
        if (begin_field_table("cbar")) {
            field_row("Text");
            if (color_swatch("##cbtextcol", st.colorbar_local.text_color)) push_colorbar();
            field_row("size");
            if (drag_float("##cbsz", &st.colorbar_local.fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_colorbar();
            field_row("Font");
            if (font_combo("##cbfont", st.colorbar_local.font_path)) push_colorbar();
            field_row("Border");
            if (color_swatch("##cbborder", st.colorbar_local.border_color)) push_colorbar();
            field_row("width");
            if (drag_float("##cbbw", &st.colorbar_local.border_linewidth, 0.0f, 6.0f, 0.02f)) push_colorbar();
            end_field_table();
        }
    }

    ImGui::End();
}

// Top-of-window "File"/"View" menu. Must run before
// ensure_layout()/DockSpaceOverViewport() — BeginMainMenuBar()
// shrinks ImGui's main-viewport WorkSize by its own height as it's
// submitted, and the dockspace needs to see that shrunk size to leave room
// for the menu bar rather than sit underneath it.
void draw_menu_bar(const FigureSnapshot& fsnap, PanelState& st) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save")) {
                if (!st.save_dialog_open) {
                    // Prefill with the live plot size so the dialog shows
                    // real numbers (not zeros) the first time it opens.
                    st.save_width  = st.live_plot_w.load(std::memory_order_relaxed);
                    st.save_height = st.live_plot_h.load(std::memory_order_relaxed);
                }
                st.save_dialog_open = true;
            }
            if (ImGui::MenuItem("Resize to plot frame")) {
                // Prefill with the frame the selected axes currently has, so
                // the dialog opens on the status quo and a nudge from there
                // is meaningful.
                if (!st.resize_dialog_open) {
                    const int lw = st.live_plot_w.load(std::memory_order_relaxed);
                    const int lh = st.live_plot_h.load(std::memory_order_relaxed);
                    if (lw > 0 && lh > 0) {
                        const FigureLayout fl = compute_figure_layout(fsnap, lw, lh);
                        for (const auto& c : fl.cells) {
                            if (c.slot.index != st.selected_slot_index) continue;
                            st.resize_frame_w = static_cast<int>(std::lround(c.frame.w));
                            st.resize_frame_h = static_cast<int>(std::lround(c.frame.h));
                            break;
                        }
                    }
                }
                st.resize_dialog_open = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Control Panel", nullptr, &st.controls_visible);
            // Turning Data on should bring it to the front of the shared tab
            // bar; toggling Controls must not. ensure_layout() consumes and
            // clears the flag on its next rebuild.
            if (ImGui::MenuItem("Data Panel", nullptr, &st.data_visible))
                st.focus_data_on_rebuild = st.data_visible;
            ImGui::EndMenu();
        }
        // The two interaction modes. They belong here rather than in the
        // Controls panel because neither is a property of a figure or of the
        // selected axes — they govern what the mouse does over the plot — and
        // because Controls can be hidden, which used to take the only way of
        // reaching them with it.
        if (ImGui::BeginMenu("Edit")) {
            ImGui::MenuItem("Navigate", nullptr, &st.navigate_enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Left-drag pans, scroll zooms at the cursor,\n"
                                  "double-click resets — on the axes selected in Controls.");
            ImGui::MenuItem("Hints", nullptr, &st.hints_enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show a tooltip for the data point under the cursor.");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

// Undocked popup for the save path/size, opened from the File menu. Flagged
// NoDocking and never registered with the dockspace, so it floats freely.
//
// The "Figure / Plot frame" selector and size fields are shared by the Save
// and Resize dialogs so the two mean the same thing by the same words. In
// PlotFrame mode the entered numbers describe the selected subplot's data
// area and the figure size is derived from them; the derived number is shown,
// since it is what the file or the window actually becomes.
void size_mode_fields(const FigureSnapshot& fsnap, PanelState& st,
                      PanelState::SizeMode& mode, int* w, int* h,
                      const char* frame_hint) {
    int m = (mode == PanelState::SizeMode::PlotFrame) ? 1 : 0;
    if (ImGui::RadioButton("Figure", &m, 0)) mode = PanelState::SizeMode::Figure;
    ImGui::SameLine();
    if (ImGui::RadioButton("Plot frame", &m, 1)) mode = PanelState::SizeMode::PlotFrame;

    ImGui::InputInt("Width",  w);
    ImGui::InputInt("Height", h);

    if (mode == PanelState::SizeMode::PlotFrame) {
        if (*w > 0 && *h > 0) {
            const LayoutSize s = figure_size_for_frame(fsnap, st.selected_slot_index,
                                                       static_cast<float>(*w),
                                                       static_cast<float>(*h));
            ImGui::TextDisabled("Figure becomes %.0f x %.0f (axis %d)",
                                static_cast<double>(s.width), static_cast<double>(s.height),
                                st.selected_slot_index);
        }
        // Legend and colorbar are carved from the cell that owns them, so a
        // frame size only pins down the axes it was asked about.
        if (fsnap.axes.size() > 1)
            ImGui::TextDisabled("Other subplots may differ (legend/colorbar).");
    } else {
        ImGui::TextDisabled("%s", frame_hint);
    }
}

void draw_save_dialog(const FigureSnapshot& fsnap, PanelState& st) {
    if (!st.save_dialog_open) return;

    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Save Figure", &st.save_dialog_open,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::InputText("File", st.save_path_buf, sizeof(st.save_path_buf));
    size_mode_fields(fsnap, st, st.save_size_mode, &st.save_width, &st.save_height,
                     "<=0 uses the Plot panel's current size.");

    if (ImGui::Button("Save")) {
        st.save_requested = true;
        st.save_dialog_open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        st.save_dialog_open = false;

    ImGui::End();
}

// Resizes the live window so the selected subplot's plot frame
// comes out at the requested size — the figure size is derived, and the
// window then grows by the menu bar and Controls column on top of that.
void draw_resize_dialog(const FigureSnapshot& fsnap, PanelState& st) {
    if (!st.resize_dialog_open) return;

    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Resize", &st.resize_dialog_open,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);

    // Always frame-driven here — resizing the figure directly is what
    // dragging the window edge already does.
    PanelState::SizeMode mode = PanelState::SizeMode::PlotFrame;
    size_mode_fields(fsnap, st, mode, &st.resize_frame_w, &st.resize_frame_h, "");

    ImGui::BeginDisabled(st.resize_frame_w <= 0 || st.resize_frame_h <= 0);
    if (ImGui::Button("Apply")) {
        const LayoutSize s = figure_size_for_frame(fsnap, st.selected_slot_index,
                                                   static_cast<float>(st.resize_frame_w),
                                                   static_cast<float>(st.resize_frame_h));
        st.pending_plot_w.store(static_cast<int>(std::lround(s.width)),
                                std::memory_order_relaxed);
        st.pending_plot_h.store(static_cast<int>(std::lround(s.height)),
                                std::memory_order_relaxed);
        st.resize_dialog_open = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        st.resize_dialog_open = false;

    ImGui::End();
}

// Applies a pending plot-area size to the real window. The request names the
// *plot* size, so whatever the menu bar and the Controls/Data column occupy is
// measured off the current frame and added back; deriving that chrome from
// panel_width and a menu-bar height would go stale the moment the user dragged
// the dock splitter or hid Controls.
//
// Only the window thread may call this, which is why the request is an atomic
// this consumes rather than a direct call.
void apply_pending_resize(GLContext& ctx, PanelState& st) {
    const int want_w = st.pending_plot_w.load(std::memory_order_relaxed);
    const int want_h = st.pending_plot_h.load(std::memory_order_relaxed);
    if (want_w <= 0 || want_h <= 0) return;

    const int plot_w = st.live_plot_w.load(std::memory_order_relaxed);
    const int plot_h = st.live_plot_h.load(std::memory_order_relaxed);
    // Nothing has been rendered yet, so there is no chrome to measure —
    // leave the request pending rather than guessing at it.
    if (plot_w <= 0 || plot_h <= 0) return;

    st.pending_plot_w.store(0, std::memory_order_relaxed);
    st.pending_plot_h.store(0, std::memory_order_relaxed);

    const int target_fb_w = want_w + (ctx.width()  - plot_w);
    const int target_fb_h = want_h + (ctx.height() - plot_h);
    if (target_fb_w <= 0 || target_fb_h <= 0) return;

    // GlfwSetWindowSize speaks screen coordinates; everything above is in
    // framebuffer pixels, and the two differ under OS display scaling.
    int win_w = 0, win_h = 0, fb_w = 0, fb_h = 0;
    glfwGetWindowSize(ctx.window(), &win_w, &win_h);
    glfwGetFramebufferSize(ctx.window(), &fb_w, &fb_h);
    const double sx = (fb_w > 0) ? static_cast<double>(win_w) / fb_w : 1.0;
    const double sy = (fb_h > 0) ? static_cast<double>(win_h) / fb_h : 1.0;

    glfwSetWindowSize(ctx.window(),
                      static_cast<int>(std::lround(target_fb_w * sx)),
                      static_cast<int>(std::lround(target_fb_h * sy)));
}

} // namespace

void draw_widget_panel(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                       PlotFbo& plot_fbo, const FigureSnapshot& fsnap,
                       const FigureOptions& opts,
                       FigureEditBox& edit_box, PanelState& st)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    draw_menu_bar(fsnap, st);

    const ImGuiID dockspace_id = ImGui::GetID("SextantDockspace");
    ensure_layout(dockspace_id, opts.panel_width, st);
    ImGui::DockSpaceOverViewport(dockspace_id);

    draw_plot_panel(ctx, nvg, data, plot_fbo, fsnap, edit_box, st, opts.supersample);
    if (st.controls_visible)
        draw_controls_panel(fsnap, edit_box, st);
    if (st.data_visible)
        draw_data_panel(fsnap, edit_box, st);
    draw_save_dialog(fsnap, st);
    draw_resize_dialog(fsnap, st);

    // After the plot panel has published this frame's live size, so the
    // chrome it measures is current.
    apply_pending_resize(ctx, st);

    // Serviced here rather than inline in draw_save_dialog() because a PNG
    // save needs ctx/nvg/data. export_figure_png() reuses whatever GL context
    // is current and renders into its own throwaway FBO, so this is safe
    // mid-frame. Width/height <=0 default to the Plot panel's live size.
    //
    // It exports `fsnap` -- the snapshot the render thread holds, already
    // patched with any panel edits -- so what is saved matches what is on
    // screen without the caller thread having to refresh() first.
    if (st.save_requested) {
        st.save_requested = false;
        int sw = st.save_width  > 0 ? st.save_width  : st.live_plot_w.load(std::memory_order_relaxed);
        int sh = st.save_height > 0 ? st.save_height : st.live_plot_h.load(std::memory_order_relaxed);
        if (st.save_size_mode == PanelState::SizeMode::PlotFrame
            && st.save_width > 0 && st.save_height > 0) {
            const LayoutSize s = figure_size_for_frame(fsnap, st.selected_slot_index,
                                                       static_cast<float>(st.save_width),
                                                       static_cast<float>(st.save_height));
            sw = static_cast<int>(std::lround(s.width));
            sh = static_cast<int>(std::lround(s.height));
        }
        if (sw > 0 && sh > 0) {
            const std::string path = st.save_path_buf;
            const auto dot = path.rfind('.');
            const auto ext = (dot == std::string::npos) ? "" : path.substr(dot);
            if (ext == ".svg" || ext == ".SVG")
                export_figure_svg(fsnap, path, sw, sh);
            else
                export_figure_png(ctx, nvg, data, fsnap, path, sw, sh, opts.supersample);
        }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace sextant
