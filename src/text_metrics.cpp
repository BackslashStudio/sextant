// STB_TRUETYPE_STATIC is not optional: with SEXTANT_USE_FREETYPE=OFF,
// nanovg.c's fontstash.h defines STB_TRUETYPE_IMPLEMENTATION itself, so a
// second external-linkage copy here would collide at link time. The header
// resolves to nanovg's or stb's copy depending on include order; they are the
// same file, which is why the ambiguity is harmless.
#define STB_TRUETYPE_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "text_metrics.h"
#include "font_discovery.h"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sextant {
namespace {

// Used only when the font file cannot be read at all — see text_width().
// Roughly the advance of a digit in a typical serif face at 1 px em.
constexpr float kFallbackAdvanceFactor = 0.55f;

// Tick labels change as the view pans, so the width cache would otherwise
// grow without bound over a long interactive session. Cleared wholesale
// rather than evicted by age: a rebuild costs one measurement per visible
// label, which is a handful of microseconds.
constexpr std::size_t kWidthCacheCap = 8192;

// Transparent hashing, so a width lookup can be done straight from the
// caller's string_view without materializing a std::string to look it up
// with. Layout measures every visible tick label on every frame; an
// allocation per lookup is the difference between a cache that helps and one
// that just moves the cost around.
struct SvHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

using WidthMap = std::unordered_map<std::string, float, SvHash, std::equal_to<>>;

struct LoadedFont {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool  ok = false;
    // Normalized exactly as fons__loadFont does, so font_vmetrics() below
    // reproduces fonsVertMetrics(): the line gap is folded into the ascender
    // and both are divided by the resulting em box, which makes line_height
    // identically equal to the font size.
    float ascent_norm  = 0.0f;
    float descent_norm = 0.0f;
    float lineh_norm   = 0.0f;

    // Measured widths, bucketed by quantized size so the inner lookup hashes
    // nothing but the text itself. Held per font rather than in one global
    // map keyed on a concatenated path+size+text string, which is what this
    // started as — building that key allocated on every call.
    std::unordered_map<short, WidthMap> widths;
    std::size_t width_entries = 0;
};

std::mutex                                   g_mutex;
std::unordered_map<std::string, LoadedFont>  g_fonts;    // resolved path -> font

// "" means the default font, resolved through pick_default_font() -- the same
// call NvgRenderer and the SVG writer make, so all three agree on which file
// is being measured, drawn and named. Returns a reference, and resolves the
// default once: this is called for every measurement.
const std::string& resolve_path(const std::string& font_path) {
    if (!font_path.empty()) return font_path;
    static const std::string fallback = [] {
        const FontEntry* def = pick_default_font();
        return def ? def->path : std::string{};
    }();
    return fallback;
}

// fontstash keeps the font size as a short in tenths of a pixel (isize), so
// every measurement starts by applying the same quantization -- otherwise a
// 10.04 px request would measure differently here than it draws there.
//
// The short itself is carried around rather than only the divided-back size,
// because fonsVertMetrics multiplies by isize *before* dividing by 10 and
// float multiplication is not associative; reordering costs the last couple
// of bits, enough to fail an exact comparison against nvgTextMetrics.
short quantize_isize(float px_size) {
    if (px_size <= 0.0f)   return 0;
    if (px_size > 3200.0f) px_size = 3200.0f;   // keep the short below its range
    return static_cast<short>(px_size * 10.0f);
}

// Caller must hold g_mutex. Non-const because callers memoize measured
// widths into the returned entry. Returns an entry with ok=false (cached, so the
// file IO is not retried on every call) when the font can't be used.
LoadedFont& get_font(const std::string& path) {
    if (auto it = g_fonts.find(path); it != g_fonts.end()) return it->second;

    // Constructed in place: stbtt_fontinfo stores a raw pointer into `data`,
    // so the buffer must not be relocated after stbtt_InitFont() runs.
    LoadedFont& f = g_fonts[path];
    if (path.empty()) return f;

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return f;
    const std::streamoff size = in.tellg();
    if (size <= 0) return f;

    f.data.resize(static_cast<std::size_t>(size));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(f.data.data()), size)) {
        f.data.clear();
        return f;
    }

    const int offset = stbtt_GetFontOffsetForIndex(f.data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&f.info, f.data.data(), offset)) {
        f.data.clear();
        return f;
    }

    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&f.info, &ascent, &descent, &line_gap);
    ascent += line_gap;
    const float fh = static_cast<float>(ascent - descent);
    if (fh <= 0.0f) {
        f.data.clear();
        return f;
    }
    f.ascent_norm  = static_cast<float>(ascent)  / fh;
    f.descent_norm = static_cast<float>(descent) / fh;
    f.lineh_norm   = f.ascent_norm - f.descent_norm;
    f.ok = true;
    return f;
}

// Minimal UTF-8 decode. Bytes that don't form a valid sequence are skipped
// without emitting a codepoint, which is how fons__decutf8's DFA behaves for
// the cases that matter here; the exact recovery behaviour after an invalid
// sequence is not reproduced, since every string this measures (tick labels,
// titles, legend entries) is well-formed in practice.
template <class F>
void for_each_codepoint(std::string_view s, F&& fn) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        std::size_t   n  = 0;
        if      (b0 < 0x80) { cp = b0;        n = 1; }
        else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1Fu; n = 2; }
        else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0Fu; n = 3; }
        else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07u; n = 4; }
        else { ++i; continue; }                       // stray continuation byte

        if (i + n > s.size()) return;                 // truncated tail
        bool valid = true;
        for (std::size_t k = 1; k < n; ++k) {
            const auto b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if (valid) fn(static_cast<int>(cp));
        i += valid ? n : 1;
    }
}

