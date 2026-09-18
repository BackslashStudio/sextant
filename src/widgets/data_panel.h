#pragma once
// The "Data" dock panel: the selected axes' plot objects as editable tables.
// Render-thread only, inside an ImGui frame; edits go out via FigureEditBox.
#include "../plot_objects.h"
#include <cstddef>
#include <vector>

namespace sextant {
    class FigureEditBox;
    struct PanelState;
    struct PlotDataTable;

    void draw_data_panel(const FigureSnapshot& fsnap, FigureEditBox& edit_box, PanelState& st);

    // One Data-panel tab: a plot object's table (`table` indexes
    // collect_plot_data_tables()) or a plane (`table == -1`, `plane` its index).
    // Every plane gets a tab, placed before its objects' tabs.
    struct DataPanelTab {
        int table = -1;
        int plane = -1;
    };

    std::vector<DataPanelTab> data_panel_tabs(const std::vector<PlotDataTable>& tables,
                                              std::size_t plane_count);
} // namespace sextant
