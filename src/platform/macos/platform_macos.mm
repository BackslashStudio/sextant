#include "../platform.h"

#include <pthread.h>
#include <OpenGL/OpenGL.h>
#import <AppKit/AppKit.h>

namespace sextant::platform {
    bool this_thread_owns_windows() { return pthread_main_np() != 0; }

    // CGLGetCurrentContext() is null on a thread with no context current, which
    // is every thread but a render thread between make-current and teardown --
    // so the lock is simply skipped there rather than guessing at a context.
    void lock_current_gl_context() {
        if (CGLContextObj ctx = CGLGetCurrentContext()) CGLLockContext(ctx);
    }

    void unlock_current_gl_context() {
        if (CGLContextObj ctx = CGLGetCurrentContext()) CGLUnlockContext(ctx);
    }

    // NSPasteboard is documented thread-safe, so the render thread reads it
    // itself instead of asking the pump and waiting a frame for the answer.
    // Writes still go through the request queue: they are not urgent, and one
    // path for them is one path to get wrong.
    bool read_clipboard(std::string& out) {
        @autoreleasepool {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            NSString* s = [pb stringForType:NSPasteboardTypeString];
            out = s ? [s UTF8String] : "";
        }
        return true;
    }
} // namespace sextant::platform
