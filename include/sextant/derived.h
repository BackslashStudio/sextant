#pragma once
// The derived layer: plots that work something out from the data (bins, an
// extent) and then draw it through the public plotting calls of Axes or
// Plane2D. Each is a free function taking the target first; what it adds is an
// ordinary object of the core kind it called (a bar, a heatmap) and reads
// back, picks and is removed as one. Nothing here reaches past the public API.
#include "axes.h"
#include "axes3d.h"
#include "export.h"
#include "style.h"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sextant {
    // Binning options for hist(); bar drawing is BarOptions.
    struct HistOptions {
        bool density = false;
        bool cumulative = false;
    };

    // A 2D histogram (hist2d()), as matplotlib's hist2d.
    struct Hist2dOptions {
        // Even bins: `xbins` x `ybins` over `xrange` x `yrange` (unset = the
        // extent of the points, widened by 0.5 each way when it is one value).
        int xbins = 10, ybins = 10;
        std::optional<Range> xrange, yrange;

        // Bin edges for an axis (at least two, finite, strictly increasing):
        // they replace its bins and range, e.g. log-spaced edges for a log axis.
        // With either set the histogram is a heatmap_rec(), else a heatmap().
        std::vector<double> x_edges, y_edges;

        // Each bin's count / (points inside the edges * bin area) rather than its count.
        bool density = false;

        // A bin whose value (count, or density) is below cmin or above cmax is
        // empty (NaN): cmin = 1 hides the empty bins.
        std::optional<double> cmin, cmax;

        Colormap cmap = Colormap::Viridis;
        ColorNorm norm;            // see ColorNorm
        // Unset = the bins' smallest and largest value (under log(), the smallest
        // above 0; under two_slope(), widened to keep the centre between them).
        std::optional<float> vmin, vmax;
        bool colorbar = false;
        std::string name;    // see HeatmapOptions::name
        bool visible = true; // see LineOptions::visible
        double zorder = 0.0; // see LineOptions::zorder
    };

    // A histogram of `data` in `bins` even bins over its extent (widened by 0.5
    // each way when it is one value), drawn as bar() at the bin centres: the
    // result is a bar for read-back (bar_count(), bar_data() with the counts),
    // picking and removal. BarOptions::width is a fraction of the bin width;
    // the default argument sets it to 1.0 (bins touch), and passing your own
    // BarOptions restores 0.8. BarOptions::orientation Horizontal lays the bins
    // along y, and BarOptions::bottom takes one value or one per bin. Empty data
    // or bins < 1 adds nothing.
    SEXTANT_API Axes& hist(Axes& ax, std::span<const double> data, int bins = 10,
                           BarOptions bar_opts = {.width = 1.0f}, HistOptions hist_opts = {});

    // A 2D histogram of the points (x[i], y[i]), as matplotlib's hist2d: the
    // count (or density) of each bin, drawn as a heatmap() over even bins, or
    // as a heatmap_rec() when Hist2dOptions gives edges for either axis. The
    // result counts in heatmap_count(), and its values read back through
    // heatmap_data() (row 0 the lowest y bin). A point outside the bins or with
    // a NaN coordinate is not counted. Throws std::invalid_argument if x and y
    // differ in length, a bin count is < 1, a range is non-finite or empty,
    // edges are too few, non-finite or not increasing, or the norm doesn't fit
    // vmin/vmax; nothing is added then.
    SEXTANT_API Axes& hist2d(Axes& ax, std::span<const double> x, std::span<const double> y,
                             Hist2dOptions opts = {});

    // heatmap() of row-major rows x cols values over x in [0, cols], y in
    // [0, rows]: unit cells, so tick 3 is the boundary between columns 2 and 3.
    // A heatmap for read-back and picking.
    SEXTANT_API Axes& imshow(Axes& ax, std::span<const double> data, int rows, int cols,
                             HeatmapOptions opts = {});

    // The same on a 3D plane, over its (u, v).
    SEXTANT_API Plane2D& imshow(Plane2D& plane, std::span<const double> data, int rows, int cols,
                                HeatmapOptions opts = {});
} // namespace sextant
