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

// Copies the caller's ErrorBar into ErrorBarData. Each span must be empty or
// one entry per point, else throws. Values are not checked (see ErrorBar).
ErrorBarData take_error_bars(const ErrorBar& eb, std::size_t n, const char* who) {
    auto take = [&](std::span<const double> s, const char* field) {
        if (!s.empty() && s.size() != n)
            throw std::invalid_argument(
                std::string(who) + ": err." + field + " has " +
                std::to_string(s.size()) + " entries, need one per point (" +
                std::to_string(n) + ")");
        return CowVec<double>(std::vector<double>(s.begin(), s.end()));
    };
    ErrorBarData d;
    d.x_cap_lo = take(eb.x_cap_lo, "x_cap_lo");
    d.x_cap_hi = take(eb.x_cap_hi, "x_cap_hi");
    d.x_box_lo = take(eb.x_box_lo, "x_box_lo");
    d.x_box_hi = take(eb.x_box_hi, "x_box_hi");
    d.y_cap_lo = take(eb.y_cap_lo, "y_cap_lo");
    d.y_cap_hi = take(eb.y_cap_hi, "y_cap_hi");
    d.y_box_lo = take(eb.y_box_lo, "y_box_lo");
    d.y_box_hi = take(eb.y_box_hi, "y_box_hi");
    return d;
}

} // namespace

// Vector-shaped ingests, shared by Axes and Plane2D. `who` names the caller in
// error messages.
void Axes::Impl::ingest_line(std::span<const double> x, std::span<const double> y,
                             const ErrorBar& eb, LineOptions opts, const char* who) {
    if (x.size() != y.size())
        throw std::invalid_argument(std::string(who) + ": x and y must have the same length");
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    lines.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::move(err),
        std::move(opts),
    });
}

void Axes::Impl::ingest_scatter(std::span<const double> x, std::span<const double> y,
                                const ErrorBar& eb, ScatterOptions opts, const char* who) {
    if (x.size() != y.size())
        throw std::invalid_argument(std::string(who) + ": x and y must have the same length");
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    scatters.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::move(err),
        std::move(opts),
    });
}

void Axes::Impl::ingest_scatter_z(std::span<const double> x, std::span<const double> y,
                                  std::span<const double> z, const ErrorBar& eb,
                                  ScatterZOptions opts, const char* who) {
    if (x.size() != y.size() || x.size() != z.size())
        throw std::invalid_argument(std::string(who) + ": x, y, and z must have the same length");
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    scatter_z.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::vector<double>(z.begin(), z.end()),
        std::move(err),
        std::move(opts),
    });
}

void Axes::Impl::ingest_bar(std::span<const double> x, std::span<const double> height,
                            const ErrorBar& eb, BarOptions opts, const char* who) {
    if (x.size() != height.size())
        throw std::invalid_argument(std::string(who) + ": x and height must have the same length");
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    // Derive data-space bar width from inter-bar spacing × fractional opts.width.
    double spacing = 1.0;
    if (x.size() > 1)
        spacing = std::abs(x[1] - x[0]);
    bars.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(height.begin(), height.end()),
        spacing * static_cast<double>(opts.width),
        std::move(err),
        std::move(opts),
    });
}

// Each kind with and without an ErrorBar; the latter passes an empty one.
Axes& Axes::line(std::span<const double> x, std::span<const double> y,
                 LineOptions opts) {
    return line(x, y, ErrorBar{}, std::move(opts));
}

Axes& Axes::line(std::span<const double> x, std::span<const double> y,
                 const ErrorBar& err, LineOptions opts) {
    d->ingest_line(x, y, err, std::move(opts), "line");
    return *this;
}

Axes& Axes::line(std::span<const double> y, LineOptions opts) {
    return line(y, ErrorBar{}, std::move(opts));
}

Axes& Axes::line(std::span<const double> y, const ErrorBar& err, LineOptions opts) {
    std::vector<double> x(y.size());
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<double>(i);
    return line(x, y, err, std::move(opts));
}

