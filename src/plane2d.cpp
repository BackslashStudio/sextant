#include "plane2d_impl.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sextant {

Plane2D::Plane2D(PlaneOrientation orient, double offset, Plane2DOptions opts)
    : d(std::make_unique<Impl>()) {
    d->orient = orient;
    d->offset = offset;
    d->opts   = opts;
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

Plane2D& Plane2D::heatmap(std::span<const float> data, int rows, int cols,
                          Range xrange, Range yrange, HeatmapOptions opts) {
    d->sheet.ingest_heatmap(data, rows, cols, xrange, yrange, std::move(opts),
                            "Plane2D::heatmap");
    return *this;
}

Plane2D& Plane2D::imshow(std::span<const float> data, int rows, int cols,
                         HeatmapOptions opts) {
    const Range xr{ 0.0, static_cast<double>(std::max(cols, 1)) };
    const Range yr{ 0.0, static_cast<double>(std::max(rows, 1)) };
    return heatmap(data, rows, cols, xr, yr, std::move(opts));
}

Plane2D& Plane2D::set_offset(double offset) {
    if (!std::isfinite(offset))
        throw std::invalid_argument("Plane2D::set_offset: offset must be finite");
    d->offset = offset;
    return *this;
}

Plane2D& Plane2D::set_alpha(float alpha) {
    d->opts.alpha = std::clamp(alpha, 0.0f, 1.0f);
    return *this;
}

// Clears what is *on* the plane, not where the plane is: an offset and an
// orientation are placement, and cla() has always meant "drop the data".
Plane2D& Plane2D::cla() {
    d->sheet = Axes::Impl{};
    return *this;
}

PlaneOrientation Plane2D::orientation() const { return d->orient; }
double           Plane2D::offset()      const { return d->offset; }

} // namespace sextant
