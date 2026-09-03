#pragma once
#include "sextant/style.h"   // SuptitleOptions, for suptitle_band_height()
#include <string>

namespace sextant {

struct PlotRect {
    float x, y;   // top-left in window pixels (y=0 at top)
    float w, h;
};

// Height of the band reserved at the top of the figure for the suptitle, once
// for the whole figure rather than per row; 0 when there is none.
//
// The single definition, shared by the raster path, the SVG path and the SVG
// writer itself. Derived from the font size rather than fixed, because
// SuptitleOptions::fontsize is editable: a fixed band meant a large suptitle
// overflowed into the first subplot row. The padding is what makes it
// reproduce exactly 36 px at the 21 px default.
constexpr float kSuptitlePad = 15.0f;

inline float suptitle_band_height(const std::string& text, const SuptitleOptions& opts) {
    return text.empty() ? 0.0f : opts.fontsize + kSuptitlePad;
}

// Inset of a Left/Right-aligned suptitle from the figure edge.
constexpr float kSuptitleSideMargin = 10.0f;

// Anchor point for the suptitle, shared so the raster and SVG paths cannot
// disagree on where it sits. The *alignment* itself still has to be applied
// by each renderer in its own idiom (NanoVG's nvgTextAlign vs SVG's
// text-anchor), but both align against this same x.
inline float suptitle_anchor_x(float fig_w, const SuptitleOptions& opts) {
    float x = fig_w * 0.5f;
    if      (opts.align == HAlign::Left)  x = kSuptitleSideMargin;
    else if (opts.align == HAlign::Right) x = fig_w - kSuptitleSideMargin;
    return x + opts.offset_x;
}

// Vertical centre of the text within the reserved band. offset_y nudges
// within the band without resizing it (see SuptitleOptions).
inline float suptitle_center_y(float band_height, const SuptitleOptions& opts) {
    return band_height * 0.5f + opts.offset_y;
}

} // namespace sextant
