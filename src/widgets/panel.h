#pragma once
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

    // The Cosmetic panel alone, inside a caller's ImGui frame (for tests).
    void draw_cosmetic_panel(const FigureSnapshot& fsnap, FigureEditBox& edit_box,
                             PanelState& st);

    // --- Subplot selection (GL-free, tested directly) ---------------------------

    // The cell containing (x, y) by the whole subplot rect, or null (margins, gaps,
    // suptitle). Cells never overlap.
    const AxesLayout* find_cell_at(const std::vector<AxesLayout>& layout, float x, float y);

    // Changes the selection and re-seeds the per-slot scratch fields.
    void select_slot(PanelState& st, const FigureSnapshot& fsnap, int slot_index);

    // Ensures the selection names an existing slot (else the first) with synced
    // scratch fields. Returns it, or -1 with no axes.
    int sync_selected_slot(PanelState& st, const FigureSnapshot& fsnap);

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

    GridDragOut update_grid_drag(PanelState& st, const FigureSnapshot& fsnap,
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
    PlotNavGate update_plot_selection(PanelState& st, const FigureSnapshot& fsnap,
                                      const std::vector<AxesLayout>& layout,
                                      const PlotPointer& in);
} // namespace sextant
