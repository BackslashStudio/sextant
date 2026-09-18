#include "data_panel.h"
#include "panel.h"
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
#include <functional>
#include <optional>
#include <string>

namespace sextant {

namespace {

// Every table here carries the same two-column frozen gutter: a row index and
// the +/- row controls. A bar3d grid adds a third, its u coordinate, because
// unlike a heatmap's rows its lines sit at a value the reader can change.
constexpr int kGutterCols      = 2;
constexpr int kBar3DGutterCols = 3;

// Which of x/y/z a bar3d's u, v and standing directions are. The tooltip
// names them the same way (fmt_bar3d in hint.cpp) so the two agree about what
// a grid coordinate is called.
constexpr const char* kAxisName[3] = { "x", "y", "z" };

// Where a table's edits go. The two table functions below push through this
// rather than reaching for FigureEditBox themselves, because a table's ops go
// to one of two lanes -- AxesEdit::plot_ops for a 2D axes, AxesEdit3D's for a
// plane of a 3D one, with the plane index stamped on. Six push sites and one
// place that knows which lane it is, rather than six branches; forgetting to
// stamp the plane at one of them would silently edit the wrong object.
using OpSink = std::function<void(PlotDataOp)>;

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
                       const OpSink& push_op, PanelState& st) {
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
            push_op(BarWidthEdit{t.plot_index, w});
    }

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY
                                     | ImGuiTableFlags_Resizable;
    // Index + row-controls form a frozen left gutter, so the add/remove
    // buttons stay reachable when the data columns overflow horizontally —
    // which they routinely do, since this panel shares the narrow Cosmetic
    // dock column. (ImGui can only freeze from the left, so putting the
    // controls last would have parked them permanently off-screen.)
    // A fixed-row table keeps the index gutter and loses the controls: the
    // frozen column count follows, or the header row would name a column the
    // body never fills. See PlotDataTable::rows_fixed for why a mesh is one.
    const int gutter = t.rows_fixed ? 1 : kGutterCols;
    if (!ImGui::BeginTable("##vec", ncols + gutter, kFlags)) return;

    // Freezing columns at all requires ScrollX — TableSetupScrollFreeze
    // ignores its column count without it.
    ImGui::TableSetupScrollFreeze(gutter, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    if (!t.rows_fixed)
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

            if (!t.rows_fixed && ImGui::TableSetColumnIndex(1)) {
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
                if (!ImGui::TableSetColumnIndex(c + gutter)) continue;
                const DataColumn& col = t.columns[c];
                if (static_cast<std::size_t>(r) >= col.count) continue;
                // Each column against its own min/max. x and y are
                // unrelated quantities, so a range shared across them would
                // flatten whichever has the smaller span into one colour.
                // Cached, because the range is over the whole column while
                // the clipper only ever visits the visible rows.
                const float shade = st.shade_cells
                    ? st.cell_shading.column(data_generation, slot_idx, t.plane_index,
                                             t.kind, t.plot_index, c,
                                             col.values, col.count)
                                     .norm(col.values[r])
                    : -1.0f;
                // TableBeginCell pushes no ID of its own, so without this every
                // cell in a row would collide on "##c".
                ImGui::PushID(r);
                ImGui::PushID(c);
                double nv;
                if (edit_cell(col.values[r], fmt, "%.17g", &nv, shade))
                    push_op(PlotCellEdit{t.kind, t.plot_index, c,
                                         static_cast<std::size_t>(r), nv});
                ImGui::PopID();
                ImGui::PopID();
            }
        }
    }
    ImGui::EndTable();

    // Appending has to live outside the table: with zero rows there is no row
    // to hang a "+" off, and that is exactly the state you get after removing
    // the last point.
    if (!t.rows_fixed) {
        if (ImGui::SmallButton("+ point"))
            row_edit = PlotRowEdit{PlotRowEdit::Op::Insert, t.kind, t.plot_index, rows};
        ImGui::SameLine();
        ImGui::TextDisabled("new points copy the row above");
    }

    if (row_edit) push_op(*row_edit);
}

// Heatmap matrix as a rows x cols grid.
void draw_heatmap_grid(const PlotDataTable& t, const char* fmt, int slot_idx,
                       unsigned long long data_generation,
                       const OpSink& push_op, PanelState& st) {
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
                                                      t.plane_index, t.plot_index,
                                                      hp.data);
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
    // The table is indexed by row/column, but the plot is not: since the
    // extent became an argument (Axes::heatmap()), a cell's coordinates are
    // nothing the numbers in this grid reveal. Naming it here is what keeps
    // the panel honest about which cell of the picture a row is.
    ImGui::TextDisabled("Extent: x %g .. %g, y %g .. %g (cell %g x %g)",
                        hp.xrange.lo, hp.xrange.hi, hp.yrange.lo, hp.yrange.hi,
                        hp.cell_w(), hp.cell_h());

    int first = 0;
    if (hp.cols > kMaxGridCols) {
        st.grid_col_offset = std::clamp(st.grid_col_offset, 0, hp.cols - kMaxGridCols);
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputInt("First column", &st.grid_col_offset);
        st.grid_col_offset = std::clamp(st.grid_col_offset, 0, hp.cols - kMaxGridCols);
        first = st.grid_col_offset;
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
                                             t.plane_index, t.plot_index, hp.data)
                                     .norm(static_cast<double>(hp.data[idx]))
                    : -1.0f;
                ImGui::PushID(r);
                ImGui::PushID(first + c);
                double nv;
                // "%.9g" round-trips a float; "%.17g" would just expose the
                // binary noise of widening it to double.
                if (edit_cell(static_cast<double>(hp.data[idx]), fmt, "%.9g", &nv, shade))
                    push_op(PlotCellEdit{t.kind, t.plot_index, 0, idx, nv});
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

    if (line_edit) push_op(*line_edit);
}

