#include "polar_axes_impl.h"
#include "object_remove.h"
#include "polar_geometry.h"
#include "read_back.h"
#include "rich_text.h"
#include "tick.h"
#include "ticker.h"
#include <cmath>
#include <stdexcept>
#include <string>

namespace sextant {

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
    // (θ, r) is a pair: a data point, or a place in the frame's box.
    void check_text_spaces(const Pos& theta, const Pos& r, const char* who) {
        if (theta.space != r.space)
            throw std::invalid_argument(std::string("PolarAxes::") + who
                                        + ": theta and r must be in the same space (both data, both "
                                          "Pos::fraction() or both Pos::pixels())");
    }
}

PolarAxes::PolarAxes() : d(std::make_unique<Impl>()) {}

PolarAxes::~PolarAxes() = default;

PolarAxes& PolarAxes::plot(std::span<const double> theta, std::span<const double> r, LineOptions opts) {
    d->sheet.ingest_line(theta, r, ErrorBar{}, std::move(opts), "PolarAxes::plot");
    return *this;
}

PolarAxes& PolarAxes::plot(std::span<const double> r, LineOptions opts) {
    std::vector<double> theta(r.size());
    for (std::size_t i = 0; i < theta.size(); ++i) theta[i] = static_cast<double>(i);
    return plot(theta, r, std::move(opts));
}

PolarAxes& PolarAxes::scatter(std::span<const double> theta, std::span<const double> r, ScatterOptions opts) {
    d->sheet.ingest_scatter(theta, r, ErrorBar{}, std::move(opts), "PolarAxes::scatter");
    return *this;
}

PolarAxes& PolarAxes::scatter_z(std::span<const double> theta, std::span<const double> r,
                                std::span<const double> z, ScatterZOptions opts) {
    d->sheet.ingest_scatter_z(theta, r, z, ErrorBar{}, std::move(opts), "PolarAxes::scatter_z");
    return *this;
}

PolarAxes& PolarAxes::bar(std::span<const double> theta, std::span<const double> height, BarOptions opts) {
    if (opts.orientation != Orientation::Vertical)
        throw std::invalid_argument("PolarAxes::bar: a polar bar runs along r (Orientation::Vertical only)");
    d->sheet.ingest_bar(theta, height, ErrorBar{}, std::move(opts), "PolarAxes::bar");
    return *this;
}

PolarAxes& PolarAxes::fill(std::span<const double> theta, std::span<const double> r1, std::span<const double> r2,
                           FillOptions opts) {
    d->sheet.ingest_fill(theta, r1, r2, std::move(opts), "PolarAxes::fill");
    return *this;
}

PolarAxes& PolarAxes::fill(std::span<const double> theta, std::span<const double> r1, double r2, FillOptions opts) {
    const std::vector<double> base(theta.size(), r2);
    d->sheet.ingest_fill(theta, r1, base, std::move(opts), "PolarAxes::fill");
    return *this;
}

PolarAxes& PolarAxes::shape_poly(std::span<const double> theta, std::span<const double> r, PolyOptions opts) {
    if (opts.space != Coords::Data)
        throw std::invalid_argument("PolarAxes::shape_poly: a polar polygon's vertices are (theta, r) data "
                                    "(Coords::Data only)");
    d->sheet.ingest_poly(theta, r, std::move(opts), "PolarAxes::shape_poly");
    return *this;
}

PolarAxes& PolarAxes::text(std::string_view s, Pos theta, Pos r, TextOptions opts) {
    check_text_spaces(theta, r, "PolarAxes::text");
    TextContent c;
    c.text = s;
    c.x = theta;
    c.y = r;
    d->sheet.ingest_text(std::move(c), { std::move(opts), ArrowOptions{} }, "PolarAxes::text");
    return *this;
}

PolarAxes& PolarAxes::annotate(double theta, double r, std::string_view s, Pos ttheta, Pos tr,
                               TextOptions opts, ArrowOptions arrow) {
    check_text_spaces(ttheta, tr, "PolarAxes::annotate");
    TextContent c;
    c.text = s;
    c.x = ttheta;
    c.y = tr;
    c.arrow = true;
    c.px = theta;
    c.py = r;
    d->sheet.ingest_text(std::move(c), { std::move(opts), std::move(arrow) }, "PolarAxes::annotate");
    return *this;
}

