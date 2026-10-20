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

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
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
    // Ink boxes, the same way (math layout only).
    std::unordered_map<short, std::unordered_map<std::string, InkBox, SvHash, std::equal_to<>>> inks;
    std::size_t ink_entries = 0;
};

std::mutex                                   g_mutex;
std::unordered_map<std::string, LoadedFont>  g_fonts;    // font_key() -> font

// A face's file and index inside it (0 unless a .ttc's later face).
struct FaceRef {
    std::string file;
    int index = 0;
};

// (resolved path, Italic or Bold) -> that face, nullopt when it has none.
std::unordered_map<std::string, std::optional<FaceRef>> g_styled;

std::string font_key(const std::string& file, int index) {
    return index == 0 ? file : file + "#" + std::to_string(index);
}

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

// Face `index` of `path`. Caller must hold g_mutex. Unusable fonts are cached
// with ok=false.
LoadedFont& get_font(const std::string& path, int index = 0) {
    const std::string key = font_key(path, index);
    if (auto it = g_fonts.find(key); it != g_fonts.end()) return it->second;

    // In place: stbtt_fontinfo points into `data`, which must not move.
    LoadedFont& f = g_fonts[key];
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

    const int offset = stbtt_GetFontOffsetForIndex(f.data.data(), index);
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
// matter for matching NanoVG. A glyph `f` lacks comes from the first of
// `fallbacks` that has it, at that font's scale, as fons__getGlyph() does.
float measure(const LoadedFont& f, const std::vector<const LoadedFont*>& fallbacks,
              float size, std::string_view text) {
    // fons__getGlyph bails below 0.2 px, so nothing advances at all there.
    if (size * 10.0f < 2.0f) return 0.0f;

    const float scale = stbtt_ScaleForMappingEmToPixels(&f.info, size);
    float x    = 0.0f;
    int   prev = -1;

    for_each_codepoint(text, [&](int cp) {
        // Missing everywhere, a glyph measures as the font's glyph 0 (.notdef),
        // as fontstash draws it.
        int g = stbtt_FindGlyphIndex(&f.info, cp);
        const LoadedFont* from = &f;
        if (g == 0)
            for (const LoadedFont* fb : fallbacks)
                if (const int k = stbtt_FindGlyphIndex(&fb->info, cp); k != 0) {
                    g = k;
                    from = fb;
                    break;
                }

        // fontstash kerns with the string's own font, whichever font the glyph
        // came from.
        x += kern_advance(f.info, prev, g, scale);

        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&from->info, g, &advance, &lsb);
        const float gscale = from == &f ? scale : stbtt_ScaleForMappingEmToPixels(&from->info, size);
        const short xadv = static_cast<short>(gscale * static_cast<float>(advance) * 10.0f);
        x += static_cast<float>(static_cast<int>(xadv / 10.0f + 0.5f));

        prev = g;
    });

    return x;
}

// The italic or bold face of the font at resolved `path`, memoized. Caller
// must hold g_mutex.
const std::optional<FaceRef>& styled_of(const std::string& path, FontFace style) {
    const std::string key = path + (style == FontFace::Bold ? "|bold" : "|italic");
    if (auto it = g_styled.find(key); it != g_styled.end()) return it->second;
    std::optional<FaceRef> face;
    if (const FontEntry* e = find_font_entry(path)) {
        if (style == FontFace::Italic && !e->italic_path.empty()) face = FaceRef{e->italic_path, e->italic_index};
        if (style == FontFace::Bold && !e->bold_path.empty()) face = FaceRef{e->bold_path, e->bold_index};
    }
    return g_styled[key] = std::move(face);
}

// The loaded face `style` reads at resolved `path`: the family's italic or
// bold face when it has a usable one, else nullopt (upright). Caller must hold
// g_mutex.
std::optional<FaceRef> face_for(const std::string& path, FontFace style) {
    if (style == FontFace::Upright) return std::nullopt;
    const std::optional<FaceRef>& f = styled_of(path, style);
    if (f && get_font(f->file, f->index).ok) return f;
    return std::nullopt;
}

// The fallback fonts for a string in the font at `path`, in order. Caller must
// hold g_mutex.
std::vector<const LoadedFont*> fallbacks_for(const std::string& path) {
    std::vector<const LoadedFont*> fallbacks;
    for (const FontEntry* e : fallback_fonts()) {
        if (e->path == path && e->index == 0) continue;
        const LoadedFont& fb = get_font(e->path, e->index);   // map nodes never move
        if (fb.ok) fallbacks.push_back(&fb);
    }
    return fallbacks;
}

