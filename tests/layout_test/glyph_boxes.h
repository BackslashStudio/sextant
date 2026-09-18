// A font's own glyph ink, read without NanoVG, for text_metrics.cpp's overhang
// check. Its own translation unit: stb_truetype's implementation and
// imgui_internal.h (via layout_test.h) declare conflicting stbrp_node types.
#pragma once

#include <memory>
#include <string>

namespace lt {
    class GlyphBoxes {
    public:
        explicit GlyphBoxes(const std::string& font_file);   // face 0, as fontstash loads it
        ~GlyphBoxes();

        bool ok() const;

        // How far `s`'s extent runs past its advance: the pen origin plus each
        // glyph's pixel box at its pen position, the extent fonsTextBounds
        // measures minus its padding. `path` is what text_width() gets ("" =
        // default); pens come from it, so they carry fontstash's rounding and
        // kerning exactly.
        float ink_overhang(const std::string& path, float size, const std::string& s) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace lt
