// The derived layer (sextant/derived.h): each function works out what to draw
// and hands it to a public plotting call; nothing here touches Axes::Impl.
#include "sextant/derived.h"
#include "hist2d.h"
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace sextant {

Axes& hist(Axes& ax, std::span<const double> data, int bins,
           BarOptions bar_opts, HistOptions hist_opts) {
    if (data.empty() || bins < 1) return ax;

    double lo = *std::min_element(data.begin(), data.end());
    double hi = *std::max_element(data.begin(), data.end());
    if (lo == hi) { lo -= 0.5; hi += 0.5; }

    const double bin_w = (hi - lo) / bins;
    std::vector<double> counts(static_cast<std::size_t>(bins), 0.0);
    for (double v : data) {
        int idx = static_cast<int>((v - lo) / bin_w);
        if (idx >= bins) idx = bins - 1;
        counts[static_cast<std::size_t>(idx)]++;
    }

    if (hist_opts.density) {
        double total = static_cast<double>(data.size()) * bin_w;
        for (double& c : counts) c /= total;
    }
    if (hist_opts.cumulative) {
        for (int i = 1; i < bins; ++i)
            counts[static_cast<std::size_t>(i)] += counts[static_cast<std::size_t>(i - 1)];
    }

    std::vector<double> centers(static_cast<std::size_t>(bins));
    for (int i = 0; i < bins; ++i)
        centers[static_cast<std::size_t>(i)] = lo + (i + 0.5) * bin_w;

    // bar() takes `width` relative to the spacing of the centres, which is the
    // bin width; a single bar's spacing is 1, so scale it by the bin width.
    if (bins == 1) bar_opts.width = static_cast<float>(bin_w * bar_opts.width);
    return ax.bar(centers, counts, std::move(bar_opts));
}

// Binned here; the result is an ordinary heatmap (even bins) or mesh, which
// does the drawing checks (the norm against the resolved limits among them).
Axes& hist2d(Axes& ax, std::span<const double> x, std::span<const double> y, Hist2dOptions opts) {
    const Hist2dBins b = hist2d_bins(x, y, opts, "hist2d");
    if (b.even) {
        HeatmapOptions ho;
        ho.cmap = opts.cmap;
        ho.vmin = b.vmin;
        ho.vmax = b.vmax;
        ho.norm = opts.norm;
        ho.colorbar = opts.colorbar;
        ho.name = std::move(opts.name);
        ho.visible = opts.visible;
        ho.zorder = opts.zorder;
        return ax.heatmap(b.values, b.rows(), b.cols(), Range{ b.x_edges.front(), b.x_edges.back() },
                          Range{ b.y_edges.front(), b.y_edges.back() }, std::move(ho));
    }
    HeatmapRecOptions ro;
    ro.cmap = opts.cmap;
    ro.vmin = b.vmin;
    ro.vmax = b.vmax;
    ro.norm = opts.norm;
    ro.colorbar = opts.colorbar;
    ro.name = std::move(opts.name);
    ro.visible = opts.visible;
    ro.zorder = opts.zorder;
    return ax.heatmap_rec(b.values, b.rows(), b.cols(), b.x_edges, b.y_edges, std::move(ro));
}

// heatmap() over the index extent: one unit per cell, origin at (0,0). Clamped
// so an invalid shape reports heatmap()'s rows/cols error rather than a range
// error.
Axes& imshow(Axes& ax, std::span<const double> data, int rows, int cols, HeatmapOptions opts) {
    const Range xr{ 0.0, static_cast<double>(std::max(cols, 1)) };
    const Range yr{ 0.0, static_cast<double>(std::max(rows, 1)) };
    return ax.heatmap(data, rows, cols, xr, yr, std::move(opts));
}

Plane2D& imshow(Plane2D& plane, std::span<const double> data, int rows, int cols,
                HeatmapOptions opts) {
    const Range xr{ 0.0, static_cast<double>(std::max(cols, 1)) };
    const Range yr{ 0.0, static_cast<double>(std::max(rows, 1)) };
    return plane.heatmap(data, rows, cols, xr, yr, std::move(opts));
}

} // namespace sextant
