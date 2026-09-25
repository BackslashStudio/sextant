#pragma once
#include "figure_edits.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace sextant {

// Thread-safe merge holder for widget-panel edits (the reverse of SnapshotBox):
// per-field deltas accumulate between drains. Draining is destructive, so each
// edit is applied exactly once.
//
// The render-thread drain only patches the published snapshot, so every edit
// is also journaled and replayed onto Axes::Impl/Figure::Impl by the caller
// thread (take_journal()): data ops in order, everything else as its latest
// value, each with the stamps of the snapshot it was made over so a later
// setter call wins.
class FigureEditBox {
public:
    void update(int slot_index, const std::function<void(AxesEdit&)>& fn) {
        std::scoped_lock lk(mutex_);
        AxesEdit& e = slot_edit(slot_index);
        fn(e);
        stamp_from_drawn(slot_index, e);
    }

    // Same, for an Axes3D slot.
    void update3d(int slot_index, const std::function<void(AxesEdit3D&)>& fn) {
        std::scoped_lock lk(mutex_);
        AxesEdit3D& e = slot_edit3d(slot_index);
        fn(e);
        stamp_from_drawn(slot_index, e);
    }

    // Figure-level edits (currently the suptitle).
    void update_figure(const std::function<void(FigureEdits&)>& fn) {
        std::scoped_lock lk(mutex_);
        fn(pending_);
        if (drawn_) pending_.fig_seen = drawn_->stamps;
    }

    // The snapshot the panels are drawn from this frame. update() records its
    // stamps (StyleStamps, placement and figure stamps) on every edit, so a push
    // site need not. Exact because the render thread drains once per frame:
    // everything pending was pushed over this one snapshot. Without one (tests,
    // no window) edits keep their any() stamps.
    void set_drawn(std::shared_ptr<const FigureSnapshot> snap) {
        std::scoped_lock lk(mutex_);
        drawn_ = std::move(snap);
    }

    // Caller-thread drain; applied straight to Axes::Impl, so no journal.
    std::optional<FigureEdits> load_and_clear() {
        std::scoped_lock lk(mutex_);
        return take_pending();
    }

    // Render-thread drain; also journals everything for replay.
    std::optional<FigureEdits> load_and_clear_journaled() {
        std::scoped_lock lk(mutex_);
        // Both lanes into one journal keyed by slot (a slot is one kind only).
        for (const auto& [idx, e] : pending_.per_axes) {
            journal_titles(idx, e);
            journal_limits(idx, e);
            journal_style(journal_.styles, idx, e);
            if (e.plot_ops.empty()) continue;
            auto& dst = journal_slot(idx);
            dst.insert(dst.end(), e.plot_ops.begin(), e.plot_ops.end());
        }
        for (const auto& [idx, e] : pending_.per_axes3d) {
            journal_titles(idx, e);
            journal_limits(idx, e);
            journal_style(journal_.styles3d, idx, e);
            if (e.camera) journal_camera(idx, {*e.camera, e.camera_seen});
            if (e.plot_ops.empty()) continue;
            auto& dst = journal_slot(idx);
            dst.insert(dst.end(), e.plot_ops.begin(), e.plot_ops.end());
        }
        merge_figure_edits(journal_.figure, pending_);
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

    // The drawn snapshot's stamps for slot `idx`, onto a pending edit.
    void stamp_from_drawn(int idx, AxesEdit& e) const {
        const RenderSnapshot* s = drawn_axes<RenderSnapshot>(idx);
        if (!s) return;
        e.style_seen = s->style_stamps;
        for (PlotStyleEdit& p : e.plot_styles) p.seen = s->style_stamps.cleared;
    }

    void stamp_from_drawn(int idx, AxesEdit3D& e) const {
        const RenderSnapshot3D* s = drawn_axes<RenderSnapshot3D>(idx);
        if (!s) return;
        e.style_seen = s->style_stamps;
        const auto plane = [s](int pi) -> const PlaneSnapshot* {
            return pi >= 0 && static_cast<std::size_t>(pi) < s->planes.size()
                       ? &s->planes[static_cast<std::size_t>(pi)] : nullptr;
        };
        for (auto& pe : e.planes)
            if (const PlaneSnapshot* p = plane(pe.plane_index)) pe.seen = p->placement_stamp;
        for (PlotStyleEdit& ps : e.plot_styles)
            if (const PlaneSnapshot* p = plane(ps.plane_index))
                ps.seen = p->sheet.style_stamps.cleared;
        const unsigned long long cleared = s->style_stamps.cleared;
        auto objects = [cleared](auto& edits) { for (auto& o : edits) o.seen = cleared; };
        objects(e.bars3d);
        objects(e.surfaces);
        objects(e.scatter3d);
        objects(e.lines3d);
        objects(e.surface_tri);
    }

    template <class Snap>
    const Snap* drawn_axes(int idx) const {
        if (!drawn_) return nullptr;
        for (const auto& fa : drawn_->axes)
            if (fa.slot.index == idx) return std::get_if<Snap>(&fa.snap);
        return nullptr;
    }

    // Everything without a lane of its own, latest value only.
    template <class E>
    static void journal_style(std::vector<std::pair<int, E>>& lane, int idx, const E& e) {
        if (style_edits_empty(e)) return;
        for (auto& [i, j] : lane)
            if (i == idx) { merge_style_edits(j, e); return; }
        E made;
        merge_style_edits(made, e);
        lane.push_back({idx, std::move(made)});
    }

    // Titles, latest value only.
    template <typename E>
    void journal_titles(int idx, const E& e) {
        TitleEdits typed;
        merge_title_edits(typed, e);
        if (typed.empty()) return;
        for (auto& [i, t] : journal_.titles)
            if (i == idx) { merge_title_edits(t, typed); return; }
        journal_.titles.push_back({idx, std::move(typed)});
    }

    // Limits, latest value only.
    template <typename E>
    void journal_limits(int idx, const E& e) {
        LimitEdits made;
        merge_limit_edits(made, e);
        if (made.empty()) return;
        for (auto& [i, l] : journal_.limits)
            if (i == idx) { merge_limit_edits(l, made); return; }
        journal_.limits.push_back({idx, std::move(made)});
    }

    // The camera, latest value only.
    void journal_camera(int idx, PlotDataJournal::CameraEdit c) {
        for (auto& [i, cur] : journal_.cameras)
            if (i == idx) { cur = c; return; }
        journal_.cameras.push_back({idx, c});
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
    std::shared_ptr<const FigureSnapshot> drawn_;
};

} // namespace sextant
