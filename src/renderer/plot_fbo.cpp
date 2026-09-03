#include "plot_fbo.h"
#include <sextant/figure.h>   // kMaxSupersample
#include <glad/glad.h>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace sextant {

namespace {

// Fullscreen triangle generated from gl_VertexID — no vertex buffer, but
// core profile still requires *some* VAO to be bound for a draw call.
constexpr char k_resolve_vert[] = R"(
#version 410 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Box filter: each destination pixel averages the uSamples x uSamples block
// of source texels it was rendered from. texelFetch (not texture()) so the
// mapping is exact and independent of filter/wrap state — with an integer
// scale factor a box average is the correct downsample, and going through
// the bilinear sampler instead would silently skip source texels for any
// factor above 2.
constexpr char k_resolve_frag[] = R"(
#version 410 core
uniform sampler2D uSrc;
uniform int uSamples;
out vec4 FragColor;
void main() {
    ivec2 base = ivec2(gl_FragCoord.xy) * uSamples;
    vec4 sum = vec4(0.0);
    for (int y = 0; y < uSamples; ++y)
        for (int x = 0; x < uSamples; ++x)
            sum += texelFetch(uSrc, base + ivec2(x, y), 0);
    FragColor = sum / float(uSamples * uSamples);
}
)";

unsigned int compile(GLenum type, const char* src) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("PlotFbo resolve shader compile error: ") + log);
    }
    return s;
}

} // namespace

PlotFbo::~PlotFbo() {
    destroy();
    if (resolve_vao_)     { glDeleteVertexArrays(1, &resolve_vao_); resolve_vao_ = 0; }
    if (resolve_program_) { glDeleteProgram(resolve_program_);      resolve_program_ = 0; }
}

void PlotFbo::destroy() {
    if (depth_rb_)     { glDeleteRenderbuffers(1, &depth_rb_);   depth_rb_ = 0; }
    if (color_tex_)    { glDeleteTextures(1, &color_tex_);       color_tex_ = 0; }
    if (fbo_)          { glDeleteFramebuffers(1, &fbo_);         fbo_ = 0; }
    if (resolved_tex_) { glDeleteTextures(1, &resolved_tex_);    resolved_tex_ = 0; }
    if (resolved_fbo_) { glDeleteFramebuffers(1, &resolved_fbo_); resolved_fbo_ = 0; }
    width_ = height_ = 0;
    supersample_ = 1;
}

void PlotFbo::ensure_size(int width, int height, int supersample) {
    width  = std::max(1, width);
    height = std::max(1, height);
    const int ss = std::clamp(supersample, 1, kMaxSupersample);
    if (width == width_ && height == height_ && ss == supersample_ && fbo_ != 0) return;
    destroy();

    width_       = width;
    height_      = height;
    supersample_ = ss;

    const int rw = render_width(), rh = render_height();

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &color_tex_);
    glBindTexture(GL_TEXTURE_2D, color_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_tex_, 0);

    // Combined depth+stencil attachment — NanoVG needs the 8-bit stencil
    // for its stencil-then-cover fill technique (same as FboReadback and
    // the live GLFW window's own GLFW_STENCIL_BITS hint). Never sampled,
    // so a renderbuffer (not a texture) is the right/cheaper choice here.
    glGenRenderbuffers(1, &depth_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, rw, rh);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, depth_rb_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("PlotFbo: framebuffer incomplete");

    // Display-size resolve target, only when there is something to resolve.
    if (supersample_ > 1) {
        glGenFramebuffers(1, &resolved_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, resolved_fbo_);

        glGenTextures(1, &resolved_tex_);
        glBindTexture(GL_TEXTURE_2D, resolved_tex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, resolved_tex_, 0);
        // No depth/stencil: the resolve pass is one unblended fullscreen
        // triangle with no tests enabled.
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("PlotFbo: resolve framebuffer incomplete");
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PlotFbo::ensure_resolve_program() {
    if (resolve_program_) return;
    unsigned int v = compile(GL_VERTEX_SHADER,   k_resolve_vert);
    unsigned int f = compile(GL_FRAGMENT_SHADER, k_resolve_frag);
    resolve_program_ = glCreateProgram();
    glAttachShader(resolve_program_, v);
    glAttachShader(resolve_program_, f);
    glLinkProgram(resolve_program_);
    int ok = 0;
    glGetProgramiv(resolve_program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(resolve_program_, sizeof(log), nullptr, log);
        glDeleteProgram(resolve_program_);
        resolve_program_ = 0;
        glDeleteShader(v);
        glDeleteShader(f);
        throw std::runtime_error(std::string("PlotFbo resolve program link error: ") + log);
    }
    glDeleteShader(v);
    glDeleteShader(f);

    resolve_loc_samples_ = glGetUniformLocation(resolve_program_, "uSamples");
    glUseProgram(resolve_program_);
    glUniform1i(glGetUniformLocation(resolve_program_, "uSrc"), 0);
    glUseProgram(0);

    glGenVertexArrays(1, &resolve_vao_);
}

void PlotFbo::resolve() {
    if (supersample_ <= 1 || resolved_fbo_ == 0) {
        // Nothing to filter — color_texture() already hands out the target
        // that was rendered into.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    ensure_resolve_program();

    glBindFramebuffer(GL_FRAMEBUFFER, resolved_fbo_);
    glViewport(0, 0, width_, height_);

    // The source already carries final composited colors; blending or
    // clipping them again here would corrupt the average. The caller is
    // mid-ImGui-frame, whose backend re-establishes its own full draw state
    // at ImGui_ImplOpenGL3_RenderDrawData() time, so leaving these off does
    // not disturb it.
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);

    glUseProgram(resolve_program_);
    glUniform1i(resolve_loc_samples_, supersample_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_tex_);

    glBindVertexArray(resolve_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PlotFbo::bind()   { glBindFramebuffer(GL_FRAMEBUFFER, fbo_); }
void PlotFbo::unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

} // namespace sextant
