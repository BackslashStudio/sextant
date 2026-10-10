#pragma once
#include "sextant/style.h"
#include "plot_objects.h"
#include "tick.h"
#include <algorithm>
#include <array>
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
    // The object and its plane (see ObjectId), resolved before the index:
    // filled in by FigureEditBox from the drawn snapshot, 0 = by index alone.
    ObjectId id = 0;
    ObjectId plane_id = 0;
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
    ObjectId id = 0, plane_id = 0;    // see PlotCellEdit

    // The removed point, on the Insert that undoes a Remove:
    // Insert writes these values instead of copying row-1, and puts a value
    // into an optional column only where the point had one.
    struct Restore {
        std::vector<double> cols;                   // the kind's required columns, in order
        std::optional<std::string> label;           // its hint label
        std::array<std::optional<double>, 8> err;   // see edits_detail::err_columns()
    };
    std::optional<Restore> restore;
};

// Appearance of one 2D plot object, from its Data-panel tab. Addressed like a
// PlotDataOp; the variant alternative is the kind. Carries the whole options
// struct (a text's TextOptions and ArrowOptions together).
struct PlotStyleEdit {
    int plot_index  = 0;
    int plane_index = -1;
    std::variant<LineOptions, ScatterOptions, BarOptions,
                 HeatmapOptions, ScatterZOptions, TextStyle> opts;
    ObjectId id = 0, plane_id = 0;   // see PlotCellEdit
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
    ObjectId id = 0, plane_id = 0;    // see PlotCellEdit

    // The removed line, on the Insert that undoes a Remove (as PlotRowEdit's).
    struct Restore {
        std::vector<double> values;        // of the data matrix
        std::vector<double> alt;           // of bar3d's bottoms; empty = none
        std::vector<std::string> labels;   // of hint_labels; empty = none
        double coord = 0.0;                // grid kinds: the line's coordinate
    };
    std::optional<Restore> restore;
    // On the inverse of an op that padded (or cut) hint_labels to rows x cols:
    // the size they are put back to afterwards.
    std::optional<std::size_t> labels_size;
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
    ObjectId id = 0, plane_id = 0;    // see PlotCellEdit
};

// A text's string and placement, replaced whole from its Data-panel tab and
// journaled like other data (a text is one row, not a table).
struct TextDataEdit {
    int         plot_index  = 0;
    TextContent content;
    int         plane_index = -1;   // see PlotCellEdit
    PlotKind    kind        = PlotKind::Text;
    unsigned long long seen = ~0ull;   // see PlotCellEdit
    ObjectId id = 0, plane_id = 0;    // see PlotCellEdit
};

// One op in a plot's edit stream. Order matters: indices refer to the arrays
// as they were when recorded, so ops must replay in sequence.
using PlotDataOp = std::variant<PlotCellEdit, PlotRowEdit, MatrixLineEdit, BarWidthEdit,
                                TextDataEdit>;

// The plane_index every op carries.
inline int plot_op_plane(const PlotDataOp& op) {
    return std::visit([](const auto& o) { return o.plane_index; }, op);
}

