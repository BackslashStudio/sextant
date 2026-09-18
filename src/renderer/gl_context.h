#pragma once
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
    };

    // Owns a single GLFWwindow, GLAD function pointers, and NanoVG context.
    // Must be created and used on the same thread (GLFW requirement).
    class GLContext {
    public:
        explicit GLContext(GLContextOptions opts);

        ~GLContext();

        // Non-copyable, non-movable (GLFW/NVG context is not portable)
        GLContext(const GLContext&) = delete;

        GLContext& operator=(const GLContext&) = delete;

        GLFWwindow* window() const { return window_; }
        NVGcontext* nvg() const { return nvg_; }

        bool should_close() const;

        void poll_events();

        void swap_buffers();

        void make_current();

        int width() const { return width_; }
        int height() const { return height_; }

        // NanoVG frame wrappers. w/h (logical pixels) override this context's size
        // for an offscreen target (<= 0: own size). pixel_ratio is the device-pixel
        // ratio (the supersample factor), which keeps NanoVG's AA and text sharp.
        void begin_nvg_frame(int w = 0, int h = 0, float pixel_ratio = 1.0f) const;

        void end_nvg_frame() const;

        // Called by the framebuffer resize callback — do not call directly.
        void on_resize(int w, int h);

    private:
        GLFWwindow* window_ = nullptr;
        NVGcontext* nvg_ = nullptr;
        int width_ = 0;
        int height_ = 0;
    };
} // namespace sextant
