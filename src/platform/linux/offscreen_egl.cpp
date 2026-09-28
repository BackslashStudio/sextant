#include "../platform.h"

#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// A GL context with no window and no display server: what a headless savefig()
// draws into on Linux, so an export works in a container, on a CI runner or
// over SSH, and from any thread without a second GLFW thread.
//
// libEGL is dlopen()ed, never linked. A Python wheel may not depend on it
// (manylinux allows libGL.so.1, not libEGL.so.1), and it is a vendor-dispatch
// library that has to be the system's, so bundling it is not an option either.
// A machine without it keeps the hidden-window path it always had.
//
// The EGL declarations below are the handful this file uses, from the Khronos
// EGL 1.5 registry, rather than <EGL/egl.h>: every call goes through a
// dlsym()ed pointer anyway, and a build then needs no libegl-dev.
namespace sextant::platform {
    namespace {
        using EGLint       = std::int32_t;
        using EGLBoolean   = unsigned int;
        using EGLenum      = unsigned int;
        using EGLDisplay   = void*;
        using EGLConfig    = void*;
        using EGLContext   = void*;
        using EGLSurface   = void*;
        using EGLDeviceEXT = void*;

        constexpr EGLint  EGL_NONE               = 0x3038;
        constexpr EGLint  EGL_RENDERABLE_TYPE    = 0x3040;
        constexpr EGLint  EGL_OPENGL_BIT         = 0x0008;
        constexpr EGLint  EGL_EXTENSIONS         = 0x3055;
        constexpr EGLint  EGL_DRAW               = 0x3059;
        constexpr EGLint  EGL_READ               = 0x305A;
        constexpr EGLenum EGL_OPENGL_API         = 0x30A2;
        // EGL_KHR_create_context (core in 1.5 under the same values).
        constexpr EGLint  EGL_CONTEXT_MAJOR_VERSION_KHR               = 0x3098;
        constexpr EGLint  EGL_CONTEXT_MINOR_VERSION_KHR               = 0x30FB;
        constexpr EGLint  EGL_CONTEXT_FLAGS_KHR                       = 0x30FC;
        constexpr EGLint  EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR         = 0x30FD;
        constexpr EGLint  EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR     = 0x0001;
        constexpr EGLint  EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR = 0x0002;
        // Platforms (EGL_MESA_platform_surfaceless, EGL_EXT_platform_device).
        constexpr EGLenum EGL_PLATFORM_SURFACELESS_MESA = 0x31DD;
        constexpr EGLenum EGL_PLATFORM_DEVICE_EXT       = 0x313F;

        constexpr EGLDisplay EGL_NO_DISPLAY = nullptr;
        constexpr EGLContext EGL_NO_CONTEXT = nullptr;
        constexpr EGLSurface EGL_NO_SURFACE = nullptr;
        constexpr EGLConfig  EGL_NO_CONFIG_KHR = nullptr;

        // The entry points, resolved once.
        struct Egl {
            void* lib = nullptr;
            EGLDisplay (*GetDisplay)(void*) = nullptr;
            EGLBoolean (*Initialize)(EGLDisplay, EGLint*, EGLint*) = nullptr;
            const char* (*QueryString)(EGLDisplay, EGLint) = nullptr;
            EGLBoolean (*BindAPI)(EGLenum) = nullptr;
            EGLBoolean (*ChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) = nullptr;
            EGLContext (*CreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*) = nullptr;
            EGLBoolean (*DestroyContext)(EGLDisplay, EGLContext) = nullptr;
            EGLBoolean (*MakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
            EGLContext (*GetCurrentContext)() = nullptr;
            EGLDisplay (*GetCurrentDisplay)() = nullptr;
            EGLSurface (*GetCurrentSurface)(EGLint) = nullptr;
            EGLint (*GetError)() = nullptr;
            void* (*GetProcAddress)(const char*) = nullptr;
            // Extensions, through GetProcAddress.
            EGLDisplay (*GetPlatformDisplayEXT)(EGLenum, void*, const EGLint*) = nullptr;
            EGLBoolean (*QueryDevicesEXT)(EGLint, EGLDeviceEXT*, EGLint*) = nullptr;
        };

        // The one display every offscreen context is made on, found once.
        // Never terminated: eglTerminate() would pull the contexts out from
        // under every other thread's export.
        struct Display {
            EGLDisplay dpy = EGL_NO_DISPLAY;
            EGLConfig config = EGL_NO_CONFIG_KHR;
            std::string error;   // why there is none, if there is none
        };

        bool has_ext(const char* list, std::string_view name) {
            if (!list) return false;
            std::string_view s(list);
            while (!s.empty()) {
                const auto sp = s.find(' ');
                if (s.substr(0, sp) == name) return true;
                if (sp == std::string_view::npos) break;
                s.remove_prefix(sp + 1);
            }
            return false;
        }

        template <class F>
        bool load(void* lib, F& f, const char* name) {
            f = reinterpret_cast<F>(dlsym(lib, name));
            return f != nullptr;
        }

