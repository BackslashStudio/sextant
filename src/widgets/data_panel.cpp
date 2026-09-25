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
        // Every table has a frozen gutter: row index and +/- row controls (a bar3d grid
        // adds its u coordinate).
        constexpr int kGutterCols = 2;
        constexpr int kBar3DGutterCols = 3;

        // Which of x/y/z a bar3d's u, v and standing directions are (named as the
        // tooltip names them).
        constexpr const char* kAxisName[3] = {"x", "y", "z"};

        // Where a table's edits go: AxesEdit::plot_ops for a 2D axes, AxesEdit3D's for
        // a plane with the plane index stamped on. One sink so no push site forgets
        // the plane.
        using OpSink = std::function<void(PlotDataOp)>;

        // BeginTable asserts columns < 512 (compiled out under NDEBUG), so clamp here:
        // 509 matrix columns per page after the gutter.
        constexpr int kMaxGridCols = IMGUI_TABLE_MAX_COLUMNS - 1 - kGutterCols;

        // One editable numeric cell, re-seeded from the snapshot every frame (safe:
        // the active InputText keeps its own buffer). Commits on Enter or focus loss,
        // not per keystroke (each edit copies the snapshot). Returns true and writes
        // *out on the commit frame. `shade` (< 0 = none) tints the frame background,
        // since the input widget covers the cell background.
        bool edit_cell(double current, const char* display_fmt, const char* edit_fmt,
                       double* out, float shade) {
            const ImGuiID cid = ImGui::GetID("##c");
            const bool active = ImGui::GetActiveID() == cid;

            const bool shaded = shade >= 0.0f;
            if (shaded) {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, shade_color(shade));
                // Hover/active nudge the value's color toward white.
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, shade_highlight(shade, 0.22f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, shade_highlight(shade, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_Text, shade_text_color());
            }

            // Edit at round-trip precision, or the display format ("%.4g") would be
            // committed back.
            double v = current;
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool changed = ImGui::InputDouble("##c", &v, 0.0, 0.0,
                                                    active ? edit_fmt : display_fmt);

            // Activation happens inside the call above, so the buffer was seeded at
            // display precision; ask ImGui to re-read it next frame (WIP #2890 path).
            if (ImGui::IsItemActivated())
                if (ImGuiInputTextState* s = ImGui::GetInputTextState(cid))
                    s->ReloadUserBufAndSelectAll();

            // Pop before returning; IsItem*() below only reads recorded state.
            if (shaded) ImGui::PopStyleColor(4);

            if (changed && ImGui::IsItemDeactivatedAfterEdit()) {
                *out = v;
                return true;
            }
            return false;
        }

        // A "low [====] high" strip of the shading ramp, drawn inline.
        void shade_legend() {
            ImGui::TextDisabled("low");
            ImGui::SameLine(0.0f, 4.0f);

            constexpr int kSteps = 16;
            constexpr float kWidth = 64.0f;
            const float h = ImGui::GetTextLineHeight();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
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

        // x / y / z (or center / height) columns, one row per point.
        void draw_vector_table(const PlotDataTable& t, const char* fmt, int slot_idx,
                               unsigned long long data_generation,
                               const OpSink& push_op, PanelState& st) {
            const int ncols = static_cast<int>(t.columns.size());
            std::size_t rows = 0;
            for (const auto& c: t.columns) rows = std::max(rows, c.count);

            ImGui::Text("%zu points", rows);

            // Structural edits are applied after EndTable() (mid-table changes would
            // shift the clipper's rows). At most one per frame.
            std::optional<PlotRowEdit> row_edit;

            // Bar width is one scalar per plot, edited beside the table.
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
            // Index and row controls form a frozen left gutter so they stay reachable
            // when columns overflow. Fixed-row tables (meshes) keep only the index.
            const int gutter = t.rows_fixed ? 1 : kGutterCols;
            if (!ImGui::BeginTable("##vec", ncols + gutter, kFlags)) return;

            // Freezing columns requires ScrollX.
            ImGui::TableSetupScrollFreeze(gutter, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            if (!t.rows_fixed)
                ImGui::TableSetupColumn("+/-", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                                        46.0f);
            for (const auto& c: t.columns)
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
                        // Scoped under a string before the row index, so the "x" button
                        // doesn't collide with the "x" column header's ID.
                        ImGui::PushID("rowctl");
                        ImGui::PushID(r);
                        if (ImGui::SmallButton("+"))
                            row_edit = PlotRowEdit{
                                PlotRowEdit::Op::Insert, t.kind, t.plot_index,
                                static_cast<std::size_t>(r) + 1
                            };
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Insert a point below row %d", r);
                        ImGui::SameLine(0.0f, 2.0f);
                        if (ImGui::SmallButton("x"))
                            row_edit = PlotRowEdit{
                                PlotRowEdit::Op::Remove, t.kind, t.plot_index,
                                static_cast<std::size_t>(r)
                            };
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Remove row %d", r);
                        ImGui::PopID();
                        ImGui::PopID();
                    }

                    for (int c = 0; c < ncols; ++c) {
                        if (!ImGui::TableSetColumnIndex(c + gutter)) continue;
                        const DataColumn& col = t.columns[c];
                        if (static_cast<std::size_t>(r) >= col.count) continue;
                        // Each column against its own (cached) min/max.
                        const float shade = st.shade_cells
                                                ? st.cell_shading.column(data_generation, slot_idx, t.plane_index,
                                                                         t.kind, t.plot_index, c,
                                                                         col.values, col.count)
                                                .norm(col.values[r])
                                                : -1.0f;
                        // TableBeginCell pushes no ID; avoid "##c" collisions.
                        ImGui::PushID(r);
                        ImGui::PushID(c);
                        double nv;
                        if (edit_cell(col.values[r], fmt, "%.17g", &nv, shade))
                            push_op(PlotCellEdit{
                                t.kind, t.plot_index, c,
                                static_cast<std::size_t>(r), nv
                            });
                        ImGui::PopID();
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndTable();

            // Append outside the table (an empty table has no row for a "+").
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

            // Deferred like draw_vector_table's row_edit; one per frame.
            std::optional<MatrixLineEdit> line_edit;

            ImGui::Text("%d rows x %d cols", hp.rows, hp.cols);
            // One range for the whole matrix (the data's, not vmin/vmax); shown.
            if (st.shade_cells) {
                const ValueRange& vr = st.cell_shading.matrix(data_generation, slot_idx,
                                                              t.plane_index, t.plot_index,
                                                              hp.data);
                ImGui::SameLine();
                if (vr.valid) ImGui::TextDisabled("| shaded over %g .. %g", vr.lo, vr.hi);
                else ImGui::TextDisabled("| nothing finite to shade");
            }
            // Always shown: storage is row 0 first regardless of origin.
            ImGui::TextDisabled("%s", hp.opts.origin == "upper"
                                          ? "Row 0 is first in storage; origin=\"upper\" draws it at the top."
                                          : "Row 0 is first in storage; origin=\"lower\" draws it at the bottom.");
            // Name the extent, since cell coordinates aren't visible in the grid.
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

            // Two frozen header rows: indices and per-column insert/remove buttons.
            ImGui::TableSetupScrollFreeze(kGutterCols, 2);
            ImGui::TableSetupColumn("r\\c", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("+/-", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 46.0f);
            for (int c = 0; c < shown; ++c) {
                char head[16];
                std::snprintf(head, sizeof(head), "%d", first + c);
                ImGui::TableSetupColumn(head, ImGuiTableColumnFlags_WidthFixed, 84.0f);
            }
            ImGui::TableHeadersRow();

            // Column controls, before the clipper (a frozen row); "colctl" keeps ids
            // distinct.
            ImGui::TableNextRow();
            ImGui::PushID("colctl");
            for (int c = 0; c < shown; ++c) {
                if (!ImGui::TableSetColumnIndex(c + kGutterCols)) continue;
                const int abs_c = first + c;
                ImGui::PushID(abs_c);
                if (ImGui::SmallButton("+"))
                    line_edit = MatrixLineEdit{
                        MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col,
                        t.plot_index, static_cast<std::size_t>(abs_c) + 1
                    };
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert a column after %d", abs_c);
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::BeginDisabled(hp.cols <= 1);
                if (ImGui::SmallButton("x"))
                    line_edit = MatrixLineEdit{
                        MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Col,
                        t.plot_index, static_cast<std::size_t>(abs_c)
                    };
                ImGui::EndDisabled();
                // AllowWhenDisabled, so the disabled button's tooltip still shows.
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (hp.cols <= 1) ImGui::SetTooltip("A matrix cannot lose its last column");
                    else ImGui::SetTooltip("Remove column %d", abs_c);
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
                            line_edit = MatrixLineEdit{
                                MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                                t.plot_index, static_cast<std::size_t>(r) + 1
                            };
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert a row below %d", r);
                        ImGui::SameLine(0.0f, 2.0f);
                        ImGui::BeginDisabled(hp.rows <= 1);
                        if (ImGui::SmallButton("x"))
                            line_edit = MatrixLineEdit{
                                MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                                t.plot_index, static_cast<std::size_t>(r)
                            };
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            if (hp.rows <= 1) ImGui::SetTooltip("A matrix cannot lose its last row");
                            else ImGui::SetTooltip("Remove row %d", r);
                        }
                        ImGui::PopID();
                        ImGui::PopID();
                    }

                    for (int c = 0; c < shown; ++c) {
                        // Off-screen columns are skipped, keeping wide pages cheap.
                        if (!ImGui::TableSetColumnIndex(c + kGutterCols)) continue;
                        const std::size_t idx = static_cast<std::size_t>(r) * static_cast<std::size_t>(hp.cols)
                                                + static_cast<std::size_t>(first + c);
                        if (idx >= hp.data.size()) continue;
                        // One range for the whole matrix.
                        const float shade = st.shade_cells
                                                ? st.cell_shading.matrix(data_generation, slot_idx,
                                                                         t.plane_index, t.plot_index, hp.data)
                                                .norm(static_cast<double>(hp.data[idx]))
                                                : -1.0f;
                        ImGui::PushID(r);
                        ImGui::PushID(first + c);
                        double nv;
                        // "%.9g" round-trips a float.
                        if (edit_cell(static_cast<double>(hp.data[idx]), fmt, "%.9g", &nv, shade))
                            push_op(PlotCellEdit{t.kind, t.plot_index, 0, idx, nv});
                        ImGui::PopID();
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndTable();

            // Append an edge line below the table (no in-table anchor).
            if (ImGui::SmallButton("+ row"))
                line_edit = MatrixLineEdit{
                    MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                    t.plot_index, static_cast<std::size_t>(hp.rows)
                };
            ImGui::SameLine();
            if (ImGui::SmallButton("+ column"))
                line_edit = MatrixLineEdit{
                    MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col,
                    t.plot_index, static_cast<std::size_t>(hp.cols)
                };
            ImGui::SameLine();
            ImGui::TextDisabled("new lines copy the previous one");

            if (line_edit) push_op(*line_edit);
        }

        // A grid with coordinates: a |u| x |v| matrix of heights (or bar bases) with
        // u and v editable in the headers, so the cell under "u = 400, v = 1.5" is the
        // bar the tooltip calls x=400, y=1.5. Serves bar3d and surface; only
        // footprints and bases differ.
        void draw_grid_table(const PlotDataTable& t, const char* fmt, int slot_idx,
                             unsigned long long data_generation,
                             const OpSink& push_op, PanelState& st) {
            const Bar3DPlot* b = t.bars3d;
            const SurfacePlot* s = t.surface;
            if (!b && !s) return;

            const PlotKind kind = b ? PlotKind::Bar3D : PlotKind::Surface;
            const CowVec<double>& gu = b ? b->u : s->u;
            const CowVec<double>& gv = b ? b->v : s->v;
            const CowVec<double>& primary = b ? b->heights : s->heights;
            const PlaneOrientation orient = b ? b->orient : s->orient;

            // Minimum grid lines: a bar grid can be one wide; a surface needs two
            // (as Axes3D::surface() requires).
            const std::size_t min_lines = b ? 1u : 2u;

            const std::size_t nu = gu.size(), nv = gv.size();
            if (nu == 0 || nv == 0 || primary.size() != nu * nv) {
                ImGui::TextDisabled("Empty grid.");
                return;
            }
            const Axis3Map m = axis_map(orient);

            // Deferred like the other tables' structural edits.
            std::optional<MatrixLineEdit> line_edit;

            ImGui::Text(b ? "%zu x %zu bars" : "%zu x %zu samples", nu, nv);
            ImGui::SameLine();
            ImGui::TextDisabled(b
                                    ? "| u = %s, v = %s, standing along %s"
                                    : "| u = %s, v = %s, rising along %s",
                                kAxisName[m.u], kAxisName[m.v], kAxisName[m.h]);
            if (s)
                ImGui::TextDisabled("%zu x %zu cells are drawn between them -- a surface is made of "
                                    "the gaps, so the grid cannot go below 2 x 2.",
                                    s->cell_rows(), s->cell_cols());

            // Heights/bases selector, only when the plot has per-bar bases.
            const bool has_bases = b && b->bottoms.size() == nu * nv;
            if (!has_bases) st.bar3d_show_bases = false;
            if (has_bases) {
                ImGui::TextDisabled("Cells:");
                ImGui::SameLine();
                if (ImGui::RadioButton("heights", !st.bar3d_show_bases)) st.bar3d_show_bases = false;
                ImGui::SameLine();
                if (ImGui::RadioButton("bases", st.bar3d_show_bases)) st.bar3d_show_bases = true;
            } else if (b) {
                ImGui::TextDisabled("Every bar stands on %g (one base for the plot, "
                                    "Bar3DOptions::bottom).", b->opts.bottom);
            }
            const bool bases = has_bases && st.bar3d_show_bases;
            const CowVec<double>& cells = bases ? b->bottoms : primary;
            // PlotCellEdit::column, also this matrix's shading-cache key.
            const int cell_col = bases ? 3 : 2;

            if (st.shade_cells) {
                const ValueRange& vr = st.cell_shading.column(data_generation, slot_idx,
                                                              t.plane_index, kind,
                                                              t.plot_index, cell_col,
                                                              cells.data(), cells.size());
                if (vr.valid) ImGui::TextDisabled("shaded over %g .. %g", vr.lo, vr.hi);
                else ImGui::TextDisabled("nothing finite to shade");
            }

            // The data-space footprint, edited beside the grid (bars only; surfaces
            // have none).
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

            // Three frozen header rows: v indices, per-column buttons, then v values
            // (directly above the cells they label).
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

            // Column controls on their own frozen row ("colctl" ids).
            ImGui::TableNextRow();
            ImGui::PushID("colctl");
            for (int c = 0; c < shown; ++c) {
                if (!ImGui::TableSetColumnIndex(c + kBar3DGutterCols)) continue;
                const int abs_c = first + c;
                ImGui::PushID(abs_c);
                if (ImGui::SmallButton("+"))
                    line_edit = MatrixLineEdit{
                        MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col,
                        t.plot_index, static_cast<std::size_t>(abs_c) + 1,
                        -1, kind
                    };
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert a v line after %d", abs_c);
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::BeginDisabled(nv <= min_lines);
                if (ImGui::SmallButton("x"))
                    line_edit = MatrixLineEdit{
                        MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Col,
                        t.plot_index, static_cast<std::size_t>(abs_c),
                        -1, kind
                    };
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (nv <= min_lines)
                        ImGui::SetTooltip(b
                                              ? "A grid cannot lose its last v line"
                                              : "A surface needs at least two v lines to have a cell");
                    else ImGui::SetTooltip("Remove v line %d", abs_c);
                }
                ImGui::PopID();
            }
            ImGui::PopID();

            // The v coordinates, editable, above their columns.
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
                    push_op(PlotCellEdit{
                        kind, t.plot_index, 1,
                        static_cast<std::size_t>(abs_c), val
                    });
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
                            line_edit = MatrixLineEdit{
                                MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                                t.plot_index, static_cast<std::size_t>(r) + 1,
                                -1, kind
                            };
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert a u line below %d", r);
                        ImGui::SameLine(0.0f, 2.0f);
                        ImGui::BeginDisabled(nu <= min_lines);
                        if (ImGui::SmallButton("x"))
                            line_edit = MatrixLineEdit{
                                MatrixLineEdit::Op::Remove, MatrixLineEdit::Axis::Row,
                                t.plot_index, static_cast<std::size_t>(r),
                                -1, kind
                            };
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            if (nu <= min_lines)
                                ImGui::SetTooltip(b
                                                      ? "A grid cannot lose its last u line"
                                                      : "A surface needs at least two u lines to have a cell");
                            else ImGui::SetTooltip("Remove u line %d", r);
                        }
                        ImGui::PopID();
                        ImGui::PopID();
                    }

                    // The u coordinate, in the gutter.
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
                            push_op(PlotCellEdit{
                                kind, t.plot_index, 0,
                                static_cast<std::size_t>(r), val
                            });
                        ImGui::PopID();
                        ImGui::PopID();
                    }

                    for (int c = 0; c < shown; ++c) {
                        if (!ImGui::TableSetColumnIndex(c + kBar3DGutterCols)) continue;
                        const std::size_t idx = static_cast<std::size_t>(r) * nv
                                                + static_cast<std::size_t>(first + c);
                        if (idx >= cells.size()) continue;
                        // One range for the whole matrix.
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

            // Append an edge line below the table (no in-table anchor).
            if (ImGui::SmallButton("+ u line"))
                line_edit = MatrixLineEdit{
                    MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row,
                    t.plot_index, nu, -1, kind
                };
            ImGui::SameLine();
            if (ImGui::SmallButton("+ v line"))
                line_edit = MatrixLineEdit{
                    MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col,
                    t.plot_index, nv, -1, kind
                };
            ImGui::SameLine();
            ImGui::TextDisabled("new lines sit between their neighbours; their values copy the previous line");

            if (line_edit) push_op(*line_edit);
        }

        // ---- Per-object controls -------------------------------------------------
        // An object with a tab is edited in that tab only. These edit PanelState's
        // scratch copies and publish on the AxesEdit3D appearance lane;
        // sync_selected_slot() keeps the copies current.

        // A plane's tab: placement and visibility (its objects have their own tabs).
        void draw_plane_tab(PanelState& st, const RenderSnapshot3D& sn, FigureEditBox& edit_box,
                            int idx, int pi, int objects) {
            if (pi < 0 || pi >= static_cast<int>(st.planes_local.size()) ||
                pi >= static_cast<int>(sn.planes.size()))
                return;
            PanelState::PlaneUi& p = st.planes_local[static_cast<std::size_t>(pi)];

            // Merged into the pending entry for this plane, so a drag doesn't append
            // one entry per frame.
            auto push_plane = [&] {
                edit_box.update3d(idx, [&](AxesEdit3D& e) {
                    for (auto& pe: e.planes)
                        if (pe.plane_index == pi) {
                            pe.orient = p.orient;
                            pe.offset = p.offset;
                            pe.opts = p.opts;
                            return;
                        }
                    e.planes.push_back({pi, p.orient, p.offset, p.opts});
                });
            };

            ImGui::TextDisabled("%s", plane_group_label(sn.planes[static_cast<std::size_t>(pi)], pi).c_str());
            ImGui::Separator();

            if (ImGui::Checkbox("Visible", &p.opts.visible)) push_plane();
            if (begin_field_table("plane")) {
                field_row("Facing");
                static const char* kOrients[] = {"XY", "YZ", "ZX"};
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
            // Offset can change the box limits; hiding changes only the drawing.
            ImGui::TextDisabled("Offset is in the units of the axis the plane faces.");
            ImGui::TextDisabled("Hiding a plane leaves the limits, colorbar and legend as they are.");
            ImGui::Spacing();
            if (objects == 0)
                ImGui::TextDisabled("Nothing is plotted on this plane yet.");
            else
                ImGui::TextDisabled("Its %d object%s: the P%d tabs that follow.",
                                    objects, objects == 1 ? "" : "s", pi);

            // No `show_axis` control: Plane2DOptions has no such field.
        }

        // A 2D plot object's appearance, above its table. Read directly from the
        // snapshot (checkboxes need no scratch copy; one frame of latency).
        template<class Opts>
        void push_plot_style(FigureEditBox& box, int idx, bool is3d,
                             int plane_index, int plot_index, const Opts& o) {
            // Coalesced on the object's address: a second change replaces the first.
            auto merge = [&](std::vector<PlotStyleEdit>& v) {
                for (auto& s: v)
                    if (s.plot_index == plot_index && s.plane_index == plane_index
                        && std::holds_alternative<Opts>(s.opts)) {
                        s.opts = o;
                        return;
                    }
                v.push_back({plot_index, plane_index, o});
            };
            if (is3d) box.update3d(idx, [&](AxesEdit3D& e) { merge(e.plot_styles); });
            else box.update(idx, [&](AxesEdit& e) { merge(e.plot_styles); });
        }

        void draw_plot_appearance(PanelState& st, const RenderSnapshot& sheet, const PlotDataTable& t,
                                  FigureEditBox& edit_box, int idx, bool is3d) {
            const int pi = t.plot_index;
            if (pi < 0) return;
            const std::size_t i = static_cast<std::size_t>(pi);

            // `show_legend` for keyable kinds, `colorbar` for color-scale kinds
            // (scatter_z has both).
            bool legend = false, has_legend = false;
            bool colorbar = false, has_colorbar = false;
            switch (t.kind) {
                case PlotKind::Line:
                    if (i >= sheet.lines.size()) return;
                    legend = sheet.lines[i].opts.show_legend;
                    has_legend = true;
                    break;
                case PlotKind::Scatter:
                    if (i >= sheet.scatters.size()) return;
                    legend = sheet.scatters[i].opts.show_legend;
                    has_legend = true;
                    break;
                case PlotKind::Bar:
                    if (i >= sheet.bars.size()) return;
                    legend = sheet.bars[i].opts.show_legend;
                    has_legend = true;
                    break;
                case PlotKind::ScatterZ:
                    if (i >= sheet.scatter_z.size()) return;
                    legend = sheet.scatter_z[i].opts.show_legend;
                    has_legend = true;
                    colorbar = sheet.scatter_z[i].opts.colorbar;
                    has_colorbar = true;
                    break;
                case PlotKind::Heatmap:
                    if (i >= sheet.heatmaps.size()) return;
                    colorbar = sheet.heatmaps[i].opts.colorbar;
                    has_colorbar = true;
                    break;
                default: return; // Bar3D and Surface have blocks of their own
            }
            if (!has_legend && !has_colorbar) return;
            if (!section("Appearance", true)) return;

            // The name (legend key and colorbar title); one shared buffer, see
            // text_field().
            {
                std::string name =
                        t.kind == PlotKind::Line
                            ? sheet.lines[i].opts.name
                            : t.kind == PlotKind::Scatter
                                  ? sheet.scatters[i].opts.name
                                  : t.kind == PlotKind::Bar
                                        ? sheet.bars[i].opts.name
                                        : t.kind == PlotKind::Heatmap
                                              ? sheet.heatmaps[i].opts.name
                                              : sheet.scatter_z[i].opts.name;
                if (begin_field_table("plotname")) {
                    field_row("Name");
                    if (text_field("##plotname", name, st.name_buf, sizeof st.name_buf)) {
                        switch (t.kind) {
                            case PlotKind::Line: {
                                auto o = sheet.lines[i].opts;
                                o.name = name;
                                push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                                break;
                            }
                            case PlotKind::Scatter: {
                                auto o = sheet.scatters[i].opts;
                                o.name = name;
                                push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                                break;
                            }
                            case PlotKind::Bar: {
                                auto o = sheet.bars[i].opts;
                                o.name = name;
                                push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                                break;
                            }
                            case PlotKind::Heatmap: {
                                auto o = sheet.heatmaps[i].opts;
                                o.name = name;
                                push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                                break;
                            }
                            default: {
                                auto o = sheet.scatter_z[i].opts;
                                o.name = name;
                                push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                                break;
                            }
                        }
                    }
                    end_field_table();
                }
            }

            if (has_legend) {
                if (ImGui::Checkbox("Legend key##plotleg", &legend)) {
                    switch (t.kind) {
                        case PlotKind::Line: {
                            auto o = sheet.lines[i].opts;
                            o.show_legend = legend;
                            push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                            break;
                        }
                        case PlotKind::Scatter: {
                            auto o = sheet.scatters[i].opts;
                            o.show_legend = legend;
                            push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                            break;
                        }
                        case PlotKind::Bar: {
                            auto o = sheet.bars[i].opts;
                            o.show_legend = legend;
                            push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                            break;
                        }
                        case PlotKind::ScatterZ: {
                            auto o = sheet.scatter_z[i].opts;
                            o.show_legend = legend;
                            push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                            break;
                        }
                        default: break;
                    }
                }
                // A key needs both the switch and a name; say which is missing.
                const bool named =
                        t.kind == PlotKind::Line
                            ? !sheet.lines[i].opts.name.empty()
                            : t.kind == PlotKind::Scatter
                                  ? !sheet.scatters[i].opts.name.empty()
                                  : t.kind == PlotKind::Bar
                                        ? !sheet.bars[i].opts.name.empty()
                                        : !sheet.scatter_z[i].opts.name.empty();
                if (legend && !named)
                    ImGui::TextDisabled("No name, so no key is drawn.");
            }

            if (has_colorbar) {
                if (ImGui::Checkbox("Colorbar##plotcb", &colorbar)) {
                    if (t.kind == PlotKind::Heatmap) {
                        auto o = sheet.heatmaps[i].opts;
                        o.colorbar = colorbar;
                        push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                    } else {
                        auto o = sheet.scatter_z[i].opts;
                        o.colorbar = colorbar;
                        push_plot_style(edit_box, idx, is3d, t.plane_index, pi, o);
                    }
                }
            }
            ImGui::Separator();
        }

        // A bar3d grid's appearance, above its table.
        void draw_bar3d_appearance(PanelState& st, FigureEditBox& edit_box, int idx, int bi) {
            if (bi < 0 || bi >= static_cast<int>(st.bars3d_local.size())) return;
            if (!section("Appearance", true)) return;
            Bar3DOptions& o = st.bars3d_local[static_cast<std::size_t>(bi)];
            auto push_bar = [&] {
                edit_box.update3d(idx, [&](AxesEdit3D& e) {
                    for (auto& be: e.bars3d)
                        if (be.plot_index == bi) {
                            be.opts = o;
                            return;
                        }
                    e.bars3d.push_back({bi, o});
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
                    for (auto& se: e.surfaces)
                        if (se.plot_index == si) {
                            se.opts = o;
                            return;
                        }
                    e.surfaces.push_back({si, o});
                });
            };
            // One name: the colorbar title with `colormap`, else the legend key.
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
                    // Both or neither: an empty interval means "the data's range".
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

        // A cloud's appearance, above its table (including the marker shape).
        void draw_scatter3d_appearance(PanelState& st, FigureEditBox& edit_box, int idx, int ci) {
            if (ci < 0 || ci >= static_cast<int>(st.scatter3d_local.size())) return;
            if (!section("Appearance", true)) return;
            Scatter3DOptions& o = st.scatter3d_local[static_cast<std::size_t>(ci)];
            auto push_cloud = [&] {
                edit_box.update3d(idx, [&](AxesEdit3D& e) {
                    for (auto& ce: e.scatter3d)
                        if (ce.plot_index == ci) {
                            ce.opts = o;
                            return;
                        }
                    e.scatter3d.push_back({ci, o});
                });
            };
            // One name for the colorbar and the legend key.
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
            // vmin/vmax only with `colors`, both or neither (empty = data range).
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

        // A path's appearance: stroke width and `loop` instead of marker fields. No
        // line style (Line3DOptions has none).
        void draw_line3d_appearance(PanelState& st, FigureEditBox& edit_box, int idx, int li) {
            if (li < 0 || li >= static_cast<int>(st.line3d_local.size())) return;
            if (!section("Appearance", true)) return;
            Line3DOptions& o = st.line3d_local[static_cast<std::size_t>(li)];
            auto push_path = [&] {
                edit_box.update3d(idx, [&](AxesEdit3D& e) {
                    for (auto& le: e.lines3d)
                        if (le.plot_index == li) {
                            le.opts = o;
                            return;
                        }
                    e.lines3d.push_back({li, o});
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
                // Pixels at the box centre; thinner with distance.
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

        // A mesh's appearance: as a surface without "Colour by height" (a mesh is
        // colormapped by having `colors`); vmin/vmax always shown.
        void draw_surface_tri_appearance(PanelState& st, FigureEditBox& edit_box,
                                         int idx, int mi) {
            if (mi < 0 || mi >= static_cast<int>(st.surface_tri_local.size())) return;
            if (!section("Appearance", true)) return;
            SurfaceTriOptions& o = st.surface_tri_local[static_cast<std::size_t>(mi)];
            auto push_mesh = [&] {
                edit_box.update3d(idx, [&](AxesEdit3D& e) {
                    for (auto& me: e.surface_tri)
                        if (me.plot_index == mi) {
                            me.opts = o;
                            return;
                        }
                    e.surface_tri.push_back({mi, o});
                });
            };
            // One name: the colorbar title when colormapped, else the legend key.
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

        // A mesh's topology, read only (editing it would re-mesh). Shown so the face
        // count can be checked.
        void draw_mesh_topology(const PlotDataTable& t) {
            const SurfaceTriPlot& m = *t.mesh;
            if (!section("Topology", false)) return;
            ImGui::Text("%zu faces over %zu vertices", m.face_count(), m.count());
            ImGui::TextDisabled("Read only: an index names a vertex, so editing one "
                "re-meshes rather than moves anything.");
            constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                               | ImGuiTableFlags_ScrollY;
            // Fixed height, leaving the rest for the vertex table.
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
        // Emit tabs for planes up to p that haven't had one (so empty planes get a
        // tab too, in order).
        auto planes_through = [&](int p) {
            for (; next_plane <= p && next_plane < np; ++next_plane)
                out.push_back({-1, next_plane});
        };
        for (std::size_t i = 0; i < tables.size(); ++i) {
            const int p = tables[i].plane_index;
            if (p >= 0) planes_through(p);
            out.push_back({static_cast<int>(i), p});
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

        // Sync the selected slot here too: this panel's tabs edit the scratch
        // copies, and it can run on its own.
        const int idx = sync_selected_slot(st, fsnap);
        const FigureAxesSnapshot* cur = &fsnap.axes.front();
        for (const auto& fa: fsnap.axes)
            if (fa.slot.index == idx) {
                cur = &fa;
                break;
            }

        // A 3D slot contributes its planes' objects and its own 3D objects
        // (plane index -1).
        const RenderSnapshot* cur2d = cur->snap2d();
        const RenderSnapshot3D* cur3d = cur->snap3d();
        if (cur3d && cur3d->planes.empty() && cur3d->bars3d.empty() && cur3d->surfaces.empty()
            && cur3d->scatter3d.empty() && cur3d->lines3d.empty()
            && cur3d->surface_tri.empty()) {
            ImGui::TextDisabled("3D axes: no plot data.");
            ImGui::End();
            return;
        }

        // --- Display format, shared by every cell below.
        static const char* kNotations[] = {"General", "Fixed", "Scientific"};
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

        // Display-only; nothing reaches the plot.
        ImGui::Checkbox("Shade cells", &st.shade_cells);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Tint each cell by where its value falls between the\n"
                "low and high of its own column (light blue to light red).\n"
                "A heatmap shades against the whole matrix instead,\n"
                "over the data's own range rather than vmin/vmax.");
        if (st.shade_cells) {
            // A strip of the ramp (ranges are per column, so no numbers).
            ImGui::SameLine(0.0f, 12.0f);
            shade_legend();
        }
        ImGui::Separator();

        const auto tables = cur2d
                                ? collect_plot_data_tables(*cur2d)
                                : collect_plot_data_tables(*cur3d);
        // A 3D slot with a plane always has something to show (the plane's tab).
        const std::size_t nplanes = cur3d ? cur3d->planes.size() : 0;
        if (tables.empty() && nplanes == 0) {
            ImGui::TextDisabled("This axes has no plot objects.");
            ImGui::End();
            return;
        }
        const std::vector<DataPanelTab> tabs = data_panel_tabs(tables, nplanes);

        // One op sink per table. The lane depends on the slot's kind, not the
        // plane index (a 3D axes' own objects use plane -1 too).
        const bool is3d = (cur3d != nullptr);
        auto sink_for = [&edit_box, idx, is3d](const PlotDataTable& t) -> OpSink {
            const unsigned long long seen = t.data_stamp;
            if (!is3d)
                return [&edit_box, idx, seen](PlotDataOp op) {
                    std::visit([seen](auto& o) { o.seen = seen; }, op);
                    edit_box.update(idx, [&](AxesEdit& e) { e.plot_ops.push_back(std::move(op)); });
                };
            return [&edit_box, idx, plane_index = t.plane_index, seen](PlotDataOp op) {
                // The plane index and stamp are set here, in one place.
                std::visit([plane_index, seen](auto& o) {
                    o.plane_index = plane_index;
                    o.seen = seen;
                }, op);
                edit_box.update3d(idx, [&](AxesEdit3D& e) { e.plot_ops.push_back(std::move(op)); });
            };
        };

        // Include the slot in the ID stack so tabs don't leak across subplots.
        ImGui::PushID(idx);
        if (ImGui::BeginTabBar("##plots", ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (const DataPanelTab& tb: tabs) {
                if (tb.table < 0) {
                    // "##plane" keeps it apart from an object labelled "P0".
                    const std::string tab = "P" + std::to_string(tb.plane)
                                            + "##plane" + std::to_string(tb.plane);
                    if (ImGui::BeginTabItem(tab.c_str())) {
                        const auto objects = std::count_if(tables.begin(), tables.end(),
                                                           [&](const PlotDataTable& t) {
                                                               return t.plane_index == tb.plane;
                                                           });
                        draw_plane_tab(st, *cur3d, edit_box, idx, tb.plane,
                                       static_cast<int>(objects));
                        ImGui::EndTabItem();
                    }
                    continue;
                }
                const std::size_t i = static_cast<std::size_t>(tb.table);
                const PlotDataTable& t = tables[i];
                // A plane's objects are prefixed ("P1 heatmap 0") rather than
                // nested in a second tab bar.
                const std::string name = t.plane_index >= 0
                                             ? "P" + std::to_string(t.plane_index) + " " + t.label
                                             : t.label;
                // "###i": the ID is the index alone, so it survives a rename (the
                // name is the label; with "##" each keystroke in the Name field
                // re-IDs the tab and its widgets, dropping the field's focus).
                const std::string tab = name + "###" + std::to_string(i);
                if (ImGui::BeginTabItem(tab.c_str())) {
                    if (!t.group.empty()) {
                        ImGui::TextDisabled("%s", t.group.c_str());
                        ImGui::Separator();
                    }
                    // Appearance above the data (tables take the remaining height).
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
                        // A 2D kind on an axes or a plane; the sheet carries its
                        // options.
                        const RenderSnapshot* sheet =
                                t.plane_index < 0
                                    ? cur2d
                                    : (cur3d && static_cast<std::size_t>(t.plane_index) < cur3d->planes.size()
                                           ? &cur3d->planes[static_cast<std::size_t>(t.plane_index)].sheet
                                           : nullptr);
                        if (sheet) draw_plot_appearance(st, *sheet, t, edit_box, idx, is3d);
                    }
                    const OpSink sink = sink_for(t);
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
