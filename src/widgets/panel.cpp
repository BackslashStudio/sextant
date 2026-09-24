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
#include "../plot_data_view.h"
#include "../renderer/gl_context.h"
#include "../renderer/nvg_renderer.h"
#include "../renderer/data_renderer.h"
#include "../renderer/figure_layout.h"
#include "../renderer/plot_fbo.h"
#include "../font_discovery.h"
#include "sextant/figure.h"
#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder* — not part of ImGui's stable public API
#include "imgui_impl_sextant.h"
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace sextant {

namespace {

// The plot panel's current measurements (a fresh measure before its first
// frame).
std::shared_ptr<const FigureMeasure> on_screen_measure(const PanelState& st,
                                                       const FigureSnapshot& fsnap) {
    if (auto m = st.layout.load()) return m;
    return std::make_shared<const FigureMeasure>(measure_figure(fsnap));
}

// Takes the 2D snapshot; never call with a 3D cell.
void sync_from_snapshot(PanelState& st, int slot_index, const RenderSnapshot& sn) {
    std::snprintf(st.title_buf,  sizeof(st.title_buf),  "%s", sn.title.c_str());
    std::snprintf(st.xtitle_buf, sizeof(st.xtitle_buf), "%s", sn.xtitle.c_str());
    std::snprintf(st.ytitle_buf, sizeof(st.ytitle_buf), "%s", sn.ytitle.c_str());
    st.grid_local  = sn.grid_enabled;
    st.xauto_local = sn.xlim_auto; st.xmin_local = sn.xmin; st.xmax_local = sn.xmax;
    st.yauto_local = sn.ylim_auto; st.ymin_local = sn.ymin; st.ymax_local = sn.ymax;
    st.xticks_scratch = sn.xticks_override.value_or(std::vector<Tick>{});
    st.yticks_scratch = sn.yticks_override.value_or(std::vector<Tick>{});
    st.axes_style_local = sn.axes_style;
    st.origin_x_scratch = sn.axes_style.origin_x.value_or(0.0);
    st.origin_y_scratch = sn.axes_style.origin_y.value_or(0.0);
    st.grid_opts_local  = sn.grid_opts;
    st.legend_enabled_local = sn.legend_enabled;
    st.legend_local     = sn.legend_opts;
    st.colorbar_local   = sn.colorbar_opts;
    st.last_synced_slot = slot_index;
}

// The 3D counterpart: fills shared scratch fields and the 3D-only ones, via the
// same last_synced_slot gate.
// The planes' scratch copy, also re-seeded when the plane count changes
// (indices shift, so the whole list is re-read).
void sync_planes(PanelState& st, const RenderSnapshot3D& sn) {
    st.planes_local.clear();
    st.planes_local.reserve(sn.planes.size());
    for (const auto& p : sn.planes)
        st.planes_local.push_back({ p.orient, p.offset, p.opts });
}

// The 3D kinds' appearance, on the same rule.
void sync_scene_objects(PanelState& st, const RenderSnapshot3D& sn) {
    st.bars3d_local.clear();
    st.bars3d_local.reserve(sn.bars3d.size());
    for (const auto& b : sn.bars3d) st.bars3d_local.push_back(b.opts);
    st.surfaces_local.clear();
    st.surfaces_local.reserve(sn.surfaces.size());
    for (const auto& s : sn.surfaces) st.surfaces_local.push_back(s.opts);
    st.scatter3d_local.clear();
    st.scatter3d_local.reserve(sn.scatter3d.size());
    for (const auto& c : sn.scatter3d) st.scatter3d_local.push_back(c.opts);
    st.line3d_local.clear();
    st.line3d_local.reserve(sn.lines3d.size());
    for (const auto& l : sn.lines3d) st.line3d_local.push_back(l.opts);
    st.surface_tri_local.clear();
    st.surface_tri_local.reserve(sn.surface_tri.size());
    for (const auto& m : sn.surface_tri) st.surface_tri_local.push_back(m.opts);
}

void sync_from_snapshot(PanelState& st, int slot_index, const RenderSnapshot3D& sn) {
    std::snprintf(st.title_buf,  sizeof(st.title_buf),  "%s", sn.title.c_str());
    std::snprintf(st.xtitle_buf, sizeof(st.xtitle_buf), "%s", sn.xtitle.c_str());
    std::snprintf(st.ytitle_buf, sizeof(st.ytitle_buf), "%s", sn.ytitle.c_str());
    std::snprintf(st.ztitle_buf, sizeof(st.ztitle_buf), "%s", sn.ztitle.c_str());
    st.grid_local  = sn.grid_enabled;
    st.xauto_local = sn.xlim_auto; st.xmin_local = sn.xmin; st.xmax_local = sn.xmax;
    st.yauto_local = sn.ylim_auto; st.ymin_local = sn.ymin; st.ymax_local = sn.ymax;
    st.zauto_local = sn.zlim_auto; st.zmin_local = sn.zmin; st.zmax_local = sn.zmax;
    st.xticks_scratch = sn.xticks_override.value_or(std::vector<Tick>{});
    st.yticks_scratch = sn.yticks_override.value_or(std::vector<Tick>{});
    st.zticks_scratch = sn.zticks_override.value_or(std::vector<Tick>{});
    st.axes_style_local = sn.axes_style;
    st.origin_x_scratch = sn.axes_style.origin_x.value_or(0.0);
    st.origin_y_scratch = sn.axes_style.origin_y.value_or(0.0);
    st.origin_z_scratch = sn.axes_style.origin_z.value_or(0.0);
    st.grid_opts_local  = sn.grid_opts;
    st.legend_enabled_local = sn.legend_enabled;
    st.legend_local     = sn.legend_opts;
    st.colorbar_local   = sn.colorbar_opts;
    st.camera_local     = sn.camera;
    st.box3d_local      = sn.box_style;
    st.aspect_local     = sn.aspect;
    sync_planes(st, sn);
    sync_scene_objects(st, sn);
    st.last_synced_slot = slot_index;
}

// For axes on "auto", the limit fields track the resolved limits every frame
// (the snapshot only has declared defaults). Dragging a field clears `auto`,
// after which the declared value is correct.
void track_resolved_limits(PanelState& st, int slot,
                           bool xauto, bool yauto, bool zauto) {
    const PanelState::ResolvedLimits* r = st.resolved_for(slot);
    if (!r) return;   // no frame drawn yet (the headless panel test)
    if (xauto) { st.xmin_local = r->xmin; st.xmax_local = r->xmax; }
    if (yauto) { st.ymin_local = r->ymin; st.ymax_local = r->ymax; }
    if (zauto && r->is_3d) { st.zmin_local = r->zmin; st.zmax_local = r->zmax; }
}

// Figure-level: seeded once, not on slot change (would discard an edit).
void sync_figure_from_snapshot(PanelState& st, const FigureSnapshot& fsnap) {
    if (st.suptitle_synced) return;
    std::snprintf(st.suptitle_buf, sizeof(st.suptitle_buf), "%s", fsnap.suptitle.c_str());
    st.suptitle_local  = fsnap.suptitle_opts;
    st.suptitle_synced = true;
}

// Same once-only rule, with its own flag.
void sync_layout_from_snapshot(PanelState& st, const FigureSnapshot& fsnap) {
    if (st.layout_synced) return;
    st.margins_local = fsnap.margins;
    st.col_gap_local = fsnap.col_gap;
    st.row_gap_local = fsnap.row_gap;
    st.layout_synced = true;
}

// A "position | label | remove" table for a tick override; true on change.
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

// Dock layout: "Plot" alone, or split with a right column (panel_width wide)
// holding "Cosmetic" and/or "Data" in one shared node. Rebuilt on the first
// frame and when a side panel toggles (restoring the default split).
void ensure_layout(ImGuiID dockspace_id, float panel_width, PanelState& st) {
    const bool first_build = ImGui::DockBuilderGetNode(dockspace_id) == nullptr;
    if (!first_build && st.layout_cosmetic_visible == st.cosmetic_visible
                     && st.layout_data_visible == st.data_visible) return;
    st.layout_cosmetic_visible = st.cosmetic_visible;
    st.layout_data_visible     = st.data_visible;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    // Use the viewport's logical size (not framebuffer pixels). panel_width
    // arrives DPI-scaled; the split is stored as a fraction, so only the first
    // build needs the scale.
    const ImVec2 size = ImGui::GetMainViewport()->Size;
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    if (st.cosmetic_visible || st.data_visible) {
        const float side_frac = std::clamp(panel_width / size.x, 0.05f, 0.6f);
        ImGuiID dock_side, dock_plot;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, side_frac,
                                    &dock_side, &dock_plot);
        ImGui::DockBuilderDockWindow("Plot", dock_plot);
        if (st.cosmetic_visible) ImGui::DockBuilderDockWindow("Cosmetic", dock_side);
        if (st.data_visible)     ImGui::DockBuilderDockWindow("Data",     dock_side);

        // Seed the tab bar's selection (a rebuilt node otherwise selects the
        // last tab). ImGui also gives the tab to the focused window, so the
        // wanted panel is focused explicitly too (pending_panel_focus).
        if (st.cosmetic_visible && st.data_visible) {
            const char* want = st.focus_data_on_rebuild ? "Data" : "Cosmetic";
            if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(dock_side))
                n->SelectedTabId = ImHashStr("#TAB", 0, ImHashStr(want));
            st.pending_panel_focus = want;
        }
    } else {
        ImGui::DockBuilderDockWindow("Plot", dockspace_id);
    }
    st.focus_data_on_rebuild = false;
    ImGui::DockBuilderFinish(dockspace_id);
}