// A grid with coordinates: a |u| x |v| matrix of heights (or of the per-bar
// bases, for a bar grid that has them), with the grid's own coordinates
// editable in the headers.
//
// The third table shape, and it exists because a gridded 3D plot is genuinely
// neither of the other two -- a heatmap's matrix has no coordinates of its
// own, and a vector table has no matrix. Splitting it into three tables (u, v,
// heights) was the alternative and is worse: none of the three means anything
// without the others, and the reader would have to hold two indices in their
// head to find one bar. Here the row and column headers *are* u and v, so the
// cell under "u = 400, v = 1.5" is the bar the tooltip calls x=400, y=1.5.
//
// Written for `bar3d` in step 6c and serving a `surface` unchanged since 7d,
// which is the return on having built it as a shape rather than as one plot's
// table: everything below reads `kind`, `u`, `v` and `cells`, and the two
// places the plots really differ -- a bar grid's footprints and its optional
// per-bar bases -- are the only branches.
void draw_grid_table(const PlotDataTable& t, const char* fmt, int slot_idx,
                     unsigned long long data_generation,
                     const OpSink& push_op, PanelState& st) {
    const Bar3DPlot*   b = t.bars3d;
    const SurfacePlot* s = t.surface;
    if (!b && !s) return;

    const PlotKind kind = b ? PlotKind::Bar3D : PlotKind::Surface;
    const CowVec<double>& gu = b ? b->u : s->u;
    const CowVec<double>& gv = b ? b->v : s->v;
    const CowVec<double>& primary = b ? b->heights : s->heights;
    const PlaneOrientation orient = b ? b->orient : s->orient;

    // How few grid lines the kind can live with. A bar grid can be one bar
    // wide; a surface is made of the *gaps* between samples, so one line means
    // no cells and nothing drawn -- the same condition Axes3D::surface()
    // refuses at ingest, and the panel must not be able to manufacture a shape
    // the public API would reject.
    const std::size_t min_lines = b ? 1u : 2u;

    const std::size_t nu = gu.size(), nv = gv.size();
    if (nu == 0 || nv == 0 || primary.size() != nu * nv) {
        ImGui::TextDisabled("Empty grid.");
        return;
    }
    const Axis3Map m = axis_map(orient);

    // Deferred exactly like the other two tables' structural edits, and for
    // the same reason: re-striding mid-table would move the cells the clipper
    // is still walking.
    std::optional<MatrixLineEdit> line_edit;

    ImGui::Text(b ? "%zu x %zu bars" : "%zu x %zu samples", nu, nv);
    ImGui::SameLine();
    ImGui::TextDisabled(b ? "| u = %s, v = %s, standing along %s"
                          : "| u = %s, v = %s, rising along %s",
                        kAxisName[m.u], kAxisName[m.v], kAxisName[m.h]);
    if (s)
        ImGui::TextDisabled("%zu x %zu cells are drawn between them -- a surface is made of "
                            "the gaps, so the grid cannot go below 2 x 2.",
                            s->cell_rows(), s->cell_cols());

    // Which matrix the cells edit. Offered only when there is a second one:
    // a plot standing on the single Bar3DOptions::bottom has no per-bar base
    // to show, a surface has no base at all, and a radio pair with one live
    // option is a control that lies.
    const bool has_bases = b && b->bottoms.size() == nu * nv;
    if (!has_bases) st.bar3d_show_bases = false;
    if (has_bases) {
        ImGui::TextDisabled("Cells:");
        ImGui::SameLine();
        if (ImGui::RadioButton("heights", !st.bar3d_show_bases)) st.bar3d_show_bases = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("bases", st.bar3d_show_bases))    st.bar3d_show_bases = true;
    } else if (b) {
        ImGui::TextDisabled("Every bar stands on %g (one base for the plot, "
                            "Bar3DOptions::bottom).", b->opts.bottom);
    }
    const bool bases = has_bases && st.bar3d_show_bases;
    const CowVec<double>& cells = bases ? b->bottoms : primary;
    // PlotCellEdit::column, which is also this matrix's key in the shading
    // cache -- so heights and bases are shaded against their own ranges.
    const int cell_col = bases ? 3 : 2;

    if (st.shade_cells) {
        const ValueRange& vr = st.cell_shading.column(data_generation, slot_idx,
                                                      t.plane_index, kind,
                                                      t.plot_index, cell_col,
                                                      cells.data(), cells.size());
        if (vr.valid) ImGui::TextDisabled("shaded over %g .. %g", vr.lo, vr.hi);
        else          ImGui::TextDisabled("nothing finite to shade");
    }

    // The footprint, which is data-space extent rather than the fraction the
    // caller passed -- resolved at ingest, exactly as BarPlot::bar_width is.
    // It belongs beside the grid for the same reason the 2D bar width does:
    // editing coordinates without it is how you get overlapping or gappy bars.
    // A surface has none: its cells reach from one sample to the next, so
    // there is no footprint to be out of step with the coordinates.
    if (b) {
        double uw = b->u_width, vw = b->v_width;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputDouble("Width along u", &uw, 0.0, 0.0, fmt)
            && ImGui::IsItemDeactivatedAfterEdit())
            push_op(BarWidthEdit{t.plot_index, uw, -1, kind, 0});
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputDouble("along v", &vw, 0.0, 0.0, fmt)
            && ImGui::IsItemDeactivatedAfterEdit())
            push_op(BarWidthEdit{t.plot_index, vw, -1, kind, 1});
    }

    int first = 0;
    const int ncols = static_cast<int>(nv);
    if (ncols > kMaxGridCols) {
        st.grid_col_offset = std::clamp(st.grid_col_offset, 0, ncols - kMaxGridCols);
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputInt("First v", &st.grid_col_offset);
        st.grid_col_offset = std::clamp(st.grid_col_offset, 0, ncols - kMaxGridCols);
        first = st.grid_col_offset;
        ImGui::SameLine();
        ImGui::TextDisabled("showing %d..%d", first, first + kMaxGridCols - 1);
    }
    const int shown = std::min(ncols - first, kMaxGridCols);

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##grid", shown + kBar3DGutterCols, kFlags)) return;

    // Three frozen header rows rather than the heatmap's two: the v indices,
    // then the per-column insert/remove buttons, then the v *values*. All
    // three address a column, and all three are useless once they scroll away.
    //
    // The values go *below* the buttons, not above, so that the two rows of
    // chrome stay together and the v row sits directly on top of the cells it
    // labels -- it is the column's coordinate, so it reads as the top of the
    // data, the way the u column in the gutter reads as the left of it.
    ImGui::TableSetupScrollFreeze(kBar3DGutterCols, 3);
    ImGui::TableSetupColumn("i\\j", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableSetupColumn("+/-", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 46.0f);
    ImGui::TableSetupColumn("u", ImGuiTableColumnFlags_WidthFixed, 88.0f);
    for (int c = 0; c < shown; ++c) {
        char head[16];
        std::snprintf(head, sizeof(head), "%d", first + c);
        ImGui::TableSetupColumn(head, ImGuiTableColumnFlags_WidthFixed, 88.0f);
    }
    ImGui::TableHeadersRow();

    // Column controls, on their own frozen row -- "colctl" keeps their ids
    // off TableHeadersRow()'s PushID(column_n) path and the row gutter's.
    ImGui::TableNextRow();
    ImGui::PushID("colctl");
    for (int c = 0; c < shown; ++c) {
        if (!ImGui::TableSetColumnIndex(c + kBar3DGutterCols)) continue;
        const int abs_c = first + c;
        ImGui::PushID(abs_c);
        if (ImGui::SmallButton("+"))
            line_edit = MatrixLineEdit{MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col,
                                       t.plot_index, static_cast<std::size_t>(abs_c) + 1,
                                       -1, kind};
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert a v line after %d", abs_c);
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginDisabled(nv <= min_lines);
        if (ImGui::SmallButton("x"))
            line_edit = MatrixLineEdit{MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Col,
                                       t.plot_index, static_cast<std::size_t>(abs_c),
                                       -1, kind};
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (nv <= min_lines) ImGui::SetTooltip(b ? "A grid cannot lose its last v line"
                                                    : "A surface needs at least two v lines to have a cell");
            else         ImGui::SetTooltip("Remove v line %d", abs_c);
        }
        ImGui::PopID();
    }
    ImGui::PopID();

    // The v coordinates, directly above the cells they label. Editable here
    // rather than in a table of their own, which is the whole point of this
    // shape: a column *is* a v.
    ImGui::TableNextRow();
    ImGui::PushID("vrow");
    if (ImGui::TableSetColumnIndex(2)) ImGui::TextDisabled("v");
    for (int c = 0; c < shown; ++c) {
        if (!ImGui::TableSetColumnIndex(c + kBar3DGutterCols)) continue;
        const int abs_c = first + c;
        const float shade = st.shade_cells
            ? st.cell_shading.column(data_generation, slot_idx, t.plane_index,
                                     kind, t.plot_index, 1,
                                     gv.data(), nv)
                             .norm(gv[static_cast<std::size_t>(abs_c)])
            : -1.0f;
        ImGui::PushID(abs_c);
        double val;
        if (edit_cell(gv[static_cast<std::size_t>(abs_c)], fmt, "%.17g", &val, shade))
            push_op(PlotCellEdit{kind, t.plot_index, 1,
                                 static_cast<std::size_t>(abs_c), val});
        ImGui::PopID();
    }
    ImGui::PopID();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(nu));
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
                                               t.plot_index, static_cast<std::size_t>(r) + 1,
                                               -1, kind};
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert a u line below %d", r);
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::BeginDisabled(nu <= min_lines);
                if (ImGui::SmallButton("x"))
                    line_edit = MatrixLineEdit{MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                                               t.plot_index, static_cast<std::size_t>(r),
                                               -1, kind};
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (nu <= min_lines) ImGui::SetTooltip(b ? "A grid cannot lose its last u line"
                                                            : "A surface needs at least two u lines to have a cell");
                    else         ImGui::SetTooltip("Remove u line %d", r);
                }
                ImGui::PopID();
                ImGui::PopID();
            }

            // The u coordinate, in the gutter so it stays beside its row when
            // the grid overflows horizontally.
            if (ImGui::TableSetColumnIndex(2)) {
                const float shade = st.shade_cells
                    ? st.cell_shading.column(data_generation, slot_idx, t.plane_index,
                                             kind, t.plot_index, 0,
                                             gu.data(), nu)
                                     .norm(gu[static_cast<std::size_t>(r)])
                    : -1.0f;
                ImGui::PushID("ucol");
                ImGui::PushID(r);
                double val;
                if (edit_cell(gu[static_cast<std::size_t>(r)], fmt, "%.17g", &val, shade))
                    push_op(PlotCellEdit{kind, t.plot_index, 0,
                                         static_cast<std::size_t>(r), val});
                ImGui::PopID();
                ImGui::PopID();
            }

            for (int c = 0; c < shown; ++c) {
                if (!ImGui::TableSetColumnIndex(c + kBar3DGutterCols)) continue;
                const std::size_t idx = static_cast<std::size_t>(r) * nv
                                      + static_cast<std::size_t>(first + c);
                if (idx >= cells.size()) continue;
                // One range for the whole matrix, as the heatmap grid has:
                // it is one quantity, and shading each column against its own
                // span would destroy the picture the grid is showing.
                const float shade = st.shade_cells
                    ? st.cell_shading.column(data_generation, slot_idx, t.plane_index,
                                             kind, t.plot_index, cell_col,
                                             cells.data(), cells.size())
                                     .norm(cells[idx])
                    : -1.0f;
                ImGui::PushID(r);
                ImGui::PushID(first + c);
                double val;
                if (edit_cell(cells[idx], fmt, "%.17g", &val, shade))
                    push_op(PlotCellEdit{kind, t.plot_index, cell_col, idx, val});
                ImGui::PopID();
                ImGui::PopID();
            }
        }
    }
    ImGui::EndTable();

    // Appending an edge line has no in-table anchor, the same reason the other
    // two tables' append buttons sit here.
    if (ImGui::SmallButton("+ u line"))
        line_edit = MatrixLineEdit{MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                                   t.plot_index, nu, -1, kind};
    ImGui::SameLine();
    if (ImGui::SmallButton("+ v line"))
        line_edit = MatrixLineEdit{MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col,
                                   t.plot_index, nv, -1, kind};
    ImGui::SameLine();
    ImGui::TextDisabled("new lines sit between their neighbours; their values copy the previous line");

    if (line_edit) push_op(*line_edit);
}

