#pragma once
#include <string>
#include <string_view>

namespace sextant {

// Text measurement without a NanoVG context, so raster and SVG layout agree.
// Reproduces fontstash's advance arithmetic exactly (fons__getQuad). Logical
// pixels; ignores supersample (sub-pixel differences above 1 are accepted).
// Thread-safe.

struct FontVMetrics {
    float ascent      = 0.0f;   // above the baseline, positive
    float descent     = 0.0f;   // below the baseline, negative
    float line_height = 0.0f;   // ascent - descent
};

// Advance width of `text` at `px_size`. "" = the default font, resolved as the
// renderers do. Falls back to a per-character estimate if the font can't be read.
float text_width(const std::string& font_path, float px_size, std::string_view text);

FontVMetrics font_vmetrics(const std::string& font_path, float px_size);

// True when real glyph metrics are in use (not the fallback). For tests.
bool text_metrics_font_loaded(const std::string& font_path);

} // namespace sextant
