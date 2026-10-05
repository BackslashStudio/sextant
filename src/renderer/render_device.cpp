#include "render_device.h"
#include <glad/glad.h>
// NANOVG_GL3_IMPLEMENTATION must be defined in exactly one TU — here.
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
#include <mutex>
#include <stdexcept>

namespace sextant {
    bool load_gl(GLLoadProc loader) {
        static std::mutex m;
        std::lock_guard<std::mutex> lock(m);
        // Loaded already, by the library or by a host: keep that table.
        if (glad_glGetString) return true;
        return gladLoadGLLoader(reinterpret_cast<GLADloadproc>(loader)) != 0;
    }

    RenderDevice::RenderDevice() {
        if (!glad_glGetString)
            throw std::runtime_error("RenderDevice: GL functions are not loaded "
                                     "(call load_gl() with the context current)");
        if (!GLAD_GL_VERSION_4_1)
            throw std::runtime_error("RenderDevice: needs OpenGL 4.1 or later");
        vg_ = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        if (!vg_) throw std::runtime_error("nvgCreateGL3 failed");
        renderer_ = std::make_unique<NvgRenderer>(vg_);
    }

    RenderDevice::~RenderDevice() {
        renderer_.reset();
        if (vg_) nvgDeleteGL3(vg_);
    }

    void RenderDevice::begin_nvg_frame(int w, int h, float pixel_ratio) const {
        nvgBeginFrame(vg_, static_cast<float>(w), static_cast<float>(h),
                      pixel_ratio > 0.0f ? pixel_ratio : 1.0f);
    }

    void RenderDevice::end_nvg_frame() const {
        nvgEndFrame(vg_);
    }
} // namespace sextant