// Renders the plot into plot_fbo at the "Plot" panel's live size and shows it
// via ImGui::Image(), making the plot a resizable dock panel.
void draw_plot_panel(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                     PlotFbo& plot_fbo, const FigureSnapshot& fsnap,
                     FigureEditBox& edit_box, PanelState& st, int supersample) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Plot", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    // Three units meet here. ImGui's are window coordinates (points on macOS);
    // the FBO is framebuffer pixels (ImGui units x DisplayFramebufferScale);
    // the plot is laid out in logical pixels (framebuffer pixels / the
    // display's content scale), so a font size looks the same on every display
    // and only the sharpness changes. `to_plot` takes ImGui units to plot ones.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float fb_scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const float display_scale = std::max(ctx.link().content_scale(), 0.01f);
    const float to_plot = fb_scale / display_scale;
    const int fb_w = std::max(1, static_cast<int>(avail.x * fb_scale));
    const int fb_h = std::max(1, static_cast<int>(avail.y * fb_scale));
    const int render_w = std::max(1, static_cast<int>(std::lround(fb_w / display_scale)));
    const int render_h = std::max(1, static_cast<int>(std::lround(fb_h / display_scale)));
    st.live_plot_w.store(render_w, std::memory_order_relaxed);
    st.live_plot_h.store(render_h, std::memory_order_relaxed);
    st.live_plot_fb_w.store(fb_w, std::memory_order_relaxed);
    st.live_plot_fb_h.store(fb_h, std::memory_order_relaxed);
    // The FBO is framebuffer-sized, times the supersample factor; the layout
    // stays logical.
    plot_fbo.ensure_size(fb_w, fb_h, supersample);
    const float pixel_ratio = display_scale * static_cast<float>(plot_fbo.supersample());

    // From stored measurements, re-measured on layout generation, size change
    // or File > Refit layout.
    const FigureLayout fl = st.layout.fit(fsnap, render_w, render_h);

    std::vector<AxesLayout> layout;
    plot_fbo.bind();
    render_frame(ctx, nvg, data, fsnap, render_w, render_h,
                 pixel_ratio, &layout, &fl);
    plot_fbo.unbind();

    // Store the resolved auto limits for the Cosmetic panel.
    st.resolved.clear();
    st.resolved.reserve(layout.size());
    for (const AxesLayout& al : layout) {
        PanelState::ResolvedLimits r;
        r.slot = al.slot.index;
        if (al.proj3d) {
            const Transform3D& t = al.proj3d->transform();
            r.is_3d = true;
            r.xmin = t.xmin; r.xmax = t.xmax;
            r.ymin = t.ymin; r.ymax = t.ymax;
            r.zmin = t.zmin; r.zmax = t.zmax;
        } else {
            r.xmin = al.tr.xmin; r.xmax = al.tr.xmax;
            r.ymin = al.tr.ymin; r.ymax = al.tr.ymax;
        }
        st.resolved.push_back(r);
    }

    // GL textures are bottom-up; flip v.
    const ImVec2 image_pos = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(plot_fbo.color_texture()), avail,
                ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

    // An invisible button over the image provides hover/active tracking,
    // keeping a drag active if the cursor leaves the image.
    ImGui::SetCursorScreenPos(image_pos);
    ImGui::InvisibleButton("##plot_nav", avail);

    // Selection and navigation gating, read while the button is the last item.
    PlotNavGate gate;
    const bool in_hovered = ImGui::IsItemHovered();
    {
        const ImGuiIO& io = ImGui::GetIO();
        PlotPointer in;
        in.x = (io.MousePos.x - image_pos.x) * to_plot;
        in.y = (io.MousePos.y - image_pos.y) * to_plot;
        in.hovered        = in_hovered;
        in.active         = ImGui::IsItemActive();
        in.pressed        = ImGui::IsItemActivated();
        in.double_clicked = in.pressed && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        in.released       = ImGui::IsItemDeactivated();
        in.dragged        = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left]
                            >= io.MouseDragThreshold * io.MouseDragThreshold;

        // Grid boundary dragging first; what it owns doesn't select or
        // navigate.
        GridDragOut grid = update_grid_drag(st, fsnap, fl, render_w, render_h, in,
                                            4.0f * to_plot);
        if (grid.col_ratios || grid.row_ratios)
            edit_box.update_figure([&](FigureEdits& f) {
                if (grid.col_ratios) f.col_ratios = std::move(*grid.col_ratios);
                if (grid.row_ratios) f.row_ratios = std::move(*grid.row_ratios);
            });
        if (grid.cursor_ew) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (grid.cursor_ns) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (grid.owns) {
            in.hovered = in.active = in.pressed = in.double_clicked = in.released = false;
        }
        gate = update_plot_selection(st, fsnap, layout, in);
    }

    // Outline the selected subplot, drawn over the image (never saved). Only
    // with more than one subplot.
    if (fsnap.axes.size() > 1) {
        for (const AxesLayout& al : layout) {
            if (al.slot.index != st.selected_slot_index) continue;
            const ImVec2 p0(image_pos.x + al.cell.x / to_plot + 1.0f,
                            image_pos.y + al.cell.y / to_plot + 1.0f);
            const ImVec2 p1(image_pos.x + (al.cell.x + al.cell.w) / to_plot - 1.0f,
                            image_pos.y + (al.cell.y + al.cell.h) / to_plot - 1.0f);
            ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            accent.w *= 0.75f;
            ImGui::GetWindowDrawList()->AddRect(p0, p1, ImGui::GetColorU32(accent),
                                                0.0f, 0, 1.5f);
            break;
        }
    }

    // Hover hints over any subplot (independent of selection), drawn into the
    // already-rendered texture in a new NanoVG frame.
    if (st.hints_enabled && in_hovered) {
        const ImGuiIO& io = ImGui::GetIO();
        const float cursor_x = (io.MousePos.x - image_pos.x) * to_plot;
        const float cursor_y = (io.MousePos.y - image_pos.y) * to_plot;
        if (const AxesLayout* cell = find_hint_cell(layout, cursor_x, cursor_y)) {
            const FigureAxesSnapshot* fa = nullptr;
            for (const auto& a : fsnap.axes)
                if (a.slot.index == cell->slot.index) { fa = &a; break; }
            // 3D: ray cast against planes and bars, then the 2D search on the
            // nearest plane hit.
            std::optional<HintResult> hint;
            if (fa && fa->snap2d()) {
                st.hint_index.set_frame_key(fsnap.data_generation, cell->slot.index);
                hint = find_hint(*fa->snap2d(), cell->tr, cursor_x, cursor_y,
                                 &st.hint_index);
            } else if (fa && fa->snap3d() && cell->proj3d) {
                st.hint_index.set_frame_key(fsnap.data_generation, cell->slot.index);
                hint = find_hint3d(*fa->snap3d(), *cell->proj3d, cursor_x, cursor_y,
                                   &st.hint_index);
            }
            if (hint) {
                plot_fbo.bind();
                glViewport(0, 0, plot_fbo.render_width(), plot_fbo.render_height());
                ctx.begin_nvg_frame(render_w, render_h, pixel_ratio);
                nvg.draw_hint(render_w, render_h, hint->anchor_x, hint->anchor_y, hint->text);
                ctx.end_nvg_frame();
                plot_fbo.unbind();
            }
        }
    }

    if (st.navigate_enabled) {
        const AxesLayout* cur = nullptr;
        for (const auto& al : layout)
            if (al.slot.index == st.selected_slot_index) { cur = &al; break; }

        // A 3D slot navigates its camera. Both kinds push through
        // FigureEditBox, so the view survives refresh().
        const RenderSnapshot3D* sel3d = nullptr;
        for (const auto& a : fsnap.axes)
            if (a.slot.index == st.selected_slot_index) { sel3d = a.snap3d(); break; }

        if (cur && sel3d) {
            const ImGuiIO& io = ImGui::GetIO();
            const int idx = cur->slot.index;
            Camera3D cam = st.camera_local;
            bool moved = false;

            if (gate.drag && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
                cam = orbit_camera(cam, io.MouseDelta.x * to_plot,
                                        io.MouseDelta.y * to_plot);
                moved = true;
            }

            if (gate.wheel && io.MouseWheel != 0.0f) {
                cam = zoom_camera(cam, io.MouseWheel);
                moved = true;
            }

            // Fly keys only while the selected cell is hovered or dragged and
            // ImGui doesn't want the keyboard (typing "W" shouldn't fly).
            if (gate.keys && !io.WantCaptureKeyboard) {
                FlyInput fly;
                fly.forward = ImGui::IsKeyDown(ImGuiKey_W);
                fly.back    = ImGui::IsKeyDown(ImGuiKey_S);
                fly.left    = ImGui::IsKeyDown(ImGuiKey_A);
                fly.right   = ImGui::IsKeyDown(ImGuiKey_D);
                fly.up      = ImGui::IsKeyDown(ImGuiKey_E);
                fly.down    = ImGui::IsKeyDown(ImGuiKey_Q);
                fly.dt      = io.DeltaTime;
                if (fly.forward || fly.back || fly.left || fly.right || fly.up || fly.down) {
                    // The camera basis from this frame's projector (A/D strafe,
                    // W/S dolly along the view).
                    cam = fly_camera(cam,
                                     cur->proj3d ? cur->proj3d->right()
                                                 : Vec3{ 0.0, 1.0, 0.0 },
                                     cur->proj3d ? cur->proj3d->forward()
                                                 : Vec3{ -1.0, 0.0, 0.0 },
                                     fly);
                    moved = true;
                }
            }

            if (gate.reset) {
                cam = sel3d->default_camera;
                moved = true;
            }

            if (moved) {
                st.camera_local = cam;
                edit_box.update3d(idx, [&](AxesEdit3D& e) { e.camera = cam; });
            }
        } else if (cur) {
            const ImGuiIO& io = ImGui::GetIO();
            const int idx = cur->slot.index;

            if (gate.drag && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
                const auto lim = pan_limits(cur->tr,
                    io.MouseDelta.x * to_plot, io.MouseDelta.y * to_plot);
                st.xmin_local = lim.xmin; st.xmax_local = lim.xmax;
                st.ymin_local = lim.ymin; st.ymax_local = lim.ymax;
                st.xauto_local = st.yauto_local = false;
                edit_box.update(idx, [&](AxesEdit& e) {
                    e.xmin = lim.xmin; e.xmax = lim.xmax; e.xlim_auto = false;
                    e.ymin = lim.ymin; e.ymax = lim.ymax; e.ylim_auto = false;
                });
            }

            if (gate.wheel && io.MouseWheel != 0.0f) {
                const float cursor_x = (io.MousePos.x - image_pos.x) * to_plot;
                const float cursor_y = (io.MousePos.y - image_pos.y) * to_plot;
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

            if (gate.reset) {
                st.xauto_local = st.yauto_local = true;
                edit_box.update(idx, [&](AxesEdit& e) {
                    e.xlim_auto = true; e.ylim_auto = true;
                });
            }
        }
    }

    // Box-filter the supersampled target into the texture Image() references
    // (sampled later, at RenderDrawData()).
    plot_fbo.resolve();

    ImGui::End();
}

