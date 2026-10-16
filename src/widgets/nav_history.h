#pragma once
// Navigation history: Back, Forward and Home over the views a figure's window
// showed. GL- and ImGui-free. A view is the whole figure's limits and cameras;
// the plot view records one when a navigation gesture starts, the menu (or a
// host) walks them. Restores go through the edit box like any navigation, and
// skip an axis the program has set since the view was taken.
#include "sextant/axes3d.h"
#include "../plot_objects.h"
#include <vector>

namespace sextant {
    struct FigureContext;

    // One 2D axes (twins included, by axes_key()) in a view: its declared
    // limits and auto flags, and the stamps they were taken under.
    struct NavLimits {
        int key = 0;
        double xmin = 0, xmax = 1, ymin = 0, ymax = 1;
        bool xauto = true, yauto = true;
        LimitStamps stamps;
    };

    // One 3D axes' camera in a view.
    struct NavCamera {
        int slot = 0;
        Camera3D camera;
        unsigned long long stamp = 0;
    };

    struct NavView {
        std::vector<NavLimits> limits;
        std::vector<NavCamera> cameras;
    };

    // What the plot view saw this frame, for gesture detection.
    struct NavInput {
        bool drag = false;      // a navigating drag is held (pan or orbit)
        bool moved = false;     // ...and it moved the view this frame
        bool box_done = false;  // a box zoom is applied this frame
        bool wheel = false;     // the wheel navigated this frame
        bool fly = false;       // a fly key navigated this frame
        int slot = -1;          // the cell navigated
        double time = 0.0;      // seconds (io.Time)
    };

    struct NavHistory {
        // Wheel notches further apart than this are separate gestures.
        static constexpr double kWheelGap = 0.5;
        // The most views Back keeps; the oldest goes first.
        static constexpr std::size_t kMaxBack = 100;

        // What the program last asked for: the first view drawn, each axis
        // re-taken when its stamp moves (nav_track_home()).
        NavView home;
        bool have_home = false;
        std::vector<NavView> back, forward;

        // Gesture tracking (nav_gesture_starts()).
        bool in_drag = false;
        bool in_fly = false;
        int wheel_slot = -1;
        double wheel_time = -1.0e30;
    };

    // The view `fsnap` shows (declared limits; for an automatic axis only the
    // flag means anything).
    NavView capture_view(const FigureSnapshot& fsnap);

    // Whether two views show the same thing: equal flags, equal limits where
    // not automatic, equal cameras. Stamps are not compared. With `slot` >= 0
    // only that cell's axes (its layers) count.
    bool same_view(const NavView& a, const NavView& b, int slot = -1);

    // Keeps Home current: taken from the first snapshot seen; an axis whose stamp
    // differs from Home's is re-taken (the program set it, and the snapshot shows
    // its value); new axes join, gone ones leave. A 3D axes with
    // set_default_camera() has that camera as Home's. Once per frame, and before any
    // other call below reads Home.
    void nav_track_home(NavHistory& h, const FigureSnapshot& fsnap);

    // Whether this frame's input starts a gesture; updates the tracking. A drag
    // starts at its first moving frame, a wheel run at a notch over another cell
    // or after kWheelGap, a fly run at its first frame, a box at its release.
    bool nav_gesture_starts(NavHistory& h, const NavInput& in);

    // Records the view on screen before a gesture changes it: pushed on Back,
    // Forward cleared.
    void nav_begin_gesture(NavHistory& h, const FigureSnapshot& fsnap);

    bool can_back(const NavHistory& h);
    bool can_forward(const NavHistory& h);
    // Whether the screen shows Home (all of it, or one cell with `slot`).
    bool at_home(NavHistory& h, const FigureSnapshot& fsnap, int slot = -1);

    // Each returns whether it pushed any edit. Back and Forward swap the view on
    // screen with the next one; Home restores Home's view (one cell's with
    // `slot` >= 0: the double-click), recorded as a gesture so Back undoes it.
    // The edits reach the next frame; the slot view's locals follow now.
    bool nav_back(NavHistory& h, FigureContext& ctx);
    bool nav_forward(NavHistory& h, FigureContext& ctx);
    bool nav_home(NavHistory& h, FigureContext& ctx, int slot = -1);

    // Pushes the edits that put `view` on screen (one cell's with `slot` >= 0),
    // skipping each axis whose stamp moved since. Whether it pushed any.
    bool nav_restore(const NavView& view, FigureContext& ctx, int slot = -1);
} // namespace sextant
