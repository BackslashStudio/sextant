#pragma once
// Internal header — defines Axes::Impl.
#include "sextant/axes.h"
#include "sextant/style.h"
#include "plot_objects.h"
#include "tick.h"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sextant {

struct Axes::Impl {
    std::vector<LinePlot>     lines;
    std::vector<ScatterPlot>  scatters;
    std::vector<BarPlot>      bars;
    std::vector<HeatmapPlot>  heatmaps;
    std::vector<ScatterZPlot> scatter_z;
    // Font sizes for these live in axes_style, so the panel edits them via AxesEdit.
    std::string   title, xtitle, ytitle;
    TitleStamps   title_stamps;
    bool          grid_enabled    = false;
    GridOptions   grid_opts;
    bool          legend_enabled  = false;
    LegendOptions   legend_opts;
    ColorbarOptions colorbar_opts;
    AxesStyle     axes_style;

    // Axis limits (auto = not yet set by user)
    double xmin = 0, xmax = 1;
    double ymin = 0, ymax = 1;
    bool   xlim_auto = true;
    bool   ylim_auto = true;
    LimitStamps limit_stamps;
    StyleStamps style_stamps;

    // Explicit tick override set via Axes::set_xticks/set_yticks (or the
    // widget panel's tick table). Absent = auto-generated ticks.
    std::optional<std::vector<Tick>> xticks_override, yticks_override;

    // TODO: PlotObject list

    // Copy for the render thread. Plot vectors are CowVec, so this shares
    // buffers instead of copying the data.
    RenderSnapshot build_snapshot() const {
        RenderSnapshot s;
        s.lines = lines; s.scatters = scatters; s.bars = bars; s.heatmaps = heatmaps;
        s.scatter_z = scatter_z;
        s.title = title; s.xtitle = xtitle; s.ytitle = ytitle;
        s.title_stamps = title_stamps;
        s.grid_enabled   = grid_enabled;   s.grid_opts   = grid_opts;
        s.legend_enabled = legend_enabled; s.legend_opts = legend_opts;
        s.colorbar_opts = colorbar_opts;
        s.axes_style = axes_style;
        s.xmin = xmin; s.xmax = xmax; s.ymin = ymin; s.ymax = ymax;
        s.xlim_auto = xlim_auto; s.ylim_auto = ylim_auto;
        s.limit_stamps = limit_stamps;
        s.style_stamps = style_stamps;
        s.xticks_override = xticks_override;
        s.yticks_override = yticks_override;
        return s;
    }

    // Ingest shared by Axes and Plane2D, so both validate identically. `who`
    // names the caller in error messages. No hist(): planes don't expose it.
    void ingest_line(std::span<const double> x, std::span<const double> y,
                     const ErrorBar& err, LineOptions opts, const char* who);
    void ingest_scatter(std::span<const double> x, std::span<const double> y,
                        const ErrorBar& err, ScatterOptions opts, const char* who);
    void ingest_scatter_z(std::span<const double> x, std::span<const double> y,
                          std::span<const double> z, const ErrorBar& err,
                          ScatterZOptions opts, const char* who);
    void ingest_bar(std::span<const double> x, std::span<const double> height,
                    const ErrorBar& err, BarOptions opts, const char* who);
    void ingest_heatmap(std::span<const double> data, int rows, int cols,
                        Range xrange, Range yrange, HeatmapOptions opts,
                        const char* who);

    // set_*_data() for Axes and Plane2D: replace object i's data, validated as
    // when it was plotted. `who` names the caller.
    void set_line_data(std::size_t i, const LineData& v, const char* who);
    void set_scatter_data(std::size_t i, const ScatterData& v, const char* who);
    void set_scatter_z_data(std::size_t i, const ScatterZData& v, const char* who);
    void set_bar_data(std::size_t i, const BarData& v, const char* who);
    void set_heatmap_data(std::size_t i, const HeatmapData& v, const char* who);
};

} // namespace sextant
