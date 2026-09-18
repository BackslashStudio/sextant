#pragma once
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace sextant {

struct Tick { double value; std::string label; };

// Builds an explicit tick list from caller-supplied positions, falling back to
// generate_ticks()'s own "%g" format when a position has no matching label.
// Shared by Axes::set_xticks/set_yticks and Axes3D's three, which is why it
// lives here rather than in either .cpp.
inline std::vector<Tick> make_tick_override(std::span<const double> pos,
                                            const std::vector<std::string>& labels) {
    std::vector<Tick> ticks;
    ticks.reserve(pos.size());
    for (std::size_t i = 0; i < pos.size(); ++i) {
        if (i < labels.size()) {
            ticks.push_back({pos[i], labels[i]});
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", pos[i]);
            ticks.push_back({pos[i], buf});
        }
    }
    return ticks;
}

} // namespace sextant
