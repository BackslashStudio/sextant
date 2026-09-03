#pragma once
#include "sextant/style.h"
#include <cstdint>

namespace sextant::colormaps {

// Returns a pointer to 256*4 bytes (RGBA, each in [0,255]).
const uint8_t* get(Colormap cmap);

} // namespace sextant::colormaps
