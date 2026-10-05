#include "panel.h"
#include "data_panel.h"
#include "panel_state.h"
#include "figure_context.h"
#include "panel_widgets.h"
#include "../figure_edits.h"
#include "../edit_box.h"
#include "../plot_objects.h"
#include "../render_frame.h"
#include "../figure_export.h"
#include "../plot_data_view.h"
#include "../renderer/figure_layout.h"
#include "../font_discovery.h"
#include "../messages.h"
#include "sextant/figure.h"
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace sextant {

std::shared_ptr<const FigureMeasure> on_screen_measure(const PlotViewInfo* view,
                                                       const FigureSnapshot& fsnap) {
    if (view && view->measure) return view->measure;
    return std::make_shared<const FigureMeasure>(measure_figure(fsnap));
}

namespace {

// A sheet's 2D appearance scratch, without hint_labels.
void sync_sheet(DataPanelState::SheetStyles& dst, const RenderSnapshot& sn) {
    auto copy = [](auto& out, const auto& plots) {
        out.clear();
        out.reserve(plots.size());
        for (const auto& p : plots) {
            out.push_back(p.opts);
            out.back().hint_labels.clear();
        }
    };
    copy(dst.lines, sn.lines);
    copy(dst.scatters, sn.scatters);
    copy(dst.bars, sn.bars);
    copy(dst.heatmaps, sn.heatmaps);
    copy(dst.scatter_z, sn.scatter_z);
}

bool sheet_counts_differ(const DataPanelState::SheetStyles& s, const RenderSnapshot& sn) {
    return s.lines.size() != sn.lines.size() || s.scatters.size() != sn.scatters.size()
        || s.bars.size() != sn.bars.size() || s.heatmaps.size() != sn.heatmaps.size()
        || s.scatter_z.size() != sn.scatter_z.size();
}

// Seeding from the selected slot, one owner at a time (GUI-kit R3). Each state
// pulls when its user next runs: seed_* when the Selection generation moved,
// the per-frame follows otherwise.

// The Cosmetic inspector's per-slot fields. Takes the 2D snapshot; never call
// with a 3D cell.
void seed_cosmetic(CosmeticState& c, const RenderSnapshot& sn) {
    std::snprintf(c.title_buf,  sizeof(c.title_buf),  "%s", sn.title.c_str());
    std::snprintf(c.xtitle_buf, sizeof(c.xtitle_buf), "%s", sn.xtitle.c_str());
    std::snprintf(c.ytitle_buf, sizeof(c.ytitle_buf), "%s", sn.ytitle.c_str());
    c.grid_local = sn.grid_enabled;
    c.xticks_scratch = sn.xticks_override.value_or(std::vector<Tick>{});
    c.yticks_scratch = sn.yticks_override.value_or(std::vector<Tick>{});
    c.axes_style_local = sn.axes_style;
    c.origin_x_scratch = sn.axes_style.origin_x.value_or(0.0);
    c.origin_y_scratch = sn.axes_style.origin_y.value_or(0.0);
    c.grid_opts_local  = sn.grid_opts;
    c.legend_enabled_local = sn.legend_enabled;
    c.legend_local     = sn.legend_opts;
    c.colorbar_local   = sn.colorbar_opts;
}

// The 3D counterpart: the shared fields and the 3D-only ones.
void seed_cosmetic(CosmeticState& c, const RenderSnapshot3D& sn) {
    std::snprintf(c.title_buf,  sizeof(c.title_buf),  "%s", sn.title.c_str());
    std::snprintf(c.xtitle_buf, sizeof(c.xtitle_buf), "%s", sn.xtitle.c_str());
    std::snprintf(c.ytitle_buf, sizeof(c.ytitle_buf), "%s", sn.ytitle.c_str());
    std::snprintf(c.ztitle_buf, sizeof(c.ztitle_buf), "%s", sn.ztitle.c_str());
    c.grid_local = sn.grid_enabled;
    c.xticks_scratch = sn.xticks_override.value_or(std::vector<Tick>{});
    c.yticks_scratch = sn.yticks_override.value_or(std::vector<Tick>{});
    c.zticks_scratch = sn.zticks_override.value_or(std::vector<Tick>{});
    c.axes_style_local = sn.axes_style;
    c.origin_x_scratch = sn.axes_style.origin_x.value_or(0.0);
    c.origin_y_scratch = sn.axes_style.origin_y.value_or(0.0);
    c.origin_z_scratch = sn.axes_style.origin_z.value_or(0.0);
    c.grid_opts_local  = sn.grid_opts;
    c.legend_enabled_local = sn.legend_enabled;
    c.legend_local     = sn.legend_opts;
    c.colorbar_local   = sn.colorbar_opts;
    c.box3d_local      = sn.box_style;
    c.aspect_local     = sn.aspect;
}

// The shared slot view: limits, and in 3D the camera.
void seed_slot_view(SlotViewState& v, const RenderSnapshot& sn) {
    v.xauto_local = sn.xlim_auto; v.xmin_local = sn.xmin; v.xmax_local = sn.xmax;
    v.yauto_local = sn.ylim_auto; v.ymin_local = sn.ymin; v.ymax_local = sn.ymax;
    v.limit_stamps_local = sn.limit_stamps;
}

void seed_slot_view(SlotViewState& v, const RenderSnapshot3D& sn) {
    v.xauto_local = sn.xlim_auto; v.xmin_local = sn.xmin; v.xmax_local = sn.xmax;
    v.yauto_local = sn.ylim_auto; v.ymin_local = sn.ymin; v.ymax_local = sn.ymax;
    v.zauto_local = sn.zlim_auto; v.zmin_local = sn.zmin; v.zmax_local = sn.zmax;
    v.limit_stamps_local = sn.limit_stamps;
    v.camera_local       = sn.camera;
    v.camera_stamp_local = sn.camera_stamp;
}

// The planes' scratch copy, also re-seeded when the plane count changes
// (indices shift, so the whole list is re-read).
void sync_planes(DataPanelState& d, const RenderSnapshot3D& sn) {
    d.planes_local.clear();
    d.planes_local.reserve(sn.planes.size());
    for (const auto& p : sn.planes)
        d.planes_local.push_back({ p.orient, p.offset, p.opts });
}

// Every plane's sheet, re-seeded as one list (like sync_planes()).
void sync_plane_sheets(DataPanelState& d, const RenderSnapshot3D& sn) {
    d.plane_sheets_local.resize(sn.planes.size());
    for (std::size_t p = 0; p < sn.planes.size(); ++p)
        sync_sheet(d.plane_sheets_local[p], sn.planes[p].sheet);
}

bool plane_sheets_differ(const DataPanelState& d, const RenderSnapshot3D& sn) {
    if (d.plane_sheets_local.size() != sn.planes.size()) return true;
    for (std::size_t p = 0; p < sn.planes.size(); ++p)
        if (sheet_counts_differ(d.plane_sheets_local[p], sn.planes[p].sheet)) return true;
    return false;
}

// The 3D kinds' appearance, on the same rule.
void sync_scene_objects(DataPanelState& d, const RenderSnapshot3D& sn) {
    d.bars3d_local.clear();
    d.bars3d_local.reserve(sn.bars3d.size());
    for (const auto& b : sn.bars3d) d.bars3d_local.push_back(b.opts);
    d.surfaces_local.clear();
    d.surfaces_local.reserve(sn.surfaces.size());
    for (const auto& s : sn.surfaces) d.surfaces_local.push_back(s.opts);
    d.scatter3d_local.clear();
    d.scatter3d_local.reserve(sn.scatter3d.size());
    for (const auto& c : sn.scatter3d) d.scatter3d_local.push_back(c.opts);
    d.line3d_local.clear();
    d.line3d_local.reserve(sn.lines3d.size());
    for (const auto& l : sn.lines3d) d.line3d_local.push_back(l.opts);
    d.surface_tri_local.clear();
    d.surface_tri_local.reserve(sn.surface_tri.size());
    for (const auto& m : sn.surface_tri) d.surface_tri_local.push_back(m.opts);
}

// For axes on "auto", the limit fields track the resolved limits every frame
// (the snapshot only has declared defaults). Dragging a field clears `auto`,
// after which the declared value is correct. No view = no frame drawn (the
// headless panel test, or a figure no plot view shows): declared values.
void track_resolved_limits(SlotViewState& v, const PlotViewInfo* view, int slot,
                           bool xauto, bool yauto, bool zauto) {
    const ResolvedLimits* r = view ? view->resolved_for(slot) : nullptr;
    if (!r) return;
    if (xauto) { v.xmin_local = r->xmin; v.xmax_local = r->xmax; }
    if (yauto) { v.ymin_local = r->ymin; v.ymax_local = r->ymax; }
    if (zauto && r->is_3d) { v.zmin_local = r->zmin; v.zmax_local = r->zmax; }
}

// Figure-level: seeded once, not on slot change (would discard an edit).
void sync_figure_from_snapshot(CosmeticState& c, const FigureSnapshot& fsnap) {
    if (c.suptitle_synced) return;
    std::snprintf(c.suptitle_buf, sizeof(c.suptitle_buf), "%s", fsnap.suptitle.c_str());
    c.suptitle_local  = fsnap.suptitle_opts;
    c.suptitle_synced = true;
}

// Same once-only rule, with its own flag.
void sync_layout_from_snapshot(CosmeticState& c, const FigureSnapshot& fsnap) {
    if (c.layout_synced) return;
    c.margins_local = fsnap.margins;
    c.col_gap_local = fsnap.col_gap;
    c.row_gap_local = fsnap.row_gap;
    c.layout_synced = true;
}

// The Cosmetic inspector's own pull.
void pull_cosmetic(CosmeticState& c, const Selection& sel, const FigureAxesSnapshot& fa) {
    if (c.synced_generation == sel.generation) return;
    std::visit([&](const auto& sn) { seed_cosmetic(c, sn); }, fa.snap);
    c.synced_generation = sel.generation;
}

// What the Cosmetic helpers below reach: its own state, the shared slot view
// (already pulled), and the plot view if one shows the figure.
struct CosmeticRefs {
    CosmeticState&       cosmetic;
    SlotViewState&       slot_view;
    const PlotViewInfo*  view;
};

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

// The suptitle controls, figure-level and shared by the 2D and 3D panels
// (under the Figure group's "Suptitle" heading).
void draw_suptitle_fields(CosmeticRefs& st, FigureEditBox& edit_box) {
    auto push = [&]{ edit_box.update_figure([&](FigureEdits& f){ f.suptitle_opts = st.cosmetic.suptitle_local; }); };

    // Three tables (the middle row has two pairs); matching first columns.
    if (begin_field_table("suptxt")) {
        field_row("Suptitle");
        if (ImGui::InputText("##suptitle", st.cosmetic.suptitle_buf, sizeof(st.cosmetic.suptitle_buf)))
            edit_box.update_figure([&](FigureEdits& f){ f.suptitle = st.cosmetic.suptitle_buf; });
        field_row("Font");
        if (font_combo("##supfont", st.cosmetic.suptitle_local.font_path)) push();
        end_field_table();
    }
    if (begin_field_table("supcs", 2)) {
        field_row("Color");
        if (color_swatch("##supcol", st.cosmetic.suptitle_local.color)) push();
        field_next("Size");
        if (drag_float("##supsz", &st.cosmetic.suptitle_local.fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push();
        end_field_table();
    }
    if (begin_field_table("supal")) {
        field_row("Align");
        if (halign_combo("##supalign", st.cosmetic.suptitle_local.align)) push();
        field_row("Offset");
        split_begin(2);
        if (drag_float("##supox", &st.cosmetic.suptitle_local.offset_x, -2000.0f, 2000.0f, 0.5f, "x %.0f px")) push();
        split_next();
        if (drag_float("##supoy", &st.cosmetic.suptitle_local.offset_y, -2000.0f, 2000.0f, 0.5f, "y %.0f px")) push();
        split_end();
        end_field_table();
    }
}

// Margins and gaps: figure-level, shared by both panels (under "Layout").
// Decoration space is measured from text, so there is no control for it.
void draw_layout_fields(CosmeticRefs& st, const FigureSnapshot& fsnap,
                        FigureEditBox& edit_box, int idx) {
    auto push_margins = [&]{ edit_box.update_figure([&](FigureEdits& f){ f.margins = st.cosmetic.margins_local; }); };
    auto push_gaps    = [&]{ edit_box.update_figure([&](FigureEdits& f){
                                 f.col_gap = st.cosmetic.col_gap_local;
                                 f.row_gap = st.cosmetic.row_gap_local; }); };

    // Split rows: each value names its side; the row label carries the unit.
    if (begin_field_table("margins")) {
        field_row("Margin px");
        split_begin(4);
        if (drag_float("##marl", &st.cosmetic.margins_local.left, 0.0f, 2000.0f, 0.5f, "L %.0f")) push_margins();
        split_next();
        if (drag_float("##marr", &st.cosmetic.margins_local.right, 0.0f, 2000.0f, 0.5f, "R %.0f")) push_margins();
        split_next();
        if (drag_float("##mart", &st.cosmetic.margins_local.top, 0.0f, 2000.0f, 0.5f, "T %.0f")) push_margins();
        split_next();
        if (drag_float("##marb", &st.cosmetic.margins_local.bottom, 0.0f, 2000.0f, 0.5f, "B %.0f")) push_margins();
        split_end();
        end_field_table();
    }

    // Gaps only apply with more than one subplot.
    ImGui::BeginDisabled(fsnap.axes.size() <= 1);
    if (begin_field_table("gaps")) {
        field_row("Gap px");
        split_begin(2);
        if (drag_float("##gapc", &st.cosmetic.col_gap_local, 0.0f, 2000.0f, 0.5f, "col %.0f")) push_gaps();
        split_next();
        if (drag_float("##gapr", &st.cosmetic.row_gap_local, 0.0f, 2000.0f, 0.5f, "row %.0f")) push_gaps();
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
    const int live_w = st.view ? st.view->plot_w : 0;
    const int live_h = st.view ? st.view->plot_h : 0;
    if (live_w > 0 && live_h > 0) {
        const FigureLayout fl = compute_figure_layout(fsnap, *on_screen_measure(st.view, fsnap),
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
void draw_figure_group(CosmeticRefs& st, const FigureSnapshot& fsnap,
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
void draw_legend_colorbar_group(CosmeticRefs& st, bool has_colorbar, Push&& push) {
    auto push_legend   = [&]{ push([&](Edit& e){ e.legend_opts   = st.cosmetic.legend_local; }); };
    auto push_colorbar = [&]{ push([&](Edit& e){ e.colorbar_opts = st.cosmetic.colorbar_local; }); };

    if (!section("Legend & colorbar")) return;

    ImGui::SeparatorText("Legend");
    if (ImGui::Checkbox("Show##legend", &st.cosmetic.legend_enabled_local))
        push([&](Edit& e){ e.legend_enabled = st.cosmetic.legend_enabled_local; });
    ImGui::BeginDisabled(!st.cosmetic.legend_enabled_local);
    if (begin_field_table("legtext", 2)) {
        field_row("Text");
        if (color_swatch("##legtextcol", st.cosmetic.legend_local.text_color)) push_legend();
        field_next("Size");
        if (drag_float("##legsz", &st.cosmetic.legend_local.fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_legend();
        end_field_table();
    }
    if (begin_field_table("legpos")) {
        field_row("Font");
        if (font_combo("##legfont", st.cosmetic.legend_local.font_path)) push_legend();
        field_row("Anchor");
        if (legend_anchor_combo("##leganchor", st.cosmetic.legend_local.anchor)) push_legend();
        field_row("Margin");
        if (drag_float("##legmargin", &st.cosmetic.legend_local.margin, 0.0f, 400.0f, 0.5f, "%.0f px")) push_legend();
        field_row("Offset");
        split_begin(2);
        if (drag_float("##legox", &st.cosmetic.legend_local.offset_x, -400.0f, 400.0f, 0.5f, "x %.0f px")) push_legend();
        split_next();
        if (drag_float("##legoy", &st.cosmetic.legend_local.offset_y, -400.0f, 400.0f, 0.5f, "y %.0f px")) push_legend();
        split_end();
        end_field_table();
    }
    if (ImGui::Checkbox("Frame##legendframe", &st.cosmetic.legend_local.frameon)) push_legend();
    ImGui::BeginDisabled(!st.cosmetic.legend_local.frameon);
    if (begin_field_table("legendframe", 3)) {
        field_row("Fill");
        if (color_swatch("##legfill", st.cosmetic.legend_local.frame_color)) push_legend();
        field_next("Border");
        if (color_swatch("##legborder", st.cosmetic.legend_local.border_color)) push_legend();
        field_next("Width");
        if (drag_float("##legbw", &st.cosmetic.legend_local.border_linewidth, 0.0f, 6.0f, 0.02f, "%.2f px")) push_legend();
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
        if (color_swatch("##cbtextcol", st.cosmetic.colorbar_local.text_color)) push_colorbar();
        field_next("Size");
        if (drag_float("##cbsz", &st.cosmetic.colorbar_local.fontsize, 1.0f, 96.0f, 0.2f, "%.1f px")) push_colorbar();
        end_field_table();
    }
    if (begin_field_table("cbfont")) {
        field_row("Font");
        if (font_combo("##cbfont", st.cosmetic.colorbar_local.font_path)) push_colorbar();
        end_field_table();
    }
    if (begin_field_table("cbborder", 2)) {
        field_row("Border");
        if (color_swatch("##cbborder", st.cosmetic.colorbar_local.border_color)) push_colorbar();
        field_next("Width");
        if (drag_float("##cbbw", &st.cosmetic.colorbar_local.border_linewidth, 0.0f, 6.0f, 0.02f, "%.2f px")) push_colorbar();
        end_field_table();
    }
    if (begin_field_table("cbanchor")) {
        field_row("Anchor");
        if (colorbar_anchor_combo("##cbanchor", st.cosmetic.colorbar_local.anchor)) push_colorbar();
        end_field_table();
    }
    if (begin_field_table("cbsize", 2)) {
        field_row("Bar");
        if (drag_float("##cbwidth", &st.cosmetic.colorbar_local.width, 1.0f, 200.0f, 0.5f, "%.0f px")) push_colorbar();
        field_next("Margin");
        if (drag_float("##cbmargin", &st.cosmetic.colorbar_local.margin, 0.0f, 400.0f, 0.5f, "%.0f px")) push_colorbar();
        end_field_table();
    }
    if (begin_field_table("cboffset")) {
        field_row("Offset");
        split_begin(2);
        if (drag_float("##cbox", &st.cosmetic.colorbar_local.offset_x, -400.0f, 400.0f, 0.5f, "x %.0f px")) push_colorbar();
        split_next();
        if (drag_float("##cboy", &st.cosmetic.colorbar_local.offset_y, -400.0f, 400.0f, 0.5f, "y %.0f px")) push_colorbar();
        split_end();
        end_field_table();
    }
}

// The Cosmetic panel for a 3D slot (separate from 2D: nearly every section
// differs).
void draw_cosmetic_3d(CosmeticRefs& st, const RenderSnapshot3D& sn,
                      const FigureSnapshot& fsnap, FigureEditBox& edit_box, int idx) {
    auto& sty = st.cosmetic.axes_style_local;

    auto push_style  = [&]{ edit_box.update3d(idx, [&](AxesEdit3D& e){ e.axes_style = sty; }); };
    auto push_grid   = [&]{ edit_box.update3d(idx, [&](AxesEdit3D& e){ e.grid_opts  = st.cosmetic.grid_opts_local; }); };
    auto push_camera = [&]{ st.slot_view.camera_local = clamp_camera(st.slot_view.camera_local);
                            edit_box.update3d(idx, [&](AxesEdit3D& e){
                                e.camera = st.slot_view.camera_local;
                                e.camera_seen = sn.camera_stamp;
                            }); };
    auto push_box    = [&]{ edit_box.update3d(idx, [&](AxesEdit3D& e){ e.box_style = st.cosmetic.box3d_local; }); };
    // Clamped: Axes3D::set_box_aspect() rejects non-positive sides.
    auto push_aspect = [&]{
        st.cosmetic.aspect_local.x = std::clamp(st.cosmetic.aspect_local.x, 0.05, 20.0);
        st.cosmetic.aspect_local.y = std::clamp(st.cosmetic.aspect_local.y, 0.05, 20.0);
        st.cosmetic.aspect_local.z = std::clamp(st.cosmetic.aspect_local.z, 0.05, 20.0);
        edit_box.update3d(idx, [&](AxesEdit3D& e){ e.aspect = st.cosmetic.aspect_local; });
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
        const bool persp = st.slot_view.camera_local.projection == Projection::Perspective;
        if (begin_field_table("cam3d", 2)) {
            field_row("Azim");
            if (drag_double("##azim", &st.slot_view.camera_local.azimuth, 0.25f, "%.1f deg")) push_camera();
            field_next("Elev");
            if (drag_double("##elev", &st.slot_view.camera_local.elevation, 0.25f, "%.1f deg")) push_camera();
            // FOV only under perspective.
            field_row("Zoom");
            if (drag_double("##zoom3d", &st.slot_view.camera_local.zoom, 0.005f, "%.2fx")) push_camera();
            if (persp) {
                field_next("FOV");
                if (drag_double("##fov3d", &st.slot_view.camera_local.fov, 0.1f, "%.0f deg")) push_camera();
            }
            end_field_table();
        }
        if (begin_field_table("cam3dp")) {
            field_row("Projection");
            if (projection_combo("##proj3d", st.slot_view.camera_local.projection)) push_camera();
            end_field_table();
        }
        // No distance control: the box is fitted every frame, and under
        // perspective fov determines the eye distance; zoom magnifies.
        ImGui::TextDisabled("Target %.2f, %.2f, %.2f",
                            st.slot_view.camera_local.target.x, st.slot_view.camera_local.target.y,
                            st.slot_view.camera_local.target.z);
        if (persp)
            ImGui::TextDisabled("Wider fov = closer camera. The box fills the cell either way.");
        if (ImGui::SmallButton("Reset view")) {
            st.slot_view.camera_local = sn.default_camera;
            edit_box.update3d(idx, [&](AxesEdit3D& e){
                e.camera = st.slot_view.camera_local;
                e.camera_seen = sn.camera_stamp;
            });
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
            if (drag_double("##aspx", &st.cosmetic.aspect_local.x, 0.005f, "x %.2f")) push_aspect();
            split_next();
            if (drag_double("##aspy", &st.cosmetic.aspect_local.y, 0.005f, "y %.2f")) push_aspect();
            split_next();
            if (drag_double("##aspz", &st.cosmetic.aspect_local.z, 0.005f, "z %.2f")) push_aspect();
            split_end();
            field_row("Margin");
            if (drag_float("##boxmargin", &st.cosmetic.box3d_local.margin, 0.0f, 0.45f, 0.001f, "%.3f")) push_box();
            end_field_table();
        }
        // Chosen, not measured: 3D label positions depend on the camera fit.
        ImGui::TextDisabled("Margin reserves room for labels, which move with the camera.");
        if (ImGui::Checkbox("Panes", &st.cosmetic.box3d_local.panes)) push_box();
        if (begin_field_table("boxcol", 2)) {
            field_row("Color");
            if (color_swatch("##panecol", st.cosmetic.box3d_local.pane_color)) push_box();
            field_next("Edge color");
            if (color_swatch("##paneedge", st.cosmetic.box3d_local.pane_edge_color)) push_box();
            end_field_table();
        }
        // "##grid3d" suffix avoids an id clash with a same-named header.
        if (ImGui::Checkbox("Grid##grid3d", &st.cosmetic.grid_local))
            edit_box.update3d(idx, [&](AxesEdit3D& e){ e.grid_enabled = st.cosmetic.grid_local; });
        ImGui::BeginDisabled(!st.cosmetic.grid_local);
        if (begin_field_table("grid3d", 3)) {
            field_row("Color");
            if (color_swatch("##gridcol3d", st.cosmetic.grid_opts_local.color)) push_grid();
            field_next("Style");
            if (linestyle_combo("##gridls3d", st.cosmetic.grid_opts_local.linestyle)) push_grid();
            field_next("Width");
            if (drag_float("##gridw3d", &st.cosmetic.grid_opts_local.linewidth, 0.1f, 10.0f, 0.05f, "%.2f px")) push_grid();
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
            unsigned long long TitleStamps::*stamp;
            Color* color; float* size;
        };
        const TitleUi titles[4] = {
            { "Title",   "title",  st.cosmetic.title_buf,  sizeof(st.cosmetic.title_buf),  &AxesEdit3D::title, &TitleStamps::title,
              &sty.title_color,  &sty.title_fontsize },
            { "X title", "xtitle", st.cosmetic.xtitle_buf, sizeof(st.cosmetic.xtitle_buf), &AxesEdit3D::xtitle, &TitleStamps::xtitle,
              &sty.xtitle_color, &sty.xtitle_fontsize },
            { "Y title", "ytitle", st.cosmetic.ytitle_buf, sizeof(st.cosmetic.ytitle_buf), &AxesEdit3D::ytitle, &TitleStamps::ytitle,
              &sty.ytitle_color, &sty.ytitle_fontsize },
            { "Z title", "ztitle", st.cosmetic.ztitle_buf, sizeof(st.cosmetic.ztitle_buf), &AxesEdit3D::ztitle, &TitleStamps::ztitle,
              &sty.ztitle_color, &sty.ztitle_fontsize },
        };
        for (const TitleUi& t : titles) {
            ImGui::PushID(t.id);
            if (begin_field_table("text")) {
                field_row(t.label);
                if (ImGui::InputText("##text", t.buf, t.cap))
                    edit_box.update3d(idx, [&](AxesEdit3D& e){
                        e.*t.text = std::string(t.buf);
                        e.title_seen.*t.stamp = sn.title_stamps.*t.stamp;
                    });
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
                { "at x", &sty.origin_x, &st.cosmetic.origin_x_scratch, &st.slot_view.xmin_local, &st.slot_view.xmax_local },
                { "at y", &sty.origin_y, &st.cosmetic.origin_y_scratch, &st.slot_view.ymin_local, &st.slot_view.ymax_local },
                { "at z", &sty.origin_z, &st.cosmetic.origin_z_scratch, &st.slot_view.zmin_local, &st.slot_view.zmax_local },
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
            { "X", &st.slot_view.xmin_local, &st.slot_view.xmax_local,
              &AxesEdit3D::xmin, &AxesEdit3D::xmax, &AxesEdit3D::xlim_auto },
            { "Y", &st.slot_view.ymin_local, &st.slot_view.ymax_local,
              &AxesEdit3D::ymin, &AxesEdit3D::ymax, &AxesEdit3D::ylim_auto },
            { "Z", &st.slot_view.zmin_local, &st.slot_view.zmax_local,
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
                        e.lim_seen = sn.limit_stamps;
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
            { "X ticks", "xt3d", &st.cosmetic.xticks_scratch, &AxesEdit3D::xticks_override },
            { "Y ticks", "yt3d", &st.cosmetic.yticks_scratch, &AxesEdit3D::yticks_override },
            { "Z ticks", "zt3d", &st.cosmetic.zticks_scratch, &AxesEdit3D::zticks_override },
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

namespace {

// An axis the program set since the limits were seeded (its stamp moved)
// re-seeds from the snapshot, so the Limits fields show what is drawn.
template <typename Snap>
void follow_program_limits(SlotViewState& v, const Snap& sn) {
    LimitStamps& have = v.limit_stamps_local;
    if (have.x != sn.limit_stamps.x) {
        v.xauto_local = sn.xlim_auto; v.xmin_local = sn.xmin; v.xmax_local = sn.xmax;
    }
    if (have.y != sn.limit_stamps.y) {
        v.yauto_local = sn.ylim_auto; v.ymin_local = sn.ymin; v.ymax_local = sn.ymax;
    }
    if constexpr (requires { sn.zmin; })
        if (have.z != sn.limit_stamps.z) {
            v.zauto_local = sn.zlim_auto; v.zmin_local = sn.zmin; v.zmax_local = sn.zmax;
        }
    have = sn.limit_stamps;
}

} // namespace

const FigureAxesSnapshot* normalize_selection(Selection& sel, const FigureSnapshot& fsnap) {
    if (fsnap.axes.empty()) return nullptr;
    const FigureAxesSnapshot* fa = axes_for_slot(fsnap, sel.slot);
    if (!fa) fa = &fsnap.axes.front();
    // Normalized: File > Resize and the Save dialog read selection.slot.
    sel.select(fa->slot.index);
    return fa;
}

SlotViewState& slot_view(FigureContext& ctx) {
    SlotViewState& v = ctx.slot_view_state;
    const FigureAxesSnapshot* fa = normalize_selection(ctx.selection, ctx.snap);
    if (!fa) return v;
    if (v.synced_generation != ctx.selection.generation) {
        std::visit([&](const auto& sn) { seed_slot_view(v, sn); }, fa->snap);
        v.synced_generation = ctx.selection.generation;
        return v;
    }
    std::visit([&](const auto& sn) { follow_program_limits(v, sn); }, fa->snap);
    if (const RenderSnapshot3D* sn = fa->snap3d()) {
        // The program set the camera since it was seeded: follow it, or the next
        // drag would start from a view no longer on screen.
        if (v.camera_stamp_local != sn->camera_stamp) {
            v.camera_local = sn->camera;
            v.camera_stamp_local = sn->camera_stamp;
        }
    }
    return v;
}

void pull_data_panel(DataPanelState& d, const Selection& sel, const FigureAxesSnapshot& fa) {
    if (d.synced_generation != sel.generation) {
        if (const RenderSnapshot3D* sn = fa.snap3d()) {
            sync_planes(d, *sn);
            sync_plane_sheets(d, *sn);
            sync_scene_objects(d, *sn);
        } else if (const RenderSnapshot* sn = fa.snap2d()) {
            sync_sheet(d.sheet_local, *sn);
        }
        d.synced_generation = sel.generation;
        return;
    }
    // Object count changed: re-seed the lists (indices shifted).
    if (const RenderSnapshot3D* sn = fa.snap3d()) {
        if (d.planes_local.size() != sn->planes.size())
            sync_planes(d, *sn);
        if (plane_sheets_differ(d, *sn))
            sync_plane_sheets(d, *sn);
        if (d.bars3d_local.size() != sn->bars3d.size() ||
            d.surfaces_local.size() != sn->surfaces.size() ||
            d.scatter3d_local.size() != sn->scatter3d.size() ||
            d.line3d_local.size() != sn->lines3d.size() ||
            d.surface_tri_local.size() != sn->surface_tri.size())
            sync_scene_objects(d, *sn);
    } else if (const RenderSnapshot* sn = fa.snap2d()) {
        if (sheet_counts_differ(d.sheet_local, *sn)) sync_sheet(d.sheet_local, *sn);
    }
}

void push_figure_id(std::uint64_t figure_id) {
    // A process-wide counter: an int holds any id a run will reach.
    ImGui::PushID(static_cast<int>(figure_id));
}

PlotNavGate update_plot_selection(Selection& sel, PlotViewState& pv, const FigureSnapshot& fsnap,
                                  const std::vector<AxesLayout>& layout,
                                  const PlotPointer& in) {
    PlotNavGate gate;
    // Navigation pulls the shared slot view itself, after any change made here.
    const FigureAxesSnapshot* cur = normalize_selection(sel, fsnap);
    const int selected = cur ? cur->slot.index : -1;
    if (selected < 0) {
        pv.press_slot = -1;
        pv.press_on_selected = false;
        return gate;
    }

    const AxesLayout* under = find_cell_at(layout, in.x, in.y);
    const int under_slot = under ? under->slot.index : -1;

    if (in.pressed) {
        pv.press_slot        = under_slot;
        pv.press_on_selected = under_slot == selected;
        // A double-click whose first click selected this cell doesn't reset it.
        gate.reset = in.double_clicked && pv.press_on_selected
                     && !pv.selected_by_last_click;
        pv.selected_by_last_click = false;
    }

    gate.drag = in.active && pv.press_on_selected;
    const bool over_selected = in.hovered && under_slot == selected;
    gate.wheel = over_selected;
    gate.keys  = over_selected || gate.drag;

    if (in.released) {
        // A click (not a drag) that ends in the cell it began in.
        if (!in.dragged && pv.press_slot >= 0 && under_slot == pv.press_slot
            && pv.press_slot != selected) {
            sel.select(pv.press_slot);
            pv.selected_by_last_click = true;
        }
        pv.press_slot        = -1;
        pv.press_on_selected = false;
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

GridDragOut update_grid_drag(PlotViewState& pv, const FigureSnapshot& fsnap,
                             const FigureLayout& layout, int fig_w, int fig_h,
                             const PlotPointer& in, float tol) {
    GridDragOut out;
    PlotViewState::GridDrag& g = pv.grid_drag;
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
void draw_cosmetic_panel(FigureContext& ctx, CosmeticState& cosmetic) {
    const FigureSnapshot& fsnap = ctx.snap;
    FigureEditBox& edit_box = ctx.edits;

    // Navigate and Hints are in the Edit menu; this panel edits the selected
    // axes only.
    const FigureAxesSnapshot* cur = normalize_selection(ctx.selection, fsnap);
    if (!cur) {
        ImGui::TextDisabled("No axes yet.");
        return;
    }

    // The selection comes from the menu bar or a click. This panel's own fields
    // re-seed when it has moved; the shared slot view re-seeds on access.
    pull_cosmetic(cosmetic, ctx.selection, *cur);
    CosmeticRefs st{ cosmetic, slot_view(ctx), ctx.view };

    // Separate functions per kind; they share the Figure group.
    sync_figure_from_snapshot(st.cosmetic, fsnap);
    sync_layout_from_snapshot(st.cosmetic, fsnap);

    const int idx = cur->slot.index;

    if (const RenderSnapshot3D* cur3d = cur->snap3d()) {
        track_resolved_limits(st.slot_view, st.view, idx, cur3d->xlim_auto, cur3d->ylim_auto,
                              cur3d->zlim_auto);
        draw_cosmetic_3d(st, *cur3d, fsnap, edit_box, idx);
        return;
    }

    const RenderSnapshot* cur2d = cur->snap2d();
    track_resolved_limits(st.slot_view, st.view, idx, cur2d->xlim_auto, cur2d->ylim_auto, false);

    // Each group republishes its whole options struct on change.
    auto& sty = st.cosmetic.axes_style_local;
    auto push_style    = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.axes_style     = sty; }); };
    auto push_grid     = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.grid_opts      = st.cosmetic.grid_opts_local; }); };
    auto push_legend   = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.legend_opts    = st.cosmetic.legend_local; }); };
    auto push_colorbar = [&]{ edit_box.update(idx, [&](AxesEdit& e){ e.colorbar_opts  = st.cosmetic.colorbar_local; }); };

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
            unsigned long long TitleStamps::*stamp;
            Color* color; float* size;
        };
        const TitleUi titles[3] = {
            { "Title",   "title",  st.cosmetic.title_buf,  sizeof(st.cosmetic.title_buf),  &AxesEdit::title, &TitleStamps::title,
              &sty.title_color,  &sty.title_fontsize },
            { "X title", "xtitle", st.cosmetic.xtitle_buf, sizeof(st.cosmetic.xtitle_buf), &AxesEdit::xtitle, &TitleStamps::xtitle,
              &sty.xtitle_color, &sty.xtitle_fontsize },
            { "Y title", "ytitle", st.cosmetic.ytitle_buf, sizeof(st.cosmetic.ytitle_buf), &AxesEdit::ytitle, &TitleStamps::ytitle,
              &sty.ytitle_color, &sty.ytitle_fontsize },
        };
        for (const TitleUi& t : titles) {
            ImGui::PushID(t.id);
            if (begin_field_table("text")) {
                field_row(t.label);
                if (ImGui::InputText("##text", t.buf, t.cap))
                    edit_box.update(idx, [&](AxesEdit& e){
                        e.*t.text = std::string(t.buf);
                        e.title_seen.*t.stamp = cur2d->title_stamps.*t.stamp;
                    });
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
                { "X axis", "at y", &sty.xaxis_y, &sty.origin_y, &st.cosmetic.origin_y_scratch,
                  &st.slot_view.ymin_local, &st.slot_view.ymax_local },
                { "Y axis", "at x", &sty.yaxis_x, &sty.origin_x, &st.cosmetic.origin_x_scratch,
                  &st.slot_view.xmin_local, &st.slot_view.xmax_local },
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
        if (ImGui::Checkbox("Grid##grid", &st.cosmetic.grid_local))
            edit_box.update(idx, [&](AxesEdit& e){ e.grid_enabled = st.cosmetic.grid_local; });
        ImGui::BeginDisabled(!st.cosmetic.grid_local);
        if (begin_field_table("grid", 3)) {
            field_row("Color");
            if (color_swatch("##gridcol", st.cosmetic.grid_opts_local.color)) push_grid();
            field_next("Style");
            if (linestyle_combo("##gridls", st.cosmetic.grid_opts_local.linestyle)) push_grid();
            field_next("Width");
            if (drag_float("##gridw", &st.cosmetic.grid_opts_local.linewidth, 0.1f, 6.0f, 0.02f, "%.2f px")) push_grid();
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
            { "X", &st.slot_view.xauto_local, &st.slot_view.xmin_local, &st.slot_view.xmax_local,
              &AxesEdit::xmin, &AxesEdit::xmax, &AxesEdit::xlim_auto },
            { "Y", &st.slot_view.yauto_local, &st.slot_view.ymin_local, &st.slot_view.ymax_local,
              &AxesEdit::ymin, &AxesEdit::ymax, &AxesEdit::ylim_auto },
        };
        if (begin_field_table("lim")) {
            for (const AxisLim& a : axes) {
                ImGui::PushID(a.label);
                // One row per axis: Auto, then low and high.
                field_row(a.label);
                if (ImGui::Checkbox("Auto", a.autoscale))
                    edit_box.update(idx, [&](AxesEdit& e){
                        e.*a.auto_field = *a.autoscale; e.lim_seen = cur2d->limit_stamps;
                    });
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::BeginDisabled(*a.autoscale);
                const float speed = limit_drag_speed(*a.lo, *a.hi);
                // The checkbox used the fill width; request it again.
                ImGui::SetNextItemWidth(-FLT_MIN);
                split_begin(2);
                if (drag_double("##lo", a.lo, speed))
                    edit_box.update(idx, [&](AxesEdit& e){
                        e.*a.lo_field = *a.lo; e.*a.auto_field = false;
                        e.lim_seen = cur2d->limit_stamps;
                    });
                split_next();
                if (drag_double("##hi", a.hi, speed))
                    edit_box.update(idx, [&](AxesEdit& e){
                        e.*a.hi_field = *a.hi; e.*a.auto_field = false;
                        e.lim_seen = cur2d->limit_stamps;
                    });
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
        if (draw_tick_table("xticks", st.cosmetic.xticks_scratch))
            edit_box.update(idx, [&](AxesEdit& e){ e.xticks_override = st.cosmetic.xticks_scratch; });
        ImGui::TextDisabled("Y ticks");
        if (draw_tick_table("yticks", st.cosmetic.yticks_scratch))
            edit_box.update(idx, [&](AxesEdit& e){ e.yticks_override = st.cosmetic.yticks_scratch; });
    }

    // ==== Legend & colorbar ===============================================
    // Shared with the 3D panel.
    draw_legend_colorbar_group<AxesEdit>(
        st, !find_colorbar_requests(*cur2d).empty(),
        [&](auto&& fn){ edit_box.update(idx, fn); });

}

namespace {

// The plot view's live size in logical pixels; 0 x 0 with none on screen.
void live_plot_size(const FigureContext& ctx, int& w, int& h) {
    w = ctx.view ? ctx.view->plot_w : 0;
    h = ctx.view ? ctx.view->plot_h : 0;
}

// The figure size for a frame of w x h on the selected slot, with the view's
// stored measure.
LayoutSize figure_size_for_selected_frame(const FigureContext& ctx, int w, int h) {
    return figure_size_for_frame(ctx.snap, *on_screen_measure(ctx.view, ctx.snap),
                                 ctx.selection.slot,
                                 static_cast<float>(w), static_cast<float>(h));
}

// The "Figure / Plot frame" selector and the size fields, shared by both
// dialogs; in PlotFrame mode the figure size is derived and shown.
void size_mode_fields(const FigureContext& ctx, SaveDialogState::SizeMode& mode,
                      int* w, int* h, const char* frame_hint) {
    int m = (mode == SaveDialogState::SizeMode::PlotFrame) ? 1 : 0;
    if (ImGui::RadioButton("Figure", &m, 0)) mode = SaveDialogState::SizeMode::Figure;
    ImGui::SameLine();
    if (ImGui::RadioButton("Plot frame", &m, 1)) mode = SaveDialogState::SizeMode::PlotFrame;

    ImGui::InputInt("Width",  w);
    ImGui::InputInt("Height", h);

    if (mode == SaveDialogState::SizeMode::PlotFrame) {
        if (*w > 0 && *h > 0) {
            const LayoutSize s = figure_size_for_selected_frame(ctx, *w, *h);
            ImGui::TextDisabled("Figure becomes %.0f x %.0f (axis %d)",
                                static_cast<double>(s.width), static_cast<double>(s.height),
                                ctx.selection.slot);
        }
        // A frame size only determines the axes it was asked about.
        if (ctx.snap.axes.size() > 1)
            ImGui::TextDisabled("Other subplots may differ (legend/colorbar).");
    } else {
        ImGui::TextDisabled("%s", frame_hint);
    }
}

// Whether the filename asks for SVG (the same test perform_save() uses).
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

} // namespace

void open_save_dialog(SaveDialogState& st, const FigureContext& ctx) {
    // Prefill with the live plot size.
    if (!st.open) live_plot_size(ctx, st.width, st.height);
    st.open = true;
}

void open_resize_dialog(ResizeDialogState& st, const FigureContext& ctx) {
    // Prefill with the selected axes' current frame.
    if (!st.open) {
        int lw = 0, lh = 0;
        live_plot_size(ctx, lw, lh);
        if (lw > 0 && lh > 0) {
            const FigureLayout fl =
                compute_figure_layout(ctx.snap, *on_screen_measure(ctx.view, ctx.snap), lw, lh);
            for (const auto& c : fl.cells) {
                if (c.slot.index != ctx.selection.slot) continue;
                st.frame_w = static_cast<int>(std::lround(c.frame.w));
                st.frame_h = static_cast<int>(std::lround(c.frame.h));
                break;
            }
        }
    }
    st.open = true;
}

std::optional<SaveRequest> resolve_save_request(const SaveDialogState& st,
                                                const FigureContext& ctx) {
    int lw = 0, lh = 0;
    live_plot_size(ctx, lw, lh);
    int sw = st.width  > 0 ? st.width  : lw;
    int sh = st.height > 0 ? st.height : lh;
    if (st.size_mode == SaveDialogState::SizeMode::PlotFrame && st.width > 0 && st.height > 0) {
        const LayoutSize s = figure_size_for_selected_frame(ctx, st.width, st.height);
        sw = static_cast<int>(std::lround(s.width));
        sh = static_cast<int>(std::lround(s.height));
    }
    if (sw <= 0 || sh <= 0) return std::nullopt;
    return SaveRequest{ .path = st.path_buf, .width = sw, .height = sh,
                        .max_splits = st.max_splits, .peel_layers = st.peel_layers };
}

std::optional<ResizeRequest> resolve_resize_request(const ResizeDialogState& st,
                                                    const FigureContext& ctx) {
    if (st.frame_w <= 0 || st.frame_h <= 0) return std::nullopt;
    const LayoutSize s = figure_size_for_selected_frame(ctx, st.frame_w, st.frame_h);
    const ResizeRequest r{ static_cast<int>(std::lround(s.width)),
                           static_cast<int>(std::lround(s.height)) };
    if (r.plot_w <= 0 || r.plot_h <= 0) return std::nullopt;
    return r;
}

// The warning raised by a knowingly misordered export (the file is already
// written) or a failed one (it is not). Shows the exporter's own sentence. The
// host's window is undocked rather than a popup, since it is raised outside
// any window scope.
void draw_save_warning(SaveDialogState& st) {
    if (st.warning.empty()) return;
    ImGui::TextWrapped(st.failed ? "The figure was not saved."
                                 : "The file was written, but part of it is not in the "
                                   "right order.");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(410.0f);
    ImGui::TextUnformatted(st.warning.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("OK")) st.warning.clear();
}

std::optional<SaveRequest> draw_save_dialog(FigureContext& ctx, SaveDialogState& st) {
    if (!st.open) return std::nullopt;

    ImGui::InputText("File", st.path_buf, sizeof(st.path_buf));
    size_mode_fields(ctx, st.size_mode, &st.width, &st.height,
                     "<=0 uses the Plot panel's current size.");

    // Export bounds, only for the chosen format and only when the figure has
    // 3D (0 = automatic).
    if (scene_has_3d(ctx.snap)) {
        ImGui::Separator();
        if (save_path_is_svg(st.path_buf)) {
            ImGui::InputInt("Max splits", &st.max_splits);
            if (st.max_splits < 0) st.max_splits = 0;
            ImGui::TextDisabled("0 = automatic (8 x polygons + 64).");
            ImGui::TextDisabled("Raise if a save reports it gave up.");
        } else {
            ImGui::InputInt("Peel layers", &st.peel_layers);
            st.peel_layers = std::clamp(st.peel_layers, 0, 64);
            ImGui::TextDisabled("0 = automatic (8). Translucent layers a ray");
            ImGui::TextDisabled("may cross before the rest is dropped.");
        }
    }

    std::optional<SaveRequest> req;
    if (ImGui::Button("Save")) {
        req = resolve_save_request(st, ctx);
        st.open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        st.open = false;
    return req;
}

// Resizes the window so the selected subplot's frame gets the requested size.
std::optional<ResizeRequest> draw_resize_dialog(FigureContext& ctx, ResizeDialogState& st) {
    if (!st.open) return std::nullopt;

    // Always frame-driven (dragging the window edge resizes the figure).
    SaveDialogState::SizeMode mode = SaveDialogState::SizeMode::PlotFrame;
    size_mode_fields(ctx, mode, &st.frame_w, &st.frame_h, "");

    std::optional<ResizeRequest> req;
    ImGui::BeginDisabled(st.frame_w <= 0 || st.frame_h <= 0);
    if (ImGui::Button("Apply")) {
        req = resolve_resize_request(st, ctx);
        st.open = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        st.open = false;
    return req;
}

} // namespace sextant