// ---- Per-object controls (v1.0 step 10.3) --------------------------------
// The Cosmetic panel's Planes, Bars and Surfaces sections, moved here under
// one rule: an object with a tab in this panel is edited in that tab and
// nowhere else. So each shows only the object its tab names, rather than a
// section listing every object in the axes.
//
// They edit PanelState's per-object scratch copies (planes_local and friends)
// and publish on the AxesEdit3D appearance lane, exactly as the sections did;
// sync_selected_slot() keeps those copies current whichever panel is shown.

// A plane's own tab: placement and visibility. What is *on* the plane is in
// the tabs that follow it, which carry the same plane_group_label() heading.
void draw_plane_tab(PanelState& st, const RenderSnapshot3D& sn, FigureEditBox& edit_box,
                    int idx, int pi, int objects) {
    if (pi < 0 || pi >= static_cast<int>(st.planes_local.size()) ||
        pi >= static_cast<int>(sn.planes.size()))
        return;
    PanelState::PlaneUi& p = st.planes_local[static_cast<std::size_t>(pi)];

    // Merged into the pending entry for this plane rather than appended, or a
    // drag would push one entry per frame and the vector would grow with the
    // duration of the gesture.
    auto push_plane = [&] {
        edit_box.update3d(idx, [&](AxesEdit3D& e) {
            for (auto& pe : e.planes)
                if (pe.plane_index == pi) {
                    pe.orient = p.orient; pe.offset = p.offset; pe.opts = p.opts;
                    return;
                }
            e.planes.push_back({ pi, p.orient, p.offset, p.opts });
        });
    };

    ImGui::TextDisabled("%s", plane_group_label(sn.planes[static_cast<std::size_t>(pi)], pi).c_str());
    ImGui::Separator();

    if (ImGui::Checkbox("Visible", &p.opts.visible)) push_plane();
    if (begin_field_table("plane")) {
        field_row("Facing");
        static const char* kOrients[] = { "XY", "YZ", "ZX" };
        int o = static_cast<int>(p.orient);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##orient", &o, kOrients, IM_ARRAYSIZE(kOrients))) {
            p.orient = static_cast<PlaneOrientation>(o);
            push_plane();
        }
        field_row("Offset");
        if (drag_double("##planeoff", &p.offset, 0.005f, "%.4g")) push_plane();
        field_row("Alpha");
        if (drag_float("##planealpha", &p.opts.alpha, 0.0f, 1.0f, 0.005f, "%.2f"))
            push_plane();
        end_field_table();
    }
    // Both are worth saying because both are the opposite of what the other
    // fields here do: changing an offset moves the plane through the data and
    // can pull the box's limits with it, while hiding one deliberately
    // changes nothing but the ink.
    ImGui::TextDisabled("Offset is in the units of the axis the plane faces.");
    ImGui::TextDisabled("Hiding a plane leaves the limits, colorbar and legend as they are.");
    ImGui::Spacing();
    if (objects == 0)
        ImGui::TextDisabled("Nothing is plotted on this plane yet.");
    else
        ImGui::TextDisabled("Its %d object%s: the P%d tabs that follow.",
                            objects, objects == 1 ? "" : "s", pi);

    // `show_axis` -- a plane's own in-plane frame, ticks and labels -- is not
    // here because it is not in Plane2DOptions: it was designed in spec_3d.md
    // §6 and deliberately not shipped, and a control for a field that does
    // not exist is worse than no control.
}


