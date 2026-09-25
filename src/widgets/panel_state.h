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
    // Render-thread-only UI state for the widget panel, used only inside
    // draw_widget_panel() (exceptions are marked below).
    struct PanelState {
        int selected_slot_index = 1;

        // Show/hide for the "Cosmetic" dock (View menu). layout_cosmetic_visible
        // records what the dockspace was built for, so ensure_layout() rebuilds
        // only on a real toggle.
        bool cosmetic_visible = true;
        bool layout_cosmetic_visible = true;

        // The Data panel shares Cosmetic's dock node as a second tab; its own
        // layout mirror gates ensure_layout(). On by default.
        bool data_visible = true;
        bool layout_data_visible = true;

        // Which panel owns the shared tab bar after a rebuild (a rebuilt node
        // otherwise selects the last tab).
        bool focus_data_on_rebuild = false;

        // The panel ("Cosmetic" or "Data") to focus after a rebuild, once both are
        // drawn (the last-drawn window otherwise wins). A string literal or null.
        const char* pending_panel_focus = nullptr;

        // Display format for Data-panel cells.
        ValueFormat value_format;

        // Tint Data-panel cells by their column's min/max (see cell_shading.h),
        // with the cached ranges.
        bool shade_cells = true;
        CellShadingCache cell_shading;

        // First matrix column shown when a grid is wider than one table (see
        // kMaxGridCols); shared by all grids and clamped every frame.
        int grid_col_offset = 0;

        // Which bar3d matrix the grid cells edit: false = heights, true = per-bar
        // bases (offered only when the plot has bases).
        bool bar3d_show_bases = false;

        // Pan/zoom: left-drag pans and scroll zooms the selected slot only; a drag
        // must start on it and continues if the cursor leaves.
        bool navigate_enabled = false;

        // Click-to-select: the slot under the press (-1 = none), cleared on
        // release. Selection changes on release of a click that stayed in the cell.
        int press_slot = -1;
        // Whether the press was on the already-selected cell (only those navigate).
        bool press_on_selected = false;
        // Set by a selecting release, so the double-click completing it doesn't
        // also reset that cell's view.
        bool selected_by_last_click = false;

        // Dragging a grid boundary. Captured at the press (`cols`: between columns;
        // `k`: the track after it), so each frame is computed from the press state.
        struct GridDrag {
            bool active = false;
            bool cols = true;
            int k = 0;
            float press = 0.0f;
            std::vector<float> w0;
            float len_a = 0.0f, len_b = 0.0f;
            float min_a = 0.0f, min_b = 0.0f;
            float last_a = -1.0f; // the split last pushed, so a still cursor pushes nothing
        };

        GridDrag grid_drag;

        // The hover tooltip toggle (Cosmetic panel's "Hints").
        bool hints_enabled = true;

        // Spatial index for find_hint(), keyed on data_generation.
        HintIndexCache hint_index;

        char title_buf[256]{};
        char xtitle_buf[128]{};
        char ytitle_buf[128]{};

        // Scratch buffer for the one name field the Data panel draws per frame;
        // text_field() re-seeds it while inactive.
        char name_buf[128]{};
        bool grid_local = false;
        bool xauto_local = true, yauto_local = true;
        double xmin_local = 0, xmax_local = 1, ymin_local = 0, ymax_local = 1;
        // The snapshot limit_stamps the limits were seeded from; an axis whose
        // stamp changes was set by the program, so it is re-seeded.
        LimitStamps limit_stamps_local;
        std::vector<Tick> xticks_scratch, yticks_scratch;

        // Live values for the optional origin_x/origin_y drag boxes; also restores
        // the previous number when a pin is re-ticked.
        double origin_x_scratch = 0.0, origin_y_scratch = 0.0;
        // z is kept with x/y (one control group in the panel).
        double origin_z_scratch = 0.0;

        // Cosmetics sections. Color widgets bind to &<field>.r.
        AxesStyle axes_style_local;
        GridOptions grid_opts_local;
        bool legend_enabled_local = false;
        LegendOptions legend_local;
        ColorbarOptions colorbar_local;

        // 3D-only scratch (third axis, camera, box); shared fields above are reused
        // and re-seeded via last_synced_slot.
        char ztitle_buf[128]{};
        double zmin_local = 0, zmax_local = 1;
        bool zauto_local = true;
        std::vector<Tick> zticks_scratch;
        Camera3D camera_local;
        // The snapshot camera_stamp camera_local was seeded from; a new one means
        // the program set the camera, so camera_local is re-seeded.
        unsigned long long camera_stamp_local = 0;
        Box3DStyle box3d_local;
        BoxAspect aspect_local;

        // One plane's Data-panel tab. Scratch copies keep drag values stable across
        // a gesture; positional, re-seeded on slot or plane-count change.
        struct PlaneUi {
            PlaneOrientation orient = PlaneOrientation::XY;
            double offset = 0.0;
            Plane2DOptions opts;
        };

        std::vector<PlaneUi> planes_local;

        // The 3D kinds' appearance, scratch for the same reason; re-seeded in
        // sync_selected_slot() on slot or count change.
        std::vector<Bar3DOptions> bars3d_local;
        std::vector<SurfaceOptions> surfaces_local;
        std::vector<Scatter3DOptions> scatter3d_local;
        std::vector<Line3DOptions> line3d_local;
        std::vector<SurfaceTriOptions> surface_tri_local;

        // The 2D kinds' appearance, for a 2D axes' own sheet and for each plane's
        // sheet in 3D; same rule. hint_labels are dropped (an edit keeps the
        // object's own).
        struct SheetStyles {
            std::vector<LineOptions> lines;
            std::vector<ScatterOptions> scatters;
            std::vector<BarOptions> bars;
            std::vector<HeatmapOptions> heatmaps;
            std::vector<ScatterZOptions> scatter_z;
        };

        SheetStyles sheet_local;
        std::vector<SheetStyles> plane_sheets_local;

        // The value under a bar-width drag while it is active (the snapshot lags
        // the op by a frame); re-read from the snapshot otherwise. One suffices,
        // since only one drag is active at a time.
        double width_held = 0.0;

        // Forces a re-sync of the *_local/*_buf/*_scratch fields whenever the
        // selected slot changes (-1 = first frame).
        int last_synced_slot = -1;

        // Figure-level, seeded once on the first frame (not per slot).
        char suptitle_buf[256]{};
        SuptitleOptions suptitle_local;
        bool suptitle_synced = false;

        // Layout controls: figure-level, seeded once (re-seeding every frame would
        // fight a drag).
        FigureMargins margins_local;
        float col_gap_local = 0.0f;
        float row_gap_local = 0.0f;
        bool layout_synced = false;

        // "File > Save" dialog. save_width/save_height <= 0 = the Plot panel's live
        // size.
        bool save_dialog_open = false;
        char save_path_buf[260] = "figure.png";
        int save_width = 0;
        int save_height = 0;
        bool save_requested = false;

        // Whether the save size is the whole figure or the selected plot frame
        // (then converted via figure_size_for_frame()).
        enum class SizeMode { Figure, PlotFrame };

        SizeMode save_size_mode = SizeMode::Figure;

        // Export bounds (0 = automatic): SvgExportOptions::max_splits and
        // PngExportOptions::peel_layers. Only the current format's field is shown.
        int save_max_splits = 0;
        int save_peel_layers = 0;

        // Warning from the last save; non-empty opens a modal (cleared by it).
        std::string save_warning;
        bool save_warning_open = false;

        // "File > Resize to plot frame" dialog; applied via pending_plot_w/h.
        bool resize_dialog_open = false;
        int resize_frame_w = 0;
        int resize_frame_h = 0;

        // Requested plot size in logical pixels (> 0 = pending). Written from any
        // thread (Figure::resize()) or the dialog; applied in draw_widget_panel().
        std::atomic<int> pending_plot_w{0};
        std::atomic<int> pending_plot_h{0};

        // The plot's last laid-out size in logical pixels -- what the layout,
        // the panels and a save see. Exception: read from any thread by
        // savefig_png()/savefig_svg().
        std::atomic<int> live_plot_w{0};
        std::atomic<int> live_plot_h{0};

        // The same plot in framebuffer pixels (logical x display scale), for
        // the one thing that has to add window chrome to it: a resize.
        std::atomic<int> live_plot_fb_w{0};
        std::atomic<int> live_plot_fb_h{0};

        // The window's stored layout. fit() is render-thread only; load() is read
        // from any thread (savefig(), size_for_frame()) and by all panels.
        LayoutStore layout;

        // What each slot's auto limits resolved to in the last drawn frame (the
        // snapshot only has the declared limits). Stored by draw_plot_panel().
        struct ResolvedLimits {
            int slot = 0;
            bool is_3d = false;
            double xmin = 0.0, xmax = 1.0;
            double ymin = 0.0, ymax = 1.0;
            double zmin = 0.0, zmax = 1.0; // 3D only
        };

        std::vector<ResolvedLimits> resolved;

        const ResolvedLimits* resolved_for(int slot) const {
            for (const ResolvedLimits& r: resolved)
                if (r.slot == slot) return &r;
            return nullptr;
        }
    };
} // namespace sextant
