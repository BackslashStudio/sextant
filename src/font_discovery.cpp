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

struct RawFontInfo { std::string family, subfamily, path; int index = 0; };

// Reads one byte range of an open font file (fonts are read in ranges, not
// whole; only the header, directory and 'name' table are needed).
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

// Family (nameID 1) and subfamily (2) from an sfnt 'name' table, since
// filenames often aren't family names ("times.ttf"), of face `index` (a .ttc
// holds several). No FreeType needed. nullopt if the file doesn't parse.
std::optional<RawFontInfo> read_font_names(const fs::path& font_path, int index = 0) {
    std::ifstream f(font_path, std::ios::binary);
    if (!f) return std::nullopt;

    std::vector<char> head;
    if (!read_range(f, 0, 16, head)) return std::nullopt;

    size_t sfnt_offset = 0;
    if (read_u32be(head.data()) == 0x74746366u /* 'ttcf' */) {
        std::vector<char> off;
        if (index < 0 || static_cast<uint32_t>(index) >= read_u32be(head.data() + 8)
            || !read_range(f, 12 + size_t(index) * 4, 4, off))
            return std::nullopt;
        sfnt_offset = read_u32be(off.data());
        if (!read_range(f, sfnt_offset, 12, head)) return std::nullopt;
    } else if (index != 0) {
        return std::nullopt;
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

    // Offsets are relative to the 'name' table start, which is buf[0].
    std::vector<char> buf;
    if (read_at_most(f, name_off, name_len, buf) < 6) return std::nullopt;

    const uint16_t count      = read_u16be(buf.data() + 2);
    const uint16_t string_off = read_u16be(buf.data() + 4);
    const size_t records = 6;

    // Some fonts under-report the table length; grow the buffer to cover the
    // furthest string record.
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

        // Windows/Unicode platforms store UTF-16BE; decoded lossily as ASCII.
        std::string out;
        for (size_t i = 0; i + 1 < size_t(length); i += 2) {
            uint16_t cp = read_u16be(s + i);
            out += (cp < 0x80) ? char(cp) : '?';
        }
        return out;
    };

    RawFontInfo info;
    info.path = font_path.string();
    info.index = index;
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

bool is_ttc(const fs::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return ext == ".ttc";
}

// Each file's first face into `out`; a collection's other faces into `more`,
// which only supply italic faces (an entry's path loads face 0).
void scan_dir(const fs::path& dir, bool recursive, std::vector<RawFontInfo>& out,
              std::vector<RawFontInfo>& more) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) return;

    auto visit = [&](const fs::directory_entry& entry) {
        std::error_code file_ec;
        if (!entry.is_regular_file(file_ec) || file_ec || !has_font_ext(entry.path())) return;
        if (auto info = read_font_names(entry.path()))
            out.push_back(std::move(*info));
        else
            out.push_back({entry.path().stem().string(), "", entry.path().string()});
        if (is_ttc(entry.path()))
            for (int k = 1; k < 64; ++k) {
                auto face = read_font_names(entry.path(), k);
                if (!face) break;
                more.push_back(std::move(*face));
            }
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

// A plain italic face: "Italic" or "Oblique", alone or after "Regular"/"Book";
// a bold or light italic is not the regular face's.
bool is_plain_italic(const std::string& subfamily) {
    std::string s = subfamily;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    for (const char* name : {"italic", "oblique", "regular italic", "regular oblique", "book italic",
                             "book oblique"})
        if (s == name) return true;
    return false;
}

std::vector<FontEntry> scan_all() {
    std::vector<RawFontInfo> raw, more;
#if defined(_WIN32)
    scan_dir("C:/Windows/Fonts", false, raw, more);
#elif defined(__APPLE__)
    scan_dir("/System/Library/Fonts", true, raw, more);
    scan_dir("/Library/Fonts", true, raw, more);
    if (const char* home = std::getenv("HOME"))
        scan_dir(fs::path(home) / "Library/Fonts", true, raw, more);
#else
    scan_dir("/usr/share/fonts", true, raw, more);
    scan_dir("/usr/local/share/fonts", true, raw, more);
    if (const char* home = std::getenv("HOME"))
        scan_dir(fs::path(home) / ".fonts", true, raw, more);
#endif

    std::sort(raw.begin(), raw.end(),
              [](const RawFontInfo& a, const RawFontInfo& b) { return a.family < b.family; });

    // Collapse each family to one entry, preferring the "Regular" file.
    std::vector<FontEntry> found;
    for (size_t i = 0; i < raw.size(); ) {
        size_t j = i;
        const RawFontInfo* best = &raw[i];
        while (j < raw.size() && raw[j].family == raw[i].family) {
            // "Book" is DejaVu's name for its regular face.
            if (contains_ci(raw[j].subfamily, "regular") || contains_ci(raw[j].subfamily, "book"))
                best = &raw[j];
            ++j;
        }
        found.push_back({best->family, best->path});
        i = j;
    }

    // Each family's italic face: the first plain one in path order, so the
    // pick does not depend on the directory listing's order.
    std::vector<const RawFontInfo*> italics;
    for (const auto* list : {&raw, &more})
        for (const RawFontInfo& r : *list)
            if (is_plain_italic(r.subfamily)) italics.push_back(&r);
    std::sort(italics.begin(), italics.end(), [](const RawFontInfo* a, const RawFontInfo* b) {
        return a->path != b->path ? a->path < b->path : a->index < b->index;
    });
    for (FontEntry& e : found)
        for (const RawFontInfo* r : italics)
            if (r->family == e.name) {
                e.italic_path = r->path;
                e.italic_index = r->index;
                break;
            }
    return found;
}

} // namespace

const std::vector<FontEntry>& discover_system_fonts() {
    static std::vector<FontEntry> cached = scan_all();
    return cached;
}

const FontEntry* find_font_entry(const std::string& font_path) {
    if (font_path.empty()) return pick_default_font();
    const fs::path wanted(font_path);
    for (const auto& f : discover_system_fonts())
        if (fs::path(f.path) == wanted) return &f;
    return nullptr;
}

const FontEntry* pick_default_font() {
    // Memoized; called for every measured string and every SVG text element.
    static const FontEntry* cached = [] () -> const FontEntry* {
        const auto& fonts = discover_system_fonts();
        for (const auto& f : fonts) {
            if (contains_ci(f.name, "times")) return &f;
        }
        return fonts.empty() ? nullptr : &fonts.front();
    }();
    return cached;
}

const std::vector<const FontEntry*>& fallback_fonts() {
    static const std::vector<const FontEntry*> cached = [] {
        // Symbol and math coverage first, then broad text faces with Greek.
#if defined(_WIN32)
        const char* wanted[] = {"Segoe UI Symbol", "DejaVu Sans", "Segoe UI"};
#elif defined(__APPLE__)
        const char* wanted[] = {"STIX Two Math", "Apple Symbols", "STIXGeneral", "DejaVu Sans"};
#else
        const char* wanted[] = {"DejaVu Sans", "Noto Sans Math", "Noto Sans Symbols", "FreeSerif"};
#endif
        std::vector<const FontEntry*> out;
        for (const char* name : wanted)
            for (const auto& f : discover_system_fonts())
                if (f.name == name) {
                    out.push_back(&f);
                    break;
                }
        return out;
    }();
    return cached;
}

} // namespace sextant
