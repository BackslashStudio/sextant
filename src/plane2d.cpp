#include "plane2d_impl.h"
#include "read_back.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sextant {
    Plane2D::Plane2D(PlaneOrientation orient, double offset, Plane2DOptions opts)
        : d(std::make_unique<Impl>()) {
        d->orient = orient;
        d->offset = offset;
        d->opts = opts;
        // Stamped as by set_offset(). An edit meant for a plane a cla() removed
        // cannot land on this one: it carries that plane's id (see ObjectId).
        d->placement_stamp = next_snapshot_generation();
    }

    Plane2D::~Plane2D() = default;

    Plane2D& Plane2D::line(std::span<const double> x, std::span<const double> y,
                           LineOptions opts) {
        return line(x, y, ErrorBar{}, std::move(opts));
    }

    Plane2D& Plane2D::line(std::span<const double> x, std::span<const double> y,
                           const ErrorBar& err, LineOptions opts) {
        d->sheet.ingest_line(x, y, err, std::move(opts), "Plane2D::line");
        return *this;
    }

    Plane2D& Plane2D::line(std::span<const double> y, LineOptions opts) {
        return line(y, ErrorBar{}, std::move(opts));
    }

    Plane2D& Plane2D::line(std::span<const double> y, const ErrorBar& err, LineOptions opts) {
        std::vector<double> x(y.size());
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<double>(i);
        return line(x, y, err, std::move(opts));
    }

    Plane2D& Plane2D::scatter(std::span<const double> x, std::span<const double> y,
                              ScatterOptions opts) {
        return scatter(x, y, ErrorBar{}, std::move(opts));
    }

    Plane2D& Plane2D::scatter(std::span<const double> x, std::span<const double> y,
                              const ErrorBar& err, ScatterOptions opts) {
        d->sheet.ingest_scatter(x, y, err, std::move(opts), "Plane2D::scatter");
        return *this;
    }

    Plane2D& Plane2D::scatter_z(std::span<const double> x, std::span<const double> y,
                                std::span<const double> z, ScatterZOptions opts) {
        return scatter_z(x, y, z, ErrorBar{}, std::move(opts));
    }

    Plane2D& Plane2D::scatter_z(std::span<const double> x, std::span<const double> y,
                                std::span<const double> z, const ErrorBar& err,
                                ScatterZOptions opts) {
        d->sheet.ingest_scatter_z(x, y, z, err, std::move(opts), "Plane2D::scatter_z");
        return *this;
    }

    Plane2D& Plane2D::bar(std::span<const double> x, std::span<const double> height,
                          BarOptions opts) {
        return bar(x, height, ErrorBar{}, std::move(opts));
    }

    Plane2D& Plane2D::bar(std::span<const double> x, std::span<const double> height,
                          const ErrorBar& err, BarOptions opts) {
        d->sheet.ingest_bar(x, height, err, std::move(opts), "Plane2D::bar");
        return *this;
    }

    Plane2D& Plane2D::heatmap(std::span<const double> data, int rows, int cols,
                              Range xrange, Range yrange, HeatmapOptions opts) {
        d->sheet.ingest_heatmap(data, rows, cols, xrange, yrange, std::move(opts),
                                "Plane2D::heatmap");
        return *this;
    }

    Plane2D& Plane2D::imshow(std::span<const double> data, int rows, int cols,
                             HeatmapOptions opts) {
        const Range xr{0.0, static_cast<double>(std::max(cols, 1))};
        const Range yr{0.0, static_cast<double>(std::max(rows, 1))};
        return heatmap(data, rows, cols, xr, yr, std::move(opts));
    }

    Plane2D& Plane2D::fill(std::span<const double> x, std::span<const double> y1,
                                   std::span<const double> y2, FillOptions opts) {
        d->sheet.ingest_fill(x, y1, y2, std::move(opts), "Plane2D::fill");
        return *this;
    }

    Plane2D& Plane2D::fill(std::span<const double> x, std::span<const double> y1,
                                   double y2, FillOptions opts) {
        const std::vector<double> base(x.size(), y2);
        d->sheet.ingest_fill(x, y1, base, std::move(opts), "Plane2D::fill");
        return *this;
    }

    Plane2D& Plane2D::stack(std::span<const double> x, std::span<const std::vector<double>> ys,
                            StackOptions opts) {
        d->sheet.ingest_stack(x, ys, opts, "Plane2D::stack");
        return *this;
    }

    Plane2D& Plane2D::set_offset(double offset) {
        if (!std::isfinite(offset))
            throw std::invalid_argument("Plane2D::set_offset: offset must be finite");
        d->offset = offset;
        d->placement_stamp = next_snapshot_generation();
        return *this;
    }

    Plane2D& Plane2D::set_alpha(float alpha) {
        d->opts.alpha = std::clamp(alpha, 0.0f, 1.0f);
        d->placement_stamp = next_snapshot_generation();
        return *this;
    }

    // Clears the plane's data (its objects go, with their ids); placement, the
    // figure's mathtext setting and the parent's color cycle stay.
    Plane2D& Plane2D::cla() {
        const bool mathtext = d->sheet.mathtext;
        std::shared_ptr<ColorCycle> cycle = std::move(d->sheet.cycle);
        d->sheet = Axes::Impl{};
        d->sheet.mathtext = mathtext;
        d->sheet.cycle = std::move(cycle);
        return *this;
    }

    PlaneOrientation Plane2D::orientation() const { return d->orient; }
    double Plane2D::offset() const { return d->offset; }

    std::size_t Plane2D::line_count() const      { return d->sheet.lines.size(); }
    std::size_t Plane2D::scatter_count() const   { return d->sheet.scatters.size(); }
    std::size_t Plane2D::scatter_z_count() const { return d->sheet.scatter_z.size(); }
    std::size_t Plane2D::bar_count() const       { return d->sheet.bars.size(); }
    std::size_t Plane2D::heatmap_count() const   { return d->sheet.heatmaps.size(); }
    std::size_t Plane2D::fill_count() const      { return d->sheet.fills.size(); }

    LineData Plane2D::line_data(std::size_t i) const {
        return read_back::to_data(read_back::at(d->sheet.lines, i, "Plane2D::line_data"));
    }
    ScatterData Plane2D::scatter_data(std::size_t i) const {
        return read_back::to_data(read_back::at(d->sheet.scatters, i, "Plane2D::scatter_data"));
    }
    ScatterZData Plane2D::scatter_z_data(std::size_t i) const {
        return read_back::to_data(read_back::at(d->sheet.scatter_z, i, "Plane2D::scatter_z_data"));
    }
    BarData Plane2D::bar_data(std::size_t i) const {
        return read_back::to_data(read_back::at(d->sheet.bars, i, "Plane2D::bar_data"));
    }
    HeatmapData Plane2D::heatmap_data(std::size_t i) const {
        return read_back::to_data(read_back::at(d->sheet.heatmaps, i, "Plane2D::heatmap_data"));
    }
    FillData Plane2D::fill_data(std::size_t i) const {
        return read_back::to_data(read_back::at(d->sheet.fills, i, "Plane2D::fill_data"));
    }

    Plane2D& Plane2D::set_line_data(std::size_t i, const LineData& data) {
        d->sheet.set_line_data(i, data.x, data.y, "Plane2D::set_line_data");
        return *this;
    }
    Plane2D& Plane2D::set_line_data(std::size_t i, std::span<const double> x,
                                    std::span<const double> y) {
        d->sheet.set_line_data(i, x, y, "Plane2D::set_line_data");
        return *this;
    }
    Plane2D& Plane2D::set_scatter_data(std::size_t i, const ScatterData& data) {
        d->sheet.set_scatter_data(i, data.x, data.y, &data.sizes, "Plane2D::set_scatter_data");
        return *this;
    }
    Plane2D& Plane2D::set_scatter_data(std::size_t i, std::span<const double> x,
                                       std::span<const double> y) {
        d->sheet.set_scatter_data(i, x, y, nullptr, "Plane2D::set_scatter_data");
        return *this;
    }
    Plane2D& Plane2D::set_scatter_z_data(std::size_t i, const ScatterZData& data) {
        d->sheet.set_scatter_z_data(i, data.x, data.y, data.z, &data.sizes, "Plane2D::set_scatter_z_data");
        return *this;
    }
    Plane2D& Plane2D::set_scatter_z_data(std::size_t i, std::span<const double> x,
                                         std::span<const double> y, std::span<const double> z) {
        d->sheet.set_scatter_z_data(i, x, y, z, nullptr, "Plane2D::set_scatter_z_data");
        return *this;
    }
    Plane2D& Plane2D::set_bar_data(std::size_t i, const BarData& data) {
        d->sheet.set_bar_data(i, data.x, data.height, &data.bottom, "Plane2D::set_bar_data");
        return *this;
    }
    Plane2D& Plane2D::set_bar_data(std::size_t i, std::span<const double> x,
                                   std::span<const double> height) {
        d->sheet.set_bar_data(i, x, height, nullptr, "Plane2D::set_bar_data");
        return *this;
    }
    Plane2D& Plane2D::set_fill_data(std::size_t i, const FillData& data) {
        d->sheet.set_fill_data(i, data.x, data.y1, data.y2, &data.exclude, "Plane2D::set_fill_data");
        return *this;
    }
    Plane2D& Plane2D::set_fill_data(std::size_t i, std::span<const double> x,
                                    std::span<const double> y1, std::span<const double> y2) {
        d->sheet.set_fill_data(i, x, y1, y2, nullptr, "Plane2D::set_fill_data");
        return *this;
    }
    Plane2D& Plane2D::set_heatmap_data(std::size_t i, const HeatmapData& data) {
        d->sheet.set_heatmap_data(i, data.data, data.rows, data.cols, data.xrange, data.yrange,
                                  "Plane2D::set_heatmap_data");
        return *this;
    }
    Plane2D& Plane2D::set_heatmap_data(std::size_t i, std::span<const double> data, int rows,
                                       int cols, Range xrange, Range yrange) {
        d->sheet.set_heatmap_data(i, data, rows, cols, xrange, yrange, "Plane2D::set_heatmap_data");
        return *this;
    }
} // namespace sextant
