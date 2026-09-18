#pragma once
#include "sextant/style.h"
#include "plot_objects.h"
#include "tick.h"
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace sextant {

// One scalar the Data panel changed, addressed by (kind, index-within-that-
// kind's-vector, column, element) since plot objects carry no identity of
// their own. Column is 0 = x/centers, 1 = y/heights, 2 = z, and is unused for
// Heatmap, whose `element` is the row-major index row*cols + col.
//
// A 3D slot holds its plot objects on planes rather than on the axes, so the
// address gained one field in step 6b: `plane_index` says which plane the
// (kind, plot_index) pair indexes into, and -1 means the axes itself -- the
// only form a 2D slot ever produces. It is last and defaulted in every op
// below, so every existing positional construction keeps meaning what it
// meant, and an op recorded before this existed replays unchanged.
struct PlotCellEdit {
    PlotKind    kind;
    int         plot_index;
    int         column;
    std::size_t element;
    double      value;
    int         plane_index = -1;
};

// A structural change to one plot object's parallel data arrays: insert a new
// point at `row`, or remove the point there. Every array of that plot moves in
// lockstep (x/y, x/y/z, centers/heights, plus the index-aligned hint_labels
// and error-bar vectors) so they can never desynchronize.
//
// Heatmaps are not addressable here -- a matrix has no "point" to insert
// without also choosing row-vs-column and re-striding. That is
// MatrixLineEdit's job.
struct PlotRowEdit {
    enum class Op { Insert, Remove };
    Op          op         = Op::Insert;
    PlotKind    kind       = PlotKind::Line;
    int         plot_index = 0;
    // Insert: the index the new point will occupy (== the old size to append).
    // Its values are copied from row-1 where that exists, so the new point
    // lands on top of its neighbour and the plot's shape is undisturbed until
    // the user edits it; at row 0, or into an empty plot, it is zero-filled.
    // Remove: the index to erase.
    std::size_t row        = 0;
    int         plane_index = -1;   // see PlotCellEdit
};

// The appearance of one 2D plot object, from that object's own tab in the
// Data panel (v1.0 step 11.6).
//
// The 3D kinds have had this since step 7d -- AxesEdit3D::Bar3DEdit and
// SurfaceEdit -- and the 2D ones had nothing at all: a LinePlot's or a
// HeatmapPlot's options could not be edited from any panel, and neither could
// a plane's, since PlaneEdit carries only the plane's own placement. What
// forced the gap closed is that `colorbar` and `show_legend` are per-object
// flags with nowhere to be toggled from.
//
// Addressed exactly as a PlotDataOp is -- (plane, index), plane -1 meaning the
// axes itself -- because it names the same object, and two addressing schemes
// for one object are two chances to disagree about which one a control edits.
//
// There is deliberately **no `kind` field**, unlike PlotCellEdit: the variant's
// own alternative names the kind, and a separate tag could disagree with it.
// PlotCellEdit needs one because a `double value` carries no type.
//
// The whole options struct rather than per-field deltas, as everywhere else on
// this channel: the panel keeps a local copy and republishes it on any change.
struct PlotStyleEdit {
    int plot_index  = 0;
    int plane_index = -1;
    std::variant<LineOptions, ScatterOptions, BarOptions,
                 HeatmapOptions, ScatterZOptions> opts;
};

// Insert or remove a whole matrix row / column of a HeatmapPlot, re-striding
// the row-major buffer (and HeatmapOptions::hint_labels, which shares that
// layout) around the change.
//
// Never allowed below 1x1: Axes::heatmap() rejects rows<1 || cols<1, so the
// panel must not be able to manufacture a shape the public API would refuse.
// New lines copy their predecessor, as PlotRowEdit's inserts do, so the
// picture does not jump until a value is actually edited; at index 0 there is
// no predecessor and they are zero-filled.
// A `bar3d` grid rides this too (step 6c): its heights are a |u| x |v|
// row-major matrix with u as the major index, so "row" is a u line and
// "column" a v line, and the same re-striding applies. What differs is that a
// bar3d line also owns a *coordinate* -- the u or v value the new line sits
// at -- which a heatmap's does not, its extent being the axis.
struct MatrixLineEdit {
    enum class Op   { Insert, Remove };
    enum class Axis { Row, Col };
    Op          op         = Op::Insert;
    Axis        axis       = Axis::Row;
    int         plot_index = 0;
    // Insert: the index the new line will occupy (== rows/cols to append).
    // Remove: the index to erase.
    std::size_t index      = 0;
    int         plane_index = -1;   // see PlotCellEdit
    // Which gridded kind this addresses. Last and defaulted, like
    // plane_index, so every construction written before bar3d arrived still
    // names a heatmap.
    PlotKind    kind       = PlotKind::Heatmap;
};

// BarPlot::bar_width is a single per-plot scalar rather than a per-bar column,
// so it can't ride in a PlotCellEdit — but it is still plot *data*, and is
// journaled and replayed alongside the rest.
//
// A `bar3d` grid has two of them, `u_width` and `v_width`, which is what
// `column` selects; a 2D bar has one and ignores it.
struct BarWidthEdit {
    int    plot_index  = 0;
    double width       = 1.0;
    int    plane_index = -1;   // see PlotCellEdit
    PlotKind kind      = PlotKind::Bar;
    int    column      = 0;    // Bar3D: 0 = along u, 1 = along v
};

// One entry of a plot object's edit stream. Ordering across the whole stream
// is load-bearing, and the reason these live in one sequence rather than in
// per-kind buckets: every index is relative to the arrays as they stood when
// the op was recorded, so a cell edit staged before a row insert must be
// applied before it too. (Clicking a row button deactivates the focused
// input, so both routinely land in the same frame.)
using PlotDataOp = std::variant<PlotCellEdit, PlotRowEdit, MatrixLineEdit, BarWidthEdit>;

// The one field every alternative shares, read without caring which it is.
// Here rather than at each call site because "which plane is this op for" is
// asked by the router, the journal and the panel alike, and a std::visit
// written three times is three chances to add a fifth op and forget one.
inline int plot_op_plane(const PlotDataOp& op) {
    return std::visit([](const auto& o) { return o.plane_index; }, op);
}

// One axes slot's worth of pending widget-panel edits. Every field is
// optional: absent = untouched this round. For xticks_override/
// yticks_override the *outer* optional means "the tick table was touched
// since the last drain"; an empty inner vector means "clear the override,
// revert to auto ticks" (distinct from "not touched").
struct AxesEdit {
    std::optional<std::string> title, xtitle, ytitle;
    std::optional<bool>   xlim_auto, ylim_auto, grid_enabled;
    std::optional<double> xmin, xmax, ymin, ymax;
    std::optional<std::vector<Tick>> xticks_override, yticks_override;
    std::optional<AxesStyle> axes_style;

    // Each mirrors a whole options struct rather than per-field
    // deltas, matching how axes_style above has always worked — the panel
    // keeps a local copy and republishes it on any change.
    std::optional<GridOptions>     grid_opts;
    std::optional<bool>            legend_enabled;
    std::optional<LegendOptions>   legend_opts;
    std::optional<ColorbarOptions> colorbar_opts;

    // Data panel. Appended to (never cleared), so an empty vector
    // simply means "no data was committed since the last drain" — no outer
    // optional needed, unlike the tick overrides above where empty-vs-absent
    // carries meaning. Strictly ordered; see PlotDataOp.
    std::vector<PlotDataOp> plot_ops;

    // Per-plot-object appearance, from the same panel and on the same
    // append-only terms (v1.0 step 11.6). Separate from plot_ops because that
    // stream exists to replay *data* onto the caller thread and this does not:
    // a style edit is applied wherever it lands and has no journal.
    std::vector<PlotStyleEdit> plot_styles;
};

// Keyed by AxesSlot::index (1-indexed, unique within one Figure). A
// vector-of-pairs rather than a map: slot counts are tiny (subplot grids,
// not thousands of axes).
// The 3D counterpart, and a separate struct rather than extra fields on
// AxesEdit for the reason RenderSnapshot3D is separate from RenderSnapshot: a
// slot is one kind or the other, so one struct with two disjoint halves would
// be half-meaningless whichever slot it addressed. The overlap that does exist
// (titles, grid, axes_style) is genuine -- those mean the same thing to both.
//
// plot_ops arrived with the planes: an Axes3D holds no plot objects of its
// own, but every plane does, and each op names the plane it belongs to
// (PlotCellEdit::plane_index). One stream per slot rather than one per plane,
// for the reason PlotDataOp is one stream rather than per-kind buckets --
// order across the whole stream is what makes an index recorded before a
// structural op still mean what it meant.
struct AxesEdit3D {
    std::optional<std::string> title, xtitle, ytitle, ztitle;
    std::optional<bool>   xlim_auto, ylim_auto, zlim_auto, grid_enabled;
    std::optional<double> xmin, xmax, ymin, ymax, zmin, zmax;
    std::optional<std::vector<Tick>> xticks_override, yticks_override, zticks_override;

    std::optional<AxesStyle>   axes_style;
    std::optional<GridOptions> grid_opts;

    // The hoisted decoration's switch and cosmetics, the same three AxesEdit
    // carries and meaning the same thing (v1.0 step 11.2). A colorbar's
    // styling could not ride this channel before, because it was not the
    // axes' -- it was read off whichever plane had asked, where nothing could
    // set it.
    std::optional<bool>            legend_enabled;
    std::optional<LegendOptions>   legend_opts;
    std::optional<ColorbarOptions> colorbar_opts;

    // The camera rides this channel rather than living in PanelState, which
    // is what makes a dragged view survive refresh() and lets a caller script
    // the same thing through Axes3D::set_camera(). It is the exact analogue of
    // pan/zoom pushing limits.
    std::optional<Camera3D>   camera;
    std::optional<Box3DStyle> box_style;
    std::optional<BoxAspect>  aspect;

    // One plane's placement, from a plane's own tab in the Data panel. A
    // vector rather than one optional-per-field on AxesEdit3D because an
    // axes holds any number of planes; `plane_index` is positional, exactly
    // as a plot object's index is, and an entry naming a plane that has since
    // gone is skipped rather than clamped.
    struct PlaneEdit {
        int plane_index = 0;
        std::optional<PlaneOrientation> orient;
        std::optional<double>           offset;
        // The whole options struct, matching how axes_style and grid_opts
        // above work: the panel keeps a local copy and republishes it.
        std::optional<Plane2DOptions>   opts;
    };
    std::vector<PlaneEdit> planes;

    // The appearance of one `bar3d` grid or one surface, from the Appearance
    // block in its Data-panel tab (step 7d, in the Controls panel until 10.3;
    // the bar3d one is the row step 6c recorded as deliberately missing). Vectors and positional indices for exactly
    // PlaneEdit's reason: an axes holds any number of each.
    //
    // The whole options struct, as everywhere else on this channel -- but see
    // apply_axes3d_edit(), which restores `hint_labels` afterwards. Those are
    // *data*, edited in the Data panel and re-strided by a MatrixLineEdit, and
    // a stale copy riding an appearance edit would silently revert them.
    struct Bar3DEdit   { int plot_index = 0; std::optional<Bar3DOptions>   opts; };
    struct SurfaceEdit { int plot_index = 0; std::optional<SurfaceOptions> opts; };
    // And one scatter3d cloud (v1.0 step 12.8), on identical terms.
    struct Scatter3DEdit { int plot_index = 0; std::optional<Scatter3DOptions> opts; };
    // And one line3d path (v1.0 step 13.5), on identical terms.
    struct Line3DEdit  { int plot_index = 0; std::optional<Line3DOptions>  opts; };
    // And one triangulated mesh (v1.0 step 14.4), on identical terms.
    struct SurfaceTriEdit { int plot_index = 0; std::optional<SurfaceTriOptions> opts; };
    std::vector<Bar3DEdit>     bars3d;
    std::vector<SurfaceEdit>   surfaces;
    std::vector<Scatter3DEdit> scatter3d;
    std::vector<Line3DEdit>    lines3d;
    std::vector<SurfaceTriEdit> surface_tri;

    // Data panel, addressed at a plane. Appended to, never cleared -- the
    // same reading AxesEdit::plot_ops has.
    std::vector<PlotDataOp> plot_ops;

    // One plane's plot object's appearance, addressed at the plane (v1.0 step
    // 11.6). What sits directly on the axes goes down `bars3d` and `surfaces`
    // above instead, so an entry here always names a plane.
    std::vector<PlotStyleEdit> plot_styles;
};

struct FigureEdits {
    std::vector<std::pair<int, AxesEdit>>   per_axes;
    std::vector<std::pair<int, AxesEdit3D>> per_axes3d;

    // Figure-level, so deliberately outside per_axes: one suptitle spans the
    // whole subplot grid, and the panel edits it from whichever axes happens
    // to be selected.
    std::optional<std::string>     suptitle;
    std::optional<SuptitleOptions> suptitle_opts;

    // Figure-level for the same reason the suptitle is: the margins
    // are the border around the whole grid and the gaps are between
    // subplots, so neither belongs to any one axes slot.
    std::optional<FigureMargins> margins;
    std::optional<float>         col_gap, row_gap;

    // The grid's weights, from dragging a boundary between subplots or the
    // Layout fields (v1.0 step 15.3). Whole vectors, as everywhere on this lane.
    // Unlike the rest of it they are journaled, so a drag survives refresh().
    std::optional<std::vector<float>> col_ratios, row_ratios;

    // FigureEditBox's drain tests this, not per_axes.empty(): a
    // figure-level-only edit carries no per-axes entry and would otherwise be
    // dropped. Every field above has to be listed. Missing one is not a
    // compile error, it is a control that silently does nothing until some
    // unrelated per-axes edit happens to ride along with it.
    bool empty() const {
        return per_axes.empty() && per_axes3d.empty() && !suptitle && !suptitle_opts
               && !margins && !col_gap && !row_gap && !col_ratios && !row_ratios;
    }
};

// Whether a drain carries nothing but navigation, and so leaves the stored
// layout alone (v1.0 step 15.2; see FigureSnapshot::layout_generation). An
// allow-list, deliberately: a field added to either edit struct later counts
// as a change until someone decides otherwise, which costs a refit rather
// than a stale layout.
//
// 2D: limits and tick overrides, whose only layout effect is the tick labels.
// 3D: the camera, which has none -- a 3D cell's labels are inside its frame.
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

// The data ops the render thread has already folded into the published
// snapshot, kept so the caller thread can replay them onto the authoritative
// Axes::Impl. Only *data* is journaled, not AxesEdit's appearance/limit
// fields -- see FigureEditBox -- plus, since v1.0 step 15.3, the grid's weights.
struct PlotDataJournal {
    std::vector<std::pair<int, std::vector<PlotDataOp>>> per_axes;

    // The grid's weights as last dragged (v1.0 step 15.3): the one figure-level
    // edit journaled, and only its latest value, since a drag restages the whole
    // vector every frame and only where it ended matters.
    std::optional<std::vector<float>> col_ratios, row_ratios;

    bool empty() const { return per_axes.empty() && !col_ratios && !row_ratios; }
};

// ---------------------------------------------------------------------------
// Applying data ops
//
// Each overload targets either an Axes::Impl (caller thread) or a
// RenderSnapshot (render thread). Templated because those two declare the same
// five plot vectors under the same names, so one body keeps the two drain
// paths semantically identical -- which is what makes an op journaled on one
// thread and replayed on the other produce the same result.
//
// Every index is re-validated here and out-of-range ops are silently skipped:
// the panel indexes frame N's snapshot while this runs against frame N+1, and
// throwing is not an option because the render-thread path runs on
// WindowThread, which has no exception barrier.
//
// Plot bulk data is a CowVec, so every write goes through mut(). Note the
// ordering in each case: validate the index *before* calling mut(), so an
// out-of-range op costs no clone.
// ---------------------------------------------------------------------------

namespace edits_detail {

// Uniform "give me something writable" for the templates below, which have to
// work over both a CowVec (the plot data) and the plain std::vector<std::string>
// that opts.hint_labels still is.
template <class T> std::vector<T>& mut_ref(CowVec<T>& v)      { return v.mut(); }
template <class T> std::vector<T>& mut_ref(std::vector<T>& v) { return v; }

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
            if (pi >= t.lines.size()) return;
            put(e.column == 0 ? t.lines[pi].x : t.lines[pi].y, e.element, e.value);
            return;
        case PlotKind::Scatter:
            if (pi >= t.scatters.size()) return;
            put(e.column == 0 ? t.scatters[pi].x : t.scatters[pi].y, e.element, e.value);
            return;
        case PlotKind::Bar:
            if (pi >= t.bars.size()) return;
            put(e.column == 0 ? t.bars[pi].centers : t.bars[pi].heights, e.element, e.value);
            return;
        case PlotKind::ScatterZ: {
            if (pi >= t.scatter_z.size()) return;
            auto& sp = t.scatter_z[pi];
            put(e.column == 0 ? sp.x : (e.column == 1 ? sp.y : sp.z), e.element, e.value);
            return;
        }
        case PlotKind::Heatmap: {
            if (pi >= t.heatmaps.size()) return;
            auto& hp = t.heatmaps[pi];
            // Re-check the shape too: `element` was folded as row*cols+col
            // against the dimensions the panel saw, which may have changed —
            // and now *can* change structurally, via MatrixLineEdit.
            if (hp.rows <= 0 || hp.cols <= 0) return;
            const std::size_t n = static_cast<std::size_t>(hp.rows)
                                * static_cast<std::size_t>(hp.cols);
            if (e.element >= n || e.element >= hp.data.size()) return;
            hp.data.mut()[e.element] = static_cast<float>(e.value);
            return;
        }
        case PlotKind::Bar3D:
            // Native to a 3D axes, so it never indexes any of the five
            // vectors above -- see apply_axes3d_data_op() below.
            return;
    }
}

