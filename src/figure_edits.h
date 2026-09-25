#pragma once
#include "sextant/style.h"
#include "plot_objects.h"
#include "tick.h"
#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace sextant {

// One scalar edited in the Data panel, addressed by (plane, kind, index,
// column, element). Column: 0 = x/centers, 1 = y/heights, 2 = z; unused for
// Heatmap, whose `element` is row*cols + col. `plane_index` -1 = the axes itself.
struct PlotCellEdit {
    PlotKind    kind;
    int         plot_index;
    int         column;
    std::size_t element;
    double      value;
    int         plane_index = -1;
    // The data_stamp of the plot as the panel showed it; the op is dropped if
    // the plot has been re-plotted or set_*_data()'d since. Every op has one.
    unsigned long long seen = ~0ull;
};

// Insert or remove one point, moving every parallel array of the plot (x/y/z,
// hint_labels, error bars) together. Not for heatmaps; see MatrixLineEdit.
struct PlotRowEdit {
    enum class Op { Insert, Remove };
    Op          op         = Op::Insert;
    PlotKind    kind       = PlotKind::Line;
    int         plot_index = 0;
    // Insert: index of the new point (old size to append); values copied from
    // row-1, or zero-filled at row 0. Remove: the index to erase.
    std::size_t row        = 0;
    int         plane_index = -1;   // see PlotCellEdit
    unsigned long long seen = ~0ull;   // see PlotCellEdit
};

// Appearance of one 2D plot object, from its Data-panel tab. Addressed like a
// PlotDataOp; the variant alternative is the kind. Carries the whole options
// struct.
struct PlotStyleEdit {
    int plot_index  = 0;
    int plane_index = -1;
    std::variant<LineOptions, ScatterOptions, BarOptions,
                 HeatmapOptions, ScatterZOptions> opts;
    // The `cleared` stamp of the axes or plane sheet it was made on (see
    // StyleStamps); filled in by FigureEditBox.
    unsigned long long seen = ~0ull;
};

// Insert or remove a whole row/column of a heatmap (or a bar3d/surface grid,
// where a row is a u line and a column a v line), re-striding the buffer and
// hint_labels. Never below 1x1. New lines copy their predecessor, or are
// zero-filled at index 0; grid kinds also get a new coordinate.
struct MatrixLineEdit {
    enum class Op   { Insert, Remove };
    enum class Axis { Row, Col };
    Op          op         = Op::Insert;
    Axis        axis       = Axis::Row;
    int         plot_index = 0;
    // Insert: index of the new line (rows/cols to append). Remove: index to erase.
    std::size_t index      = 0;
    int         plane_index = -1;   // see PlotCellEdit
    // Defaults to Heatmap.
    PlotKind    kind       = PlotKind::Heatmap;
    unsigned long long seen = ~0ull;   // see PlotCellEdit
};

// A plot's bar width (a per-plot scalar), journaled like other data. For
// Bar3D, `column` selects u_width or v_width.
struct BarWidthEdit {
    int    plot_index  = 0;
    double width       = 1.0;
    int    plane_index = -1;   // see PlotCellEdit
    PlotKind kind      = PlotKind::Bar;
    int    column      = 0;    // Bar3D: 0 = along u, 1 = along v
    unsigned long long seen = ~0ull;   // see PlotCellEdit
};

// One op in a plot's edit stream. Order matters: indices refer to the arrays
// as they were when recorded, so ops must replay in sequence.
using PlotDataOp = std::variant<PlotCellEdit, PlotRowEdit, MatrixLineEdit, BarWidthEdit>;

// The plane_index every op carries.
inline int plot_op_plane(const PlotDataOp& op) {
    return std::visit([](const auto& o) { return o.plane_index; }, op);
}

// Titles typed in the panel, each with the stamp it was typed over. The
// journal's per-slot record; AxesEdit/AxesEdit3D carry the same members.
struct TitleEdits {
    std::optional<std::string> title, xtitle, ytitle, ztitle;
    TitleStamps title_seen = TitleStamps::any();

    bool empty() const { return !title && !xtitle && !ytitle && !ztitle; }
};

// Latest value wins, per field, keeping each text with its own stamp.
template <typename E>
void merge_title_edits(TitleEdits& dst, const E& e) {
    auto one = [](std::optional<std::string>& text, unsigned long long& seen,
                  const std::optional<std::string>& v, unsigned long long v_seen) {
        if (v) { text = v; seen = v_seen; }
    };
    one(dst.title,  dst.title_seen.title,  e.title,  e.title_seen.title);
    one(dst.xtitle, dst.title_seen.xtitle, e.xtitle, e.title_seen.xtitle);
    one(dst.ytitle, dst.title_seen.ytitle, e.ytitle, e.title_seen.ytitle);
    if constexpr (requires { e.ztitle; })
        one(dst.ztitle, dst.title_seen.ztitle, e.ztitle, e.title_seen.ztitle);
}

// Applies each title `e` carries unless dst's was set after the snapshot it was
// typed over. For Axes::Impl, Axes3D::Impl and both snapshot kinds.
template <typename T, typename E>
void apply_title_edits(T& dst, const E& e) {
    auto one = [](std::string& text, unsigned long long have,
                  const std::optional<std::string>& v, unsigned long long seen) {
        if (v && have <= seen) text = *v;
    };
    one(dst.title,  dst.title_stamps.title,  e.title,  e.title_seen.title);
    one(dst.xtitle, dst.title_stamps.xtitle, e.xtitle, e.title_seen.xtitle);
    one(dst.ytitle, dst.title_stamps.ytitle, e.ytitle, e.title_seen.ytitle);
    if constexpr (requires { dst.ztitle; e.ztitle; })
        one(dst.ztitle, dst.title_stamps.ztitle, e.ztitle, e.title_seen.ztitle);
}

// Limits from pan/zoom or the Limits fields, per axis with the stamp they were
// made over. The journal's per-slot record, like TitleEdits; AxesEdit/AxesEdit3D
// carry the same members.
struct LimitEdits {
    std::optional<bool>   xlim_auto, ylim_auto, zlim_auto;
    std::optional<double> xmin, xmax, ymin, ymax, zmin, zmax;
    LimitStamps lim_seen = LimitStamps::any();

