// See gl_poison.h. Part of sextant_layout_test.
#include "gl_poison.h"

#include <glad/glad.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <random>
#include <vector>

namespace lt {
    namespace {
        std::mutex g_rng_mutex;   // sextant allocates from window threads too
        std::mt19937 g_rng;
        PFNGLTEXIMAGE2DPROC real_tex_image_2d = nullptr;
        PFNGLRENDERBUFFERSTORAGEPROC real_renderbuffer_storage = nullptr;
        PFNGLBUFFERDATAPROC real_buffer_data = nullptr;
        std::atomic<int> g_textures{0}, g_renderbuffers{0}, g_buffers{0};

        std::vector<unsigned char> noise(std::size_t n) {
            std::lock_guard lock(g_rng_mutex);
            std::vector<unsigned char> v(n);
            for (auto& b: v) b = static_cast<unsigned char>(g_rng());
            return v;
        }

        float unit() {
            std::lock_guard lock(g_rng_mutex);
            return std::uniform_real_distribution<float>(0.0f, 1.0f)(g_rng);
        }

        std::size_t bytes_per_pixel(GLenum format, GLenum type) {
            if (type == GL_UNSIGNED_INT_24_8 || type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV)
                return type == GL_UNSIGNED_INT_24_8 ? 4 : 8;
            std::size_t comps = 4;
            switch (format) {
                case GL_RED: case GL_RED_INTEGER: case GL_DEPTH_COMPONENT: case GL_STENCIL_INDEX:
                    comps = 1; break;
                case GL_RG: case GL_RG_INTEGER:
                    comps = 2; break;
                case GL_RGB: case GL_BGR: case GL_RGB_INTEGER:
                    comps = 3; break;
                default: break;
            }
            std::size_t size = 4;
            switch (type) {
                case GL_UNSIGNED_BYTE: case GL_BYTE: size = 1; break;
                case GL_UNSIGNED_SHORT: case GL_SHORT: case GL_HALF_FLOAT: size = 2; break;
                default: break;
            }
            return comps * size;
        }

        void APIENTRY poisoned_tex_image_2d(GLenum target, GLint level, GLint internal_format,
                                            GLsizei w, GLsizei h, GLint border, GLenum format,
                                            GLenum type, const void* data) {
            GLint unpack_buffer = 0;
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
            if (data || unpack_buffer || w <= 0 || h <= 0) {
                real_tex_image_2d(target, level, internal_format, w, h, border, format, type, data);
                return;
            }
            // Sized for whatever unpack state is current. Random bytes in a float
            // texture include NaNs; reading one is exactly what this looks for.
            GLint align = 4, row_length = 0, skip_rows = 0, skip_pixels = 0;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
            const std::size_t bpp = bytes_per_pixel(format, type);
            const std::size_t row_px = static_cast<std::size_t>(row_length > 0 ? row_length : w);
            const std::size_t a = static_cast<std::size_t>(align > 0 ? align : 1);
            const std::size_t stride = (row_px * bpp + a - 1) / a * a;
            const std::vector<unsigned char> buf =
                    noise(stride * static_cast<std::size_t>(h + skip_rows) +
                          static_cast<std::size_t>(skip_pixels + 1) * bpp + a);
            real_tex_image_2d(target, level, internal_format, w, h, border, format, type, buf.data());
            ++g_textures;
        }

        // Renderbuffers take no data, so the new storage is cleared to random
        // values through a scratch framebuffer, with every touched state restored.
        void APIENTRY poisoned_renderbuffer_storage(GLenum target, GLenum internal_format,
                                                    GLsizei w, GLsizei h) {
            real_renderbuffer_storage(target, internal_format, w, h);
            if (w <= 0 || h <= 0) return;

            const bool depth = internal_format == GL_DEPTH24_STENCIL8 ||
                               internal_format == GL_DEPTH32F_STENCIL8 ||
                               internal_format == GL_DEPTH_COMPONENT16 ||
                               internal_format == GL_DEPTH_COMPONENT24 ||
                               internal_format == GL_DEPTH_COMPONENT32F ||
                               internal_format == GL_DEPTH_COMPONENT;
            const bool stencil = internal_format == GL_DEPTH24_STENCIL8 ||
                                 internal_format == GL_DEPTH32F_STENCIL8;

            GLint rb = 0, draw_fbo = 0, read_fbo = 0, stencil_clear = 0;
            GLint stencil_mask = 0;
            GLfloat color_clear[4] = {}, depth_clear = 1.0f;
            GLboolean color_mask[4] = {}, depth_mask = GL_TRUE;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &rb);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
            glGetFloatv(GL_COLOR_CLEAR_VALUE, color_clear);
            glGetFloatv(GL_DEPTH_CLEAR_VALUE, &depth_clear);
            glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &stencil_clear);
            glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
            glGetIntegerv(GL_STENCIL_WRITEMASK, &stencil_mask);
            const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);

            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            if (depth) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                          stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                                          GL_RENDERBUFFER, static_cast<GLuint>(rb));
                glDrawBuffer(GL_NONE);
                glReadBuffer(GL_NONE);
            } else {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                          static_cast<GLuint>(rb));
            }
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glDisable(GL_SCISSOR_TEST);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_TRUE);
                glStencilMask(0xFF);
                glClearColor(unit(), unit(), unit(), unit());
                glClearDepth(unit());
                glClearStencil(static_cast<GLint>(unit() * 255.0f));
                glClear(depth ? (GL_DEPTH_BUFFER_BIT | (stencil ? GL_STENCIL_BUFFER_BIT : 0))
                              : GL_COLOR_BUFFER_BIT);
                ++g_renderbuffers;
            }

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_fbo));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_fbo));
            glDeleteFramebuffers(1, &fbo);
            if (scissor) glEnable(GL_SCISSOR_TEST);
            glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
            glDepthMask(depth_mask);
            glStencilMask(static_cast<GLuint>(stencil_mask));
            glClearColor(color_clear[0], color_clear[1], color_clear[2], color_clear[3]);
            glClearDepth(depth_clear);
            glClearStencil(stencil_clear);
        }

        void APIENTRY poisoned_buffer_data(GLenum target, GLsizeiptr size, const void* data,
                                           GLenum usage) {
            if (data || size <= 0) {
                real_buffer_data(target, size, data, usage);
                return;
            }
            const std::vector<unsigned char> buf = noise(static_cast<std::size_t>(size));
            real_buffer_data(target, size, buf.data(), usage);
            ++g_buffers;
        }
    } // namespace

    void install_gl_poison(unsigned seed) {
        g_rng.seed(seed);
        if (!real_tex_image_2d) {
            real_tex_image_2d = glad_glTexImage2D;
            real_renderbuffer_storage = glad_glRenderbufferStorage;
            real_buffer_data = glad_glBufferData;
        }
        glad_glTexImage2D = poisoned_tex_image_2d;
        glad_glRenderbufferStorage = poisoned_renderbuffer_storage;
        glad_glBufferData = poisoned_buffer_data;
    }

    PoisonCounts gl_poison_counts() { return {g_textures, g_renderbuffers, g_buffers}; }
} // namespace lt
