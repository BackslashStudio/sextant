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

        // from_name: the string's length says whether there is alpha (v1.1 step 28).
        using sextant::Color;
        check(is(Color::from_name("#112233"), 0x11, 0x22, 0x33, 0xFF), "#rrggbb");
        check(is(Color::from_name("#11223344"), 0x11, 0x22, 0x33, 0x44), "#rrggbbaa");
        check(is(Color::from_name("#000000ff"), 0x00, 0x00, 0x00, 0xFF),
              "#rrggbbaa with red 0: opaque black, not blue");
        check(is(Color::from_name("#0000ff80"), 0x00, 0x00, 0xFF, 0x80),
              "#rrggbbaa with red 0: half-transparent blue");
        check(is(Color::from_name("#00FF00"), 0x00, 0xFF, 0x00, 0xFF), "upper-case digits");
        const Color grey = Color::from_name("grey");
        check(grey.r == Color::Gray.r && grey.g == Color::Gray.g && grey.b == Color::Gray.b,
              "a name");

        auto throws = [](std::string_view s) {
            try { (void) Color::from_name(s); } catch (const std::invalid_argument&) { return true; }
            return false;
        };
        for (std::string_view bad : {"#12zz56", "#1234", "#1234567", "#", "#123456789", "#0x1234",
                                     "#-12345", "teal", ""})
            check(throws(bad), "from_name(\"" + std::string(bad) + "\") throws");
    }
} // namespace lt
