#pragma once
#include "sextant/style.h"
#include "sextant/axes3d.h"
#include "../hint_index.h"
#include "../plot_data_view.h"
#include "cell_shading.h"
#include "../tick.h"
#include "../renderer/figure_layout.h"
#include <atomic>
#include <string>
#include <vector>

namespace sextant {

// Pure render-thread UI state for the widget panel: never touched by
// Figure::refresh() or Axes::Impl directly, only ever read/written from
// inside draw_widget_panel() on the same thread it was constructed on.
struct PanelState {
    int  selected_slot_index = 1;

    // Runtime show/hide for the "Cosmetic" dock panel, bound to the View menu.
    // The "Plot" panel always exists. layout_cosmetic_visible tracks what the
    // dockspace was last built for, so ensure_layout() rebuilds (losing any
    // user-dragged splitter position) only on an actual toggle.
    bool cosmetic_visible = true;
    bool layout_cosmetic_visible = true;

    // The Data panel docks as a second tab in the same node as
    // "Cosmetic" (or takes that node alone when Cosmetic is hidden), so it
    // needs its own layout mirror for ensure_layout()'s rebuild gate.
    // Default on since v1.0 step 10.3, when the per-object controls (planes,
    // bar grids, surfaces) moved into its tabs -- off, they would be two
    // clicks away. Cosmetic still owns the shared tab bar on open, because
    // focus_data_on_rebuild below starts false.
    bool data_visible = true;
    bool layout_data_visible = true;

    // Which panel owns the shared tab bar's selection the next time
    // ensure_layout() rebuilds. DockBuilderRemoveNode() discards the node's
    // SelectedTabId and ImGui then selects the *last* tab, so without this,
    // toggling Cosmetic off and on while Data is visible re-selects Data.
    bool focus_data_on_rebuild = false;

    // The panel ("Cosmetic" or "Data") a layout rebuild wants in front,
    // focused by draw_widget_panel() once both panels have been drawn that
    // frame. Needed on top of the seeded SelectedTabId because ImGui hands the
    // tab to whichever docked window has focus, and a window is focused as it
    // appears -- so with both appearing at once, the one drawn second won.
    // Points at a string literal; null when nothing is pending.
    const char* pending_panel_focus = nullptr;

    // Display format for every cell of the Data panel (notation + precision).
    ValueFormat value_format;

    // Tint each Data-panel cell by where its value sits in its own
    // column's min/max range (see cell_shading.h), and the cached ranges that
    // drive it. On by default — the shading is the readable state, and this
    // is panel chrome, so nothing a figure renders changes either way.
    bool             shade_cells = true;
    CellShadingCache cell_shading;

    // Left-most matrix column shown by the Data panel's grid tables. Only used
    // when a grid is wider than one table can hold (see kMaxGridCols in
    // data_panel.cpp); shared across every grid rather than tracked per plot
    // -- only one tab is on screen at a time -- and clamped to the current
    // grid every frame.
    int grid_col_offset = 0;

    // Which of a bar3d grid's two row-major matrices the Data panel's cells
    // edit: false = heights, true = the per-bar bases. Only offered when the
    // plot actually has bases; a plot standing on the single
    // Bar3DOptions::bottom has nothing per bar to edit.
    bool bar3d_show_bases = false;

    // Pan/zoom: when on, left-drag pans and scroll zooms the currently
    // selected axes slot -- and only it: a drag must *start* on the selected
    // cell and the wheel must be over it (see update_plot_selection()). A drag
    // that has started keeps going when the cursor leaves the cell, which is
    // ImGui's IsItemActive() on the InvisibleButton over the plot image.
    bool navigate_enabled = false;

    // Click-to-select (v1.0 step 10.2). The slot under the cursor when the
    // left button went down on the plot, -1 for none; cleared on release.
    // Selection changes on the *release* of a click that stayed in that cell,
    // so the first click selects and only a later press navigates.
    int  press_slot        = -1;
    // Whether that press landed on the cell that was already selected -- the
    // only kind of press a drag may navigate with.
    bool press_on_selected = false;
    // Set by a release that changed the selection, cleared by the next press:
    // the double-click that completes the selecting click must not also reset
    // the view of the cell it just picked.
    bool selected_by_last_click = false;

    // Dragging a boundary between two grid tracks (v1.0 step 15.3). Captured
    // at the press: which boundary (`cols` true for one between columns, `k`
    // the track after it), where the press was, the weights then, and the two
    // tracks' lengths and minimum lengths in pixels. Every later frame of the
    // drag is computed from these, not from the frame before, so a drag that
    // is clamped and comes back ends where the cursor is.
    struct GridDrag {
        bool  active = false;
        bool  cols   = true;
        int   k      = 0;
        float press  = 0.0f;
        std::vector<float> w0;
        float len_a = 0.0f, len_b = 0.0f;
        float min_a = 0.0f, min_b = 0.0f;
        float last_a = -1.0f;   // the split last pushed, so a still cursor pushes nothing
    };
    GridDrag grid_drag;

