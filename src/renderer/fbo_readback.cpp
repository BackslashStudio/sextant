#include "fbo_readback.h"
#include <sextant/figure.h>   // kMaxSupersample
#include <glad/glad.h>
#include <stdexcept>
#include <algorithm>

namespace sextant {

FboReadback::FboReadback(int width, int height, int supersample)
    : width_(width), height_(height)
    , supersample_(std::clamp(supersample, 1, kMaxSupersample))
{
    const int rw = render_width(), rh = render_height();

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenRenderbuffers(1, &color_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, color_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, rw, rh);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, color_rb_);

    glGenRenderbuffers(1, &depth_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, rw, rh);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, depth_rb_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("FboReadback: framebuffer incomplete");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

FboReadback::~FboReadback() {
    if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
    if (color_rb_) glDeleteRenderbuffers(1, &color_rb_);
    if (fbo_)      glDeleteFramebuffers(1, &fbo_);
}

void FboReadback::bind() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
}

void FboReadback::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::vector<uint8_t> FboReadback::read_pixels() const {
    const int rw = render_width(), rh = render_height();
    const std::size_t src_row_bytes = static_cast<std::size_t>(rw) * 4;
    std::vector<uint8_t> src(src_row_bytes * static_cast<std::size_t>(rh));

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, src.data());

    // Box-filter the supersampled readback down to the final image size —
    // the CPU counterpart of PlotFbo::resolve()'s fragment shader, and
    // deliberately the same filter so a savefig() PNG matches what the
    // window shows. Done on the CPU here because savefig is a one-shot
    // operation whose result has to reach host memory regardless, so a GPU
    // pass would only add a second render target for no saving.
    std::vector<uint8_t> buf(static_cast<std::size_t>(width_) * 4
                             * static_cast<std::size_t>(height_));
    if (supersample_ == 1) {
        buf.swap(src);
    } else {
        const int ss = supersample_;
        const int n  = ss * ss;
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                int acc[4] = { 0, 0, 0, 0 };
                for (int sy = 0; sy < ss; ++sy) {
                    const uint8_t* row = &src[(static_cast<std::size_t>(y) * ss + sy) * src_row_bytes
                                              + static_cast<std::size_t>(x) * ss * 4];
                    for (int sx = 0; sx < ss; ++sx)
                        for (int c = 0; c < 4; ++c)
                            acc[c] += row[sx * 4 + c];
                }
                uint8_t* dst = &buf[(static_cast<std::size_t>(y) * width_ + x) * 4];
                for (int c = 0; c < 4; ++c)
                    dst[c] = static_cast<uint8_t>((acc[c] + n / 2) / n);  // round, don't truncate
            }
        }
    }

    // OpenGL origin is bottom-left; flip rows to get top-down image.
    const std::size_t row_bytes = static_cast<std::size_t>(width_) * 4;
    for (int top = 0, bot = height_ - 1; top < bot; ++top, --bot)
        std::swap_ranges(buf.begin() + top * static_cast<int>(row_bytes),
                         buf.begin() + top * static_cast<int>(row_bytes) + static_cast<int>(row_bytes),
                         buf.begin() + bot * static_cast<int>(row_bytes));
    return buf;
}

} // namespace sextant
