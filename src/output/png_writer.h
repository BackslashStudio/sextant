#pragma once
#include <span>
#include <string_view>
#include <cstdint>
#include <vector>

namespace sextant {
    // Encode RGBA pixels (top row first) as PNG in memory (libpng if
    // SEXTANT_USE_LIBPNG, else stb). Also embeds heatmaps in SVG as data: URIs.
    std::vector<uint8_t> write_png_to_memory(int width, int height,
                                             std::span<const uint8_t> rgba_pixels);

    // write_png_to_memory() written to a file, byte for byte.
    void write_png(std::string_view path, int width, int height,
                   std::span<const uint8_t> rgba_pixels);
} // namespace sextant
