#include "platform.h"

// Windows and Linux: every thread may own a window, no context lock is needed,
// the clipboard is GLFW's to read, and a headless export draws into a hidden
// window because making one costs nothing here. macOS has none of that; see
// macos/platform_macos.mm.
namespace sextant::platform {
    bool this_thread_owns_windows() { return true; }

    void lock_current_gl_context() {
    }

    void unlock_current_gl_context() {
    }

    bool read_clipboard(std::string&) { return false; }

    OffscreenGL* create_offscreen_gl() { return nullptr; }

    void destroy_offscreen_gl(OffscreenGL*) {
    }

    void make_offscreen_gl_current(OffscreenGL*) {
    }

    void* offscreen_gl_proc_address(const char*) { return nullptr; }
} // namespace sextant::platform
