#pragma once
#include <cstdint>

namespace sextant {
    // The identity of a plot object or a plane: handed out by one process-wide
    // counter when the plotting call makes it, kept by set_*_data(), unchanged by
    // removing its siblings, never reused. 0 means none. Axes::last_object()
    // returns the id of the object a call just made; Axes::remove() and
    // Axes::find_object() take it back.
    using ObjectId = std::uint64_t;
} // namespace sextant
