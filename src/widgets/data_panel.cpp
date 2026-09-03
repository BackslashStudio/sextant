#include "data_panel.h"
#include "panel_state.h"
#include "panel_widgets.h"
#include "../edit_box.h"
#include "../figure_edits.h"
#include "../plot_data_view.h"
#include <imgui.h>
#include <imgui_internal.h>  // GetActiveID/GetInputTextState — not stable public API
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <optional>
#include <string>

namespace sextant {

namespace {

// Both tables here carry the same two-column frozen gutter: a row index and
// the +/- row controls.
constexpr int kGutterCols = 2;

// ImGui asserts `columns_count < IMGUI_TABLE_MAX_COLUMNS` (512) in
// BeginTable, and IM_ASSERT compiles out under NDEBUG — so the clamp has to
// live here, not rely on the assert. Strictly less-than, minus the gutter,
// leaves 509 matrix columns per page.
constexpr int kMaxGridCols = IMGUI_TABLE_MAX_COLUMNS - 1 - kGutterCols;

// One editable numeric cell.
//
// The snapshot is const, so rather than keeping a scratch copy of the whole
// dataset each cell re-seeds a plain local from the snapshot every frame.
// That is safe while the user is typing: InputTextEx reads the caller's
// buffer only on the activation frame, and its own edit buffer wins for as
// long as the item is active.
//
// Commits on Enter or focus loss, never per keystroke: every pending edit
// costs a full FigureSnapshot deep copy, so per-keystroke pushes would be
// pathological on large data.
//
// Returns true and writes *out only on the commit frame. `shade` is the
// cell's position in its column's range, or negative for no shading; it tints
// the *frame* background rather than the table cell background, because the
// input widget paints its own ImGuiCol_FrameBg over the full cell and would
// hide anything behind it.
bool edit_cell(double current, const char* display_fmt, const char* edit_fmt,
               double* out, float shade) {
    const ImGuiID cid = ImGui::GetID("##c");
    const bool active = ImGui::GetActiveID() == cid;

    const bool shaded = shade >= 0.0f;
    if (shaded) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        shade_color(shade));
        // Hover and active keep the value's own colour, nudged toward white.
        // The theme's flat highlight would replace it with a constant, losing
        // the reading exactly while the cell is being pointed at.
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, shade_highlight(shade, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  shade_highlight(shade, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_Text,           shade_text_color());
    }

    // InputScalar seeds its edit buffer by formatting the value with whatever
    // spec it is handed, so editing under a lossy display format ("%.4g")
    // would silently commit 1.23456789 back as 1.2346. Feed it a round-trip
    // format while the cell is being edited instead.
    double v = current;
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed = ImGui::InputDouble("##c", &v, 0.0, 0.0,
                                            active ? edit_fmt : display_fmt);

    // Activation happens *inside* the call above, so on that one frame
    // GetActiveID() was still stale and the buffer got seeded at display
    // precision. Ask ImGui to re-read it next frame, when the `active` branch
    // will format at full precision (the WIP #2890 path documented on
    // ImGuiInputTextState in imgui_internal.h).
    if (ImGui::IsItemActivated())
        if (ImGuiInputTextState* s = ImGui::GetInputTextState(cid))
            s->ReloadUserBufAndSelectAll();

    // Popped before the return, not after the caller is done: IsItem*() below
    // only inspects state ImGui already recorded, so nothing here is drawn
    // under these colours.
    if (shaded) ImGui::PopStyleColor(4);

    if (changed && ImGui::IsItemDeactivatedAfterEdit()) { *out = v; return true; }
    return false;
}

// A "low [====] high" strip of the shading ramp, drawn inline. Lives here
// rather than in cell_shading.h so that header stays colour/range arithmetic
// with nothing that draws.
void shade_legend() {
    ImGui::TextDisabled("low");
    ImGui::SameLine(0.0f, 4.0f);

    constexpr int   kSteps = 16;
    constexpr float kWidth = 64.0f;
    const float  h  = ImGui::GetTextLineHeight();
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < kSteps; ++i) {
        const float t0 = static_cast<float>(i) / kSteps;
        const float t1 = static_cast<float>(i + 1) / kSteps;
        dl->AddRectFilled(ImVec2(p.x + kWidth * t0, p.y),
                          ImVec2(p.x + kWidth * t1, p.y + h),
                          shade_color((t0 + t1) * 0.5f));
    }
    ImGui::Dummy(ImVec2(kWidth, h));

    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextDisabled("high");
}

