#include "gl_context.h"
#include "../window_broker.h"
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
// NANOVG_GL3_IMPLEMENTATION must be defined in exactly one TU — here.
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
#include "../platform/platform.h"
#include <cstdio>
#include <mutex>
#include <stdexcept>

namespace sextant {
    namespace {
        // GLAD's table is process-wide, so it is loaded once and never again:
        // reloading would rewrite it under other rendering threads. On the one
        // platform that has both loaders they answer with the same entry points
        // -- GLFW's own goes to the same framework the offscreen path dlsym()s
        // -- so whichever context comes first is the one that loads it.
        bool ensure_glad(GLADloadproc loader) {
            static std::once_flag once;
            static bool ok = false;
            std::call_once(once, [loader] { ok = gladLoadGLLoader(loader) != 0; });
            return ok;
        }

        void warn_no_offscreen(const char* why) {
            static std::once_flag once;
            std::call_once(once, [why] {
                std::fprintf(stderr,
                             "sextant: no windowless GL context (%s). Exports fall back to a "
                             "hidden window, which on this platform is the main thread's to "
                             "make.\n", why);
            });
        }
    } // namespace

    GLContext::GLContext(GLContextOptions opts) {
        if (opts.headless && platform::has_offscreen_gl) {
            // No window, no window system, no main thread: a context of the
            // platform's own, current on this thread, whose only target is the
            // export's FBO. What lets savefig() be called from anywhere.
            try {
                offscreen_ = platform::create_offscreen_gl();
                width_ = opts.width;
                height_ = opts.height;
            } catch (const std::exception& e) {
                // A hidden window is what this path used before there was an
                // offscreen one, and on the main thread it still works -- so a
                // machine that cannot give one is told about it and served
                // anyway, rather than losing an export it would have got.
                warn_no_offscreen(e.what());
            }
        }

        if (!offscreen_) {
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
        }

        if (!ensure_glad(offscreen_
                             ? reinterpret_cast<GLADloadproc>(&platform::offscreen_gl_proc_address)
                             : reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            release_target();
            throw std::runtime_error("gladLoadGLLoader failed");
        }

        nvg_ = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        if (!nvg_) {
            release_target();
            throw std::runtime_error("nvgCreateGL3 failed");
        }
    }

    GLContext::~GLContext() {
        if (nvg_) {
            nvgDeleteGL3(nvg_);
            nvg_ = nullptr;
        }
        release_target();
        link_.reset();
    }

    void GLContext::release_target() {
        if (offscreen_) {
            platform::destroy_offscreen_gl(offscreen_);
            offscreen_ = nullptr;
            return;
        }
        if (window_) {
            // The context goes before the window does, and the window goes back
            // to the broker rather than being destroyed here -- elsewhere that
            // is the same thing, on macOS it is a request to the main thread.
            glfwMakeContextCurrent(nullptr);
            destroy_window(window_);
            window_ = nullptr;
        }
    }

    bool GLContext::should_close() const {
        return !window_ || glfwWindowShouldClose(window_);
    }

    void GLContext::poll_events() {
        if (!window_) return;  // headless: no events, no link

        // Where one thread owns every window, that thread pumps this link too
        // (window_broker.cpp); doing it here would be the main-thread violation
        // the whole split exists to avoid.
        if constexpr (platform::windows_on_main_thread) return;

        glfwPollEvents();        // the link's callbacks run in here
        link_->sync_state();
        link_->service_requests();
    }

    void GLContext::swap_buffers() {
        if (window_) glfwSwapBuffers(window_);
    }

    void GLContext::make_current() {
        if (offscreen_) platform::make_offscreen_gl_current(offscreen_);
        else glfwMakeContextCurrent(window_);
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