// One 2D plot object's appearance, above its table, as `draw_bar3d_appearance`
// is for a grid (v1.0 step 11.6). The two flags here are per-object and had
// nowhere to be set from: a legend key could only be switched off by clearing
// the name, and a colorbar not at all.
//
// Read straight from the published snapshot rather than through a scratch copy,
// which is what the 3D blocks above use. A drag needs a local to mutate between
// frames; a checkbox does not, and the scratch is the thing that went stale in
// the step 10.2 and 10.3 bugs. It costs one frame of latency, which a checkbox
// does not have.
template <class Opts>
void push_plot_style(FigureEditBox& box, int idx, bool is3d,
                     int plane_index, int plot_index, const Opts& o) {
    // Coalesced on the object's whole address, exactly as a bar grid's edit is:
    // a second tick before the drain replaces the first rather than queueing.
    auto merge = [&](std::vector<PlotStyleEdit>& v) {
        for (auto& s : v)
            if (s.plot_index == plot_index && s.plane_index == plane_index
                && std::holds_alternative<Opts>(s.opts)) { s.opts = o; return; }
        v.push_back({ plot_index, plane_index, o });
    };
    if (is3d) box.update3d(idx, [&](AxesEdit3D& e) { merge(e.plot_styles); });
    else      box.update  (idx, [&](AxesEdit&   e) { merge(e.plot_styles); });
}

void draw_plot_appearance(PanelState& st, const RenderSnapshot& sheet, const PlotDataTable& t,
                          FigureEditBox& edit_box, int idx, bool is3d) {
    const int pi = t.plot_index;
    if (pi < 0) return;
    const std::size_t i = static_cast<std::size_t>(pi);

    // `show_legend` for the kinds a legend can key, `colorbar` for the kinds
    // that carry a colour scale. scatter_z is the one with both -- its key
    // says which shape, its bar says what the colours mean.
    bool legend = false, has_legend = false;
    bool colorbar = false, has_colorbar = false;
    switch (t.kind) {
        case PlotKind::Line:
            if (i >= sheet.lines.size()) return;
            legend = sheet.lines[i].opts.show_legend; has_legend = true; break;
        case PlotKind::Scatter:
            if (i >= sheet.scatters.size()) return;
            legend = sheet.scatters[i].opts.show_legend; has_legend = true; break;
        case PlotKind::Bar:
            if (i >= sheet.bars.size()) return;
            legend = sheet.bars[i].opts.show_legend; has_legend = true; break;
        case PlotKind::ScatterZ:
            if (i >= sheet.scatter_z.size()) return;
            legend   = sheet.scatter_z[i].opts.show_legend; has_legend = true;
            colorbar = sheet.scatter_z[i].opts.colorbar;    has_colorbar = true; break;
        case PlotKind::Heatmap:
            if (i >= sheet.heatmaps.size()) return;
            colorbar = sheet.heatmaps[i].opts.colorbar; has_colorbar = true; break;
        default: return;   // Bar3D and Surface have blocks of their own
    }
    if (!has_legend && !has_colorbar) return;
    if (!section("Appearance", true)) return;

    // The object's name, which is what both flags below are about: it keys the
    // legend and it names the scale on the colorbar, and until now it could
    // only be set from code. One buffer in PanelState serves it, because the panel draws a
    // single tab per frame -- see text_field().
    {
        std::string name =
            t.kind == PlotKind::Line     ? sheet.lines[i].opts.name :
            t.kind == PlotKind::Scatter  ? sheet.scatters[i].opts.name :
            t.kind == PlotKind::Bar      ? sheet.bars[i].opts.name :
            t.kind == PlotKind::Heatmap  ? sheet.heatmaps[i].opts.name :
                                           sheet.scatter_z[i].opts.name;
        if (begin_field_table("plotname")) {
            field_row("Name");
            if (text_field("##plotname", name, st.name_buf, sizeof st.name_buf)) {
                switch (t.kind) {
                    case PlotKind::Line: {
                        auto o = sheet.lines[i].opts; o.name = name;
                        push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o); break; }
                    case PlotKind::Scatter: {
                        auto o = sheet.scatters[i].opts; o.name = name;
                        push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o); break; }
                    case PlotKind::Bar: {
                        auto o = sheet.bars[i].opts; o.name = name;
                        push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o); break; }
                    case PlotKind::Heatmap: {
                        auto o = sheet.heatmaps[i].opts; o.name = name;
                        push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o); break; }
                    default: {
                        auto o = sheet.scatter_z[i].opts; o.name = name;
                        push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o); break; }
                }
            }
            end_field_table();
        }
    }

    if (has_legend) {
        if (ImGui::Checkbox("Legend key##plotleg", &legend)) {
            switch (t.kind) {
                case PlotKind::Line: {
                    auto o = sheet.lines[i].opts; o.show_legend = legend;
                    push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o); break; }
                case PlotKind::Scatter: {
                    auto o = sheet.scatters[i].opts; o.show_legend = legend;
                    push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o); break; }
                case PlotKind::Bar: {
                    auto o = sheet.bars[i].opts; o.show_legend = legend;
                    push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o); break; }
                case PlotKind::ScatterZ: {
                    auto o = sheet.scatter_z[i].opts; o.show_legend = legend;
                    push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o); break; }
                default: break;
            }
        }
        // A key needs a name as well as the switch: the rule both renderers
        // apply is `show_legend && !name.empty()`, so say which half is
        // missing rather than leaving a ticked box that draws nothing.
        const bool named =
            t.kind == PlotKind::Line     ? !sheet.lines[i].opts.name.empty() :
            t.kind == PlotKind::Scatter  ? !sheet.scatters[i].opts.name.empty() :
            t.kind == PlotKind::Bar      ? !sheet.bars[i].opts.name.empty() :
                                           !sheet.scatter_z[i].opts.name.empty();
        if (legend && !named)
            ImGui::TextDisabled("No name, so no key is drawn.");
    }

    if (has_colorbar) {
        if (ImGui::Checkbox("Colorbar##plotcb", &colorbar)) {
            if (t.kind == PlotKind::Heatmap) {
                auto o = sheet.heatmaps[i].opts; o.colorbar = colorbar;
                push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
            } else {
                auto o = sheet.scatter_z[i].opts; o.colorbar = colorbar;
                push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
            }
        }
    }
    ImGui::Separator();
}

