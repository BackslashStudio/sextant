#pragma once
#include <span>
#include <string_view>
#include <cstdint>
#include <vector>

namespace sextant {
    // Write RGBA pixels to a PNG file (libpng if SEXTANT_USE_LIBPNG, else stb).
    void write_png(std::string_view path, int width, int height,
                   std::span<const uint8_t> rgba_pixels);

    // Same, to memory (used to embed heatmaps in SVG as data: URIs).
    std::vector<uint8_t> write_png_to_memory(int width, int height,
                                             std::span<const uint8_t> rgba_pixels);
} // namespace sextant
