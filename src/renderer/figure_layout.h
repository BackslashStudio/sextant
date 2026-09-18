#pragma once
#include "plot_rect.h"
#include "../coord_transform.h"
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include "../tick.h"
#include "sextant/style.h"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sextant {
    // The figure's whole layout, computed per frame from the snapshot and target
    // size; shared by the raster (render_frame.cpp) and SVG (figure_export.cpp)
    // paths. Decoration space is measured from its text (text_metrics.h), so text
    // position and reserved space come from the same numbers. Margins are the gap
    // between the figure edge and the whole grid (FigureMargins).

    // ---------------------------------------------------------------------------
    // Which series get a legend key, in order (determines box size and contents).

    enum class LegendKind { Line, Marker, Bar };

    struct LegendEntry {
        Color color;
        std::string name;
        LegendKind kind = LegendKind::Line;
        LineStyle style = LineStyle::Solid; // Line entries only
        // Marker entries only: the key's shape.
        MarkerStyle marker = MarkerStyle::Circle;

        // Marker entries only; alpha 0 (default) = no edge. scatter_z keys are
        // white with a black edge. Edged Cross/Plus are stroked in `edge`.
        Color edge = {0.0f, 0.0f, 0.0f, 0.0f};

        // Line entries only: draw the swatch with `cmap` swept along it (the key
        // for a colormapped path) instead of a flat stroke in `color`.
        bool swept = false;
        Colormap cmap = Colormap::Viridis;
    };

    std::vector<LegendEntry> collect_legend_entries(const RenderSnapshot& snap);

    // 3D: the axes' own keys, then each plane's, in plane order.
    std::vector<LegendEntry> collect_legend_entries(const RenderSnapshot3D& snap);

    // Legend box metrics, shared by sizing and both renderers.
    inline constexpr float kLegendPad = 8.0f;
    inline constexpr float kLegendSwatchW = 20.0f;
    inline constexpr float kLegendGap = 6.0f;
    // Gap between entries on one row (Outside top/bottom anchors).
    inline constexpr float kLegendColGap = 12.0f;

    inline float legend_row_height(float fontsize) { return fontsize + 8.0f; }

    // ---------------------------------------------------------------------------
    // Spacing constants
    // ---------------------------------------------------------------------------
    // Gap between a tick mark and its label.
    inline constexpr float kTickLabelGap = 4.0f;
    // Gap between a title and what it titles (frame, or tick labels).
    inline constexpr float kTitleGap = 6.0f;
    // Gap between a colorbar and its numbers, and between the numbers and its name.
    inline constexpr float kColorbarLabelGap = 5.0f;
    // Minimum plot frame size, so the frame never collapses or inverts.
    inline constexpr float kMinFrameSize = 4.0f;

    // ---------------------------------------------------------------------------
    // Insets
    // ---------------------------------------------------------------------------
    // Space on each side of a rectangle.
    struct PlotInsets {
        float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    };

    // ---------------------------------------------------------------------------
    // Per-cell decorations, and the inverse layout
    // ---------------------------------------------------------------------------
    // A cell's legend and colorbar measurements, independent of figure size, so
    // forward and inverse layout use the same numbers.
    struct ColorbarSpec {
        Colormap cmap = Colormap::Viridis;
        float vmin = 0.0f, vmax = 1.0f;
        std::string name;
        // Total thickness: margin + width + number gap + numbers' extent, plus
        // gap + line height when named.
        float block = 0.0f;
    };

    struct CellDecorations {
        // One per request, in find_colorbar_requests() order (outward).
        std::vector<ColorbarSpec> colorbars;
        float colorbar_block = 0.0f; // the sum of their blocks

        std::vector<LegendEntry> legend_entries;
        // Each entry's swatch + gap + text width, for row layout.
        std::vector<float> legend_entry_w;
        // The box as one column (the shape at every anchor except Outside
        // top/bottom, which wrap; see layout_legend_rows()).
        float legend_box_w = 0.0f, legend_box_h = 0.0f;
        // margin + legend_box_w: what a side-anchored legend reserves.
        float legend_block = 0.0f;
    };

    // A legend laid out in rows no wider than `max_w` (at least one entry per
    // row); swatch positions are relative to the box's top-left.
    struct LegendSlot {
        float x = 0.0f, cy = 0.0f;
    }; // swatch left, row centre
    struct LegendRows {
        float w = 0.0f, h = 0.0f;
        std::vector<LegendSlot> slots;
    };

    LegendRows layout_legend_rows(const CellDecorations& dec, float fontsize, float max_w);

    CellDecorations compute_cell_decorations(const RenderSnapshot& snap);

    // 3D: legend and colorbars are placed beside the cell's frame, as in 2D.
    CellDecorations compute_cell_decorations(const RenderSnapshot3D& snap);

    // ---------------------------------------------------------------------------
    // Measurements: the stored half of the layout
    // ---------------------------------------------------------------------------
    // Everything measured that doesn't depend on the figure size. The window
    // re-measures only on events (see LayoutStore), so panning doesn't move the
    // frame. Limits, ticks and transforms are always resolved from the snapshot.
    enum class DecorSide { None, Left, Right, Top, Bottom };

    struct CellMeasure {
        AxesSlot slot;
        bool is3d = false;
        PlotInsets axis; // furniture around the frame; zero for 3D
        float title = 0.0f; // band above everything else
        float frame_margin = 0.0f;
        CellDecorations dec;
        DecorSide legend = DecorSide::None; // None also for an Inside legend
        DecorSide bars = DecorSide::Right;
        float legend_margin = 0.0f;
        float legend_fontsize = 10.0f;
    };

    // Grid-aligned measurements: 2D axis furniture per column (left/right) and row
    // (top/bottom), and the title band per row. See solve_grid().
    struct GridMeasure {
        int rows = 1, cols = 1;
        std::vector<float> ax_l, ax_r, ax_t, ax_b;
        std::vector<float> title;
    };

    struct FigureMeasure {
        std::vector<CellMeasure> cells; // index-parallel with FigureSnapshot::axes
        GridMeasure grid;
        float suptitle_band = 0.0f;
    };

    // Measures `fsnap`. With `frozen`, 2D cells in matching slots keep `frozen`'s
    // axis furniture (so an export while a window is open matches the screen).
    FigureMeasure measure_figure(const FigureSnapshot& fsnap, const FigureMeasure* frozen = nullptr);

    // Whether `m` fits `fsnap`'s grid (same slots, kinds, legend entry and
    // colorbar counts). Layout measures afresh when it doesn't.
    bool measure_fits(const FigureMeasure& m, const FigureSnapshot& fsnap);

    // ---------------------------------------------------------------------------
    // Grid weights
    // ---------------------------------------------------------------------------
    // `ratios` if it has one finite positive entry per track, else equal weights.
    std::vector<float> grid_weights(const std::vector<float>& ratios, int n);

    // Column and row positions in figure pixels, after margins, suptitle band and
    // gaps, shared by weight. A span includes its inner gaps. Also used for the
    // boundary drag hit-test.
    struct GridTracks {
        std::vector<float> col_x, col_w;
        std::vector<float> row_y, row_h;
    };

    GridTracks grid_tracks(const FigureSnapshot& fsnap, float suptitle_band, int fig_w, int fig_h);

    struct LayoutSize {
        float width = 0.0f, height = 0.0f;
    };

    // Inverse of compute_figure_layout(): the figure size at which slot
    // `slot_index`'s frame is `frame_w` x `frame_h`. Closed-form (reservations
    // don't depend on figure size); only integer rounding keeps it from exact.
    LayoutSize figure_size_for_frame(const FigureSnapshot& fsnap, int slot_index,
                                     float frame_w, float frame_h);

    // The same, from stored measurements.
    LayoutSize figure_size_for_frame(const FigureSnapshot& fsnap, const FigureMeasure& measure,
                                     int slot_index, float frame_w, float frame_h);

    // ---------------------------------------------------------------------------
    // Per-cell and per-figure layout
    // ---------------------------------------------------------------------------
    // Text anchors are line centres (NVG_ALIGN_MIDDLE); SVG adds
    // middle_baseline_offset().
    //
    // A 3D cell's replacement for the CoordTransform and tick lists; other
    // CellLayout fields mean the same in both kinds.
    struct Box3DLayout {
        Projector3D proj;
        std::vector<Tick> xticks, yticks, zticks;
    };

    // One colorbar's placement and scale; cosmetics are the cell's ColorbarOptions.
    struct ColorbarBox {
        PlotRect rect{};
        Colormap cmap = Colormap::Viridis;
        float vmin = 0.0f, vmax = 1.0f;

        // Top/Bottom bars: vmin at the left. Left/Right: vmin at the bottom.
        // Positions include the offset.
        bool horizontal = false;

        // Number anchors (line centres); `num_align` is Left beside a Right bar,
        // Right beside a Left one, Center for horizontal bars.
        float vmin_x = 0.0f, vmin_y = 0.0f;
        float vmax_x = 0.0f, vmax_y = 0.0f;
        HAlign num_align = HAlign::Left;

        // The bar's name and its line centre, in the unrotated frame for a
        // vertical bar (the rotation point). Empty: no text, nothing reserved.
        std::string name;
        float name_x = 0.0f, name_y = 0.0f;
    };

    struct CellLayout {
        AxesSlot slot;
        PlotRect cell; // the whole subplot: the grid's share, before insets
        PlotRect frame; // the data area, i.e. what the spine outlines
        CoordTransform tr;
        std::vector<Tick> xticks, yticks;

        // 3D cells only (`tr`, tick lists and x/y title anchors are 2D only).
        std::optional<Box3DLayout> box3d;
        bool is_3d() const { return box3d.has_value(); }

        // Frame + axis furniture + frame_margin: what outside legends and
        // colorbars are placed against.
        PlotRect extended{};

        // Cell edge to frame: grid-aligned furniture and title band, plus this
        // cell's own frame_margin, legend and colorbars.
        PlotInsets reserved{};

        // As drawn (offset included); w <= 0 = absent. `legend_slots` parallels
        // the entries: swatch left edge and row centre.
        PlotRect legend{};
        std::vector<LegendEntry> legend_entries;
        std::vector<LegendSlot> legend_slots;

        // One per request, outward from the extended frame; empty = none.
        std::vector<ColorbarBox> colorbars;

        // Title anchors. The axes title is at the cell top; x/y titles at the
        // outer edge of the grid-aligned furniture, so titles line up.
        float title_x = 0.0f, title_y = 0.0f;
        float xtitle_x = 0.0f, xtitle_y = 0.0f;
        float ytitle_x = 0.0f, ytitle_y = 0.0f; // centre of the rotated line

        // Axis line positions (`xaxis_y`, `yaxis_x`) and tick directions (+1 low
        // side, -1 high side). `*_interior`: the line isn't on a frame edge and
        // needs its own stroke (resolved after clamping).
        float xaxis_y = 0.0f, yaxis_x = 0.0f;
        float xtick_dir = 1.0f, ytick_dir = -1.0f;
        bool xaxis_interior = false, yaxis_interior = false;

        // x labels hang from xlabel_top (NVG_ALIGN_TOP); y labels are centred on
        // the tick at ylabel_x, aligned by ylabel_align (facing their axis).
        float xlabel_top = 0.0f;
        float ylabel_x = 0.0f;
        HAlign ylabel_align = HAlign::Right;

        bool has_legend() const { return legend.w > 0.0f; }
        bool has_colorbar() const { return !colorbars.empty(); }
    };

    struct FigureLayout {
        std::vector<CellLayout> cells; // index-parallel with FigureSnapshot::axes
        float suptitle_band = 0.0f;
    };

    // A fresh measure, laid out at fig_w x fig_h; `out_measure` returns it too.
    FigureLayout compute_figure_layout(const FigureSnapshot& fsnap, int fig_w, int fig_h,
                                       FigureMeasure* out_measure = nullptr);

    // Stored measurements laid out at fig_w x fig_h (no text measured). A measure
    // that doesn't fit is ignored and a fresh one taken.
    FigureLayout compute_figure_layout(const FigureSnapshot& fsnap, const FigureMeasure& measure,
                                       int fig_w, int fig_h);

    // ---------------------------------------------------------------------------
    // The window's stored layout
    // ---------------------------------------------------------------------------
    // Owned by the render thread. Re-measures when layout_generation or the size
    // changes, or a refit was requested; otherwise lays out from what it holds.
    class LayoutStore {
    public:
        // Render thread: this frame's layout.
        FigureLayout fit(const FigureSnapshot& fsnap, int fig_w, int fig_h);

        // Render thread: the next fit() re-measures.
        void request_refit() { refit_ = true; }

        // Any thread: the measure in use (null before the first frame). Immutable.
        std::shared_ptr<const FigureMeasure> load() const;

    private:
        mutable std::mutex mutex_;
        std::shared_ptr<const FigureMeasure> measure_;
        unsigned long long generation_ = 0;
        int w_ = 0, h_ = 0;
        bool refit_ = false;
    };

    // ---------------------------------------------------------------------------
    // Baseline conversions
    // ---------------------------------------------------------------------------
    // Offsets from NanoVG's alignment point to SVG's baseline, from fontstash's
    // vertical metrics.
    float middle_baseline_offset(const std::string& font_path, float fontsize);

    float top_baseline_offset(const std::string& font_path, float fontsize);
} // namespace sextant
