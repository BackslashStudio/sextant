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

// A face of `font_path`'s family, for math: its italic (FontEntry::italic_path)
// or bold (FontEntry::bold_path) face.
enum class FontFace { Upright, Italic, Bold };

// A family with no bold face draws its upright face twice, kFauxBoldEm apart
// (ems of the size): the run is that much wider.
constexpr float kFauxBoldEm = 0.04f;

// text_width() in `face`, with the same fallback fonts. A font with no italic
// face measures upright (the renderers slant it, which keeps its advances); one
// with no bold face upright plus kFauxBoldEm.
float text_width_face(const std::string& font_path, float px_size, std::string_view text, FontFace face);
float text_width_italic(const std::string& font_path, float px_size, std::string_view text);

// Whether `font_path` ("" = the default) has the face text_width_face() reads
// (always for Upright). For the renderers and tests.
bool has_face(const std::string& font_path, FontFace face);
bool has_italic_face(const std::string& font_path);

FontVMetrics font_vmetrics(const std::string& font_path, float px_size);

// The glyph outlines' extent of `text` at `px_size`, y up from the baseline,
// x from the pen's start: glyphs and pen steps as text_width() takes them, the
// fallback fonts included (the italic face when `italic`, as
// text_width_italic()). All zero for an empty string or one with no outlines
// (spaces); a guess when the font can't be read. For math layout, which places
// fractions and roots by ink, as TeX does.
struct InkBox {
    float left = 0.0f, right = 0.0f;
    float bottom = 0.0f, top = 0.0f;
};
InkBox ink_box(const std::string& font_path, float px_size, std::string_view text, bool italic = false);
// In `face`, as text_width_face() takes it (a faux bold's ink kFauxBoldEm wider).
InkBox ink_box(const std::string& font_path, float px_size, std::string_view text, FontFace face);

// The ink of `text` (upright) between heights `low` and `high` (px, y up), as
// ink_box() takes it: the outlines' control polygons clipped to the band, so a
// little wide at a curve. All zero when no outline reaches the band. For the
// scripts of an integral, set by its hooks' actual reach. Not cached.
InkBox ink_band(const std::string& font_path, float px_size, std::string_view text, float low, float high);

// Whether the font at `font_path` itself (no fallback) has a glyph for every
// code point of `text`. For the math font's Unicode math letters.
bool font_has_glyphs(const std::string& font_path, std::string_view text);

// True when real glyph metrics are in use (not the fallback). For tests.
bool text_metrics_font_loaded(const std::string& font_path);

} // namespace sextant