        // Empty `error` on success.
        Egl open_egl(std::string& error) {
            Egl e;
            for (const char* name : {"libEGL.so.1", "libEGL.so"})
                if ((e.lib = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
            if (!e.lib) {
                error = "libEGL.so.1 not found";
                return e;
            }
            const bool ok = load(e.lib, e.GetDisplay, "eglGetDisplay")
                         && load(e.lib, e.Initialize, "eglInitialize")
                         && load(e.lib, e.QueryString, "eglQueryString")
                         && load(e.lib, e.BindAPI, "eglBindAPI")
                         && load(e.lib, e.ChooseConfig, "eglChooseConfig")
                         && load(e.lib, e.CreateContext, "eglCreateContext")
                         && load(e.lib, e.DestroyContext, "eglDestroyContext")
                         && load(e.lib, e.MakeCurrent, "eglMakeCurrent")
                         && load(e.lib, e.GetCurrentContext, "eglGetCurrentContext")
                         && load(e.lib, e.GetCurrentDisplay, "eglGetCurrentDisplay")
                         && load(e.lib, e.GetCurrentSurface, "eglGetCurrentSurface")
                         && load(e.lib, e.GetError, "eglGetError")
                         && load(e.lib, e.GetProcAddress, "eglGetProcAddress");
            if (!ok) {
                error = "libEGL.so.1 lacks an EGL 1.4 entry point";
                return e;
            }
            e.GetPlatformDisplayEXT = reinterpret_cast<decltype(e.GetPlatformDisplayEXT)>(
                e.GetProcAddress("eglGetPlatformDisplayEXT"));
            e.QueryDevicesEXT = reinterpret_cast<decltype(e.QueryDevicesEXT)>(
                e.GetProcAddress("eglQueryDevicesEXT"));
            return e;
        }

        // Opened once; `error` is empty when the table is complete.
        struct EglLoad {
            Egl e;
            std::string error;
        };

        const EglLoad& egl_load() {
            static const EglLoad l = [] {
                EglLoad r;
                r.e = open_egl(r.error);
                return r;
            }();
            return l;
        }

        const Egl& egl() { return egl_load().e; }

        std::string egl_error_hex(const Egl& e) {
            char b[16];
            std::snprintf(b, sizeof b, "0x%04X", static_cast<unsigned>(e.GetError()));
            return b;
        }

        // Asks for exactly what the window asks GLFW for (window_broker.cpp):
        // 4.1 core, forward-compatible -- so the two outputs can agree pixel
        // for pixel.
        const EGLint kContextAttribs[] = {
            EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
            EGL_CONTEXT_MINOR_VERSION_KHR, 1,
            EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
            EGL_CONTEXT_FLAGS_KHR, EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
            EGL_NONE
        };

        // Initialize `dpy` and check it can make the context this file needs.
        // Empty on success, else why not.
        std::string try_display(const Egl& e, EGLDisplay dpy, EGLConfig& config) {
            if (dpy == EGL_NO_DISPLAY) return "no display";
            EGLint major = 0, minor = 0;
            if (!e.Initialize(dpy, &major, &minor)) return "eglInitialize failed";
            const char* ext = e.QueryString(dpy, EGL_EXTENSIONS);
            const bool v15 = major > 1 || (major == 1 && minor >= 5);
            if (!v15 && !has_ext(ext, "EGL_KHR_create_context"))
                return "no EGL_KHR_create_context";
            if (!has_ext(ext, "EGL_KHR_surfaceless_context"))
                return "no EGL_KHR_surfaceless_context";
            if (!e.BindAPI(EGL_OPENGL_API)) return "no desktop OpenGL";

            config = EGL_NO_CONFIG_KHR;
            if (!has_ext(ext, "EGL_KHR_no_config_context")) {
                const EGLint want[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
                EGLint n = 0;
                if (!e.ChooseConfig(dpy, want, &config, 1, &n) || n < 1)
                    return "no OpenGL config";
            }
            // A trial context: a display that initializes may still not do 4.1 core.
            EGLContext ctx = e.CreateContext(dpy, config, EGL_NO_CONTEXT, kContextAttribs);
            if (ctx == EGL_NO_CONTEXT) return "no OpenGL 4.1 core context";
            e.DestroyContext(dpy, ctx);
            return {};
        }

        // Mesa's surfaceless platform first (a GPU render node, or llvmpipe
        // with none), then EGL devices (NVIDIA's driver has no surfaceless
        // platform), then the default display. The first that can make the
        // context wins.
        Display find_display() {
            Display d;
            if (!egl_load().error.empty()) {
                d.error = egl_load().error;
                return d;
            }
            const Egl& e = egl();
            const char* client = e.QueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
            const bool platforms = e.GetPlatformDisplayEXT && has_ext(client, "EGL_EXT_platform_base");
            std::string why;
            auto attempt = [&](EGLDisplay dpy, const char* what) {
                EGLConfig config = EGL_NO_CONFIG_KHR;
                const std::string err = try_display(e, dpy, config);
                if (err.empty()) {
                    d.dpy = dpy;
                    d.config = config;
                    return true;
                }
                why += std::string(why.empty() ? "" : "; ") + what + ": " + err;
                return false;
            };

            if (platforms && has_ext(client, "EGL_MESA_platform_surfaceless")
                && attempt(e.GetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr),
                           "surfaceless"))
                return d;

            if (platforms && e.QueryDevicesEXT && has_ext(client, "EGL_EXT_platform_device")) {
                EGLint n = 0;
                if (e.QueryDevicesEXT(0, nullptr, &n) && n > 0) {
                    std::vector<EGLDeviceEXT> devs(static_cast<std::size_t>(n));
                    if (e.QueryDevicesEXT(n, devs.data(), &n))
                        for (EGLint i = 0; i < n; ++i)
                            if (attempt(e.GetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, devs[i], nullptr),
                                        "device"))
                                return d;
                }
            }

            if (attempt(e.GetDisplay(nullptr), "default display")) return d;

            d.error = "no EGL display could make an OpenGL 4.1 core context (" + why + ")";
            return d;
        }

        const Display& display() {
            static const Display d = find_display();
            return d;
        }
    } // namespace

