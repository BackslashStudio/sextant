#pragma once
// The "Data" dock panel: the current axes' plot objects as editable tables --
// x/y[/z] vectors side by side, heatmap matrices as a 2D grid -- under a
// shared numeric display format.
//
// Same threading contract as draw_widget_panel(): render-thread only, called
// inside ImGui's NewFrame()/Render() bracket, and it never touches live
// Axes::Impl -- committed cells go out through FigureEditBox.
#include "../plot_objects.h"

namespace sextant {

class FigureEditBox;
struct PanelState;

void draw_data_panel(const FigureSnapshot& fsnap, FigureEditBox& edit_box, PanelState& st);

} // namespace sextant
