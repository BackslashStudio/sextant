#pragma once
#include <vector>
#include <cstdint>

namespace sextant {

// RAII offscreen FBO for PNG export; read_pixels() returns RGBA.
class FboReadback {
public:
    // width/height: the output image size. The render target is `supersample`
    // times larger and box-filtered back down in read_pixels().
    FboReadback(int width, int height, int supersample = 1);
    ~FboReadback();

    void bind();
    void unbind();

    // Reads a width x height top-down RGBA buffer, box-filtering the
    // supersampled target.
    std::vector<uint8_t> read_pixels() const;

    // Final image size.
    int width()  const { return width_; }
    int height() const { return height_; }

    // Render-target size (width()/height() x supersample), for raw GL state
    // such as glViewport.
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
