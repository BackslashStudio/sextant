#pragma once

namespace sextant {

// Resizable off-screen render target for the 3-pass plot render. The plot is
// rendered into this FBO's color *texture* (not a renderbuffer -- it must be
// sampleable) exactly as it would be into a window, then displayed inside the
// "Plot" dock panel via ImGui::Image(). render_frame() stays unaware that a
// dockspace exists.
//
// Distinct from FboReadback (savefig's CPU-readback FBO): this one stays
// entirely on the GPU and is never read back to host memory.
//
// Must be constructed and destroyed on the thread that owns the GLContext,
// while it is current -- the same discipline as NvgRenderer/DataRenderer.
class PlotFbo {
public:
    PlotFbo() = default;
    ~PlotFbo();
    PlotFbo(const PlotFbo&) = delete;
    PlotFbo& operator=(const PlotFbo&) = delete;

    // (Re)allocates GPU storage only when the requested geometry differs from
    // the current allocation, so this is cheap to call every frame. Values
    // <= 0 are clamped to 1 (a docked panel can transiently report a zero-size
    // content region while being dragged).
    //
    // width/height are the *display* size in framebuffer pixels; supersample
    // enlarges only the internal render target. Render into it via
    // render_width()/render_height(), then call resolve().
    void ensure_size(int width, int height, int supersample = 1);

    void bind();
    void unbind();

    // Box-filters the supersampled render target down into the display-size
    // texture color_texture() returns. Call once per frame after all drawing
    // into this FBO is finished and before the texture is sampled. A no-op at
    // supersample == 1. Leaves no framebuffer bound.
    void resolve();

    // Raw GL texture name of the display-size color attachment — cast to
    // ImTextureID (e.g. `(ImTextureID)(intptr_t)color_texture()`) for
    // ImGui::Image(). Only meaningful after resolve().
    unsigned int color_texture() const { return resolved_tex_ ? resolved_tex_ : color_tex_; }

    // Display size — what color_texture() measures, and what a caller should
    // draw the image at.
    int width()  const { return width_; }
    int height() const { return height_; }

    // Size of the render target bind() selects — width()/height() times the
    // supersample factor. Note render_frame() wants the *logical* size
    // (width()/height()) plus the factor, not these; these are for raw GL
    // state such as glViewport.
    int render_width()  const { return width_  * supersample_; }
    int render_height() const { return height_ * supersample_; }
    int supersample()   const { return supersample_; }

private:
    void destroy();
    void ensure_resolve_program();

    int          width_       = 0;
    int          height_      = 0;
    int          supersample_ = 1;
    unsigned int fbo_         = 0;   // supersampled render target
    unsigned int color_tex_   = 0;   // its color attachment
    unsigned int depth_rb_    = 0;   // its depth+stencil attachment

    // Display-size resolve target. Unused (0) when supersample_ == 1, in
    // which case color_texture() hands out color_tex_ directly and resolve()
    // does nothing — the common no-antialiasing path allocates and costs
    // the un-supersampled case.
    unsigned int resolved_fbo_ = 0;
    unsigned int resolved_tex_ = 0;

    // Fullscreen box-filter pass used by resolve(). Built lazily on first
    // supersampled resolve and kept for this PlotFbo's lifetime, so a resize
    // (which reallocates the targets) doesn't rebuild the program.
    unsigned int resolve_program_ = 0;
    unsigned int resolve_vao_     = 0;
    int          resolve_loc_samples_ = -1;
};

} // namespace sextant
