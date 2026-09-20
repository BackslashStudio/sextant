#include "platform.h"

// Windows and Linux: every thread may own a window, no context lock is needed,
// and the clipboard is GLFW's to read. macOS has none of that; see
// macos/platform_macos.mm.
namespace sextant::platform {
    bool this_thread_owns_windows() { return true; }

    void lock_current_gl_context() {
    }

    void unlock_current_gl_context() {
    }

    bool read_clipboard(std::string&) { return false; }
} // namespace sextant::platform
