#pragma once
#include <optional>
#include <vector>

namespace sextant {
class  GLContext;
class  NvgRenderer;
class  DataRenderer;
class  PlotFbo;
struct FigureSnapshot;
struct FigureOptions;
class  FigureEditBox;
struct PanelState;
struct AxesLayout;
struct FigureLayout;
struct GridTracks;
struct PlotPointer;

// One full frame of the docked Plot (+ optional Cosmetic/Data) layout: sets up
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

// The Cosmetic panel alone, inside a caller-supplied ImGui frame. Exposed for
// the same reason draw_data_panel() is: sextant_layout_test drives it through
// a null-backend frame, which is the only way to assert on what the panel does
// without standing up a window.
void draw_cosmetic_panel(const FigureSnapshot& fsnap, FigureEditBox& edit_box,
                         PanelState& st);

// --- Subplot selection (v1.0 step 10.2) -------------------------------------
// Everything below is GL-free, so the layout test drives it directly.

// The cell under (x, y) in the layout's pixels, by the *whole* subplot rect
// (AxesLayout::cell), or null in a margin, a gap or the suptitle band. A
// span's cell includes the gaps it covers. Cells never overlap: a Figure has
// one grid shape and refuses a subplot on an occupied cell, so the first cell
// containing the point is the only one.
const AxesLayout* find_cell_at(const std::vector<AxesLayout>& layout, float x, float y);

// The one way the selection changes: sets selected_slot_index and re-seeds
// the per-slot scratch fields (camera_local, the limits, ...) at once. The
// Cosmetic panel used to be the only thing that re-seeded them, so with it
// hidden a slot change left navigation orbiting from the previous axes'
// camera.
void select_slot(PanelState& st, const FigureSnapshot& fsnap, int slot_index);

// Makes the selection name a slot that exists (falling back to the first, as
// the panels always have) and the scratch fields describe it. Returns the
// slot, or -1 with no axes at all.
int sync_selected_slot(PanelState& st, const FigureSnapshot& fsnap);

// --- Grid boundary drag (v1.0 step 15.3) -------------------------------------
// GL-free for the same reason.

// A boundary between two tracks of the grid: `cols` true for one between
// columns k-1 and k, false for one between rows k-1 and k.
struct GridBoundary {
    bool found = false;
    bool cols  = true;
    int  k     = 0;
};

// The boundary under (x, y): within `tol` pixels of the gap between two
// tracks, and only along the stretch of it where no subplot spans across it --
// a boundary through the middle of a span is not an edge of anything there.
// A column boundary wins where it crosses a row boundary.
GridBoundary find_grid_boundary(const FigureSnapshot& fsnap, const GridTracks& tracks,
                                float x, float y, float tol);

// What the boundary drag did with one frame of the pointer. `owns` means the
// pointer is the drag's this frame -- a press on a boundary, the drag it
// starts, or hovering one -- so selection and navigation must not see it.
// A double-click on a boundary gives its two tracks equal weight.
struct GridDragOut {
    bool owns      = false;
    bool cursor_ew = false;   // show a column-resize cursor
    bool cursor_ns = false;   // ...or a row-resize one
    std::optional<std::vector<float>> col_ratios, row_ratios;   // weights to push
};
GridDragOut update_grid_drag(PanelState& st, const FigureSnapshot& fsnap,
                             const FigureLayout& layout, int fig_w, int fig_h,
                             const PlotPointer& in, float tol);

// One frame of the left button over the plot image, in the layout's pixels.
struct PlotPointer {
    float x = 0.0f, y = 0.0f;
    bool  hovered        = false;   // over the image, and nothing is on top of it
    bool  active         = false;   // a press on the image is being held
    bool  pressed        = false;   // the button went down on the image this frame
    bool  double_clicked = false;   // ...and that press completes a double-click
    bool  released       = false;   // the held press ended this frame
    bool  dragged        = false;   // it moved past the drag threshold before ending
};

// What navigation may do with this frame's input. Everything is false unless
// it concerns the selected cell: a drag must have started on it, the wheel and
// the fly keys need the cursor over it (or its own drag held).
struct PlotNavGate {
    bool drag  = false;
    bool wheel = false;
    bool keys  = false;
    bool reset = false;   // double-click to restore the default view
};

// Click-to-select plus the gate above. A click -- press and release inside
// one cell without passing the drag threshold -- selects that cell on the
// release. Runs whether or not Navigate is on: the selection is what every
// panel edits, not only what the mouse moves.
PlotNavGate update_plot_selection(PanelState& st, const FigureSnapshot& fsnap,
                                  const std::vector<AxesLayout>& layout,
                                  const PlotPointer& in);

} // namespace sextant