    bool empty() const {
        return !xlim_auto && !ylim_auto && !zlim_auto && !xmin && !xmax
               && !ymin && !ymax && !zmin && !zmax;
    }
};

namespace edits_detail {

// One axis of a limit edit: its auto flag and bounds.
template <typename B, typename D>
struct LimitAxis { B& autoscale; D& lo; D& hi; };

} // namespace edits_detail

// Latest value wins, per field; an axis takes the stamp of its latest edit.
template <typename E>
void merge_limit_edits(LimitEdits& dst, const E& e) {
    using edits_detail::LimitAxis;
    using OB = std::optional<bool>;
    using OD = std::optional<double>;
    auto one = [](LimitAxis<OB, OD> d, unsigned long long& seen,
                  LimitAxis<const OB, const OD> v, unsigned long long v_seen) {
        if (!v.autoscale && !v.lo && !v.hi) return;
        if (v.autoscale) d.autoscale = v.autoscale;
        if (v.lo) d.lo = v.lo;
        if (v.hi) d.hi = v.hi;
        seen = v_seen;
    };
    one({dst.xlim_auto, dst.xmin, dst.xmax}, dst.lim_seen.x,
        {e.xlim_auto, e.xmin, e.xmax}, e.lim_seen.x);
    one({dst.ylim_auto, dst.ymin, dst.ymax}, dst.lim_seen.y,
        {e.ylim_auto, e.ymin, e.ymax}, e.lim_seen.y);
    if constexpr (requires { e.zmin; })
        one({dst.zlim_auto, dst.zmin, dst.zmax}, dst.lim_seen.z,
            {e.zlim_auto, e.zmin, e.zmax}, e.lim_seen.z);
}

// Applies each axis `e` carries unless dst's was set after the snapshot it was
// made over, as apply_title_edits().
template <typename T, typename E>
void apply_limit_edits(T& dst, const E& e) {
    using edits_detail::LimitAxis;
    using OB = const std::optional<bool>;
    using OD = const std::optional<double>;
    auto one = [](LimitAxis<bool, double> d, unsigned long long have,
                  LimitAxis<OB, OD> v, unsigned long long seen) {
        if (have > seen) return;
        if (v.autoscale) d.autoscale = *v.autoscale;
        if (v.lo) d.lo = *v.lo;
        if (v.hi) d.hi = *v.hi;
    };
    one({dst.xlim_auto, dst.xmin, dst.xmax}, dst.limit_stamps.x,
        {e.xlim_auto, e.xmin, e.xmax}, e.lim_seen.x);
    one({dst.ylim_auto, dst.ymin, dst.ymax}, dst.limit_stamps.y,
        {e.ylim_auto, e.ymin, e.ymax}, e.lim_seen.y);
    if constexpr (requires { dst.zmin; e.zmin; })
        one({dst.zlim_auto, dst.zmin, dst.zmax}, dst.limit_stamps.z,
            {e.zlim_auto, e.zmin, e.zmax}, e.lim_seen.z);
}

// One axes slot's pending panel edits; absent = untouched. For the tick
// overrides, an empty inner vector means "revert to auto ticks".
struct AxesEdit {
    std::optional<std::string> title, xtitle, ytitle;
    // The stamps of the snapshot the titles were typed over (see TitleStamps).
    TitleStamps title_seen = TitleStamps::any();
    std::optional<bool>   xlim_auto, ylim_auto, grid_enabled;
    std::optional<double> xmin, xmax, ymin, ymax;
    // The stamps of the snapshot pan/zoom or the Limits fields worked over.
    LimitStamps lim_seen = LimitStamps::any();
    // The style stamps of the snapshot the panel drew; filled in by FigureEditBox.
    StyleStamps style_seen = StyleStamps::any();
    std::optional<std::vector<Tick>> xticks_override, yticks_override;
    std::optional<AxesStyle> axes_style;

    // Whole options structs; the panel republishes its local copy on change.
    std::optional<GridOptions>     grid_opts;
    std::optional<bool>            legend_enabled;
    std::optional<LegendOptions>   legend_opts;
    std::optional<ColorbarOptions> colorbar_opts;

    // Data panel ops, appended in order; empty = none since the last drain.
    std::vector<PlotDataOp> plot_ops;

    // Per-object appearance. Not journaled, unlike plot_ops.
    std::vector<PlotStyleEdit> plot_styles;
};

// The 3D counterpart of AxesEdit. plot_ops name their plane via plane_index.
struct AxesEdit3D {
    std::optional<std::string> title, xtitle, ytitle, ztitle;
    TitleStamps title_seen = TitleStamps::any();
    std::optional<bool>   xlim_auto, ylim_auto, zlim_auto, grid_enabled;
    std::optional<double> xmin, xmax, ymin, ymax, zmin, zmax;
    LimitStamps lim_seen = LimitStamps::any();
    std::optional<std::vector<Tick>> xticks_override, yticks_override, zticks_override;
    StyleStamps style_seen = StyleStamps::any();   // as in AxesEdit

    std::optional<AxesStyle>   axes_style;
    std::optional<GridOptions> grid_opts;

    // Same meaning as in AxesEdit.
    std::optional<bool>            legend_enabled;
    std::optional<LegendOptions>   legend_opts;
    std::optional<ColorbarOptions> colorbar_opts;

    // Camera edits go through here (not PanelState) so a dragged view survives
    // refresh().
    std::optional<Camera3D>   camera;
    // The camera_stamp of the snapshot it was navigated from (see TitleStamps).
    unsigned long long camera_seen = ~0ull;
    std::optional<Box3DStyle> box_style;
    std::optional<BoxAspect>  aspect;

    // Per-plane placement from the plane's Data-panel tab; entries naming a
    // plane that no longer exists are skipped.
    struct PlaneEdit {
        int plane_index = 0;
        std::optional<PlaneOrientation> orient;
        std::optional<double>           offset;
        // Whole options struct.
        std::optional<Plane2DOptions>   opts;
        // The plane's placement_stamp; filled in by FigureEditBox.
        unsigned long long seen = ~0ull;
    };
    std::vector<PlaneEdit> planes;

