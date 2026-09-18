#pragma once
#include "figure_edits.h"
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace sextant {

// Thread-safe merge holder for widget-panel edits (the reverse of SnapshotBox):
// per-field deltas accumulate between drains. Draining is destructive, so each
// edit is applied exactly once.
//
// The render-thread drain only patches the published snapshot, so plot-data
// ops are also journaled and replayed onto Axes::Impl by the caller thread
// (take_journal()). Other edits are live preview and are not journaled, except
// the grid ratios (latest value only).
class FigureEditBox {
public:
    void update(int slot_index, const std::function<void(AxesEdit&)>& fn) {
        std::scoped_lock lk(mutex_);
        fn(slot_edit(slot_index));
    }

    // Same, for an Axes3D slot.
    void update3d(int slot_index, const std::function<void(AxesEdit3D&)>& fn) {
        std::scoped_lock lk(mutex_);
        fn(slot_edit3d(slot_index));
    }

    // Figure-level edits (currently the suptitle).
    void update_figure(const std::function<void(FigureEdits&)>& fn) {
        std::scoped_lock lk(mutex_);
        fn(pending_);
    }

    // Caller-thread drain; applied straight to Axes::Impl, so no journal.
    std::optional<FigureEdits> load_and_clear() {
        std::scoped_lock lk(mutex_);
        return take_pending();
    }

    // Render-thread drain; also journals the data ops for replay.
    std::optional<FigureEdits> load_and_clear_journaled() {
        std::scoped_lock lk(mutex_);
        // Both lanes into one journal keyed by slot (a slot is one kind only).
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
        // Grid ratios, latest value only.
        if (pending_.col_ratios) journal_.col_ratios = pending_.col_ratios;
        if (pending_.row_ratios) journal_.row_ratios = pending_.row_ratios;
        return take_pending();
    }

    // An explicit set_col_ratios()/set_row_ratios() overrides any pending or
    // journaled drag.
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
        // FigureEdits::empty(): a figure-level edit has no per-axes entry.
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
