#pragma once
#include "sextant/style.h"
#include "../hint_index.h"
#include "../plot_data_view.h"
#include "cell_shading.h"
#include "../tick.h"
#include <atomic>
#include <vector>

namespace sextant {

// Pure render-thread UI state for the widget panel: never touched by
// Figure::refresh() or Axes::Impl directly, only ever read/written from
// inside draw_widget_panel() on the same thread it was constructed on.
struct PanelState {
    int  selected_slot_index = 1;

    // Runtime show/hide for the "Controls" dock panel, bound to the View menu.
    // The "Plot" panel always exists. layout_controls_visible tracks what the
    // dockspace was last built for, so ensure_layout() rebuilds (losing any
    // user-dragged splitter position) only on an actual toggle.
    bool controls_visible = true;
    bool layout_controls_visible = true;

    // The Data panel docks as a second tab in the same node as
    // "Controls" (or takes that node alone when Controls is hidden), so it
    // needs its own layout mirror for ensure_layout()'s rebuild gate.
    // Default off — an unused Data panel should cost nothing.
    bool data_visible = false;
    bool layout_data_visible = false;

    // Which panel owns the shared tab bar's selection the next time
    // ensure_layout() rebuilds. DockBuilderRemoveNode() discards the node's
    // SelectedTabId and ImGui then selects the *last* tab, so without this,
    // toggling Controls off and on while Data is visible re-selects Data.
    bool focus_data_on_rebuild = false;

    // Display format for every cell of the Data panel (notation + precision).
    ValueFormat value_format;

    // Tint each Data-panel cell by where its value sits in its own
    // column's min/max range (see cell_shading.h), and the cached ranges that
    // drive it. On by default — the shading is the readable state, and this
    // is panel chrome, so nothing a figure renders changes either way.
    bool             shade_cells = true;
    CellShadingCache cell_shading;

    // Left-most matrix column shown by the Data panel's 2D grid. Only used
    // when a heatmap is wider than one table can hold (see kMaxGridCols in
    // data_panel.cpp); shared across heatmaps rather than tracked per plot,
    // and clamped to the current matrix every frame.
    int heatmap_col_offset = 0;

    // Pan/zoom: when on, left-drag pans and scroll zooms the currently
    // selected axes slot, regardless of which subplot cell the cursor is over.
    // Drag persistence across frames comes from ImGui's IsItemActive() on the
    // InvisibleButton over the plot image, so no extra state is needed here.
    bool navigate_enabled = false;

    // Default-on toggle for the hover tooltip, bound
    // directly to the Controls panel's "Hints" checkbox.
    bool hints_enabled = true;

    // Spatial index behind find_hint(), keyed on data_generation and built
    // lazily. Lives here rather than beside DataRenderer's caches because
    // hit-testing is panel work, though the invalidation rule is identical.
    HintIndexCache hint_index;

    char title_buf[256]{};
    char xtitle_buf[128]{};
    char ytitle_buf[128]{};
    bool   grid_local  = false;
    bool   xauto_local = true, yauto_local = true;
    double xmin_local = 0, xmax_local = 1, ymin_local = 0, ymax_local = 1;
    std::vector<Tick> xticks_scratch, yticks_scratch;

    // Cosmetics sections (spine/tick/label/title colors+sizes+thickness, and
    // the grid/legend/colorbar option structs). Color widgets bind directly
    // to &<field>.r since Color is already a plain {r,g,b,a} float struct.
    AxesStyle       axes_style_local;
    GridOptions     grid_opts_local;
    bool            legend_enabled_local = false;
    LegendOptions   legend_local;
    ColorbarOptions colorbar_local;

    // Forces a re-sync of every *_local/*_buf/*_scratch field above from
    // the current RenderSnapshot whenever the selected axes slot changes
    // (including the very first frame, since -1 never matches a real slot).
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
    // bar and Controls column occupy.
    std::atomic<int> pending_plot_w{0};
    std::atomic<int> pending_plot_h{0};

    // Live physical-pixel size the plot was most recently rendered at. The one
    // exception to this struct's render-thread-only contract: savefig_png()/
    // savefig_svg() read these from an arbitrary caller thread to default
    // their save size to whatever the window currently shows.
    std::atomic<int> live_plot_w{0};
    std::atomic<int> live_plot_h{0};
};

} // namespace sextant
