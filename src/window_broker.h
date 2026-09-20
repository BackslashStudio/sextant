#pragma once
#include <functional>
#include <memory>
#include <string>

struct GLFWwindow;

namespace sextant {
    class WindowLink;

    // Who makes and unmakes windows, and who pumps them.
    //
    // GLFW puts window creation, destruction and the event pump on one thread on
    // macOS -- the main one -- while everything a frame needs (make-current,
    // swap, and GL itself) is free to live anywhere. So those two halves are
    // split: the broker owns the first, the render thread keeps the second.
    //
    // On Windows and Linux the broker does its work inline on the calling thread
    // and the pump is each window thread's own, exactly as before; the split is
    // still there, so the same code path is the one that runs everywhere.

    struct WindowSpec {
        int width = 800;
        int height = 600;
        std::string title = "sextant";
        bool visible = true;
        bool resizable = true;
        bool scale_to_monitor = false;
    };

    // A window and the link carrying its events and state. The broker keeps its
    // own reference to the link until the window is really gone, because the
    // window's callbacks reach the link through its user pointer and a destroy
    // may be served long after the render thread has let go.
    struct BrokeredWindow {
        GLFWwindow* window = nullptr;
        std::shared_ptr<WindowLink> link;
    };

    // Makes a window with its link attached. Inline when this thread may own one
    // -- always, outside macOS. Otherwise the request is queued for the pump and
    // this blocks until the window arrives, warning once on stderr after about
    // two seconds: a caller that has simply not started pumping yet is a correct
    // program, so refusing it outright would reject more than it saved.
    BrokeredWindow create_window(const WindowSpec& spec);

    // Hands a window back. Inline where this thread may destroy it, otherwise
    // queued without waiting -- so a render thread on its way out never blocks
    // on the pump, and a close() on the pumping thread never waits on itself.
    // The link stops being pumped at once either way, and a window handed back
    // twice is dropped the second time.
    void destroy_window(GLFWwindow* w);

    // One round of the pump, for the thread that owns windows: queued broker
    // work, GLFW's events, then every live window's state mirror and whatever
    // its render thread asked for. timeout_s > 0 waits that long for something
    // to happen rather than returning at once.
    //
    // A no-op where each window thread pumps its own window; where it is not,
    // calling it off the owning thread throws std::logic_error.
    void pump_windows(double timeout_s);

    // Whether pump_windows() does anything on this thread -- true only on the
    // macOS main thread. What a wait uses to choose between pumping and blocking.
    bool pump_runs_here();

    // Waits for `done`, pumping while it waits. Returns false if timeout_s ran
    // out first; a negative timeout waits forever. Pumping thread only (it is
    // pump_windows() in a loop).
    bool pump_until(const std::function<bool()>& done, double timeout_s);

    // Serves queued broker work without pumping GLFW: what a close() does after
    // joining its render thread, so the window goes now rather than at whatever
    // poll happens next. Does nothing off the owning thread.
    void serve_broker_requests();

    // How many windows the broker is holding -- exactly the ones its pump walks.
    int live_window_count();
} // namespace sextant