    struct OffscreenGL {
        EGLDisplay dpy = EGL_NO_DISPLAY;
        EGLContext ctx = EGL_NO_CONTEXT;
        // What was current on the creating thread, put back when this one goes
        // (see platform_macos.mm's OffscreenGL). Only EGL's own: a GLX context
        // current here is replaced, as a hidden window's would replace it.
        EGLDisplay prev_dpy = EGL_NO_DISPLAY;
        EGLContext prev_ctx = EGL_NO_CONTEXT;
        EGLSurface prev_draw = EGL_NO_SURFACE;
        EGLSurface prev_read = EGL_NO_SURFACE;
    };

    OffscreenGL* create_offscreen_gl() {
        const Display& d = display();
        if (d.dpy == EGL_NO_DISPLAY) throw std::runtime_error(d.error);
        const Egl& e = egl();

        // The rendering API is per thread, so it is bound on every one.
        if (!e.BindAPI(EGL_OPENGL_API))
            throw std::runtime_error("eglBindAPI(EGL_OPENGL_API) failed");
        EGLContext ctx = e.CreateContext(d.dpy, d.config, EGL_NO_CONTEXT, kContextAttribs);
        if (ctx == EGL_NO_CONTEXT)
            throw std::runtime_error("eglCreateContext failed (EGL error " + egl_error_hex(e) + ")");

        auto* c = new OffscreenGL{d.dpy, ctx, e.GetCurrentDisplay(), e.GetCurrentContext(),
                                  e.GetCurrentSurface(EGL_DRAW), e.GetCurrentSurface(EGL_READ)};
        // No surface: an export draws only into its own FBO.
        if (!e.MakeCurrent(d.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
            const std::string err = egl_error_hex(e);
            e.DestroyContext(d.dpy, ctx);
            delete c;
            throw std::runtime_error("eglMakeCurrent with no surface failed (EGL error " + err + ")");
        }
        return c;
    }

    void destroy_offscreen_gl(OffscreenGL* c) {
        if (!c) return;
        const Egl& e = egl();
        if (e.GetCurrentContext() == c->ctx) {
            if (c->prev_ctx != EGL_NO_CONTEXT)
                e.MakeCurrent(c->prev_dpy, c->prev_draw, c->prev_read, c->prev_ctx);
            else
                e.MakeCurrent(c->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        e.DestroyContext(c->dpy, c->ctx);
        delete c;
    }

    void make_offscreen_gl_current(OffscreenGL* c) {
        const Egl& e = egl();
        if (c) e.MakeCurrent(c->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, c->ctx);
        else if (EGLDisplay cur = e.GetCurrentDisplay())
            e.MakeCurrent(cur, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    // Core entry points from libOpenGL/libGL, the rest from EGL. Under
    // libglvnd both libraries export the same dispatch stubs GLX hands a
    // window's context, which is what lets GLAD's one process-wide table serve
    // an EGL export and a GLX window alike, whichever loaded it first.
    void* offscreen_gl_proc_address(const char* name) {
        static void* gl = [] {
            for (const char* lib : {"libOpenGL.so.0", "libGL.so.1"})
                if (void* h = dlopen(lib, RTLD_NOW | RTLD_LOCAL)) return h;
            return static_cast<void*>(nullptr);
        }();
        if (gl)
            if (void* p = dlsym(gl, name)) return p;
        const Egl& e = egl();
        return e.GetProcAddress ? e.GetProcAddress(name) : nullptr;
    }

    const char* offscreen_gl_hint() {
        return "For exports without a display, install EGL and Mesa "
               "(Debian/Ubuntu: apt install libegl1 libgl1-mesa-dri).";
    }
} // namespace sextant::platform