    // Appearance of the axes' own 3D objects, positional. apply_axes3d_edit()
    // preserves `hint_labels`, which are data edited elsewhere.
    // `seen` is the axes' `cleared` stamp, as PlotStyleEdit's.
    struct Bar3DEdit   { int plot_index = 0; std::optional<Bar3DOptions>   opts; unsigned long long seen = ~0ull; };
    struct SurfaceEdit { int plot_index = 0; std::optional<SurfaceOptions> opts; unsigned long long seen = ~0ull; };
    struct Scatter3DEdit { int plot_index = 0; std::optional<Scatter3DOptions> opts; unsigned long long seen = ~0ull; };
    struct Line3DEdit  { int plot_index = 0; std::optional<Line3DOptions>  opts; unsigned long long seen = ~0ull; };
    struct SurfaceTriEdit { int plot_index = 0; std::optional<SurfaceTriOptions> opts; unsigned long long seen = ~0ull; };
    std::vector<Bar3DEdit>     bars3d;
    std::vector<SurfaceEdit>   surfaces;
    std::vector<Scatter3DEdit> scatter3d;
    std::vector<Line3DEdit>    lines3d;
    std::vector<SurfaceTriEdit> surface_tri;

    // Data panel ops, appended in order.
    std::vector<PlotDataOp> plot_ops;

    // Appearance of plot objects on planes (always plane-addressed).
    std::vector<PlotStyleEdit> plot_styles;
};

// Pending edits for a whole figure. Per-axes entries are keyed by
// AxesSlot::index.
struct FigureEdits {
    std::vector<std::pair<int, AxesEdit>>   per_axes;
    std::vector<std::pair<int, AxesEdit3D>> per_axes3d;

    // Figure-level: the suptitle spans the whole grid.
    std::optional<std::string>     suptitle;
    std::optional<SuptitleOptions> suptitle_opts;

    // Figure-level layout.
    std::optional<FigureMargins> margins;
    std::optional<float>         col_gap, row_gap;

    // Grid weights from dragging or the Layout fields; journaled, so a drag
    // survives refresh().
    std::optional<std::vector<float>> col_ratios, row_ratios;

    // The figure stamps of the snapshot the panel drew; filled in by FigureEditBox.
    FigureStamps fig_seen = FigureStamps::any();

    // Must list every field above: a missing one is silently dropped when it
    // arrives without a per-axes edit.
    bool empty() const {
        return per_axes.empty() && per_axes3d.empty() && !suptitle && !suptitle_opts
               && !margins && !col_gap && !row_gap && !col_ratios && !row_ratios;
    }
};

// True if a drain holds only navigation (2D limits/ticks, 3D camera), which
// leaves the stored layout alone. An allow-list: new fields count as changes.
inline bool is_navigation_only(const FigureEdits& f) {
    if (f.suptitle || f.suptitle_opts || f.margins || f.col_gap || f.row_gap
        || f.col_ratios || f.row_ratios)
        return false;
    for (const auto& [idx, e] : f.per_axes) {
        if (e.title || e.xtitle || e.ytitle || e.grid_enabled || e.axes_style || e.grid_opts
            || e.legend_enabled || e.legend_opts || e.colorbar_opts
            || !e.plot_ops.empty() || !e.plot_styles.empty())
            return false;
    }
    for (const auto& [idx, e] : f.per_axes3d) {
        if (e.title || e.xtitle || e.ytitle || e.ztitle
            || e.xlim_auto || e.ylim_auto || e.zlim_auto || e.grid_enabled
            || e.xmin || e.xmax || e.ymin || e.ymax || e.zmin || e.zmax
            || e.xticks_override || e.yticks_override || e.zticks_override
            || e.axes_style || e.grid_opts || e.legend_enabled || e.legend_opts
            || e.colorbar_opts || e.box_style || e.aspect || !e.planes.empty()
            || !e.bars3d.empty() || !e.surfaces.empty() || !e.scatter3d.empty()
            || !e.lines3d.empty() || !e.surface_tri.empty()
            || !e.plot_ops.empty() || !e.plot_styles.empty())
            return false;
    }
    return true;
}

// Panel edits already applied to the published snapshot, for the caller thread
// to replay onto Axes::Impl and Figure::Impl: data ops in order, everything else
// as its latest value.
struct PlotDataJournal {
    std::vector<std::pair<int, std::vector<PlotDataOp>>> per_axes;

    // Titles as last typed, per slot (latest value only).
    std::vector<std::pair<int, TitleEdits>> titles;

    // Limits as last navigated or typed, per slot (latest value only).
    std::vector<std::pair<int, LimitEdits>> limits;

    // A 3D slot's camera as last navigated, with the stamp it was navigated from.
    struct CameraEdit { Camera3D camera; unsigned long long seen = ~0ull; };
    std::vector<std::pair<int, CameraEdit>> cameras;

    // Grid weights as last dragged (latest value only).
    std::optional<std::vector<float>> col_ratios, row_ratios;

    // Every other edit, latest value per field (merge_style_edits()): styles,
    // grid, legend, colorbar, ticks, plot appearance, and in 3D the box, aspect,
    // planes and objects. Titles, limits, camera and plot_ops stay empty here.
    std::vector<std::pair<int, AxesEdit>>   styles;
    std::vector<std::pair<int, AxesEdit3D>> styles3d;
    // Figure-level: suptitle, its style, margins and gaps (per_axes unused).
    FigureEdits figure;

    bool empty() const {
        return per_axes.empty() && titles.empty() && limits.empty() && cameras.empty()
               && !col_ratios && !row_ratios && styles.empty() && styles3d.empty()
               && figure.empty();
    }
};

// ---------------------------------------------------------------------------
// Applying data ops
//
// Templated over Axes::Impl (caller thread) and RenderSnapshot (render thread),
// which share member names, so a journaled op replays identically. Indices are
// re-validated and stale ops silently skipped (the panel indexed an older
// frame; the render thread cannot throw). Validate before mut() to avoid a
// needless clone.
// ---------------------------------------------------------------------------

namespace edits_detail {

// Writable access for both CowVec and plain vectors (opts.hint_labels).
template <class T> std::vector<T>& mut_ref(CowVec<T>& v)      { return v.mut(); }
template <class T> std::vector<T>& mut_ref(std::vector<T>& v) { return v; }

// False for an op recorded over an older copy of the plot (PlotCellEdit::seen).
template <class P, class E>
bool op_current(const P& p, const E& e) { return p.data_stamp <= e.seen; }

} // namespace edits_detail