std::size_t PolarAxes::line_count() const { return d->sheet.lines.size(); }
LineData PolarAxes::line_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->sheet.lines, i, "PolarAxes::line_data"));
}
PolarAxes& PolarAxes::set_line_data(std::size_t i, const LineData& data) {
    d->sheet.set_line_data(i, data.x, data.y, "PolarAxes::set_line_data");
    return *this;
}
PolarAxes& PolarAxes::set_line_data(std::size_t i, std::span<const double> theta, std::span<const double> r) {
    d->sheet.set_line_data(i, theta, r, "PolarAxes::set_line_data");
    return *this;
}
PolarAxes& PolarAxes::set_line_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->sheet.lines, i, visible, "PolarAxes::set_line_visible");
    return *this;
}
bool PolarAxes::line_visible(std::size_t i) const {
    return read_back::visible(d->sheet.lines, i, "PolarAxes::line_visible");
}

std::size_t PolarAxes::scatter_count() const { return d->sheet.scatters.size(); }
ScatterData PolarAxes::scatter_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->sheet.scatters, i, "PolarAxes::scatter_data"));
}
PolarAxes& PolarAxes::set_scatter_data(std::size_t i, const ScatterData& data) {
    d->sheet.set_scatter_data(i, data.x, data.y, &data.sizes, "PolarAxes::set_scatter_data");
    return *this;
}
PolarAxes& PolarAxes::set_scatter_data(std::size_t i, std::span<const double> theta, std::span<const double> r) {
    d->sheet.set_scatter_data(i, theta, r, nullptr, "PolarAxes::set_scatter_data");
    return *this;
}
PolarAxes& PolarAxes::set_scatter_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->sheet.scatters, i, visible, "PolarAxes::set_scatter_visible");
    return *this;
}
bool PolarAxes::scatter_visible(std::size_t i) const {
    return read_back::visible(d->sheet.scatters, i, "PolarAxes::scatter_visible");
}

std::size_t PolarAxes::scatter_z_count() const { return d->sheet.scatter_z.size(); }
ScatterZData PolarAxes::scatter_z_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->sheet.scatter_z, i, "PolarAxes::scatter_z_data"));
}
PolarAxes& PolarAxes::set_scatter_z_data(std::size_t i, const ScatterZData& data) {
    d->sheet.set_scatter_z_data(i, data.x, data.y, data.z, &data.sizes, "PolarAxes::set_scatter_z_data");
    return *this;
}
PolarAxes& PolarAxes::set_scatter_z_data(std::size_t i, std::span<const double> theta, std::span<const double> r,
                                         std::span<const double> z) {
    d->sheet.set_scatter_z_data(i, theta, r, z, nullptr, "PolarAxes::set_scatter_z_data");
    return *this;
}
PolarAxes& PolarAxes::set_scatter_z_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->sheet.scatter_z, i, visible, "PolarAxes::set_scatter_z_visible");
    return *this;
}
bool PolarAxes::scatter_z_visible(std::size_t i) const {
    return read_back::visible(d->sheet.scatter_z, i, "PolarAxes::scatter_z_visible");
}

std::size_t PolarAxes::bar_count() const { return d->sheet.bars.size(); }
BarData PolarAxes::bar_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->sheet.bars, i, "PolarAxes::bar_data"));
}
PolarAxes& PolarAxes::set_bar_data(std::size_t i, const BarData& data) {
    d->sheet.set_bar_data(i, data.x, data.height, &data.bottom, "PolarAxes::set_bar_data");
    return *this;
}
PolarAxes& PolarAxes::set_bar_data(std::size_t i, std::span<const double> theta, std::span<const double> height) {
    d->sheet.set_bar_data(i, theta, height, nullptr, "PolarAxes::set_bar_data");
    return *this;
}
PolarAxes& PolarAxes::set_bar_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->sheet.bars, i, visible, "PolarAxes::set_bar_visible");
    return *this;
}
bool PolarAxes::bar_visible(std::size_t i) const {
    return read_back::visible(d->sheet.bars, i, "PolarAxes::bar_visible");
}

std::size_t PolarAxes::fill_count() const { return d->sheet.fills.size(); }
FillData PolarAxes::fill_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->sheet.fills, i, "PolarAxes::fill_data"));
}
PolarAxes& PolarAxes::set_fill_data(std::size_t i, const FillData& data) {
    d->sheet.set_fill_data(i, data.x, data.y1, data.y2, &data.exclude, "PolarAxes::set_fill_data");
    return *this;
}
PolarAxes& PolarAxes::set_fill_data(std::size_t i, std::span<const double> theta, std::span<const double> r1,
                                    std::span<const double> r2) {
    d->sheet.set_fill_data(i, theta, r1, r2, nullptr, "PolarAxes::set_fill_data");
    return *this;
}
PolarAxes& PolarAxes::set_fill_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->sheet.fills, i, visible, "PolarAxes::set_fill_visible");
    return *this;
}
bool PolarAxes::fill_visible(std::size_t i) const {
    return read_back::visible(d->sheet.fills, i, "PolarAxes::fill_visible");
}