    // Default-on toggle for the hover tooltip, bound
    // directly to the Cosmetic panel's "Hints" checkbox.
    bool hints_enabled = true;

    // Spatial index behind find_hint(), keyed on data_generation and built
    // lazily. Lives here rather than beside DataRenderer's caches because
    // hit-testing is panel work, though the invalidation rule is identical.
    HintIndexCache hint_index;

    char title_buf[256]{};
    char xtitle_buf[128]{};
    char ytitle_buf[128]{};

    // Scratch for the one plot-object name field the Data panel draws (v1.0
    // step 11.6 follow-up). One buffer rather than one per object because the
    // panel draws a single tab per frame, so only one such field can exist at
    // a time; text_field() re-seeds it from the published value whenever the
    // widget is not the active item, which is the whole of its lifecycle.
    char name_buf[128]{};
    bool   grid_local  = false;
    bool   xauto_local = true, yauto_local = true;
    double xmin_local = 0, xmax_local = 1, ymin_local = 0, ymax_local = 1;
    std::vector<Tick> xticks_scratch, yticks_scratch;

    // AxesStyle::origin_x/origin_y are optional, and a drag box needs a live
    // double to point at whether or not one is engaged (v1.0 step 19).
    // Keeping the last value here is also what lets un-ticking and re-ticking
    // the pin restore the number the user had, instead of snapping to zero.
    double origin_x_scratch = 0.0, origin_y_scratch = 0.0;
    // z has no 2D use, but it lives here rather than in the 3D block below
    // because the three components are one control group in the panel, and
    // splitting them across two structs would say they were not (step 20).
    double origin_z_scratch = 0.0;

    // Cosmetics sections (spine/tick/label/title colors+sizes+thickness, and
    // the grid/legend/colorbar option structs). Color widgets bind directly
    // to &<field>.r since Color is already a plain {r,g,b,a} float struct.
    AxesStyle       axes_style_local;
    GridOptions     grid_opts_local;
    bool            legend_enabled_local = false;
    LegendOptions   legend_local;
    ColorbarOptions colorbar_local;

    // 3D scratch (v1.0 step 2). Only what a 3D axes has and a 2D one does
    // not: the third axis, the camera and the box. Everything the two kinds
    // genuinely share -- the title buffers, the x/y limits, the two tick
    // tables, axes_style_local, grid_opts_local, grid_local -- is reused from
    // the fields above rather than duplicated, since a slot is one kind or
    // the other and last_synced_slot re-seeds them whenever the selection
    // moves between them.
    char ztitle_buf[128]{};
    double zmin_local = 0, zmax_local = 1;
    bool   zauto_local = true;
    std::vector<Tick> zticks_scratch;
    Camera3D   camera_local;
    Box3DStyle box3d_local;
    BoxAspect  aspect_local;

    // One plane's tab in the Data panel (Planes section of the Cosmetic panel
    // until v1.0 step 10.3). A scratch copy for the same reason
    // aspect_local is one: a drag field has to hold a stable value across the
    // frames of a gesture, and the snapshot lags a frame behind the edit.
    // Positional, exactly as the plot indices are; the list is re-seeded
    // whenever the selection moves *or* the plane count changes.
    struct PlaneUi {
        PlaneOrientation orient = PlaneOrientation::XY;
        double           offset = 0.0;
        Plane2DOptions   opts;
    };
    std::vector<PlaneUi> planes_local;

    // The gridded 3D kinds' appearance, held for the same reason and re-seeded
    // by the same rule: a drag field needs a stable value across a gesture,
    // and the index is positional so a plot added or dropped re-reads the
    // whole list rather than resizing it. Both re-seeds -- on a slot change
    // and on a count change -- happen in sync_selected_slot(), which every
    // panel and the plot run, so neither depends on which panel is shown.
    std::vector<Bar3DOptions>   bars3d_local;
    std::vector<SurfaceOptions> surfaces_local;
    std::vector<Scatter3DOptions> scatter3d_local;
    std::vector<Line3DOptions>    line3d_local;
    std::vector<SurfaceTriOptions> surface_tri_local;

    // Forces a re-sync of every *_local/*_buf/*_scratch field above from
    // the current RenderSnapshot whenever the selected axes slot changes
    // (including the very first frame, since -1 never matches a real slot).
    // Checked by select_slot()/sync_selected_slot() as well as by the Cosmetic
    // panel: navigation reads camera_local too, and Cosmetic can be hidden.
    int last_synced_slot = -1;

