#include "gl_context.h"
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// NANOVG_GL3_IMPLEMENTATION must be defined in exactly one TU — here.
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
#include <mutex>
#include <stdexcept>

namespace sextant {
    namespace {
        // Serialises window creation/destruction, which touch unlocked process-wide
        // state (GLFW's window list, GLAD's function table). Not held during frames.
        std::mutex& global_gl_mutex() {
            static std::mutex m;
            return m;
        }

        void ensure_glfw_init() {
            static std::once_flag s_init;
            std::call_once(s_init, [] {
#if defined(__linux__)
                glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
                if (!glfwInit())
                    throw std::runtime_error("glfwInit failed");
            });
        }

    } // namespace

    GLContext::GLContext(GLContextOptions opts) {
        ensure_glfw_init();

        // Also covers the window hints, which are global state.
        std::lock_guard<std::mutex> lock(global_gl_mutex());

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, opts.visible ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, opts.resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_STENCIL_BITS, 8); // required by NanoVG
        // Always set: hints are sticky, so a live window's setting would otherwise
        // scale the next headless export.
        glfwWindowHint(GLFW_SCALE_TO_MONITOR,
                       opts.scale_to_monitor ? GLFW_TRUE : GLFW_FALSE);

        window_ = glfwCreateWindow(opts.width, opts.height,
                                   opts.title.c_str(), nullptr, nullptr);
        if (!window_)
            throw std::runtime_error("glfwCreateWindow failed");

        // The link owns this window's callbacks and user pointer, and seeds its
        // mirror from the window as created.
        link_.attach(window_);

        glfwMakeContextCurrent(window_);
        glfwSwapInterval(opts.vsync ? 1 : 0);

        // Load GLAD once per process: its table is shared, and reloading would
        // rewrite it under other rendering threads. Caller holds global_gl_mutex().
        static bool glad_loaded = false;
        if (!glad_loaded) {
            if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
                throw std::runtime_error("gladLoadGLLoader failed");
            glad_loaded = true;
        }

        nvg_ = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        if (!nvg_)
            throw std::runtime_error("nvgCreateGL3 failed");
    }

    GLContext::~GLContext() {
        std::lock_guard<std::mutex> lock(global_gl_mutex());
        if (nvg_) {
            nvgDeleteGL3(nvg_);
            nvg_ = nullptr;
        }
        if (window_) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
    }

    bool GLContext::should_close() const {
        return glfwWindowShouldClose(window_);
    }

    void GLContext::poll_events() {
        glfwPollEvents();        // the link's callbacks run in here
        link_.sync_state();
        link_.service_requests();
    }

    void GLContext::swap_buffers() {
        glfwSwapBuffers(window_);
    }

    void GLContext::make_current() {
        glfwMakeContextCurrent(window_);
    }

    void GLContext::begin_nvg_frame(int w, int h, float pixel_ratio) const {
        const float fw = w > 0 ? static_cast<float>(w) : static_cast<float>(width());
        const float fh = h > 0 ? static_cast<float>(h) : static_cast<float>(height());
        nvgBeginFrame(nvg_, fw, fh, pixel_ratio > 0.0f ? pixel_ratio : 1.0f);
    }

    void GLContext::end_nvg_frame() const {
        nvgEndFrame(nvg_);
    }
} // namespace sextant
