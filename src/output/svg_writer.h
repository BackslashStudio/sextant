#pragma once
#include "../plot_objects.h"
#include "../renderer/figure_layout.h"
#include "../renderer/plot_rect.h"
#include "../coord_transform.h"
#include "sextant/style.h"
#include <string>
#include <string_view>
#include <vector>

namespace sextant {

// All data needed to write one axes to SVG, extracted from Axes::Impl by
// Figure::savefig_svg (a friend of Axes). The writer itself is a plain function.
struct SvgAxesData {
    int width  = 800;
    int height = 600;
    // Frame, transform, ticks, text anchors and the legend/colorbar boxes —
    // all of it computed by compute_figure_layout(), the same call the
    // window and PNG paths make. Nothing in this writer derives a position
    // of its own any more.
    CellLayout    layout;
    std::vector<LinePlot>     lines;
    std::vector<ScatterPlot>  scatters;
    std::vector<BarPlot>      bars;
    std::vector<ScatterZPlot> scatter_z;
    std::vector<HeatmapPlot>  heatmaps;
    // Their font sizes come from axes_style, like every other text size here.
    std::string title, xtitle, ytitle;
    bool  grid_enabled    = false;
    GridOptions grid_opts;
    AxesStyle axes_style;
    // Whether either decoration is present, and its box, both live in
    // `layout` (has_legend()/has_colorbar()). Only the cosmetics are here.
    LegendOptions   legend_opts;
    ColorbarOptions colorbar_opts;
};

// All data needed to write a whole figure (one or more axes, e.g. from
// Figure::add_subplot) to SVG. width/height are the figure-wide canvas size;
// each SvgAxesData's own width/height fields are unused in this path.
struct SvgFigureData {
    int width  = 800;
    int height = 600;
    std::string suptitle;
    SuptitleOptions suptitle_opts;
    std::vector<SvgAxesData> axes;
};

// Write a single-axes figure to an SVG file. No GL context required.
void write_svg(std::string_view path, const SvgAxesData& d);

// Write a multi-axes figure (one <g> per axes, each independently clipped
// to its own PlotRect) to a single SVG file. No GL context required.
void write_svg(std::string_view path, const SvgFigureData& d);

} // namespace sextant
