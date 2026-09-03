#pragma once
#include <string>
#include <vector>

namespace sextant {

struct FontEntry {
    std::string name;  // display name, derived from filename stem
    std::string path;  // absolute path to the font file
};

// Scans OS-standard font directories once (lazily cached) and returns the
// sorted, de-duplicated list of discovered .ttf/.ttc/.otf files.
const std::vector<FontEntry>& discover_system_fonts();

// Picks the font used whenever AxesStyle::font_path is unset ("" = default):
// prefers "Times New Roman" among discover_system_fonts()'s results, else
// falls back to the first discovered entry. Returns nullptr if no fonts were
// found at all. Shared by NvgRenderer's live default and SvgWriter's
// font-family fallback, so headless SVG output never diverges from what the
// on-screen renderer actually shows.
const FontEntry* pick_default_font();

} // namespace sextant
