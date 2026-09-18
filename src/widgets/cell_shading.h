#pragma once
// Data-panel cell shading: each cell is tinted by where its value sits between
// its column's min and max. Presentation only (may use ImGui types).
#include "../plot_data_view.h"
#include "../plot_objects.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <unordered_map>

namespace sextant {
    // The span a set of cells is shaded against.
    struct ValueRange {
        double lo = 0.0, hi = 0.0;
        bool valid = false; // false: nothing finite to scale against

        // 0 at lo, 1 at hi; a flat column or non-finite value gives 0.5.
        float norm(double v) const {
            if (!valid || hi <= lo || !std::isfinite(v)) return 0.5f;
            return static_cast<float>(std::clamp((v - lo) / (hi - lo), 0.0, 1.0));
        }
    };

    // The shading ramp (panel chrome, not a Colormap): light blue through
    // near-white to light red, all light enough for the text drawn over it.
    inline ImU32 shade_color(float t, float alpha = 1.0f) {
        static constexpr float kLo[3] = {0.56f, 0.75f, 0.88f};
        static constexpr float kMid[3] = {0.95f, 0.95f, 0.94f};
        static constexpr float kHi[3] = {0.91f, 0.58f, 0.55f};

        t = std::clamp(t, 0.0f, 1.0f);
        const float* a = (t < 0.5f) ? kLo : kMid;
        const float* b = (t < 0.5f) ? kMid : kHi;
        const float u = (t < 0.5f) ? t * 2.0f : (t - 0.5f) * 2.0f;

        return IM_COL32(static_cast<int>((a[0] + (b[0] - a[0]) * u) * 255.0f),
                        static_cast<int>((a[1] + (b[1] - a[1]) * u) * 255.0f),
                        static_cast<int>((a[2] + (b[2] - a[2]) * u) * 255.0f),
                        static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
    }

    // Hover/active: nudge the cell color toward white instead of the theme's flat
    // highlight.
    inline ImU32 shade_highlight(float t, float amount) {
        static constexpr float kWhite = 1.0f;
        const ImU32 base = shade_color(t);
        const auto ch = [&](int shift) {
            const float v = static_cast<float>((base >> shift) & 0xFF) / 255.0f;
            return static_cast<int>((v + (kWhite - v) * amount) * 255.0f);
        };
        return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), 255);
    }

    // Text over a shaded cell; fixed dark, since the ramp is the same in every
    // theme.
    inline ImU32 shade_text_color() { return IM_COL32(24, 24, 28, 255); }

    // Per-column (and per-matrix) min/max cache: the range covers every value
    // while tables draw only visible rows. Keyed on data_generation (0 always
    // re-scans).
    class CellShadingCache {
    public:
        // One vector column (x and y ranged separately). `plane` is the 3D plane,
        // or -1 for a 2D axes.
        const ValueRange& column(unsigned long long generation, int slot, int plane,
                                 PlotKind kind, int plot_index, int column,
                                 const double* values, std::size_t count) {
            Entry& e = entries_[key_of(slot, plane, kind, plot_index, column)];
            if (fresh(e, generation, count)) return e.range;
            e.range = ValueRange{};
            for (std::size_t i = 0; i < count; ++i) accumulate(e.range, values[i]);
            stamp(e, generation, count);
            return e.range;
        }

        // Packed key: one column of one plot of one axes.
        const ValueRange& matrix(unsigned long long generation, int slot, int plane,
                                 int plot_index, const CowVec<float>& values) {
            Entry& e = entries_[key_of(slot, plane, PlotKind::Heatmap, plot_index, kMatrixCol)];
            if (fresh(e, generation, values.size())) return e.range;
            e.range = ValueRange{};
            for (std::size_t i = 0; i < values.size(); ++i)
                accumulate(e.range, static_cast<double>(values[i]));
            stamp(e, generation, values.size());
            return e.range;
        }

    private:
        static constexpr int kMatrixCol = 0xFFFF;

        struct Entry {
            unsigned long long generation = 0;
            std::size_t count = 0;
            ValueRange range;
        };

        static unsigned long long key_of(int slot, int plane, PlotKind kind,
                                         int plot_index, int column) {
            const auto u = [](int v, unsigned mask) {
                return static_cast<unsigned long long>(static_cast<unsigned>(v) & mask);
            };
            // The plane uses bits 16..27, biased so -1 is 0 (2D keys unchanged).
            return (u(slot, 0xFFFF) << 48) | (u(static_cast<int>(kind), 0xF) << 44)
                   | (u(plot_index, 0xFFFF) << 28) | (u(plane + 1, 0xFFF) << 16)
                   | u(column, 0xFFFF);
        }

        static bool fresh(const Entry& e, unsigned long long generation, std::size_t count) {
            return generation != 0 && e.generation == generation && e.count == count;
        }

        static void stamp(Entry& e, unsigned long long generation, std::size_t count) {
            e.generation = generation;
            e.count = count;
        }

        // Non-finite values are skipped (one NaN would neutralize the column).
        static void accumulate(ValueRange& r, double v) {
            if (!std::isfinite(v)) return;
            if (!r.valid) {
                r.lo = r.hi = v;
                r.valid = true;
                return;
            }
            r.lo = std::min(r.lo, v);
            r.hi = std::max(r.hi, v);
        }

        std::unordered_map<unsigned long long, Entry> entries_;
    };
} // namespace sextant
