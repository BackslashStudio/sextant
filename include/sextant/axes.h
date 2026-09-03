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

    // Continuous-color scatter: each point's color comes from mapping z[i]
    // through opts.cmap/vmin/vmax (see ScatterZOptions), not a fixed color.
    Axes& scatter_z(std::span<const double> x, std::span<const double> y,
                    std::span<const double> z, ScatterZOptions opts = {});

    Axes& bar(std::span<const double> x, std::span<const double> height,
              BarOptions opts = {});

    // Bins `data` into a bar plot, so it takes both option structs.
    // BarOptions::width is a fraction of the *bin* width here; the default
    // argument raises it to 1.0 so bins touch, which means passing your own
    // BarOptions brings BarOptions' 0.8 back unless you set it yourself.
    Axes& hist(std::span<const double> data, int bins = 10,
               BarOptions bar_opts = {.width = 1.0f},
               HistOptions hist_opts = {});

    Axes& heatmap(std::span<const float> data, int rows, int cols,
                  HeatmapOptions opts = {});

    // ----------------------------------------------------------------
    // Decoration
    // ----------------------------------------------------------------

    // "Title" names an axis or the whole axes; "label" is the per-tick text
    // (see set_xticks and AxesStyle::label_color).
    //
    // fontsize is in pixels as drawn and is stored in AxesStyle, so a
    // set_axes_style() call after one of these resets it to that default.
    Axes& set_title(std::string_view text, float fontsize = 18.0f);
    Axes& set_xtitle(std::string_view text, float fontsize = 16.5f);
    Axes& set_ytitle(std::string_view text, float fontsize = 16.5f);
    Axes& set_xlim(double lo, double hi);
    Axes& set_ylim(double lo, double hi);
    Axes& grid(bool enable = true, GridOptions opts = {});
    Axes& set_axes_style(AxesStyle opts = {});
    Axes& legend(LegendOptions opts = {});
    // Styling only — a colorbar is drawn when a plot object asks for one via
    // HeatmapOptions::colorbar / ScatterZOptions::colorbar, not by this call.
    Axes& set_colorbar_style(ColorbarOptions opts = {});
    Axes& set_xticks(std::span<const double> positions,
                     std::vector<std::string> labels = {});
    Axes& set_yticks(std::span<const double> positions,
                     std::vector<std::string> labels = {});

    // Clear all plot objects and reset limits
    Axes& cla();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    friend class Figure;
    explicit Axes();
};

} // namespace sextant
