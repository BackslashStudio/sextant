#pragma once
#include "sextant/style.h"   // SuptitleOptions, for suptitle_band_height()
#include <string>

namespace sextant {

struct PlotRect {
    float x, y;   // top-left in window pixels (y=0 at top)
    float w, h;
};

// Height of the figure-wide suptitle band (0 when there is none), derived from
// the font size; 36 px at the 21 px default. Shared by raster and SVG paths.
constexpr float kSuptitlePad = 15.0f;

inline float suptitle_band_height(const std::string& text, const SuptitleOptions& opts) {
    return text.empty() ? 0.0f : opts.fontsize + kSuptitlePad;
}

// Inset of a Left/Right-aligned suptitle from the figure edge.
constexpr float kSuptitleSideMargin = 10.0f;

// Suptitle anchor x, shared by raster and SVG; each applies the alignment in
// its own way.
inline float suptitle_anchor_x(float fig_w, const SuptitleOptions& opts) {
    float x = fig_w * 0.5f;
    if      (opts.align == HAlign::Left)  x = kSuptitleSideMargin;
    else if (opts.align == HAlign::Right) x = fig_w - kSuptitleSideMargin;
    return x + opts.offset_x;
}

// Vertical centre of the text in the band; offset_y doesn't resize the band.
inline float suptitle_center_y(float band_height, const SuptitleOptions& opts) {
    return band_height * 0.5f + opts.offset_y;
}

} // namespace sextant
