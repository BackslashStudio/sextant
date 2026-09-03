#pragma once

namespace sextant {
class  GLContext;
class  NvgRenderer;
class  DataRenderer;
class  PlotFbo;
struct FigureSnapshot;
struct FigureOptions;
class  FigureEditBox;
struct PanelState;

// One full frame of the docked Plot (+ optional Controls/Data) layout: sets up
// the DockSpaceOverViewport, renders the 3-pass plot into plot_fbo sized to
// the live content region of the "Plot" panel, displays it via ImGui::Image(),
// then draws the side panels. One full ImGui NewFrame()...RenderDrawData()
// cycle, and the only per-frame render entry point for a live window. Must run
// on the same thread as ctx's GLContext and its ImGuiPanelContext, and before
// ctx.swap_buffers().
void draw_widget_panel(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                       PlotFbo& plot_fbo, const FigureSnapshot& fsnap,
                       const FigureOptions& opts,
                       FigureEditBox& edit_box, PanelState& state);

} // namespace sextant