// The suptitle controls, figure-level and shared by the 2D and 3D panels
// (under the Figure group's "Suptitle" heading).
void draw_suptitle_fields(PanelState& st, FigureEditBox& edit_box) {
    auto push = [&]{ edit_box.update_figure([&](FigureEdits& f){ f.suptitle_opts = st.suptitle_local; }); };

    // Three tables (the middle row has two pairs); matching first columns.
    if (begin_field_table("suptxt")) {
        field_row("Suptitle");
        if (ImGui::InputText("##suptitle", st.suptitle_buf, sizeof(st.suptitle_buf)))
            edit_box.update_figure([&](FigureEdits& f){ f.suptitle = st.suptitle_buf; });
        field_row("Font");
        if (font_combo("##supfont", st.suptitle_local.font_path)) push();
        end_field_table();
    }
    if (begin_field_table("supcs", 2)) {
        field_row("Color");
        if (color_swatch("##supcol", st.suptitle_local.color)) push();
        field_next("Size");
        if (drag_float("##supsz", &st.suptitle_local.fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push();
        end_field_table();
    }
    if (begin_field_table("supal")) {
        field_row("Align");
        if (halign_combo("##supalign", st.suptitle_local.align)) push();
        field_row("Offset");
        split_begin(2);
        if (drag_float("##supox", &st.suptitle_local.offset_x, -2000.0f, 2000.0f, 0.5f, "x %.0f px")) push();
        split_next();
        if (drag_float("##supoy", &st.suptitle_local.offset_y, -2000.0f, 2000.0f, 0.5f, "y %.0f px")) push();
        split_end();
        end_field_table();
    }
}

// Margins and gaps: figure-level, shared by both panels (under "Layout").
// Decoration space is measured from text, so there is no control for it.
void draw_layout_fields(PanelState& st, const FigureSnapshot& fsnap,
                        FigureEditBox& edit_box, int idx) {
    auto push_margins = [&]{ edit_box.update_figure([&](FigureEdits& f){ f.margins = st.margins_local; }); };
    auto push_gaps    = [&]{ edit_box.update_figure([&](FigureEdits& f){
                                 f.col_gap = st.col_gap_local;
                                 f.row_gap = st.row_gap_local; }); };

    // Split rows: each value names its side; the row label carries the unit.
    if (begin_field_table("margins")) {
        field_row("Margin px");
        split_begin(4);
        if (drag_float("##marl", &st.margins_local.left, 0.0f, 2000.0f, 0.5f, "L %.0f")) push_margins();
        split_next();
        if (drag_float("##marr", &st.margins_local.right, 0.0f, 2000.0f, 0.5f, "R %.0f")) push_margins();
        split_next();
        if (drag_float("##mart", &st.margins_local.top, 0.0f, 2000.0f, 0.5f, "T %.0f")) push_margins();
        split_next();
        if (drag_float("##marb", &st.margins_local.bottom, 0.0f, 2000.0f, 0.5f, "B %.0f")) push_margins();
        split_end();
        end_field_table();
    }

    // Gaps only apply with more than one subplot.
    ImGui::BeginDisabled(fsnap.axes.size() <= 1);
    if (begin_field_table("gaps")) {
        field_row("Gap px");
        split_begin(2);
        if (drag_float("##gapc", &st.col_gap_local, 0.0f, 2000.0f, 0.5f, "col %.0f")) push_gaps();
        split_next();
        if (drag_float("##gapr", &st.row_gap_local, 0.0f, 2000.0f, 0.5f, "row %.0f")) push_gaps();
        split_end();
        end_field_table();
    }
    ImGui::EndDisabled();

    // Grid weights, read from the snapshot every frame (they are journaled, so
    // no local copy is needed). Boundary drags edit the same values.
    const int grid_rows = fsnap.axes.empty() ? 1 : std::max(1, fsnap.axes.front().slot.rows);
    const int grid_cols = fsnap.axes.empty() ? 1 : std::max(1, fsnap.axes.front().slot.cols);
    auto ratio_row = [&](const char* id, const char* label, int n, bool cols) {
        if (n <= 1) return;
        std::vector<float> w = grid_weights(cols ? fsnap.col_ratios : fsnap.row_ratios, n);
        if (!begin_field_table(id)) return;
        field_row(label);
        split_begin(n);
        bool changed = false;
        for (int k = 0; k < n; ++k) {
            if (k > 0) split_next();
            ImGui::PushID(k);
            changed |= drag_float("##w", &w[static_cast<std::size_t>(k)], 0.05f, 100.0f, 0.01f, "%.2f");
            ImGui::PopID();
        }
        split_end();
        end_field_table();
        if (changed)
            edit_box.update_figure([&](FigureEdits& f) {
                if (cols) f.col_ratios = w; else f.row_ratios = w;
            });
    };
    ratio_row("colratios", "Col ratio", grid_cols, true);
    ratio_row("rowratios", "Row ratio", grid_rows, false);

    // Read-only: the selected axes' resulting plot frame, laid out from the
    // stored measurements.
    const int live_w = st.live_plot_w.load(std::memory_order_relaxed);
    const int live_h = st.live_plot_h.load(std::memory_order_relaxed);
    if (live_w > 0 && live_h > 0) {
        const FigureLayout fl = compute_figure_layout(fsnap, *on_screen_measure(st, fsnap),
                                                      live_w, live_h);
        for (const auto& c : fl.cells) {
            if (c.slot.index != idx) continue;
            ImGui::TextDisabled("Frame %.0f x %.0f at (%.0f, %.0f)",
                                static_cast<double>(c.frame.w), static_cast<double>(c.frame.h),
                                static_cast<double>(c.frame.x), static_cast<double>(c.frame.y));
            ImGui::TextDisabled("Reserved L%.0f R%.0f T%.0f B%.0f",
                                static_cast<double>(c.reserved.left),
                                static_cast<double>(c.reserved.right),
                                static_cast<double>(c.reserved.top),
                                static_cast<double>(c.reserved.bottom));
            break;
        }
    }
}

// The Figure group, shared by both panels; closed by default.
void draw_figure_group(PanelState& st, const FigureSnapshot& fsnap,
                       FigureEditBox& edit_box, int idx) {
    if (!section("Figure")) return;
    ImGui::SeparatorText("Suptitle");
    draw_suptitle_fields(st, edit_box);
    ImGui::SeparatorText("Layout");
    draw_layout_fields(st, fsnap, edit_box, idx);
}

// The Legend & colorbar group, shared by both panels, templated on the edit
// struct (AxesEdit or AxesEdit3D).
template <typename Edit, typename Push>
void draw_legend_colorbar_group(PanelState& st, bool has_colorbar, Push&& push) {
    auto push_legend   = [&]{ push([&](Edit& e){ e.legend_opts   = st.legend_local; }); };
    auto push_colorbar = [&]{ push([&](Edit& e){ e.colorbar_opts = st.colorbar_local; }); };

    if (!section("Legend & colorbar")) return;

    ImGui::SeparatorText("Legend");
    if (ImGui::Checkbox("Show##legend", &st.legend_enabled_local))
        push([&](Edit& e){ e.legend_enabled = st.legend_enabled_local; });
    ImGui::BeginDisabled(!st.legend_enabled_local);
    if (begin_field_table("legtext", 2)) {
        field_row("Text");
        if (color_swatch("##legtextcol", st.legend_local.text_color)) push_legend();
        field_next("Size");
        if (drag_float("##legsz", &st.legend_local.fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_legend();
        end_field_table();
    }
    if (begin_field_table("legpos")) {
        field_row("Font");
        if (font_combo("##legfont", st.legend_local.font_path)) push_legend();
        field_row("Anchor");
        if (legend_anchor_combo("##leganchor", st.legend_local.anchor)) push_legend();
        field_row("Margin");
        if (drag_float("##legmargin", &st.legend_local.margin, 0.0f, 400.0f, 0.5f, "%.0f px")) push_legend();
        field_row("Offset");
        split_begin(2);
        if (drag_float("##legox", &st.legend_local.offset_x, -400.0f, 400.0f, 0.5f, "x %.0f px")) push_legend();
        split_next();
        if (drag_float("##legoy", &st.legend_local.offset_y, -400.0f, 400.0f, 0.5f, "y %.0f px")) push_legend();
        split_end();
        end_field_table();
    }
    if (ImGui::Checkbox("Frame##legendframe", &st.legend_local.frameon)) push_legend();
    ImGui::BeginDisabled(!st.legend_local.frameon);
    if (begin_field_table("legendframe", 3)) {
        field_row("Fill");
        if (color_swatch("##legfill", st.legend_local.frame_color)) push_legend();
        field_next("Border");
        if (color_swatch("##legborder", st.legend_local.border_color)) push_legend();
        field_next("Width");
        if (drag_float("##legbw", &st.legend_local.border_linewidth, 0.0f, 6.0f, 0.02f, "%.2f px")) push_legend();
        end_field_table();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::SeparatorText("Colorbar");
    // Colorbars are requested per plot object; these cosmetics style every bar.
    if (!has_colorbar)
        ImGui::TextDisabled("No colorbar on this axis.");
    if (begin_field_table("cbtext", 2)) {
        field_row("Text");
        if (color_swatch("##cbtextcol", st.colorbar_local.text_color)) push_colorbar();
        field_next("Size");
        if (drag_float("##cbsz", &st.colorbar_local.fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_colorbar();
        end_field_table();
    }
    if (begin_field_table("cbfont")) {
        field_row("Font");
        if (font_combo("##cbfont", st.colorbar_local.font_path)) push_colorbar();
        end_field_table();
    }
    if (begin_field_table("cbborder", 2)) {
        field_row("Border");
        if (color_swatch("##cbborder", st.colorbar_local.border_color)) push_colorbar();
        field_next("Width");
        if (drag_float("##cbbw", &st.colorbar_local.border_linewidth, 0.0f, 6.0f, 0.02f, "%.2f px")) push_colorbar();
        end_field_table();
    }
    if (begin_field_table("cbanchor")) {
        field_row("Anchor");
        if (colorbar_anchor_combo("##cbanchor", st.colorbar_local.anchor)) push_colorbar();
        end_field_table();
    }
    if (begin_field_table("cbsize", 2)) {
        field_row("Bar");
        if (drag_float("##cbwidth", &st.colorbar_local.width, 1.0f, 200.0f, 0.5f, "%.0f px")) push_colorbar();
        field_next("Margin");
        if (drag_float("##cbmargin", &st.colorbar_local.margin, 0.0f, 400.0f, 0.5f, "%.0f px")) push_colorbar();
        end_field_table();
    }
    if (begin_field_table("cboffset")) {
        field_row("Offset");
        split_begin(2);
        if (drag_float("##cbox", &st.colorbar_local.offset_x, -400.0f, 400.0f, 0.5f, "x %.0f px")) push_colorbar();
        split_next();
        if (drag_float("##cboy", &st.colorbar_local.offset_y, -400.0f, 400.0f, 0.5f, "y %.0f px")) push_colorbar();
        split_end();
        end_field_table();
    }
}

// The Cosmetic panel for a 3D slot (separate from 2D: nearly every section
// differs).
void draw_cosmetic_3d(PanelState& st, const RenderSnapshot3D& sn,
                      const FigureSnapshot& fsnap, FigureEditBox& edit_box, int idx) {
    auto& sty = st.axes_style_local;

    auto push_style  = [&]{ edit_box.update3d(idx, [&](AxesEdit3D& e){ e.axes_style = sty; }); };
    auto push_grid   = [&]{ edit_box.update3d(idx, [&](AxesEdit3D& e){ e.grid_opts  = st.grid_opts_local; }); };
    auto push_camera = [&]{ st.camera_local = clamp_camera(st.camera_local);
                            edit_box.update3d(idx, [&](AxesEdit3D& e){ e.camera = st.camera_local; }); };
    auto push_box    = [&]{ edit_box.update3d(idx, [&](AxesEdit3D& e){ e.box_style = st.box3d_local; }); };
    // Clamped: Axes3D::set_box_aspect() rejects non-positive sides.
    auto push_aspect = [&]{
        st.aspect_local.x = std::clamp(st.aspect_local.x, 0.05, 20.0);
        st.aspect_local.y = std::clamp(st.aspect_local.y, 0.05, 20.0);
        st.aspect_local.z = std::clamp(st.aspect_local.z, 0.05, 20.0);
        edit_box.update3d(idx, [&](AxesEdit3D& e){ e.aspect = st.aspect_local; });
    };

    // Five groups:
    //   View   -- camera, box, grid
    //   Figure -- suptitle, layout
    //   Axis   -- titles, axis frame
    //   Ticks  -- limits (first), ticks and labels
    //   Legend & colorbar -- shared with 2D

    // ==== View ============================================================
    if (section("View", true)) {
        ImGui::SeparatorText("Camera");
        const bool persp = st.camera_local.projection == Projection::Perspective;
        if (begin_field_table("cam3d", 2)) {
            field_row("Azim");
            if (drag_double("##azim", &st.camera_local.azimuth, 0.25f, "%.1f deg")) push_camera();
            field_next("Elev");
            if (drag_double("##elev", &st.camera_local.elevation, 0.25f, "%.1f deg")) push_camera();
            // FOV only under perspective.
            field_row("Zoom");
            if (drag_double("##zoom3d", &st.camera_local.zoom, 0.005f, "%.2fx")) push_camera();
            if (persp) {
                field_next("FOV");
                if (drag_double("##fov3d", &st.camera_local.fov, 0.1f, "%.0f deg")) push_camera();
            }
            end_field_table();
        }
        if (begin_field_table("cam3dp")) {
            field_row("Projection");
            if (projection_combo("##proj3d", st.camera_local.projection)) push_camera();
            end_field_table();
        }
        // No distance control: the box is fitted every frame, and under
        // perspective fov determines the eye distance; zoom magnifies.
        ImGui::TextDisabled("Target %.2f, %.2f, %.2f",
                            st.camera_local.target.x, st.camera_local.target.y,
                            st.camera_local.target.z);
        if (persp)
            ImGui::TextDisabled("Wider fov = closer camera. The box fills the cell either way.");
        if (ImGui::SmallButton("Reset view")) {
            st.camera_local = sn.default_camera;
            edit_box.update3d(idx, [&](AxesEdit3D& e){ e.camera = st.camera_local; });
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(or double-click the plot)");
        ImGui::TextDisabled(persp
            ? "Navigate: drag to orbit, W/S dolly, AD/QE move, scroll to zoom."
            : "Navigate: drag to orbit, WASD/QE to move, scroll to zoom.");
        ImGui::SeparatorText("Box");
        if (begin_field_table("box3d")) {
            field_row("Aspect");
            split_begin(3);
            if (drag_double("##aspx", &st.aspect_local.x, 0.005f, "x %.2f")) push_aspect();
            split_next();
            if (drag_double("##aspy", &st.aspect_local.y, 0.005f, "y %.2f")) push_aspect();
            split_next();
            if (drag_double("##aspz", &st.aspect_local.z, 0.005f, "z %.2f")) push_aspect();
            split_end();
            field_row("Margin");
            if (drag_float("##boxmargin", &st.box3d_local.margin, 0.0f, 0.45f, 0.001f, "%.3f")) push_box();
            end_field_table();
        }
        // Chosen, not measured: 3D label positions depend on the camera fit.
        ImGui::TextDisabled("Margin reserves room for labels, which move with the camera.");
        if (ImGui::Checkbox("Panes", &st.box3d_local.panes)) push_box();
        if (begin_field_table("boxcol", 2)) {
            field_row("Color");
            if (color_swatch("##panecol", st.box3d_local.pane_color)) push_box();
            field_next("Edge color");
            if (color_swatch("##paneedge", st.box3d_local.pane_edge_color)) push_box();
            end_field_table();
        }
        // "##grid3d" suffix avoids an id clash with a same-named header.
        if (ImGui::Checkbox("Grid##grid3d", &st.grid_local))
            edit_box.update3d(idx, [&](AxesEdit3D& e){ e.grid_enabled = st.grid_local; });
        ImGui::BeginDisabled(!st.grid_local);
        if (begin_field_table("grid3d", 3)) {
            field_row("Color");
            if (color_swatch("##gridcol3d", st.grid_opts_local.color)) push_grid();
            field_next("Style");
            if (linestyle_combo("##gridls3d", st.grid_opts_local.linestyle)) push_grid();
            field_next("Width");
            if (drag_float("##gridw3d", &st.grid_opts_local.linewidth, 0.1f, 10.0f, 0.05f, "%.2f px")) push_grid();
            end_field_table();
        }
        ImGui::EndDisabled();
    }

    // ==== Figure ==========================================================
    // Shared with the 2D panel.
    draw_figure_group(st, fsnap, edit_box, idx);

    // ==== Axis ============================================================
    if (section("Axis", true)) {
        ImGui::SeparatorText("Titles");
        // Each title's text field, then its color and size on the next row
        // (separate tables; no column span).
        struct TitleUi {
            const char* label; const char* id;
            char* buf; std::size_t cap;
            std::optional<std::string> AxesEdit3D::*text;
            Color* color; float* size;
        };
        const TitleUi titles[4] = {
            { "Title",   "title",  st.title_buf,  sizeof(st.title_buf),  &AxesEdit3D::title,
              &sty.title_color,  &sty.title_fontsize },
            { "X title", "xtitle", st.xtitle_buf, sizeof(st.xtitle_buf), &AxesEdit3D::xtitle,
              &sty.xtitle_color, &sty.xtitle_fontsize },
            { "Y title", "ytitle", st.ytitle_buf, sizeof(st.ytitle_buf), &AxesEdit3D::ytitle,
              &sty.ytitle_color, &sty.ytitle_fontsize },
            { "Z title", "ztitle", st.ztitle_buf, sizeof(st.ztitle_buf), &AxesEdit3D::ztitle,
              &sty.ztitle_color, &sty.ztitle_fontsize },
        };
        for (const TitleUi& t : titles) {
            ImGui::PushID(t.id);
            if (begin_field_table("text")) {
                field_row(t.label);
                if (ImGui::InputText("##text", t.buf, t.cap))
                    edit_box.update3d(idx, [&](AxesEdit3D& e){ e.*t.text = std::string(t.buf); });
                end_field_table();
            }
            if (begin_field_table("style", 2)) {
                field_row("Color");
                if (color_swatch("##color", *t.color)) push_style();
                field_next("Size");
                if (drag_float("##size", t.size, 1.0f, 96.0f, 0.2f, "%.1f px")) push_style();
                end_field_table();
            }
            ImGui::PopID();
        }
        if (begin_field_table("font3d")) {
            field_row("Font");
            if (font_combo("##axesfont3d", sty.font_path)) push_style();
            end_field_table();
        }
        ImGui::SeparatorText("Axis frame");
        if (begin_field_table("spine3d", 2)) {
            field_row("Color");
            if (color_swatch("##spinecol3d", sty.spine_color)) push_style();
            field_next("Width");
            if (drag_float("##spinew3d", &sty.spine_linewidth, 0.1f, 10.0f, 0.05f, "%.2f px")) push_style();
            end_field_table();
        }
        if (begin_field_table("framemargin3d")) {
            field_row("Margin");
            if (drag_float("##framemargin3d", &sty.frame_margin, 0.0f, 400.0f, 0.5f, "%.0f px")) push_style();
            end_field_table();
        }
        ImGui::SeparatorText("Axis position");
        {
            // Six placements (two coordinates per axis). Auto is the silhouette
            // edge; Low/High are fixed box faces.
            struct Pos3 {
                const char*   label;
                bool          row;    // starts a row, rather than continuing one
                AxisPosition* pos;
                const std::optional<double>* pin;   // what supersedes it
            };
            const Pos3 pos3[6] = {
                { "X at y", true,  &sty.xaxis_y, &sty.origin_y },
                { "and z",  false, &sty.xaxis_z, &sty.origin_z },
                { "Y at x", true,  &sty.yaxis_x, &sty.origin_x },
                { "and z",  false, &sty.yaxis_z, &sty.origin_z },
                { "Z at x", true,  &sty.zaxis_x, &sty.origin_x },
                { "and y",  false, &sty.zaxis_y, &sty.origin_y },
            };
            if (begin_field_table("axpos3d", 2)) {
                int id = 0;
                for (const Pos3& p : pos3) {
                    ImGui::PushID(id++);
                    if (p.row) field_row(p.label); else field_next(p.label);
                    // Greyed while its component is pinned.
                    ImGui::BeginDisabled(p.pin->has_value());
                    if (axis_position_combo("##pos", *p.pos, "Auto (camera)")) push_style();
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                end_field_table();
            }

            // Three origin components (not six pins): x and z axes share the
            // same y.
            ImGui::TextDisabled("Origin components, which supersede the above.");
            struct Org {
                const char* label;
                std::optional<double>* pin;
                double* scratch;
                const double* lo;
                const double* hi;
            };
            const Org orgs[3] = {
                { "at x", &sty.origin_x, &st.origin_x_scratch, &st.xmin_local, &st.xmax_local },
                { "at y", &sty.origin_y, &st.origin_y_scratch, &st.ymin_local, &st.ymax_local },
                { "at z", &sty.origin_z, &st.origin_z_scratch, &st.zmin_local, &st.zmax_local },
            };
            if (begin_field_table("origin3d")) {
                for (const Org& o : orgs) {
                    ImGui::PushID(o.label);
                    field_row(o.label);
                    bool pinned = o.pin->has_value();
                    if (ImGui::Checkbox("##pin", &pinned)) {
                        if (pinned) *o.pin = *o.scratch;
                        else        o.pin->reset();
                        push_style();
                    }
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::BeginDisabled(!pinned);
                    // The checkbox used the fill width; request it again.
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (drag_double("##pinval", o.scratch, limit_drag_speed(*o.lo, *o.hi))) {
                        *o.pin = *o.scratch;
                        push_style();
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                end_field_table();
            }
        }
        ImGui::TextDisabled("An Auto edge turns with the camera. Titles stay");
        ImGui::TextDisabled("on it even when the line moves inward.");
    }

    // ==== Ticks ===========================================================
    if (section("Ticks", true)) {
        ImGui::SeparatorText("Limits");
        struct AxisLim {
            const char* label;
            double* lo; double* hi;
            std::optional<double> AxesEdit3D::*lo_field;
            std::optional<double> AxesEdit3D::*hi_field;
            std::optional<bool>   AxesEdit3D::*auto_field;
        };
        const AxisLim axes[3] = {
            { "X", &st.xmin_local, &st.xmax_local,
              &AxesEdit3D::xmin, &AxesEdit3D::xmax, &AxesEdit3D::xlim_auto },
            { "Y", &st.ymin_local, &st.ymax_local,
              &AxesEdit3D::ymin, &AxesEdit3D::ymax, &AxesEdit3D::ylim_auto },
            { "Z", &st.zmin_local, &st.zmax_local,
              &AxesEdit3D::zmin, &AxesEdit3D::zmax, &AxesEdit3D::zlim_auto },
        };
        if (begin_field_table("lim3d")) {
            for (const AxisLim& a : axes) {
                ImGui::PushID(a.label);
                auto push = [&]{
                    // Refuse a degenerate pair, as Axes3D::set_xlim() does.
                    if (*a.lo == *a.hi) return;
                    edit_box.update3d(idx, [&](AxesEdit3D& e){
                        e.*a.lo_field = *a.lo;
                        e.*a.hi_field = *a.hi;
                        e.*a.auto_field = false;
                    });
                };
                // Drag fields (Ctrl+click types an exact value).
                const float speed = limit_drag_speed(*a.lo, *a.hi);
                field_row(a.label);
                split_begin(2);
                if (drag_double("##lo", a.lo, speed)) push();
                split_next();
                if (drag_double("##hi", a.hi, speed)) push();
                split_end();
                ImGui::PopID();
            }
            end_field_table();
        }
        // Limits set the data range the box spans, not its size.
        ImGui::TextDisabled("Limits set the range the box spans.");
        ImGui::TextDisabled("Its size is under View: Box aspect and the camera.");
        ImGui::SeparatorText("Ticks & labels");
        // Mark on one row, label on the next, in one table.
        if (begin_field_table("tick3d", 3)) {
            field_row("Mark");
            if (color_swatch("##tickcol3d", sty.tick_color)) push_style();
            field_next("Length");
            if (drag_float("##ticklen3d", &sty.tick_length, 0.0f, 40.0f, 0.1f, "%.1f px")) push_style();
            field_next("Width");
            if (drag_float("##tickw3d", &sty.tick_linewidth, 0.1f, 10.0f, 0.05f, "%.2f px")) push_style();
            field_row("Label");
            if (color_swatch("##labcol3d", sty.label_color)) push_style();
            field_next("Size");
            if (drag_float("##labsz3d", &sty.label_fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_style();
            end_field_table();
        }
        // Tick tables choose which numbers show (labels are thinned per camera).
        ImGui::TextDisabled("Labels are thinned to fit the edge they sit on.");

        struct AxisTicks {
            const char* label; const char* id;
            std::vector<Tick>* scratch;
            std::optional<std::vector<Tick>> AxesEdit3D::*field;
        };
        const AxisTicks tt[3] = {
            { "X ticks", "xt3d", &st.xticks_scratch, &AxesEdit3D::xticks_override },
            { "Y ticks", "yt3d", &st.yticks_scratch, &AxesEdit3D::yticks_override },
            { "Z ticks", "zt3d", &st.zticks_scratch, &AxesEdit3D::zticks_override },
        };
        for (const AxisTicks& a : tt) {
            ImGui::TextDisabled("%s", a.label);
            if (draw_tick_table(a.id, *a.scratch))
                edit_box.update3d(idx, [&](AxesEdit3D& e){ e.*a.field = *a.scratch; });
        }
    }

    // Planes, bar grids and surfaces are edited in their Data-panel tabs.

    // ==== Legend & colorbar ===============================================
    // Shared with the 2D panel; styling is the axes'.
    draw_legend_colorbar_group<AxesEdit3D>(
        st, !find_colorbar_requests(sn).empty(),
        [&](auto&& fn){ edit_box.update3d(idx, fn); });
}

const FigureAxesSnapshot* axes_for_slot(const FigureSnapshot& fsnap, int slot_index) {
    for (const FigureAxesSnapshot& fa : fsnap.axes)
        if (fa.slot.index == slot_index) return &fa;
    return nullptr;
}

} // namespace

const AxesLayout* find_cell_at(const std::vector<AxesLayout>& layout, float x, float y) {
    for (const AxesLayout& al : layout) {
        const PlotRect& c = al.cell;
        if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return &al;
    }
    return nullptr;
}

int sync_selected_slot(PanelState& st, const FigureSnapshot& fsnap) {
    if (fsnap.axes.empty()) return -1;
    const FigureAxesSnapshot* fa = axes_for_slot(fsnap, st.selected_slot_index);
    if (!fa) fa = &fsnap.axes.front();
    // Normalized: File > Resize and the Save dialog read selected_slot_index.
    st.selected_slot_index = fa->slot.index;
    if (st.last_synced_slot != fa->slot.index) {
        std::visit([&](const auto& sn) { sync_from_snapshot(st, fa->slot.index, sn); },
                   fa->snap);
    } else if (const RenderSnapshot3D* sn = fa->snap3d()) {
        // Object count changed: re-seed the lists (indices shifted). Done here
        // so it happens even with the Cosmetic panel hidden.
        if (st.planes_local.size() != sn->planes.size())
            sync_planes(st, *sn);
        if (st.bars3d_local.size() != sn->bars3d.size() ||
            st.surfaces_local.size() != sn->surfaces.size() ||
            st.scatter3d_local.size() != sn->scatter3d.size() ||
            st.line3d_local.size() != sn->lines3d.size() ||
            st.surface_tri_local.size() != sn->surface_tri.size())
            sync_scene_objects(st, *sn);
    }
    return fa->slot.index;
}

void select_slot(PanelState& st, const FigureSnapshot& fsnap, int slot_index) {
    st.selected_slot_index = slot_index;
    sync_selected_slot(st, fsnap);
}

PlotNavGate update_plot_selection(PanelState& st, const FigureSnapshot& fsnap,
                                  const std::vector<AxesLayout>& layout,
                                  const PlotPointer& in) {
    PlotNavGate gate;
    // First, so navigation starts from the selected slot's own camera/limits.
    const int selected = sync_selected_slot(st, fsnap);
    if (selected < 0) {
        st.press_slot = -1;
        st.press_on_selected = false;
        return gate;
    }

    const AxesLayout* under = find_cell_at(layout, in.x, in.y);
    const int under_slot = under ? under->slot.index : -1;

    if (in.pressed) {
        st.press_slot        = under_slot;
        st.press_on_selected = under_slot == selected;
        // A double-click whose first click selected this cell doesn't reset it.
        gate.reset = in.double_clicked && st.press_on_selected
                     && !st.selected_by_last_click;
        st.selected_by_last_click = false;
    }

    gate.drag = in.active && st.press_on_selected;
    const bool over_selected = in.hovered && under_slot == selected;
    gate.wheel = over_selected;
    gate.keys  = over_selected || gate.drag;

    if (in.released) {
        // A click (not a drag) that ends in the cell it began in.
        if (!in.dragged && st.press_slot >= 0 && under_slot == st.press_slot
            && st.press_slot != selected) {
            select_slot(st, fsnap, st.press_slot);
            st.selected_by_last_click = true;
        }
        st.press_slot        = -1;
        st.press_on_selected = false;
    }
    return gate;
}

GridBoundary find_grid_boundary(const FigureSnapshot& fsnap, const GridTracks& t,
                                float x, float y, float tol) {
    const int cols = static_cast<int>(t.col_x.size());
    const int rows = static_cast<int>(t.row_y.size());
    // Whether a subplot covers both sides of boundary k at track `other`.
    auto crossed = [&](bool col_boundary, int k, int other) {
        for (const auto& fa : fsnap.axes) {
            const AxesSlot& s = fa.slot;
            if (col_boundary) {
                if (s.col0() < k && k <= s.col1() && s.row0() <= other && other <= s.row1())
                    return true;
            } else {
                if (s.row0() < k && k <= s.row1() && s.col0() <= other && other <= s.col1())
                    return true;
            }
        }
        return false;
    };
    // The track of the other axis containing `v`, or -1.
    auto track_at = [](const std::vector<float>& pos, const std::vector<float>& len, float v) {
        for (std::size_t i = 0; i < pos.size(); ++i)
            if (v >= pos[i] && v <= pos[i] + len[i]) return static_cast<int>(i);
        return -1;
    };

    for (int k = 1; k < cols; ++k) {
        const float lo = t.col_x[k - 1] + t.col_w[k - 1] - tol;
        const float hi = t.col_x[k] + tol;
        if (x < lo || x > hi) continue;
        const int r = track_at(t.row_y, t.row_h, y);
        if (r >= 0 && !crossed(true, k, r)) return { true, true, k };
    }
    for (int k = 1; k < rows; ++k) {
        const float lo = t.row_y[k - 1] + t.row_h[k - 1] - tol;
        const float hi = t.row_y[k] + tol;
        if (y < lo || y > hi) continue;
        const int c = track_at(t.col_x, t.col_w, x);
        if (c >= 0 && !crossed(false, k, c)) return { true, false, k };
    }
    return {};
}

GridDragOut update_grid_drag(PanelState& st, const FigureSnapshot& fsnap,
                             const FigureLayout& layout, int fig_w, int fig_h,
                             const PlotPointer& in, float tol) {
    GridDragOut out;
    PanelState::GridDrag& g = st.grid_drag;
    const GridTracks t = grid_tracks(fsnap, layout.suptitle_band, fig_w, fig_h);

    auto emit = [&](bool cols, const std::vector<float>& w) {
        if (cols) out.col_ratios = w; else out.row_ratios = w;
    };

    if (g.active) {
        out.owns = true;
        out.cursor_ew = g.cols;
        out.cursor_ns = !g.cols;
        // Split from the press state, respecting both tracks' minimums.
        const float d     = (g.cols ? in.x : in.y) - g.press;
        const float total = g.len_a + g.len_b;
        float a = g.len_a;
        if (g.min_a <= total - g.min_b)
            a = std::clamp(g.len_a + d, g.min_a, total - g.min_b);
        if (a != g.last_a && total > 0.0f
            && g.k > 0 && g.k < static_cast<int>(g.w0.size())) {
            std::vector<float> w = g.w0;
            const float wsum = g.w0[g.k - 1] + g.w0[g.k];
            w[g.k - 1] = wsum * a / total;
            w[g.k]     = wsum - w[g.k - 1];
            emit(g.cols, w);
            g.last_a = a;
        }
        if (in.released || !in.active) g.active = false;
        return out;
    }

    // Hovering or pressing: only while no other drag holds the button.
    if (!in.hovered || (in.active && !in.pressed)) return out;
    const GridBoundary b = find_grid_boundary(fsnap, t, in.x, in.y, tol);
    if (!b.found) return out;
    out.owns = true;
    out.cursor_ew = b.cols;
    out.cursor_ns = !b.cols;
    if (!in.pressed) return out;

    const int n = static_cast<int>(b.cols ? t.col_x.size() : t.row_y.size());
    std::vector<float> w0 = grid_weights(b.cols ? fsnap.col_ratios : fsnap.row_ratios, n);

    if (in.double_clicked) {
        const float half = (w0[b.k - 1] + w0[b.k]) * 0.5f;
        w0[b.k - 1] = w0[b.k] = half;
        emit(b.cols, w0);
        return out;
    }

    // Minimum track size: its single-track cells' reservations plus the
    // smallest frame (spans set no minimum).
    auto min_len = [&](int track) {
        float m = 0.0f;
        for (const CellLayout& c : layout.cells) {
            const AxesSlot& s = c.slot;
            if (b.cols && s.col0() == track && s.col1() == track)
                m = std::max(m, c.reserved.left + c.reserved.right);
            if (!b.cols && s.row0() == track && s.row1() == track)
                m = std::max(m, c.reserved.top + c.reserved.bottom);
        }
        return m + kMinFrameSize;
    };

    g.active = true;
    g.cols   = b.cols;
    g.k      = b.k;
    g.press  = b.cols ? in.x : in.y;
    g.w0     = std::move(w0);
    g.len_a  = b.cols ? t.col_w[b.k - 1] : t.row_h[b.k - 1];
    g.len_b  = b.cols ? t.col_w[b.k]     : t.row_h[b.k];
    g.min_a  = min_len(b.k - 1);
    g.min_b  = min_len(b.k);
    g.last_a = g.len_a;
    return out;
}

// Not file-local: the layout test drives it through a null-backend frame.
void draw_cosmetic_panel(const FigureSnapshot& fsnap, FigureEditBox& edit_box, PanelState& st) {
    ImGui::Begin("Cosmetic", nullptr, ImGuiWindowFlags_NoCollapse);

    // Navigate and Hints are in the Edit menu; this panel edits the selected
    // axes only.
    if (fsnap.axes.empty()) {
        ImGui::TextDisabled("No axes yet.");
        ImGui::End();
        return;
    }

    // The selection comes from the menu bar or a click; synced here too so the
    // panel works on its own (as in tests).
    const FigureAxesSnapshot* cur = axes_for_slot(fsnap, sync_selected_slot(st, fsnap));

    // Separate functions per kind; they share the Figure group.
    sync_figure_from_snapshot(st, fsnap);
    sync_layout_from_snapshot(st, fsnap);

    const int idx = cur->slot.index;

    if (const RenderSnapshot3D* cur3d = cur->snap3d()) {
        track_resolved_limits(st, idx, cur3d->xlim_auto, cur3d->ylim_auto,
                              cur3d->zlim_auto);
        draw_cosmetic_3d(st, *cur3d, fsnap, edit_box, idx);
        ImGui::End();
        return;
    }

    const RenderSnapshot* cur2d = cur->snap2d();
    track_resolved_limits(st, idx, cur2d->xlim_auto, cur2d->ylim_auto, false);

    // Each group republishes its whole options struct on change.
    auto& sty = st.axes_style_local;
    auto push_style    = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.axes_style     = sty; }); };
    auto push_grid     = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.grid_opts      = st.grid_opts_local; }); };
    auto push_legend   = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.legend_opts    = st.legend_local; }); };
    auto push_colorbar = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.colorbar_opts  = st.colorbar_local; }); };

    // Four groups, matching the 3D panel's names:
    //   Figure -- suptitle, layout
    //   Axis   -- titles, axis frame, grid
    //   Ticks  -- limits, ticks and labels
    //   Legend & colorbar

    // ==== Figure ==========================================================
    draw_figure_group(st, fsnap, edit_box, idx);

    // ==== Axis ============================================================
    if (section("Axis", true)) {
        ImGui::SeparatorText("Titles");
        // Each title's text, then its color and size on the next row.
        struct TitleUi {
            const char* label; const char* id;
            char* buf; std::size_t cap;
            std::optional<std::string> AxesEdit::*text;
            Color* color; float* size;
        };
        const TitleUi titles[3] = {
            { "Title",   "title",  st.title_buf,  sizeof(st.title_buf),  &AxesEdit::title,
              &sty.title_color,  &sty.title_fontsize },
            { "X title", "xtitle", st.xtitle_buf, sizeof(st.xtitle_buf), &AxesEdit::xtitle,
              &sty.xtitle_color, &sty.xtitle_fontsize },
            { "Y title", "ytitle", st.ytitle_buf, sizeof(st.ytitle_buf), &AxesEdit::ytitle,
              &sty.ytitle_color, &sty.ytitle_fontsize },
        };
        for (const TitleUi& t : titles) {
            ImGui::PushID(t.id);
            if (begin_field_table("text")) {
                field_row(t.label);
                if (ImGui::InputText("##text", t.buf, t.cap))
                    edit_box.update(idx, [&](AxesEdit& e){ e.*t.text = std::string(t.buf); });
                end_field_table();
            }
            if (begin_field_table("style", 2)) {
                field_row("Color");
                if (color_swatch("##color", *t.color)) push_style();
                field_next("Size");
                if (drag_float("##size", t.size, 1.0f, 96.0f, 0.2f, "%.1f px")) push_style();
                end_field_table();
            }
            ImGui::PopID();
        }
        if (begin_field_table("font")) {
            field_row("Font");
            if (font_combo("##axesfont", sty.font_path)) push_style();
            end_field_table();
        }
        ImGui::SeparatorText("Axis frame");
        if (begin_field_table("spine", 2)) {
            field_row("Color");
            if (color_swatch("##spinecol", sty.spine_color)) push_style();
            field_next("Width");
            if (drag_float("##spinew", &sty.spine_linewidth, 0.5f, 6.0f, 0.02f, "%.2f px")) push_style();
            end_field_table();
        }
        if (begin_field_table("framemargin")) {
            field_row("Margin");
            if (drag_float("##framemargin", &sty.frame_margin, 0.0f, 400.0f, 0.5f, "%.0f px")) push_style();
            end_field_table();
        }
        // Frame edges, independent of ticks.
        if (begin_field_table("spines", 2)) {
            field_row("Bottom");
            if (ImGui::Checkbox("##spinebottom", &sty.spine_bottom)) push_style();
            field_next("Top");
            if (ImGui::Checkbox("##spinetop", &sty.spine_top)) push_style();
            field_row("Left");
            if (ImGui::Checkbox("##spineleft", &sty.spine_left)) push_style();
            field_next("Right");
            if (ImGui::Checkbox("##spineright", &sty.spine_right)) push_style();
            end_field_table();
        }

        ImGui::SeparatorText("Axis position");
        {
            // One row per axis: its placement, then the origin component
            // ("X axis ... at y"). `scratch` survives un-ticking the pin.
            struct AxisPos {
                const char* label;
                const char* pin_label;
                AxisPosition* pos;
                std::optional<double>* pin;
                double* scratch;
                const double* lo;      // the limits of the coordinate the
                const double* hi;      // component is measured along
            };
            const AxisPos pos_axes[2] = {
                { "X axis", "at y", &sty.xaxis_y, &sty.origin_y, &st.origin_y_scratch,
                  &st.ymin_local, &st.ymax_local },
                { "Y axis", "at x", &sty.yaxis_x, &sty.origin_x, &st.origin_x_scratch,
                  &st.xmin_local, &st.xmax_local },
            };
            if (begin_field_table("axpos", 2)) {
                for (const AxisPos& a : pos_axes) {
                    ImGui::PushID(a.label);
                    field_row(a.label);
                    // Greyed while pinned.
                    ImGui::BeginDisabled(a.pin->has_value());
                    if (axis_position_combo("##pos", *a.pos)) push_style();
                    ImGui::EndDisabled();

                    field_next(a.pin_label);
                    bool pinned = a.pin->has_value();
                    if (ImGui::Checkbox("##pin", &pinned)) {
                        if (pinned) *a.pin = *a.scratch;
                        else        a.pin->reset();
                        push_style();
                    }
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::BeginDisabled(!pinned);
                    // The checkbox used the fill width; request it again.
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (drag_double("##pinval", a.scratch, limit_drag_speed(*a.lo, *a.hi))) {
                        *a.pin = *a.scratch;
                        push_style();
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                end_field_table();
            }
        }
        ImGui::SeparatorText("Grid");
        // The ##suffix keeps it apart from the 3D panel's grid checkbox.
        if (ImGui::Checkbox("Grid##grid", &st.grid_local))
            edit_box.update(idx, [&](AxesEdit& e){ e.grid_enabled = st.grid_local; });
        ImGui::BeginDisabled(!st.grid_local);
        if (begin_field_table("grid", 3)) {
            field_row("Color");
            if (color_swatch("##gridcol", st.grid_opts_local.color)) push_grid();
            field_next("Style");
            if (linestyle_combo("##gridls", st.grid_opts_local.linestyle)) push_grid();
            field_next("Width");
            if (drag_float("##gridw", &st.grid_opts_local.linewidth, 0.1f, 6.0f, 0.02f, "%.2f px")) push_grid();
            end_field_table();
        }
        ImGui::EndDisabled();
    }

    // ==== Ticks ===========================================================
    if (section("Ticks", true)) {
        ImGui::SeparatorText("Limits");
        struct AxisLim {
            const char* label;
            bool* autoscale; double* lo; double* hi;
            std::optional<double> AxesEdit::*lo_field;
            std::optional<double> AxesEdit::*hi_field;
            std::optional<bool>   AxesEdit::*auto_field;
        };
        const AxisLim axes[2] = {
            { "X", &st.xauto_local, &st.xmin_local, &st.xmax_local,
              &AxesEdit::xmin, &AxesEdit::xmax, &AxesEdit::xlim_auto },
            { "Y", &st.yauto_local, &st.ymin_local, &st.ymax_local,
              &AxesEdit::ymin, &AxesEdit::ymax, &AxesEdit::ylim_auto },
        };
        if (begin_field_table("lim")) {
            for (const AxisLim& a : axes) {
                ImGui::PushID(a.label);
                // One row per axis: Auto, then low and high.
                field_row(a.label);
                if (ImGui::Checkbox("Auto", a.autoscale))
                    edit_box.update(idx, [&](AxesEdit& e){ e.*a.auto_field = *a.autoscale; });
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::BeginDisabled(*a.autoscale);
                const float speed = limit_drag_speed(*a.lo, *a.hi);
                // The checkbox used the fill width; request it again.
                ImGui::SetNextItemWidth(-FLT_MIN);
                split_begin(2);
                if (drag_double("##lo", a.lo, speed))
                    edit_box.update(idx, [&](AxesEdit& e){ e.*a.lo_field = *a.lo; e.*a.auto_field = false; });
                split_next();
                if (drag_double("##hi", a.hi, speed))
                    edit_box.update(idx, [&](AxesEdit& e){ e.*a.hi_field = *a.hi; e.*a.auto_field = false; });
                split_end();
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            end_field_table();
        }
        ImGui::SeparatorText("Ticks & labels");
        // Mark over Label in one table, so pairs line up.
        if (begin_field_table("tick", 3)) {
            field_row("Mark");
            if (color_swatch("##tickcol", sty.tick_color)) push_style();
            field_next("Length");
            if (drag_float("##ticklen", &sty.tick_length, 0.0f, 20.0f, 0.1f, "%.1f px")) push_style();
            field_next("Width");
            if (drag_float("##tickw", &sty.tick_linewidth, 0.5f, 6.0f, 0.02f, "%.2f px")) push_style();
            field_row("Label");
            if (color_swatch("##labelcol", sty.label_color)) push_style();
            field_next("Size");
            if (drag_float("##labelsz", &sty.label_fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_style();
            end_field_table();
        }

        ImGui::TextDisabled("X ticks");
        if (draw_tick_table("xticks", st.xticks_scratch))
            edit_box.update(idx, [&](AxesEdit& e){ e.xticks_override = st.xticks_scratch; });
        ImGui::TextDisabled("Y ticks");
        if (draw_tick_table("yticks", st.yticks_scratch))
            edit_box.update(idx, [&](AxesEdit& e){ e.yticks_override = st.yticks_scratch; });
    }

    // ==== Legend & colorbar ===============================================
    // Shared with the 3D panel.
    draw_legend_colorbar_group<AxesEdit>(
        st, !find_colorbar_requests(*cur2d).empty(),
        [&](auto&& fn){ edit_box.update(idx, fn); });

    ImGui::End();
}

namespace {

// The main menu bar. Must run before ensure_layout()/DockSpaceOverViewport(),
// so the dockspace sees the work area shrunk by the menu bar.
void draw_menu_bar(const FigureSnapshot& fsnap, PanelState& st) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save")) {
                if (!st.save_dialog_open) {
                    // Prefill with the live plot size.
                    st.save_width  = st.live_plot_w.load(std::memory_order_relaxed);
                    st.save_height = st.live_plot_h.load(std::memory_order_relaxed);
                }
                st.save_dialog_open = true;
            }
            if (ImGui::MenuItem("Resize to plot frame")) {
                // Prefill with the selected axes' current frame.
                if (!st.resize_dialog_open) {
                    const int lw = st.live_plot_w.load(std::memory_order_relaxed);
                    const int lh = st.live_plot_h.load(std::memory_order_relaxed);
                    if (lw > 0 && lh > 0) {
                        const FigureLayout fl =
                            compute_figure_layout(fsnap, *on_screen_measure(st, fsnap), lw, lh);
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
            // Re-measure on demand (e.g. tick labels frozen by navigation).
            if (ImGui::MenuItem("Refit layout"))
                st.layout.request_refit();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Cosmetic Panel", nullptr, &st.cosmetic_visible);
            // Turning Data on brings it to the front; ensure_layout() clears
            // the flag.
            if (ImGui::MenuItem("Data Panel", nullptr, &st.data_visible))
                st.focus_data_on_rebuild = st.data_visible;
            ImGui::EndMenu();
        }
        // Interaction modes: they govern the mouse over the plot.
        if (ImGui::BeginMenu("Edit")) {
            ImGui::MenuItem("Navigate", nullptr, &st.navigate_enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Left-drag pans, scroll zooms at the cursor,\n"
                                  "double-click resets — on the selected subplot.");
            ImGui::MenuItem("Hints", nullptr, &st.hints_enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show a tooltip for the data point under the cursor.");
            ImGui::EndMenu();
        }
        // The subplot every panel edits (clicking a subplot also sets it).
        if (fsnap.axes.size() > 1) {
            int sel = st.selected_slot_index;
            ImGui::SetNextItemWidth(220.0f);
            if (axes_selector("##axessel", fsnap, sel))
                select_slot(st, fsnap, sel);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The subplot the panels edit and Navigate moves.\n"
                                  "Clicking a subplot selects it too.");
        }
        ImGui::EndMainMenuBar();
    }
}

// Floating save dialog (NoDocking). The "Figure / Plot frame" selector is
// shared with the Resize dialog; in PlotFrame mode the figure size is derived
// and shown.
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
            const LayoutSize s = figure_size_for_frame(fsnap, *on_screen_measure(st, fsnap),
                                                       st.selected_slot_index,
                                                       static_cast<float>(*w),
                                                       static_cast<float>(*h));
            ImGui::TextDisabled("Figure becomes %.0f x %.0f (axis %d)",
                                static_cast<double>(s.width), static_cast<double>(s.height),
                                st.selected_slot_index);
        }
        // A frame size only determines the axes it was asked about.
        if (fsnap.axes.size() > 1)
            ImGui::TextDisabled("Other subplots may differ (legend/colorbar).");
    } else {
        ImGui::TextDisabled("%s", frame_hint);
    }
}

// Whether the filename asks for SVG (the same test the save site uses).
bool save_path_is_svg(const char* path) {
    const std::string s = path ? path : "";
    const auto dot = s.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string ext = s.substr(dot);
    return ext == ".svg" || ext == ".SVG";
}

bool scene_has_3d(const FigureSnapshot& fsnap) {
    for (const FigureAxesSnapshot& a : fsnap.axes)
        if (a.snap3d()) return true;
    return false;
}

// The warning window raised by a knowingly misordered export (the file is
// already written). Shows the exporter's own sentence. An undocked window
// rather than a popup, since it is raised outside any window scope.
void draw_save_warning(PanelState& st) {
    if (st.save_warning.empty()) return;
    st.save_warning_open = false;

    ImGui::SetNextWindowSize(ImVec2(430, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Export warning", nullptr,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextWrapped("The file was written, but part of it is not in the "
                       "right order.");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(410.0f);
    ImGui::TextUnformatted(st.save_warning.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("OK")) st.save_warning.clear();
    ImGui::End();
}

void draw_save_dialog(const FigureSnapshot& fsnap, PanelState& st) {
    if (!st.save_dialog_open) return;

    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Save Figure", &st.save_dialog_open,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::InputText("File", st.save_path_buf, sizeof(st.save_path_buf));
    size_mode_fields(fsnap, st, st.save_size_mode, &st.save_width, &st.save_height,
                     "<=0 uses the Plot panel's current size.");

    // Export bounds, only for the chosen format and only when the figure has
    // 3D (0 = automatic).
    if (scene_has_3d(fsnap)) {
        ImGui::Separator();
        if (save_path_is_svg(st.save_path_buf)) {
            ImGui::InputInt("Max splits", &st.save_max_splits);
            if (st.save_max_splits < 0) st.save_max_splits = 0;
            ImGui::TextDisabled("0 = automatic (8 x polygons + 64).");
            ImGui::TextDisabled("Raise if a save reports it gave up.");
        } else {
            ImGui::InputInt("Peel layers", &st.save_peel_layers);
            st.save_peel_layers = std::clamp(st.save_peel_layers, 0, 64);
            ImGui::TextDisabled("0 = automatic (8). Translucent layers a ray");
            ImGui::TextDisabled("may cross before the rest is dropped.");
        }
    }

    if (ImGui::Button("Save")) {
        st.save_requested = true;
        st.save_dialog_open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        st.save_dialog_open = false;

    ImGui::End();
}

// Resizes the window so the selected subplot's frame gets the requested size.
void draw_resize_dialog(const FigureSnapshot& fsnap, PanelState& st) {
    if (!st.resize_dialog_open) return;

    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Resize", &st.resize_dialog_open,
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);

    // Always frame-driven (dragging the window edge resizes the figure).
    PanelState::SizeMode mode = PanelState::SizeMode::PlotFrame;
    size_mode_fields(fsnap, st, mode, &st.resize_frame_w, &st.resize_frame_h, "");

    ImGui::BeginDisabled(st.resize_frame_w <= 0 || st.resize_frame_h <= 0);
    if (ImGui::Button("Apply")) {
        const LayoutSize s = figure_size_for_frame(fsnap, *on_screen_measure(st, fsnap),
                                                   st.selected_slot_index,
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

// Applies a pending plot-area size to the window, adding the chrome measured
// from the current frame. Render thread only (the request is an atomic); the
// window itself is resized by the pumping thread, from the link's queue.
void apply_pending_resize(GLContext& ctx, PanelState& st) {
    const int want_w = st.pending_plot_w.load(std::memory_order_relaxed);
    const int want_h = st.pending_plot_h.load(std::memory_order_relaxed);
    if (want_w <= 0 || want_h <= 0) return;

    const int plot_w = st.live_plot_w.load(std::memory_order_relaxed);
    const int plot_h = st.live_plot_h.load(std::memory_order_relaxed);
    // Nothing rendered yet: leave the request pending.
    if (plot_w <= 0 || plot_h <= 0) return;

    st.pending_plot_w.store(0, std::memory_order_relaxed);
    st.pending_plot_h.store(0, std::memory_order_relaxed);

    // The request is logical; the window chrome around the plot is measured in
    // framebuffer pixels, so the plot goes through the display scale first.
    const int plot_fb_w = st.live_plot_fb_w.load(std::memory_order_relaxed);
    const int plot_fb_h = st.live_plot_fb_h.load(std::memory_order_relaxed);
    const float display_scale = std::max(ctx.link().content_scale(), 0.01f);
    const int target_fb_w = static_cast<int>(std::lround(want_w * display_scale))
                            + (ctx.width()  - plot_fb_w);
    const int target_fb_h = static_cast<int>(std::lround(want_h * display_scale))
                            + (ctx.height() - plot_fb_h);
    if (target_fb_w <= 0 || target_fb_h <= 0) return;

    // A window is sized in screen coordinates, not framebuffer pixels.
    int win_w = 0, win_h = 0, fb_w = 0, fb_h = 0;
    ctx.link().window_size(win_w, win_h);
    ctx.link().framebuffer_size(fb_w, fb_h);
    const double sx = (fb_w > 0) ? static_cast<double>(win_w) / fb_w : 1.0;
    const double sy = (fb_h > 0) ? static_cast<double>(win_h) / fb_h : 1.0;

    ctx.link().post_resize(static_cast<int>(std::lround(target_fb_w * sx)),
                           static_cast<int>(std::lround(target_fb_h * sy)));
}

} // namespace

void draw_widget_panel(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                       PlotFbo& plot_fbo, const FigureSnapshot& fsnap,
                       const FigureOptions& opts,
                       FigureEditBox& edit_box, PanelState& st)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSextant_NewFrame(ctx.link());
    ImGui::NewFrame();

    draw_menu_bar(fsnap, st);

    const ImGuiID dockspace_id = ImGui::GetID("SextantDockspace");
    ensure_layout(dockspace_id,
                  opts.panel_width * ImGui::GetStyle().FontScaleDpi, st);
    ImGui::DockSpaceOverViewport(dockspace_id);

    draw_plot_panel(ctx, nvg, data, plot_fbo, fsnap, edit_box, st, opts.supersample);
    if (st.cosmetic_visible)
        draw_cosmetic_panel(fsnap, edit_box, st);
    if (st.data_visible)
        draw_data_panel(fsnap, edit_box, st);
    // After both side panels have begun (see ensure_layout()).
    if (st.pending_panel_focus) {
        ImGui::SetWindowFocus(st.pending_panel_focus);
        st.pending_panel_focus = nullptr;
    }
    draw_save_dialog(fsnap, st);
    // Reads the stored warning (set by a save one frame earlier).
    draw_save_warning(st);
    draw_resize_dialog(fsnap, st);

    // After the plot panel has published this frame's live size.
    apply_pending_resize(ctx, st);

    // Serviced here since a PNG save needs ctx/nvg/data (safe mid-frame: its
    // own FBO). Exports the render thread's snapshot, including panel edits,
    // laid out with the window's measurements. Width/height <= 0 = live size.
    if (st.save_requested) {
        st.save_requested = false;
        const auto on_screen = on_screen_measure(st, fsnap);
        int sw = st.save_width  > 0 ? st.save_width  : st.live_plot_w.load(std::memory_order_relaxed);
        int sh = st.save_height > 0 ? st.save_height : st.live_plot_h.load(std::memory_order_relaxed);
        if (st.save_size_mode == PanelState::SizeMode::PlotFrame
            && st.save_width > 0 && st.save_height > 0) {
            const LayoutSize s = figure_size_for_frame(fsnap, *on_screen,
                                                       st.selected_slot_index,
                                                       static_cast<float>(st.save_width),
                                                       static_cast<float>(st.save_height));
            sw = static_cast<int>(std::lround(s.width));
            sh = static_cast<int>(std::lround(s.height));
        }
        if (sw > 0 && sh > 0) {
            const std::string path = st.save_path_buf;
            const auto dot = path.rfind('.');
            const auto ext = (dot == std::string::npos) ? "" : path.substr(dot);
            if (ext == ".svg" || ext == ".SVG") {
                SvgSaveReport report;
                export_figure_svg(fsnap, path, sw, sh,
                                  { .max_splits = static_cast<std::size_t>(
                                        std::max(0, st.save_max_splits)) },
                                  &report, on_screen.get());
                // Tell the user: the file is written either way.
                if (!report.scene_order_exact) {
                    st.save_warning      = report.warning;
                    st.save_warning_open = true;
                }
            } else {
                // At the figure's dpi (1x by default), not the display's: a
                // file is the same on every machine.
                export_figure_png(ctx, nvg, data, fsnap, path, sw, sh,
                                  opts.supersample, st.save_peel_layers, on_screen.get(),
                                  opts.dpi / 96.0f);
            }
        }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace sextant
