#pragma once
#include "sextant/style.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sextant::colormaps {

// Returns a pointer to 256*4 bytes (RGBA, each in [0,255]), entry 0 at vmin;
// already reversed for a reversed map. Valid for the rest of the process.
const uint8_t* get(Colormap cmap);

// Built-ins take ids 0 .. builtin_count() - 1; registered maps follow.
std::size_t builtin_count();

// The built-ins' groups, in the order the panel lists them.
enum class Category { Sequential, Diverging, Cyclic };
Category category(Colormap cmap); // a registered map: Cyclic or Sequential

// Where t (0 at vmin, 1 at vmax) lands on the table, in [0, 1]: clamped, or for a
// cyclic map wrapped (t mod 1, so vmax lands where vmin does). Non-finite = 0.
inline double fold(double t, bool cyclic) {
    if (!std::isfinite(t)) return 0.0;
    return cyclic ? t - std::floor(t) : std::clamp(t, 0.0, 1.0);
}

// The table entry for t, as fold() places it.
inline int index(double t, bool cyclic) {
    return std::min(255, static_cast<int>(fold(t, cyclic) * 255.0));
}

} // namespace sextant::colormaps