// A bar3d grid's appearance, above its table. Its coordinates, heights and
// footprints are data, and are the table and the fields beside it.
void draw_bar3d_appearance(PanelState& st, FigureEditBox& edit_box, int idx, int bi) {
    if (bi < 0 || bi >= static_cast<int>(st.bars3d_local.size())) return;
    if (!section("Appearance", true)) return;
    Bar3DOptions& o = st.bars3d_local[static_cast<std::size_t>(bi)];
    auto push_bar = [&] {
        edit_box.update3d(idx, [&](AxesEdit3D& e) {
            for (auto& be : e.bars3d)
                if (be.plot_index == bi) { be.opts = o; return; }
            e.bars3d.push_back({ bi, o });
        });
    };
    if (begin_field_table("bar3dname")) {
        field_row("Name");
        if (text_field("##b3dname", o.name, st.name_buf, sizeof st.name_buf)) push_bar();
        end_field_table();
    }
    if (begin_field_table("bar3d")) {
        field_row("Color");
        if (color_swatch("##bcol", o.color)) push_bar();
        field_row("Alpha");
        if (drag_float("##balpha", &o.alpha, 0.0f, 1.0f, 0.005f, "%.2f")) push_bar();
        field_row("Shading");
        if (drag_float("##bshade", &o.shading, 0.0f, 1.0f, 0.005f, "%.2f")) push_bar();
        end_field_table();
    }
    if (ImGui::Checkbox("Edges", &o.edges)) push_bar();
    ImGui::BeginDisabled(!o.edges);
    if (begin_field_table("bar3de")) {
        field_row("Color");
        if (color_swatch("##becol", o.edgecolor)) push_bar();
        field_row("Alpha");
        if (drag_float("##bealpha", &o.edge_alpha, 0.0f, 1.0f, 0.005f, "%.2f")) push_bar();
        field_row("Width");
        if (drag_float("##bew", &o.edge_linewidth, 0.1f, 10.0f, 0.05f, "%.2f px")) push_bar();
        end_field_table();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Edge width is pixels at the box centre; it thins with distance.");
    ImGui::Separator();
}

// A surface's appearance, above its table.
void draw_surface_appearance(PanelState& st, FigureEditBox& edit_box, int idx, int si) {
    if (si < 0 || si >= static_cast<int>(st.surfaces_local.size())) return;
    if (!section("Appearance", true)) return;
    SurfaceOptions& o = st.surfaces_local[static_cast<std::size_t>(si)];
    auto push_surf = [&] {
        edit_box.update3d(idx, [&](AxesEdit3D& e) {
            for (auto& se : e.surfaces)
                if (se.plot_index == si) { se.opts = o; return; }
            e.surfaces.push_back({ si, o });
        });
    };
    // One name, shown whichever way the sheet is coloured: it titles the
    // colorbar when `colormap` is on and keys the legend when it is off, and
    // the two are mutually exclusive by that flag.
    if (begin_field_table("surfname")) {
        field_row("Name");
        if (text_field("##surfname", o.name, st.name_buf, sizeof st.name_buf)) push_surf();
        end_field_table();
    }
    if (ImGui::Checkbox("Colour by height", &o.colormap)) push_surf();
    if (ImGui::Checkbox("Colorbar##surfcb", &o.colorbar)) push_surf();
    if (!o.colormap && o.colorbar)
        ImGui::TextDisabled("Flat colour, so no bar is drawn.");
    if (begin_field_table("surf")) {
        if (!o.colormap) {
            field_row("Color");
            if (color_swatch("##scol", o.color)) push_surf();
        } else {
            // Both, or neither: an empty interval is what means "the data's
            // own range", so leaving one editable would let the reader set a
            // scale that silently is not one.
            field_row("vmin");
            if (drag_float("##svmin", &o.vmin, -FLT_MAX, FLT_MAX, 0.01f, "%.4g")) push_surf();
            field_row("vmax");
            if (drag_float("##svmax", &o.vmax, -FLT_MAX, FLT_MAX, 0.01f, "%.4g")) push_surf();
        }
        field_row("Alpha");
        if (drag_float("##salpha", &o.alpha, 0.0f, 1.0f, 0.005f, "%.2f")) push_surf();
        field_row("Shading");
        if (drag_float("##sshade", &o.shading, 0.0f, 1.0f, 0.005f, "%.2f")) push_surf();
        end_field_table();
    }
    if (o.colormap && o.vmin == o.vmax)
        ImGui::TextDisabled("vmin == vmax: coloured over the surface's own range.");
    if (ImGui::Checkbox("Wireframe", &o.edges)) push_surf();
    ImGui::BeginDisabled(!o.edges);
    if (begin_field_table("surfe")) {
        field_row("Color");
        if (color_swatch("##secol", o.edgecolor)) push_surf();
        field_row("Alpha");
        if (drag_float("##sealpha", &o.edge_alpha, 0.0f, 1.0f, 0.005f, "%.2f")) push_surf();
        field_row("Width");
        if (drag_float("##sew", &o.edge_linewidth, 0.1f, 10.0f, 0.05f, "%.2f px")) push_surf();
        end_field_table();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("The wireframe is the sample grid, not a spacing of its own.");
    ImGui::Separator();
}


// A cloud's appearance, above its table. The marker combo is the control the
// other 3D kinds have no counterpart for: a bar and a sheet are shapes in the
// scene, and a cloud's shape is chosen.
void draw_scatter3d_appearance(PanelState& st, FigureEditBox& edit_box, int idx, int ci) {
    if (ci < 0 || ci >= static_cast<int>(st.scatter3d_local.size())) return;
    if (!section("Appearance", true)) return;
    Scatter3DOptions& o = st.scatter3d_local[static_cast<std::size_t>(ci)];
    auto push_cloud = [&] {
        edit_box.update3d(idx, [&](AxesEdit3D& e) {
            for (auto& ce : e.scatter3d)
                if (ce.plot_index == ci) { ce.opts = o; return; }
            e.scatter3d.push_back({ ci, o });
        });
    };
    // One name for both keys, as a surface has: it titles the colorbar and
    // keys the legend, and unlike a surface a cloud gets both at once.
    if (begin_field_table("cloudname")) {
        field_row("Name");
        if (text_field("##cloudname", o.name, st.name_buf, sizeof st.name_buf))
            push_cloud();
        end_field_table();
    }
    if (ImGui::Checkbox("Legend key##cloudlg", &o.show_legend)) push_cloud();
    if (ImGui::Checkbox("Colorbar##cloudcb", &o.colorbar)) push_cloud();
    if (begin_field_table("cloud")) {
        field_row("Marker");
        if (marker_combo("##cmark", o.marker)) push_cloud();
        field_row("Size");
        if (drag_float("##csize", &o.size, 1.0f, 100.0f, 0.2f, "%.1f px")) push_cloud();
        field_row("Color");
        if (color_swatch("##ccol", o.color)) push_cloud();
        field_row("Alpha");
        if (drag_float("##calpha", &o.alpha, 0.0f, 1.0f, 0.005f, "%.2f")) push_cloud();
        field_row("Depth shade");
        if (drag_float("##cshade", &o.depthshade, 0.0f, 1.0f, 0.005f, "%.2f")) push_cloud();
        end_field_table();
    }
    // vmin/vmax are only a scale when there is a fourth dimension to put on
    // it, so they are shown only then -- and both together, for the reason a
    // surface shows both: an empty interval is what means "the data's own
    // range", so one editable half could set a scale that silently is not one.
    if (begin_field_table("cloudv")) {
        field_row("vmin");
        if (drag_float("##cvmin", &o.vmin, -FLT_MAX, FLT_MAX, 0.01f, "%.4g")) push_cloud();
        field_row("vmax");
        if (drag_float("##cvmax", &o.vmax, -FLT_MAX, FLT_MAX, 0.01f, "%.4g")) push_cloud();
        end_field_table();
    }
    if (o.vmin == o.vmax)
        ImGui::TextDisabled("vmin == vmax: coloured over the series' own range.");
    ImGui::TextDisabled("Color applies when the series has no 'c' column; the colormap "
                        "overrides it when it has.");
    ImGui::Separator();
}

// A path's appearance. The cloud's block with the marker fields replaced by
// the two a stroke has, and with `loop` -- which is the one control here that
// changes what is *drawn* rather than how, since it adds a segment.
//
// No line-style combo, and that is not an omission: a world-space ribbon has
// no pixel arc length to dash along (spec_3d.md §4), so Line3DOptions has no
// `linestyle` for a control to bind to.
void draw_line3d_appearance(PanelState& st, FigureEditBox& edit_box, int idx, int li) {
    if (li < 0 || li >= static_cast<int>(st.line3d_local.size())) return;
    if (!section("Appearance", true)) return;
    Line3DOptions& o = st.line3d_local[static_cast<std::size_t>(li)];
    auto push_path = [&] {
        edit_box.update3d(idx, [&](AxesEdit3D& e) {
            for (auto& le : e.lines3d)
                if (le.plot_index == li) { le.opts = o; return; }
            e.lines3d.push_back({ li, o });
        });
    };
    if (begin_field_table("pathname")) {
        field_row("Name");
        if (text_field("##pathname", o.name, st.name_buf, sizeof st.name_buf))
            push_path();
        end_field_table();
    }
    if (ImGui::Checkbox("Legend key##pathlg", &o.show_legend)) push_path();
    if (ImGui::Checkbox("Colorbar##pathcb", &o.colorbar)) push_path();
    if (ImGui::Checkbox("Close the loop##pathloop", &o.loop)) push_path();
    if (begin_field_table("path")) {
        field_row("Color");
        if (color_swatch("##pcol", o.color)) push_path();
        field_row("Width");
        // Labelled px, as bar3d's edge width is, and true in the same
        // qualified way: it is a pixel width at the box centre, converted once
        // to a length in the scene, so a distant stretch draws thinner.
        if (drag_float("##pwidth", &o.linewidth, 0.1f, 20.0f, 0.05f, "%.2f px"))
            push_path();
        field_row("Alpha");
        if (drag_float("##palpha", &o.alpha, 0.0f, 1.0f, 0.005f, "%.2f")) push_path();
        field_row("Depth shade");
        if (drag_float("##pshade", &o.depthshade, 0.0f, 1.0f, 0.005f, "%.2f")) push_path();
        end_field_table();
    }
    if (begin_field_table("pathv")) {
        field_row("vmin");
        if (drag_float("##pvmin", &o.vmin, -FLT_MAX, FLT_MAX, 0.01f, "%.4g")) push_path();
        field_row("vmax");
        if (drag_float("##pvmax", &o.vmax, -FLT_MAX, FLT_MAX, 0.01f, "%.4g")) push_path();
        end_field_table();
    }
    if (o.vmin == o.vmax)
        ImGui::TextDisabled("vmin == vmax: coloured over the series' own range.");
    ImGui::TextDisabled("Color applies when the series has no 'c' column; the colormap "
                        "overrides it when it has.");
    ImGui::Separator();
}

// A mesh's appearance. A grid surface's block minus "Colour by height" --
// a mesh has no height axis to colour by, so the fourth dimension has to be
// given as a `colors` vector and *having* one is what makes the mesh
// colormapped -- and so the vmin/vmax pair is always shown, as a cloud's and
// a path's are.
void draw_surface_tri_appearance(PanelState& st, FigureEditBox& edit_box,
                                 int idx, int mi) {
    if (mi < 0 || mi >= static_cast<int>(st.surface_tri_local.size())) return;
    if (!section("Appearance", true)) return;
    SurfaceTriOptions& o = st.surface_tri_local[static_cast<std::size_t>(mi)];
    auto push_mesh = [&] {
        edit_box.update3d(idx, [&](AxesEdit3D& e) {
            for (auto& me : e.surface_tri)
                if (me.plot_index == mi) { me.opts = o; return; }
            e.surface_tri.push_back({ mi, o });
        });
    };
    // One name, shown whichever way the sheet is coloured -- a grid
    // surface's arrangement, and the two keys are mutually exclusive by the
    // same rule: a colormapped sheet titles its colorbar, a flat one keys the
    // legend.
    if (begin_field_table("meshname")) {
        field_row("Name");
        if (text_field("##meshname", o.name, st.name_buf, sizeof st.name_buf))
            push_mesh();
        end_field_table();
    }
    if (ImGui::Checkbox("Legend key##meshlg", &o.show_legend)) push_mesh();
    if (ImGui::Checkbox("Colorbar##meshcb", &o.colorbar)) push_mesh();
    if (begin_field_table("mesh")) {
        field_row("Color");
        if (color_swatch("##mcol", o.color)) push_mesh();
        field_row("Alpha");
        if (drag_float("##malpha", &o.alpha, 0.0f, 1.0f, 0.005f, "%.2f")) push_mesh();
        field_row("Shading");
        if (drag_float("##mshade", &o.shading, 0.0f, 1.0f, 0.005f, "%.2f")) push_mesh();
        end_field_table();
    }
    if (begin_field_table("meshv")) {
        field_row("vmin");
        if (drag_float("##mvmin", &o.vmin, -FLT_MAX, FLT_MAX, 0.01f, "%.4g")) push_mesh();
        field_row("vmax");
        if (drag_float("##mvmax", &o.vmax, -FLT_MAX, FLT_MAX, 0.01f, "%.4g")) push_mesh();
        end_field_table();
    }
    if (o.vmin == o.vmax)
        ImGui::TextDisabled("vmin == vmax: coloured over the mesh's own range.");
    if (ImGui::Checkbox("Wireframe##meshw", &o.edges)) push_mesh();
    ImGui::BeginDisabled(!o.edges);
    if (begin_field_table("meshe")) {
        field_row("Color");
        if (color_swatch("##mecol", o.edgecolor)) push_mesh();
        field_row("Alpha");
        if (drag_float("##mealpha", &o.edge_alpha, 0.0f, 1.0f, 0.005f, "%.2f")) push_mesh();
        field_row("Width");
        if (drag_float("##mew", &o.edge_linewidth, 0.1f, 10.0f, 0.05f, "%.2f px")) push_mesh();
        end_field_table();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Color applies when the mesh has no 'c' column; the colormap "
                        "overrides it when it has.");
    ImGui::Separator();
}

// A mesh's topology, above its vertex table and **read only**. It is not
// data: the indices say which vertices each face joins, so changing one is a
// re-mesh rather than a move, and a mesh built by the Delaunay overloads is
// deliberately not re-triangulated by an edit. Shown rather than hidden
// because a face count is how a caller checks their own mesh -- and because
// ingest keeps degenerate triangles precisely so that count agrees with
// theirs.
void draw_mesh_topology(const PlotDataTable& t) {
    const SurfaceTriPlot& m = *t.mesh;
    if (!section("Topology", false)) return;
    ImGui::Text("%zu faces over %zu vertices", m.face_count(), m.count());
    ImGui::TextDisabled("Read only: an index names a vertex, so editing one "
                        "re-meshes rather than moves anything.");
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_ScrollY;
    // A fixed height, unlike the tables below it: this one sits *above* the
    // vertex table and must not take the height that one needs.
    if (!ImGui::BeginTable("##topo", 4, kFlags, ImVec2(0.0f, 120.0f))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("face", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m.face_count()));
    while (clipper.Step()) {
        for (int f = clipper.DisplayStart; f < clipper.DisplayEnd; ++f) {
            std::size_t a = 0, b = 0, c = 0;
            m.face_verts(static_cast<std::size_t>(f), a, b, c);
            ImGui::TableNextRow();
            if (ImGui::TableSetColumnIndex(0)) ImGui::Text("%d", f);
            if (ImGui::TableSetColumnIndex(1)) ImGui::Text("%zu", a);
            if (ImGui::TableSetColumnIndex(2)) ImGui::Text("%zu", b);
            if (ImGui::TableSetColumnIndex(3)) ImGui::Text("%zu", c);
        }
    }
    ImGui::EndTable();
    ImGui::Separator();
}

} // namespace

std::vector<DataPanelTab> data_panel_tabs(const std::vector<PlotDataTable>& tables,
                                          std::size_t plane_count) {
    std::vector<DataPanelTab> out;
    out.reserve(tables.size() + plane_count);
    const int np = static_cast<int>(plane_count);
    int next_plane = 0;
    // Every plane up to and including p that has not had its tab yet. Called
    // before each plane's first object and once at the end, so a plane with
    // no objects -- which contributes no table -- still gets its tab, in order.
    auto planes_through = [&](int p) {
        for (; next_plane <= p && next_plane < np; ++next_plane)
            out.push_back({ -1, next_plane });
    };
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const int p = tables[i].plane_index;
        if (p >= 0) planes_through(p);
        out.push_back({ static_cast<int>(i), p });
    }
    planes_through(np - 1);
    return out;
}