// Kerning, but only on the builds whose text is actually kerned.
//
// fontstash applies `fons__tt_getGlyphKernAdvance() * scale` in fons__getQuad.
// Its stb backend returns kerning in *font units*, so that scaling is right.
// Its FreeType backend returns FT_Get_Kerning already in pixels -- scaling
// that again shrinks it by ~1/2048 and `(int)(adv + 0.5f)` floors it to zero.
// So a FreeType build draws *unkerned* text, and measuring with kerning would
// over-reserve by a pixel or two per label.
float kern_advance([[maybe_unused]] const stbtt_fontinfo& info,
                   [[maybe_unused]] int prev,
                   [[maybe_unused]] int glyph,
                   [[maybe_unused]] float scale) {
#ifdef FONS_USE_FREETYPE
    return 0.0f;
#else
    if (prev == -1) return 0.0f;
    // FONSstate::spacing is 0 for every call sextant makes.
    const float adv = static_cast<float>(stbtt_GetGlyphKernAdvance(&info, prev, glyph)) * scale;
    return static_cast<float>(static_cast<int>(adv + 0.5f));
#endif
}

// The arithmetic below is fontstash's, not a reimplementation of "text
// width": per glyph the advance is scaled into tenths of a pixel and
// truncated into a short (fons__getGlyph), then each pen step is rounded to a
// whole pixel (fons__getQuad). Both truncations are load-bearing — dropping
// them puts this up to half a pixel per glyph away from what NanoVG draws.
float measure(const LoadedFont& f, float size, std::string_view text) {
    // fons__getGlyph bails below 0.2 px, so nothing advances at all there.
    if (size * 10.0f < 2.0f) return 0.0f;

    const float scale = stbtt_ScaleForMappingEmToPixels(&f.info, size);
    float x    = 0.0f;
    int   prev = -1;

    for_each_codepoint(text, [&](int cp) {
        // A codepoint the font lacks resolves to glyph 0 (.notdef) and is
        // measured with its advance — fontstash caches an empty glyph for it
        // the same way, since sextant registers no fallback fonts.
        const int g = stbtt_FindGlyphIndex(&f.info, cp);

        x += kern_advance(f.info, prev, g, scale);

        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&f.info, g, &advance, &lsb);
        const short xadv = static_cast<short>(scale * static_cast<float>(advance) * 10.0f);
        x += static_cast<float>(static_cast<int>(xadv / 10.0f + 0.5f));

        prev = g;
    });

    return x;
}

} // namespace

float text_width(const std::string& font_path, float px_size, std::string_view text) {
    if (text.empty()) return 0.0f;
    const short isize = quantize_isize(px_size);
    if (isize <= 0) return 0.0f;
    const float size = static_cast<float>(isize) / 10.0f;

    const std::string& path = resolve_path(font_path);

    std::lock_guard<std::mutex> lock(g_mutex);
    LoadedFont& f = get_font(path);
    if (!f.ok) {
        // No usable font file. Count codepoints rather than bytes so a
        // multi-byte label isn't wildly over-reserved.
        std::size_t chars = 0;
        for_each_codepoint(text, [&](int) { ++chars; });
        return static_cast<float>(chars) * size * kFallbackAdvanceFactor;
    }

    WidthMap& bucket = f.widths[isize];
    // Heterogeneous find: hashes `text` where it already lives, with no
    // string constructed for the lookup. Only a miss allocates.
    if (auto it = bucket.find(text); it != bucket.end()) return it->second;

    const float w = measure(f, size, text);
    if (f.width_entries >= kWidthCacheCap) {
        f.widths.clear();
        f.width_entries = 0;
        f.widths[isize].emplace(text, w);
    } else {
        bucket.emplace(text, w);
    }
    ++f.width_entries;
    return w;
}

FontVMetrics font_vmetrics(const std::string& font_path, float px_size) {
    const short isize = quantize_isize(px_size);
    const float fsize = static_cast<float>(isize);
    const std::string& path = resolve_path(font_path);

    std::lock_guard<std::mutex> lock(g_mutex);
    const LoadedFont& f = get_font(path);
    if (!f.ok) {
        // Same split a typical face has, so a fontless figure still stacks
        // its decorations in roughly the right places.
        const float size = fsize / 10.0f;
        return { size * 0.8f, size * -0.2f, size };
    }
    // Multiply first, divide second — fonsVertMetrics' own expression, for
    // the reason given on quantize_isize(). line_height comes out exactly
    // equal to the font size, since get_font() divides both metrics by their
    // own sum; the layout code can rely on that.
    return { f.ascent_norm  * fsize / 10.0f,
             f.descent_norm * fsize / 10.0f,
             f.lineh_norm   * fsize / 10.0f };
}

bool text_metrics_font_loaded(const std::string& font_path) {
    const std::string& path = resolve_path(font_path);
    std::lock_guard<std::mutex> lock(g_mutex);
    return get_font(path).ok;
}

} // namespace sextant
