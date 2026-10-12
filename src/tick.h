#pragma once
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace sextant {
    struct Tick {
        double value;
        std::string label;
        // The label was made here, not given: a tick format (set_xtick_format())
        // replaces it.
        bool auto_label = false;
    };

    // Explicit ticks from caller positions; a position without a label gets "%g"
    // (marked auto_label).
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
                ticks.push_back({pos[i], buf, true});
            }
        }
        return ticks;
    }
} // namespace sextant
