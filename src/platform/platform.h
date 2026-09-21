#pragma once
#include <string>

// What differs on macOS, behind one header so nothing else has to know which
// platform it is compiled for. The implementations live in
// platform_generic.cpp and macos/platform_macos.mm.
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

    // Whether this platform can make a GL context with no window, no window
    // system and no particular thread -- what a headless savefig() wants, since
    // here a window is the main thread's business and an export has no reason
    // to be. False elsewhere, where a hidden window costs nothing because any
    // thread may make one.
    inline constexpr bool has_offscreen_gl =
#if defined(__APPLE__)
            true;
#else
            false;
#endif

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
    // the offscreen path, where glfwInit() has not necessarily happened and on
    // this platform could not happen off the main thread anyway.
    void* offscreen_gl_proc_address(const char* name);
} // namespace sextant::platform
