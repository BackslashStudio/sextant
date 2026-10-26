#include "sextant/polar_axes.h"
#include "axes_impl.h"
#include "polar_geometry.h"
#include <cmath>
#include <stdexcept>
#include <string>

namespace sextant {

// A PolarAxes is its sheet: a hidden Axes whose Impl carries the PolarState.
// Everything but the frame forwards to it.
struct PolarAxes::Impl {
    std::shared_ptr<Axes> sheet;

    Axes::Impl& ax() const { return *sheet->d; }
    PolarState& st() const { return *ax().polar; }
    void stamp() const { ax().style_stamps.polar = next_snapshot_generation(); }

    // A new polar axes' defaults on the sheet: the frame, and the grid on.
    void defaults() const {
        ax().polar = PolarState{};
        ax().grid_enabled = true;
    }
};

namespace {
    void check_finite(double v, const char* who) {
        if (!std::isfinite(v))
            throw std::invalid_argument(std::string("PolarAxes::") + who + ": the angle must be finite");
    }
    void check_thetalim(double lo, double hi, const char* who) {
        if (!std::isfinite(lo) || !std::isfinite(hi))
            throw std::invalid_argument(std::string("PolarAxes::") + who + ": the limits must be finite");
        if (!(lo < hi) || hi - lo > 360.0)
            throw std::invalid_argument(std::string("PolarAxes::") + who + ": thetamin ("
                                        + std::to_string(lo) + ") must be below thetamax ("
                                        + std::to_string(hi) + ") and at most 360 degrees from it");
    }
}

PolarAxes::PolarAxes(std::shared_ptr<Axes> sheet) : d(std::make_unique<Impl>()) {
    d->sheet = std::move(sheet);
    d->defaults();
}

PolarAxes::~PolarAxes() = default;

PolarAxes& PolarAxes::plot(std::span<const double> theta, std::span<const double> r, LineOptions opts) {
    d->ax().ingest_line(theta, r, ErrorBar{}, std::move(opts), "plot");
    return *this;
}

PolarAxes& PolarAxes::plot(std::span<const double> r, LineOptions opts) {
    std::vector<double> theta(r.size());
    for (std::size_t i = 0; i < theta.size(); ++i) theta[i] = static_cast<double>(i);
    return plot(theta, r, std::move(opts));
}

PolarAxes& PolarAxes::scatter(std::span<const double> theta, std::span<const double> r, ScatterOptions opts) {
    d->ax().ingest_scatter(theta, r, ErrorBar{}, std::move(opts), "scatter");
    return *this;
}

PolarAxes& PolarAxes::scatter_z(std::span<const double> theta, std::span<const double> r,
                                std::span<const double> z, ScatterZOptions opts) {
    d->ax().ingest_scatter_z(theta, r, z, ErrorBar{}, std::move(opts), "scatter_z");
    return *this;
}

PolarAxes& PolarAxes::bar(std::span<const double> theta, std::span<const double> height, BarOptions opts) {
    if (opts.orientation != Orientation::Vertical)
        throw std::invalid_argument("PolarAxes::bar: a polar bar runs along r (Orientation::Vertical only)");
    d->ax().ingest_bar(theta, height, ErrorBar{}, std::move(opts), "bar");
    return *this;
}

std::size_t PolarAxes::line_count() const { return d->sheet->line_count(); }
LineData PolarAxes::line_data(std::size_t i) const { return d->sheet->line_data(i); }
PolarAxes& PolarAxes::set_line_data(std::size_t i, const LineData& data) {
    d->sheet->set_line_data(i, data);
    return *this;
}
PolarAxes& PolarAxes::set_line_data(std::size_t i, std::span<const double> theta, std::span<const double> r) {
    d->sheet->set_line_data(i, theta, r);
    return *this;
}
PolarAxes& PolarAxes::set_line_visible(std::size_t i, bool visible) {
    d->sheet->set_line_visible(i, visible);
    return *this;
}
bool PolarAxes::line_visible(std::size_t i) const { return d->sheet->line_visible(i); }

std::size_t PolarAxes::scatter_count() const { return d->sheet->scatter_count(); }
ScatterData PolarAxes::scatter_data(std::size_t i) const { return d->sheet->scatter_data(i); }
PolarAxes& PolarAxes::set_scatter_data(std::size_t i, const ScatterData& data) {
    d->sheet->set_scatter_data(i, data);
    return *this;
}
PolarAxes& PolarAxes::set_scatter_data(std::size_t i, std::span<const double> theta, std::span<const double> r) {
    d->sheet->set_scatter_data(i, theta, r);
    return *this;
}
PolarAxes& PolarAxes::set_scatter_visible(std::size_t i, bool visible) {
    d->sheet->set_scatter_visible(i, visible);
    return *this;
}
bool PolarAxes::scatter_visible(std::size_t i) const { return d->sheet->scatter_visible(i); }

std::size_t PolarAxes::scatter_z_count() const { return d->sheet->scatter_z_count(); }
ScatterZData PolarAxes::scatter_z_data(std::size_t i) const { return d->sheet->scatter_z_data(i); }
PolarAxes& PolarAxes::set_scatter_z_data(std::size_t i, const ScatterZData& data) {
    d->sheet->set_scatter_z_data(i, data);
    return *this;
}
PolarAxes& PolarAxes::set_scatter_z_data(std::size_t i, std::span<const double> theta, std::span<const double> r,
                                         std::span<const double> z) {
    d->sheet->set_scatter_z_data(i, theta, r, z);
    return *this;
}
PolarAxes& PolarAxes::set_scatter_z_visible(std::size_t i, bool visible) {
    d->sheet->set_scatter_z_visible(i, visible);
    return *this;
}
bool PolarAxes::scatter_z_visible(std::size_t i) const { return d->sheet->scatter_z_visible(i); }

std::size_t PolarAxes::bar_count() const { return d->sheet->bar_count(); }
BarData PolarAxes::bar_data(std::size_t i) const { return d->sheet->bar_data(i); }
PolarAxes& PolarAxes::set_bar_data(std::size_t i, const BarData& data) {
    d->sheet->set_bar_data(i, data);
    return *this;
}
PolarAxes& PolarAxes::set_bar_data(std::size_t i, std::span<const double> theta, std::span<const double> height) {
    d->sheet->set_bar_data(i, theta, height);
    return *this;
}
PolarAxes& PolarAxes::set_bar_visible(std::size_t i, bool visible) {
    d->sheet->set_bar_visible(i, visible);
    return *this;
}
bool PolarAxes::bar_visible(std::size_t i) const { return d->sheet->bar_visible(i); }

ObjectId PolarAxes::last_object() const { return d->sheet->last_object(); }
bool PolarAxes::remove(ObjectId id) { return d->sheet->remove(id); }
std::optional<ObjectLocation> PolarAxes::find_object(ObjectId id) const { return d->sheet->find_object(id); }
bool PolarAxes::set_zorder(ObjectId id, double zorder) { return d->sheet->set_zorder(id, zorder); }
std::optional<double> PolarAxes::zorder(ObjectId id) const { return d->sheet->zorder(id); }

PolarAxes& PolarAxes::set_title(std::string_view text, float fontsize) {
    d->sheet->set_title(text, fontsize);
    return *this;
}
std::string PolarAxes::title() const { return d->sheet->title(); }
PolarAxes& PolarAxes::legend(LegendOptions opts) {
    d->sheet->legend(opts);
    return *this;
}
PolarAxes& PolarAxes::set_color_cycle(std::vector<Color> colors) {
    d->sheet->set_color_cycle(std::move(colors));
    return *this;
}
PolarAxes& PolarAxes::grid(bool enable, GridOptions opts) {
    d->sheet->grid(enable, opts);
    return *this;
}
PolarAxes& PolarAxes::set_axes_style(AxesStyle opts) {
    d->sheet->set_axes_style(opts);
    return *this;
}
PolarAxes& PolarAxes::set_colorbar_style(ColorbarOptions opts) {
    d->sheet->set_colorbar_style(std::move(opts));
    return *this;
}
PolarAxes& PolarAxes::cla() {
    d->sheet->cla();
    d->defaults();
    return *this;
}

PolarAxes& PolarAxes::set_rlim(double lo, double hi) {
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo == hi)
        throw std::invalid_argument("PolarAxes::set_rlim: the limits must be finite and differ ("
                                    + std::to_string(lo) + ", " + std::to_string(hi) + ")");
    d->sheet->set_ylim(lo, hi);
    return *this;
}
PolarAxes& PolarAxes::set_rmin(double lo) { return set_rlim(lo, rlim().hi); }
PolarAxes& PolarAxes::set_rmax(double hi) { return set_rlim(rlim().lo, hi); }
Range PolarAxes::rlim() const { return polar_rlim(d->ax().build_snapshot()); }

