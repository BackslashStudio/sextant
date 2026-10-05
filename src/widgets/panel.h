#pragma once
#include "../figure_export.h"
#include <optional>
#include <vector>

namespace sextant {
    class GLContext;
    class NvgRenderer;
    class DataRenderer;
    class PlotFbo;
    struct FigureSnapshot;
    struct FigureOptions;
    class FigureEditBox;
    struct PanelState;
    struct FigureContext;
    struct CosmeticState;
    struct PlotViewState;
    struct SaveDialogState;
    struct ResizeDialogState;
    struct Selection;
    struct AxesLayout;
    struct FigureLayout;
    struct GridTracks;
    struct PlotPointer;

    // One full ImGui frame of the docked layout: renders the plot into plot_fbo at
    // the Plot panel's size, shows it via ImGui::Image(), then draws the side
    // panels. The live window's only per-frame entry point; call on the GL/ImGui
    // thread before swap_buffers().
    void draw_widget_panel(GLContext& ctx, NvgRenderer& nvg, DataRenderer& data,
                           PlotFbo& plot_fbo, const FigureSnapshot& fsnap,
                           const FigureOptions& opts,
                           FigureEditBox& edit_box, PanelState& state);

    // The Cosmetic inspector's contents, into the caller's current window (the
    // caller opens it, and pushes the figure id). `cosmetic` is the inspector's
    // own state, owned by the host.
    void draw_cosmetic_panel(FigureContext& ctx, CosmeticState& cosmetic);

    // --- The Save and Resize dialogs (GUI-kit R4) --------------------------------
    // Contents only, like the inspectors: the host opens the window (with the
    // state's `open` as its close button) and pushes the figure id. A dialog
    // closes itself through `open` and returns a request on Save/Apply; the host
    // performs it (perform_save(), or a window resize).

    // A window resize asked for: the plot area's size in logical pixels, > 0.
    struct ResizeRequest {
        int plot_w = 0;
        int plot_h = 0;
    };

    // Opens a dialog, prefilled if it was closed: Save with the live plot size
    // (0 = live size when no plot view is on screen), Resize with the selected
    // axes' current frame.
    void open_save_dialog(SaveDialogState& st, const FigureContext& ctx);
    void open_resize_dialog(ResizeDialogState& st, const FigureContext& ctx);

    // The dialog's fields as a request, GL- and ImGui-free. A size <= 0 is the
    // live plot size from ctx.view; frame mode becomes a figure size through
    // figure_size_for_frame() with the view's stored measure. Nullopt when a
    // size stays unknown (<= 0 with no view, or before the first frame).
    std::optional<SaveRequest> resolve_save_request(const SaveDialogState& st,
                                                    const FigureContext& ctx);
    std::optional<ResizeRequest> resolve_resize_request(const ResizeDialogState& st,
                                                        const FigureContext& ctx);

    std::optional<SaveRequest> draw_save_dialog(FigureContext& ctx, SaveDialogState& st);
    std::optional<ResizeRequest> draw_resize_dialog(FigureContext& ctx, ResizeDialogState& st);

    // The last save's warning or failure, while SaveDialogState::warning is
    // non-empty (the host's window is titled by `failed`). OK clears it.
    void draw_save_warning(SaveDialogState& st);

    // --- Subplot selection (GL-free, tested directly) ---------------------------

    // The cell containing (x, y) by the whole subplot rect, or null (margins, gaps,
    // suptitle). Cells never overlap.
    const AxesLayout* find_cell_at(const std::vector<AxesLayout>& layout, float x, float y);

    // Selection changes go through Selection::select(); each state seeded from
    // the slot re-seeds on its next pull (see figure_context.h).

    // --- Grid boundary drag (GL-free) --------------------------------------------

    // A boundary between tracks k-1 and k (`cols`: columns, else rows).
    struct GridBoundary {
        bool found = false;
        bool cols = true;
        int k = 0;
    };

    // The boundary within `tol` pixels of (x, y), excluding stretches a span
    // covers. Column boundaries win at crossings.
    GridBoundary find_grid_boundary(const FigureSnapshot& fsnap, const GridTracks& tracks,
                                    float x, float y, float tol);

    // One frame of boundary dragging. `owns`: the pointer belongs to the drag this
    // frame (hiding it from selection/navigation). Double-click equalizes the two
    // tracks.
    struct GridDragOut {
        bool owns = false;
        bool cursor_ew = false; // show a column-resize cursor
        bool cursor_ns = false; // ...or a row-resize one
        std::optional<std::vector<float>> col_ratios, row_ratios; // weights to push
    };

    GridDragOut update_grid_drag(PlotViewState& pv, const FigureSnapshot& fsnap,
                                 const FigureLayout& layout, int fig_w, int fig_h,
                                 const PlotPointer& in, float tol);

    // One frame of the left button over the plot image, in layout pixels.
    struct PlotPointer {
        float x = 0.0f, y = 0.0f;
        bool hovered = false; // over the image, and nothing is on top of it
        bool active = false; // a press on the image is being held
        bool pressed = false; // the button went down on the image this frame
        bool double_clicked = false; // ...and that press completes a double-click
        bool released = false; // the held press ended this frame
        bool dragged = false; // it moved past the drag threshold before ending
    };

    // What navigation may do this frame; only for the selected cell (a drag
    // started on it, or the cursor over it for wheel and fly keys).
    struct PlotNavGate {
        bool drag = false;
        bool wheel = false;
        bool keys = false;
        bool reset = false; // double-click to restore the default view
    };

    // Click-to-select (a click without drag selects on release) plus the gate.
    // Runs whether or not Navigate is on.
    PlotNavGate update_plot_selection(Selection& sel, PlotViewState& pv,
                                      const FigureSnapshot& fsnap,
                                      const std::vector<AxesLayout>& layout,
                                      const PlotPointer& in);
} // namespace sextant
