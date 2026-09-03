#include "png_writer.h"
#include <stdexcept>
#include <system_error>
#include <string>
#include <cerrno>

#ifdef SEXTANT_USE_LIBPNG
// -------------------------------------------------------------------------
// libpng path
// -------------------------------------------------------------------------
#include <png.h>
#include <cstdio>

namespace sextant {

void write_png(std::string_view path, int width, int height,
               std::span<const uint8_t> rgba_pixels)
{
    const std::string path_str(path);
    FILE* fp = fopen(path_str.c_str(), "wb");
    if (!fp)
        throw std::system_error(errno, std::system_category(),
                                "write_png: cannot open '" + path_str + "'");

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        throw std::runtime_error("write_png: png_create_write_struct failed");
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        throw std::runtime_error("write_png: png_create_info_struct failed");
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        throw std::runtime_error("write_png: libpng encode error");
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info,
                 static_cast<png_uint_32>(width),
                 static_cast<png_uint_32>(height),
                 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    // rgba_pixels is already top-down (FboReadback::read_pixels flips it).
    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y)
        png_write_row(png,
            const_cast<png_bytep>(rgba_pixels.data() + static_cast<std::size_t>(y) * stride));

    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
}

std::vector<uint8_t> write_png_to_memory(int width, int height,
                                         std::span<const uint8_t> rgba_pixels)
{
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              nullptr, nullptr, nullptr);
    if (!png)
        throw std::runtime_error("write_png_to_memory: png_create_write_struct failed");

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        throw std::runtime_error("write_png_to_memory: png_create_info_struct failed");
    }

    std::vector<uint8_t> out;
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        throw std::runtime_error("write_png_to_memory: libpng encode error");
    }

    png_set_write_fn(png, &out,
        [](png_structp p, png_bytep data, png_size_t len) {
            auto* buf = static_cast<std::vector<uint8_t>*>(png_get_io_ptr(p));
            buf->insert(buf->end(), data, data + len);
        },
        nullptr);

    png_set_IHDR(png, info,
                 static_cast<png_uint_32>(width),
                 static_cast<png_uint_32>(height),
                 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y)
        png_write_row(png,
            const_cast<png_bytep>(rgba_pixels.data() + static_cast<std::size_t>(y) * stride));

    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    return out;
}

} // namespace sextant

#else
// -------------------------------------------------------------------------
// stb_image_write fallback — always available
// -------------------------------------------------------------------------
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace sextant {

void write_png(std::string_view path, int width, int height,
               std::span<const uint8_t> rgba_pixels)
{
    const std::string path_str(path);
    const int stride = width * 4;
    const int ok = stbi_write_png(path_str.c_str(), width, height,
                                  4, rgba_pixels.data(), stride);
    if (!ok)
        throw std::system_error(errno, std::system_category(),
                                "write_png: stbi_write_png failed for '" + path_str + "'");
}

std::vector<uint8_t> write_png_to_memory(int width, int height,
                                         std::span<const uint8_t> rgba_pixels)
{
    std::vector<uint8_t> out;
    const int stride = width * 4;
    const int ok = stbi_write_png_to_func(
        [](void* context, void* data, int size) {
            auto* buf = static_cast<std::vector<uint8_t>*>(context);
            const auto* bytes = static_cast<const uint8_t*>(data);
            buf->insert(buf->end(), bytes, bytes + size);
        },
        &out, width, height, 4, rgba_pixels.data(), stride);
    if (!ok)
        throw std::runtime_error("write_png_to_memory: stbi_write_png_to_func failed");
    return out;
}

} // namespace sextant

#endif
