#include "plot_events.h"
#include "coord_transform3d.h"
#include "hint.h"
#include "key_names.h"

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

void collect_plot_events(PlotEventTracker& t, const PlotInputFrame& in,
                         const PlotEventInfo& what, const FigureSnapshot& fsnap,
                         const std::vector<AxesLayout>& layout,
                         std::uint32_t wanted, std::vector<Event>& out) {
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
                if (wanted & event_bit(EventKind::MouseDown)) {
                    Event e = pointer_event(EventKind::MouseDown);
                    e.button = b;
                    e.double_click = in.double_click[b];
                    e.consumed = t.held[b];
                    out.push_back(std::move(e));
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
