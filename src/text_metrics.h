#pragma once
#include <string>
#include <string_view>

namespace sextant {

// Text measurement that does not need a NanoVG context.
//
// Layout has to reserve space for text, and must come out *identical* in the
// raster path (which has an NVGcontext) and the SVG path (headless, no GL at
// all). nvgTextBounds() is therefore unavailable to half the callers.
//
// The numbers are not an estimate: this reproduces fontstash's own advance
// arithmetic exactly -- the same 0.1 px font-size quantization, the same
// per-glyph truncation to a short, the same rounding of each pen step to a
// whole pixel (see fons__getQuad in third_party/nanovg/fontstash.h). So the
// space layout reserves is the space NanoVG actually draws into.
//
// Everything is in *logical* pixels and ignores FigureOptions::supersample.
// NanoVG quantizes at the device-pixel size, so above supersample 1 the drawn
// advance can differ by a fraction of a pixel; layout must not depend on the
// supersample factor, so that fraction is accepted rather than tracked.
//
// Thread-safe: the caches are mutex-guarded, because layout runs both on the
// render thread and on whatever caller thread calls savefig().

struct FontVMetrics {
    float ascent      = 0.0f;   // above the baseline, positive
    float descent     = 0.0f;   // below the baseline, negative
    float line_height = 0.0f;   // ascent - descent
};

// Advance width (total pen movement, what nvgText/nvgTextBounds return) of
// `text` at `px_size`. "" means the default font, resolved through
// pick_default_font() exactly as NvgRenderer and the SVG writer resolve it.
// Falls back to a per-character heuristic if the font file cannot be read, so
// a figure with no usable font still lays out, just less precisely.
float text_width(const std::string& font_path, float px_size, std::string_view text);

FontVMetrics font_vmetrics(const std::string& font_path, float px_size);

// True when the font behind `font_path` was loaded and real glyph metrics
// are in use — i.e. text_width() is exact rather than the fallback estimate.
// Exists so a test can assert it is not silently validating the heuristic
// against itself.
bool text_metrics_font_loaded(const std::string& font_path);

} // namespace sextant
