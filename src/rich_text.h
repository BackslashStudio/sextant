#pragma once
#include "text_metrics.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sextant {

// Rich text: a subset of matplotlib's mathtext. A string with
// an even, nonzero count of `$` not preceded by a backslash holds math spans;
// inside one, `^`/`_` scripts, `{}` groups, `\name` symbols and TeX spacing.
// Letters in a span are italic, as TeX's: Latin and lower-case
// Greek; digits, symbols, Greek capitals, function names, `\mathrm`/`\text`
// upright; `\mathit` makes letters and digits italic.
// Outside the spans, `\$` is a dollar. Any other string is plain and is
// measured and drawn exactly as before (text_width(), nvgText()). Malformed
// math never throws: the string draws as written. Measuring needs no GL
// context, like text_metrics.h.

// One piece of a laid-out line, drawn by one nvgText() / one <tspan>.
struct RichRun {
    std::string text;      // UTF-8
    float x = 0.0f;        // pen position, from the line's left end
    float dy = 0.0f;       // baseline shift, y down (a superscript's is negative)
    float size = 0.0f;     // font size, px
    float width = 0.0f;    // advance of `text` at `size`
    // A math letter: the family's italic face, or the upright one
    // slanted when it has none; measured by text_width_italic().
    bool italic = false;
};

// The slant of an italic run drawn from an upright face: horizontal shift per
// unit of height (FreeType's oblique, about 11 degrees).
constexpr float kSyntheticSlant = 0.2f;

struct RichLine {
    std::vector<RichRun> runs;   // in pen order
    float width = 0.0f;
    // Over every run, and never inside the plain font's: line_height = ascent - descent.
    FontVMetrics vm;
};

// matplotlib's test: an even, nonzero count of `$`, less the `\$`s.
bool has_math(std::string_view s);

struct MathError {
    std::size_t column = 0;   // 1-based, in code points
    std::string reason;
};

// Why the math in `s` does not parse; nullopt when it parses or has none.
std::optional<MathError> math_error(std::string_view s);

// Whether `s` draws as rich text: math is on for this thread (MathTextScope)
// and `s` has math that parses.
bool is_rich(std::string_view s);

// `s` laid out at `size`, left end at 0 and baseline at 0. A plain string is
// one run measured by text_width() at font_vmetrics().
RichLine layout_rich(const std::string& font_path, float size, std::string_view s);

// A label's width and vertical extent: text_width() / font_vmetrics() unless
// is_rich(s), so plain text measures exactly as before.
float        label_width(const std::string& font_path, float size, std::string_view s);
FontVMetrics label_vmetrics(const std::string& font_path, float size, std::string_view s);

// FigureOptions::mathtext for every measure and draw on this thread while it
// lives. The figure-level entry points (layout, render, export) open one;
// scopes nest, and with none open math is on.
// Whether math is on for this thread (the innermost MathTextScope's).
bool mathtext_on();

class MathTextScope {
public:
    explicit MathTextScope(bool on);
    ~MathTextScope();
    MathTextScope(const MathTextScope&) = delete;
    MathTextScope& operator=(const MathTextScope&) = delete;
private:
    bool prev_;
};

// For a string set through the API: one emit_message() when math is on and
// the math in `s` does not parse ("<who>: math not parsed at column N: ...;
// drawn as written"). Each line of `s` is judged on its own, as it is drawn.
// The caller must hold no sextant lock.
void warn_math(bool mathtext, std::string_view who, std::string_view s);

} // namespace sextant
