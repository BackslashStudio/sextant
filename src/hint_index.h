#pragma once
// Uniform-grid spatial index for the hover hint. Built lazily in data space
// and keyed on FigureSnapshot::data_generation, so it survives pan and zoom.
#include "cow_vec.h"
#include "plot_objects.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sextant {

// Below this many points a linear scan is used instead of a grid.
inline constexpr std::size_t kHintIndexMinPoints = 1024;

// Target average points per cell.
inline constexpr std::size_t kHintIndexPointsPerCell = 4;

// Cap on total cells.
inline constexpr std::size_t kHintIndexMaxCells = 1u << 20;

// One plot object's points, bucketed by position. `indexed == false` falls
// back to a linear scan (small plots, unstamped snapshots).
struct PointGrid {
    bool   indexed = false;
    double x0 = 0.0, y0 = 0.0;      // grid origin = data minimum
    double inv_cw = 0.0, inv_ch = 0.0;  // cells per data unit
    int    nx = 1, ny = 1;
    std::vector<std::uint32_t> cell_start;  // nx*ny + 1 prefix sums into points
    std::vector<std::uint32_t> points;      // point indices, grouped by cell

    // Calls f(i) for every point that might lie in the box. The indexed path is
    // conservative (no re-test; the caller does the real hit test); the linear
    // path tests bounds.
    template <class F>
    void for_each_in(const CowVec<double>& x, const CowVec<double>& y,
                     double xlo, double xhi, double ylo, double yhi, F&& f) const {
        if (!indexed) {
            const std::size_t n = x.size();
            for (std::size_t i = 0; i < n; ++i)
                if (x[i] >= xlo && x[i] <= xhi && y[i] >= ylo && y[i] <= yhi)
                    f(i);
            return;
        }
        const int cx0 = cell_of(xlo, x0, inv_cw, nx);
        const int cx1 = cell_of(xhi, x0, inv_cw, nx);
        const int cy0 = cell_of(ylo, y0, inv_ch, ny);
        const int cy1 = cell_of(yhi, y0, inv_ch, ny);
        for (int cy = cy0; cy <= cy1; ++cy) {
            const std::size_t row = static_cast<std::size_t>(cy)
                                  * static_cast<std::size_t>(nx);
            for (int cx = cx0; cx <= cx1; ++cx) {
                const std::size_t c = row + static_cast<std::size_t>(cx);
                for (std::uint32_t k = cell_start[c]; k < cell_start[c + 1]; ++k)
                    f(static_cast<std::size_t>(points[k]));
            }
        }
    }

    // Clamped to [0, n-1]; a NaN bound lands in cell 0.
    static int cell_of(double v, double origin, double inv, int n) {
        if (!(v > origin)) return 0;
        const double f = (v - origin) * inv;
        if (!(f < static_cast<double>(n - 1))) return n - 1;
        return static_cast<int>(f);
    }
};

// Per-window-thread cache of PointGrids, one per plot object; owned by
// PanelState. Call set_frame_key() per hovered axes per frame, then grid() per
// plot. Stale entries are not evicted (bounded by the max plot count).
class HintIndexCache {
public:
    // data_generation 0 ("never stamped") disables caching and falls back to
    // the linear scan. `plane_index` is the 3D plane (-1 for 2D) and is part of
    // the key.
    void set_frame_key(unsigned long long data_generation, int axes_index,
                       int plane_index = -1) {
        data_generation_ = data_generation;
        axes_index_ = axes_index;
        plane_index_ = plane_index;
    }

    // Change only the plane, for a 3D hit test across several planes.
    void set_plane(int plane_index) { plane_index_ = plane_index; }

