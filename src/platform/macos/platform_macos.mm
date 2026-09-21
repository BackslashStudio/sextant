// CGL is deprecated as of 10.14 and is still the only GL there is on this
// platform; step 21.6 sets this for every translation unit.
#define GL_SILENCE_DEPRECATION

#include "../platform.h"

#include <dlfcn.h>
#include <initializer_list>
#include <pthread.h>
#include <stdexcept>
#include <string>
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

    // -------------------------------------------------------------------------
    // A GL context with no window: what savefig() draws into when no window is
    // open. CGL needs neither the main thread nor GLFW, so an export asked for
    // from a worker thread is served there and then, instead of queueing a
    // hidden window to a main thread that may never pump.
    // -------------------------------------------------------------------------
    struct OffscreenGL {
        CGLContextObj cgl = nullptr;
        // What was current on the creating thread, put back when this one goes.
        // Null for every caller but a render thread exporting its own figure,
        // which is the one case where clearing instead would leave a live render
        // loop with no context. Safe to hold raw: that context is this thread's,
        // and this thread is inside the export.
        CGLContextObj previous = nullptr;
    };

    namespace {
        // The windowed path asks GLFW for 4.1 core and the export has to agree
        // with it pixel for pixel, so this asks CGL for the same profile -- and
        // falls back the same way, dropping kCGLPFAAccelerated where no GPU
        // matched (a CI runner, a VNC session) to admit Apple's software
        // renderer. That retry is exactly what cmake/glfw/0001 adds for a
        // window; tests/gl_probe/cgl_probe.cpp is where this list was measured.
        // No depth or stencil in the format itself: nothing is ever drawn to
        // this context's own buffers, and FboReadback brings its own.
        CGLPixelFormatObj choose_pixel_format() {
            const CGLPixelFormatAttribute attrs[] = {
                kCGLPFAAccelerated, // dropped on the retry: must stay first
                kCGLPFAOpenGLProfile, static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_GL4_Core),
                kCGLPFAColorSize, static_cast<CGLPixelFormatAttribute>(24),
                kCGLPFAAlphaSize, static_cast<CGLPixelFormatAttribute>(8),
                static_cast<CGLPixelFormatAttribute>(0)
            };
            for (const CGLPixelFormatAttribute* a : {attrs, attrs + 1}) {
                CGLPixelFormatObj pix = nullptr;
                GLint n = 0;
                if (CGLChoosePixelFormat(a, &pix, &n) == kCGLNoError && pix && n > 0)
                    return pix;
                if (pix) CGLReleasePixelFormat(pix);
            }
            return nullptr;
        }
    } // namespace

    OffscreenGL* create_offscreen_gl() {
        CGLPixelFormatObj pix = choose_pixel_format();
        if (!pix)
            throw std::runtime_error(
                "CGLChoosePixelFormat: no offscreen OpenGL 4.1 core pixel format, "
                "accelerated or software");

        CGLContextObj cgl = nullptr;
        const CGLError err = CGLCreateContext(pix, nullptr, &cgl);
        CGLReleasePixelFormat(pix);
        if (err != kCGLNoError || !cgl)
            throw std::runtime_error(std::string("CGLCreateContext failed: ") +
                                     CGLErrorString(err));

        CGLContextObj previous = CGLGetCurrentContext();
        CGLSetCurrentContext(cgl);
        return new OffscreenGL{cgl, previous};
    }

    void destroy_offscreen_gl(OffscreenGL* c) {
        if (!c) return;
        if (CGLGetCurrentContext() == c->cgl) CGLSetCurrentContext(c->previous);
        CGLDestroyContext(c->cgl);
        delete c;
    }

    void make_offscreen_gl_current(OffscreenGL* c) {
        CGLSetCurrentContext(c ? c->cgl : nullptr);
    }

    // dlsym on the framework, which is what GLFW's own NSGL loader does: the
    // same addresses, so a headless export and a window share one GLAD table
    // whichever of them loaded it.
    void* offscreen_gl_proc_address(const char* name) {
        static void* framework = dlopen(
            "/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL",
            RTLD_LAZY | RTLD_LOCAL);
        return framework ? dlsym(framework, name) : nullptr;
    }
} // namespace sextant::platform