// measure()'s pen walk, collecting the glyphs' outline boxes instead; with a
// `band` (px, y up: low, high), only the outline points (on-curve and control)
// inside it.
InkBox measure_ink(const LoadedFont& f, const std::vector<const LoadedFont*>& fallbacks,
                   float size, std::string_view text,
                   std::optional<std::pair<float, float>> band = std::nullopt) {
    InkBox box;
    if (size * 10.0f < 2.0f) return box;
    const float scale = stbtt_ScaleForMappingEmToPixels(&f.info, size);
    float x = 0.0f;
    int prev = -1;
    bool any = false;
    for_each_codepoint(text, [&](int cp) {
        int g = stbtt_FindGlyphIndex(&f.info, cp);
        const LoadedFont* from = &f;
        if (g == 0)
            for (const LoadedFont* fb : fallbacks)
                if (const int k = stbtt_FindGlyphIndex(&fb->info, cp); k != 0) {
                    g = k;
                    from = fb;
                    break;
                }
        x += kern_advance(f.info, prev, g, scale);
        const float gscale = from == &f ? scale : stbtt_ScaleForMappingEmToPixels(&from->info, size);
        const auto grow = [&](const InkBox& gb) {
            if (!any) box = gb;
            box.left = std::min(box.left, gb.left);
            box.right = std::max(box.right, gb.right);
            box.bottom = std::min(box.bottom, gb.bottom);
            box.top = std::max(box.top, gb.top);
            any = true;
        };
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        if (band) {
            stbtt_vertex* v = nullptr;
            const int n = stbtt_GetGlyphShape(&from->info, g, &v);
            // Each edge of the outline's control polygon (which holds the
            // curves), clipped to the band.
            const float lo = band->first, hi = band->second;
            float px = 0.0f, py = 0.0f;
            const auto edge = [&](float vx, float vy) {
                const float ax = x + px * gscale, ay = py * gscale;
                const float bx = x + vx * gscale, by = vy * gscale;
                px = vx;
                py = vy;
                const float y0c = std::max(std::min(ay, by), lo), y1c = std::min(std::max(ay, by), hi);
                if (y0c > y1c) return;
                const auto at = [&](float y) {
                    return ay == by ? std::max(ax, bx) : ax + (bx - ax) * (y - ay) / (by - ay);
                };
                const float xa = ay == by ? std::min(ax, bx) : at(y0c), xb = at(y1c);
                grow({std::min(xa, xb), std::max(xa, xb), y0c, y1c});
            };
            for (int k = 0; k < n; ++k) {
                if (v[k].type == STBTT_vmove) {
                    px = v[k].x;
                    py = v[k].y;
                    continue;
                }
                if (v[k].type == STBTT_vcurve || v[k].type == STBTT_vcubic) edge(v[k].cx, v[k].cy);
                if (v[k].type == STBTT_vcubic) edge(v[k].cx1, v[k].cy1);
                edge(v[k].x, v[k].y);
            }
            stbtt_FreeShape(&from->info, v);
        } else if (stbtt_GetGlyphBox(&from->info, g, &x0, &y0, &x1, &y1) && x1 > x0 && y1 > y0) {
            grow({x + x0 * gscale, x + x1 * gscale, y0 * gscale, y1 * gscale});
        }
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&from->info, g, &advance, &lsb);
        const short xadv = static_cast<short>(gscale * static_cast<float>(advance) * 10.0f);
        x += static_cast<float>(static_cast<int>(xadv / 10.0f + 0.5f));
        prev = g;
    });
    return box;
}

// Ink of `text` in face `index` of `path`, cached. Caller must hold g_mutex.
InkBox ink_locked(const std::string& path, int index, short isize, std::string_view text) {
    const float size = static_cast<float>(isize) / 10.0f;
    LoadedFont& f = get_font(path, index);
    if (!f.ok) {
        // No usable font: x-height letters across the estimated advance.
        std::size_t chars = 0;
        for_each_codepoint(text, [&](int) { ++chars; });
        return {0.0f, static_cast<float>(chars) * size * kFallbackAdvanceFactor, 0.0f, size * 0.5f};
    }
    auto& bucket = f.inks[isize];
    if (auto it = bucket.find(text); it != bucket.end()) return it->second;
    const InkBox b = measure_ink(f, fallbacks_for(path), size, text);
    if (f.ink_entries >= kWidthCacheCap) {
        f.inks.clear();
        f.ink_entries = 0;
        f.inks[isize].emplace(text, b);
    } else {
        bucket.emplace(text, b);
    }
    ++f.ink_entries;
    return b;
}

