#pragma once
#include "sextant/style.h"
#include "sextant/axes3d.h"
#include "../hint_index.h"
#include "../plot_data_view.h"
#include "../plot_events.h"
#include "cell_shading.h"
#include "../tick.h"
#include "../renderer/figure_layout.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sextant {
    class EventChannel;

    // Render-thread-only UI state for the window's shell and the kit components,
    // used only inside a frame (exceptions are marked below). Split by owner
    // (GUI-kit R2): one struct per component or shell, none referring to
    // another, so a host can own each one; the library window aggregates them
    // in FigureWindowState.
    //
    // Shared per figure (see FigureContext): Selection and SlotViewState. Each
    // state seeded from the selected slot remembers the Selection generation it
    // last synced and re-seeds itself when that moves (GUI-kit R3), so no code
    // has to know which components exist.
    //
    // No access crosses owners any more. Settled in R3: the plot view writing
    // the camera/limit scratch (now the shared SlotViewState), Cosmetic reading
    // PlotViewState (now through FigureContext::view), the shared re-seed (now
    // per-state pulls). Settled in R4: the dialogs read Selection and the live
    // size through FigureContext and return requests the shell performs; the
    // menu, shell code, writes the plot view's own settings (navigate/hints)
    // and asks it to refit, as a host does.

    // A synced generation no Selection has: the first pull always seeds.
    inline constexpr std::uint64_t kNeverSynced = ~std::uint64_t{0};

    // Which subplot the panels address; shared by every component.
    struct Selection {
        int slot = 1;
        // Bumped by every real change of `slot` (select(), or normalization to an
        // existing slot); the states seeded from the slot compare against it.
        std::uint64_t generation = 0;

        void select(int s) {
            if (s == slot) return;
            slot = s;
            ++generation;
        }
    };

    // The selected slot's view, written by both the plot view (pan/zoom/orbit)
    // and the Cosmetic inspector (the Limits and camera fields). Reached only
    // through slot_view(FigureContext&), which re-seeds it first when the
    // selection has moved -- whoever touches it, drawn panels or not -- and
    // otherwise follows limits and cameras the program set since.
    struct SlotViewState {
        bool xauto_local = true, yauto_local = true, zauto_local = true;
        double xmin_local = 0, xmax_local = 1, ymin_local = 0, ymax_local = 1;
        double zmin_local = 0, zmax_local = 1;
        // The snapshot limit_stamps the limits were seeded from; an axis whose
        // stamp changes was set by the program, so it is re-seeded.
        LimitStamps limit_stamps_local;
        Camera3D camera_local;
        // The snapshot camera_stamp camera_local was seeded from; a new one means
        // the program set the camera, so camera_local is re-seeded.
        unsigned long long camera_stamp_local = 0;
        std::uint64_t synced_generation = kNeverSynced;
    };

    // The library window's own chrome: docks, focus, pending resize.
    struct ShellState {
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

        // Requested plot size in logical pixels (> 0 = pending). Written from any
        // thread (Figure::resize()) or the dialog; applied by
        // FigureWindowShell::frame().
        std::atomic<int> pending_plot_w{0};
        std::atomic<int> pending_plot_h{0};
    };

    // The plot view: navigation, selection gesture, hints, events, layout.
    struct PlotViewState {
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

        // Where the plot's mouse/scroll/key/resize events go (Figure::connect()).
        // Borrowed from the Figure, which outlives the window thread; null in a
        // test's bare panel. The tracker is what turns frames of input into events.
        EventChannel* events = nullptr;
        PlotEventTracker event_tracker;

        // The hover tooltip toggle (Cosmetic panel's "Hints").
        bool hints_enabled = true;

        // Spatial index for find_hint(), keyed on data_generation.
        HintIndexCache hint_index;

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
        // from any thread (savefig(), size_for_frame()); the panels read it
        // through PlotViewInfo::measure.
        LayoutStore layout;
    };

    // What one slot's auto limits resolved to in a drawn frame (the snapshot only
    // has the declared limits).
    struct ResolvedLimits {
        int slot = 0;
        bool is_3d = false;
        double xmin = 0.0, xmax = 1.0;
        double ymin = 0.0, ymax = 1.0;
        double zmin = 0.0, zmax = 1.0; // 3D only
    };

    // What the plot view learned drawing a frame, returned by PlotView::draw()
    // (GUI-kit R6) and read by the other components through FigureContext::view:
    // only a drawn frame knows the live size and the resolved auto limits.
    struct PlotViewInfo {
        // The plot's laid-out size in logical pixels; 0 before the first frame.
        int plot_w = 0;
        int plot_h = 0;

        // The view's stored measurements (PlotViewState::layout); null before
        // its first fit.
        std::shared_ptr<const FigureMeasure> measure;

        // One per slot drawn.
        std::vector<ResolvedLimits> resolved;

        const ResolvedLimits* resolved_for(int slot) const {
            for (const ResolvedLimits& r: resolved)
                if (r.slot == slot) return &r;
            return nullptr;
        }
    };

    // The Cosmetic inspector's scratch: what its widgets bind to between edits.
    struct CosmeticState {
        char title_buf[256]{};
        char xtitle_buf[128]{};
        char ytitle_buf[128]{};

        bool grid_local = false;
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

        // 3D-only scratch (third axis, box); the shared fields above are reused.
        // The limits and camera are in SlotViewState.
        char ztitle_buf[128]{};
        std::vector<Tick> zticks_scratch;
        Box3DStyle box3d_local;
        BoxAspect aspect_local;

        // Figure-level, seeded once on the first frame (not per slot).
        char suptitle_buf[256]{};
        SuptitleOptions suptitle_local;
        bool suptitle_synced = false;

        // Layout controls: figure-level, seeded once (re-seeding every frame would
        // fight a drag).
        FigureMargins margins_local;
        Color background_local = {0.93f, 0.93f, 0.93f, 1.0f};
        float col_gap_local = 0.0f;
        float row_gap_local = 0.0f;
        bool layout_synced = false;

        // The Selection generation the per-slot fields above were seeded at.
        std::uint64_t synced_generation = kNeverSynced;
    };

    // The Data inspector: cell display, and the per-object appearance scratch its
    // tabs edit (re-seeded by pull_data_panel() in panel.cpp).
    struct DataPanelState {
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

        // Scratch buffer for the one name field the Data panel draws per frame;
        // text_field() re-seeds it while inactive.
        char name_buf[128]{};

        // One plane's Data-panel tab. Scratch copies keep drag values stable across
        // a gesture; positional, re-seeded on a slot change or when the planes
        // (their ids, in order) are no longer the ones seeded from.
        struct PlaneUi {
            PlaneOrientation orient = PlaneOrientation::XY;
            double offset = 0.0;
            Plane2DOptions opts;
        };

        std::vector<PlaneUi> planes_local;
        std::vector<ObjectId> plane_ids;   // what planes_local was seeded from

        // The 3D kinds' appearance, scratch for the same reason; re-seeded in
        // pull_data_panel() on the same rule (a remove and an add leave the
        // count alone but not the ids).
        std::vector<Bar3DOptions> bars3d_local;
        std::vector<SurfaceOptions> surfaces_local;
        std::vector<Scatter3DOptions> scatter3d_local;
        std::vector<Line3DOptions> line3d_local;
        std::vector<SurfaceTriOptions> surface_tri_local;
        std::vector<ObjectId> scene_ids;   // seeded from, kind by kind

        // The 2D kinds' appearance, for a 2D axes' own sheet and for each plane's
        // sheet in 3D; same rule. hint_labels are dropped (an edit keeps the
        // object's own).
        struct SheetStyles {
            std::vector<LineOptions> lines;
            std::vector<ScatterOptions> scatters;
            std::vector<BarOptions> bars;
            std::vector<HeatmapOptions> heatmaps;
            std::vector<ScatterZOptions> scatter_z;
            std::vector<ObjectId> ids;   // seeded from, kind by kind
        };

        SheetStyles sheet_local;
        std::vector<SheetStyles> plane_sheets_local;

        // The value under a bar-width drag while it is active (the snapshot lags
        // the op by a frame); re-read from the snapshot otherwise. One suffices,
        // since only one drag is active at a time.
        double width_held = 0.0;

        // The Selection generation the per-object scratch was seeded at; changed
        // objects re-seed on their own, every frame the panel draws.
        std::uint64_t synced_generation = kNeverSynced;
    };

    // "File > Save" dialog. width/height <= 0 = the Plot panel's live size.
    struct SaveDialogState {
        bool open = false;
        char path_buf[260] = "figure.png";
        int width = 0;
        int height = 0;

        // Whether the save size is the whole figure or the selected plot frame
        // (then converted via figure_size_for_frame()).
        enum class SizeMode { Figure, PlotFrame };

        SizeMode size_mode = SizeMode::Figure;

        // Export bounds (0 = automatic): SvgExportOptions::max_splits and
        // PngExportOptions::peel_layers. Only the current format's field is shown.
        int max_splits = 0;
        int peel_layers = 0;

        // Warning from the last save; non-empty opens its window (cleared by OK).
        // failed: the file was not written and warning says why.
        std::string warning;
        bool failed = false;
    };

    // "File > Resize to plot frame" dialog; its request goes to
    // ShellState::pending_plot_w/h.
    struct ResizeDialogState {
        bool open = false;
        int frame_w = 0;
        int frame_h = 0;
    };

    // The library window's state (GUI-kit R7; was PanelState): its shell's own
    // plus that of the components it hosts, for the one figure it shows. Lives in
    // Figure::Impl, not in FigureWindowShell: other threads read its atomics and
    // LayoutStore (Figure::resize(), savefig()), and it outlives a window (a
    // second show() keeps the selection and the inspectors' scratch).
    struct FigureWindowState {
        Selection         selection;
        SlotViewState     slot_view;
        ShellState        shell;
        PlotViewState     plot;
        CosmeticState     cosmetic;
        DataPanelState    data;
        SaveDialogState   save;
        ResizeDialogState resize;

        // The plot view's output from its last frame, kept by the shell for
        // FigureContext::view (the menu, drawn before the view, sees the
        // previous frame's).
        PlotViewInfo      plot_info;

        // The figure's id for ImGui::PushID (FigureContext::figure_id); set once
        // by Figure::Impl.
        std::uint64_t     figure_id = 0;
    };
} // namespace sextant
