#pragma once
// Spatial index behind the hover hint, which runs on every frame the cursor
// is over the plot and was otherwise O(N) in the point count.
//
// Buckets a plot's points into a uniform grid so a query visits only the cells
// the hit radius overlaps. Built in *data* space and keyed on
// FigureSnapshot::data_generation, so it survives pan and zoom -- an index
// keyed on the view would be rebuilt exactly when it is needed most. Built
// lazily: only a plot that is actually hovered ever pays for one.
#include "cow_vec.h"
#include "plot_objects.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sextant {

// Below this many points a linear scan beats building (and allocating) a
// grid, so PointGrid stays in its unindexed pass-through mode. Bars and
// small scatters land here and never allocate anything.
inline constexpr std::size_t kHintIndexMinPoints = 1024;

// Average points per cell to aim for. Higher means fewer, fatter cells: less
// per-cell iteration overhead for the same candidate count.
inline constexpr std::size_t kHintIndexPointsPerCell = 4;

// Ceiling on total cells, so a pathological point count cannot turn the
// index itself into the memory problem.
inline constexpr std::size_t kHintIndexMaxCells = 1u << 20;

// One plot object's points, bucketed by data-space position.
//
// `indexed == false` is a fully working fallback rather than an error state:
// for_each_in() then scans linearly, applying the bounds test inline. That is
// what small plots use, and what an unstamped snapshot generation degrades to.
struct PointGrid {
    bool   indexed = false;
    double x0 = 0.0, y0 = 0.0;      // grid origin = data minimum
    double inv_cw = 0.0, inv_ch = 0.0;  // cells per data unit
    int    nx = 1, ny = 1;
    std::vector<std::uint32_t> cell_start;  // nx*ny + 1 prefix sums into points
    std::vector<std::uint32_t> points;      // point indices, grouped by cell

    // Calls f(i) for every point index i that *might* lie in the data-space
    // box. The indexed path is deliberately conservative — it hands over every
    // point of every overlapped cell without re-testing — because the caller
    // applies the real (circular, pixel-space) hit test anyway, and a second
    // bounds test here would just cost more. The unindexed path does test,
    // since there is nothing else narrowing it down.
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

    // Clamped to [0, n-1]. Written with negated comparisons so a NaN bound
    // (possible only from a degenerate transform) lands in cell 0 rather than
    // producing an out-of-range cast.
    static int cell_of(double v, double origin, double inv, int n) {
        if (!(v > origin)) return 0;
        const double f = (v - origin) * inv;
        if (!(f < static_cast<double>(n - 1))) return n - 1;
        return static_cast<int>(f);
    }
};

// Per-window-thread cache of PointGrids, one per plot object.
//
// Held by PanelState (render-thread-only state, one per Figure), and used
// exactly like DataRenderer's caches: set_frame_key() once per hovered axes
// per frame, then a lookup per plot object. Entries for plots that later
// disappear are not evicted — plot indices are positional, so the residue is
// bounded by the largest plot count the figure has ever had, the same
// trade-off DataRenderer's caches make.
class HintIndexCache {
public:
    // A data_generation of 0 means "never stamped" (see FigureSnapshot) and
    // disables caching — which here means falling back to the linear scan,
    // NOT rebuilding a grid every frame. Rebuilding would be strictly worse
    // than the scan it replaces.
    void set_frame_key(unsigned long long data_generation, int axes_index) {
        data_generation_ = data_generation;
        axes_index_ = axes_index;
    }

    const PointGrid& grid(PlotKind kind, std::size_t plot_index,
                          const CowVec<double>& x, const CowVec<double>& y) {
        static const PointGrid unindexed{};
        const std::size_t n = x.size();
        if (data_generation_ == 0 || n < kHintIndexMinPoints || n != y.size()
            || n > 0xFFFFFFFFull)
            return unindexed;

        Entry& e = map_[Key{ axes_index_, static_cast<int>(kind),
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

        // Bounds over finite points only. Non-finite points are excluded from
        // the index entirely, which matches what the linear scan already did
        // with them: NaN fails every comparison, and an infinite coordinate
        // transforms to an infinite distance, so neither could ever win.
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
        // A degenerate axis gets a single row/column, so all the resolution
        // goes to the axis that actually has extent.
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

        // Counting sort. Each point's cell is computed once, into a scratch
        // array, rather than recomputed in the scatter pass — at 1M points
        // that pass is the difference between a ~10 ms and a ~7 ms build, and
        // this build lands on a frame the user is already hovering.
        // kSkip marks a non-finite point, so the finiteness test is also paid
        // only once.
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
        int axes = -1, kind = -1, plot = -1;
        bool operator==(const Key& o) const {
            return axes == o.axes && kind == o.kind && plot == o.plot;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            return (static_cast<std::size_t>(static_cast<unsigned>(k.axes)) << 40)
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
    std::unordered_map<Key, Entry, KeyHash> map_;
};

} // namespace sextant
