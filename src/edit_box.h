#pragma once
#include "figure_edits.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

namespace sextant {

// The ids of the objects and planes a per-axes edit names by index, and the
// style and placement stamps, from the snapshot it is made over,
// so each edit follows its object whatever moves before it is applied. An id
// already set (by an earlier update(), or by a host) is kept.
namespace stamp_detail {

template <class Holder>
void fill_id(ObjectId& id, const Holder& h, PlotKind kind, int index) {
    if (id == 0) id = object_id_at(h, kind, index);
}

// PlotStyleEdit's alternatives are in PlotKind's 2D order, then a text's.
inline PlotKind style_kind(const PlotStyleEdit& p) {
    using Opts = decltype(PlotStyleEdit::opts);
    static_assert(static_cast<int>(PlotKind::Heatmap) == 3
                  && std::is_same_v<std::variant_alternative_t<3, Opts>, HeatmapOptions>
                  && static_cast<int>(PlotKind::ScatterZ) == 4
                  && std::is_same_v<std::variant_alternative_t<4, Opts>, ScatterZOptions>
                  && std::is_same_v<std::variant_alternative_t<5, Opts>, TextStyle>);
    if (p.opts.index() == 5) return PlotKind::Text;
    return static_cast<PlotKind>(p.opts.index());
}

} // namespace stamp_detail

inline void stamp_edit(AxesEdit& e, const RenderSnapshot& s) {
    using stamp_detail::fill_id;
    e.style_seen = s.style_stamps;
    for (PlotDataOp& op : e.plot_ops)
        std::visit([&s](auto& o) { fill_id(o.id, s, o.kind, o.plot_index); }, op);
    for (PlotStyleEdit& p : e.plot_styles)
        fill_id(p.id, s, stamp_detail::style_kind(p), p.plot_index);
}

inline void stamp_edit(AxesEdit3D& e, const RenderSnapshot3D& s) {
    using stamp_detail::fill_id;
    e.style_seen = s.style_stamps;
    const auto plane = [&s](int pi) -> const PlaneSnapshot* {
        return pi >= 0 && static_cast<std::size_t>(pi) < s.planes.size()
                   ? &s.planes[static_cast<std::size_t>(pi)] : nullptr;
    };
    // An entry on a plane: the plane's id, then the object's on its sheet.
    const auto on_plane = [&plane](ObjectId& id, ObjectId& plane_id, int pi,
                                   PlotKind kind, int index) {
        const PlaneSnapshot* p = plane(pi);
        if (plane_id == 0) plane_id = p ? p->id : kNoObject;
        if (p) fill_id(id, p->sheet, kind, index);
        else if (id == 0) id = kNoObject;
    };
    for (auto& pe : e.planes)
        if (const PlaneSnapshot* p = plane(pe.plane_index)) {
            pe.seen = p->placement_stamp;
            if (pe.id == 0) pe.id = p->id;
        } else if (pe.id == 0) {
            pe.id = kNoObject;
        }
    for (PlotStyleEdit& ps : e.plot_styles)
        on_plane(ps.id, ps.plane_id, ps.plane_index, stamp_detail::style_kind(ps), ps.plot_index);
    for (PlotDataOp& op : e.plot_ops)
        std::visit([&](auto& o) {
            if (o.plane_index < 0) fill_id(o.id, s, o.kind, o.plot_index);
            else on_plane(o.id, o.plane_id, o.plane_index, o.kind, o.plot_index);
        }, op);
    const auto objects = [&s](auto& edits, PlotKind kind) {
        for (auto& o : edits) fill_id(o.id, s, kind, o.plot_index);
    };
    objects(e.bars3d, PlotKind::Bar3D);
    objects(e.surfaces, PlotKind::Surface);
    objects(e.scatter3d, PlotKind::Scatter3D);
    objects(e.lines3d, PlotKind::Line3D);
    objects(e.surface_tri, PlotKind::SurfaceTri);
    objects(e.texts, PlotKind::Text);
}

// The stamps a push site records itself (titles, limits, the camera), from
// the same snapshot.
template <class E, class Snap>
void stamp_view(E& e, const Snap& s) {
    e.title_seen = s.title_stamps;
    e.lim_seen = s.limit_stamps;
    if constexpr (requires { e.camera_seen; s.camera_stamp; }) e.camera_seen = s.camera_stamp;
}

// Every stamp and id of a whole edit, from snapshot `snap`: what
// Figure::Impl::apply_edits() does to an edit (an undo) before applying it.
inline void stamp_figure_edits(FigureEdits& f, const FigureSnapshot& snap) {
    auto slot = [&snap]<class Snap>(int idx, const Snap*) -> const Snap* {
        for (const auto& fa : snap.axes)
            if (fa.slot.index == idx) return std::get_if<Snap>(&fa.snap);
        return nullptr;
    };
    for (auto& [idx, e] : f.per_axes)
        if (const auto* s = slot(idx, static_cast<const RenderSnapshot*>(nullptr))) {
            stamp_edit(e, *s);
            stamp_view(e, *s);
        }
    for (auto& [idx, e] : f.per_axes3d)
        if (const auto* s = slot(idx, static_cast<const RenderSnapshot3D*>(nullptr))) {
            stamp_edit(e, *s);
            stamp_view(e, *s);
        }
    f.fig_seen = snap.stamps;
}

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
    // stamps (StyleStamps, placement and figure stamps) on every edit, and the
    // ids of the objects and planes it names by index, so a push site need
    // not. Exact because the render thread drains once per frame: everything
    // pending was pushed over this one snapshot. Without one (tests, no window)
    // edits keep their any() stamps and no ids, so they address by index.
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
        journal_locked(pending_);
        return take_pending();
    }

    // Journals an edit applied to the snapshot by other means than a drain
    // (Figure::Impl::apply_edits()), after anything already journaled.
    void journal(const FigureEdits& f) {
        std::scoped_lock lk(mutex_);
        journal_locked(f);
    }

    // An explicit set_col_ratios()/set_row_ratios() overrides any pending or
    // journaled drag.
    void discard_ratios(bool cols, bool rows) {
        std::scoped_lock lk(mutex_);
        if (cols) { pending_.col_ratios.reset(); journal_.col_ratios.reset(); }
        if (rows) { pending_.row_ratios.reset(); journal_.row_ratios.reset(); }
    }

    // Whether a replay is waiting for the caller thread. Pending edits are not
    // counted: a host driving the figure itself asks after the frame's drain,
    // which journals them (see Figure::Impl::host_frame()).
    bool has_journal() {
        std::scoped_lock lk(mutex_);
        return !journal_.empty();
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

    // The drawn snapshot's stamps and ids for slot `idx`, onto a pending edit
    // (see stamp_edit()).
    template <class E>
    void stamp_from_drawn(int idx, E& e) const {
        using Snap = std::conditional_t<std::is_same_v<E, AxesEdit3D>, RenderSnapshot3D,
                                        RenderSnapshot>;
        if (const Snap* s = drawn_axes<Snap>(idx)) stamp_edit(e, *s);
    }

    // Both lanes into one journal keyed by slot (a slot is one kind only).
    void journal_locked(const FigureEdits& f) {
        for (const auto& [idx, e] : f.per_axes) {
            journal_titles(idx, e);
            journal_limits(idx, e);
            journal_style(journal_.styles, idx, e);
            if (e.plot_ops.empty()) continue;
            auto& dst = journal_slot(idx);
            dst.insert(dst.end(), e.plot_ops.begin(), e.plot_ops.end());
        }
        for (const auto& [idx, e] : f.per_axes3d) {
            journal_titles(idx, e);
            journal_limits(idx, e);
            journal_style(journal_.styles3d, idx, e);
            if (e.camera) journal_camera(idx, {*e.camera, e.camera_seen});
            if (e.plot_ops.empty()) continue;
            auto& dst = journal_slot(idx);
            dst.insert(dst.end(), e.plot_ops.begin(), e.plot_ops.end());
        }
        merge_figure_edits(journal_.figure, f);
        // Grid ratios, latest value only.
        if (f.col_ratios) journal_.col_ratios = f.col_ratios;
        if (f.row_ratios) journal_.row_ratios = f.row_ratios;
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
