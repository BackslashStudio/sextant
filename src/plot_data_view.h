#pragma once
// Flattens a snapshot's plot objects into "data tables" for the Data panel,
// plus value formatting. No ImGui dependency; widgets/data_panel draws them.
#include "plot_objects.h"
#include <cstddef>
#include <string>
#include <vector>

namespace sextant {

// One editable column. `values` aliases the RenderSnapshot (see PlotDataTable).
struct DataColumn {
    const char*  name;
    const double* values;
    std::size_t  count;
};

// One plot object as a table: `columns` (vector mode), `heatmap` (matrix) or a
// grid (`bars3d`/`surface`, with u/v headers).
//
// LIFETIME: all pointers alias the snapshot passed to collect_plot_data_tables()
// and are valid for one render frame only. Never cache across frames.
struct PlotDataTable {
    PlotKind    kind;
    int         plot_index;   // index within the snapshot's per-kind vector
    // The plot's data_stamp; every op made from this table carries it.
    unsigned long long data_stamp = 0;
    std::string label;        // opts.name when non-empty, else "line 0" etc.

    // 3D plane this object is on, or -1 (same addressing as PlotCellEdit).
    int         plane_index = -1;
    // Plane tab prefix, e.g. "plane 0 (XY @ 0.5)"; empty for non-plane objects.
    std::string group;

    std::vector<DataColumn> columns;          // vector mode; empty for Heatmap
    const HeatmapPlot*      heatmap = nullptr;// non-null => 2D grid mode

    // Non-null => grid mode: |u| x |v| heights with u/v as headers.
    const Bar3DPlot* bars3d = nullptr;

    // Non-null => grid mode for a surface.
    const SurfacePlot* surface = nullptr;

    // Non-null => a mesh; vertices are in `columns`, and this adds the
    // read-only topology.
    const SurfaceTriPlot* mesh = nullptr;

    // True when either grid pointer is set.
    bool is_grid() const { return bars3d != nullptr || surface != nullptr; }

    // Rows may be edited but not added or removed (a mesh: faces index
    // vertices, so row count changes would re-mesh).
    bool rows_fixed = false;

    // Bar only: the plot's shared bar width.
    const double* bar_width = nullptr;
};

// Every plot object in `snap`, in RenderSnapshot member order.
std::vector<PlotDataTable> collect_plot_data_tables(const RenderSnapshot& snap);

// 3D: the axes' own bar3d grids first (plane_index -1), then each plane's
// sheet with `plane_index`/`group` set. Same lifetime rule.
std::vector<PlotDataTable> collect_plot_data_tables(const RenderSnapshot3D& snap);

// "plane 0 (XY @ 0.5)", used as both group heading and plane tab title.
std::string plane_group_label(const PlaneSnapshot& p, int index);

// Display format shared by every cell of the Data panel.
struct ValueFormat {
    enum class Notation { General, Fixed, Scientific };  // %g / %f / %e
    Notation notation  = Notation::General;
    int      precision = 4;
};

// Writes a printf spec ("%.4g") into `buf` (>= 8 chars); precision clamped to [0, 17].
const char* format_spec(const ValueFormat& vf, char* buf, std::size_t n);

} // namespace sextant