template <class T>
void apply_plot_data_op(T& t, const PlotCellEdit& e) {
    auto put = [](CowVec<double>& v, std::size_t i, double value) {
        if (i < v.size()) v.mut()[i] = value;
    };
    if (e.plot_index < 0) return;
    const std::size_t pi = static_cast<std::size_t>(e.plot_index);
    switch (e.kind) {
        case PlotKind::Line:
            if (pi >= t.lines.size() || !edits_detail::op_current(t.lines[pi], e)) return;
            put(e.column == 0 ? t.lines[pi].x : t.lines[pi].y, e.element, e.value);
            return;
        case PlotKind::Scatter:
            if (pi >= t.scatters.size() || !edits_detail::op_current(t.scatters[pi], e)) return;
            put(e.column == 0 ? t.scatters[pi].x : t.scatters[pi].y, e.element, e.value);
            return;
        case PlotKind::Bar:
            if (pi >= t.bars.size() || !edits_detail::op_current(t.bars[pi], e)) return;
            put(e.column == 0 ? t.bars[pi].centers : t.bars[pi].heights, e.element, e.value);
            return;
        case PlotKind::ScatterZ: {
            if (pi >= t.scatter_z.size() || !edits_detail::op_current(t.scatter_z[pi], e)) return;
            auto& sp = t.scatter_z[pi];
            put(e.column == 0 ? sp.x : (e.column == 1 ? sp.y : sp.z), e.element, e.value);
            return;
        }
        case PlotKind::Heatmap: {
            if (pi >= t.heatmaps.size() || !edits_detail::op_current(t.heatmaps[pi], e)) return;
            auto& hp = t.heatmaps[pi];
            // Re-check the shape; it may have changed since the panel saw it.
            if (hp.rows <= 0 || hp.cols <= 0) return;
            const std::size_t n = static_cast<std::size_t>(hp.rows)
                                * static_cast<std::size_t>(hp.cols);
            if (e.element >= n || e.element >= hp.data.size()) return;
            hp.data.mut()[e.element] = static_cast<float>(e.value);
            return;
        }
        case PlotKind::Bar3D:
        case PlotKind::Surface:
        case PlotKind::Scatter3D:
        case PlotKind::Line3D:
        case PlotKind::SurfaceTri:
            // 3D-native; see apply_axes3d_data_op().
            return;
    }
}

template <class T>
void apply_plot_data_op(T& t, const BarWidthEdit& e) {
    if (e.kind != PlotKind::Bar || e.plot_index < 0) return;
    const std::size_t pi = static_cast<std::size_t>(e.plot_index);
    if (pi < t.bars.size() && edits_detail::op_current(t.bars[pi], e))
        t.bars[pi].bar_width = e.width;
}

namespace edits_detail {

// Insert at `row` (seeded from row-1 if `copy_prev`, else value-initialized) or
// erase at `row`. No-op when the index doesn't fit.
template <class V>
void row_insert(V& v, std::size_t row, bool copy_prev) {
    if (row > v.size()) return;
    auto& vv = mut_ref(v);
    typename V::value_type seed{};
    if (copy_prev && row > 0 && row - 1 < vv.size()) seed = vv[row - 1];
    vv.insert(vv.begin() + static_cast<std::ptrdiff_t>(row), seed);
}

template <class V>
void row_remove(V& v, std::size_t row) {
    if (row >= v.size()) return;
    auto& vv = mut_ref(v);
    vv.erase(vv.begin() + static_cast<std::ptrdiff_t>(row));
}

} // namespace edits_detail

// Adds or removes one data point.
template <class T>
void apply_plot_data_op(T& t, const PlotRowEdit& e) {
    if (e.plot_index < 0) return;
    const std::size_t pi = static_cast<std::size_t>(e.plot_index);
    const bool insert = (e.op == PlotRowEdit::Op::Insert);

    // Apply the change to all required parallel arrays; new points copy their
    // predecessor.
    auto required = [&](auto&... cols) {
        if (insert) (edits_detail::row_insert(cols, e.row, true), ...);
        else        (edits_detail::row_remove(cols, e.row), ...);
    };

    // Optional index-aligned arrays (hint_labels, error bars) move too; empty
    // ones are left alone. New points get a blank label but copy the
    // neighbour's error data.
    auto optional_cols = [&](bool copy_prev, auto&... cols) {
        auto one = [&](auto& c) {
            if (c.empty()) return;
            if (insert) edits_detail::row_insert(c, e.row, copy_prev);
            else        edits_detail::row_remove(c, e.row);
        };
        (one(cols), ...);
    };
    auto labels   = [&](auto& c)          { optional_cols(false, c); };
    auto err_cols = [&](ErrorBarData& err) {
        optional_cols(true, err.x_cap_lo, err.x_cap_hi, err.x_box_lo, err.x_box_hi,
                            err.y_cap_lo, err.y_cap_hi, err.y_box_lo, err.y_box_hi);
    };

    switch (e.kind) {
        case PlotKind::Line:
            if (pi < t.lines.size() && edits_detail::op_current(t.lines[pi], e)) {
                auto& lp = t.lines[pi];
                required(lp.x, lp.y);
                labels(lp.opts.hint_labels);
                err_cols(lp.err);
            }
            break;
        case PlotKind::Scatter:
            if (pi < t.scatters.size() && edits_detail::op_current(t.scatters[pi], e)) {
                auto& sp = t.scatters[pi];
                required(sp.x, sp.y);
                labels(sp.opts.hint_labels);
                err_cols(sp.err);
            }
            break;
        case PlotKind::Bar:
            if (pi < t.bars.size() && edits_detail::op_current(t.bars[pi], e)) {
                auto& bp = t.bars[pi];
                required(bp.centers, bp.heights);
                labels(bp.opts.hint_labels);
                err_cols(bp.err);
            }
            break;
        case PlotKind::ScatterZ:
            if (pi < t.scatter_z.size() && edits_detail::op_current(t.scatter_z[pi], e)) {
                auto& sp = t.scatter_z[pi];
                required(sp.x, sp.y, sp.z);
                labels(sp.opts.hint_labels);
                err_cols(sp.err);
            }
            break;
        case PlotKind::Heatmap:
        case PlotKind::Bar3D:
            // Not point-addressable; MatrixLineEdit handles these.
            break;
        case PlotKind::Surface:
        case PlotKind::Scatter3D:
        case PlotKind::Line3D:
        case PlotKind::SurfaceTri:
            // 3D-native; see apply_axes3d_data_op().
            break;
    }
}

