#include "figure_window_shell.h"
#include "panel.h"
#include "data_panel.h"
#include "panel_state.h"
#include "figure_context.h"
#include "panel_widgets.h"
#include "imgui_impl_sextant.h"
#include "../figure_export.h"
#include "../window_link.h"
#include "../renderer/gl_context.h"
#include "../renderer/render_device.h"
#include "../renderer/figure_layout.h"
#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder* — not part of ImGui's stable public API
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace sextant {

namespace {

// Dock layout: "Plot" alone, or split with a right column (panel_width wide)
// holding "Cosmetic" and/or "Data" in one shared node. Rebuilt on the first
// frame and when a side panel toggles (restoring the default split).
void ensure_layout(ImGuiID dockspace_id, float panel_width, FigureWindowState& st) {
    const bool first_build = ImGui::DockBuilderGetNode(dockspace_id) == nullptr;
    if (!first_build && st.shell.layout_cosmetic_visible == st.shell.cosmetic_visible
                     && st.shell.layout_data_visible == st.shell.data_visible) return;
    st.shell.layout_cosmetic_visible = st.shell.cosmetic_visible;
    st.shell.layout_data_visible     = st.shell.data_visible;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    // Use the viewport's logical size (not framebuffer pixels). panel_width
    // arrives DPI-scaled; the split is stored as a fraction, so only the first
    // build needs the scale.
    const ImVec2 size = ImGui::GetMainViewport()->Size;
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    if (st.shell.cosmetic_visible || st.shell.data_visible) {
        const float side_frac = std::clamp(panel_width / size.x, 0.05f, 0.6f);
        ImGuiID dock_side, dock_plot;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, side_frac,
                                    &dock_side, &dock_plot);
        ImGui::DockBuilderDockWindow("Plot", dock_plot);
        if (st.shell.cosmetic_visible) ImGui::DockBuilderDockWindow("Cosmetic", dock_side);
        if (st.shell.data_visible)     ImGui::DockBuilderDockWindow("Data",     dock_side);

        // Seed the tab bar's selection (a rebuilt node otherwise selects the
        // last tab). ImGui also gives the tab to the focused window, so the
        // wanted panel is focused explicitly too (pending_panel_focus).
        if (st.shell.cosmetic_visible && st.shell.data_visible) {
            const char* want = st.shell.focus_data_on_rebuild ? "Data" : "Cosmetic";
            if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(dock_side))
                n->SelectedTabId = ImHashStr("#TAB", 0, ImHashStr(want));
            st.shell.pending_panel_focus = want;
        }
    } else {
        ImGui::DockBuilderDockWindow("Plot", dockspace_id);
    }
    st.shell.focus_data_on_rebuild = false;
    ImGui::DockBuilderFinish(dockspace_id);
}

// The main menu bar: shell code, over the window's own states. Must run before
// ensure_layout()/DockSpaceOverViewport(), so the dockspace sees the work area
// shrunk by the menu bar.
void draw_menu_bar(const FigureContext& fig, FigureWindowState& st) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save"))
                open_save_dialog(st.save, fig);
            if (ImGui::MenuItem("Resize to plot frame"))
                open_resize_dialog(st.resize, fig);
            // Re-measure on demand (e.g. tick labels frozen by navigation).
            if (ImGui::MenuItem("Refit layout"))
                st.plot.layout.request_refit();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Cosmetic Panel", nullptr, &st.shell.cosmetic_visible);
            // Turning Data on brings it to the front; ensure_layout() clears
            // the flag.
            if (ImGui::MenuItem("Data Panel", nullptr, &st.shell.data_visible))
                st.shell.focus_data_on_rebuild = st.shell.data_visible;
            ImGui::EndMenu();
        }
        // Interaction modes: they govern the mouse over the plot, so they are
        // the plot view's own settings.
        if (ImGui::BeginMenu("Edit")) {
            ImGui::MenuItem("Navigate", nullptr, &st.plot.navigate_enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Left-drag pans, scroll zooms at the cursor,\n"
                                  "double-click resets — on the selected subplot.");
            ImGui::MenuItem("Hints", nullptr, &st.plot.hints_enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show a tooltip for the data point under the cursor.");
            ImGui::EndMenu();
        }
        // The subplot every panel edits (clicking a subplot also sets it).
        if (fig.snap.axes.size() > 1) {
            int sel = fig.selection.slot;
            ImGui::SetNextItemWidth(220.0f);
            if (axes_selector("##axessel", fig.snap, sel))
                fig.selection.select(sel);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The subplot the panels edit and Navigate moves.\n"
                                  "Clicking a subplot selects it too.");
        }
        ImGui::EndMainMenuBar();
    }
}

