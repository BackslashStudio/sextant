// STBTT_STATIC is required: without FreeType, nanovg's fontstash.h
// already defines the stb_truetype implementation, so an external-linkage copy
// here would collide at link time.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#ifdef FONS_USE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H   // FREETYPE_MAJOR/MINOR: see uses_typo_metrics()
#endif

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

// Fallback advance per character at 1 px em, when no font can be read.
constexpr float kFallbackAdvanceFactor = 0.55f;

// The width cache is cleared wholesale when it grows past this (tick labels
// change as the view pans).
constexpr std::size_t kWidthCacheCap = 8192;

// Transparent hashing, so lookups from a string_view don't allocate.
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
    // Normalized as fons__loadFont does (line gap folded into the ascender,
    // both divided by the em box), so line_height equals the font size.
    float ascent_norm  = 0.0f;
    float descent_norm = 0.0f;
    float lineh_norm   = 0.0f;

    // Measured widths, bucketed by quantized size.
    std::unordered_map<short, WidthMap> widths;
    std::size_t width_entries = 0;
};

std::mutex                                   g_mutex;
std::unordered_map<std::string, LoadedFont>  g_fonts;    // resolved path -> font

// "" = the default font, resolved once via pick_default_font() as the
// renderers do.
const std::string& resolve_path(const std::string& font_path) {
    if (!font_path.empty()) return font_path;
    static const std::string fallback = [] {
        const FontEntry* def = pick_default_font();
        return def ? def->path : std::string{};
    }();
    return fallback;
}

// fontstash stores the size as a short in tenths of a pixel; apply the same
// quantization. Keep the short itself, since fonsVertMetrics multiplies before
// dividing and float order matters for exact agreement.
short quantize_isize(float px_size) {
    if (px_size <= 0.0f)   return 0;
    if (px_size > 3200.0f) px_size = 3200.0f;   // keep the short below its range
    return static_cast<short>(px_size * 10.0f);
}

// Which vertical metrics fontstash reads. Its stb backend takes `hhea`
// (stbtt_GetFontVMetrics). Its FreeType backend takes face->ascender/descender/
// height, which FreeType >= 2.10 fills from the OS/2 sTypo* fields when the font
// sets USE_TYPO_METRICS (fsSelection bit 7), else from `hhea` (sfobjs.c).
// FreeType also falls back to OS/2 when `hhea` ascent and descent are both
// zero; stb does not, and such fonts are not handled here.
bool uses_typo_metrics([[maybe_unused]] const stbtt_fontinfo& info) {
#if defined(FONS_USE_FREETYPE) && \
    (FREETYPE_MAJOR > 2 || (FREETYPE_MAJOR == 2 && FREETYPE_MINOR >= 10))
    const stbtt_uint32 os2 = stbtt__find_table(info.data, static_cast<stbtt_uint32>(info.fontstart), "OS/2");
    if (!os2) return false;
    const stbtt_uint8* fs_selection = info.data + os2 + 62;
    return (fs_selection[1] & 0x80) != 0;   // big-endian u16, bit 7 in the low byte
#else
    return false;
#endif
}

// Caller must hold g_mutex. Unusable fonts are cached with ok=false.
LoadedFont& get_font(const std::string& path) {
    if (auto it = g_fonts.find(path); it != g_fonts.end()) return it->second;

    // In place: stbtt_fontinfo points into `data`, which must not move.
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
    if (!uses_typo_metrics(f.info) ||
        !stbtt_GetFontVMetricsOS2(&f.info, &ascent, &descent, &line_gap))
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

// Minimal UTF-8 decode; invalid bytes are skipped (inputs are well-formed in
// practice).
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

// Kerning only on builds whose text is kerned: fontstash's FreeType backend
// effectively drops kerning (it rescales a pixel value), the stb one doesn't.
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

// fontstash's exact arithmetic: per-glyph advance truncated to tenths of a
// pixel in a short, each pen step rounded to a whole pixel. Both truncations
// matter for matching NanoVG.
float measure(const LoadedFont& f, float size, std::string_view text) {
    // fons__getGlyph bails below 0.2 px, so nothing advances at all there.
    if (size * 10.0f < 2.0f) return 0.0f;

    const float scale = stbtt_ScaleForMappingEmToPixels(&f.info, size);
    float x    = 0.0f;
    int   prev = -1;

    for_each_codepoint(text, [&](int cp) {
        // Missing glyphs measure as glyph 0 (.notdef), as fontstash draws them.
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
        // No usable font: estimate per codepoint (not per byte).
        std::size_t chars = 0;
        for_each_codepoint(text, [&](int) { ++chars; });
        return static_cast<float>(chars) * size * kFallbackAdvanceFactor;
    }

    WidthMap& bucket = f.widths[isize];
    // Heterogeneous find; only a miss allocates.
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
        // A typical split, so a fontless figure still lays out sensibly.
        const float size = fsize / 10.0f;
        return { size * 0.8f, size * -0.2f, size };
    }
    // Multiply first, divide second, as fonsVertMetrics does. line_height
    // equals the font size exactly.
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
