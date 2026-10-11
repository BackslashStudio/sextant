#pragma once
// Internal header — the color cycle an unset `color` draws from. One per Axes,
// and one per Axes3D shared with its planes (hence held by shared_ptr). Touched
// only by caller-thread plotting calls, one at a time per figure graph.
#include "sextant/style.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace sextant {

// matplotlib's tab10; its first color is Color::Blue, the old fixed default.
std::vector<Color> default_color_cycle();

struct ColorCycle {
    std::vector<Color> colors = default_color_cycle();
    std::size_t next = 0;

    // Empty restores the default. Restarts from the first color either way.
    void set(std::vector<Color> c) {
        colors = c.empty() ? default_color_cycle() : std::move(c);
        next = 0;
    }

    // Fills an unset `color`. `advance` is false for an object drawn through a
    // colormap: it keeps the color a panel would switch it to, without using
    // one up. A set color is kept and never advances the cycle.
    void resolve(std::optional<Color>& color, bool advance = true) {
        if (color) return;
        color = colors[next % colors.size()];
        if (advance) next = (next + 1) % colors.size();
    }
};

} // namespace sextant
