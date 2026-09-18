#pragma once

namespace sextant {

// Resizable offscreen target for the plot, rendered into a color texture and
// shown in the "Plot" dock panel via ImGui::Image(). GPU-only (unlike
// FboReadback). Create/destroy on the GL thread with the context current.
class PlotFbo {
public:
    PlotFbo() = default;
    ~PlotFbo();
    PlotFbo(const PlotFbo&) = delete;
    PlotFbo& operator=(const PlotFbo&) = delete;

    // Reallocates only when the geometry changes (cheap per frame); values
    // <= 0 clamp to 1. width/height are the display size; render at
    // render_width()/render_height(), then call resolve().
    void ensure_size(int width, int height, int supersample = 1);

    void bind();
    void unbind();

    // Box-filter the supersampled target into color_texture(). Call after
    // drawing, before sampling; no-op at supersample 1. Unbinds the FBO.
    void resolve();

    // Display-size color texture for ImGui::Image(); valid after resolve().
    unsigned int color_texture() const { return resolved_tex_ ? resolved_tex_ : color_tex_; }

    // Display size.
    int width()  const { return width_; }
    int height() const { return height_; }

    // Render-target size (display x supersample), for raw GL state such as
    // glViewport.
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

    // Display-size resolve target; 0 at supersample 1, where color_tex_ is used
    // directly.
    unsigned int resolved_fbo_ = 0;
    unsigned int resolved_tex_ = 0;

    // Box-filter program, built lazily and kept across resizes.
    unsigned int resolve_program_ = 0;
    unsigned int resolve_vao_     = 0;
    int          resolve_loc_samples_ = -1;
};

} // namespace sextant
