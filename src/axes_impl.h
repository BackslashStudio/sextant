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
    // Plot objects
    std::vector<LinePlot>     lines;
    std::vector<ScatterPlot>  scatters;
    std::vector<BarPlot>      bars;
    std::vector<HeatmapPlot>  heatmaps;
    std::vector<ScatterZPlot> scatter_z;
    // Decoration
    // Font sizes for these three live in axes_style, not here — that is what
    // lets the widget panel drive them through AxesEdit's existing
    // std::optional<AxesStyle> channel with no extra plumbing.
    std::string   title, xtitle, ytitle;
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


    // Explicit tick override set via Axes::set_xticks/set_yticks (or the
    // widget panel's tick table). Absent = auto-generated ticks.
    std::optional<std::vector<Tick>> xticks_override, yticks_override;

    // TODO: PlotObject list

    // Captures everything the render path needs, so it can be handed across
    // the caller/render-thread boundary without touching this Impl.
    // Decoration and limits are copied; the plot vectors are CowVec, so the
    // five assignments below share buffers rather than duplicating them, which
    // is what makes refresh() cheap at large point counts.
    RenderSnapshot build_snapshot() const {
        RenderSnapshot s;
        s.lines = lines; s.scatters = scatters; s.bars = bars; s.heatmaps = heatmaps;
        s.scatter_z = scatter_z;
        s.title = title; s.xtitle = xtitle; s.ytitle = ytitle;
        s.grid_enabled   = grid_enabled;   s.grid_opts   = grid_opts;
        s.legend_enabled = legend_enabled; s.legend_opts = legend_opts;
        s.colorbar_opts = colorbar_opts;
        s.axes_style = axes_style;
        s.xmin = xmin; s.xmax = xmax; s.ymin = ymin; s.ymax = ymax;
        s.xlim_auto = xlim_auto; s.ylim_auto = ylim_auto;
        s.xticks_override = xticks_override;
        s.yticks_override = yticks_override;
        return s;
    }

    // Ingest, shared by Axes and Plane2D. One copy rather than two because
    // "what a plot kind accepts" is a property of the plot object, not of what
    // it is drawn on -- a plane that validated its extent, its lengths or its
    // error bars differently from an axes would be a difference nothing in the
    // picture could justify. Members of Impl rather than free functions so that
    // neither caller needs access to a type that is private to Axes. `who` only
    // names the caller in the error messages.
    //
    // hist() is deliberately absent: it bins to a BarPlot, and the 3D analogue
    // of a histogram is bar3d over a 2D histogram the caller computes (see
    // memory/spec_3d.md's Scope), so a plane does not re-expose it.
    void ingest_line(std::span<const double> x, std::span<const double> y,
                     const ErrorBar& err, LineOptions opts, const char* who);
    void ingest_scatter(std::span<const double> x, std::span<const double> y,
                        const ErrorBar& err, ScatterOptions opts, const char* who);
    void ingest_scatter_z(std::span<const double> x, std::span<const double> y,
                          std::span<const double> z, const ErrorBar& err,
                          ScatterZOptions opts, const char* who);
    void ingest_bar(std::span<const double> x, std::span<const double> height,
                    const ErrorBar& err, BarOptions opts, const char* who);
    void ingest_heatmap(std::span<const float> data, int rows, int cols,
                        Range xrange, Range yrange, HeatmapOptions opts,
                        const char* who);
};

} // namespace sextant