namespace edits_detail {

// Insert or remove one line of a rows x cols row-major buffer. `row_axis`: a
// row is `cols` contiguous elements. Empty buffers are left alone.
template <class V>
void grid_reshape(V& v, std::size_t rows, std::size_t cols,
                  bool row_axis, bool insert, std::size_t index) {
    using Value = typename std::decay_t<decltype(v)>::value_type;
    if (v.empty()) return;
    auto& vv = mut_ref(v);
    if (row_axis) {
        const std::size_t at = index * cols;
        if (insert) {
            // Seed from the row above; zero-filled at row 0.
            std::vector<Value> seed(cols);
            if (index > 0)
                seed.assign(vv.begin() + static_cast<std::ptrdiff_t>(at - cols),
                            vv.begin() + static_cast<std::ptrdiff_t>(at));
            vv.insert(vv.begin() + static_cast<std::ptrdiff_t>(at),
                      seed.begin(), seed.end());
        } else {
            vv.erase(vv.begin() + static_cast<std::ptrdiff_t>(at),
                     vv.begin() + static_cast<std::ptrdiff_t>(at + cols));
        }
    } else {
        // Bottom-up, so offsets computed with the original stride stay valid.
        for (std::size_t r = rows; r-- > 0; ) {
            const std::size_t at = r * cols + index;
            if (insert) {
                Value seed = (index > 0) ? vv[at - 1] : Value{};
                vv.insert(vv.begin() + static_cast<std::ptrdiff_t>(at), std::move(seed));
            } else {
                vv.erase(vv.begin() + static_cast<std::ptrdiff_t>(at));
            }
        }
    }
}

// Whether a structural grid edit is allowed; never below 1x1.
inline bool grid_line_ok(std::size_t rows, std::size_t cols,
                         bool row_axis, bool insert, std::size_t index) {
    if (rows == 0 || cols == 0) return false;
    const std::size_t extent = row_axis ? rows : cols;
    if (insert ? (index > extent) : (index >= extent)) return false;
    return insert || extent > 1;
}

} // namespace edits_detail

// Insert or remove a heatmap row/column, re-striding data and hint_labels.
template <class T>
void apply_plot_data_op(T& t, const MatrixLineEdit& e) {
    if (e.kind != PlotKind::Heatmap || e.plot_index < 0) return;
    const std::size_t pi = static_cast<std::size_t>(e.plot_index);
    if (pi >= t.heatmaps.size() || !edits_detail::op_current(t.heatmaps[pi], e)) return;
    auto& hp = t.heatmaps[pi];
    if (hp.rows <= 0 || hp.cols <= 0) return;

    const std::size_t rows = static_cast<std::size_t>(hp.rows);
    const std::size_t cols = static_cast<std::size_t>(hp.cols);
    // A buffer not matching its shape is left alone.
    if (hp.data.size() != rows * cols) return;

    const bool insert = (e.op == MatrixLineEdit::Op::Insert);
    const bool row_ax = (e.axis == MatrixLineEdit::Axis::Row);
    if (!edits_detail::grid_line_ok(rows, cols, row_ax, insert, e.index)) return;

    // Non-empty hint_labels are padded to rows*cols first so they stay aligned.
    auto& labels = hp.opts.hint_labels;
    if (!labels.empty()) labels.resize(rows * cols);

    edits_detail::grid_reshape(hp.data, rows, cols, row_ax, insert, e.index);
    edits_detail::grid_reshape(labels,  rows, cols, row_ax, insert, e.index);
    if (row_ax) hp.rows += insert ? 1 : -1;
    else        hp.cols += insert ? 1 : -1;
}

// ---------------------------------------------------------------------------
// Applying data ops to a 3D axes' own plot objects (plane index -1). The
// `kind` guard keeps these and the 2D bodies from both firing. Same
// re-validation rules as above.
// ---------------------------------------------------------------------------

namespace edits_detail {

// Coordinate for a new grid line: midway between neighbours, or one spacing
// past the end.
inline double grid_coord_seed(const CowVec<double>& c, std::size_t at) {
    const std::size_t n = c.size();
    if (n == 0) return 0.0;
    if (at == 0) return c[0] - (n >= 2 ? c[1] - c[0] : 1.0);
    if (at >= n) return c[n - 1] + (n >= 2 ? c[n - 1] - c[n - 2] : 1.0);
    return 0.5 * (c[at - 1] + c[at]);
}

} // namespace edits_detail

// Bar3D columns: 0 = u, 1 = v (`element` indexes the vector), 2 = heights,
// 3 = bottoms (`element` is i * |v| + j). Surfaces use columns 0-2.
namespace edits_detail {
inline void put_cell(CowVec<double>& v, std::size_t i, double value) {
    if (i < v.size()) v.mut()[i] = value;
}
} // namespace edits_detail

