#pragma once
#include <atomic>
#include <string>

// What differs by platform, behind one header so nothing else has to know which
// one it is compiled for. The implementations live in platform_generic.cpp
// (Windows, Linux), macos/platform_macos.mm, and linux/offscreen_egl.cpp (the
// Linux offscreen context).
namespace sextant::platform {
    // True where the window system insists that windows are created, destroyed
    // and pumped on one particular thread. A property of the platform, not of
    // the run, so `if constexpr` can drop the other path entirely.
    inline constexpr bool windows_on_main_thread =
#if defined(__APPLE__)
            true;
#else
            false;
#endif

    // Whether the calling thread is one that may own and pump windows. On macOS
    // that is the main thread alone; everywhere else every thread may, so this
    // is always true.
    bool this_thread_owns_windows();

    // The current GL context's own lock, which the render thread holds across
    // render and swap. On macOS it is CGLLockContext -- the same lock the
    // patched GLFW takes around [NSOpenGLContext update], which the main thread
    // runs while a resize is in flight. Nothing else needs one: both are empty
    // elsewhere.
    void lock_current_gl_context();

    void unlock_current_gl_context();

    struct GLContextLock {
        GLContextLock() { lock_current_gl_context(); }

        ~GLContextLock() { unlock_current_gl_context(); }

        GLContextLock(const GLContextLock&) = delete;

        GLContextLock& operator=(const GLContextLock&) = delete;
    };

    // Reads the clipboard on the calling thread. True when that worked here --
    // on macOS, where NSPasteboard is readable from any thread and GLFW's own
    // clipboard call is not. False elsewhere, where the caller should ask GLFW.
    bool read_clipboard(std::string& out);

    // Whether reading stdin now would not block: a line is waiting, or it is at
    // EOF. What lets show(true) wait for ENTER on the thread that also has to
    // pump the window. Only called where that thread pumps (macOS); false
    // elsewhere.
    bool console_input_ready();

    // Whether this platform can make a GL context with no window, no window
    // system and no particular thread -- what a headless savefig() wants. macOS:
    // CGL, since a window there is the main thread's business. Linux: EGL, since
    // a window there needs a display, which a container, a CI runner or an SSH
    // session does not have. False on Windows, where a hidden window costs
    // nothing and every session has a desktop.
    inline constexpr bool has_offscreen_gl =
#if defined(__APPLE__) || defined(__linux__)
            true;
#else
            false;
#endif

    // Test hook, not API: off makes headless exports take the hidden-window
    // path even where has_offscreen_gl, so a test can compare the two. On by
    // default; process-wide.
    inline std::atomic<bool>& offscreen_gl_switch() {
        static std::atomic<bool> on{true};
        return on;
    }

    inline void set_offscreen_gl_enabled(bool on) { offscreen_gl_switch().store(on); }

    inline bool offscreen_gl_enabled() { return offscreen_gl_switch().load(); }

    // One offscreen GL context. Opaque: what is inside it is the platform's.
    struct OffscreenGL;

    // Makes one and makes it current on the calling thread, which is then the
    // only thread that uses it. Null where the platform has none, so the caller
    // falls back to a hidden window; throws std::runtime_error where it has one
    // and it failed.
    OffscreenGL* create_offscreen_gl();

    // Unmakes it, clearing it first if it is the calling thread's current one.
    void destroy_offscreen_gl(OffscreenGL* c);

    void make_offscreen_gl_current(OffscreenGL* c);

    // One GL entry point by name, without GLFW -- what GLAD is loaded with on
    // the offscreen path, where glfwInit() has not necessarily happened (and on
    // macOS could not off the main thread; on Linux may have no display to
    // happen with). GLAD's table is process-wide, so these must be the same
    // entry points a window's context would get.
    void* offscreen_gl_proc_address(const char* name);

    // What a failed headless export should tell the user to do about it, or
    // empty. Appended to the error when neither context could be made.
    const char* offscreen_gl_hint();
} // namespace sextant::platform
