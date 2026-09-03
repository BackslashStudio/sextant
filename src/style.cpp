#include "sextant/style.h"
#include <stdexcept>
#include <string>
#include <algorithm>
#include <charconv>

namespace sextant {

const Color Color::Blue   = {0.122f, 0.467f, 0.706f, 1.0f};
const Color Color::Red    = {0.839f, 0.153f, 0.157f, 1.0f};
const Color Color::Green  = {0.173f, 0.627f, 0.173f, 1.0f};
const Color Color::Orange = {1.000f, 0.498f, 0.055f, 1.0f};
const Color Color::Purple = {0.580f, 0.404f, 0.741f, 1.0f};
const Color Color::Cyan   = {0.090f, 0.745f, 0.812f, 1.0f};
const Color Color::Black  = {0.000f, 0.000f, 0.000f, 1.0f};
const Color Color::White  = {1.000f, 1.000f, 1.000f, 1.0f};
const Color Color::Gray   = {0.500f, 0.500f, 0.500f, 1.0f};

Color Color::from_hex(uint32_t hex) {
    const bool has_alpha = hex > 0xFFFFFF;
    const uint8_t r = (hex >> (has_alpha ? 24 : 16)) & 0xFF;
    const uint8_t g = (hex >> (has_alpha ?  8 :  8)) & 0xFF;
    const uint8_t b = (hex >> (has_alpha ? 16 :  0)) & 0xFF;
    const uint8_t a = has_alpha ? (hex & 0xFF) : 0xFF;
    return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

Color Color::from_name(std::string_view name) {
    if (name.starts_with('#')) {
        std::string s(name.substr(1));
        uint32_t hex = 0;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), hex, 16);
        if (ec != std::errc{})
            throw std::invalid_argument("invalid hex color: " + std::string(name));
        return from_hex(hex);
    }
    // Basic named colors
    if (name == "red")    return Red;
    if (name == "blue")   return Blue;
    if (name == "green")  return Green;
    if (name == "orange") return Orange;
    if (name == "purple") return Purple;
    if (name == "cyan")   return Cyan;
    if (name == "black")  return Black;
    if (name == "white")  return White;
    if (name == "gray" || name == "grey") return Gray;
    throw std::invalid_argument("unknown color name: " + std::string(name));
}

} // namespace sextant
