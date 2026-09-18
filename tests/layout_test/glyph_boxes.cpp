// See glyph_boxes.h. Part of sextant_layout_test.
#include "glyph_boxes.h"

#include "text_metrics.h"

// A private copy: static, so it cannot collide with the library's.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

namespace lt {
    struct GlyphBoxes::Impl {
        std::vector<unsigned char> data;
        stbtt_fontinfo info{};
        bool ok = false;
    };

    GlyphBoxes::GlyphBoxes(const std::string& font_file) : impl_(std::make_unique<Impl>()) {
        std::ifstream in(font_file, std::ios::binary);
        impl_->data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (impl_->data.empty()) return;
        const int offset = stbtt_GetFontOffsetForIndex(impl_->data.data(), 0);
        impl_->ok = offset >= 0 && stbtt_InitFont(&impl_->info, impl_->data.data(), offset);
    }

    GlyphBoxes::~GlyphBoxes() = default;

    bool GlyphBoxes::ok() const { return impl_->ok; }

    float GlyphBoxes::ink_overhang(const std::string& path, float size, const std::string& s) const {
        if (!impl_->ok) return 0.0f;
        const stbtt_fontinfo& info = impl_->info;
        const float scale = stbtt_ScaleForMappingEmToPixels(&info, size);

        // The extent starts at the pen origin, as fonsTextBounds' does.
        float minx = 0.0f, maxx = 0.0f;
        for (std::size_t i = 0; i < s.size();) {
            std::size_t n = 1;
            while (i + n < s.size() && (static_cast<unsigned char>(s[i + n]) & 0xC0) == 0x80) ++n;
            const std::string glyph = s.substr(i, n);

            auto b = [&](std::size_t k) { return static_cast<unsigned char>(glyph[k]); };
            int cp = b(0);
            if (n == 2) cp = ((b(0) & 0x1F) << 6) | (b(1) & 0x3F);
            else if (n == 3) cp = ((b(0) & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F);
            else if (n == 4) cp = ((b(0) & 0x07) << 18) | ((b(1) & 0x3F) << 12) |
                                  ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);

            // Every pen step is a whole pixel, so this is where glyph i is drawn,
            // after any kerning against the glyph before it.
            const float pen = sextant::text_width(path, size, s.substr(0, i + n)) -
                              sextant::text_width(path, size, glyph);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetGlyphBitmapBox(&info, stbtt_FindGlyphIndex(&info, cp), scale, scale,
                                    &x0, &y0, &x1, &y1);
            if (x1 > x0) {
                minx = std::min(minx, pen + static_cast<float>(x0));
                maxx = std::max(maxx, pen + static_cast<float>(x1));
            }
            i += n;
        }
        return (maxx - minx) - sextant::text_width(path, size, s);
    }
} // namespace lt