template <class T>
void apply_axes3d_data_op(T& t, const PlotCellEdit& e) {
    if (e.plot_index < 0) return;
    const std::size_t pi = static_cast<std::size_t>(e.plot_index);
    using edits_detail::put_cell;
    if (e.kind == PlotKind::Bar3D) {
        if (pi >= t.bars3d.size() || !edits_detail::op_current(t.bars3d[pi], e)) return;
        auto& b = t.bars3d[pi];
        switch (e.column) {
            case 0: put_cell(b.u, e.element, e.value);       return;
            case 1: put_cell(b.v, e.element, e.value);       return;
            case 2: put_cell(b.heights, e.element, e.value); return;
            case 3: put_cell(b.bottoms, e.element, e.value); return;
            default: return;
        }
    }
    if (e.kind == PlotKind::Surface) {
        if (pi >= t.surfaces.size() || !edits_detail::op_current(t.surfaces[pi], e)) return;
        auto& s = t.surfaces[pi];
        switch (e.column) {
            case 0: put_cell(s.u, e.element, e.value);       return;
            case 1: put_cell(s.v, e.element, e.value);       return;
            case 2: put_cell(s.heights, e.element, e.value); return;
            default: return;
        }
    }
    // Scatter3D: x, y, z, colors (`element` is the point index; colors is empty
    // for a flat series).
    if (e.kind == PlotKind::Scatter3D) {
        if (pi >= t.scatter3d.size() || !edits_detail::op_current(t.scatter3d[pi], e)) return;
        auto& s = t.scatter3d[pi];
        switch (e.column) {
            case 0: put_cell(s.x, e.element, e.value);      return;
            case 1: put_cell(s.y, e.element, e.value);      return;
            case 2: put_cell(s.z, e.element, e.value);      return;
            case 3: put_cell(s.colors, e.element, e.value); return;
            default: return;
        }
    }
    // Line3D: same columns as Scatter3D.
    if (e.kind == PlotKind::Line3D) {
        if (pi >= t.lines3d.size() || !edits_detail::op_current(t.lines3d[pi], e)) return;
        auto& l = t.lines3d[pi];
        switch (e.column) {
            case 0: put_cell(l.x, e.element, e.value);      return;
            case 1: put_cell(l.y, e.element, e.value);      return;
            case 2: put_cell(l.z, e.element, e.value);      return;
            case 3: put_cell(l.colors, e.element, e.value); return;
            default: return;
        }
    }
    // SurfaceTri: same columns (vertex index). Topology is not editable, and
    // editing a vertex does not re-triangulate.
    if (e.kind == PlotKind::SurfaceTri) {
        if (pi >= t.surface_tri.size() || !edits_detail::op_current(t.surface_tri[pi], e)) return;
        auto& m = t.surface_tri[pi];
        switch (e.column) {
            case 0: put_cell(m.x, e.element, e.value);      return;
            case 1: put_cell(m.y, e.element, e.value);      return;
            case 2: put_cell(m.z, e.element, e.value);      return;
            case 3: put_cell(m.colors, e.element, e.value); return;
            default: return;
        }
    }
}

template <class T>
void apply_axes3d_data_op(T& t, const BarWidthEdit& e) {
    if (e.kind != PlotKind::Bar3D || e.plot_index < 0) return;
    const std::size_t pi = static_cast<std::size_t>(e.plot_index);
    if (pi >= t.bars3d.size() || !edits_detail::op_current(t.bars3d[pi], e)) return;
    if (e.column == 0)      t.bars3d[pi].u_width = e.width;
    else if (e.column == 1) t.bars3d[pi].v_width = e.width;
}

namespace edits_detail {

// Move a grid's coordinate vector and every matrix striding against it
// together. `alt` is an optional second matrix (bar bottoms); empty stays empty.
template <class Labels>
void grid_line_apply(CowVec<double>& u, CowVec<double>& v,
                     CowVec<double>& primary, CowVec<double>& alt, Labels& labels,
                     const MatrixLineEdit& e) {
    const std::size_t rows = u.size(), cols = v.size();
    const bool insert = (e.op == MatrixLineEdit::Op::Insert);
    const bool row_ax = (e.axis == MatrixLineEdit::Axis::Row);
    if (!grid_line_ok(rows, cols, row_ax, insert, e.index)) return;
    // A mismatched buffer is left alone, as for heatmaps.
    if (primary.size() != rows * cols) return;
    if (!alt.empty() && alt.size() != rows * cols) return;

    if (!labels.empty()) labels.resize(rows * cols);

    // Coordinate first, while rows/cols still describe the buffers.
    CowVec<double>& coord = row_ax ? u : v;
    if (insert) {
        const double at = grid_coord_seed(coord, e.index);
        auto& cv = coord.mut();
        cv.insert(cv.begin() + static_cast<std::ptrdiff_t>(e.index), at);
    } else {
        row_remove(coord, e.index);
    }
    grid_reshape(primary, rows, cols, row_ax, insert, e.index);
    grid_reshape(alt,     rows, cols, row_ax, insert, e.index);
    grid_reshape(labels,  rows, cols, row_ax, insert, e.index);
}

} // namespace edits_detail

template <class T>
void apply_axes3d_data_op(T& t, const MatrixLineEdit& e) {
    if (e.plot_index < 0) return;
    const std::size_t pi = static_cast<std::size_t>(e.plot_index);
    if (e.kind == PlotKind::Bar3D) {
        if (pi >= t.bars3d.size() || !edits_detail::op_current(t.bars3d[pi], e)) return;
        auto& b = t.bars3d[pi];
        edits_detail::grid_line_apply(b.u, b.v, b.heights, b.bottoms,
                                      b.opts.hint_labels, e);
        return;
    }
    if (e.kind == PlotKind::Surface) {
        if (pi >= t.surfaces.size() || !edits_detail::op_current(t.surfaces[pi], e)) return;
        auto& s = t.surfaces[pi];
        // No second matrix: pass an empty one.
        CowVec<double> none;
        edits_detail::grid_line_apply(s.u, s.v, s.heights, none,
                                      s.opts.hint_labels, e);
        return;
    }
}

template <class T>
void apply_axes3d_data_op(T&, const PlotRowEdit&) {
    // Not point-addressable for grids; MatrixLineEdit handles them.
}

// Applies an ordered op stream. The 2D overload skips plane-addressed ops; the
// 3D overload (targets with planes) routes plane -1 to the axes' own objects
// and others to the plane's sheet.
template <class T>
void apply_plot_data_ops(T& t, const std::vector<PlotDataOp>& ops) {
    for (const auto& op : ops) {
        if (plot_op_plane(op) >= 0) continue;
        std::visit([&t](const auto& o) { apply_plot_data_op(t, o); }, op);
    }
}

template <class T>
    requires requires(T& t) { t.plane_at(std::size_t{0}).sheet; }
void apply_plot_data_ops(T& t, const std::vector<PlotDataOp>& ops) {
    for (const auto& op : ops) {
        const int pi = plot_op_plane(op);
        if (pi < 0) {
            std::visit([&t](const auto& o) { apply_axes3d_data_op(t, o); }, op);
            continue;
        }
        if (static_cast<std::size_t>(pi) >= t.plane_count()) continue;
        auto& sheet = t.plane_at(static_cast<std::size_t>(pi)).sheet;
        std::visit([&sheet](const auto& o) { apply_plot_data_op(sheet, o); }, op);
    }
}

