#include "plot_events.h"
#include "coord_transform3d.h"
#include "hint.h"
#include "key_names.h"
#include <optional>

namespace sextant {
namespace {
    const FigureAxesSnapshot* find_axes(const FigureSnapshot& fsnap, int slot_index) {
        for (const FigureAxesSnapshot& a : fsnap.axes)
            if (a.slot.index == slot_index) return &a;
        return nullptr;
    }

    void fill_location(Event& e, const PointLocation& p) {
        e.axes = p.axes;
        e.has_data = p.has_data;
        if (!p.has_data) return;
        e.xdata = p.x;
        e.ydata = p.y;
        // A 2D axes has no third coordinate; locate_point() leaves it NaN there.
        e.zdata = p.z;
    }
} // namespace

PointLocation locate_point(const FigureSnapshot& fsnap,
                           const std::vector<AxesLayout>& layout, float x, float y) {
    PointLocation loc;
    const AxesLayout* cell = find_hint_cell(layout, x, y);
    if (!cell) return loc;
    loc.axes = cell->slot.index;

    const FigureAxesSnapshot* fa = find_axes(fsnap, cell->slot.index);
    if (!fa) return loc;

    if (fa->snap2d()) {
        loc.has_data = true;
        loc.x = cell->tr.to_data_x(x);
        loc.y = cell->tr.to_data_y(y);
        loc.z = std::numeric_limits<double>::quiet_NaN();
        return loc;
    }

    // 3D: the nearest visible plane the cursor ray meets.
    if (const RenderSnapshot3D* s3 = fa->snap3d(); s3 && cell->proj3d) {
        float best_depth = 0.0f;
        for (const PlaneSnapshot& pl : s3->planes) {
            if (!plane_drawn(pl)) continue;
            double u = 0.0, v = 0.0;
            float depth = 0.0f;
            if (!plane_ray_hit(*cell->proj3d, pl.orient, pl.offset, x, y, u, v, depth)) continue;
            if (loc.has_data && depth >= best_depth) continue;
            const Vec3 p = plane_point(pl.orient, u, v, pl.offset);
            loc.has_data = true;
            best_depth = depth;
            loc.x = p.x; loc.y = p.y; loc.z = p.z;
        }
    }
    return loc;
}

static bool fill_pick(Event& e, const FigureSnapshot& fsnap, const std::vector<AxesLayout>& layout,
               HintIndexCache* index) {
    const AxesLayout* cell = find_hint_cell(layout, e.x, e.y);
    if (!cell) return false;
    const FigureAxesSnapshot* fa = find_axes(fsnap, cell->slot.index);
    if (!fa) return false;

    std::optional<PickHit> hit;
    if (fa->snap2d()) {
        if (index) index->set_frame_key(fsnap.data_generation, cell->slot.index);
        hit = find_pick(*fa->snap2d(), HintProjector(cell->tr), e.x, e.y, index);
    } else if (fa->snap3d() && cell->proj3d) {
        if (index) index->set_frame_key(fsnap.data_generation, cell->slot.index);
        hit = find_pick3d(*fa->snap3d(), *cell->proj3d, e.x, e.y, index);
    }
    if (!hit) return false;

    e.kind = EventKind::Pick;
    e.pick_kind = hit->kind;
    e.pick_object = static_cast<int>(hit->object);
    e.pick_index = static_cast<int>(hit->element);
    e.pick_row = hit->row;
    e.pick_col = hit->col;
    e.pick_plane = hit->plane;
    return true;
}

void collect_plot_events(PlotEventTracker& t, const PlotInputFrame& in,
                         const PlotEventInfo& what, const FigureSnapshot& fsnap,
                         const std::vector<AxesLayout>& layout,
                         std::uint32_t wanted, std::vector<Event>& out,
                         HintIndexCache* hint_index) {
    // The pointer's own fields, filled once and only if something needs them.
    bool located = false;
    PointLocation loc;
    auto pointer_event = [&](EventKind kind) {
        if (!located) { loc = locate_point(fsnap, layout, in.x, in.y); located = true; }
        Event e;
        e.kind = kind;
        e.mods = in.mods;
        e.x = in.x;
        e.y = in.y;
        fill_location(e, loc);
        return e;
    };

    bool any_began = false;
    for (int b = 0; b < 3; ++b) {
        if (in.down[b] && !t.prev_down[b]) {
            if (in.hovered) {
                t.began[b] = true;
                t.held[b] = EventConsumed::None;
                if (b == 0) {
                    if (what.grid_owns) t.held[b] = EventConsumed::GridDrag;
                    else if (what.nav_drag || what.nav_reset) t.held[b] = EventConsumed::Navigate;
                }
                const bool want_down = wanted & event_bit(EventKind::MouseDown);
                const bool want_pick = wanted & event_bit(EventKind::Pick);
                if (want_down || want_pick) {
                    Event e = pointer_event(EventKind::MouseDown);
                    e.button = b;
                    e.double_click = in.double_click[b];
                    e.consumed = t.held[b];
                    // The pick is the same press, found only when someone asked.
                    Event pick = e;
                    const bool picked = want_pick && fill_pick(pick, fsnap, layout, hint_index);
                    if (want_down) out.push_back(std::move(e));
                    if (picked) out.push_back(std::move(pick));
                }
            }
        } else if (!in.down[b] && t.prev_down[b] && t.began[b]) {
            if (wanted & event_bit(EventKind::MouseUp)) {
                Event e = pointer_event(EventKind::MouseUp);
                e.button = b;
                e.consumed = (b == 0 && what.selected_now) ? EventConsumed::Select : t.held[b];
                out.push_back(std::move(e));
            }
            t.began[b] = false;
        }
        t.prev_down[b] = in.down[b];
        any_began = any_began || t.began[b];
    }

    const bool moved = !t.have_pos || in.x != t.last_x || in.y != t.last_y;
    t.have_pos = true;
    t.last_x = in.x;
    t.last_y = in.y;
    if (moved && (in.hovered || any_began) && (wanted & event_bit(EventKind::MouseMove))) {
        Event e = pointer_event(EventKind::MouseMove);
        e.consumed = t.began[0] ? t.held[0]
                                : (what.grid_owns ? EventConsumed::GridDrag : EventConsumed::None);
        out.push_back(std::move(e));
    }

    if (in.hovered && (in.wheel_x != 0.0f || in.wheel_y != 0.0f)
        && (wanted & event_bit(EventKind::Scroll))) {
        Event e = pointer_event(EventKind::Scroll);
        e.scroll_x = in.wheel_x;
        e.scroll_y = in.wheel_y;
        e.consumed = what.nav_wheel ? EventConsumed::Navigate : EventConsumed::None;
        out.push_back(std::move(e));
    }

    if (in.width > 0 && in.height > 0) {
        const bool changed = t.have_size && (in.width != t.last_w || in.height != t.last_h);
        t.have_size = true;
        t.last_w = in.width;
        t.last_h = in.height;
        if (changed && (wanted & event_bit(EventKind::Resize))) {
            Event e;
            e.kind = EventKind::Resize;
            e.width = in.width;
            e.height = in.height;
            out.push_back(std::move(e));
        }
    }
}

bool make_key_event(int glfw_key, int mods, bool down, Event& out) {
    std::string name = key_event_name(glfw_key, mods);
    if (name.empty()) return false;
    out = Event{};
    out.kind = down ? EventKind::KeyDown : EventKind::KeyUp;
    out.mods = mods;
    out.key = std::move(name);
    return true;
}
} // namespace sextant
