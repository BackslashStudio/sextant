#pragma once
#include "object_id.h"
#include <cstddef>
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
        Resize,     // the plot area changed size; not sent for the initial size
        Pick        // a plot object was under the press: sent right after its MouseDown
    };

    // Modifier bits, in Event::mods.
    enum EventMods : int {
        kModCtrl = 1 << 0,
        kModShift = 1 << 1,
        kModAlt = 1 << 2,
        kModSuper = 1 << 3
    };

    // What a Pick event found. Objects are named by ObjectId; Axes::find_object()
    // gives the index within the kind -- the `i` of the read-back calls
    // (line_data(i), bar3d_data(i), ...).
    enum class PickKind : int {
        None,
        Line, Scatter, ScatterZ, Bar, Heatmap,                // 2D kinds, on an Axes or a Plane2D
        Bar3D, Surface, SurfaceTri, Scatter3D, Line3D,         // 3D kinds
        Quiver,                                               // 2D, appended
        Fill,                                                 // 2D (fill(), stack()), appended
        Poly, Circle, Wedge,                                  // 2D (shape_poly(), shape_circle(), shape_wedge()), appended
        HeatmapTri,                                           // 2D (heatmap_tri()), appended
        Contour,                                              // 2D (contour(), contourf()), appended
        Quiver3D,                                             // 3D (Axes3D::quiver3d()), appended
        Pie                                                   // 2D (Axes::pie()), appended; pick_index = the slice
    };

    // Where an object is now: its kind and its index within the kind, the `i` of
    // the read-back calls. Axes::find_object() gives it for an ObjectId.
    struct ObjectLocation {
        PickKind    kind  = PickKind::None;
        std::size_t index = 0;
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

        // A 2D axes with twins (Axes::twinx()/twiny()): xdata and ydata stay the
        // axes' own; y2data is the twinx's y under (x, y) and x2data the twiny's
        // x. NaN without that twin.
        double x2data = std::numeric_limits<double>::quiet_NaN();
        double y2data = std::numeric_limits<double>::quiet_NaN();

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

        // Pick (the position fields above describe the press): what was under it.
        // A point, marker, arrow or path vertex within the pick radius
        // (Figure::set_pick_radius(), 12 by default) answers first, the nearest;
        // else the topmost bar, fill, heatmap or heatmap_tri the press is inside, whose
        // position is the event's xdata/ydata. `pick_object`: the object's ObjectId
        // (0 = none; Axes::find_object() or Plane2D::find_object() maps it to the
        // index within its kind, nothing if the object was removed since);
        // `pick_index`: the element -- point, bar, marker or vertex index; a sample of a surface;
        // the flat cell index (row * columns + column) of a heatmap or bar3d; a
        // fill's segment i, between its points i and i + 1; a heatmap_tri's triangle t
        // (tri[3t] .. tri[3t + 2]); a contour's level index (the line near the press)
        // or a contourf's band index (level i to i + 1); a pie's slice;
        // `pick_row`/`pick_col` for those grids, else -1. `pick_plane` is the
        // plane's ObjectId (Axes3D::plane_index() maps it to the index in
        // plane_at()), or 0 for an object of the axes itself. `pick_index`,
        // `pick_row` and `pick_col` refer to the data the window drew: a program
        // that changed the data since may see them out of date.
        PickKind pick_kind = PickKind::None;
        ObjectId pick_object = 0;
        int pick_index = -1;
        int pick_row = -1;
        int pick_col = -1;
        ObjectId pick_plane = 0;
        // Whose object `pick_object` is in a 2D cell with twins: 0 the axes' own,
        // 1 its twinx(), 2 its twiny() -- the Axes to ask find_object(). Of two
        // equally near points, or two areas, the later call wins, whichever axes
        // took it.
        int pick_twin = 0;
    };

    using EventCallback = std::function<void(const Event&)>;
} // namespace sextant