    const PointGrid& grid(PlotKind kind, std::size_t plot_index,
                          const CowVec<double>& x, const CowVec<double>& y) {
        static const PointGrid unindexed{};
        const std::size_t n = x.size();
        if (data_generation_ == 0 || n < kHintIndexMinPoints || n != y.size()
            || n > 0xFFFFFFFFull)
            return unindexed;

        Entry& e = map_[Key{ axes_index_, plane_index_, static_cast<int>(kind),
                             static_cast<int>(plot_index) }];
        if (e.data_generation != data_generation_) {
            build(e.grid, x, y);
            e.data_generation = data_generation_;
        }
        return e.grid;
    }

private:
    static void build(PointGrid& g, const CowVec<double>& x, const CowVec<double>& y) {
        const std::size_t n = x.size();

        // Bounds over finite points only; non-finite points are left out of
        // the index (they could never win the hit test anyway).
        double xlo = 0, xhi = 0, ylo = 0, yhi = 0;
        std::size_t finite = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isfinite(x[i]) || !std::isfinite(y[i])) continue;
            if (finite == 0) { xlo = xhi = x[i]; ylo = yhi = y[i]; }
            else {
                if (x[i] < xlo) xlo = x[i]; else if (x[i] > xhi) xhi = x[i];
                if (y[i] < ylo) ylo = y[i]; else if (y[i] > yhi) yhi = y[i];
            }
            ++finite;
        }

        const double span_x = xhi - xlo, span_y = yhi - ylo;
        std::size_t target = finite / kHintIndexPointsPerCell;
        if (target < 1) target = 1;
        if (target > kHintIndexMaxCells) target = kHintIndexMaxCells;
        // A degenerate axis gets one row/column; the other gets all the cells.
        int side = static_cast<int>(std::sqrt(static_cast<double>(target)));
        if (side < 1) side = 1;
        g.nx = (span_x > 0.0) ? side : 1;
        g.ny = (span_y > 0.0) ? side : 1;
        if (g.nx == 1 && g.ny > 1) g.ny = static_cast<int>(target);
        if (g.ny == 1 && g.nx > 1) g.nx = static_cast<int>(target);

        g.x0 = xlo; g.y0 = ylo;
        g.inv_cw = (span_x > 0.0) ? (g.nx / span_x) : 0.0;
        g.inv_ch = (span_y > 0.0) ? (g.ny / span_y) : 0.0;

        const std::size_t ncells = static_cast<std::size_t>(g.nx)
                                 * static_cast<std::size_t>(g.ny);

        // Counting sort. Each point's cell is computed once into `cells`;
        // kSkip marks a non-finite point.
        constexpr std::uint32_t kSkip = 0xFFFFFFFFu;
        std::vector<std::uint32_t> cells(n);
        g.cell_start.assign(ncells + 1, 0);
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isfinite(x[i]) || !std::isfinite(y[i])) { cells[i] = kSkip; continue; }
            const std::uint32_t c = static_cast<std::uint32_t>(cell_index(g, x[i], y[i]));
            cells[i] = c;
            ++g.cell_start[c + 1];
        }
        for (std::size_t c = 0; c < ncells; ++c)
            g.cell_start[c + 1] += g.cell_start[c];

        std::vector<std::uint32_t> cursor(g.cell_start.begin(), g.cell_start.end() - 1);
        g.points.assign(g.cell_start[ncells], 0);
        for (std::size_t i = 0; i < n; ++i) {
            if (cells[i] == kSkip) continue;
            g.points[cursor[cells[i]]++] = static_cast<std::uint32_t>(i);
        }
        g.indexed = true;
    }

    static std::size_t cell_index(const PointGrid& g, double x, double y) {
        const int cx = PointGrid::cell_of(x, g.x0, g.inv_cw, g.nx);
        const int cy = PointGrid::cell_of(y, g.y0, g.inv_ch, g.ny);
        return static_cast<std::size_t>(cy) * static_cast<std::size_t>(g.nx)
             + static_cast<std::size_t>(cx);
    }

    struct Key {
        int axes = -1, plane = -1, kind = -1, plot = -1;
        bool operator==(const Key& o) const {
            return axes == o.axes && plane == o.plane && kind == o.kind && plot == o.plot;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            return (static_cast<std::size_t>(static_cast<unsigned>(k.axes)) << 40)
                 ^ (static_cast<std::size_t>(static_cast<unsigned>(k.plane)) << 20)
                 ^ (static_cast<std::size_t>(static_cast<unsigned>(k.kind)) << 32)
                 ^ static_cast<unsigned>(k.plot);
        }
    };
    struct Entry {
        unsigned long long data_generation = 0;
        PointGrid          grid;
    };

    unsigned long long data_generation_ = 0;
    int                axes_index_ = -1;
    int                plane_index_ = -1;
    std::unordered_map<Key, Entry, KeyHash> map_;
};

} // namespace sextant
