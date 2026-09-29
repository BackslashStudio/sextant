#pragma once
#include <functional>
#include <limits>
#include <string>

namespace sextant {
    // What happened to a figure's window. Connect a callback with
    // Figure::connect(); it receives an Event whose `kind` says which fields mean
    // anything.
    enum class EventKind : int {
        Close,      // the window closed (by the user or by Figure::close())
        MouseDown,  // a button went down over the plot
        MouseUp,    // ...and came up; also off the plot, if the press began on it
        MouseMove,  // the cursor moved over the plot, or during a press begun on it
        Scroll,     // the wheel turned over the plot
        KeyDown,    // a key went down (no auto-repeat) while no text field is being edited
        KeyUp,
        Resize      // the plot area changed size; not sent for the initial size
    };

    // Modifier bits, in Event::mods.
    enum EventMods : int {
        kModCtrl = 1 << 0,
        kModShift = 1 << 1,
        kModAlt = 1 << 2,
        kModSuper = 1 << 3
    };

    // What sextant itself did with the input. Informational only: an event is
    // reported either way and a callback cannot suppress the default behaviour.
    enum class EventConsumed : int {
        None,
        Select,   // a click that selected the subplot under it (MouseUp)
        Navigate, // pan, zoom or view reset (Navigate on, the selected subplot)
        GridDrag  // a subplot-grid boundary was being dragged
    };

    // One flat record for every kind; the comments say which kinds fill a field.
    // Pixel coordinates are the figure's own: logical pixels, origin at the top
    // left of the plot area -- the frame savefig() writes at scale 1.
    struct Event {
        EventKind kind = EventKind::Close;

        // MouseDown/Up/Move, Scroll, KeyDown/Up: EventMods.
        int mods = 0;

        // MouseDown/Up: 0 left, 1 right, 2 middle. double_click: MouseDown only.
        int button = 0;
        bool double_click = false;

        // MouseDown/Up/Move, Scroll.
        float x = 0.0f;
        float y = 0.0f;

        // The subplot whose data frame is under (x, y), as the index add_subplot()
        // takes (a span: its first cell), or -1 outside every frame.
        int axes = -1;

        // The data under (x, y). In a 2D axes: xdata and ydata, zdata is NaN. In a
        // 3D axes: the point where the cursor ray meets the nearest visible
        // Plane2D (all three); has_data is false where it meets none -- a 3D
        // cursor position is otherwise not a single point.
        bool has_data = false;
        double xdata = std::numeric_limits<double>::quiet_NaN();
        double ydata = std::numeric_limits<double>::quiet_NaN();
        double zdata = std::numeric_limits<double>::quiet_NaN();

        // Scroll: the wheel offset, in notches (positive y = away from the user);
        // events queued back to back are summed.
        double scroll_x = 0.0;
        double scroll_y = 0.0;

        EventConsumed consumed = EventConsumed::None;

        // KeyDown/Up: "a", "A" (shift), "ctrl+a", "escape", "f5", "left", ...
        // A letter is lower case, upper case with shift; any other key gets
        // "shift+" first, and "ctrl+", "alt+", "super+" before that.
        std::string key;

        // Resize: the new plot area, in logical pixels.
        int width = 0;
        int height = 0;
    };

    using EventCallback = std::function<void(const Event&)>;
} // namespace sextant
