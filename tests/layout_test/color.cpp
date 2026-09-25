// Color construction. Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"

#include <cmath>

namespace lt {
    void test_color_from_hex() {
        std::printf("\n[Color::from_hex]\n");

        auto byte_is = [](float c, int b) { return std::fabs(c - b / 255.0f) < 1e-6f; };
        auto is = [&](const sextant::Color& c, int r, int g, int b, int a) {
            return byte_is(c.r, r) && byte_is(c.g, g) && byte_is(c.b, b) && byte_is(c.a, a);
        };

        check(is(sextant::Color::from_hex(0x112233), 0x11, 0x22, 0x33, 0xFF),
              "0xRRGGBB: channels in order, opaque");
        check(is(sextant::Color::from_hex(0x11223344), 0x11, 0x22, 0x33, 0x44),
              "0xRRGGBBAA: green and blue are not swapped, alpha last");
        check(is(sextant::Color::from_hex(0xFF000080), 0xFF, 0x00, 0x00, 0x80),
              "0xRRGGBBAA: half-transparent red");
        check(is(sextant::Color::from_hex(0x00FF00), 0x00, 0xFF, 0x00, 0xFF),
              "0xRRGGBB: pure green");
    }
} // namespace lt
