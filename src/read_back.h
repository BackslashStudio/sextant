#pragma once
// Plot object <-> public data struct, shared by Axes, Plane2D and Axes3D: the
// read-back conversions here, the set_*_data() bodies in each source file.
#include "sextant/axes3d.h"
#include "plot_objects.h"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace sextant::read_back {
    // The i-th object of a kind; `who` names the getter or setter in the error.
    template <typename V>
    auto at(V& v, std::size_t i, const char* who) -> decltype(v[i]) {
        if (i >= v.size())
            throw std::out_of_range(std::string(who) + ": index " + std::to_string(i)
                                    + " out of range, count is " + std::to_string(v.size()));
        return v[i];
    }

    // For set_*_data(): error bars and hint_labels stay while the point count
    // (a grid's shape) is unchanged and are dropped otherwise, as they would no
    // longer line up. Then takes a fresh data_stamp.
    template <typename P>
    void keep_aligned(P& p, bool same_shape) {
        if (!same_shape) {
            if constexpr (requires { p.err; }) p.err = {};
            p.opts.hint_labels.clear();
        }
        p.data_stamp = next_snapshot_generation();
    }

    inline LineData     to_data(const LinePlot& p)     { return { p.x.get(), p.y.get() }; }
    inline ScatterData  to_data(const ScatterPlot& p)  { return { p.x.get(), p.y.get() }; }
    inline ScatterZData to_data(const ScatterZPlot& p) { return { p.x.get(), p.y.get(), p.z.get() }; }
    inline BarData      to_data(const BarPlot& p)      { return { p.centers.get(), p.heights.get() }; }

    inline HeatmapData to_data(const HeatmapPlot& p) {
        return { std::vector<double>(p.data.begin(), p.data.end()),
                 p.rows, p.cols, p.xrange, p.yrange };
    }

    inline Bar3DData to_data(const Bar3DPlot& p) {
        return { p.orient, p.u.get(), p.v.get(), p.heights.get(), p.bottoms.get() };
    }

    inline SurfaceData to_data(const SurfacePlot& p) {
        return { p.orient, p.u.get(), p.v.get(), p.heights.get() };
    }

    inline SurfaceTriData to_data(const SurfaceTriPlot& p) {
        return { p.x.get(), p.y.get(), p.z.get(), p.tri.get(), p.colors.get() };
    }

    inline Scatter3DData to_data(const Scatter3DPlot& p) {
        return { p.x.get(), p.y.get(), p.z.get(), p.colors.get() };
    }

    inline Line3DData to_data(const Line3DPlot& p) {
        return { p.x.get(), p.y.get(), p.z.get(), p.colors.get() };
    }
} // namespace sextant::read_back
