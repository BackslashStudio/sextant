#pragma once
#include <string>
#include <vector>

namespace sextant {
    struct FontEntry {
        std::string name; // display name, derived from filename stem
        std::string path; // absolute path to the font file
    };

    // Scans OS font directories once (cached): sorted, de-duplicated .ttf/.ttc/.otf.
    const std::vector<FontEntry>& discover_system_fonts();

    // The default font when font_path is "": "Times New Roman" if found, else the
    // first discovered font, else nullptr. Shared by NvgRenderer and the SVG writer.
    const FontEntry* pick_default_font();

    // Fonts for the glyphs a string's own font lacks (math symbols, Greek), in
    // order: the first discovered of a per-platform list of symbol and math
    // families (v1.1 step 32a). NanoVG registers them as fallbacks of every font
    // it loads, and text_width() follows the same lookup. May be empty.
    const std::vector<const FontEntry*>& fallback_fonts();
} // namespace sextant
