// Text measurement against fontstash itself. Part of sextant_layout_test; see
// layout_test.h.
#include "layout_test.h"

#include "glyph_boxes.h"

namespace lt {
    // text_metrics.cpp must reproduce fontstash's advance arithmetic exactly: every
    // string is measured both ways and must agree to the last bit (a tolerance
    // would hide quantization mistakes). Compared at devicePxRatio = 1.

    const std::vector<std::string>& sample_strings() {
        // Tick labels, titles and legend entries, plus adversarial cases: a lone
        // glyph, repeated glyphs (rounding accumulates), a kerned pair, non-ASCII.
        static const std::vector<std::string> s = {
            "0", "-1", "0.5", "1.0", "-0.25", "12345", "1e+08", "-1.5e-08",
            "100000", "0.000001", "2.5", "-273.15",
            "x", "Time (s)", "Amplitude", "sin(x)", "A Title With Spaces",
            "AV", "AVATAR", "WWWWWWWWWW", "iiiiiiiiii", "....", "|",
            "\xC2\xB5m", // "µm"
            "\xE2\x88\x92" "1.0", // U+2212 MINUS SIGN, then "1.0"
            "\xF0\x9F\x93\x88", // U+1F4C8, a codepoint the font will lack
        };
        return s;
    }

    // The library's default sizes, the panel's extremes, and fractional sizes that
    // fontstash's 0.1 px quantization changes.
    const std::vector<float>& sample_sizes() {
        static const std::vector<float> s = {
            1.0f, 2.0f, 5.0f, 8.0f, 10.0f, 11.0f, 12.0f, 16.5f, 18.0f, 21.0f,
            24.0f, 28.0f, 30.0f, 40.0f, 64.0f, 96.0f,
            10.04f, 11.06f, 13.999f, 17.25f,
        };
        return s;
    }