// Applies a pending plot-area size to the window, adding the chrome measured
// from the current frame. Render thread only (the request is an atomic); the
// window itself is resized by the pumping thread, from the link's queue.
void apply_pending_resize(GLContext& ctx, FigureWindowState& st) {
    const int want_w = st.shell.pending_plot_w.load(std::memory_order_relaxed);
    const int want_h = st.shell.pending_plot_h.load(std::memory_order_relaxed);
    if (want_w <= 0 || want_h <= 0) return;

    const int plot_w = st.plot.live_plot_w.load(std::memory_order_relaxed);
    const int plot_h = st.plot.live_plot_h.load(std::memory_order_relaxed);
    // Nothing rendered yet: leave the request pending.
    if (plot_w <= 0 || plot_h <= 0) return;

    st.shell.pending_plot_w.store(0, std::memory_order_relaxed);
    st.shell.pending_plot_h.store(0, std::memory_order_relaxed);

    // The request is logical; the window chrome around the plot is measured in
    // framebuffer pixels, so the plot goes through the display scale first.
    const int plot_fb_w = st.plot.live_plot_fb_w.load(std::memory_order_relaxed);
    const int plot_fb_h = st.plot.live_plot_fb_h.load(std::memory_order_relaxed);
    const float display_scale = std::max(ctx.link().content_scale(), 0.01f);
    const int target_fb_w = static_cast<int>(std::lround(want_w * display_scale))
                            + (ctx.width()  - plot_fb_w);
    const int target_fb_h = static_cast<int>(std::lround(want_h * display_scale))
                            + (ctx.height() - plot_fb_h);
    if (target_fb_w <= 0 || target_fb_h <= 0) return;

    // A window is sized in screen coordinates, not framebuffer pixels.
    int win_w = 0, win_h = 0, fb_w = 0, fb_h = 0;
    ctx.link().window_size(win_w, win_h);
    ctx.link().framebuffer_size(fb_w, fb_h);
    const double sx = (fb_w > 0) ? static_cast<double>(win_w) / fb_w : 1.0;
    const double sy = (fb_h > 0) ? static_cast<double>(win_h) / fb_h : 1.0;

    ctx.link().post_resize(static_cast<int>(std::lround(target_fb_w * sx)),
                           static_cast<int>(std::lround(target_fb_h * sy)));
}

} // namespace

FigureWindowShell::FigureWindowShell(GLContext& ctx, const FigureOptions& opts)
    : opts_(opts), imgui_(ctx, opts) {}

void FigureWindowShell::begin_frame(const GLContext& ctx) {
    imgui_.make_current(); // this thread's context, never a sibling's
    imgui_.sync_dpi_scale(ctx);
}

