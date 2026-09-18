#pragma once
// Flattens a RenderSnapshot's plot objects into a uniform list of "data
// tables" for the Data panel, plus the numeric formatting for their values.
//
// Free of any ImGui/GLFW/NanoVG dependency (the same split as hint.h): this
// decides *what* rows and columns exist, widgets/data_panel decides how to
// draw them.
#include "plot_objects.h"
#include <cstddef>
#include <string>
#include <vector>

namespace sextant {

// One editable column of a vector-shaped plot object. `values` points
// directly into the RenderSnapshot — see PlotDataTable's lifetime note.
struct DataColumn {
    const char*  name;
    const double* values;
    std::size_t  count;
};

// One plot object presented as a table. Exactly one of `columns` (vector
// mode), `heatmap` (2D grid mode) or `bars3d` (grid-with-coordinates mode) is
// populated.
//
// LIFETIME: every pointer here aliases the RenderSnapshot passed to
// collect_plot_data_tables() and is valid only while the caller holds that
// snapshot -- i.e. one render frame. Never cache a PlotDataTable across frames.
struct PlotDataTable {
    PlotKind    kind;
    int         plot_index;   // index within the snapshot's per-kind vector
    std::string label;        // opts.name when non-empty, else "line 0" etc.

    // Which plane of a 3D axes this object lives on, or -1 for a 2D axes --
    // the same addressing PlotCellEdit::plane_index uses, and carried here so
    // that a table the panel drew can be turned straight back into an op
    // without the panel having to remember where the table came from.
    int         plane_index = -1;
    // Non-empty only for a plane: "plane 0 (XY @ 0.5)". The panel prefixes
    // the tab with it rather than nesting a second tab bar, so every object
    // in the cell stays one click away whichever plane it is on.
    std::string group;

    std::vector<DataColumn> columns;          // vector mode; empty for Heatmap
    const HeatmapPlot*      heatmap = nullptr;// non-null => 2D grid mode

    // Non-null => bar3d grid mode. A bar3d grid is a |u| x |v| row-major
    // matrix of heights *plus* the two coordinate vectors the grid stands on,
    // which is neither of the shapes above -- a heatmap's grid has no
    // coordinates of its own (its extent is the axis) and a vector table has
    // no matrix. Rather than three tables that only mean anything together, it
    // is one grid whose row and column headers carry u and v.
    const Bar3DPlot* bars3d = nullptr;

    // Non-null => the same grid mode, for a surface (step 7d). Two pointers
    // rather than one neutral view struct because the two plots genuinely
    // differ in what they carry -- a bar grid has footprints and per-bar bases,
    // a surface has neither -- and the panel's grid table reads whichever is
    // set. The *shape* is shared, which is the whole return on having built it
    // as a shape in 6c rather than as "the bar3d table".
    const SurfacePlot* surface = nullptr;

    // Non-null => a mesh's *topology*, shown beside the vertex columns
    // (v1.0 step 14.4). The vertex table itself is the plain `columns` shape
    // -- a mesh's vertices are three independent coordinates and an optional
    // fourth, which is a cloud's and a path's shape exactly -- so this is not
    // a fourth table mode. It is the one thing about a mesh that is *not*
    // data: the indices are the topology the caller gave (or that ingest
    // derived), and editing one would re-mesh rather than move anything, so
    // the panel shows it read-only.
    const SurfaceTriPlot* mesh = nullptr;

    // Whichever of the two above is set, as the one question the panel asks
    // before deciding which drawing function to call.
    bool is_grid() const { return bars3d != nullptr || surface != nullptr; }

    // True when rows may be edited but not added or removed. Set for a mesh,
    // and for a reason no other vector-shaped kind has: a face names its
    // vertices *by index*, so inserting or removing one renumbers the
    // topology under it and re-points every face past the edit. Moving a
    // vertex is an edit to a value and is fine; changing how many there are
    // is a re-mesh, which is not what a "+" on a row means.
    bool rows_fixed = false;

    // Bar only: the plot's single shared bar width (BarPlot::bar_width is
    // per-plot, not per-bar). Null for every other kind.
    const double* bar_width = nullptr;
};

// Enumerates every plot object in `snap`, in a stable order (lines, scatters,
// bars, heatmaps, scatter_z — matching RenderSnapshot's member order).
std::vector<PlotDataTable> collect_plot_data_tables(const RenderSnapshot& snap);

// The 3D counterpart: the axes' own `bar3d` grids first, then every plane's
// sheet in turn through the overload above with `plane_index` and `group`
// stamped on. The order matches RenderSnapshot3D's member order, as the 2D
// one matches RenderSnapshot's.
//
// A bar3d grid was reported by count and left uneditable until step 6c, on
// the grounds that it is neither a vector-shaped plot object nor a matrix. It
// is the third shape (see PlotDataTable::bars3d), and it lives on the *axes*,
// so its `plane_index` is -1 -- the same address a 2D slot's objects carry,
// which is what routes its edits to `bars3d` rather than to a plane's sheet.
//
// LIFETIME as above: a plane's pointers alias `snap.planes[i].sheet` and a
// bar3d table aliases `snap.bars3d[i]`.
std::vector<PlotDataTable> collect_plot_data_tables(const RenderSnapshot3D& snap);

// "plane 0 (XY @ 0.5)" — the group heading and, on the plane's own tab, the
// tab's heading, so the two name a plane identically.
std::string plane_group_label(const PlaneSnapshot& p, int index);

// Display format shared by every cell of the Data panel.
struct ValueFormat {
    enum class Notation { General, Fixed, Scientific };  // %g / %f / %e
    Notation notation  = Notation::General;
    int      precision = 4;
};

// Writes a printf spec ("%.4g") for `vf` into `buf` and returns it. `buf`
// must hold at least 8 chars; precision is clamped to [0, 17].
const char* format_spec(const ValueFormat& vf, char* buf, std::size_t n);

} // namespace sextant
