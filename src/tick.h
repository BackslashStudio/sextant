#pragma once
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace sextant {
    struct Tick {
        double value;
        std::string label;
    };

    // Explicit ticks from caller positions; a position without a label gets "%g".
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
