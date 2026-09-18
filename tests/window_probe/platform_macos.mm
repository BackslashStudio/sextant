// macOS half of platform.h.
#define GL_SILENCE_DEPRECATION
#include "platform.h"

#import <AppKit/AppKit.h>
#include <OpenGL/OpenGL.h>

namespace wp {
    // CGLGetCurrentContext() is the thread's current context, which after
    // glfwMakeContextCurrent() is the window's NSOpenGLContext's CGL object --
    // the one GLFW's patched update locks.
    void lock_current_context() {
        if (CGLContextObj c = CGLGetCurrentContext()) CGLLockContext(c);
    }

    void unlock_current_context() {
        if (CGLContextObj c = CGLGetCurrentContext()) CGLUnlockContext(c);
    }

    bool read_clipboard_here(std::string& out) {
        @autoreleasepool {
            NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
            if (!s) return false;
            out = [s UTF8String];
            return true;
        }
    }
} // namespace wp
