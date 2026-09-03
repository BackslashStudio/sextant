#include "axes_impl.h"
#include <stdexcept>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sextant {

Axes::Axes() : d(std::make_unique<Impl>()) {}
Axes::~Axes() = default;

namespace {

// Moves the six bulk vectors out of the caller's ErrorBarOptions into the plot
// object's ErrorBarData, which is where per-point data has to live (see
// plot_objects.h). Each is either empty or exactly one entry per point --
// anything else throws, like the x/y length check just above, since a short
// error vector would draw a series whose last points look certain.
//
// `accept_x` is false for line() and bar(), which draw a y error bar only.
// Setting an x field there throws rather than being silently dropped.
ErrorBarData take_error_bars(ErrorBarOptions& eb, std::size_t n,
                             const char* who, bool accept_x) {
    auto take = [&](std::vector<double>& v, const char* field) {
        if (!v.empty() && v.size() != n)
            throw std::invalid_argument(
                std::string(who) + ": errorbar." + field + " has " +
                std::to_string(v.size()) + " entries, need one per point (" +
                std::to_string(n) + ")");
        std::vector<double> out = std::move(v);
        v.clear();   // a moved-from vector is only *valid*, not certainly empty
        return out;
    };
    if (!accept_x && (!eb.xmin.empty() || !eb.xmax.empty() || !eb.xvar.empty()))
        throw std::invalid_argument(
            std::string(who) + ": error bars are y-direction only here; "
            "x error bars are drawn by scatter() and scatter_z()");

    ErrorBarData d;
    d.ymin = take(eb.ymin, "ymin");
    d.ymax = take(eb.ymax, "ymax");
    d.yvar = take(eb.yvar, "yvar");
    if (accept_x) {
        d.xmin = take(eb.xmin, "xmin");
        d.xmax = take(eb.xmax, "xmax");
        d.xvar = take(eb.xvar, "xvar");
    }
    return d;
}

} // namespace

Axes& Axes::line(std::span<const double> x, std::span<const double> y,
                 LineOptions opts) {
    if (x.size() != y.size())
        throw std::invalid_argument("line: x and y must have the same length");
    ErrorBarData err = take_error_bars(opts.errorbar, x.size(), "line", false);
    d->lines.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::move(err),
        std::move(opts),
    });
    return *this;
}

Axes& Axes::line(std::span<const double> y, LineOptions opts) {
    std::vector<double> x(y.size());
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<double>(i);
    return line(x, y, std::move(opts));
}

Axes& Axes::scatter(std::span<const double> x, std::span<const double> y,
                    ScatterOptions opts) {
    if (x.size() != y.size())
        throw std::invalid_argument("scatter: x and y must have the same length");
    ErrorBarData err = take_error_bars(opts.errorbar, x.size(), "scatter", true);
    d->scatters.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::move(err),
        std::move(opts),
    });
    return *this;
}

Axes& Axes::scatter_z(std::span<const double> x, std::span<const double> y,
                      std::span<const double> z, ScatterZOptions opts) {
    if (x.size() != y.size() || x.size() != z.size())
        throw std::invalid_argument("scatter_z: x, y, and z must have the same length");
    ErrorBarData err = take_error_bars(opts.errorbar, x.size(), "scatter_z", true);
    d->scatter_z.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::vector<double>(z.begin(), z.end()),
        std::move(err),
        std::move(opts),
    });
    return *this;
}

Axes& Axes::bar(std::span<const double> x, std::span<const double> height,
                BarOptions opts) {
    if (x.size() != height.size())
        throw std::invalid_argument("bar: x and height must have the same length");
    ErrorBarData err = take_error_bars(opts.errorbar, x.size(), "bar", false);
    // Derive data-space bar width from inter-bar spacing × fractional opts.width.
    double spacing = 1.0;
    if (x.size() > 1)
        spacing = std::abs(x[1] - x[0]);
    d->bars.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(height.begin(), height.end()),
        spacing * static_cast<double>(opts.width),
        std::move(err),
        std::move(opts),
    });
    return *this;
}