    // One font's comparison. `nvg_font` must exist in vg; `path` is what
    // text_width() gets ("" = default).
    void compare_font(NVGcontext* vg, int nvg_font, const std::string& path,
                      const char* label) {
        std::printf("[%s]\n", label);

        const GlyphBoxes glyphs(path.empty() ? sextant::pick_default_font()->path : path);

        check(sextant::text_metrics_font_loaded(path),
              std::string(label) + ": real glyph metrics in use (not the fallback estimate)");

        int worst_count = 0;
        float worst_delta = 0.0f;
        std::string worst_case;

        // Split by size (see the assertion below). "Excess" is NanoVG's ink
        // past the advance minus the font's own.
        float worst_excess_small = 0.0f, worst_excess_large = 0.0f;
        float worst_font_overhang = 0.0f;
        std::string worst_excess_case;

        for (float size: sample_sizes()) {
            nvgFontFaceId(vg, nvg_font);
            nvgFontSize(vg, size);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

            for (const auto& s: sample_strings()) {
                // nvgTextBounds' return value is the advance (what text_width()
                // computes); bounds[2]-bounds[0] is the ink extent.
                const float nvg_adv = nvgTextBounds(vg, 0.0f, 0.0f, s.c_str(), nullptr, nullptr);
                const float own_adv = sextant::text_width(path, size, s);
                const float delta = std::fabs(nvg_adv - own_adv);

                if (delta != 0.0f) {
                    ++worst_count;
                    if (delta > worst_delta) {
                        worst_delta = delta;
                        char buf[256];
                        std::snprintf(buf, sizeof(buf), "\"%s\" @ %.3g px: nvg %.4f vs own %.4f",
                                      s.c_str(), static_cast<double>(size),
                                      static_cast<double>(nvg_adv), static_cast<double>(own_adv));
                        worst_case = buf;
                    }
                }

                // Track how far NanoVG's ink runs past the advance beyond the
                // font's own overhang: up to ~3 px from its quad inset. Below
                // ~8 px glyphs overlap, so the bound is only asserted at layout
                // sizes.
                if (size >= 8.0f) {
                    float bounds[4] = {0, 0, 0, 0};
                    nvgTextBounds(vg, 0.0f, 0.0f, s.c_str(), nullptr, bounds);
                    const float font_overhang = glyphs.ink_overhang(path, size, s);
                    worst_font_overhang = std::max(worst_font_overhang, font_overhang);
                    const float excess = (bounds[2] - bounds[0]) - own_adv - font_overhang;
                    float& worst = (size >= 32.0f) ? worst_excess_large : worst_excess_small;
                    if (excess > worst) {
                        worst = excess;
                        char buf[256];
                        std::snprintf(buf, sizeof(buf), "\"%s\" @ %.3g px, font's own %.0f px",
                                      s.c_str(), static_cast<double>(size),
                                      static_cast<double>(font_overhang));
                        worst_excess_case = buf;
                    }
                }
            }
        }

        const int total = static_cast<int>(sample_sizes().size() * sample_strings().size());
        check(worst_count == 0,
              std::string(label) + ": " + std::to_string(worst_count) + "/" +
              std::to_string(total) + " advances differ (worst: " +
              (worst_case.empty() ? "none" : worst_case) + ")");
        std::printf("  %d string x size combinations, %d mismatched\n", total, worst_count);

        // What NanoVG adds is a constant ~3 px at both size ranges (padding; a
        // measuring error would scale with size). The font's own overhang is
        // taken out first: it is real ink, and some faces have plenty.
        check(glyphs.ok(), std::string(label) + ": glyph boxes readable for the overhang check");
        check(worst_excess_small <= 3.0f && worst_excess_large <= 3.0f,
              std::string(label) + ": ink past the advance beyond the font's own — 8-30 px: " +
              std::to_string(worst_excess_small) + ", >=32 px: " +
              std::to_string(worst_excess_large) + " (worst on " +
              (worst_excess_case.empty() ? "none" : worst_excess_case) + ")");
        std::printf("  ink past advance beyond the font's own: %.3f px at 8-30, %.3f px at >=32"
                    " (font's own up to %.0f px)\n",
                    static_cast<double>(worst_excess_small),
                    static_cast<double>(worst_excess_large),
                    static_cast<double>(worst_font_overhang));

        // font_vmetrics() must reproduce nvgTextMetrics().
        for (float size: sample_sizes()) {
            nvgFontFaceId(vg, nvg_font);
            nvgFontSize(vg, size);
            float asc = 0.0f, desc = 0.0f, lineh = 0.0f;
            nvgTextMetrics(vg, &asc, &desc, &lineh);

            const auto vm = sextant::font_vmetrics(path, size);
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "%s: vmetrics @ %.3g px — nvg(%.4f,%.4f,%.4f) own(%.4f,%.4f,%.4f)",
                          label, static_cast<double>(size),
                          static_cast<double>(asc), static_cast<double>(desc), static_cast<double>(lineh),
                          static_cast<double>(vm.ascent), static_cast<double>(vm.descent),
                          static_cast<double>(vm.line_height));
            check(asc == vm.ascent && desc == vm.descent && lineh == vm.line_height, buf);
        }
    }

    void test_text_metrics() {
        // Headless, but NanoVG still needs a GL context.
        sextant::GLContext ctx({
            .width = 400, .height = 300,
            .title = "layout_test", .visible = false
        });
        NVGcontext* vg = ctx.nvg();
        if (!vg) {
            std::printf("FATAL: no NanoVG context\n");
            ++g_failures;
            return;
        }

        const sextant::FontEntry* def = sextant::pick_default_font();
        if (!def) {
            std::printf("FATAL: no system font discovered — nothing to compare\n");
            ++g_failures;
            return;
        }
        std::printf("default font: %s (%s)\n\n", def->name.c_str(), def->path.c_str());

        // "" (the default font) must match the resolved path passed explicitly;
        // a different face is checked too.
        const int font_default = nvgCreateFont(vg, "default", def->path.c_str());
        check(font_default != -1, "default font loaded into NanoVG");
        if (font_default != -1) compare_font(vg, font_default, "", "default font (\"\")");

        // A second, explicitly named face (the font_path branch).
        const auto& fonts = sextant::discover_system_fonts();
        const sextant::FontEntry* other = nullptr;
        for (const auto& f: fonts)
            if (f.path != def->path) {
                other = &f;
                break;
            }

        if (other) {
            const int font_other = nvgCreateFont(vg, other->path.c_str(), other->path.c_str());
            check(font_other != -1, "second font loaded into NanoVG");
            if (font_other != -1)
                compare_font(vg, font_other, other->path, other->name.c_str());
        } else {
            std::printf("(only one font on this system — explicit-path case skipped)\n");
        }
    }

    // An unloadable font path falls back to a per-character estimate.
    void test_missing_font_fallback() {
        std::printf("\n[missing font]\n");
        const std::string bogus = "D:/this/font/does/not/exist.ttf";

        check(!sextant::text_metrics_font_loaded(bogus), "bogus path reports no glyph metrics");

        const float w1 = sextant::text_width(bogus, 12.0f, "12345");
        const float w2 = sextant::text_width(bogus, 12.0f, "1234567890");
        check(w1 > 0.0f, "fallback width is positive");
        check(w2 > w1, "fallback width grows with the string");
        check(sextant::text_width(bogus, 12.0f, "") == 0.0f, "empty string measures 0");

        const auto vm = sextant::font_vmetrics(bogus, 12.0f);
        check(vm.line_height > 0.0f && vm.ascent > 0.0f && vm.descent < 0.0f,
              "fallback vmetrics are sane");
        std::printf("  fallback: \"12345\" @ 12 px = %.2f px, line height %.2f\n",
                    static_cast<double>(w1), static_cast<double>(vm.line_height));
    }

    // The caches are shared across threads (render thread and savefig() callers):
    // hammer them concurrently and compare with single-threaded answers.
    void test_concurrent_measurement() {
        std::printf("\n[concurrency]\n");

        std::vector<float> expected;
        for (const auto& s: sample_strings())
            for (float size: sample_sizes())
                expected.push_back(sextant::text_width("", size, s));

        std::atomic<int> mismatches{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&] {
                for (int rep = 0; rep < 20; ++rep) {
                    std::size_t i = 0;
                    for (const auto& s: sample_strings())
                        for (float size: sample_sizes())
                            if (sextant::text_width("", size, s) != expected[i++])
                                ++mismatches;
                }
            });
        }
        for (auto& th: threads) th.join();

        check(mismatches.load() == 0,
              "8 threads x 20 passes agree with the single-threaded widths (" +
              std::to_string(mismatches.load()) + " mismatches)");
        std::printf("  %zu widths x 8 threads x 20 passes, %d mismatches\n",
                    expected.size(), mismatches.load());
    }
} // namespace lt
