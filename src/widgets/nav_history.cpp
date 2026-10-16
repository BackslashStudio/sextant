#include "nav_history.h"
#include "figure_context.h"
#include "panel_state.h"
#include "../edit_box.h"
#include "../figure_edits.h"
#include <algorithm>

namespace sextant {

namespace {

bool same_camera(const Camera3D& a, const Camera3D& b) {
    return a.azimuth == b.azimuth && a.elevation == b.elevation
        && a.target.x == b.target.x && a.target.y == b.target.y && a.target.z == b.target.z
        && a.zoom == b.zoom && a.projection == b.projection && a.fov == b.fov;
}

bool same_limits(const NavLimits& a, const NavLimits& b) {
    if (a.xauto != b.xauto || a.yauto != b.yauto) return false;
    if (!a.xauto && (a.xmin != b.xmin || a.xmax != b.xmax)) return false;
    if (!a.yauto && (a.ymin != b.ymin || a.ymax != b.ymax)) return false;
    return true;
}

NavLimits take_limits(int key, const RenderSnapshot& s) {
    NavLimits l;
    l.key = key;
    l.xmin = s.xmin; l.xmax = s.xmax; l.xauto = s.xlim_auto;
    l.ymin = s.ymin; l.ymax = s.ymax; l.yauto = s.ylim_auto;
    l.stamps = s.limit_stamps;
    return l;
}

bool in_cell(int key, int slot) { return slot < 0 || key_slot(key) == slot; }

} // namespace

NavView capture_view(const FigureSnapshot& fsnap) {
    NavView v;
    fsnap.for_each2d([&](int key, const RenderSnapshot& s) {
        v.limits.push_back(take_limits(key, s));
    });
    for (const FigureAxesSnapshot& fa : fsnap.axes)
        if (const RenderSnapshot3D* s = fa.snap3d())
            v.cameras.push_back({fa.slot.index, s->camera, s->camera_stamp});
    return v;
}

bool same_view(const NavView& a, const NavView& b, int slot) {
    for (const NavLimits& la : a.limits) {
        if (!in_cell(la.key, slot)) continue;
        for (const NavLimits& lb : b.limits)
            if (lb.key == la.key && !same_limits(la, lb)) return false;
    }
    for (const NavCamera& ca : a.cameras) {
        if (slot >= 0 && ca.slot != slot) continue;
        for (const NavCamera& cb : b.cameras)
            if (cb.slot == ca.slot && !same_camera(ca.camera, cb.camera)) return false;
    }
    return true;
}

void nav_track_home(NavHistory& h, const FigureSnapshot& fsnap) {
    NavView now = capture_view(fsnap);
    if (!h.have_home) {
        h.home = std::move(now);
        h.have_home = true;
        return;
    }
    // Rebuilt in the snapshot's order: kept entries, re-taken axes, new axes.
    NavView next;
    for (const NavLimits& n : now.limits) {
        const auto it = std::find_if(h.home.limits.begin(), h.home.limits.end(),
                                     [&](const NavLimits& o) { return o.key == n.key; });
        if (it == h.home.limits.end()) { next.limits.push_back(n); continue; }
        NavLimits l = *it;
        if (n.stamps.x != l.stamps.x) {
            l.xmin = n.xmin; l.xmax = n.xmax; l.xauto = n.xauto; l.stamps.x = n.stamps.x;
        }
        if (n.stamps.y != l.stamps.y) {
            l.ymin = n.ymin; l.ymax = n.ymax; l.yauto = n.yauto; l.stamps.y = n.stamps.y;
        }
        next.limits.push_back(l);
    }
    for (const NavCamera& n : now.cameras) {
        const auto it = std::find_if(h.home.cameras.begin(), h.home.cameras.end(),
                                     [&](const NavCamera& o) { return o.slot == n.slot; });
        next.cameras.push_back(it == h.home.cameras.end() || it->stamp != n.stamp ? n : *it);
    }
    h.home = std::move(next);
}

bool nav_gesture_starts(NavHistory& h, const NavInput& in) {
    bool starts = false;
    if (in.drag && in.moved && !h.in_drag) {
        h.in_drag = true;
        starts = true;
    }
    if (!in.drag) h.in_drag = false;

    if (in.box_done) starts = true;

    if (in.wheel) {
        if (in.slot != h.wheel_slot || in.time - h.wheel_time > NavHistory::kWheelGap)
            starts = true;
        h.wheel_slot = in.slot;
        h.wheel_time = in.time;
    }

    if (in.fly && !h.in_fly) starts = true;
    h.in_fly = in.fly;
    return starts;
}

void nav_begin_gesture(NavHistory& h, const FigureSnapshot& fsnap) {
    nav_track_home(h, fsnap);
    h.back.push_back(capture_view(fsnap));
    if (h.back.size() > NavHistory::kMaxBack) h.back.erase(h.back.begin());
    h.forward.clear();
}

bool can_back(const NavHistory& h) { return !h.back.empty(); }
bool can_forward(const NavHistory& h) { return !h.forward.empty(); }

bool at_home(NavHistory& h, const FigureSnapshot& fsnap, int slot) {
    nav_track_home(h, fsnap);
    return same_view(capture_view(fsnap), h.home, slot);
}

bool nav_restore(const NavView& view, FigureContext& ctx, int slot) {
    SlotViewState& sv = slot_view(ctx);
    bool pushed = false;
    for (const NavLimits& l : view.limits) {
        if (!in_cell(l.key, slot)) continue;
        const RenderSnapshot* s = ctx.snap.find2d(l.key);
        if (!s) continue;
        // An axis the program set since the view was taken is the program's.
        const bool x = s->limit_stamps.x == l.stamps.x;
        const bool y = s->limit_stamps.y == l.stamps.y;
        if (!x && !y) continue;
        ctx.edits.update(l.key, [&](AxesEdit& e) {
            if (x) {
                e.xlim_auto = l.xauto;
                if (!l.xauto) { e.xmin = l.xmin; e.xmax = l.xmax; }
            }
            if (y) {
                e.ylim_auto = l.yauto;
                if (!l.yauto) { e.ymin = l.ymin; e.ymax = l.ymax; }
            }
            e.lim_seen = s->limit_stamps;
        });
        if (l.key == ctx.selection.key()) {
            if (x) {
                sv.xauto_local = l.xauto;
                if (!l.xauto) { sv.xmin_local = l.xmin; sv.xmax_local = l.xmax; }
            }
            if (y) {
                sv.yauto_local = l.yauto;
                if (!l.yauto) { sv.ymin_local = l.ymin; sv.ymax_local = l.ymax; }
            }
        }
        pushed = true;
    }
    for (const NavCamera& c : view.cameras) {
        if (slot >= 0 && c.slot != slot) continue;
        const RenderSnapshot3D* s = ctx.snap.find3d(c.slot);
        if (!s || s->camera_stamp != c.stamp) continue;
        ctx.edits.update3d(c.slot, [&](AxesEdit3D& e) {
            e.camera = c.camera;
            e.camera_seen = s->camera_stamp;
        });
        if (c.slot == ctx.selection.slot) sv.camera_local = c.camera;
        pushed = true;
    }
    return pushed;
}

bool nav_back(NavHistory& h, FigureContext& ctx) {
    if (h.back.empty()) return false;
    nav_track_home(h, ctx.snap);
    h.forward.push_back(capture_view(ctx.snap));
    const NavView v = std::move(h.back.back());
    h.back.pop_back();
    return nav_restore(v, ctx);
}

bool nav_forward(NavHistory& h, FigureContext& ctx) {
    if (h.forward.empty()) return false;
    nav_track_home(h, ctx.snap);
    h.back.push_back(capture_view(ctx.snap));
    const NavView v = std::move(h.forward.back());
    h.forward.pop_back();
    return nav_restore(v, ctx);
}

bool nav_home(NavHistory& h, FigureContext& ctx, int slot) {
    if (at_home(h, ctx.snap, slot)) return false;
    nav_begin_gesture(h, ctx.snap);
    return nav_restore(h.home, ctx, slot);
}

} // namespace sextant
