#pragma once
#include <string>
#include <vector>

namespace sextant {
    struct FontEntry {
        std::string name; // display name, derived from filename stem
        std::string path; // absolute path to the font file
        // The family's italic (or oblique) face, "" when it has none, and its
        // face index inside a .ttc (macOS's Times.ttc holds all four styles).
        // For math letters.
        std::string italic_path;
        int italic_index = 0;
    };

    // Scans OS font directories once (cached): sorted, de-duplicated .ttf/.ttc/.otf.
    const std::vector<FontEntry>& discover_system_fonts();

    // The discovered family whose file is `font_path` ("" = pick_default_font()),
    // compared as paths; nullptr for a file discovery did not list.
    const FontEntry* find_font_entry(const std::string& font_path);

    // The default font when font_path is "": "Times New Roman" if found, else the
    // first discovered font, else nullptr. Shared by NvgRenderer and the SVG writer.
    const FontEntry* pick_default_font();

    // Fonts for the glyphs a string's own font lacks (math symbols, Greek), in
    // order: the first discovered of a per-platform list of symbol and math
    // families. NanoVG registers them as fallbacks of every font
    // it loads, and text_width() follows the same lookup. May be empty.
    const std::vector<const FontEntry*>& fallback_fonts();
} // namespace sextant