// X / y / z (or center / height) as side-by-side columns, one row per point.
void draw_vector_table(const PlotDataTable& t, const char* fmt, int slot_idx,
                       unsigned long long data_generation,
                       FigureEditBox& edit_box, PanelState& st) {
    const int ncols = static_cast<int>(t.columns.size());
    std::size_t rows = 0;
    for (const auto& c : t.columns) rows = std::max(rows, c.count);

    ImGui::Text("%zu points", rows);

    // Structural edits are recorded here and pushed only after EndTable(), the
    // same deferred-mutation idiom draw_tick_table() uses — acting mid-table
    // would shift the row indices the clipper is still iterating over. At most
    // one can fire per frame (one mouse, one click).
    std::optional<PlotRowEdit> row_edit;

    // BarPlot::bar_width is one scalar for the whole plot, not a per-bar
    // column, so it can't ride in the table — but editing centers without it
    // is how you end up with overlapping or gappy bars.
    if (t.bar_width) {
        double w = *t.bar_width;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputDouble("Bar width (shared)", &w, 0.0, 0.0, fmt)
            && ImGui::IsItemDeactivatedAfterEdit())
            edit_box.update(slot_idx, [&](AxesEdit& e) {
                e.plot_ops.push_back(BarWidthEdit{t.plot_index, w});
            });
    }

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY
                                     | ImGuiTableFlags_Resizable;
    // Index + row-controls form a frozen left gutter, so the add/remove
    // buttons stay reachable when the data columns overflow horizontally —
    // which they routinely do, since this panel shares the narrow Controls
    // dock column. (ImGui can only freeze from the left, so putting the
    // controls last would have parked them permanently off-screen.)
    if (!ImGui::BeginTable("##vec", ncols + kGutterCols, kFlags)) return;

    // Freezing columns at all requires ScrollX — TableSetupScrollFreeze
    // ignores its column count without it.
    ImGui::TableSetupScrollFreeze(kGutterCols, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableSetupColumn("+/-", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 46.0f);
    for (const auto& c : t.columns)
        ImGui::TableSetupColumn(c.name, ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            ImGui::TableNextRow();
            if (ImGui::TableSetColumnIndex(0))
                ImGui::Text("%d", r);

            if (ImGui::TableSetColumnIndex(1)) {
                // Scoped under a string before the row index, because
                // TableHeadersRow() submits each header as PushID(column_n) +
                // TableHeader(name). A bare PushID(r) + SmallButton("x") would
                // hash the same string under the same integer as the "x" data
                // column's own header, which ImGui reports as conflicting IDs.
                ImGui::PushID("rowctl");
                ImGui::PushID(r);
                if (ImGui::SmallButton("+"))
                    row_edit = PlotRowEdit{PlotRowEdit::Op::Insert, t.kind, t.plot_index,
                                           static_cast<std::size_t>(r) + 1};
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Insert a point below row %d", r);
                ImGui::SameLine(0.0f, 2.0f);
                if (ImGui::SmallButton("x"))
                    row_edit = PlotRowEdit{PlotRowEdit::Op::Remove, t.kind, t.plot_index,
                                           static_cast<std::size_t>(r)};
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Remove row %d", r);
                ImGui::PopID();
                ImGui::PopID();
            }

            for (int c = 0; c < ncols; ++c) {
                if (!ImGui::TableSetColumnIndex(c + kGutterCols)) continue;
                const DataColumn& col = t.columns[c];
                if (static_cast<std::size_t>(r) >= col.count) continue;
                // Each column against its own min/max. x and y are
                // unrelated quantities, so a range shared across them would
                // flatten whichever has the smaller span into one colour.
                // Cached, because the range is over the whole column while
                // the clipper only ever visits the visible rows.
                const float shade = st.shade_cells
                    ? st.cell_shading.column(data_generation, slot_idx, t.kind,
                                             t.plot_index, c, col.values, col.count)
                                     .norm(col.values[r])
                    : -1.0f;
                // TableBeginCell pushes no ID of its own, so without this every
                // cell in a row would collide on "##c".
                ImGui::PushID(r);
                ImGui::PushID(c);
                double nv;
                if (edit_cell(col.values[r], fmt, "%.17g", &nv, shade))
                    edit_box.update(slot_idx, [&](AxesEdit& e) {
                        e.plot_ops.push_back(PlotCellEdit{t.kind, t.plot_index, c,
                                                          static_cast<std::size_t>(r), nv});
                    });
                ImGui::PopID();
                ImGui::PopID();
            }
        }
    }
    ImGui::EndTable();

    // Appending has to live outside the table: with zero rows there is no row
    // to hang a "+" off, and that is exactly the state you get after removing
    // the last point.
    if (ImGui::SmallButton("+ point"))
        row_edit = PlotRowEdit{PlotRowEdit::Op::Insert, t.kind, t.plot_index, rows};
    ImGui::SameLine();
    ImGui::TextDisabled("new points copy the row above");

    if (row_edit)
        edit_box.update(slot_idx, [&](AxesEdit& e) { e.plot_ops.push_back(*row_edit); });
}

