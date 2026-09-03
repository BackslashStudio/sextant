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
struct PlotCellEdit {
    PlotKind    kind;
    int         plot_index;
    int         column;
    std::size_t element;
    double      value;
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
struct MatrixLineEdit {
    enum class Op   { Insert, Remove };
    enum class Axis { Row, Col };
    Op          op         = Op::Insert;
    Axis        axis       = Axis::Row;
    int         plot_index = 0;
    // Insert: the index the new line will occupy (== rows/cols to append).
    // Remove: the index to erase.
    std::size_t index      = 0;
};

// BarPlot::bar_width is a single per-plot scalar rather than a per-bar column,
// so it can't ride in a PlotCellEdit — but it is still plot *data*, and is
// journaled and replayed alongside the rest.
struct BarWidthEdit {
    int    plot_index = 0;
    double width      = 1.0;
};

// One entry of a plot object's edit stream. Ordering across the whole stream
// is load-bearing, and the reason these live in one sequence rather than in
// per-kind buckets: every index is relative to the arrays as they stood when
// the op was recorded, so a cell edit staged before a row insert must be
// applied before it too. (Clicking a row button deactivates the focused
// input, so both routinely land in the same frame.)
using PlotDataOp = std::variant<PlotCellEdit, PlotRowEdit, MatrixLineEdit, BarWidthEdit>;

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
};

// Keyed by AxesSlot::index (1-indexed, unique within one Figure). A
// vector-of-pairs rather than a map: slot counts are tiny (subplot grids,
// not thousands of axes).
struct FigureEdits {
    std::vector<std::pair<int, AxesEdit>> per_axes;

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

    // FigureEditBox's drain tests this, not per_axes.empty(): a
    // figure-level-only edit carries no per-axes entry and would otherwise be
    // dropped. Every field above has to be listed. Missing one is not a
    // compile error, it is a control that silently does nothing until some
    // unrelated per-axes edit happens to ride along with it.
    bool empty() const {
        return per_axes.empty() && !suptitle && !suptitle_opts
               && !margins && !col_gap && !row_gap;
    }
};

// The data ops the render thread has already folded into the published
// snapshot, kept so the caller thread can replay them onto the authoritative
// Axes::Impl. Only *data* is journaled, not AxesEdit's appearance/limit
// fields -- see FigureEditBox.
struct PlotDataJournal {
    std::vector<std::pair<int, std::vector<PlotDataOp>>> per_axes;
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
    }
}

template <class T>
void apply_plot_data_op(T& t, const BarWidthEdit& e) {
    if (e.plot_index < 0) return;
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
    // the six error-bar vectors. They must move too, or everything past the
    // edit point slides onto the wrong point -- invisible until someone reads
    // the plot. Each is left strictly alone when empty, since empty means
    // "this plot has none of these at all".
    //
    // `copy_prev` differs between the two deliberately: a new point gets a
    // *blank* hint label (inheriting someone else's "Peak" is an assertion
    // nobody made) but a *copy* of its neighbour's error data, because the
    // inserted point is itself a copy of that neighbour. Zero-filling would be
    // worse than blank there: ymin/ymax are absolute, so a zeroed ymin draws a
    // whisker reaching down to the origin.
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
        optional_cols(true, err.ymin, err.ymax, err.yvar,
                            err.xmin, err.xmax, err.xvar);
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
            // Not addressable — see PlotRowEdit. The Data panel never emits
            // one of these for a heatmap; ignore it if one ever arrives.
            break;
    }
}

// Inserts or removes a whole matrix row / column, re-striding the row-major
// buffer around the change. HeatmapOptions::hint_labels shares that exact
// layout (index = row*cols + col), so it is re-strided in lockstep or every
// label past the change lands on the wrong cell.
template <class T>
void apply_plot_data_op(T& t, const MatrixLineEdit& e) {
    if (e.plot_index < 0) return;
    const std::size_t pi = static_cast<std::size_t>(e.plot_index);
    if (pi >= t.heatmaps.size()) return;
    auto& hp = t.heatmaps[pi];
    if (hp.rows <= 0 || hp.cols <= 0) return;

    const std::size_t rows = static_cast<std::size_t>(hp.rows);
    const std::size_t cols = static_cast<std::size_t>(hp.cols);
    // A buffer that doesn't match its own declared shape can't be re-strided
    // coherently; leave it entirely alone rather than guess.
    if (hp.data.size() != rows * cols) return;

    const bool   insert = (e.op == MatrixLineEdit::Op::Insert);
    const bool   row_ax = (e.axis == MatrixLineEdit::Axis::Row);
    const std::size_t extent = row_ax ? rows : cols;
    if (insert ? (e.index > extent) : (e.index >= extent)) return;
    // Never below 1x1 — Axes::heatmap() rejects that shape outright, and a
    // matrix shrunk to zero on either axis can't be regrown from the panel.
    if (!insert && extent <= 1) return;

    // Hint_labels is optional and may legitimately be empty ("no custom
    // labels at all"); anything else is resized to the documented full
    // rows*cols before re-striding, so a short user-supplied vector can't
    // silently desynchronize from here on.
    auto& labels = hp.opts.hint_labels;
    if (!labels.empty()) labels.resize(rows * cols);

    auto reshape = [&](auto& v) {
        using Value = typename std::decay_t<decltype(v)>::value_type;
        if (v.empty()) return;
        auto& vv = edits_detail::mut_ref(v);
        if (row_ax) {
            const std::size_t at = e.index * cols;
            if (insert) {
                // Seed from the row above; at row 0 there is none, so the new
                // row is value-initialized.
                std::vector<Value> seed(cols);
                if (e.index > 0)
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
                const std::size_t at = r * cols + e.index;
                if (insert) {
                    Value seed = (e.index > 0) ? vv[at - 1] : Value{};
                    vv.insert(vv.begin() + static_cast<std::ptrdiff_t>(at), std::move(seed));
                } else {
                    vv.erase(vv.begin() + static_cast<std::ptrdiff_t>(at));
                }
            }
        }
    };

    reshape(hp.data);
    reshape(labels);
    if (row_ax) hp.rows += insert ? 1 : -1;
    else        hp.cols += insert ? 1 : -1;
}

// Applies a whole ordered op stream. Order is preserved exactly as recorded —
// see PlotDataOp.
template <class T>
void apply_plot_data_ops(T& t, const std::vector<PlotDataOp>& ops) {
    for (const auto& op : ops)
        std::visit([&t](const auto& o) { apply_plot_data_op(t, o); }, op);
}

} // namespace sextant