// ---------------------------------------------------------------------------
// Plot-object appearance
// ---------------------------------------------------------------------------
// Assign an object's options, keeping its current `hint_labels` (data owned by
// the Data panel, which the options copy may have stale).
template <class Vec, class Opts>
void assign_plot_opts(Vec& v, int idx, const Opts& o) {
    if (idx < 0 || static_cast<std::size_t>(idx) >= v.size()) return;
    auto labels = std::move(v[static_cast<std::size_t>(idx)].opts.hint_labels);
    v[static_cast<std::size_t>(idx)].opts = o;
    v[static_cast<std::size_t>(idx)].opts.hint_labels = std::move(labels);
}

// The variant alternative picks the vector.
template <class T>
void apply_plot_style_edit(T& t, const PlotStyleEdit& e) {
    // Made before a cla() of this axes or plane: its index may name another object.
    if (t.style_stamps.cleared > e.seen) return;
    if (const auto* o = std::get_if<LineOptions>(&e.opts))          assign_plot_opts(t.lines,     e.plot_index, *o);
    else if (const auto* o = std::get_if<ScatterOptions>(&e.opts))  assign_plot_opts(t.scatters,  e.plot_index, *o);
    else if (const auto* o = std::get_if<BarOptions>(&e.opts))      assign_plot_opts(t.bars,      e.plot_index, *o);
    else if (const auto* o = std::get_if<HeatmapOptions>(&e.opts))  assign_plot_opts(t.heatmaps,  e.plot_index, *o);
    else if (const auto* o = std::get_if<ScatterZOptions>(&e.opts)) assign_plot_opts(t.scatter_z, e.plot_index, *o);
}

// As apply_plot_data_ops(): 2D targets take plane -1 only; 3D targets take
// plane-addressed edits only (their own objects use AxesEdit3D's lanes).
template <class T>
void apply_plot_style_edits(T& t, const std::vector<PlotStyleEdit>& es) {
    for (const auto& e : es) {
        if (e.plane_index >= 0) continue;
        apply_plot_style_edit(t, e);
    }
}

template <class T>
    requires requires(T& t) { t.plane_at(std::size_t{0}).sheet; }
void apply_plot_style_edits(T& t, const std::vector<PlotStyleEdit>& es) {
    for (const auto& e : es) {
        if (e.plane_index < 0) continue;
        if (static_cast<std::size_t>(e.plane_index) >= t.plane_count()) continue;
        apply_plot_style_edit(t.plane_at(static_cast<std::size_t>(e.plane_index)).sheet, e);
    }
}

// The setter-group edits shared by 2D and 3D axes (and in 3D the box and
// aspect), each group applied unless its setter ran after the snapshot the
// panel drew (StyleStamps). For Axes::Impl, Axes3D::Impl and both snapshots.
template <typename T, typename E>
void apply_style_edits(T& dst, const E& e) {
    const StyleStamps& have = dst.style_stamps;
    const StyleStamps& seen = e.style_seen;
    // Empty = back to auto; absent = unchanged.
    auto ticks = [](auto& d, const std::optional<std::vector<Tick>>& v) {
        if (v) d = v->empty() ? std::nullopt : std::optional(*v);
    };
    if (have.grid <= seen.grid) {
        if (e.grid_enabled) dst.grid_enabled = *e.grid_enabled;
        if (e.grid_opts)    dst.grid_opts    = *e.grid_opts;
    }
    if (e.axes_style && have.style <= seen.style) dst.axes_style = *e.axes_style;
    if (have.legend <= seen.legend) {
        if (e.legend_enabled) dst.legend_enabled = *e.legend_enabled;
        if (e.legend_opts)    dst.legend_opts    = *e.legend_opts;
    }
    if (e.colorbar_opts && have.colorbar <= seen.colorbar) dst.colorbar_opts = *e.colorbar_opts;
    if (have.xticks <= seen.xticks) ticks(dst.xticks_override, e.xticks_override);
    if (have.yticks <= seen.yticks) ticks(dst.yticks_override, e.yticks_override);
    if constexpr (requires { dst.zticks_override; e.zticks_override; })
        if (have.zticks <= seen.zticks) ticks(dst.zticks_override, e.zticks_override);
    if constexpr (requires { dst.box_style; e.box_style; }) {
        if (e.box_style && have.box <= seen.box)    dst.box_style = *e.box_style;
        if (e.aspect && have.aspect <= seen.aspect) dst.aspect    = *e.aspect;
    }
}

// Apply a 2D edit to Axes::Impl (caller thread) or RenderSnapshot (render
// thread), as apply_axes3d_edit() does for 3D.
template <typename T>
void apply_axes_edit(T& dst, const AxesEdit& e) {
    apply_title_edits(dst, e);
    apply_limit_edits(dst, e);
    apply_style_edits(dst, e);
    apply_plot_data_ops(dst, e.plot_ops);
    apply_plot_style_edits(dst, e.plot_styles);
}

// Apply a 3D edit to Axes3D::Impl (caller thread) or RenderSnapshot3D (render
// thread); one template so both paths stay identical.
template <typename T>
void apply_axes3d_edit(T& dst, const AxesEdit3D& e) {
    apply_title_edits(dst, e);
    apply_limit_edits(dst, e);
    apply_style_edits(dst, e);
    if (e.camera && dst.camera_stamp <= e.camera_seen) dst.camera = *e.camera;

    // Planes: re-validated, stale indices skipped.
    apply_plot_style_edits(dst, e.plot_styles);

    for (const auto& pe : e.planes) {
        if (pe.plane_index < 0) continue;
        const std::size_t pi = static_cast<std::size_t>(pe.plane_index);
        if (pi >= dst.plane_count()) continue;
        auto& p = dst.plane_at(pi);
        // A setter (or a new plane at this index) since the panel drew wins.
        if (p.placement_stamp > pe.seen) continue;
        if (pe.orient) p.orient = *pe.orient;
        if (pe.offset) p.offset = *pe.offset;
        if (pe.opts)   p.opts   = *pe.opts;
    }

    // The axes' own 3D objects; `hint_labels` is kept (it is data). An edit made
    // before a cla() is dropped, as for plot_styles.
    auto objects = [&dst](auto& plots, const auto& edits) {
        for (const auto& oe : edits) {
            if (oe.plot_index < 0 || !oe.opts || dst.style_stamps.cleared > oe.seen) continue;
            const std::size_t i = static_cast<std::size_t>(oe.plot_index);
            if (i >= plots.size()) continue;
            auto labels = std::move(plots[i].opts.hint_labels);
            plots[i].opts = *oe.opts;
            plots[i].opts.hint_labels = std::move(labels);
        }
    };
    objects(dst.bars3d, e.bars3d);
    objects(dst.surfaces, e.surfaces);
    objects(dst.scatter3d, e.scatter3d);
    objects(dst.lines3d, e.lines3d);
    objects(dst.surface_tri, e.surface_tri);
    apply_plot_data_ops(dst, e.plot_ops);
}