void draw_data_panel(const FigureSnapshot& fsnap, FigureEditBox& edit_box, PanelState& st) {
    ImGui::Begin("Data", nullptr, ImGuiWindowFlags_NoCollapse);

    if (fsnap.axes.empty()) {
        ImGui::TextDisabled("No axes yet.");
        ImGui::End();
        return;
    }

    // The selected axes comes from the menu bar's combo or a click on the plot
    // (step 10.2); both outlive this panel and Cosmetic being hidden. Synced
    // here too, not only by the plot: the per-object tabs below edit the
    // scratch copies it re-seeds, and this panel is also driven on its own.
    const int idx = sync_selected_slot(st, fsnap);
    const FigureAxesSnapshot* cur = &fsnap.axes.front();
    for (const auto& fa : fsnap.axes)
        if (fa.slot.index == idx) { cur = &fa; break; }

    // A 3D slot contributes its *planes* -- each holds an Axes::Impl, so
    // collect_plot_data_tables() works on one unchanged, and step 6b's extra
    // address field (plane index, then the (kind, index) pair as before) is
    // what lets an edit find its way back -- and, since step 6c, its own
    // `bar3d` grids, which are on the axes and so carry plane index -1.
    const RenderSnapshot*   cur2d = cur->snap2d();
    const RenderSnapshot3D* cur3d = cur->snap3d();
    if (cur3d && cur3d->planes.empty() && cur3d->bars3d.empty() && cur3d->surfaces.empty()
        && cur3d->scatter3d.empty() && cur3d->lines3d.empty()
        && cur3d->surface_tri.empty()) {
        ImGui::TextDisabled("3D axes: no plot data.");
        ImGui::End();
        return;
    }

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

    const auto tables = cur2d ? collect_plot_data_tables(*cur2d)
                              : collect_plot_data_tables(*cur3d);
    // A 3D slot always has something to show once it has a plane, even an
    // empty one: the plane's own tab.
    const std::size_t nplanes = cur3d ? cur3d->planes.size() : 0;
    if (tables.empty() && nplanes == 0) {
        ImGui::TextDisabled("This axes has no plot objects.");
        ImGui::End();
        return;
    }
    const std::vector<DataPanelTab> tabs = data_panel_tabs(tables, nplanes);

    // One op sink per table, built once here so the table functions never have
    // to know which lane they are feeding. Which lane it is depends on the
    // *slot's kind*, not on the plane index: a 3D axes' own bar3d grids are
    // addressed at plane -1, exactly as a 2D axes' objects are, and routing on
    // the index would have sent them down the 2D lane to be dropped.
    const bool is3d = (cur3d != nullptr);
    auto sink_for = [&edit_box, idx, is3d](int plane_index) -> OpSink {
        if (!is3d)
            return [&edit_box, idx](PlotDataOp op) {
                edit_box.update(idx, [&](AxesEdit& e) { e.plot_ops.push_back(std::move(op)); });
            };
        return [&edit_box, idx, plane_index](PlotDataOp op) {
            // Stamped here, in the one place that knows the plane, rather
            // than at each of the sites that build an op.
            std::visit([plane_index](auto& o) { o.plane_index = plane_index; }, op);
            edit_box.update3d(idx, [&](AxesEdit3D& e) { e.plot_ops.push_back(std::move(op)); });
        };
    };

    // Tab ids derive from the label, so without the slot in the id stack
    // "line 0" in axes 1 and "line 0" in axes 2 would be the same tab and
    // selection would leak across a change of subplot.
    ImGui::PushID(idx);
    if (ImGui::BeginTabBar("##plots", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (const DataPanelTab& tb : tabs) {
            if (tb.table < 0) {
                // A plane's own tab. "##plane" keeps it apart from any object
                // a caller happened to label "P0".
                const std::string tab = "P" + std::to_string(tb.plane)
                                      + "##plane" + std::to_string(tb.plane);
                if (ImGui::BeginTabItem(tab.c_str())) {
                    const auto objects = std::count_if(tables.begin(), tables.end(),
                        [&](const PlotDataTable& t) { return t.plane_index == tb.plane; });
                    draw_plane_tab(st, *cur3d, edit_box, idx, tb.plane,
                                   static_cast<int>(objects));
                    ImGui::EndTabItem();
                }
                continue;
            }
            const std::size_t i = static_cast<std::size_t>(tb.table);
            const PlotDataTable& t = tables[i];
            // A plane's objects are prefixed rather than nested in a second
            // tab bar: every object in the cell then stays one click away,
            // and "P1 heatmap 0" reads as an address in a way a tab whose
            // meaning depends on another tab's state does not.
            const std::string name = t.plane_index >= 0
                ? "P" + std::to_string(t.plane_index) + " " + t.label
                : t.label;
            // "##i" keeps two plots that share a user label distinct.
            const std::string tab = name + "##" + std::to_string(i);
            if (ImGui::BeginTabItem(tab.c_str())) {
                if (!t.group.empty()) {
                    ImGui::TextDisabled("%s", t.group.c_str());
                    ImGui::Separator();
                }
                // The object's appearance, above its data: the tables below
                // take every pixel of height that is left, so nothing placed
                // after one would be reachable.
                if (t.kind == PlotKind::Bar3D)
                    draw_bar3d_appearance(st, edit_box, idx, t.plot_index);
                else if (t.kind == PlotKind::Surface)
                    draw_surface_appearance(st, edit_box, idx, t.plot_index);
                else if (t.kind == PlotKind::Scatter3D)
                    draw_scatter3d_appearance(st, edit_box, idx, t.plot_index);
                else if (t.kind == PlotKind::Line3D)
                    draw_line3d_appearance(st, edit_box, idx, t.plot_index);
                else if (t.kind == PlotKind::SurfaceTri)
                    draw_surface_tri_appearance(st, edit_box, idx, t.plot_index);
                else {
                    // A 2D kind, on a 2D axes or on a plane. The sheet it
                    // lives in is what carries its options, which is the one
                    // place the two cases differ.
                    const RenderSnapshot* sheet =
                        t.plane_index < 0
                            ? cur2d
                            : (cur3d && static_cast<std::size_t>(t.plane_index) < cur3d->planes.size()
                                   ? &cur3d->planes[static_cast<std::size_t>(t.plane_index)].sheet
                                   : nullptr);
                    if (sheet) draw_plot_appearance(st, *sheet, t, edit_box, idx, is3d);
                }
                const OpSink sink = sink_for(t.plane_index);
                if (t.mesh) draw_mesh_topology(t);
                if (t.is_grid())
                    draw_grid_table(t, fmt, idx, fsnap.data_generation, sink, st);
                else if (t.heatmap)
                    draw_heatmap_grid(t, fmt, idx, fsnap.data_generation, sink, st);
                else
                    draw_vector_table(t, fmt, idx, fsnap.data_generation, sink, st);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopID();

    ImGui::End();
}

} // namespace sextant