template <class T>
void apply_plot_data_op(T& t, const BarWidthEdit& e) {
    if (e.kind != PlotKind::Bar || e.plot_index < 0) return;
    const std::size_t pi = static_cast<std::size_t>(e.plot_index);
    if (pi < t.bars.size()) t.bars[pi].bar_width = e.width;
}

namespace edits_detail {

// Insert at `row`, seeding from row-1 when `copy_prev` and such a row exists,
// else value-initialized. Erase at `row`. Both silently no-op when the index
// does not fit this particular vector -- the arrays can have been resized out
// from under a stale edit.
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

    // One structural change applied to every parallel array of a plot object
    // at once, which is what keeps x/y/z from ever desynchronizing. A new
    // point copies its predecessor so the plotted shape is undisturbed until
    // the user edits it.
    auto required = [&](auto&... cols) {
        if (insert) (edits_detail::row_insert(cols, e.row, true), ...);
        else        (edits_detail::row_remove(cols, e.row), ...);
    };

    // Index-aligned companions the plot may or may not have: hint_labels and
    // the eight error-bar vectors. They must move too, or everything past the
    // edit point slides onto the wrong point -- invisible until someone reads
    // the plot. Each is left strictly alone when empty, since empty means
    // "this plot has none of these at all".
    //
    // `copy_prev` differs between the two deliberately: a new point gets a
    // *blank* hint label (inheriting someone else's "Peak" is an assertion
    // nobody made) but a *copy* of its neighbour's error data, because the
    // inserted point is itself a copy of that neighbour and a measurement
    // copied is copied with its uncertainty. A zero would draw nothing rather
    // than something wrong, but it would still claim the copy is exact.
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
            if (pi < t.lines.size()) {
                auto& lp = t.lines[pi];
                required(lp.x, lp.y);
                labels(lp.opts.hint_labels);
                err_cols(lp.err);
            }
            break;
        case PlotKind::Scatter:
            if (pi < t.scatters.size()) {
                auto& sp = t.scatters[pi];
                required(sp.x, sp.y);
                labels(sp.opts.hint_labels);
                err_cols(sp.err);
            }
            break;
        case PlotKind::Bar:
            if (pi < t.bars.size()) {
                auto& bp = t.bars[pi];
                required(bp.centers, bp.heights);
                labels(bp.opts.hint_labels);
                err_cols(bp.err);
            }
            break;
        case PlotKind::ScatterZ:
            if (pi < t.scatter_z.size()) {
                auto& sp = t.scatter_z[pi];
                required(sp.x, sp.y, sp.z);
                labels(sp.opts.hint_labels);
                err_cols(sp.err);
            }
            break;
        case PlotKind::Heatmap:
        case PlotKind::Bar3D:
            // Not addressable — see PlotRowEdit. Neither is a list of points:
            // a matrix and a bar grid both change shape a whole line at a
            // time, which is MatrixLineEdit's job. The Data panel never emits
            // one of these for either; ignore it if one ever arrives.
            break;
    }
}

