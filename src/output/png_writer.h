#pragma once
#include <span>
#include <string_view>
#include <cstdint>
#include <vector>

namespace sextant {

// Write an RGBA pixel buffer to a PNG file.
// Uses libpng when SEXTANT_USE_LIBPNG is defined, otherwise stb_image_write.
void write_png(std::string_view path, int width, int height,
               std::span<const uint8_t> rgba_pixels);

// Same encoding, returned as an in-memory PNG byte buffer instead of
// written to a file — used to embed heatmaps as base64 data: URIs in SVG
// output (SVG has no native raster-image primitive of its own).
std::vector<uint8_t> write_png_to_memory(int width, int height,
                                         std::span<const uint8_t> rgba_pixels);

} // namespace sextant