PolarAxes& PolarAxes::set_rticks(std::span<const double> positions, std::vector<std::string> labels) {
    d->sheet->set_yticks(positions, std::move(labels));
    return *this;
}
PolarAxes& PolarAxes::set_rtick_format(std::string_view fmt) {
    d->sheet->set_ytick_format(fmt);
    return *this;
}
PolarAxes& PolarAxes::set_rlabel_position(double degrees) {
    check_finite(degrees, "set_rlabel_position");
    d->st().rlabel_position = degrees;
    d->stamp();
    return *this;
}
double PolarAxes::rlabel_position() const { return d->st().rlabel_position; }

PolarAxes& PolarAxes::set_thetagrids(std::span<const double> degrees, std::vector<std::string> labels) {
    d->sheet->set_xticks(degrees, std::move(labels));
    return *this;
}

PolarAxes& PolarAxes::set_theta_zero_location(ThetaZero loc, double offset) {
    check_finite(offset, "set_theta_zero_location");
    double base = 0.0;
    switch (loc) {
        case ThetaZero::E:  base = 0.0; break;
        case ThetaZero::NE: base = 45.0; break;
        case ThetaZero::N:  base = 90.0; break;
        case ThetaZero::NW: base = 135.0; break;
        case ThetaZero::W:  base = 180.0; break;
        case ThetaZero::SW: base = 225.0; break;
        case ThetaZero::S:  base = 270.0; break;
        case ThetaZero::SE: base = 315.0; break;
    }
    return set_theta_offset(base + offset);
}
PolarAxes& PolarAxes::set_theta_offset(double degrees) {
    check_finite(degrees, "set_theta_offset");
    d->st().theta_offset = degrees;
    d->stamp();
    return *this;
}
double PolarAxes::theta_offset() const { return d->st().theta_offset; }
PolarAxes& PolarAxes::set_theta_direction(int direction) {
    if (direction != 1 && direction != -1)
        throw std::invalid_argument("PolarAxes::set_theta_direction: the direction must be 1 or -1, not "
                                    + std::to_string(direction));
    d->st().direction = direction;
    d->stamp();
    return *this;
}
int PolarAxes::theta_direction() const { return d->st().direction; }

PolarAxes& PolarAxes::set_thetamin(double degrees) { return set_thetalim(degrees, d->st().thetamax); }
PolarAxes& PolarAxes::set_thetamax(double degrees) { return set_thetalim(d->st().thetamin, degrees); }
PolarAxes& PolarAxes::set_thetalim(double min_degrees, double max_degrees) {
    check_thetalim(min_degrees, max_degrees, "set_thetalim");
    d->st().thetamin = min_degrees;
    d->st().thetamax = max_degrees;
    d->stamp();
    return *this;
}
Range PolarAxes::thetalim() const { return {d->st().thetamin, d->st().thetamax}; }

} // namespace sextant