std::size_t PolarAxes::shape_poly_count() const { return d->sheet.polys.size(); }
PolyData PolarAxes::shape_poly_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->sheet.polys, i, "PolarAxes::shape_poly_data"));
}
PolarAxes& PolarAxes::set_shape_poly_data(std::size_t i, const PolyData& data) {
    d->sheet.set_poly_data(i, data.x, data.y, "PolarAxes::set_shape_poly_data");
    return *this;
}
PolarAxes& PolarAxes::set_shape_poly_data(std::size_t i, std::span<const double> theta, std::span<const double> r) {
    d->sheet.set_poly_data(i, theta, r, "PolarAxes::set_shape_poly_data");
    return *this;
}
PolarAxes& PolarAxes::set_shape_poly_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->sheet.polys, i, visible, "PolarAxes::set_shape_poly_visible");
    return *this;
}
bool PolarAxes::shape_poly_visible(std::size_t i) const {
    return read_back::visible(d->sheet.polys, i, "PolarAxes::shape_poly_visible");
}

std::size_t PolarAxes::text_count() const { return d->sheet.texts.size(); }
TextData PolarAxes::text_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->sheet.texts, i, "PolarAxes::text_data"));
}
PolarAxes& PolarAxes::set_text_data(std::size_t i, const TextData& data) {
    check_text_spaces(data.x, data.y, "set_text_data");
    d->sheet.set_text_data(i, read_back::content_of(data), "PolarAxes::set_text_data");
    return *this;
}
PolarAxes& PolarAxes::set_text_data(std::size_t i, std::string_view s, Pos theta, Pos r) {
    check_text_spaces(theta, r, "set_text_data");
    TextContent c = read_back::at(d->sheet.texts, i, "PolarAxes::set_text_data").content;
    c.text = s;
    c.x = theta;
    c.y = r;
    d->sheet.set_text_data(i, std::move(c), "PolarAxes::set_text_data");
    return *this;
}
PolarAxes& PolarAxes::set_text_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->sheet.texts, i, visible, "PolarAxes::set_text_visible");
    return *this;
}
bool PolarAxes::text_visible(std::size_t i) const {
    return read_back::visible(d->sheet.texts, i, "PolarAxes::text_visible");
}

ObjectId PolarAxes::last_object() const { return d->sheet.last_id; }
bool PolarAxes::remove(ObjectId id) { return object_remove::remove(d->sheet, id, "PolarAxes::remove"); }
std::optional<ObjectLocation> PolarAxes::find_object(ObjectId id) const {
    return to_location(d->sheet.find_object(id));
}
bool PolarAxes::set_zorder(ObjectId id, double zorder) {
    return read_back::set_zorder(d->sheet, id, zorder, "PolarAxes::set_zorder");
}
std::optional<double> PolarAxes::zorder(ObjectId id) const {
    return read_back::zorder(d->sheet, id, "PolarAxes::zorder");
}

PolarAxes& PolarAxes::set_title(std::string_view text, float fontsize) {
    Axes::Impl& a = d->sheet;
    warn_math(a.mathtext, "PolarAxes::set_title", text);
    a.title = text;
    a.title_stamps.title = next_snapshot_generation();
    a.axes_style.title_fontsize = fontsize;
    return *this;
}
std::string PolarAxes::title() const { return d->sheet.title; }
PolarAxes& PolarAxes::legend(LegendOptions opts) {
    d->sheet.legend_enabled = true;
    d->sheet.legend_opts = opts;
    d->sheet.style_stamps.legend = next_snapshot_generation();
    return *this;
}
PolarAxes& PolarAxes::set_color_cycle(std::vector<Color> colors) {
    d->sheet.cycle->set(std::move(colors));
    return *this;
}
PolarAxes& PolarAxes::grid(bool enable, GridOptions opts) {
    d->sheet.grid_enabled = enable;
    d->sheet.grid_opts = opts;
    d->sheet.style_stamps.grid = next_snapshot_generation();
    return *this;
}
PolarAxes& PolarAxes::set_axes_style(AxesStyle opts) {
    d->sheet.axes_style = opts;
    d->sheet.style_stamps.style = next_snapshot_generation();
    return *this;
}
PolarAxes& PolarAxes::set_colorbar_style(ColorbarOptions opts) {
    d->sheet.colorbar_opts = std::move(opts);
    d->sheet.style_stamps.colorbar = next_snapshot_generation();
    return *this;
}
// Objects (with their ids), view, style and look go; the figure's settings,
// the axes' id and the color cycle's object (reset in place) stay.
PolarAxes& PolarAxes::cla() {
    const bool mathtext = d->sheet.mathtext;
    const std::uint64_t owner = d->sheet.owner;
    std::shared_ptr<ColorCycle> cycle = std::move(d->sheet.cycle);
    const ObjectId id = d->id;
    *d = Impl{};
    d->sheet.mathtext = mathtext;
    d->sheet.owner = owner;
    *cycle = ColorCycle{};
    d->sheet.cycle = std::move(cycle);
    d->id = id;
    // Stamps back to 0 (Impl{}), as Axes::cla(): a panel edit of the view or
    // style made before it still lands; a setter called after it is newer.
    return *this;
}

