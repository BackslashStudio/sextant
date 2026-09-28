#include "platform.h"

// Windows and Linux: every thread may own a window, no context lock is needed,
// and the clipboard is GLFW's to read. macOS has none of that; see
// macos/platform_macos.mm. A headless export on Windows draws into a hidden
// window, which costs nothing there; Linux has a windowless context instead
// (linux/offscreen_egl.cpp), since its windows need a display.
namespace sextant::platform {
    bool this_thread_owns_windows() { return true; }

    void lock_current_gl_context() {
    }

    void unlock_current_gl_context() {
    }

    bool read_clipboard(std::string&) { return false; }

    bool console_input_ready() { return false; }

#if !defined(__linux__)   // Linux's are in linux/offscreen_egl.cpp
    OffscreenGL* create_offscreen_gl() { return nullptr; }

    void destroy_offscreen_gl(OffscreenGL*) {
    }

    void make_offscreen_gl_current(OffscreenGL*) {
    }

    void* offscreen_gl_proc_address(const char*) { return nullptr; }

    const char* offscreen_gl_hint() { return ""; }
#endif
} // namespace sextant::platform
