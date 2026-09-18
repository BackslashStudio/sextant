// The two things window_probe needs that differ on macOS. Throwaway prototype
// for v1.0 step 21.1; see main.cpp.
#pragma once

#include <string>

namespace wp {
    // The render thread holds its context's CGL lock across render + swap, the
    // same lock the patched GLFW takes around [NSOpenGLContext update] on the
    // main thread (cmake/glfw/0002). No-ops elsewhere.
    void lock_current_context();
    void unlock_current_context();

    // Reads the system clipboard from the calling thread -- on macOS straight
    // from NSPasteboard, which is what the design wants to do on the render
    // thread. False where that is not how it is done (Windows/Linux).
    bool read_clipboard_here(std::string& out);
} // namespace wp
