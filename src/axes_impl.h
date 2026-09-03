#pragma once
// Internal header — defines Axes::Impl.
#include "sextant/axes.h"
#include "sextant/style.h"
#include "plot_objects.h"
#include "tick.h"
#include <optional>
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
};

} // namespace sextant
