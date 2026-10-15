#pragma once
// Axes::stack()'s curves: the baseline and the top of each layer, worked out as
// matplotlib's stackplot does (same sums in the same order), so a stack matches
// its matplotlib counterpart to rounding. NaN is left to propagate as it does
// there.
#include "sextant/style.h"

#include <cstddef>
#include <span>
#include <vector>

namespace sextant {

// For m layers of n values each, m + 1 curves of n values: curve 0 the
// baseline, curve k + 1 the top of layer k (the baseline plus the layers up
// to k). Layer k fills between curves k and k + 1.
inline std::vector<std::vector<double>> stack_curves(std::span<const std::vector<double>> ys,
                                                     std::size_t n, StackBaseline baseline) {
    const std::size_t m = ys.size();
    std::vector<std::vector<double>> curves(m + 1, std::vector<double>(n, 0.0));
    for (std::size_t k = 0; k < m; ++k)
        for (std::size_t j = 0; j < n; ++j)
            curves[k + 1][j] = (k == 0 ? 0.0 : curves[k][j]) + ys[k][j];

    std::vector<double>& first = curves[0];
    auto total = [&](std::size_t j) {
        double t = 0.0;
        for (std::size_t k = 0; k < m; ++k) t += ys[k][j];
        return t;
    };
    switch (baseline) {
    case StackBaseline::Zero:
        break;
    case StackBaseline::Sym:
        for (std::size_t j = 0; j < n; ++j) first[j] = -total(j) * 0.5;
        break;
    case StackBaseline::Wiggle:
        for (std::size_t j = 0; j < n; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < m; ++k)
                s += ys[k][j] * (static_cast<double>(m) - 0.5 - static_cast<double>(k));
            first[j] = s / -static_cast<double>(m);
        }
        break;
    case StackBaseline::WeightedWiggle: {
        // Each layer's rise from the point before, weighted by how far its
        // middle sits from the stack's middle; the baseline follows the running
        // sum of those moves, less half the total.
        double center = 0.0;
        for (std::size_t j = 0; j < n; ++j) {
            const double t = total(j);
            const double inv = t > 0.0 ? 1.0 / t : 0.0;
            double moved = 0.0;
            for (std::size_t k = 0; k < m; ++k) {
                const double increase = j == 0 ? ys[k][0] : ys[k][j] - ys[k][j - 1];
                const double move_up = j == 0 ? 0.5 : (t - curves[k + 1][j] + 0.5 * ys[k][j]) * inv;
                moved += (move_up - 0.5) * increase;
            }
            center += moved;
            first[j] = center - 0.5 * t;
        }
        break;
    }
    }
    if (baseline != StackBaseline::Zero)
        for (std::size_t k = 1; k <= m; ++k)
            for (std::size_t j = 0; j < n; ++j) curves[k][j] += first[j];
    return curves;
}

} // namespace sextant