PolarAxes& PolarAxes::set_rlim(double lo, double hi) {
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo == hi)
        throw std::invalid_argument("PolarAxes::set_rlim: the limits must be finite and differ ("
                                    + std::to_string(lo) + ", " + std::to_string(hi) + ")");
    d->view.rauto = false;
    d->view.rmin = lo;
    d->view.rmax = hi;
    d->stamps.view = next_snapshot_generation();
    return *this;
}
PolarAxes& PolarAxes::set_rmin(double lo) { return set_rlim(lo, rlim().hi); }
PolarAxes& PolarAxes::set_rmax(double hi) { return set_rlim(rlim().lo, hi); }
Range PolarAxes::rlim() const { return polar_rlim(d->build_snapshot()); }

PolarAxes& PolarAxes::set_rticks(std::span<const double> positions, std::vector<std::string> labels) {
    for (const auto& l : labels) warn_math(d->sheet.mathtext, "PolarAxes::set_rticks", l);
    if (positions.empty()) d->style.rticks_override.reset();
    else                   d->style.rticks_override = make_tick_override(positions, labels);
    d->stamps.style = next_snapshot_generation();
    return *this;
}
PolarAxes& PolarAxes::set_rtick_format(std::string_view fmt) {
    if (!fmt.empty() && !tick_format_ok(fmt))
        throw std::invalid_argument("PolarAxes::set_rtick_format: \"" + std::string(fmt)
                                    + "\" is not one %f/%e/%g conversion and text");
    warn_math(d->sheet.mathtext, "PolarAxes::set_rtick_format", fmt);
    d->style.rtick_format = fmt;
    d->stamps.style = next_snapshot_generation();
    return *this;
}
PolarAxes& PolarAxes::set_rlabel_position(double degrees) {
    check_finite(degrees, "set_rlabel_position");
    d->style.rlabel_position = degrees;
    d->stamps.style = next_snapshot_generation();
    return *this;
}
double PolarAxes::rlabel_position() const { return d->style.rlabel_position; }

PolarAxes& PolarAxes::set_thetagrids(std::span<const double> degrees, std::vector<std::string> labels) {
    for (const auto& l : labels) warn_math(d->sheet.mathtext, "PolarAxes::set_thetagrids", l);
    if (degrees.empty()) d->style.thetagrids_override.reset();
    else                 d->style.thetagrids_override = make_tick_override(degrees, labels);
    d->stamps.style = next_snapshot_generation();
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
    d->view.theta_offset = degrees;
    d->stamps.view = next_snapshot_generation();
    return *this;
}
double PolarAxes::theta_offset() const { return d->view.theta_offset; }
PolarAxes& PolarAxes::set_theta_direction(int direction) {
    if (direction != 1 && direction != -1)
        throw std::invalid_argument("PolarAxes::set_theta_direction: the direction must be 1 or -1, not "
                                    + std::to_string(direction));
    d->view.direction = direction;
    d->stamps.view = next_snapshot_generation();
    return *this;
}
int PolarAxes::theta_direction() const { return d->view.direction; }

PolarAxes& PolarAxes::set_thetamin(double degrees) { return set_thetalim(degrees, d->view.thetamax); }
PolarAxes& PolarAxes::set_thetamax(double degrees) { return set_thetalim(d->view.thetamin, degrees); }
PolarAxes& PolarAxes::set_thetalim(double min_degrees, double max_degrees) {
    check_thetalim(min_degrees, max_degrees, "set_thetalim");
    d->view.thetamin = min_degrees;
    d->view.thetamax = max_degrees;
    d->stamps.view = next_snapshot_generation();
    return *this;
}
Range PolarAxes::thetalim() const { return {d->view.thetamin, d->view.thetamax}; }

} // namespace sextant
