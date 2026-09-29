#pragma once
#include "sextant/events.h"
#include "plot_objects.h"
#include "render_frame.h"
#include <cstdint>
#include <vector>

namespace sextant {
// Turning a frame of pointer and keyboard input over the plot image into Events.
// GL-free and ImGui-free, so it is driven directly by the layout tests, as the
// selection and grid-drag logic beside it is. It only reports: nothing here
// changes what the panel does with the input.

// One frame of what the pointer and keys did, in plot (layout) pixels.
struct PlotInputFrame {
    float x = 0.0f, y = 0.0f;
    bool hovered = false;       // over the plot image with nothing on top of it
    bool down[3] = {};          // left, right, middle held now
    bool double_click[3] = {};  // the press this frame completes a double-click
    float wheel_x = 0.0f, wheel_y = 0.0f;
    int mods = 0;               // EventMods
    int width = 0, height = 0;  // the plot area, logical pixels; 0 = unknown
};

// What the panel did with this frame's pointer, for Event::consumed.
struct PlotEventInfo {
    bool grid_owns = false;     // a grid boundary owns the pointer
    bool nav_drag = false;      // Navigate is on and a drag on the selected cell may pan
    bool nav_wheel = false;     // Navigate is on and the wheel may zoom
    bool nav_reset = false;     // Navigate is on and this double-click resets the view
    bool selected_now = false;  // a click selected a cell this frame
};

// Carried from frame to frame, in PanelState.
struct PlotEventTracker {
    bool prev_down[3] = {};
    bool began[3] = {};                                  // the press started on the plot
    EventConsumed held[3] = {};                          // what sextant did with that press
    bool have_pos = false;
    float last_x = 0.0f, last_y = 0.0f;
    bool have_size = false;
    int last_w = 0, last_h = 0;
};

// The subplot and data under a pixel; see Event::axes / has_data.
struct PointLocation {
    int axes = -1;
    bool has_data = false;
    double x = 0.0, y = 0.0, z = 0.0;
};

PointLocation locate_point(const FigureSnapshot& fsnap,
                           const std::vector<AxesLayout>& layout, float x, float y);

// Appends this frame's mouse, scroll and resize events to `out`, only for the
// kinds set in `wanted` (bit = EventKind); the tracker advances either way.
void collect_plot_events(PlotEventTracker& t, const PlotInputFrame& in,
                         const PlotEventInfo& what, const FigureSnapshot& fsnap,
                         const std::vector<AxesLayout>& layout,
                         std::uint32_t wanted, std::vector<Event>& out);

// One key transition (a WindowEvent::Kind::Key's fields) as an event; false for
// a key with no name.
bool make_key_event(int glfw_key, int mods, bool down, Event& out);

inline std::uint32_t event_bit(EventKind k) { return 1u << static_cast<int>(k); }
} // namespace sextant
