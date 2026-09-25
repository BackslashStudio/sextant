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
        Axes& heatmap(std::span<const double> data, int rows, int cols,
                      Range xrange, Range yrange, HeatmapOptions opts = {});

        // heatmap() over x in [0, cols], y in [0, rows]: unit cells, so tick 3 is
        // the boundary between columns 2 and 3.
        Axes& imshow(std::span<const double> data, int rows, int cols,
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

        // ----------------------------------------------------------------
        // Read-back
        // ----------------------------------------------------------------
        // What this axes holds now: the caller's own calls, plus panel edits to
        // titles, limits (pan/zoom included) and plot data once Figure::refresh()
        // has folded them in. Other panel edits are live preview and not reflected.
        std::string title() const;

        std::string xtitle() const;

        std::string ytitle() const;

        // The limits as drawn: set_xlim()'s, or the auto scale of the data.
        Range xlim() const;

        Range ylim() const;

        // Plot objects of each kind, in the order they were added. `*_data(i)`
        // returns a copy and throws std::out_of_range for i >= `*_count()`.
        std::size_t line_count() const;
        LineData line_data(std::size_t i) const;

        std::size_t scatter_count() const;
        ScatterData scatter_data(std::size_t i) const;

        std::size_t scatter_z_count() const;
        ScatterZData scatter_z_data(std::size_t i) const;

        // bar() and hist() alike.
        std::size_t bar_count() const;
        BarData bar_data(std::size_t i) const;

        // heatmap() and imshow() alike.
        std::size_t heatmap_count() const;
        HeatmapData heatmap_data(std::size_t i) const;

        // ----------------------------------------------------------------
        // Updating plotted data
        // ----------------------------------------------------------------
        // Replace object i's data in place, keeping its options and the rest
        // of the axes, which cla() would reset: the way to animate a plot, with
        // Figure::refresh() after. Takes what `*_data(i)` returns, validated as
        // the plotting call would. Error bars and hint_labels stay while the
        // point count (a heatmap's rows and cols) is unchanged and are dropped
        // otherwise. Throws std::out_of_range for i >= `*_count()` and
        // std::invalid_argument for bad data; either way nothing changes.
        Axes& set_line_data(std::size_t i, const LineData& data);

        // Each also takes its data as spans, in the plotting call's argument
        // order: pass a caller's own arrays (an arma::vec, a column of a
        // matrix) without building the struct. Same checks and behaviour.
        Axes& set_line_data(std::size_t i, std::span<const double> x, std::span<const double> y);

        Axes& set_scatter_data(std::size_t i, const ScatterData& data);

        Axes& set_scatter_data(std::size_t i, std::span<const double> x, std::span<const double> y);

        Axes& set_scatter_z_data(std::size_t i, const ScatterZData& data);

        Axes& set_scatter_z_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                 std::span<const double> z);

        // The bar width is kept unless x changes; then it is re-derived from
        // the spacing, as bar() does.
        Axes& set_bar_data(std::size_t i, const BarData& data);

        Axes& set_bar_data(std::size_t i, std::span<const double> x, std::span<const double> height);

        Axes& set_heatmap_data(std::size_t i, const HeatmapData& data);

        // As heatmap() takes it: row-major rows x cols over xrange x yrange.
        Axes& set_heatmap_data(std::size_t i, std::span<const double> data, int rows, int cols,
                               Range xrange, Range yrange);

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
        friend class Figure;
        // A Plane2D holds an Axes::Impl rather than being an Axes.
        friend class Plane2D;

        explicit Axes();
    };
} // namespace sextant
