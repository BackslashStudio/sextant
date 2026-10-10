#pragma once
// The plot view: a figure's picture as a kit component. Renders the
// figure into its own offscreen target at the size it is given, shows it with
// ImGui::Image(), and turns the pointer over it into selection, grid drags,
// navigation, hints and Figure::connect() events. Draws into the caller's
// current window (the host opens it and pushes the figure id); never calls a
// platform backend.
#include "panel_state.h"
#include "../renderer/data_renderer.h"
#include "../renderer/plot_fbo.h"
#include <imgui.h>
#include <vector>

namespace sextant {
    class RenderDevice;
    struct FigureContext;
    struct WindowEvent;

    // One frame's inputs from the host besides the figure.
    struct PlotViewParams {
        // The image's size in ImGui units (the host's content region, usually).
        ImVec2 size{0.0f, 0.0f};
        // The display's content scale: framebuffer pixels per logical pixel.
        float display_scale = 1.0f;
        // FigureOptions::supersample.
        int supersample = 1;
        // The key transitions since the last frame, for Figure::connect()'s key
        // events; null = none. The host takes them from its backend.
        const std::vector<WindowEvent>* keys = nullptr;
    };

    // The view's GL half: its offscreen target and its data renderer, whose
    // caches are per view. Make, use and destroy it with one GL context
    // current. The view's other state (PlotViewState) is the host's, passed
    // to draw(), since parts of it are read from other threads.
    class PlotView {
    public:
        PlotView() = default;
        PlotView(const PlotView&) = delete;
        PlotView& operator=(const PlotView&) = delete;

        // One frame of the view, with `dev` on the current context. `st` is this
        // view's own state; ctx.view is not read (this is the view).
        PlotViewInfo draw(FigureContext& ctx, RenderDevice& dev, PlotViewState& st,
                          const PlotViewParams& params);

        // For a save from the view's caches (perform_save()), on the same context.
        DataRenderer& data_renderer() { return data_; }

        PlotFbo& fbo() { return fbo_; }

    private:
        DataRenderer data_;
        PlotFbo      fbo_; // sized on the first draw()
    };
} // namespace sextant
