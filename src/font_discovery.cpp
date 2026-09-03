#include "font_discovery.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>

namespace sextant {
namespace {

namespace fs = std::filesystem;

bool contains_ci(const std::string& s, const char* needle) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return lower.find(needle) != std::string::npos;
}

bool has_font_ext(const fs::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return ext == ".ttf" || ext == ".ttc" || ext == ".otf";
}

uint16_t read_u16be(const char* p) {
    return static_cast<uint16_t>((uint8_t(p[0]) << 8) | uint8_t(p[1]));
}
uint32_t read_u32be(const char* p) {
    return (uint32_t(uint8_t(p[0])) << 24) | (uint32_t(uint8_t(p[1])) << 16)
         | (uint32_t(uint8_t(p[2])) << 8)  |  uint32_t(uint8_t(p[3]));
}

struct RawFontInfo { std::string family, subfamily, path; };

// Pulls a single byte range out of an already-open font file, resizing `out`
// to however much was actually available. Fonts are read range-by-range
// rather than slurped whole: the parser below only ever needs the 16-byte
// header, the table directory, and the 'name' table (a few KB), so reading
// entire files would mean hundreds of MB of pointless copying across a system
// font directory (see read_font_names).
size_t read_at_most(std::ifstream& f, size_t pos, size_t len, std::vector<char>& out) {
    out.clear();
    if (len == 0) return 0;
    f.clear();
    f.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
    if (!f) return 0;
    out.resize(len);
    f.read(out.data(), static_cast<std::streamsize>(len));
    out.resize(static_cast<size_t>(f.gcount()));
    return out.size();
}

bool read_range(std::ifstream& f, size_t pos, size_t len, std::vector<char>& out) {
    return read_at_most(f, pos, len, out) == len;
}

// Reads nameID 1 (family) and 2 (subfamily) out of a TTF/TTC/OTF file's sfnt
// 'name' table -- just enough of the format to get a CSS-usable family string,
// since the filename often is not one ("times.ttf" -> "Times New Roman"). No
// FreeType dependency, so this works with SEXTANT_USE_FREETYPE=OFF too.
// Returns nullopt if the file does not parse as a recognizable sfnt/ttc.
std::optional<RawFontInfo> read_font_names(const fs::path& font_path) {
    std::ifstream f(font_path, std::ios::binary);
    if (!f) return std::nullopt;

    std::vector<char> head;
    if (!read_range(f, 0, 16, head)) return std::nullopt;

    size_t sfnt_offset = 0;
    if (read_u32be(head.data()) == 0x74746366u /* 'ttcf' */) {
        sfnt_offset = read_u32be(head.data() + 12); // first font in the collection
        if (!read_range(f, sfnt_offset, 12, head)) return std::nullopt;
    }

    const uint16_t num_tables = read_u16be(head.data() + 4);
    if (num_tables == 0) return std::nullopt;

    std::vector<char> dir;
    if (!read_range(f, sfnt_offset + 12, size_t(num_tables) * 16, dir)) return std::nullopt;

    uint32_t name_off = 0, name_len = 0;
    for (uint16_t i = 0; i < num_tables; ++i) {
        const char* rec = dir.data() + size_t(i) * 16;
        if (read_u32be(rec) == 0x6e616d65u /* 'name' */) {
            name_off = read_u32be(rec + 8);
            name_len = read_u32be(rec + 12);
            break;
        }
    }
    if (name_off == 0 || name_len < 6) return std::nullopt;

    // buf holds just the 'name' table, so every offset below — which the sfnt
    // format already expresses relative to the table start — indexes it directly.
    std::vector<char> buf;
    if (read_at_most(f, name_off, name_len, buf) < 6) return std::nullopt;

    const uint16_t count      = read_u16be(buf.data() + 2);
    const uint16_t string_off = read_u16be(buf.data() + 4);
    const size_t records = 6;

    // A few fonts under-report the 'name' table length, leaving the string
    // storage the records point at past the end of what we just read. Grow the
    // buffer to cover the furthest record before decoding anything, so this
    // range-based read resolves exactly the strings a whole-file read would.
    size_t needed = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const size_t rec = records + size_t(i) * 12;
        if (rec + 12 > buf.size()) break;
        needed = std::max<size_t>(needed, size_t(string_off)
                                        + read_u16be(buf.data() + rec + 10)   // offset
                                        + read_u16be(buf.data() + rec + 8));  // length
    }
    if (needed > buf.size() && read_at_most(f, name_off, needed, buf) < 6)
        return std::nullopt;

