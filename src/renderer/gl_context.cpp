#include "gl_context.h"
#include "../window_broker.h"
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "render_device.h"
#include "../platform/platform.h"
#include "../messages.h"
#include <atomic>
#include <cstdio>
#include <string>
#include <stdexcept>

namespace sextant {
    namespace {
        // Once per process. Not std::call_once: that would hold every other
        // thread wanting this message until the handler returns.
        void warn_no_offscreen(const std::string& why) {
            static std::atomic<bool> said{false};
            if (said.exchange(true)) return;
            emit_message("no windowless GL context (" + why
                         + "). Exports fall back to a hidden window"
                         + (platform::windows_on_main_thread
                                ? ", which on this platform is the main thread's to make."
                                : ", which needs a display."));
        }
    } // namespace

    GLContext::GLContext(GLContextOptions opts) {
        std::string offscreen_failure;
        if (opts.headless && platform::has_offscreen_gl && platform::offscreen_gl_enabled()) {
            // No window, no window system, no main thread: a context of the
            // platform's own, current on this thread, whose only target is the
            // export's FBO. What lets savefig() be called from anywhere, and on
            // Linux with no display at all.
            try {
                offscreen_ = platform::create_offscreen_gl();
                width_ = opts.width;
                height_ = opts.height;
            } catch (const std::exception& e) {
                // A hidden window is what this path used before there was an
                // offscreen one, and where a window can be made it still works
                // -- so a machine that cannot give one is told about it and
                // served anyway, rather than losing an export it would have got.
                offscreen_failure = e.what();
                warn_no_offscreen(offscreen_failure);
            }
        }

        if (!offscreen_) {
            // The window, its callbacks and its link: the broker's, on whichever
            // thread this platform lets own one.
            BrokeredWindow bw;
            try {
                bw = create_window({
                    .width = opts.width, .height = opts.height, .title = opts.title,
                    .visible = opts.visible, .resizable = opts.resizable,
                    .scale_to_monitor = opts.scale_to_monitor
                });
            } catch (const std::exception& e) {
                if (offscreen_failure.empty()) throw;
                // Both ways failed: say both, and what would fix the first.
                std::string msg = "no GL context for the export: no windowless one ("
                                  + offscreen_failure + "), and no window (" + e.what() + ").";
                if (const char* hint = platform::offscreen_gl_hint(); hint && *hint)
                    msg += std::string(" ") + hint;
                throw std::runtime_error(msg);
            }
            window_ = bw.window;
            link_ = std::move(bw.link);

            // Everything from here down is any-thread in GLFW, so it stays on the
            // thread that will render: this context is the one it makes current.
            glfwMakeContextCurrent(window_);
            glfwSwapInterval(opts.vsync ? 1 : 0);
        }

        // GLAD's table is process-wide and loaded once (load_gl()). On the one
        // platform that has both loaders they answer with the same entry points
        // -- GLFW's own goes to the same framework the offscreen path dlsym()s
        // -- so whichever context comes first is the one that loads it.
        if (!load_gl(offscreen_
                         ? reinterpret_cast<GLLoadProc>(&platform::offscreen_gl_proc_address)
                         : reinterpret_cast<GLLoadProc>(glfwGetProcAddress))) {
            release_target();
            throw std::runtime_error("gladLoadGLLoader failed");
        }
    }

    GLContext::~GLContext() {
        release_target();
        // Usually the last reference: the link's cursors are freed with it, and
        // those are GLFW calls.
        auto lock = glfw_state_lock();
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

        // Other windows' threads poll too: one at a time (glfw_state_lock()).
        auto lock = glfw_state_lock();
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
} // namespace sextant