Axes& Axes::scatter(std::span<const double> x, std::span<const double> y,
                    ScatterOptions opts) {
    return scatter(x, y, ErrorBar{}, std::move(opts));
}

Axes& Axes::scatter(std::span<const double> x, std::span<const double> y,
                    const ErrorBar& err, ScatterOptions opts) {
    d->ingest_scatter(x, y, err, std::move(opts), "scatter");
    return *this;
}

Axes& Axes::scatter_z(std::span<const double> x, std::span<const double> y,
                      std::span<const double> z, ScatterZOptions opts) {
    return scatter_z(x, y, z, ErrorBar{}, std::move(opts));
}

Axes& Axes::scatter_z(std::span<const double> x, std::span<const double> y,
                      std::span<const double> z, const ErrorBar& err,
                      ScatterZOptions opts) {
    d->ingest_scatter_z(x, y, z, err, std::move(opts), "scatter_z");
    return *this;
}

Axes& Axes::bar(std::span<const double> x, std::span<const double> height,
                BarOptions opts) {
    return bar(x, height, ErrorBar{}, std::move(opts));
}

Axes& Axes::bar(std::span<const double> x, std::span<const double> height,
                const ErrorBar& err, BarOptions opts) {
    d->ingest_bar(x, height, err, std::move(opts), "bar");
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

    // bar_opts passes through; `width` is relative to the bin width. No error
    // bars for histograms.

    d->bars.push_back({
        std::move(centers),
        std::move(counts),
        bin_w * static_cast<double>(bar_opts.width),
        ErrorBarData{},
        std::move(bar_opts),
    });
    return *this;
}

void Axes::Impl::ingest_heatmap(std::span<const float> data, int rows, int cols,
                                Range xrange, Range yrange, HeatmapOptions opts,
                                const char* who) {
    const std::string w = who;
    if (rows < 1 || cols < 1)
        throw std::invalid_argument(w + ": rows and cols must be positive");
    if (static_cast<int>(data.size()) < rows * cols)
        throw std::invalid_argument(w + ": data too small for rows×cols");

    // Reject degenerate ranges here once; reversed ranges mirror the image.
    for (const Range& r : { xrange, yrange }) {
        if (!std::isfinite(r.lo) || !std::isfinite(r.hi))
            throw std::invalid_argument(w + ": range bounds must be finite");
        if (r.lo == r.hi)
            throw std::invalid_argument(w + ": range must span a non-zero interval");
    }

    // Sort and de-duplicate once. Check finiteness first (NaN doesn't sort).
    for (double level : opts.contours)
        if (!std::isfinite(level))
            throw std::invalid_argument(w + ": contour levels must be finite");
    std::sort(opts.contours.begin(), opts.contours.end());
    opts.contours.erase(std::unique(opts.contours.begin(), opts.contours.end()),
                        opts.contours.end());

    heatmaps.push_back({
        std::vector<float>(data.begin(), data.begin() + rows * cols),
        rows, cols,
        xrange, yrange,
        std::move(opts),
    });
}

Axes& Axes::heatmap(std::span<const float> data, int rows, int cols,
                    Range xrange, Range yrange, HeatmapOptions opts) {
    d->ingest_heatmap(data, rows, cols, xrange, yrange, std::move(opts), "heatmap");
    return *this;
}

// heatmap() over the index extent: one unit per cell, origin at (0,0).
Axes& Axes::imshow(std::span<const float> data, int rows, int cols,
                   HeatmapOptions opts) {
    // Clamp so an invalid shape reports heatmap()'s rows/cols error rather than
    // a range error.
    const Range xr{ 0.0, static_cast<double>(std::max(cols, 1)) };
    const Range yr{ 0.0, static_cast<double>(std::max(rows, 1)) };
    return heatmap(data, rows, cols, xr, yr, std::move(opts));
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
