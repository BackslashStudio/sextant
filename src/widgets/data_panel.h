#pragma once
// The "Data" dock panel: the current axes' plot objects as editable tables --
// x/y[/z] vectors side by side, heatmap matrices as a 2D grid -- under a
// shared numeric display format.
//
// Same threading contract as draw_widget_panel(): render-thread only, called
// inside ImGui's NewFrame()/Render() bracket, and it never touches live
// Axes::Impl -- committed cells go out through FigureEditBox.
#include "../plot_objects.h"
#include <cstddef>
#include <vector>

namespace sextant {

class FigureEditBox;
struct PanelState;
struct PlotDataTable;

void draw_data_panel(const FigureSnapshot& fsnap, FigureEditBox& edit_box, PanelState& st);

// One tab of the Data panel, in tab-bar order: either a plot object's table
// (`table` indexes the collect_plot_data_tables() result) or a *plane* of its
// own (`table == -1`, `plane` its index). Since v1.0 step 10.3 a plane is a
// tab like any other 3D object -- its placement controls have to live
// somewhere, and the rule is that an object with a tab is edited in it -- so
// every plane gets one, placed directly before its own objects' tabs, and an
// empty plane gets one too, which is what makes its controls reachable.
struct DataPanelTab {
    int table = -1;
    int plane = -1;
};
std::vector<DataPanelTab> data_panel_tabs(const std::vector<PlotDataTable>& tables,
                                          std::size_t plane_count);

} // namespace sextant
