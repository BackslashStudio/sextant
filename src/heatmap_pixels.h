#pragma once
// A heatmap's cell colours as RGBA8, shared by the GL texture, the SVG <image>
// and a perspective plane's cell polygons, so every output colours a cell the
// same way: a value through the colormap, or an imshow_rgb() image's own colour
// (each channel clamped to [0, 1]). A NaN value, a value the norm leaves
// without a colour (<= 0 under log), or an image pixel with a NaN channel, is a
// transparent cell.
#include "color_norm.h"
#include "colormaps.h"
#include "plot_objects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sextant {

class HeatmapPixels {
public:
    explicit HeatmapPixels(const HeatmapPlot& hp)
        : hp_(hp), lut_(colormaps::get(hp.opts.cmap)), cyclic_(hp.opts.cmap.is_cyclic()),
          vmin_(hp.opts.vmin), vrange_(hp.opts.vmax - hp.opts.vmin),
          norm_(norm_in_force(hp.opts.norm, hp.opts.vmin, hp.opts.vmax)) {}

    // Storage cell k (row * cols + col, row 0 first whatever the origin) into
    // out[0..3]. Float arithmetic as the readers always did, so a heatmap's
    // bytes are unchanged.
    void rgba(std::size_t k, std::uint8_t* out) const {
        if (hp_.channels == 1) {
            const float v = hp_.data[k];
            if (std::isnan(v)) {
                std::memset(out, 0, 4);
                return;
            }
            double t;
            if (norm_ == ColorNorm::Kind::Linear) {
                t = (vrange_ != 0.0f) ? (v - vmin_) / vrange_ : 0.0;
            } else {
                t = norm_t(norm_, hp_.opts.norm, hp_.opts.vmin, hp_.opts.vmax, v);
                if (std::isnan(t)) {
                    std::memset(out, 0, 4);
                    return;
                }
            }
            std::memcpy(out, &lut_[colormaps::index(t, cyclic_) * 4], 4);
            return;
        }
        const std::size_t n = static_cast<std::size_t>(hp_.channels);
        const float* c = hp_.data.data() + k * n;
        for (std::size_t i = 0; i < n; ++i) {
            if (std::isnan(c[i])) {
                std::memset(out, 0, 4);
                return;
            }
            out[i] = static_cast<std::uint8_t>(std::lround(std::clamp(c[i], 0.0f, 1.0f) * 255.0f));
        }
        if (n == 3) out[3] = 255;
    }

    Color color(std::size_t k) const {
        std::uint8_t c[4];
        rgba(k, c);
        return {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f};
    }

private:
    const HeatmapPlot&   hp_;
    const std::uint8_t*  lut_;
    bool                 cyclic_;
    float                vmin_, vrange_;
    ColorNorm::Kind      norm_;
};

} // namespace sextant