// Heatmap matrix as a rows x cols grid.
void draw_heatmap_grid(const PlotDataTable& t, const char* fmt, int slot_idx,
                       unsigned long long data_generation,
                       FigureEditBox& edit_box, PanelState& st) {
    const HeatmapPlot& hp = *t.heatmap;
    if (hp.rows <= 0 || hp.cols <= 0 || hp.data.empty()) {
        ImGui::TextDisabled("Empty matrix.");
        return;
    }

    // Deferred exactly like draw_vector_table's row_edit, and for the same
    // reason: reshaping mid-table would move the cells the clipper is still
    // walking. Insert/remove of a whole row or column both ride this one slot,
    // since a frame holds at most one click.
    std::optional<MatrixLineEdit> line_edit;

    ImGui::Text("%d rows x %d cols", hp.rows, hp.cols);
    // A matrix has one range for the whole grid, so unlike the vector
    // table there is a single pair of numbers worth naming — and naming it is
    // what makes clear the shading spans the data, not opts.vmin/vmax.
    if (st.shade_cells) {
        const ValueRange& vr = st.cell_shading.matrix(data_generation, slot_idx,
                                                      t.plot_index, hp.data);
        ImGui::SameLine();
        if (vr.valid) ImGui::TextDisabled("| shaded over %g .. %g", vr.lo, vr.hi);
        else          ImGui::TextDisabled("| nothing finite to shade");
    }
    // Unconditional, because HeatmapOptions::origin defaults to "lower" — a
    // note that only fired for one setting would be either noise or absent
    // exactly when it matters. Storage is always row-major with row 0 first
    // regardless of origin; only the texture upload flips.
    ImGui::TextDisabled("%s", hp.opts.origin == "upper"
        ? "Row 0 is first in storage; origin=\"upper\" draws it at the top."
        : "Row 0 is first in storage; origin=\"lower\" draws it at the bottom.");

    int first = 0;
    if (hp.cols > kMaxGridCols) {
        st.heatmap_col_offset = std::clamp(st.heatmap_col_offset, 0, hp.cols - kMaxGridCols);
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputInt("First column", &st.heatmap_col_offset);
        st.heatmap_col_offset = std::clamp(st.heatmap_col_offset, 0, hp.cols - kMaxGridCols);
        first = st.heatmap_col_offset;
        ImGui::SameLine();
        ImGui::TextDisabled("showing %d..%d", first, first + kMaxGridCols - 1);
    }
    const int shown = std::min(hp.cols - first, kMaxGridCols);

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##grid", shown + kGutterCols, kFlags)) return;

    // Two frozen header rows, not one: the second holds the per-column
    // insert/remove buttons, which are useless if they scroll out of reach the
    // moment you look at row 40.
    ImGui::TableSetupScrollFreeze(kGutterCols, 2);
    ImGui::TableSetupColumn("r\\c", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableSetupColumn("+/-", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 46.0f);
    for (int c = 0; c < shown; ++c) {
        char head[16];
        std::snprintf(head, sizeof(head), "%d", first + c);
        ImGui::TableSetupColumn(head, ImGuiTableColumnFlags_WidthFixed, 84.0f);
    }
    ImGui::TableHeadersRow();

    // Column controls. Submitted before the clipper so it is one of the frozen
    // rows above; "colctl" keeps its ids off both TableHeadersRow()'s
    // PushID(column_n) path and the row gutter's (see draw_vector_table).
    ImGui::TableNextRow();
    ImGui::PushID("colctl");
    for (int c = 0; c < shown; ++c) {
        if (!ImGui::TableSetColumnIndex(c + kGutterCols)) continue;
        const int abs_c = first + c;
        ImGui::PushID(abs_c);
        if (ImGui::SmallButton("+"))
            line_edit = MatrixLineEdit{MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col,
                                       t.plot_index, static_cast<std::size_t>(abs_c) + 1};
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert a column after %d", abs_c);
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginDisabled(hp.cols <= 1);
        if (ImGui::SmallButton("x"))
            line_edit = MatrixLineEdit{MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Col,
                                       t.plot_index, static_cast<std::size_t>(abs_c)};
        ImGui::EndDisabled();
        // AllowWhenDisabled, or the one tooltip that explains *why* the button
        // is greyed out would be the one tooltip that never appears.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (hp.cols <= 1) ImGui::SetTooltip("A matrix cannot lose its last column");
            else              ImGui::SetTooltip("Remove column %d", abs_c);
        }
        ImGui::PopID();
    }
    ImGui::PopID();

    ImGuiListClipper clipper;
    clipper.Begin(hp.rows);
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            ImGui::TableNextRow();
            if (ImGui::TableSetColumnIndex(0))
                ImGui::Text("%d", r);

            if (ImGui::TableSetColumnIndex(1)) {
                ImGui::PushID("rowctl");
                ImGui::PushID(r);
                if (ImGui::SmallButton("+"))
                    line_edit = MatrixLineEdit{MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                                               t.plot_index, static_cast<std::size_t>(r) + 1};
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert a row below %d", r);
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::BeginDisabled(hp.rows <= 1);
                if (ImGui::SmallButton("x"))
                    line_edit = MatrixLineEdit{MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                                               t.plot_index, static_cast<std::size_t>(r)};
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (hp.rows <= 1) ImGui::SetTooltip("A matrix cannot lose its last row");
                    else              ImGui::SetTooltip("Remove row %d", r);
                }
                ImGui::PopID();
                ImGui::PopID();
            }

            for (int c = 0; c < shown; ++c) {
                // With ScrollX, off-screen columns report not-visible here, so
                // the widget below is skipped entirely — that is what keeps a
                // 500-column page affordable.
                if (!ImGui::TableSetColumnIndex(c + kGutterCols)) continue;
                const std::size_t idx = static_cast<std::size_t>(r) * static_cast<std::size_t>(hp.cols)
                                      + static_cast<std::size_t>(first + c);
                if (idx >= hp.data.size()) continue;
                // One range for the whole matrix, since it is one
                // quantity — per column here would shade each column against
                // its own span and destroy the picture the grid is showing.
                const float shade = st.shade_cells
                    ? st.cell_shading.matrix(data_generation, slot_idx,
                                             t.plot_index, hp.data)
                                     .norm(static_cast<double>(hp.data[idx]))
                    : -1.0f;
                ImGui::PushID(r);
                ImGui::PushID(first + c);
                double nv;
                // "%.9g" round-trips a float; "%.17g" would just expose the
                // binary noise of widening it to double.
                if (edit_cell(static_cast<double>(hp.data[idx]), fmt, "%.9g", &nv, shade))
                    edit_box.update(slot_idx, [&](AxesEdit& e) {
                        e.plot_ops.push_back(PlotCellEdit{t.kind, t.plot_index, 0, idx, nv});
                    });
                ImGui::PopID();
                ImGui::PopID();
            }
        }
    }
    ImGui::EndTable();

    // Appending an edge line has no in-table anchor (there is no row rows+1 to
    // hang a "+" off), the same reason draw_vector_table's "+ point" sits here.
    if (ImGui::SmallButton("+ row"))
        line_edit = MatrixLineEdit{MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                                   t.plot_index, static_cast<std::size_t>(hp.rows)};
    ImGui::SameLine();
    if (ImGui::SmallButton("+ column"))
        line_edit = MatrixLineEdit{MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col,
                                   t.plot_index, static_cast<std::size_t>(hp.cols)};
    ImGui::SameLine();
    ImGui::TextDisabled("new lines copy the previous one");

    if (line_edit)
        edit_box.update(slot_idx, [&](AxesEdit& e) { e.plot_ops.push_back(*line_edit); });
}

} // namespace

