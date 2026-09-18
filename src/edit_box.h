#pragma once
#include "figure_edits.h"
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace sextant {

// Thread-safe MERGE holder for widget-panel edits -- the reverse-direction
// counterpart to SnapshotBox. Unlike SnapshotBox's latest-value-wins, edits
// arrive as many small per-field deltas that must accumulate between drains
// rather than overwrite each other. The render thread calls update() whenever
// a widget changes; either thread can then drain.
//
// The drain is destructive, so an edit reaches exactly one of the two paths
// and is applied exactly once. That is fine for everything except plot data:
// the render-thread path patches the published snapshot only, never the
// authoritative Axes::Impl, so a later refresh() would throw those edits away.
// Hence the journal -- load_and_clear_journaled() copies the data ops aside on
// the way out and the caller thread replays them via take_journal(), likewise
// destructive so nothing lands twice.
//
// Only PlotDataOps are journaled. The rest of AxesEdit is live preview that
// pan/zoom restages every frame of a drag: journaling it would grow the
// journal with the duration of a mouse gesture and let a stale panned viewport
// overwrite an explicit set_xlim(). Plot data has neither problem.
//
// The grid's weights are the one exception (v1.0 step 15.3), because an
// arranged grid is meant to be kept. They avoid both problems: the journal
// holds only the latest vector, however long the drag, and
// Figure::set_col_ratios()/set_row_ratios() discard_ratios() on the way in, so
// an explicit call after a drag wins over it.
class FigureEditBox {
public:
    void update(int slot_index, const std::function<void(AxesEdit&)>& fn) {
        std::scoped_lock lk(mutex_);
        fn(slot_edit(slot_index));
    }

    // Same, for a slot holding an Axes3D. A slot is one kind or the other, so
    // the two lanes never address the same entry.
    void update3d(int slot_index, const std::function<void(AxesEdit3D&)>& fn) {
        std::scoped_lock lk(mutex_);
        fn(slot_edit3d(slot_index));
    }

    // Figure-level counterpart, for edits that belong to no single axes
    // (currently the suptitle). Same merge semantics: last writer wins per
    // field, accumulating between drains.
    void update_figure(const std::function<void(FigureEdits&)>& fn) {
        std::scoped_lock lk(mutex_);
        fn(pending_);
    }

    // Caller-thread drain. Whatever comes back is written straight into the
    // live Axes::Impl, so it needs no journal entry.
    std::optional<FigureEdits> load_and_clear() {
        std::scoped_lock lk(mutex_);
        return take_pending();
    }

    // Render-thread drain. Identical, except the data ops are also recorded
    // for later replay onto Axes::Impl.
    std::optional<FigureEdits> load_and_clear_journaled() {
        std::scoped_lock lk(mutex_);
        // Both lanes, into one journal keyed by slot: a slot is one kind or
        // the other, so the two can never contribute to the same entry, and a
        // 3D slot's ops already carry the plane they belong to.
        for (const auto& [idx, e] : pending_.per_axes) {
            if (e.plot_ops.empty()) continue;
            auto& dst = journal_slot(idx);
            dst.insert(dst.end(), e.plot_ops.begin(), e.plot_ops.end());
        }
        for (const auto& [idx, e] : pending_.per_axes3d) {
            if (e.plot_ops.empty()) continue;
            auto& dst = journal_slot(idx);
            dst.insert(dst.end(), e.plot_ops.begin(), e.plot_ops.end());
        }
        // The grid's weights, latest value only (v1.0 step 15.3).
        if (pending_.col_ratios) journal_.col_ratios = pending_.col_ratios;
        if (pending_.row_ratios) journal_.row_ratios = pending_.row_ratios;
        return take_pending();
    }

    // Figure::set_col_ratios()/set_row_ratios(): an explicit call replaces a
    // dragged value, so a drag staged or journaled before it must not replay
    // over it at the next refresh().
    void discard_ratios(bool cols, bool rows) {
        std::scoped_lock lk(mutex_);
        if (cols) { pending_.col_ratios.reset(); journal_.col_ratios.reset(); }
        if (rows) { pending_.row_ratios.reset(); journal_.row_ratios.reset(); }
    }

    std::optional<PlotDataJournal> take_journal() {
        std::scoped_lock lk(mutex_);
        if (journal_.empty()) return std::nullopt;
        PlotDataJournal out = std::move(journal_);
        journal_ = PlotDataJournal{};
        return out;
    }

private:
    std::optional<FigureEdits> take_pending() {
        // FigureEdits::empty(), not per_axes.empty() — a figure-level edit
        // (suptitle) carries no per-axes entry at all.
        if (pending_.empty()) return std::nullopt;
        FigureEdits out = std::move(pending_);
        pending_ = FigureEdits{};
        return out;
    }

    AxesEdit& slot_edit(int idx) {
        for (auto& [i, e] : pending_.per_axes)
            if (i == idx) return e;
        pending_.per_axes.push_back({idx, AxesEdit{}});
        return pending_.per_axes.back().second;
    }

    AxesEdit3D& slot_edit3d(int idx) {
        for (auto& [i, e] : pending_.per_axes3d)
            if (i == idx) return e;
        pending_.per_axes3d.push_back({idx, AxesEdit3D{}});
        return pending_.per_axes3d.back().second;
    }

    std::vector<PlotDataOp>& journal_slot(int idx) {
        for (auto& [i, ops] : journal_.per_axes)
            if (i == idx) return ops;
        journal_.per_axes.push_back({idx, {}});
        return journal_.per_axes.back().second;
    }

    std::mutex      mutex_;
    FigureEdits     pending_;
    PlotDataJournal journal_;
};

} // namespace sextant