// ---------------------------------------------------------------------------
// The journal's style lane: every edit without a lane of its own, as its
// latest value. Each setter group keeps the recorded stamp of the edit that
// last set it; entries addressed to a plane or object keep their own.
// ---------------------------------------------------------------------------

namespace edits_detail {

template <class V>
void take_latest(std::optional<V>& d, const std::optional<V>& s,
                 unsigned long long& d_seen, unsigned long long s_seen) {
    if (s) { d = s; d_seen = s_seen; }
}

// Replace the entry with the same key, else append.
template <class Vec, class Key>
void upsert(Vec& dst, const typename Vec::value_type& v, Key key) {
    for (auto& d : dst)
        if (key(d) == key(v)) { d = v; return; }
    dst.push_back(v);
}

} // namespace edits_detail

// Titles, limits, the camera and plot_ops have lanes of their own and are
// not copied.
template <class E>
void merge_style_edits(E& dst, const E& src) {
    using edits_detail::take_latest;
    using edits_detail::upsert;
    StyleStamps& ds = dst.style_seen;
    const StyleStamps& ss = src.style_seen;
    take_latest(dst.grid_enabled, src.grid_enabled, ds.grid, ss.grid);
    take_latest(dst.grid_opts, src.grid_opts, ds.grid, ss.grid);
    take_latest(dst.axes_style, src.axes_style, ds.style, ss.style);
    take_latest(dst.legend_enabled, src.legend_enabled, ds.legend, ss.legend);
    take_latest(dst.legend_opts, src.legend_opts, ds.legend, ss.legend);
    take_latest(dst.colorbar_opts, src.colorbar_opts, ds.colorbar, ss.colorbar);
    take_latest(dst.xticks_override, src.xticks_override, ds.xticks, ss.xticks);
    take_latest(dst.yticks_override, src.yticks_override, ds.yticks, ss.yticks);
    for (const PlotStyleEdit& s : src.plot_styles)
        upsert(dst.plot_styles, s, [](const PlotStyleEdit& p) {
            return std::tuple(p.plane_index, p.plot_index, p.opts.index());
        });
    if constexpr (requires { src.box_style; }) {
        take_latest(dst.zticks_override, src.zticks_override, ds.zticks, ss.zticks);
        take_latest(dst.box_style, src.box_style, ds.box, ss.box);
        take_latest(dst.aspect, src.aspect, ds.aspect, ss.aspect);
        for (const auto& pe : src.planes) {
            auto it = std::find_if(dst.planes.begin(), dst.planes.end(),
                                   [&](const auto& d) { return d.plane_index == pe.plane_index; });
            if (it == dst.planes.end()) { dst.planes.push_back(pe); continue; }
            if (pe.orient) it->orient = pe.orient;
            if (pe.offset) it->offset = pe.offset;
            if (pe.opts)   it->opts   = pe.opts;
            it->seen = pe.seen;
        }
        auto by_index = [](const auto& o) { return o.plot_index; };
        for (const auto& o : src.bars3d)      upsert(dst.bars3d, o, by_index);
        for (const auto& o : src.surfaces)    upsert(dst.surfaces, o, by_index);
        for (const auto& o : src.scatter3d)   upsert(dst.scatter3d, o, by_index);
        for (const auto& o : src.lines3d)     upsert(dst.lines3d, o, by_index);
        for (const auto& o : src.surface_tri) upsert(dst.surface_tri, o, by_index);
    }
}

// True if `e` carries nothing merge_style_edits() would copy.
template <class E>
bool style_edits_empty(const E& e) {
    bool empty = !e.grid_enabled && !e.grid_opts && !e.axes_style && !e.legend_enabled
                 && !e.legend_opts && !e.colorbar_opts && !e.xticks_override
                 && !e.yticks_override && e.plot_styles.empty();
    if constexpr (requires { e.box_style; })
        empty = empty && !e.zticks_override && !e.box_style && !e.aspect && e.planes.empty()
                && e.bars3d.empty() && e.surfaces.empty() && e.scatter3d.empty()
                && e.lines3d.empty() && e.surface_tri.empty();
    return empty;
}

// The figure-level fields the same way (grid ratios have their own lane).
inline void merge_figure_edits(FigureEdits& dst, const FigureEdits& src) {
    using edits_detail::take_latest;
    FigureStamps& ds = dst.fig_seen;
    const FigureStamps& ss = src.fig_seen;
    take_latest(dst.suptitle, src.suptitle, ds.suptitle, ss.suptitle);
    take_latest(dst.suptitle_opts, src.suptitle_opts, ds.suptitle_style, ss.suptitle_style);
    take_latest(dst.margins, src.margins, ds.margins, ss.margins);
    // No setter writes the gaps after create(), so they need no stamp.
    if (src.col_gap) dst.col_gap = src.col_gap;
    if (src.row_gap) dst.row_gap = src.row_gap;
}

// Applies the figure-level fields to Figure::Impl's (or a FigureSnapshot's)
// members, each unless its setter ran after the snapshot the panel drew.
// `have` is the destination's FigureStamps.
template <class Suptitle, class Opts, class Margins, class Gap>
void apply_figure_edits(const FigureEdits& e, const FigureStamps& have,
                        Suptitle& suptitle, Opts& suptitle_opts, Margins& margins,
                        Gap& col_gap, Gap& row_gap) {
    const FigureStamps& seen = e.fig_seen;
    if (e.suptitle && have.suptitle <= seen.suptitle) suptitle = *e.suptitle;
    if (e.suptitle_opts && have.suptitle_style <= seen.suptitle_style)
        suptitle_opts = *e.suptitle_opts;
    if (e.margins && have.margins <= seen.margins) margins = *e.margins;
    if (e.col_gap) col_gap = *e.col_gap;
    if (e.row_gap) row_gap = *e.row_gap;
}

} // namespace sextant
