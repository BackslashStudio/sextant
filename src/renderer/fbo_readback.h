#pragma once
#include <vector>
#include <cstdint>

namespace sextant {

// RAII wrapper for an OpenGL FBO used for off-screen rendering.
// After rendering, call read_pixels() to get RGBA data for PNG export.
class FboReadback {
public:
    // width/height are the size of the *image that will be written*.
    // supersample (see FigureOptions::supersample, clamped to
    // [1, kMaxSupersample]) enlarges only the render target, which is
    // allocated at width*supersample x height*supersample; read_pixels()
    // box-filters it back down to width x height, so PNG output gets the
    // same antialiasing as the on-screen plot.
    FboReadback(int width, int height, int supersample = 1);
    ~FboReadback();

    void bind();
    void unbind();

    // Reads the FBO contents into a width x height RGBA byte buffer
    // (bottom-up from GL, then flipped to top-down), box-filtering the
    // supersampled render target down on the way.
    std::vector<uint8_t> read_pixels() const;

    // Final image size.
    int width()  const { return width_; }
    int height() const { return height_; }

    // Size of the render target — width()/height() times the supersample
    // factor. render_frame() wants width()/height() plus the factor instead;
    // these are for raw GL state such as glViewport.
    int render_width()  const { return width_  * supersample_; }
    int render_height() const { return height_ * supersample_; }
    int supersample()   const { return supersample_; }

private:
    int          width_       = 0;
    int          height_      = 0;
    int          supersample_ = 1;
    unsigned int fbo_      = 0;
    unsigned int color_rb_ = 0;
    unsigned int depth_rb_ = 0;
};

} // namespace sextant
