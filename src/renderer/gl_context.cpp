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

// Serialises the process-wide global state that window setup and teardown
// touch: GLFW's window list and GLAD's function-pointer table, neither with
// any internal locking. Each Figure::show() runs its own window thread, so
// with more than one Figure open a second window coming up or going down
// could leave a GL entry point momentarily NULL and crash a sibling thread
// mid-frame. Contention-free in the steady state -- held only while a window
// is being created or destroyed, never during the frame loop.
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

void framebuffer_size_callback(GLFWwindow* window, int w, int h) {
    auto* ctx = static_cast<GLContext*>(glfwGetWindowUserPointer(window));
    ctx->on_resize(w, h);
}

} // namespace

GLContext::GLContext(GLContextOptions opts)
    : width_(opts.width), height_(opts.height)
{
    ensure_glfw_init();

    // Covers the window hints too — they are set on a global hint struct that
    // the next glfwCreateWindow consumes, so an interleaved second thread
    // could otherwise create its window from this one's half-written hints.
    std::lock_guard<std::mutex> lock(global_gl_mutex());

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE,   opts.visible   ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, opts.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);  // required by NanoVG

    window_ = glfwCreateWindow(opts.width, opts.height,
                               opts.title.c_str(), nullptr, nullptr);
    if (!window_)
        throw std::runtime_error("glfwCreateWindow failed");

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(opts.vsync ? 1 : 0);

    // Load exactly once, not once per window. GLAD keeps its entry points in
    // one process-wide table, so a second window re-running the loader would
    // rewrite that table underneath sibling threads already rendering through
    // it. Sound because every GLContext requests the same GL 4.1 core profile
    // from the same driver. Runs under global_gl_mutex(), held by the caller,
    // with this window's context current.
    static bool glad_loaded = false;
    if (!glad_loaded) {
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
            throw std::runtime_error("gladLoadGLLoader failed");
        glad_loaded = true;
    }

    nvg_ = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!nvg_)
        throw std::runtime_error("nvgCreateGL3 failed");

    glfwGetFramebufferSize(window_, &width_, &height_);
}

GLContext::~GLContext() {
    std::lock_guard<std::mutex> lock(global_gl_mutex());
    if (nvg_)    { nvgDeleteGL3(nvg_);         nvg_    = nullptr; }
    if (window_) { glfwDestroyWindow(window_); window_ = nullptr; }
}

bool GLContext::should_close() const {
    return glfwWindowShouldClose(window_);
}

void GLContext::poll_events() {
    glfwPollEvents();
}

void GLContext::swap_buffers() {
    glfwSwapBuffers(window_);
}

void GLContext::make_current() {
    glfwMakeContextCurrent(window_);
}

void GLContext::begin_nvg_frame(int w, int h, float pixel_ratio) const {
    const float fw = w > 0 ? static_cast<float>(w) : static_cast<float>(width_);
    const float fh = h > 0 ? static_cast<float>(h) : static_cast<float>(height_);
    nvgBeginFrame(nvg_, fw, fh, pixel_ratio > 0.0f ? pixel_ratio : 1.0f);
}

void GLContext::end_nvg_frame() const {
    nvgEndFrame(nvg_);
}

void GLContext::on_resize(int w, int h) {
    width_  = w;
    height_ = h;
}

} // namespace sextant
