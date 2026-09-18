#pragma once
#include "export.h"
#include "style.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace sextant {
    class SEXTANT_API Axes {
    public:
        ~Axes();

        // ----------------------------------------------------------------
        // Plot methods — all return *this for chaining
        // ----------------------------------------------------------------

        Axes& line(std::span<const double> x, std::span<const double> y,
                   LineOptions opts = {});

        Axes& line(std::span<const double> y, LineOptions opts = {});

        Axes& scatter(std::span<const double> x, std::span<const double> y,
                      ScatterOptions opts = {});

        // Each point's color maps z[i] through opts.cmap/vmin/vmax.
        Axes& scatter_z(std::span<const double> x, std::span<const double> y,
                        std::span<const double> z, ScatterZOptions opts = {});

        Axes& bar(std::span<const double> x, std::span<const double> height,
                  BarOptions opts = {});

        // The same four, with error bars. Write `err` with designated initializers;
        // a bare `{}` is ambiguous.
        Axes& line(std::span<const double> x, std::span<const double> y,
                   const ErrorBar& err, LineOptions opts = {});

        Axes& line(std::span<const double> y, const ErrorBar& err, LineOptions opts = {});

        Axes& scatter(std::span<const double> x, std::span<const double> y,
                      const ErrorBar& err, ScatterOptions opts = {});

        Axes& scatter_z(std::span<const double> x, std::span<const double> y,
                        std::span<const double> z, const ErrorBar& err,
                        ScatterZOptions opts = {});

        Axes& bar(std::span<const double> x, std::span<const double> height,
                  const ErrorBar& err, BarOptions opts = {});

        // BarOptions::width is a fraction of the bin width; the default argument
        // sets it to 1.0 (bins touch). Passing your own BarOptions restores 0.8.
        Axes& hist(std::span<const double> data, int bins = 10,
                   BarOptions bar_opts = {.width = 1.0f},
                   HistOptions hist_opts = {});

        // `data` is row-major rows x cols, drawn as a uniform mesh over
        // xrange x yrange (cell edges, see Range). Throws if either range is
        // non-finite or degenerate.
        Axes& heatmap(std::span<const float> data, int rows, int cols,
                      Range xrange, Range yrange, HeatmapOptions opts = {});

        // heatmap() over x in [0, cols], y in [0, rows]: unit cells, so tick 3 is
        // the boundary between columns 2 and 3.
        Axes& imshow(std::span<const float> data, int rows, int cols,
                     HeatmapOptions opts = {});

        // ----------------------------------------------------------------
        // Decoration
        // ----------------------------------------------------------------

        // "Title" names an axis or the axes; "label" is per-tick text.
        // fontsize is in pixels and stored in AxesStyle, so a later
        // set_axes_style() resets it.
        Axes& set_title(std::string_view text, float fontsize = 18.0f);

        Axes& set_xtitle(std::string_view text, float fontsize = 16.5f);

        Axes& set_ytitle(std::string_view text, float fontsize = 16.5f);

        Axes& set_xlim(double lo, double hi);

        Axes& set_ylim(double lo, double hi);

        Axes& grid(bool enable = true, GridOptions opts = {});

        Axes& set_axes_style(AxesStyle opts = {});

        Axes& legend(LegendOptions opts = {});

        // Styling only; a colorbar is requested by HeatmapOptions/ScatterZOptions::colorbar.
        Axes& set_colorbar_style(ColorbarOptions opts = {});

        Axes& set_xticks(std::span<const double> positions,
                         std::vector<std::string> labels = {});

        Axes& set_yticks(std::span<const double> positions,
                         std::vector<std::string> labels = {});

        // Clear all plot objects and reset limits.
        Axes& cla();

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
        friend class Figure;
        // A Plane2D holds an Axes::Impl rather than being an Axes.
        friend class Plane2D;

        explicit Axes();
    };
} // namespace sextant