// And its plane's id (see PlotCellEdit::plane_id).
inline ObjectId plot_op_plane_id(const PlotDataOp& op) {
    return std::visit([](const auto& o) { return o.plane_id; }, op);
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
//
// Undo: every apply_* below takes an optional `inv`, the same
// type as the edit, and records in it the value each field it writes replaced;
// a field it skips (a newer setter, a gone object) leaves nothing there.
template <typename T, typename E>
void apply_title_edits(T& dst, const E& e, E* inv = nullptr) {
    auto one = [](std::string& text, unsigned long long have,
                  const std::optional<std::string>& v, unsigned long long seen,
                  std::optional<std::string>* old) {
        if (!v || have > seen) return;
        if (old) *old = text;
        text = *v;
    };
    one(dst.title,  dst.title_stamps.title,  e.title,  e.title_seen.title,
        inv ? &inv->title : nullptr);
    one(dst.xtitle, dst.title_stamps.xtitle, e.xtitle, e.title_seen.xtitle,
        inv ? &inv->xtitle : nullptr);
    one(dst.ytitle, dst.title_stamps.ytitle, e.ytitle, e.title_seen.ytitle,
        inv ? &inv->ytitle : nullptr);
    if constexpr (requires { dst.ztitle; e.ztitle; })
        one(dst.ztitle, dst.title_stamps.ztitle, e.ztitle, e.title_seen.ztitle,
            inv ? &inv->ztitle : nullptr);
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
void apply_limit_edits(T& dst, const E& e, E* inv = nullptr) {
    using edits_detail::LimitAxis;
    using OB = const std::optional<bool>;
    using OD = const std::optional<double>;
    using RB = std::optional<bool>;
    using RD = std::optional<double>;
    // The inverse takes the whole axis, so an undo puts auto and both bounds back.
    auto one = [inv](LimitAxis<bool, double> d, unsigned long long have,
                     LimitAxis<OB, OD> v, unsigned long long seen,
                     RB E::* old_auto, RD E::* old_lo, RD E::* old_hi) {
        if (have > seen || (!v.autoscale && !v.lo && !v.hi)) return;
        if (inv) {
            inv->*old_auto = d.autoscale;
            inv->*old_lo   = d.lo;
            inv->*old_hi   = d.hi;
        }
        if (v.autoscale) d.autoscale = *v.autoscale;
        if (v.lo) d.lo = *v.lo;
        if (v.hi) d.hi = *v.hi;
    };
    one({dst.xlim_auto, dst.xmin, dst.xmax}, dst.limit_stamps.x,
        {e.xlim_auto, e.xmin, e.xmax}, e.lim_seen.x, &E::xlim_auto, &E::xmin, &E::xmax);
    one({dst.ylim_auto, dst.ymin, dst.ymax}, dst.limit_stamps.y,
        {e.ylim_auto, e.ymin, e.ymax}, e.lim_seen.y, &E::ylim_auto, &E::ymin, &E::ymax);
    if constexpr (requires { dst.zmin; e.zmin; })
        one({dst.zlim_auto, dst.zmin, dst.zmax}, dst.limit_stamps.z,
            {e.zlim_auto, e.zmin, e.zmax}, e.lim_seen.z, &E::zlim_auto, &E::zmin, &E::zmax);
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

    // Camera edits go through here (not FigureWindowState) so a dragged view survives
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
        // The plane's id (see ObjectId); filled in by FigureEditBox.
        ObjectId id = 0;
    };
    std::vector<PlaneEdit> planes;

    // Appearance of the axes' own 3D objects, addressed like PlotStyleEdit
    // (`id` filled in by FigureEditBox). apply_axes3d_edit() preserves
    // `hint_labels`, which are data edited elsewhere.
    struct Bar3DEdit   { int plot_index = 0; std::optional<Bar3DOptions>   opts; ObjectId id = 0; };
    struct SurfaceEdit { int plot_index = 0; std::optional<SurfaceOptions> opts; ObjectId id = 0; };
    struct Scatter3DEdit { int plot_index = 0; std::optional<Scatter3DOptions> opts; ObjectId id = 0; };
    struct Line3DEdit  { int plot_index = 0; std::optional<Line3DOptions>  opts; ObjectId id = 0; };
    struct SurfaceTriEdit { int plot_index = 0; std::optional<SurfaceTriOptions> opts; ObjectId id = 0; };
    // A text's look (its content is a TextDataEdit in plot_ops).
    struct TextEdit    { int plot_index = 0; std::optional<TextStyle> opts; ObjectId id = 0; };
    std::vector<Bar3DEdit>     bars3d;
    std::vector<SurfaceEdit>   surfaces;
    std::vector<Scatter3DEdit> scatter3d;
    std::vector<Line3DEdit>    lines3d;
    std::vector<SurfaceTriEdit> surface_tri;
    std::vector<TextEdit>      texts;

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
    std::optional<Color>         background;

    // Grid weights from dragging or the Layout fields; journaled, so a drag
    // survives refresh().
    std::optional<std::vector<float>> col_ratios, row_ratios;

    // The figure stamps of the snapshot the panel drew; filled in by FigureEditBox.
    FigureStamps fig_seen = FigureStamps::any();

    // Must list every field above: a missing one is silently dropped when it
    // arrives without a per-axes edit.
    bool empty() const {
        return per_axes.empty() && per_axes3d.empty() && !suptitle && !suptitle_opts
               && !margins && !col_gap && !row_gap && !background
               && !col_ratios && !row_ratios;
    }
};

// True if a drain holds only navigation (2D limits/ticks, 3D camera), which
// leaves the stored layout alone. An allow-list: new fields count as changes.
inline bool is_navigation_only(const FigureEdits& f) {
    if (f.suptitle || f.suptitle_opts || f.margins || f.col_gap || f.row_gap || f.background
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
            || !e.lines3d.empty() || !e.surface_tri.empty() || !e.texts.empty()
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
//
// Each applier returns the op that undoes it, or nothing when it
// changed nothing. The inverse addresses the object where it is now (index,
// id, and its current data_stamp as `seen`, so a later set_*_data() makes the
// undo drop); a list's inverse runs in reverse order.
// ---------------------------------------------------------------------------

namespace edits_detail {

// Writable access for both CowVec and plain vectors (opts.hint_labels).
template <class T> std::vector<T>& mut_ref(CowVec<T>& v)      { return v.mut(); }
template <class T> std::vector<T>& mut_ref(std::vector<T>& v) { return v; }

// False for an op recorded over an older copy of the plot (PlotCellEdit::seen).
template <class P, class E>
bool op_current(const P& p, const E& e) { return p.data_stamp <= e.seen; }

// The object an edit addresses, or null when it is gone. With an id, the
// object at `index` if it is that one, else a search (removal and reorder move
// objects); without one (0), the index alone.
template <class Vec>
auto* find_object(Vec& v, int index, ObjectId id) {
    using P = std::remove_reference_t<decltype(v[0])>;
    P* at = index >= 0 && static_cast<std::size_t>(index) < v.size()
                ? &v[static_cast<std::size_t>(index)] : nullptr;
    if (id == 0 || (at && at->id == id)) return at;
    for (auto& p : v)
        if (p.id == id) return &p;
    return static_cast<P*>(nullptr);
}

// The same for a plane of a 3D axes (Axes3D::Impl or RenderSnapshot3D).
template <class T>
auto* find_plane(T& t, int index, ObjectId id) {
    using P = std::remove_reference_t<decltype(t.plane_at(std::size_t{0}))>;
    const std::size_t n = t.plane_count();
    P* at = index >= 0 && static_cast<std::size_t>(index) < n
                ? &t.plane_at(static_cast<std::size_t>(index)) : nullptr;
    if (id == 0 || (at && at->id == id)) return at;
    for (std::size_t i = 0; i < n; ++i)
        if (t.plane_at(i).id == id) return &t.plane_at(i);
    return static_cast<P*>(nullptr);
}

// Where plane `p` (found by find_plane()) is now.
template <class T, class P>
int plane_index_of(const T& t, const P* p) {
    for (std::size_t i = 0; i < t.plane_count(); ++i)
        if (&t.plane_at(i) == p) return static_cast<int>(i);
    return -1;
}

// A data op's object, if it is still there and its data is the one the op was
// made over.
template <class Vec, class E>
auto* op_target(Vec& v, const E& e) {
    auto* p = find_object(v, e.plot_index, e.id);
    return p && op_current(*p, e) ? p : nullptr;
}

// A copy of `e` addressed to object `p` of `v` as it is now: the start of
// every inverse.
template <class E, class Vec, class P>
E retarget(const E& e, const Vec& v, const P& p) {
    E inv = e;
    inv.plot_index = static_cast<int>(&p - v.data());
    inv.id = p.id;
    inv.seen = p.data_stamp;
    return inv;
}

// Writes one cell; returns the value it replaced, or nothing out of range.
inline std::optional<double> put_cell(CowVec<double>& v, std::size_t i, double value) {
    if (i >= v.size()) return std::nullopt;
    const double old = v[i];
    v.mut()[i] = value;
    return old;
}

// The same cell with the value it held: a PlotCellEdit's inverse.
template <class Vec, class P>
std::optional<PlotDataOp> cell_undo(const PlotCellEdit& e, const Vec& v, const P& p,
                                    std::optional<double> old) {
    if (!old) return std::nullopt;
    PlotCellEdit inv = retarget(e, v, p);
    inv.value = *old;
    return inv;
}

} // namespace edits_detail

template <class T>
std::optional<PlotDataOp> apply_plot_data_op(T& t, const PlotCellEdit& e) {
    using edits_detail::cell_undo;
    using edits_detail::op_target;
    using edits_detail::put_cell;
    switch (e.kind) {
        case PlotKind::Line:
            if (auto* lp = op_target(t.lines, e))
                return cell_undo(e, t.lines, *lp,
                                 put_cell(e.column == 0 ? lp->x : lp->y, e.element, e.value));
            return std::nullopt;
        case PlotKind::Scatter:
            if (auto* sp = op_target(t.scatters, e))
                return cell_undo(e, t.scatters, *sp,
                                 put_cell(e.column == 0 ? sp->x : sp->y, e.element, e.value));
            return std::nullopt;
        case PlotKind::Bar:
            if (auto* bp = op_target(t.bars, e))
                return cell_undo(e, t.bars, *bp,
                                 put_cell(e.column == 0 ? bp->centers : bp->heights,
                                          e.element, e.value));
            return std::nullopt;
        case PlotKind::ScatterZ: {
            auto* sp = op_target(t.scatter_z, e);
            if (!sp) return std::nullopt;
            return cell_undo(e, t.scatter_z, *sp,
                             put_cell(e.column == 0 ? sp->x : (e.column == 1 ? sp->y : sp->z),
                                      e.element, e.value));
        }
        case PlotKind::Heatmap: {
            auto* hpp = op_target(t.heatmaps, e);
            if (!hpp) return std::nullopt;
            auto& hp = *hpp;
            // Re-check the shape; it may have changed since the panel saw it.
            if (hp.rows <= 0 || hp.cols <= 0) return std::nullopt;
            const std::size_t n = static_cast<std::size_t>(hp.rows)
                                * static_cast<std::size_t>(hp.cols);
            if (e.element >= n || e.element >= hp.data.size()) return std::nullopt;
            const double old = hp.data[e.element];
            hp.data.mut()[e.element] = static_cast<float>(e.value);
            return cell_undo(e, t.heatmaps, hp, old);
        }
        case PlotKind::Bar3D:
        case PlotKind::Surface:
        case PlotKind::Scatter3D:
        case PlotKind::Line3D:
        case PlotKind::SurfaceTri:
            // 3D-native; see apply_axes3d_data_op().
            return std::nullopt;
        case PlotKind::Text:
            // One row; see TextDataEdit.
            return std::nullopt;
    }
    return std::nullopt;
}

namespace edits_detail {

// Swaps a text's content in, from a 2D or 3D holder's `texts`.
template <class Vec>
std::optional<PlotDataOp> text_op(Vec& v, const TextDataEdit& e) {
    if (e.kind != PlotKind::Text) return std::nullopt;
    auto* tp = op_target(v, e);
    if (!tp) return std::nullopt;
    TextDataEdit inv = retarget(e, v, *tp);
    inv.content = std::move(tp->content);
    tp->content = e.content;
    return inv;
}

} // namespace edits_detail

template <class T>
std::optional<PlotDataOp> apply_plot_data_op(T& t, const TextDataEdit& e) {
    return edits_detail::text_op(t.texts, e);
}

template <class T>
std::optional<PlotDataOp> apply_plot_data_op(T& t, const BarWidthEdit& e) {
    if (e.kind != PlotKind::Bar) return std::nullopt;
    auto* bp = edits_detail::op_target(t.bars, e);
    if (!bp) return std::nullopt;
    BarWidthEdit inv = edits_detail::retarget(e, t.bars, *bp);
    inv.width = bp->bar_width;
    bp->bar_width = e.width;
    return inv;
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

// Insert a given value at `row`; no-op when the index doesn't fit.
template <class V>
void row_insert_value(V& v, std::size_t row, typename V::value_type value) {
    if (row > v.size()) return;
    auto& vv = mut_ref(v);
    vv.insert(vv.begin() + static_cast<std::ptrdiff_t>(row), std::move(value));
}

template <class V>
void row_remove(V& v, std::size_t row) {
    if (row >= v.size()) return;
    auto& vv = mut_ref(v);
    vv.erase(vv.begin() + static_cast<std::ptrdiff_t>(row));
}

// The value at `row`, if there is one.
template <class V>
std::optional<typename V::value_type> value_at(const V& v, std::size_t row) {
    if (row >= v.size()) return std::nullopt;
    return v[row];
}

// ErrorBarData's columns in PlotRowEdit::Restore::err's order.
inline std::array<CowVec<double>*, 8> err_columns(ErrorBarData& e) {
    return { &e.x_cap_lo, &e.x_cap_hi, &e.x_box_lo, &e.x_box_hi,
             &e.y_cap_lo, &e.y_cap_hi, &e.y_box_lo, &e.y_box_hi };
}

// One point in or out of object `p` of `v`: its required columns `cols` (the
// first gives the point count) and the optional index-aligned ones
// (hint_labels, error bars), which move only where they reach the row; empty
// ones are left alone. A new point copies its predecessor, except for a blank
// label, unless `e` restores a removed one.
template <class Vec, class P, class... Cols>
std::optional<PlotDataOp> row_op(const PlotRowEdit& e, const Vec& v, P& p, Cols&... cols) {
    const std::size_t n = std::get<0>(std::tie(cols...)).size();
    const bool insert = (e.op == PlotRowEdit::Op::Insert);
    if (insert ? e.row > n : e.row >= n) return std::nullopt;

    PlotRowEdit inv = retarget(e, v, p);
    inv.restore.reset();
    auto& labels = p.opts.hint_labels;
    const auto err = err_columns(p.err);
    if (insert && e.restore) {
        const PlotRowEdit::Restore& r = *e.restore;
        std::size_t i = 0;
        ((row_insert_value(cols, e.row, i < r.cols.size() ? r.cols[i] : 0.0), ++i), ...);
        if (r.label) row_insert_value(labels, e.row, *r.label);
        for (std::size_t k = 0; k < err.size(); ++k)
            if (r.err[k]) row_insert_value(*err[k], e.row, *r.err[k]);
    } else if (insert) {
        (row_insert(cols, e.row, true), ...);
        if (!labels.empty()) row_insert(labels, e.row, false);
        for (CowVec<double>* c : err)
            if (!c->empty()) row_insert(*c, e.row, true);
    } else {
        PlotRowEdit::Restore r;
        (r.cols.push_back(cols[e.row]), ...);
        r.label = value_at(labels, e.row);
        for (std::size_t k = 0; k < err.size(); ++k) r.err[k] = value_at(*err[k], e.row);
        inv.restore = std::move(r);
        (row_remove(cols, e.row), ...);
        row_remove(labels, e.row);
        for (CowVec<double>* c : err) row_remove(*c, e.row);
    }
    inv.op = insert ? PlotRowEdit::Op::Remove : PlotRowEdit::Op::Insert;
    return inv;
}

} // namespace edits_detail

// Adds or removes one data point.
template <class T>
std::optional<PlotDataOp> apply_plot_data_op(T& t, const PlotRowEdit& e) {
    using edits_detail::op_target;
    using edits_detail::row_op;
    switch (e.kind) {
        case PlotKind::Line:
            if (auto* lp = op_target(t.lines, e)) return row_op(e, t.lines, *lp, lp->x, lp->y);
            return std::nullopt;
        case PlotKind::Scatter:
            if (auto* sp = op_target(t.scatters, e))
                return row_op(e, t.scatters, *sp, sp->x, sp->y);
            return std::nullopt;
        case PlotKind::Bar:
            if (auto* bp = op_target(t.bars, e))
                return row_op(e, t.bars, *bp, bp->centers, bp->heights);
            return std::nullopt;
        case PlotKind::ScatterZ:
            if (auto* sp = op_target(t.scatter_z, e))
                return row_op(e, t.scatter_z, *sp, sp->x, sp->y, sp->z);
            return std::nullopt;
        case PlotKind::Heatmap:
        case PlotKind::Bar3D:
            // Not point-addressable; MatrixLineEdit handles these.
            return std::nullopt;
        case PlotKind::Surface:
        case PlotKind::Scatter3D:
        case PlotKind::Line3D:
        case PlotKind::SurfaceTri:
            // 3D-native; see apply_axes3d_data_op().
            return std::nullopt;
        case PlotKind::Text:
            // One row; see TextDataEdit.
            return std::nullopt;
    }
    return std::nullopt;
}

namespace edits_detail {

// Insert or remove one line of a rows x cols row-major buffer. `row_axis`: a
// row is `cols` contiguous elements. Empty buffers are left alone. An inserted
// line takes `given` when it has the line's length, else copies its
// predecessor (zero-filled at index 0).
template <class V, class G = std::vector<typename V::value_type>>
void grid_reshape(V& v, std::size_t rows, std::size_t cols,
                  bool row_axis, bool insert, std::size_t index, const G* given = nullptr) {
    using Value = typename std::decay_t<decltype(v)>::value_type;
    if (v.empty()) return;
    if (given && given->size() != (row_axis ? cols : rows)) given = nullptr;
    auto& vv = mut_ref(v);
    if (row_axis) {
        const std::size_t at = index * cols;
        if (insert) {
            // Seed from the row above; zero-filled at row 0.
            std::vector<Value> seed(cols);
            if (given)
                for (std::size_t c = 0; c < cols; ++c) seed[c] = static_cast<Value>((*given)[c]);
            else if (index > 0)
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
                Value seed = given ? static_cast<Value>((*given)[r])
                                   : (index > 0) ? vv[at - 1] : Value{};
                vv.insert(vv.begin() + static_cast<std::ptrdiff_t>(at), std::move(seed));
            } else {
                vv.erase(vv.begin() + static_cast<std::ptrdiff_t>(at));
            }
        }
    }
}

// Line `index` of a rows x cols row-major buffer; empty for an empty buffer.
template <class Out, class V>
std::vector<Out> line_of(const V& v, std::size_t rows, std::size_t cols,
                         bool row_axis, std::size_t index) {
    std::vector<Out> out;
    if (v.empty()) return out;
    const std::size_t n = row_axis ? cols : rows;
    out.reserve(n);
    for (std::size_t k = 0; k < n; ++k)
        out.push_back(static_cast<Out>(v[row_axis ? index * cols + k : k * cols + index]));
    return out;
}

// Whether a structural grid edit is allowed; never below 1x1.
inline bool grid_line_ok(std::size_t rows, std::size_t cols,
                         bool row_axis, bool insert, std::size_t index) {
    if (rows == 0 || cols == 0) return false;
    const std::size_t extent = row_axis ? rows : cols;
    if (insert ? (index > extent) : (index >= extent)) return false;
    return insert || extent > 1;
}

// Coordinate for a new grid line: midway between neighbours, or one spacing
// past the end.
inline double grid_coord_seed(const CowVec<double>& c, std::size_t at) {
    const std::size_t n = c.size();
    if (n == 0) return 0.0;
    if (at == 0) return c[0] - (n >= 2 ? c[1] - c[0] : 1.0);
    if (at >= n) return c[n - 1] + (n >= 2 ? c[n - 1] - c[n - 2] : 1.0);
    return 0.5 * (c[at - 1] + c[at]);
}

// The body of every MatrixLineEdit, checked by the caller: line e.index of
// `primary` and the buffers laid out like it (`alt`, null or empty = none;
// `labels`), plus the grid coordinate (`coord`, null for heatmaps), in or out.
// Fills the rest of `inv`. Non-empty hint_labels are padded to rows x cols
// first so they stay aligned; the inverse puts their size back.
template <class Prim, class Labels>
void matrix_line(const MatrixLineEdit& e, MatrixLineEdit& inv, std::size_t rows,
                 std::size_t cols, Prim& primary, CowVec<double>* alt, Labels& labels,
                 CowVec<double>* coord) {
    const bool insert = (e.op == MatrixLineEdit::Op::Insert);
    const bool row_ax = (e.axis == MatrixLineEdit::Axis::Row);
    const MatrixLineEdit::Restore* r = insert && e.restore ? &*e.restore : nullptr;

    inv.op = insert ? MatrixLineEdit::Op::Remove : MatrixLineEdit::Op::Insert;
    inv.restore.reset();
    inv.labels_size.reset();
    if (!labels.empty() && labels.size() != rows * cols) {
        inv.labels_size = labels.size();
        labels.resize(rows * cols);
    }
    if (!insert) {
        MatrixLineEdit::Restore made;
        made.values = line_of<double>(primary, rows, cols, row_ax, e.index);
        if (alt) made.alt = line_of<double>(*alt, rows, cols, row_ax, e.index);
        made.labels = line_of<std::string>(labels, rows, cols, row_ax, e.index);
        if (coord) made.coord = (*coord)[e.index];
        inv.restore = std::move(made);
    }

    // Coordinate first, while rows/cols still describe the buffers.
    if (coord) {
        if (insert) {
            const double at = r ? r->coord : grid_coord_seed(*coord, e.index);
            auto& cv = coord->mut();
            cv.insert(cv.begin() + static_cast<std::ptrdiff_t>(e.index), at);
        } else {
            row_remove(*coord, e.index);
        }
    }
    grid_reshape(primary, rows, cols, row_ax, insert, e.index, r ? &r->values : nullptr);
    if (alt) grid_reshape(*alt, rows, cols, row_ax, insert, e.index, r ? &r->alt : nullptr);
    grid_reshape(labels, rows, cols, row_ax, insert, e.index, r ? &r->labels : nullptr);
    if (e.labels_size) labels.resize(*e.labels_size);
}

} // namespace edits_detail

// Insert or remove a heatmap row/column, re-striding data and hint_labels.
template <class T>
std::optional<PlotDataOp> apply_plot_data_op(T& t, const MatrixLineEdit& e) {
    if (e.kind != PlotKind::Heatmap) return std::nullopt;
    auto* hpp = edits_detail::op_target(t.heatmaps, e);
    if (!hpp) return std::nullopt;
    auto& hp = *hpp;
    if (hp.rows <= 0 || hp.cols <= 0) return std::nullopt;

    const std::size_t rows = static_cast<std::size_t>(hp.rows);
    const std::size_t cols = static_cast<std::size_t>(hp.cols);
    // A buffer not matching its shape is left alone.
    if (hp.data.size() != rows * cols) return std::nullopt;

    const bool insert = (e.op == MatrixLineEdit::Op::Insert);
    const bool row_ax = (e.axis == MatrixLineEdit::Axis::Row);
    if (!edits_detail::grid_line_ok(rows, cols, row_ax, insert, e.index)) return std::nullopt;

    MatrixLineEdit inv = edits_detail::retarget(e, t.heatmaps, hp);
    edits_detail::matrix_line(e, inv, rows, cols, hp.data, nullptr, hp.opts.hint_labels, nullptr);
    if (row_ax) hp.rows += insert ? 1 : -1;
    else        hp.cols += insert ? 1 : -1;
    return inv;
}

// ---------------------------------------------------------------------------
// Applying data ops to a 3D axes' own plot objects (plane index -1). The
// `kind` guard keeps these and the 2D bodies from both firing. Same
// re-validation and inverse rules as above.
// ---------------------------------------------------------------------------

// Bar3D columns: 0 = u, 1 = v (`element` indexes the vector), 2 = heights,
// 3 = bottoms (`element` is i * |v| + j). Surfaces use columns 0-2.
template <class T>
std::optional<PlotDataOp> apply_axes3d_data_op(T& t, const PlotCellEdit& e) {
    using edits_detail::cell_undo;
    using edits_detail::op_target;
    using edits_detail::put_cell;
    // The column's vector of `p`, or null for a column it has not.
    auto cell = [&e](const auto& v, auto* p, auto pick) -> std::optional<PlotDataOp> {
        if (!p) return std::nullopt;
        CowVec<double>* col = pick(*p);
        if (!col) return std::nullopt;
        return cell_undo(e, v, *p, put_cell(*col, e.element, e.value));
    };
    if (e.kind == PlotKind::Bar3D)
        return cell(t.bars3d, op_target(t.bars3d, e), [&e](auto& b) -> CowVec<double>* {
            switch (e.column) {
                case 0: return &b.u;
                case 1: return &b.v;
                case 2: return &b.heights;
                case 3: return &b.bottoms;
                default: return nullptr;
            }
        });
    if (e.kind == PlotKind::Surface)
        return cell(t.surfaces, op_target(t.surfaces, e), [&e](auto& s) -> CowVec<double>* {
            switch (e.column) {
                case 0: return &s.u;
                case 1: return &s.v;
                case 2: return &s.heights;
                default: return nullptr;
            }
        });
    // Scatter3D, Line3D and SurfaceTri: x, y, z, colors (`element` is the
    // point or vertex index; colors is empty for a flat series). Editing a
    // vertex does not re-triangulate; topology is not editable.
    auto xyzc = [&e](auto& p) -> CowVec<double>* {
        switch (e.column) {
            case 0: return &p.x;
            case 1: return &p.y;
            case 2: return &p.z;
            case 3: return &p.colors;
            default: return nullptr;
        }
    };
    if (e.kind == PlotKind::Scatter3D) return cell(t.scatter3d, op_target(t.scatter3d, e), xyzc);
    if (e.kind == PlotKind::Line3D)    return cell(t.lines3d, op_target(t.lines3d, e), xyzc);
    if (e.kind == PlotKind::SurfaceTri)
        return cell(t.surface_tri, op_target(t.surface_tri, e), xyzc);
    return std::nullopt;
}

template <class T>
std::optional<PlotDataOp> apply_axes3d_data_op(T& t, const BarWidthEdit& e) {
    if (e.kind != PlotKind::Bar3D || (e.column != 0 && e.column != 1)) return std::nullopt;
    auto* b = edits_detail::op_target(t.bars3d, e);
    if (!b) return std::nullopt;
    double& w = e.column == 0 ? b->u_width : b->v_width;
    BarWidthEdit inv = edits_detail::retarget(e, t.bars3d, *b);
    inv.width = w;
    w = e.width;
    return inv;
}

namespace edits_detail {

// A grid kind's MatrixLineEdit: its coordinate vector and every matrix
// striding against it move together. `alt` is a second matrix (bar bottoms),
// null for none; an empty one stays empty.
template <class Vec, class P>
std::optional<PlotDataOp> grid_line_op(const MatrixLineEdit& e, const Vec& v, P& p,
                                       CowVec<double>& primary, CowVec<double>* alt) {
    const std::size_t rows = p.u.size(), cols = p.v.size();
    const bool insert = (e.op == MatrixLineEdit::Op::Insert);
    const bool row_ax = (e.axis == MatrixLineEdit::Axis::Row);
    if (!grid_line_ok(rows, cols, row_ax, insert, e.index)) return std::nullopt;
    // A mismatched buffer is left alone, as for heatmaps.
    if (primary.size() != rows * cols) return std::nullopt;
    if (alt && !alt->empty() && alt->size() != rows * cols) return std::nullopt;

    MatrixLineEdit inv = retarget(e, v, p);
    matrix_line(e, inv, rows, cols, primary, alt, p.opts.hint_labels, row_ax ? &p.u : &p.v);
    return inv;
}

} // namespace edits_detail

template <class T>
std::optional<PlotDataOp> apply_axes3d_data_op(T& t, const MatrixLineEdit& e) {
    using edits_detail::op_target;
    if (e.kind == PlotKind::Bar3D) {
        auto* bp = op_target(t.bars3d, e);
        if (!bp) return std::nullopt;
        return edits_detail::grid_line_op(e, t.bars3d, *bp, bp->heights, &bp->bottoms);
    }
    if (e.kind == PlotKind::Surface) {
        auto* sp = op_target(t.surfaces, e);
        if (!sp) return std::nullopt;
        return edits_detail::grid_line_op(e, t.surfaces, *sp, sp->heights, nullptr);
    }
    return std::nullopt;
}

template <class T>
std::optional<PlotDataOp> apply_axes3d_data_op(T&, const PlotRowEdit&) {
    // Not point-addressable for grids; MatrixLineEdit handles them.
    return std::nullopt;
}

template <class T>
std::optional<PlotDataOp> apply_axes3d_data_op(T& t, const TextDataEdit& e) {
    return edits_detail::text_op(t.texts, e);
}

// Applies an ordered op stream, prepending each op's inverse to `inv` (so it
// runs last-first). The 2D overload skips plane-addressed ops; the 3D overload
// (targets with planes) routes plane -1 to the axes' own objects and others to
// the plane's sheet.
template <class T>
void apply_plot_data_ops(T& t, const std::vector<PlotDataOp>& ops,
                         std::vector<PlotDataOp>* inv = nullptr) {
    for (const auto& op : ops) {
        if (plot_op_plane(op) >= 0) continue;
        auto undo = std::visit([&t](const auto& o) { return apply_plot_data_op(t, o); }, op);
        if (inv && undo) inv->insert(inv->begin(), std::move(*undo));
    }
}

template <class T>
    requires requires(T& t) { t.plane_at(std::size_t{0}).sheet; }
void apply_plot_data_ops(T& t, const std::vector<PlotDataOp>& ops,
                         std::vector<PlotDataOp>* inv = nullptr) {
    for (const auto& op : ops) {
        const int pi = plot_op_plane(op);
        std::optional<PlotDataOp> undo;
        if (pi < 0) {
            undo = std::visit([&t](const auto& o) { return apply_axes3d_data_op(t, o); }, op);
        } else {
            auto* plane = edits_detail::find_plane(t, pi, plot_op_plane_id(op));
            if (!plane) continue;
            auto& sheet = plane->sheet;
            undo = std::visit([&sheet](const auto& o) { return apply_plot_data_op(sheet, o); }, op);
            if (undo)
                std::visit([&](auto& u) {
                    u.plane_index = edits_detail::plane_index_of(t, plane);
                    u.plane_id = plane->id;
                }, *undo);
        }
        if (inv && undo) inv->insert(inv->begin(), std::move(*undo));
    }
}

// ---------------------------------------------------------------------------
// Plot-object appearance
// ---------------------------------------------------------------------------
// What assign_plot_opts() replaced: where the object is now, its id, and its
// old options (`hint_labels` left empty: the apply keeps them anyway).
template <class Opts>
struct ReplacedOpts {
    int      index = 0;
    ObjectId id    = 0;
    Opts     old;
};

// Assign an object's options, keeping its current `hint_labels` (data owned by
// the Data panel, which the options copy may have stale). Edits name objects
// as data ops do (find_object()). Nothing when the object is gone.
template <class Vec, class Opts>
std::optional<ReplacedOpts<Opts>> assign_plot_opts(Vec& v, int idx, ObjectId id, const Opts& o) {
    auto* p = edits_detail::find_object(v, idx, id);
    if (!p) return std::nullopt;
    if constexpr (!requires { p->opts.hint_labels; }) {
        // A text: nothing in its style is data.
        ReplacedOpts<Opts> r{ static_cast<int>(p - v.data()), p->id, std::move(p->opts) };
        p->opts = o;
        return r;
    } else {
        auto labels = std::move(p->opts.hint_labels);
        ReplacedOpts<Opts> r{ static_cast<int>(p - v.data()), p->id, std::move(p->opts) };
        r.old.hint_labels.clear();
        p->opts = o;
        p->opts.hint_labels = std::move(labels);
        return r;
    }
}

namespace edits_detail {

// The vector of a 2D holder that options type `O` styles.
template <class O, class T>
auto& styled_vector(T& t) {
    if constexpr (std::is_same_v<O, LineOptions>)         return t.lines;
    else if constexpr (std::is_same_v<O, ScatterOptions>) return t.scatters;
    else if constexpr (std::is_same_v<O, BarOptions>)     return t.bars;
    else if constexpr (std::is_same_v<O, HeatmapOptions>) return t.heatmaps;
    else if constexpr (std::is_same_v<O, TextStyle>)      return t.texts;
    else                                                  return t.scatter_z;
}

// Prepends `v` to `*inv`, if there is one: an inverse list runs last-first.
template <class V>
void prepend(std::vector<V>* inv, V v) {
    if (inv) inv->insert(inv->begin(), std::move(v));
}

} // namespace edits_detail

// The variant alternative picks the vector. Returns the inverse, addressed to
// the object as it is now (the caller fills in the plane).
template <class T>
std::optional<PlotStyleEdit> apply_plot_style_edit(T& t, const PlotStyleEdit& e) {
    return std::visit([&](const auto& o) -> std::optional<PlotStyleEdit> {
        using O = std::decay_t<decltype(o)>;
        auto r = assign_plot_opts(edits_detail::styled_vector<O>(t), e.plot_index, e.id, o);
        if (!r) return std::nullopt;
        PlotStyleEdit inv = e;
        inv.plot_index = r->index;
        inv.id = r->id;
        inv.opts = std::move(r->old);
        return inv;
    }, e.opts);
}

// As apply_plot_data_ops(): 2D targets take plane -1 only; 3D targets take
// plane-addressed edits only (their own objects use AxesEdit3D's lanes).
template <class T>
void apply_plot_style_edits(T& t, const std::vector<PlotStyleEdit>& es,
                            std::vector<PlotStyleEdit>* inv = nullptr) {
    for (const auto& e : es) {
        if (e.plane_index >= 0) continue;
        if (auto undo = apply_plot_style_edit(t, e)) edits_detail::prepend(inv, std::move(*undo));
    }
}

template <class T>
    requires requires(T& t) { t.plane_at(std::size_t{0}).sheet; }
void apply_plot_style_edits(T& t, const std::vector<PlotStyleEdit>& es,
                            std::vector<PlotStyleEdit>* inv = nullptr) {
    for (const auto& e : es) {
        if (e.plane_index < 0) continue;
        auto* plane = edits_detail::find_plane(t, e.plane_index, e.plane_id);
        if (!plane) continue;
        auto undo = apply_plot_style_edit(plane->sheet, e);
        // Snapshot only: the plane's cached raster must redraw.
        if constexpr (requires { plane->style_generation; })
            plane->style_generation = next_snapshot_generation();
        if (!undo) continue;
        undo->plane_index = edits_detail::plane_index_of(t, plane);
        undo->plane_id = plane->id;
        edits_detail::prepend(inv, std::move(*undo));
    }
}

// The setter-group edits shared by 2D and 3D axes (and in 3D the box and
// aspect), each group applied unless its setter ran after the snapshot the
// panel drew (StyleStamps). For Axes::Impl, Axes3D::Impl and both snapshots.
template <typename T, typename E>
void apply_style_edits(T& dst, const E& e, E* inv = nullptr) {
    const StyleStamps& have = dst.style_stamps;
    const StyleStamps& seen = e.style_seen;
    // The inverse's field for member `m`, or null.
    auto undo = [inv](auto E::* m) { return inv ? &(inv->*m) : nullptr; };
    auto put = [](auto& d, const auto& v, auto* old) {
        if (!v) return;
        if (old) *old = d;
        d = *v;
    };
    // Empty = back to auto; absent = unchanged.
    auto ticks = [](auto& d, const std::optional<std::vector<Tick>>& v,
                    std::optional<std::vector<Tick>>* old) {
        if (!v) return;
        if (old) *old = d ? *d : std::vector<Tick>{};
        d = v->empty() ? std::nullopt : std::optional(*v);
    };
    if (have.grid <= seen.grid) {
        put(dst.grid_enabled, e.grid_enabled, undo(&E::grid_enabled));
        put(dst.grid_opts,    e.grid_opts,    undo(&E::grid_opts));
    }
    if (have.style <= seen.style) put(dst.axes_style, e.axes_style, undo(&E::axes_style));
    if (have.legend <= seen.legend) {
        put(dst.legend_enabled, e.legend_enabled, undo(&E::legend_enabled));
        put(dst.legend_opts,    e.legend_opts,    undo(&E::legend_opts));
    }
    if (have.colorbar <= seen.colorbar)
        put(dst.colorbar_opts, e.colorbar_opts, undo(&E::colorbar_opts));
    if (have.xticks <= seen.xticks)
        ticks(dst.xticks_override, e.xticks_override, undo(&E::xticks_override));
    if (have.yticks <= seen.yticks)
        ticks(dst.yticks_override, e.yticks_override, undo(&E::yticks_override));
    if constexpr (requires { dst.zticks_override; e.zticks_override; })
        if (have.zticks <= seen.zticks)
            ticks(dst.zticks_override, e.zticks_override, undo(&E::zticks_override));
    if constexpr (requires { dst.box_style; e.box_style; }) {
        if (have.box <= seen.box)       put(dst.box_style, e.box_style, undo(&E::box_style));
        if (have.aspect <= seen.aspect) put(dst.aspect,    e.aspect,    undo(&E::aspect));
    }
}

// Apply a 2D edit to Axes::Impl (caller thread) or RenderSnapshot (render
// thread), as apply_axes3d_edit() does for 3D. The lanes' inverses commute:
// a style edit keeps `hint_labels`, a data op never touches options, and both
// find their object by id.
template <typename T>
void apply_axes_edit(T& dst, const AxesEdit& e, AxesEdit* inv = nullptr) {
    apply_title_edits(dst, e, inv);
    apply_limit_edits(dst, e, inv);
    apply_style_edits(dst, e, inv);
    apply_plot_data_ops(dst, e.plot_ops, inv ? &inv->plot_ops : nullptr);
    apply_plot_style_edits(dst, e.plot_styles, inv ? &inv->plot_styles : nullptr);
}

// Apply a 3D edit to Axes3D::Impl (caller thread) or RenderSnapshot3D (render
// thread); one template so both paths stay identical.
template <typename T>
void apply_axes3d_edit(T& dst, const AxesEdit3D& e, AxesEdit3D* inv = nullptr) {
    apply_title_edits(dst, e, inv);
    apply_limit_edits(dst, e, inv);
    apply_style_edits(dst, e, inv);
    if (e.camera && dst.camera_stamp <= e.camera_seen) {
        if (inv) inv->camera = dst.camera;
        dst.camera = *e.camera;
    }

    // Plane-addressed edits: each finds its plane and object by id (or index).
    apply_plot_style_edits(dst, e.plot_styles, inv ? &inv->plot_styles : nullptr);

    for (const auto& pe : e.planes) {
        if (pe.plane_index < 0) continue;
        auto* p = edits_detail::find_plane(dst, pe.plane_index, pe.id);
        if (!p) continue;
        // A setter since the panel drew wins.
        if (p->placement_stamp > pe.seen) continue;
        AxesEdit3D::PlaneEdit undo;
        undo.plane_index = edits_detail::plane_index_of(dst, p);
        undo.id = p->id;
        if (pe.orient) { undo.orient = p->orient; p->orient = *pe.orient; }
        if (pe.offset) { undo.offset = p->offset; p->offset = *pe.offset; }
        if (pe.opts)   { undo.opts   = p->opts;   p->opts   = *pe.opts; }
        if (inv) edits_detail::prepend(&inv->planes, std::move(undo));
    }

    // The axes' own 3D objects; `hint_labels` is kept (it is data).
    auto objects = [](auto& plots, const auto& edits, auto* undo) {
        for (const auto& oe : edits) {
            if (!oe.opts) continue;
            auto r = assign_plot_opts(plots, oe.plot_index, oe.id, *oe.opts);
            if (!r || !undo) continue;
            auto u = oe;
            u.plot_index = r->index;
            u.id = r->id;
            u.opts = std::move(r->old);
            edits_detail::prepend(undo, std::move(u));
        }
    };
    objects(dst.bars3d, e.bars3d, inv ? &inv->bars3d : nullptr);
    objects(dst.surfaces, e.surfaces, inv ? &inv->surfaces : nullptr);
    objects(dst.scatter3d, e.scatter3d, inv ? &inv->scatter3d : nullptr);
    objects(dst.lines3d, e.lines3d, inv ? &inv->lines3d : nullptr);
    objects(dst.surface_tri, e.surface_tri, inv ? &inv->surface_tri : nullptr);
    objects(dst.texts, e.texts, inv ? &inv->texts : nullptr);
    apply_plot_data_ops(dst, e.plot_ops, inv ? &inv->plot_ops : nullptr);
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

// What two edits share when they address the same object: its id, or without
// one its position. Ids never collide, so across a reorder the edits of one
// object still merge.
inline std::tuple<int, int, ObjectId> object_key(int plane_index, int index, ObjectId id) {
    if (id != 0) return { 0, 0, id };
    return { plane_index, index, 0 };
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
            return std::tuple(edits_detail::object_key(p.plane_index, p.plot_index, p.id),
                              p.opts.index());
        });
    if constexpr (requires { src.box_style; }) {
        take_latest(dst.zticks_override, src.zticks_override, ds.zticks, ss.zticks);
        take_latest(dst.box_style, src.box_style, ds.box, ss.box);
        take_latest(dst.aspect, src.aspect, ds.aspect, ss.aspect);
        for (const auto& pe : src.planes) {
            auto it = std::find_if(dst.planes.begin(), dst.planes.end(),
                                   [&](const auto& d) {
                                       return edits_detail::object_key(-1, d.plane_index, d.id)
                                              == edits_detail::object_key(-1, pe.plane_index, pe.id);
                                   });
            if (it == dst.planes.end()) { dst.planes.push_back(pe); continue; }
            if (pe.orient) it->orient = pe.orient;
            if (pe.offset) it->offset = pe.offset;
            if (pe.opts)   it->opts   = pe.opts;
            it->seen = pe.seen;
            it->plane_index = pe.plane_index;   // the latest position of the same plane
        }
        auto by_object = [](const auto& o) { return edits_detail::object_key(-1, o.plot_index, o.id); };
        for (const auto& o : src.bars3d)      upsert(dst.bars3d, o, by_object);
        for (const auto& o : src.surfaces)    upsert(dst.surfaces, o, by_object);
        for (const auto& o : src.scatter3d)   upsert(dst.scatter3d, o, by_object);
        for (const auto& o : src.lines3d)     upsert(dst.lines3d, o, by_object);
        for (const auto& o : src.surface_tri) upsert(dst.surface_tri, o, by_object);
        for (const auto& o : src.texts)       upsert(dst.texts, o, by_object);
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
                && e.lines3d.empty() && e.surface_tri.empty() && e.texts.empty();
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
    take_latest(dst.background, src.background, ds.background, ss.background);
    // No setter writes the gaps after create(), so they need no stamp.
    if (src.col_gap) dst.col_gap = src.col_gap;
    if (src.row_gap) dst.row_gap = src.row_gap;
}

// Applies the figure-level fields to Figure::Impl's (or a FigureSnapshot's)
// members, each unless its setter ran after the snapshot the panel drew.
// `have` is the destination's FigureStamps. Grid ratios are applied by the
// caller (they need the grid shape).
template <class Suptitle, class Opts, class Margins, class Background, class Gap>
void apply_figure_edits(const FigureEdits& e, const FigureStamps& have,
                        Suptitle& suptitle, Opts& suptitle_opts, Margins& margins,
                        Background& background, Gap& col_gap, Gap& row_gap,
                        FigureEdits* inv = nullptr) {
    const FigureStamps& seen = e.fig_seen;
    auto put = [](auto& d, const auto& v, auto* old) {
        if (old) *old = d;
        d = *v;
    };
    if (e.suptitle && have.suptitle <= seen.suptitle)
        put(suptitle, e.suptitle, inv ? &inv->suptitle : nullptr);
    if (e.suptitle_opts && have.suptitle_style <= seen.suptitle_style)
        put(suptitle_opts, e.suptitle_opts, inv ? &inv->suptitle_opts : nullptr);
    if (e.margins && have.margins <= seen.margins)
        put(margins, e.margins, inv ? &inv->margins : nullptr);
    if (e.background && have.background <= seen.background)
        put(background, e.background, inv ? &inv->background : nullptr);
    if (e.col_gap) put(col_gap, e.col_gap, inv ? &inv->col_gap : nullptr);
    if (e.row_gap) put(row_gap, e.row_gap, inv ? &inv->row_gap : nullptr);
}

// True if a per-axes edit (or inverse) carries nothing at all.
template <class E>
bool axes_edit_empty(const E& e) {
    TitleEdits t;
    merge_title_edits(t, e);
    LimitEdits l;
    merge_limit_edits(l, e);
    bool empty = t.empty() && l.empty() && style_edits_empty(e) && e.plot_ops.empty();
    if constexpr (requires { e.camera; }) empty = empty && !e.camera;
    return empty;
}

// ---------------------------------------------------------------------------
// Composing inverses. A host coalescing a gesture that spans
// frames folds each frame's inverse into the gesture's: `older` undoes the
// earlier frames, `newer` the latest. The result undoes both: the older value
// of every field wins, and the newer data ops run first. When a gesture ends
// is the host's call.
// ---------------------------------------------------------------------------

namespace edits_detail {

template <class V>
void keep_older(std::optional<V>& older, const std::optional<V>& newer) {
    if (!older) older = newer;
}

// Entries addressed to an object: the older one for an object wins; the newer
// entries for other objects run first.
template <class Vec, class Key>
void compose_entries(Vec& older, const Vec& newer, Key key) {
    Vec made;
    for (const auto& n : newer) {
        const bool covered = std::any_of(older.begin(), older.end(),
                                         [&](const auto& o) { return key(o) == key(n); });
        if (!covered) made.push_back(n);
    }
    older.insert(older.begin(), made.begin(), made.end());
}

} // namespace edits_detail

template <class E>
void compose_axes_inverse(E& older, const E& newer) {
    using edits_detail::keep_older;
    keep_older(older.title, newer.title);
    keep_older(older.xtitle, newer.xtitle);
    keep_older(older.ytitle, newer.ytitle);
    keep_older(older.xlim_auto, newer.xlim_auto);
    keep_older(older.ylim_auto, newer.ylim_auto);
    keep_older(older.xmin, newer.xmin);
    keep_older(older.xmax, newer.xmax);
    keep_older(older.ymin, newer.ymin);
    keep_older(older.ymax, newer.ymax);
    keep_older(older.grid_enabled, newer.grid_enabled);
    keep_older(older.grid_opts, newer.grid_opts);
    keep_older(older.axes_style, newer.axes_style);
    keep_older(older.legend_enabled, newer.legend_enabled);
    keep_older(older.legend_opts, newer.legend_opts);
    keep_older(older.colorbar_opts, newer.colorbar_opts);
    keep_older(older.xticks_override, newer.xticks_override);
    keep_older(older.yticks_override, newer.yticks_override);
    older.plot_ops.insert(older.plot_ops.begin(), newer.plot_ops.begin(), newer.plot_ops.end());
    edits_detail::compose_entries(older.plot_styles, newer.plot_styles,
                                  [](const PlotStyleEdit& p) {
        return std::tuple(edits_detail::object_key(p.plane_index, p.plot_index, p.id),
                          p.opts.index());
    });
    if constexpr (requires { older.box_style; }) {
        keep_older(older.ztitle, newer.ztitle);
        keep_older(older.zlim_auto, newer.zlim_auto);
        keep_older(older.zmin, newer.zmin);
        keep_older(older.zmax, newer.zmax);
        keep_older(older.zticks_override, newer.zticks_override);
        keep_older(older.box_style, newer.box_style);
        keep_older(older.aspect, newer.aspect);
        keep_older(older.camera, newer.camera);
        // A plane's fields one by one: the older entry may hold only some.
        for (const auto& pe : newer.planes) {
            auto it = std::find_if(older.planes.begin(), older.planes.end(), [&](const auto& o) {
                return edits_detail::object_key(-1, o.plane_index, o.id)
                       == edits_detail::object_key(-1, pe.plane_index, pe.id);
            });
            if (it == older.planes.end()) { older.planes.insert(older.planes.begin(), pe); continue; }
            keep_older(it->orient, pe.orient);
            keep_older(it->offset, pe.offset);
            keep_older(it->opts, pe.opts);
        }
        auto by_object = [](const auto& o) { return edits_detail::object_key(-1, o.plot_index, o.id); };
        edits_detail::compose_entries(older.bars3d, newer.bars3d, by_object);
        edits_detail::compose_entries(older.surfaces, newer.surfaces, by_object);
        edits_detail::compose_entries(older.scatter3d, newer.scatter3d, by_object);
        edits_detail::compose_entries(older.lines3d, newer.lines3d, by_object);
        edits_detail::compose_entries(older.surface_tri, newer.surface_tri, by_object);
        edits_detail::compose_entries(older.texts, newer.texts, by_object);
    }
}

inline void compose_inverse(FigureEdits& older, const FigureEdits& newer) {
    using edits_detail::keep_older;
    auto lane = [](auto& o, const auto& n) {
        for (const auto& [idx, e] : n) {
            auto it = std::find_if(o.begin(), o.end(), [idx](const auto& p) { return p.first == idx; });
            if (it == o.end()) o.push_back({idx, e});
            else compose_axes_inverse(it->second, e);
        }
    };
    lane(older.per_axes, newer.per_axes);
    lane(older.per_axes3d, newer.per_axes3d);
    keep_older(older.suptitle, newer.suptitle);
    keep_older(older.suptitle_opts, newer.suptitle_opts);
    keep_older(older.margins, newer.margins);
    keep_older(older.background, newer.background);
    keep_older(older.col_gap, newer.col_gap);
    keep_older(older.row_gap, newer.row_gap);
    keep_older(older.col_ratios, newer.col_ratios);
    keep_older(older.row_ratios, newer.row_ratios);
}

} // namespace sextant