// Width of `text` in face `index` of `path`, cached. Caller must hold g_mutex.
float width_locked(const std::string& path, int index, short isize, std::string_view text) {
    const float size = static_cast<float>(isize) / 10.0f;
    LoadedFont& f = get_font(path, index);
    if (!f.ok) {
        // No usable font: estimate per codepoint (not per byte).
        std::size_t chars = 0;
        for_each_codepoint(text, [&](int) { ++chars; });
        return static_cast<float>(chars) * size * kFallbackAdvanceFactor;
    }

    WidthMap& bucket = f.widths[isize];
    // Heterogeneous find; only a miss allocates.
    if (auto it = bucket.find(text); it != bucket.end()) return it->second;

    const float w = measure(f, fallbacks_for(path), size, text);
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

} // namespace

float text_width(const std::string& font_path, float px_size, std::string_view text) {
    if (text.empty()) return 0.0f;
    const short isize = quantize_isize(px_size);
    if (isize <= 0) return 0.0f;
    const std::string& path = resolve_path(font_path);
    std::lock_guard<std::mutex> lock(g_mutex);
    return width_locked(path, 0, isize, text);
}

float text_width_face(const std::string& font_path, float px_size, std::string_view text, FontFace face) {
    if (text.empty()) return 0.0f;
    const short isize = quantize_isize(px_size);
    if (isize <= 0) return 0.0f;
    const std::string& path = resolve_path(font_path);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (const auto f = face_for(path, face)) return width_locked(f->file, f->index, isize, text);
    const float w = width_locked(path, 0, isize, text);
    return face == FontFace::Bold ? w + kFauxBoldEm * static_cast<float>(isize) / 10.0f : w;
}

float text_width_italic(const std::string& font_path, float px_size, std::string_view text) {
    return text_width_face(font_path, px_size, text, FontFace::Italic);
}

bool has_face(const std::string& font_path, FontFace face) {
    if (face == FontFace::Upright) return true;
    const std::string& path = resolve_path(font_path);
    std::lock_guard<std::mutex> lock(g_mutex);
    return face_for(path, face).has_value();
}

bool has_italic_face(const std::string& font_path) { return has_face(font_path, FontFace::Italic); }

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

InkBox ink_box(const std::string& font_path, float px_size, std::string_view text, FontFace face) {
    if (text.empty()) return {};
    const short isize = quantize_isize(px_size);
    if (isize <= 0) return {};
    const std::string& path = resolve_path(font_path);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (const auto f = face_for(path, face)) return ink_locked(f->file, f->index, isize, text);
    InkBox b = ink_locked(path, 0, isize, text);
    if (face == FontFace::Bold && b.right > b.left) b.right += kFauxBoldEm * static_cast<float>(isize) / 10.0f;
    return b;
}

InkBox ink_box(const std::string& font_path, float px_size, std::string_view text, bool italic) {
    return ink_box(font_path, px_size, text, italic ? FontFace::Italic : FontFace::Upright);
}

InkBox ink_band(const std::string& font_path, float px_size, std::string_view text, float low, float high) {
    if (text.empty()) return {};
    const short isize = quantize_isize(px_size);
    if (isize <= 0) return {};
    const std::string& path = resolve_path(font_path);
    std::lock_guard<std::mutex> lock(g_mutex);
    const LoadedFont& f = get_font(path);
    if (!f.ok) return {};
    return measure_ink(f, fallbacks_for(path), static_cast<float>(isize) / 10.0f, text, std::pair{low, high});
}

bool font_has_glyphs(const std::string& font_path, std::string_view text) {
    const std::string& path = resolve_path(font_path);
    std::lock_guard<std::mutex> lock(g_mutex);
    const LoadedFont& f = get_font(path);
    if (!f.ok) return false;
    bool all = true;
    for_each_codepoint(text, [&](int cp) { all = all && stbtt_FindGlyphIndex(&f.info, cp) != 0; });
    return all;
}

bool text_metrics_font_loaded(const std::string& font_path) {
    const std::string& path = resolve_path(font_path);
    std::lock_guard<std::mutex> lock(g_mutex);
    return get_font(path).ok;
}

} // namespace sextant