    // Figure-level, so NOT covered by last_synced_slot above (which tracks
    // the per-axes fields): the suptitle belongs to the whole figure and is
    // edited from whichever axes happens to be selected. Seeded once, on the
    // first frame that draws the panel.
    char suptitle_buf[256]{};
    SuptitleOptions suptitle_local;
    bool suptitle_synced = false;

    // Layout controls -- figure-level like the suptitle, and seeded
    // by the same once-only rule for the same reason: re-seeding from the
    // snapshot every frame would fight the drag that is producing the edit.
    FigureMargins margins_local;
    float         col_gap_local = 0.0f;
    float         row_gap_local = 0.0f;
    bool          layout_synced = false;

    // "File > Save" menu and its undocked dialog. Render-thread only.
    // save_width/save_height <=0 means "use the Plot panel's live size",
    // mirroring the private Figure::savefig_png/svg convention.
    bool save_dialog_open = false;
    char save_path_buf[260] = "figure.png";
    int  save_width  = 0;
    int  save_height = 0;
    bool save_requested = false;

    // Whether save_width/save_height mean the whole figure or the selected
    // subplot's plot frame; in the latter case the figure size is derived by
    // figure_size_for_frame().
    enum class SizeMode { Figure, PlotFrame };
    SizeMode save_size_mode = SizeMode::Figure;

    // The two bounds an export is allowed to give up at, both 0 meaning the
    // automatic one -- SvgExportOptions::max_splits and
    // PngExportOptions::peel_layers. They live here rather than being asked
    // for only when something has already gone wrong, because the dialog is
    // where a user who has just been told "raise max_splits above 10,481" goes
    // to do it. Only the field for the format the filename names is shown.
    int save_max_splits  = 0;
    int save_peel_layers = 0;

    // What the last save had to admit. Non-empty opens a modal that has to be
    // dismissed -- an export that is knowingly wrong is not a status line.
    // Set by the save site in draw_widget_panel(), cleared by the modal.
    std::string save_warning;
    bool        save_warning_open = false;

    // "File > Resize to plot frame" — its own dialog, applied by writing
    // pending_plot_w/h below.
    bool resize_dialog_open = false;
    int  resize_frame_w = 0;
    int  resize_frame_h = 0;

    // A requested plot-area size in physical pixels, waiting for the window
    // thread to apply it (>0 means pending). Written by Figure::resize() from
    // any thread and by the resize dialog from the render thread; consumed in
    // draw_widget_panel(), the only place that may touch the GLFW window. The
    // request names the *plot* size, so the window grows by whatever its menu
    // bar and Cosmetic column occupy.
    std::atomic<int> pending_plot_w{0};
    std::atomic<int> pending_plot_h{0};

    // Live physical-pixel size the plot was most recently rendered at. The one
    // exception to this struct's render-thread-only contract: savefig_png()/
    // savefig_svg() read these from an arbitrary caller thread to default
    // their save size to whatever the window currently shows.
    std::atomic<int> live_plot_w{0};
    std::atomic<int> live_plot_h{0};

    // The window's stored layout (v1.0 step 15.2). fit() is render-thread
    // only, from draw_plot_panel(); load() is the second exception to this
    // struct's contract, read by savefig() and size_for_frame() on the caller
    // thread so they lay out as the window does. Every other panel consumer --
    // the Layout readout, the Save and Resize dialogs -- reads load() as well,
    // so nothing on screen is computed from a different measure than the plot.
    LayoutStore layout;

    // What the limits an axes left on "auto" actually resolved to, per slot,
    // in the frame that was last drawn.
    //
    // The Limits fields used to be seeded from the snapshot, which carries the
    // *declared* limits -- and an automatic axes never uses those, so the
    // panel showed 0..1 beside an axis reading 400..600. They are not the same
    // number and the panel had no way to know the other one: resolution
    // happens in compute_figure_layout(), on the render thread, after the
    // snapshot is built. So draw_plot_panel() stashes them here from the
    // layout it has just rendered with, which is the only place both are in
    // hand at once.
    struct ResolvedLimits {
        int    slot = 0;
        bool   is_3d = false;
        double xmin = 0.0, xmax = 1.0;
        double ymin = 0.0, ymax = 1.0;
        double zmin = 0.0, zmax = 1.0;   // 3D only
    };
    std::vector<ResolvedLimits> resolved;

    const ResolvedLimits* resolved_for(int slot) const {
        for (const ResolvedLimits& r : resolved)
            if (r.slot == slot) return &r;
        return nullptr;
    }
};

} // namespace sextant
