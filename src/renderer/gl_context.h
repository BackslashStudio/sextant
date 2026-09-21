#pragma once
#include "../platform/platform.h"
#include "../window_link.h"
#include <memory>
#include <string>

struct GLFWwindow;
struct NVGcontext;

namespace sextant {
    struct GLContextOptions {
        int width = 800;
        int height = 600;
        std::string title = "sextant";
        bool visible = true; // false for headless savefig
        bool resizable = true;

        // glfwSwapInterval: cap the loop at the display refresh rate.
        bool vsync = true;

        // GLFW_SCALE_TO_MONITOR: width/height become a physical (DPI-scaled) size.
        // Off by default so headless exports render exact pixels; only
        // WindowThread enables it.
        bool scale_to_monitor = false;

        // No window at all, where the platform can give a GL context without one
        // (platform::has_offscreen_gl). What an export asks for: nothing here
        // draws to a window, and on macOS a window would drag the main thread in.
        // Where the platform cannot, this falls back to a hidden window, which is
        // what `visible = false` has always been.
        bool headless = false;
    };

    // The render half of one window: its GL context, GLAD's function pointers
    // and a NanoVG context, all made and used on the thread that will draw.
    // The window itself comes from the broker (window_broker.h), which may be a
    // different thread entirely -- on macOS it has to be.
    //
    // A headless context has no window and no link: only nvg(), make_current(),
    // the size and the NanoVG frame wrappers mean anything on one, which is
    // exactly what figure_export.cpp uses.
    class GLContext {
    public:
        explicit GLContext(GLContextOptions opts);

        ~GLContext();

        // Non-copyable, non-movable (GLFW/NVG context is not portable)
        GLContext(const GLContext&) = delete;

        GLContext& operator=(const GLContext&) = delete;

        GLFWwindow* window() const { return window_; }
        NVGcontext* nvg() const { return nvg_; }

        // True when this context has no window behind it.
        bool is_headless() const { return offscreen_ != nullptr; }

        // This window's state mirror and its two queues: the pumping thread fills
        // them in poll_events(), the render thread reads them. Window-backed
        // contexts only.
        WindowLink& link() { return *link_; }
        const WindowLink& link() const { return *link_; }

        // True with no window to keep open.
        bool should_close() const;

        // Pumps this window: GLFW events (whose callbacks fill the link's queues),
        // then the state mirror, then what the render thread asked for. Does
        // nothing where one thread pumps every window -- there Figure's pump
        // walks this link instead.
        void poll_events();

        void swap_buffers();

        void make_current();

        // The framebuffer, in pixels, as the last poll saw it -- the size it was
        // asked for on a headless context, which has nothing to resize it.
        int width() const { return link_ ? link_->framebuffer_width() : width_; }
        int height() const { return link_ ? link_->framebuffer_height() : height_; }

        // NanoVG frame wrappers. w/h (logical pixels) override this context's size
        // for an offscreen target (<= 0: own size). pixel_ratio is the device-pixel
        // ratio (the supersample factor), which keeps NanoVG's AA and text sharp.
        void begin_nvg_frame(int w = 0, int h = 0, float pixel_ratio = 1.0f) const;

        void end_nvg_frame() const;

    private:
        // Gives the window back, or unmakes the offscreen context: what the
        // destructor does, and what a failed construction undoes.
        void release_target();

        GLFWwindow* window_ = nullptr;
        platform::OffscreenGL* offscreen_ = nullptr;
        NVGcontext* nvg_ = nullptr;
        int width_ = 0;  // headless only; a window's size comes from its link
        int height_ = 0;
        // Shared with the broker, which keeps it alive until the window is
        // really gone: a window's callbacks reach its link through the user
        // pointer, and a destroy can be served after this object is long dead.
        std::shared_ptr<WindowLink> link_;
    };
} // namespace sextant