    auto decode = [&](size_t rec) -> std::string {
        const uint16_t platform_id = read_u16be(buf.data() + rec + 0);
        const uint16_t length      = read_u16be(buf.data() + rec + 8);
        const uint16_t offset      = read_u16be(buf.data() + rec + 10);
        const size_t str_pos = size_t(string_off) + offset;
        if (str_pos + length > buf.size()) return {};
        const char* s = buf.data() + str_pos;

        if (platform_id == 1) return std::string(s, length); // Macintosh: ~ASCII already

        // Windows (3) / Unicode (0) platforms store UTF-16BE; non-ASCII code
        // points are rare in font family names, so a lossy ASCII-only
        // decode is good enough for a display/CSS name.
        std::string out;
        for (size_t i = 0; i + 1 < size_t(length); i += 2) {
            uint16_t cp = read_u16be(s + i);
            out += (cp < 0x80) ? char(cp) : '?';
        }
        return out;
    };

    RawFontInfo info;
    info.path = font_path.string();
    for (uint16_t pass = 0; pass < 3 && (info.family.empty() || info.subfamily.empty()); ++pass) {
        for (uint16_t i = 0; i < count; ++i) {
            const size_t rec = records + size_t(i) * 12;
            if (rec + 12 > buf.size()) break;
            const uint16_t platform_id = read_u16be(buf.data() + rec + 0);
            const uint16_t language_id = read_u16be(buf.data() + rec + 4);
            const uint16_t name_id     = read_u16be(buf.data() + rec + 6);
            const bool platform_match = (pass == 0 && platform_id == 3 && language_id == 0x0409)
                                      || (pass == 1 && platform_id == 3)
                                      || (pass == 2 && platform_id == 1);
            if (!platform_match) continue;
            if (name_id == 1 && info.family.empty())    info.family    = decode(rec);
            if (name_id == 2 && info.subfamily.empty()) info.subfamily = decode(rec);
        }
    }
    if (info.family.empty()) return std::nullopt;
    return info;
}

void scan_dir(const fs::path& dir, bool recursive, std::vector<RawFontInfo>& out) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) return;

    auto visit = [&](const fs::directory_entry& entry) {
        std::error_code file_ec;
        if (!entry.is_regular_file(file_ec) || file_ec || !has_font_ext(entry.path())) return;
        if (auto info = read_font_names(entry.path()))
            out.push_back(std::move(*info));
        else
            out.push_back({entry.path().stem().string(), "", entry.path().string()});
    };

    if (recursive) {
        fs::recursive_directory_iterator it(
            dir, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec))
            visit(*it);
    } else {
        fs::directory_iterator it(
            dir, fs::directory_options::skip_permission_denied, ec);
        fs::directory_iterator end;
        for (; !ec && it != end; it.increment(ec))
            visit(*it);
    }
}

std::vector<FontEntry> scan_all() {
    std::vector<RawFontInfo> raw;
#if defined(_WIN32)
    scan_dir("C:/Windows/Fonts", false, raw);
#elif defined(__APPLE__)
    scan_dir("/System/Library/Fonts", true, raw);
    scan_dir("/Library/Fonts", true, raw);
    if (const char* home = std::getenv("HOME"))
        scan_dir(fs::path(home) / "Library/Fonts", true, raw);
#else
    scan_dir("/usr/share/fonts", true, raw);
    scan_dir("/usr/local/share/fonts", true, raw);
    if (const char* home = std::getenv("HOME"))
        scan_dir(fs::path(home) / ".fonts", true, raw);
#endif

    std::sort(raw.begin(), raw.end(),
              [](const RawFontInfo& a, const RawFontInfo& b) { return a.family < b.family; });

    // Multiple style-weight files (e.g. times.ttf/timesbd.ttf/timesbi.ttf/
    // timesi.ttf) usually share one family name in their own 'name' table —
    // collapse each family down to a single Font combo entry, preferring
    // whichever file reports "Regular" so a family selection never silently
    // lands on a bold/italic weight.
    std::vector<FontEntry> found;
    for (size_t i = 0; i < raw.size(); ) {
        size_t j = i;
        const RawFontInfo* best = &raw[i];
        while (j < raw.size() && raw[j].family == raw[i].family) {
            if (contains_ci(raw[j].subfamily, "regular")) best = &raw[j];
            ++j;
        }
        found.push_back({best->family, best->path});
        i = j;
    }
    return found;
}

} // namespace

const std::vector<FontEntry>& discover_system_fonts() {
    static std::vector<FontEntry> cached = scan_all();
    return cached;
}

const FontEntry* pick_default_font() {
    // Memoized, because the answer cannot change and this is on hot paths:
    // layout calls it for every string it measures and the SVG writer for
    // every text element it emits, and each call would otherwise be a
    // case-insensitive substring search across ~130 discovered families.
    static const FontEntry* cached = [] () -> const FontEntry* {
        const auto& fonts = discover_system_fonts();
        for (const auto& f : fonts) {
            if (contains_ci(f.name, "times")) return &f;
        }
        return fonts.empty() ? nullptr : &fonts.front();
    }();
    return cached;
}

} // namespace sextant