void FigureWindowShell::frame(GLContext& ctx, RenderDevice& dev, const FigureSnapshot& fsnap,
                              FigureEditBox& edit_box, FigureWindowState& st) {
    const FigureOptions& opts = opts_;
    PlotView& view = view_;

    // Base clear, to avoid undefined content before the docks are laid out.
    glViewport(0, 0, ctx.width(), ctx.height());
    glClearColor(0.93f, 0.93f, 0.93f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSextant_NewFrame(ctx.link());
    ImGui::NewFrame();

    // The backend's key tap, emptied every frame whether or not anything is
    // connected; the plot view turns what ImGui did not take into key events.
    std::vector<WindowEvent> keys;
    ImGui_ImplSextant_TakeKeys(keys);

    // The shell owns the windows (names and flags ensure_layout() docks); the
    // components draw their contents under the figure's id. `view` is the plot
    // view's last output, refreshed below once it has drawn this frame.
    FigureContext fig{ fsnap, edit_box, st.selection, st.slot_view, &st.plot_info, st.figure_id };

    draw_menu_bar(fig, st);

    const ImGuiID dockspace_id = ImGui::GetID("SextantDockspace");
    ensure_layout(dockspace_id,
                  opts.panel_width * ImGui::GetStyle().FontScaleDpi, st);
    ImGui::DockSpaceOverViewport(dockspace_id);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Plot", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    push_figure_id(fig.figure_id);
    st.plot_info = view.draw(fig, dev, st.plot,
                             PlotViewParams{ .size = ImGui::GetContentRegionAvail(),
                                             .display_scale = ctx.link().content_scale(),
                                             .supersample = opts.supersample,
                                             .keys = &keys });
    ImGui::PopID();
    ImGui::End();
    if (st.shell.cosmetic_visible) {
        ImGui::Begin("Cosmetic", nullptr, ImGuiWindowFlags_NoCollapse);
        push_figure_id(fig.figure_id);
        draw_cosmetic_panel(fig, st.cosmetic);
        ImGui::PopID();
        ImGui::End();
    }
    if (st.shell.data_visible) {
        ImGui::Begin("Data", nullptr, ImGuiWindowFlags_NoCollapse);
        push_figure_id(fig.figure_id);
        draw_data_panel(fig, st.data);
        ImGui::PopID();
        ImGui::End();
    }
    // After both side panels have begun (see ensure_layout()).
    if (st.shell.pending_panel_focus) {
        ImGui::SetWindowFocus(st.shell.pending_panel_focus);
        st.shell.pending_panel_focus = nullptr;
    }

    // The dialogs float (NoDocking) and size to their contents.
    constexpr ImGuiWindowFlags dialog_flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
    std::optional<SaveRequest> save;
    if (st.save.open) {
        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Save Figure", &st.save.open, dialog_flags);
        push_figure_id(fig.figure_id);
        save = draw_save_dialog(fig, st.save);
        ImGui::PopID();
        ImGui::End();
    }
    // Shows the stored warning (set by a save one frame earlier).
    if (!st.save.warning.empty()) {
        ImGui::SetNextWindowSize(ImVec2(430, 0), ImGuiCond_FirstUseEver);
        // "###": one window whichever title, so it keeps its place.
        ImGui::Begin(st.save.failed ? "Save failed###save_warning"
                                    : "Export warning###save_warning",
                     nullptr, dialog_flags);
        push_figure_id(fig.figure_id);
        draw_save_warning(st.save);
        ImGui::PopID();
        ImGui::End();
    }
    if (st.resize.open) {
        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Resize", &st.resize.open, dialog_flags);
        push_figure_id(fig.figure_id);
        const std::optional<ResizeRequest> resize = draw_resize_dialog(fig, st.resize);
        ImGui::PopID();
        ImGui::End();
        // Figure::resize()'s channel, applied just below.
        if (resize) {
            st.shell.pending_plot_w.store(resize->plot_w, std::memory_order_relaxed);
            st.shell.pending_plot_h.store(resize->plot_h, std::memory_order_relaxed);
        }
    }

    // After the plot panel has published this frame's live size.
    apply_pending_resize(ctx, st);

    // Performed here since a PNG save needs ctx/nvg/data (safe mid-frame: its
    // own FBO). Exports the render thread's snapshot, including panel edits,
    // laid out with the window's measurements, at the figure's dpi (1x by
    // default), not the display's: a file is the same on every machine.
    if (save) {
        const auto on_screen = on_screen_measure(&st.plot_info, fsnap);
        const SaveResult r = perform_save(dev, view.data_renderer(), fsnap, *save, on_screen.get(),
                                          opts.supersample, opts.dpi / 96.0f);
        if (!r.written || !r.exact) {
            st.save.warning = r.warning;
            st.save.failed  = !r.written;
        }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace sextant