namespace edits_detail {

// Insert or remove one whole line of a rows x cols row-major buffer,
// re-striding it around the change. `row_axis` names the major index, so a
// row is `cols` contiguous elements and a column is one element per row.
//
// Shared by the two gridded kinds -- a heatmap's matrix and a bar3d's heights
// -- because the layout is the same layout and a second transcription of a
// splice loop written bottom-up for a reason is a second chance to write it
// top-down. Both also carry an index-aligned hint_labels through it, which
// must move in lockstep or every label past the change lands on the wrong
// cell. An empty buffer is left alone: empty means "this plot has none".
template <class V>
void grid_reshape(V& v, std::size_t rows, std::size_t cols,
                  bool row_axis, bool insert, std::size_t index) {
    using Value = typename std::decay_t<decltype(v)>::value_type;
    if (v.empty()) return;
    auto& vv = mut_ref(v);
    if (row_axis) {
        const std::size_t at = index * cols;
        if (insert) {
            // Seed from the row above; at row 0 there is none, so the new
            // row is value-initialized.
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
        // Walk bottom-up so each splice happens at a higher offset than
        // the next one: offsets computed with the *original* stride stay
        // valid for every row still to come.
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

// Whether a structural edit of a rows x cols grid is one the panel is allowed
// to make. Never below 1x1 on either kind -- Axes::heatmap() and
// Axes3D::bar3d() both reject that shape outright, so the panel must not be
// able to manufacture one the public API would refuse, and a grid shrunk to
// zero on either axis cannot be regrown from here.
inline bool grid_line_ok(std::size_t rows, std::size_t cols,
                         bool row_axis, bool insert, std::size_t index) {
    if (rows == 0 || cols == 0) return false;
    const std::size_t extent = row_axis ? rows : cols;
    if (insert ? (index > extent) : (index >= extent)) return false;
    return insert || extent > 1;
}

} // namespace edits_detail

// Inserts or removes a whole matrix row / column, re-striding the row-major
// buffer around the change. HeatmapOptions::hint_labels shares that exact
// layout (index = row*cols + col), so it is re-strided in lockstep or every
// label past the change lands on the wrong cell.
template <class T>
void apply_plot_data_op(T& t, const MatrixLineEdit& e) {
    if (e.kind != PlotKind::Heatmap || e.plot_index < 0) return;
    const std::size_t pi = static_cast<std::size_t>(e.plot_index);
    if (pi >= t.heatmaps.size()) return;
    auto& hp = t.heatmaps[pi];
    if (hp.rows <= 0 || hp.cols <= 0) return;

    const std::size_t rows = static_cast<std::size_t>(hp.rows);
    const std::size_t cols = static_cast<std::size_t>(hp.cols);
    // A buffer that doesn't match its own declared shape can't be re-strided
    // coherently; leave it entirely alone rather than guess.
    if (hp.data.size() != rows * cols) return;

    const bool insert = (e.op == MatrixLineEdit::Op::Insert);
    const bool row_ax = (e.axis == MatrixLineEdit::Axis::Row);
    if (!edits_detail::grid_line_ok(rows, cols, row_ax, insert, e.index)) return;

    // Hint_labels is optional and may legitimately be empty ("no custom
    // labels at all"); anything else is resized to the documented full
    // rows*cols before re-striding, so a short user-supplied vector can't
    // silently desynchronize from here on.
    auto& labels = hp.opts.hint_labels;
    if (!labels.empty()) labels.resize(rows * cols);

    edits_detail::grid_reshape(hp.data, rows, cols, row_ax, insert, e.index);
    edits_detail::grid_reshape(labels,  rows, cols, row_ax, insert, e.index);
    if (row_ax) hp.rows += insert ? 1 : -1;
    else        hp.cols += insert ? 1 : -1;
}

// ---------------------------------------------------------------------------
// Applying data ops to a 3D axes' own plot objects
//
// A `bar3d` grid lives on the axes rather than on a plane, so its ops carry
// plane index -1 -- the same address a 2D slot's ops carry, which is why these
// are a separate overload set rather than more cases inside the bodies above:
// the two targets have disjoint members (`bars3d` against the five 2D
// vectors), and one body over both would name members neither has.
//
// The `kind` guard on each is what keeps the two sets from ever both firing:
// an op is Bar3D or it is not, and whichever set does not want it drops it in
// its first line. Everything else -- re-validating every index, silently
// skipping what no longer fits -- follows the rules the 2D bodies set, and for
// the same reason: the panel indexed frame N while this runs against N+1.
// ---------------------------------------------------------------------------

namespace edits_detail {

// Where a new grid line's coordinate goes: midway between its neighbours when
// it lands between two, and one local spacing past the end otherwise.
//
// A copy of its predecessor -- which is what a new heatmap *row* gets, since
// that is a row of values and the extent is the axis -- would put two bars in
// exactly the same place, so the two seeds are deliberately different rules.
inline double grid_coord_seed(const CowVec<double>& c, std::size_t at) {
    const std::size_t n = c.size();
    if (n == 0) return 0.0;
    if (at == 0) return c[0] - (n >= 2 ? c[1] - c[0] : 1.0);
    if (at >= n) return c[n - 1] + (n >= 2 ? c[n - 1] - c[n - 2] : 1.0);
    return 0.5 * (c[at - 1] + c[at]);
}

} // namespace edits_detail

// Column 0/1 are the grid's own coordinate vectors (`element` indexes u or v);
// 2/3 are the row-major matrices (`element` is i * |v| + j, u major), of which
// `bottoms` exists only when the caller gave a base per bar.
//
// A surface (step 7d) is addressed the same way and shares the first three
// columns exactly, because it is stored in the same layout. It has no fourth:
// a sheet has no base to stand on.
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
        if (pi >= t.bars3d.size()) return;
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
        if (pi >= t.surfaces.size()) return;
        auto& s = t.surfaces[pi];
        switch (e.column) {
            case 0: put_cell(s.u, e.element, e.value);       return;
            case 1: put_cell(s.v, e.element, e.value);       return;
            case 2: put_cell(s.heights, e.element, e.value); return;
            default: return;
        }
    }
    // A cloud is the plain vector shape -- three coordinate columns and, when
    // the series has one, the fourth dimension. `element` is the point index
    // in every column, since all four are indexed alike; column 3 writes
    // nothing on a flat series, whose `colors` is empty.
    if (e.kind == PlotKind::Scatter3D) {
        if (pi >= t.scatter3d.size()) return;
        auto& s = t.scatter3d[pi];
        switch (e.column) {
            case 0: put_cell(s.x, e.element, e.value);      return;
            case 1: put_cell(s.y, e.element, e.value);      return;
            case 2: put_cell(s.z, e.element, e.value);      return;
            case 3: put_cell(s.colors, e.element, e.value); return;
            default: return;
        }
    }
    // A path takes a cloud's shape here too: the same four columns, the same
    // element-is-the-point-index rule. Editing a coordinate moves a vertex and
    // the two segments meeting there follow, which is what the picture shows.
    if (e.kind == PlotKind::Line3D) {
        if (pi >= t.lines3d.size()) return;
        auto& l = t.lines3d[pi];
        switch (e.column) {
            case 0: put_cell(l.x, e.element, e.value);      return;
            case 1: put_cell(l.y, e.element, e.value);      return;
            case 2: put_cell(l.z, e.element, e.value);      return;
            case 3: put_cell(l.colors, e.element, e.value); return;
            default: return;
        }
    }
    // A mesh, in the same four columns and with the same
    // element-is-the-vertex-index rule. **The topology is not reachable from
    // here at all**, and that is the step's stated consequence rather than an
    // omission: editing a vertex moves a point, and a mesh built by the
    // Delaunay overloads does *not* re-triangulate, because topology jumping
    // under a drag would be worse than a mesh that deforms.
    if (e.kind == PlotKind::SurfaceTri) {
        if (pi >= t.surface_tri.size()) return;
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
    if (pi >= t.bars3d.size()) return;
    if (e.column == 0)      t.bars3d[pi].u_width = e.width;
    else if (e.column == 1) t.bars3d[pi].v_width = e.width;
}

// Adds or removes a whole u line or v line of the grid: the coordinate vector
// and every row-major buffer that strides against it, in one step, or the
// three stop describing the same grid.
namespace edits_detail {

// The body both gridded 3D kinds share: the coordinate vector and every
// row-major buffer that strides against it, moved in one step, or the three
// stop describing the same grid. `alt` is a second matrix the plot may not
// have (a bar grid's per-bar bases); an empty one is left empty.
template <class Labels>
void grid_line_apply(CowVec<double>& u, CowVec<double>& v,
                     CowVec<double>& primary, CowVec<double>& alt, Labels& labels,
                     const MatrixLineEdit& e) {
    const std::size_t rows = u.size(), cols = v.size();
    const bool insert = (e.op == MatrixLineEdit::Op::Insert);
    const bool row_ax = (e.axis == MatrixLineEdit::Axis::Row);
    if (!grid_line_ok(rows, cols, row_ax, insert, e.index)) return;
    // A buffer that doesn't match its own grid can't be re-strided coherently;
    // leave the whole plot alone rather than guess, as the heatmap does.
    if (primary.size() != rows * cols) return;
    if (!alt.empty() && alt.size() != rows * cols) return;

    if (!labels.empty()) labels.resize(rows * cols);

    // The coordinate first, while `rows`/`cols` still describe the buffers.
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
        if (pi >= t.bars3d.size()) return;
        auto& b = t.bars3d[pi];
        edits_detail::grid_line_apply(b.u, b.v, b.heights, b.bottoms,
                                      b.opts.hint_labels, e);
        return;
    }
    if (e.kind == PlotKind::Surface) {
        if (pi >= t.surfaces.size()) return;
        auto& s = t.surfaces[pi];
        // A surface has no second matrix, so it passes an empty one that
        // grid_reshape() leaves alone -- the same slot a bar grid's absent
        // `bottoms` occupies, rather than a second code path.
        CowVec<double> none;
        edits_detail::grid_line_apply(s.u, s.v, s.heights, none,
                                      s.opts.hint_labels, e);
        return;
    }
}

template <class T>
void apply_axes3d_data_op(T&, const PlotRowEdit&) {
    // Neither gridded 3D kind has a single point to insert: adding one bar or
    // one vertex would mean choosing a u line or a v line and re-striding,
    // which is exactly what MatrixLineEdit is. The Data panel never emits one
    // of these for a grid; ignore it if one ever arrives.
}

// Applies a whole ordered op stream. Order is preserved exactly as recorded —
// see PlotDataOp.
//
// Two overloads, distinguished by whether the target *has* planes rather than
// by the caller knowing which it holds, so the four apply_plot_data_op()
// bodies above stay the single definition either way. An op addressed at a
// plane is skipped by the 2D one: it is not reachable from the panel, and it
// is the same silent-skip that a stale plot index already gets, since the
// alternative is writing a value into whichever object happens to share the
// index.
//
// Plane -1 means "the axes itself", which is every op a 2D slot produces and,
// in a 3D slot, the `bar3d` grids (step 6c) and the surfaces (step 7d) -- so
// the two overloads read the same address and route it to the objects their
// own target actually holds.
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
// Plot-object appearance (v1.0 step 11.6)
// ---------------------------------------------------------------------------
// One object's options, written into whichever vector holds its kind.
//
// `hint_labels` is carried across rather than taken from the edit, for exactly
// the reason apply_axes3d_edit() carries a bar grid's: those are *data*, owned
// by the Data panel's table and re-strided by a row insert or remove, while
// the options copy the panel republishes is only re-seeded when the selection
// or the object count changes. Without this, ticking a checkbox after adding a
// point would quietly restore the labels to their old length.
template <class Vec, class Opts>
void assign_plot_opts(Vec& v, int idx, const Opts& o) {
    if (idx < 0 || static_cast<std::size_t>(idx) >= v.size()) return;
    auto labels = std::move(v[static_cast<std::size_t>(idx)].opts.hint_labels);
    v[static_cast<std::size_t>(idx)].opts = o;
    v[static_cast<std::size_t>(idx)].opts.hint_labels = std::move(labels);
}

// The variant alternative chooses the vector, so this is one overload set
// rather than a switch: a target that holds `lines` and a target that holds
// `lines` are the same shape whether it is Axes::Impl, a RenderSnapshot, or a
// plane's sheet inside either.
template <class T>
void apply_plot_style_edit(T& t, const PlotStyleEdit& e) {
    if (const auto* o = std::get_if<LineOptions>(&e.opts))          assign_plot_opts(t.lines,     e.plot_index, *o);
    else if (const auto* o = std::get_if<ScatterOptions>(&e.opts))  assign_plot_opts(t.scatters,  e.plot_index, *o);
    else if (const auto* o = std::get_if<BarOptions>(&e.opts))      assign_plot_opts(t.bars,      e.plot_index, *o);
    else if (const auto* o = std::get_if<HeatmapOptions>(&e.opts))  assign_plot_opts(t.heatmaps,  e.plot_index, *o);
    else if (const auto* o = std::get_if<ScatterZOptions>(&e.opts)) assign_plot_opts(t.scatter_z, e.plot_index, *o);
}

// Two overloads on the same rule apply_plot_data_ops() uses: whether the
// target *has* planes, rather than the caller knowing which it holds, so the
// body above stays the single definition for Axes::Impl, RenderSnapshot,
// Axes3D::Impl and RenderSnapshot3D alike.
//
// A 3D target skips plane -1 rather than routing it: what lives directly on a
// 3D axes is bar3d grids and surfaces, and those have had lanes of their own
// (AxesEdit3D::bars3d / ::surfaces) since step 7d. A 2D target skips the
// opposite half, exactly as it skips a plane-addressed data op.
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

// The 3D half of an edit drain, written once and called from both paths:
// the caller-thread one writes live Axes3D::Impl, the render-thread one
// patches a published RenderSnapshot3D, and the two carry the same fields
// under the same names. A template rather than two copies precisely
// because a field added to one and forgotten in the other is a control
// that works until the next refresh() and then silently reverts.
template <typename T>
void apply_axes3d_edit(T& dst, const AxesEdit3D& e) {
    if (e.title)  dst.title  = *e.title;
    if (e.xtitle) dst.xtitle = *e.xtitle;
    if (e.ytitle) dst.ytitle = *e.ytitle;
    if (e.ztitle) dst.ztitle = *e.ztitle;
    if (e.xlim_auto) dst.xlim_auto = *e.xlim_auto;
    if (e.ylim_auto) dst.ylim_auto = *e.ylim_auto;
    if (e.zlim_auto) dst.zlim_auto = *e.zlim_auto;
    if (e.xmin) dst.xmin = *e.xmin;   if (e.xmax) dst.xmax = *e.xmax;
    if (e.ymin) dst.ymin = *e.ymin;   if (e.ymax) dst.ymax = *e.ymax;
    if (e.zmin) dst.zmin = *e.zmin;   if (e.zmax) dst.zmax = *e.zmax;
    if (e.grid_enabled) dst.grid_enabled = *e.grid_enabled;
    if (e.grid_opts)    dst.grid_opts    = *e.grid_opts;
    if (e.axes_style)   dst.axes_style   = *e.axes_style;
    if (e.legend_enabled) dst.legend_enabled = *e.legend_enabled;
    if (e.legend_opts)    dst.legend_opts    = *e.legend_opts;
    if (e.colorbar_opts)  dst.colorbar_opts  = *e.colorbar_opts;
    if (e.camera)       dst.camera       = *e.camera;
    if (e.box_style)    dst.box_style    = *e.box_style;
    if (e.aspect)       dst.aspect       = *e.aspect;
    // Empty means "back to auto", absent means "unchanged" -- the same
    // reading the 2D tick overrides have.
    if (e.xticks_override)
        dst.xticks_override = e.xticks_override->empty()
            ? std::nullopt : std::optional(*e.xticks_override);
    if (e.yticks_override)
        dst.yticks_override = e.yticks_override->empty()
            ? std::nullopt : std::optional(*e.yticks_override);
    if (e.zticks_override)
        dst.zticks_override = e.zticks_override->empty()
            ? std::nullopt : std::optional(*e.zticks_override);

    // The planes, placement then data. Positional and re-validated here
    // for the reason every plot index is: the panel indexed frame N while
    // this runs against frame N+1, and a plane the caller has since
    // dropped must be skipped rather than clamped onto its neighbour.
    apply_plot_style_edits(dst, e.plot_styles);

    for (const auto& pe : e.planes) {
        if (pe.plane_index < 0) continue;
        const std::size_t pi = static_cast<std::size_t>(pe.plane_index);
        if (pi >= dst.plane_count()) continue;
        auto& p = dst.plane_at(pi);
        if (pe.orient) p.orient = *pe.orient;
        if (pe.offset) p.offset = *pe.offset;
        if (pe.opts)   p.opts   = *pe.opts;
    }

    // The gridded kinds' appearance. Positional and re-validated for the
    // same reason the planes are.
    //
    // `hint_labels` is carried across rather than taken from the edit: it
    // is *data*, owned by the Data panel and re-strided whenever a grid
    // line is added or removed, and the Cosmetic panel's local copy of the
    // options struct is only re-seeded when the selection or the object
    // count changes. Without this, colouring a grid after adding a u line
    // would quietly restore the labels to their old shape.
    for (const auto& be : e.bars3d) {
        if (be.plot_index < 0 || !be.opts) continue;
        const std::size_t bi = static_cast<std::size_t>(be.plot_index);
        if (bi >= dst.bars3d.size()) continue;
        auto labels = std::move(dst.bars3d[bi].opts.hint_labels);
        dst.bars3d[bi].opts = *be.opts;
        dst.bars3d[bi].opts.hint_labels = std::move(labels);
    }
    for (const auto& se : e.surfaces) {
        if (se.plot_index < 0 || !se.opts) continue;
        const std::size_t si = static_cast<std::size_t>(se.plot_index);
        if (si >= dst.surfaces.size()) continue;
        auto labels = std::move(dst.surfaces[si].opts.hint_labels);
        dst.surfaces[si].opts = *se.opts;
        dst.surfaces[si].opts.hint_labels = std::move(labels);
    }
    for (const auto& ce : e.scatter3d) {
        if (ce.plot_index < 0 || !ce.opts) continue;
        const std::size_t ci = static_cast<std::size_t>(ce.plot_index);
        if (ci >= dst.scatter3d.size()) continue;
        auto labels = std::move(dst.scatter3d[ci].opts.hint_labels);
        dst.scatter3d[ci].opts = *ce.opts;
        dst.scatter3d[ci].opts.hint_labels = std::move(labels);
    }
    for (const auto& le : e.lines3d) {
        if (le.plot_index < 0 || !le.opts) continue;
        const std::size_t li = static_cast<std::size_t>(le.plot_index);
        if (li >= dst.lines3d.size()) continue;
        auto labels = std::move(dst.lines3d[li].opts.hint_labels);
        dst.lines3d[li].opts = *le.opts;
        dst.lines3d[li].opts.hint_labels = std::move(labels);
    }
    for (const auto& me : e.surface_tri) {
        if (me.plot_index < 0 || !me.opts) continue;
        const std::size_t mi = static_cast<std::size_t>(me.plot_index);
        if (mi >= dst.surface_tri.size()) continue;
        auto labels = std::move(dst.surface_tri[mi].opts.hint_labels);
        dst.surface_tri[mi].opts = *me.opts;
        dst.surface_tri[mi].opts.hint_labels = std::move(labels);
    }
    apply_plot_data_ops(dst, e.plot_ops);
}

} // namespace sextant