Axes& Axes::hist(std::span<const double> data, int bins,
                 BarOptions bar_opts, HistOptions hist_opts) {
    if (data.empty() || bins < 1) return *this;

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

    // bar_opts goes through untouched, so a histogram gets the same edge,
    // hint_labels and width control a bar chart has. `width` is read against
    // the bin width; 1.0, hist()'s own default argument, makes the bins touch.
    //
    // Error bars are the one exception: a bin's height is a count this
    // function derived, not a measurement the caller could have an uncertainty
    // on, so anything set there is dropped and the BarPlot is built with no
    // ErrorBarData. The only BarOptions field hist() does not honor.
    bar_opts.errorbar = ErrorBarOptions{};

    d->bars.push_back({
        std::move(centers),
        std::move(counts),
        bin_w * static_cast<double>(bar_opts.width),
        ErrorBarData{},
        std::move(bar_opts),
    });
    return *this;
}

Axes& Axes::heatmap(std::span<const float> data, int rows, int cols,
                    HeatmapOptions opts) {
    if (rows < 1 || cols < 1)
        throw std::invalid_argument("heatmap: rows and cols must be positive");
    if (static_cast<int>(data.size()) < rows * cols)
        throw std::invalid_argument("heatmap: data too small for rows×cols");

    // Sorted and de-duplicated once here rather than on every trace:
    // the draw order becomes value order however the levels were listed, and
    // a level given twice stops being traced, stroked and labelled twice on
    // top of itself. Finiteness first — a NaN would neither sort nor compare
    // usefully, and would silently trace nothing.
    for (double level : opts.contours)
        if (!std::isfinite(level))
            throw std::invalid_argument("heatmap: contour levels must be finite");
    std::sort(opts.contours.begin(), opts.contours.end());
    opts.contours.erase(std::unique(opts.contours.begin(), opts.contours.end()),
                        opts.contours.end());

    d->heatmaps.push_back({
        std::vector<float>(data.begin(), data.begin() + rows * cols),
        rows, cols,
        std::move(opts),
    });
    return *this;
}

Axes& Axes::set_title(std::string_view text, float fontsize) {
    d->title = text; d->axes_style.title_fontsize = fontsize; return *this;
}
Axes& Axes::set_xtitle(std::string_view text, float fontsize) {
    d->xtitle = text; d->axes_style.xtitle_fontsize = fontsize; return *this;
}
Axes& Axes::set_ytitle(std::string_view text, float fontsize) {
    d->ytitle = text; d->axes_style.ytitle_fontsize = fontsize; return *this;
}
Axes& Axes::set_xlim(double lo, double hi) {
    d->xmin = lo; d->xmax = hi; d->xlim_auto = false; return *this;
}
Axes& Axes::set_ylim(double lo, double hi) {
    d->ymin = lo; d->ymax = hi; d->ylim_auto = false; return *this;
}
Axes& Axes::grid(bool enable, GridOptions opts) {
    d->grid_enabled = enable; d->grid_opts = opts; return *this;
}
Axes& Axes::set_axes_style(AxesStyle opts) {
    d->axes_style = opts; return *this;
}
Axes& Axes::legend(LegendOptions opts) {
    d->legend_enabled = true; d->legend_opts = opts; return *this;
}
Axes& Axes::set_colorbar_style(ColorbarOptions opts) {
    d->colorbar_opts = opts; return *this;
}

namespace {
// Builds an explicit tick list, falling back to generate_ticks()'s own
// "%g" format when a position has no matching label.
std::vector<Tick> make_tick_override(std::span<const double> pos,
                                     const std::vector<std::string>& labels) {
    std::vector<Tick> ticks;
    ticks.reserve(pos.size());
    for (std::size_t i = 0; i < pos.size(); ++i) {
        if (i < labels.size()) {
            ticks.push_back({pos[i], labels[i]});
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", pos[i]);
            ticks.push_back({pos[i], buf});
        }
    }
    return ticks;
}
} // namespace

Axes& Axes::set_xticks(std::span<const double> pos, std::vector<std::string> labels) {
    if (pos.empty()) d->xticks_override.reset();
    else             d->xticks_override = make_tick_override(pos, labels);
    return *this;
}
Axes& Axes::set_yticks(std::span<const double> pos, std::vector<std::string> labels) {
    if (pos.empty()) d->yticks_override.reset();
    else             d->yticks_override = make_tick_override(pos, labels);
    return *this;
}
Axes& Axes::cla() {
    *d = Impl{};  // reset to defaults
    return *this;
}

} // namespace sextant
