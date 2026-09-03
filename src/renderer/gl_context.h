#pragma once
#include <string>

struct GLFWwindow;
struct NVGcontext;

namespace sextant {

struct GLContextOptions {
    int         width     = 800;
    int         height    = 600;
    std::string title     = "sextant";
    bool        visible   = true;  // false for headless savefig
    bool        resizable = true;

    // glfwSwapInterval: true caps the render loop at the display refresh
    // rate. Turning it off lets the loop run as fast as it can, which is
    // what makes end-to-end frame cost measurable — with vsync on, the GPU
    // half of a frame hides inside the swap wait (see FrameStats).
    bool        vsync     = true;
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

    GLFWwindow*  window()  const { return window_; }
    NVGcontext*  nvg()     const { return nvg_; }
    bool         should_close() const;
    void         poll_events();
    void         swap_buffers();
    void         make_current();

    int width()  const { return width_; }
    int height() const { return height_; }

    // NanoVG frame wrappers, keeping nanovg.h out of callers' include paths.
    // w/h override this GLContext's own tracked size when rendering into an
    // offscreen target of a different size; omit (<=0) to use its own.
    //
    // pixel_ratio is NanoVG's device-pixel ratio: w/h stay in *logical* pixels
    // and describe the coordinate space drawing commands use, while the actual
    // framebuffer is pixel_ratio times larger in each axis (the caller's
    // glViewport must already say so). NanoVG derives its AA fringe width and
    // glyph rasterization size from it, so passing the supersample factor here
    // is what keeps its antialiasing and text sharp in a supersampled target.
    void begin_nvg_frame(int w = 0, int h = 0, float pixel_ratio = 1.0f) const;
    void end_nvg_frame()   const;

    // Called by the framebuffer resize callback — do not call directly.
    void on_resize(int w, int h);

private:
    GLFWwindow* window_ = nullptr;
    NVGcontext* nvg_     = nullptr;
    int         width_  = 0;
    int         height_ = 0;
};

} // namespace sextant