void draw_data_panel(const FigureSnapshot& fsnap, FigureEditBox& edit_box, PanelState& st) {
    ImGui::Begin("Data", nullptr, ImGuiWindowFlags_NoCollapse);

    if (fsnap.axes.empty()) {
        ImGui::TextDisabled("No axes yet.");
        ImGui::End();
        return;
    }

    // The axis combo is *drawn* here as well as in draw_controls_panel()
    // rather than only there: Controls can be hidden while this panel is up,
    // and selected_slot_index would otherwise be unreachable. Both call the
    // same axes_selector() so the two always agree on the entry labels.
    if (fsnap.axes.size() > 1)
        axes_selector("##axesseldata", fsnap, st.selected_slot_index);

    const FigureAxesSnapshot* cur = nullptr;
    for (const auto& fa : fsnap.axes)
        if (fa.slot.index == st.selected_slot_index) { cur = &fa; break; }
    if (!cur) cur = &fsnap.axes.front();
    const int idx = cur->slot.index;

    // --- Display format, shared by every cell below.
    static const char* kNotations[] = { "General", "Fixed", "Scientific" };
    int notation = static_cast<int>(st.value_format.notation);
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::Combo("Notation", &notation, kNotations, IM_ARRAYSIZE(kNotations)))
        st.value_format.notation = static_cast<ValueFormat::Notation>(notation);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    ImGui::SliderInt("Precision", &st.value_format.precision, 0, 17);

    char fmt_buf[16];
    const char* fmt = format_spec(st.value_format, fmt_buf, sizeof(fmt_buf));
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", fmt);

    // The toggle sits with the other display controls because that is
    // what it is — nothing here reaches the plot, and turning it off restores
    // the plain table exactly.
    ImGui::Checkbox("Shade cells", &st.shade_cells);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Tint each cell by where its value falls between the\n"
                          "low and high of its own column (light blue to light red).\n"
                          "A heatmap shades against the whole matrix instead,\n"
                          "over the data's own range rather than vmin/vmax.");
    if (st.shade_cells) {
        // A strip of the ramp itself rather than a sentence about it. It
        // carries no numbers because at this point there are none to carry:
        // the range is per column, and which column is a tab away.
        ImGui::SameLine(0.0f, 12.0f);
        shade_legend();
    }
    ImGui::Separator();

    const auto tables = collect_plot_data_tables(cur->snap);
    if (tables.empty()) {
        ImGui::TextDisabled("This axes has no plot objects.");
        ImGui::End();
        return;
    }

    // Tab ids derive from the label, so without the slot in the id stack
    // "line 0" in axes 1 and "line 0" in axes 2 would be the same tab and
    // selection would leak across the Axes combo above.
    ImGui::PushID(idx);
    if (ImGui::BeginTabBar("##plots", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (std::size_t i = 0; i < tables.size(); ++i) {
            const PlotDataTable& t = tables[i];
            // "##i" keeps two plots that share a user label distinct.
            const std::string tab = t.label + "##" + std::to_string(i);
            if (ImGui::BeginTabItem(tab.c_str())) {
                if (t.heatmap)
                    draw_heatmap_grid(t, fmt, idx, fsnap.data_generation, edit_box, st);
                else
                    draw_vector_table(t, fmt, idx, fsnap.data_generation, edit_box, st);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopID();

    ImGui::End();
}

} // namespace sextant
