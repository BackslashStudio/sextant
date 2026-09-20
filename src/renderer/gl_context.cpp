#include "gl_context.h"
#include "../window_broker.h"
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// NANOVG_GL3_IMPLEMENTATION must be defined in exactly one TU — here.
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
#include "../platform/platform.h"
#include <mutex>
#include <stdexcept>

namespace sextant {
    GLContext::GLContext(GLContextOptions opts) {
        // The window, its callbacks and its link: the broker's, on whichever
        // thread this platform lets own one.
        BrokeredWindow bw = create_window({
            .width = opts.width, .height = opts.height, .title = opts.title,
            .visible = opts.visible, .resizable = opts.resizable,
            .scale_to_monitor = opts.scale_to_monitor
        });
        window_ = bw.window;
        link_ = std::move(bw.link);

        // Everything from here down is any-thread in GLFW, so it stays on the
        // thread that will render: this context is the one it makes current.
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(opts.vsync ? 1 : 0);

        // Load GLAD once per process: its table is shared, and reloading would
        // rewrite it under other rendering threads.
        static std::once_flag glad_once;
        static bool glad_ok = false;
        std::call_once(glad_once, [] {
            glad_ok = gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0;
        });
        if (!glad_ok) {
            glfwMakeContextCurrent(nullptr);
            destroy_window(window_);
            window_ = nullptr;
            throw std::runtime_error("gladLoadGLLoader failed");
        }

        nvg_ = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        if (!nvg_) {
            glfwMakeContextCurrent(nullptr);
            destroy_window(window_);
            window_ = nullptr;
            throw std::runtime_error("nvgCreateGL3 failed");
        }
    }

    GLContext::~GLContext() {
        if (nvg_) {
            nvgDeleteGL3(nvg_);
            nvg_ = nullptr;
        }
        if (window_) {
            // The context goes before the window does, and the window goes back
            // to the broker rather than being destroyed here -- elsewhere that
            // is the same thing, on macOS it is a request to the main thread.
            glfwMakeContextCurrent(nullptr);
            destroy_window(window_);
            window_ = nullptr;
        }
        link_.reset();
    }

    bool GLContext::should_close() const {
        return glfwWindowShouldClose(window_);
    }

    void GLContext::poll_events() {
        // Where one thread owns every window, that thread pumps this link too
        // (window_broker.cpp); doing it here would be the main-thread violation
        // the whole split exists to avoid.
        if constexpr (platform::windows_on_main_thread) return;

        glfwPollEvents();        // the link's callbacks run in here
        link_->sync_state();
        link_->service_requests();
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
