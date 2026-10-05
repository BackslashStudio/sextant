#pragma once
// The library's own figure window (GUI-kit R7): one consumer of the kit, as an
// app is another. It owns what is tied to the window's thread and GL context --
// the ImGui context with sextant's backend, and the plot view -- and composes
// the kit each frame: menu bar, dock layout with the windows "Plot", "Cosmetic"
// and "Data", the dialogs, the pending resize and the save. Its state is
// FigureWindowState, owned by the Figure (other threads read parts of it, and
// it outlives a window).
#include "imgui_context.h"
#include "plot_view.h"
#include "sextant/figure.h"

namespace sextant {
    class GLContext;
    class RenderDevice;
    class FigureEditBox;
    struct FigureSnapshot;
    struct FigureWindowState;

    class FigureWindowShell {
    public:
        // On the window's thread, with `ctx` current; destroy there too, before
        // the GLContext.
        FigureWindowShell(GLContext& ctx, const FigureOptions& opts);

        FigureWindowShell(const FigureWindowShell&) = delete;
        FigureWindowShell& operator=(const FigureWindowShell&) = delete;

        // Before each frame, outside the frame's GL lock: makes this window's
        // ImGui context current and follows a monitor change of the chrome scale,
        // so it applies to the frame that follows.
        void begin_frame(const GLContext& ctx);

        // One whole frame of the window into the current framebuffer, before
        // swap_buffers(): clear, ImGui frame, menu, docks, the kit's components,
        // dialogs, the pending resize and a requested save, then ImGui's draw.
        void frame(GLContext& ctx, RenderDevice& dev, const FigureSnapshot& fsnap,
                   FigureEditBox& edit_box, FigureWindowState& st);

        PlotView& view() { return view_; }

    private:
        FigureOptions     opts_;
        PlotView          view_;   // made before the ImGui context, destroyed after
        ImGuiPanelContext imgui_;
    };
} // namespace sextant
