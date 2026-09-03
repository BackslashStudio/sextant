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

// One plot object presented as a table. Exactly one of `columns` (vector mode)
// or `heatmap` (2D grid mode) is populated.
//
// LIFETIME: every pointer here aliases the RenderSnapshot passed to
// collect_plot_data_tables() and is valid only while the caller holds that
// snapshot -- i.e. one render frame. Never cache a PlotDataTable across frames.
struct PlotDataTable {
    PlotKind    kind;
    int         plot_index;   // index within the snapshot's per-kind vector
    std::string label;        // opts.label when non-empty, else "line 0" etc.

    std::vector<DataColumn> columns;          // vector mode; empty for Heatmap
    const HeatmapPlot*      heatmap = nullptr;// non-null => 2D grid mode

    // Bar only: the plot's single shared bar width (BarPlot::bar_width is
    // per-plot, not per-bar). Null for every other kind.
    const double* bar_width = nullptr;
};

// Enumerates every plot object in `snap`, in a stable order (lines, scatters,
// bars, heatmaps, scatter_z — matching RenderSnapshot's member order).
std::vector<PlotDataTable> collect_plot_data_tables(const RenderSnapshot& snap);

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
