#include "axes_impl.h"
#include "object_remove.h"
#include "axis_limits.h"
#include "color_norm.h"
#include "delaunay.h"
#include "hist2d.h"
#include "contour_plot.h"
#include "read_back.h"
#include "rich_text.h"
#include "quiver_geometry.h"
#include "stack_curves.h"
#include "ticker.h"
#include <stdexcept>
#include <string>
#include <tuple>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdio>

namespace sextant {

Axes::Axes() : d(std::make_unique<Impl>()) {}
Axes::~Axes() {
    d->leave_groups();
    // A twin the caller still holds outlives its host as a plain axes.
    for (auto* t : {&d->twin_x, &d->twin_y})
        if (*t) (*t)->d->host = nullptr;
}

// ---------------------------------------------------------------------------
// Share groups (sharex()/sharey())
// ---------------------------------------------------------------------------

void Axes::Impl::sync_shared_x() const {
    if (xshare)
        for (Impl* m : xshare->members)
            if (m != this) copy_shared_x(*m, *this);
}
void Axes::Impl::sync_shared_y() const {
    if (yshare)
        for (Impl* m : yshare->members)
            if (m != this) copy_shared_y(*m, *this);
}
void Axes::Impl::adopt_shared_x() {
    if (xshare)
        for (const Impl* m : xshare->members)
            if (m != this) { copy_shared_x(*this, *m); return; }
}
void Axes::Impl::adopt_shared_y() {
    if (yshare)
        for (const Impl* m : yshare->members)
            if (m != this) { copy_shared_y(*this, *m); return; }
}

namespace {
unsigned long long next_share_id() {
    static std::atomic<unsigned long long> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}
} // namespace

void Axes::Impl::leave_groups() {
    for (auto* g : {&xshare, &yshare}) {
        if (!*g) continue;
        std::erase((*g)->members, this);
        g->reset();
    }
}

// Joins the two groups, everyone joining taking `other`'s shared fields.
void Axes::Impl::share(Impl& other, bool x, const char* who) {
    if (this == &other) return;
    if (!owner || owner != other.owner)
        throw std::invalid_argument(std::string(who) + ": the two axes belong to different figures");
    std::shared_ptr<ShareGroup> Impl::* group = x ? &Impl::xshare : &Impl::yshare;
    const auto og = this->*group;
    auto& tg = other.*group;
    if (og && og == tg) return;

    // Everyone joining takes other's scale.
    const AxisScale& to = x ? other.xscale : other.yscale;
    const std::vector<Impl*> joining = og ? og->members : std::vector<Impl*>{this};
    // An axes new to sharing on this axis whose other axis is shared too can
    // no longer widen one under Datalim (set_aspect()).
    const auto boxed_in = [&](const Impl& m) { return m.aspect.datalim() && (x ? m.yshare : m.xshare); };
    if ((!og && boxed_in(*this)) || (!tg && boxed_in(other)))
        throw std::invalid_argument(
            std::string(who) + ": both axes of an axes with a Datalim aspect would be shared, "
            "leaving none to widen");

    if (!tg) {
        tg = std::make_shared<ShareGroup>();
        tg->id = next_share_id();
        tg->members.push_back(&other);
    }
    for (Impl* m : joining) {
        // A changed scale re-stamps the ticks lane, as set_xscale() does.
        const AxisScale& was = x ? m->xscale : m->yscale;
        if (was.kind != to.kind || was.base != to.base || was.linthresh != to.linthresh
            || was.linscale != to.linscale)
            (x ? m->style_stamps.xticks : m->style_stamps.yticks) = next_snapshot_generation();
        m->*group = tg;
        tg->members.push_back(m);
        if (x) copy_shared_x(*m, other);
        else   copy_shared_y(*m, other);
    }
    // A program change for the whole group: a pan made over the old limits on
    // any member is not replayed, and the members' stamps stay one.
    const unsigned long long st = next_snapshot_generation();
    for (Impl* m : tg->members) (x ? m->limit_stamps.x : m->limit_stamps.y) = st;
}

// ---------------------------------------------------------------------------
// Twins (twinx()/twiny())
// ---------------------------------------------------------------------------

void Axes::Impl::twin_defaults() {
    if (layer == kLayerTwinX) axes_style.show_xticks = false;
    if (layer == kLayerTwinY) axes_style.show_yticks = false;
}

std::shared_ptr<Axes> Axes::Impl::make_twin(bool x, const char* who) {
    if (host)
        throw std::invalid_argument(std::string(who) + ": this axes is a twin; twin its host instead");
    std::shared_ptr<Axes>& slot = x ? twin_x : twin_y;
    if (slot) return slot;
    auto t = std::shared_ptr<Axes>(new Axes());
    Impl& td = *t->d;
    td.owner = owner;
    td.mathtext = mathtext;
    td.host = this;
    td.layer = x ? kLayerTwinX : kLayerTwinY;
    td.cycle = cycle;
    td.twin_defaults();
    // Into the host's group on the shared axis, taking its limits and scale.
    // This throws only for a host with a Datalim aspect whose other axis is
    // shared already.
    td.share(*this, x, who);
    slot = t;
    return t;
}

namespace {
template <class... M>
void copy_fields(AxesStyle& to, const AxesStyle& from, M... m) {
    ((to.*m = from.*m), ...);
}

// One axis' own fields: what draws its marks, labels, title and frame edge.
void copy_axis_fields(AxesStyle& to, const AxesStyle& from, bool x) {
    using S = AxesStyle;
    if (x)
        copy_fields(to, from, &S::show_xticks, &S::show_xtick_labels, &S::minor_xticks, &S::xtick_color,
                    &S::xtick_length, &S::xtick_linewidth, &S::minor_xtick_length, &S::xlabel_color,
                    &S::xlabel_fontsize, &S::xtitle_color, &S::xtitle_fontsize, &S::xaxis_color);
    else
        copy_fields(to, from, &S::show_yticks, &S::show_ytick_labels, &S::minor_yticks, &S::ytick_color,
                    &S::ytick_length, &S::ytick_linewidth, &S::minor_ytick_length, &S::ylabel_color,
                    &S::ylabel_fontsize, &S::ytitle_color, &S::ytitle_fontsize, &S::yaxis_color);
}
} // namespace

// As the twin's other setters: its own axis' fields, and the shared tick and
// label ones that axis falls back on, stay on it; its shared axis' and the
// cell's (background, frame, axes title) go to the host; axis positions,
// origins and the z fields, which a twin can't use, are dropped.
void Axes::Impl::set_twin_style(const AxesStyle& s) {
    using S = AxesStyle;
    const bool own_x = layer == kLayerTwinY;
    copy_axis_fields(axes_style, s, own_x);
    copy_fields(axes_style, s, &S::tick_color, &S::tick_length, &S::tick_linewidth, &S::minor_tick_length,
                &S::label_color, &S::label_fontsize, &S::font_path);
    AxesStyle& h = host->axes_style;
    copy_axis_fields(h, s, !own_x);
    copy_fields(h, s, &S::background, &S::spine_color, &S::spine_linewidth, &S::spine_bottom, &S::spine_left,
                &S::spine_top, &S::spine_right, &S::frame_margin, &S::title_color, &S::title_fontsize);
    style_stamps.style = next_snapshot_generation();
    host->style_stamps.style = next_snapshot_generation();
}

std::shared_ptr<Axes> Axes::twinx() { return d->make_twin(true, "Axes::twinx"); }
std::shared_ptr<Axes> Axes::twiny() { return d->make_twin(false, "Axes::twiny"); }

Axes& Axes::set_aspect(double ratio, Adjustable adjustable) {
    if (!std::isfinite(ratio) || ratio < 0.0)
        throw std::invalid_argument("Axes::set_aspect: the ratio must be finite and >= 0");
    if (d->host)
        throw std::invalid_argument("Axes::set_aspect: this axes is a twin; its host's aspect sets the cell");
    const AxesAspect a{ratio, adjustable};
    if (a.datalim() && d->xshare && d->yshare)
        throw std::invalid_argument("Axes::set_aspect: Datalim widens an unshared axis, and both are "
                                    "shared (a twin shares one)");
    d->aspect = a;
    d->style_stamps.aspect = next_snapshot_generation();
    return *this;
}

Axes& Axes::sharex(Axes& other) {
    d->share(*other.d, true, "Axes::sharex");
    return *this;
}
Axes& Axes::sharey(Axes& other) {
    d->share(*other.d, false, "Axes::sharey");
    return *this;
}

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

// Data-space bar width: inter-bar spacing x the fractional opts.width.
double bar_width_for(std::span<const double> x, const BarOptions& opts) {
    const double spacing = x.size() > 1 ? std::abs(x[1] - x[0]) : 1.0;
    return spacing * static_cast<double>(opts.width);
}

void check_limits_fit(const AxisScale& s, double lo, double hi, const char* who) {
    if (!limits_fit(s, lo, hi))
        throw std::invalid_argument(std::string(who) + ": a log axis needs limits above 0");
}

// One axis' set_*scale(). Validates what the scale reads and drops set limits
// the new scale can't place.
// Takes fresh limit and tick stamps either way: a pan, zoom or panel scale
// made over the old scale is not replayed onto the new one.
void set_scale(AxisScale& slot, double lo, double hi, bool& lim_auto,
               unsigned long long& lim_stamp, unsigned long long& tick_stamp, Scale scale,
               const ScaleOptions& o, const char* who) {
    const std::string w = who;
    auto positive = [&w](double v, const char* field) {
        if (!(std::isfinite(v) && v > 0.0))
            throw std::invalid_argument(w + ": " + field + " must be finite and > 0");
    };
    if (scale != Scale::Linear) {
        if (!(std::isfinite(o.base) && o.base > 1.0))
            throw std::invalid_argument(w + ": base must be finite and > 1");
        if (scale == Scale::Symlog) {
            positive(o.linthresh, "linthresh");
            positive(o.linscale, "linscale");
        }
    }
    if (scale != Scale::Linear && scale != Scale::Log && scale != Scale::Symlog)
        throw std::invalid_argument(w + ": unknown scale");
    slot = make_axis_scale(scale, o);
    if (!lim_auto && !limits_fit(slot, lo, hi)) lim_auto = true;
    lim_stamp = next_snapshot_generation();
    tick_stamp = next_snapshot_generation();
}

// Checks a heatmap's shape and ranges and narrows the cells to float (they are
// uploaded as a float texture, and the copy is made here anyway). An image has
// `channels` 3 or 4 values per cell.
std::vector<float> heatmap_cells(std::span<const double> data, int rows, int cols,
                                 Range xrange, Range yrange, const std::string& w,
                                 int channels = 1) {
    if (rows < 1 || cols < 1)
        throw std::invalid_argument(w + ": rows and cols must be positive");
    if (data.size() < static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols)
                          * static_cast<std::size_t>(channels))
        throw std::invalid_argument(w + (channels == 1 ? ": data too small for rows×cols"
                                                       : ": data too small for rows×cols×channels"));

    // Reject degenerate ranges here once; reversed ranges mirror the image.
    for (const Range& r : { xrange, yrange }) {
        if (!std::isfinite(r.lo) || !std::isfinite(r.hi))
            throw std::invalid_argument(w + ": range bounds must be finite");
        if (r.lo == r.hi)
            throw std::invalid_argument(w + ": range must span a non-zero interval");
    }

    std::vector<float> cells(static_cast<std::size_t>(rows) * cols * channels);
    for (std::size_t i = 0; i < cells.size(); ++i)
        cells[i] = static_cast<float>(data[i]);
    return cells;
}

// A heatmap_rec()'s edges: cols + 1 and rows + 1, finite and strictly
// monotone either way. Returns the extent, first edge to last.
std::pair<Range, Range> mesh_extent(std::span<const double> x_edges, std::span<const double> y_edges,
                                    int rows, int cols, const std::string& w) {
    if (rows < 1 || cols < 1)
        throw std::invalid_argument(w + ": rows and cols must be positive");
    auto check = [&w](std::span<const double> e, int cells, const char* name) {
        if (e.size() != static_cast<std::size_t>(cells) + 1)
            throw std::invalid_argument(w + ": " + name + " needs " + std::to_string(cells + 1)
                                        + " edges, not " + std::to_string(e.size()));
        for (double v : e)
            if (!std::isfinite(v)) throw std::invalid_argument(w + ": " + name + " must be finite");
        const bool up = e[1] > e[0];
        for (std::size_t k = 1; k < e.size(); ++k)
            if (up ? !(e[k] > e[k - 1]) : !(e[k] < e[k - 1]))
                throw std::invalid_argument(w + ": " + name
                                            + " must be strictly increasing or strictly decreasing");
        return Range{ e.front(), e.back() };
    };
    const Range xr = check(x_edges, cols, "x_edges");
    const Range yr = check(y_edges, rows, "y_edges");
    return { xr, yr };
}

// Every reader tests == "lower", so anything else would silently mean upper.
void check_origin(const std::string& origin, const std::string& w) {
    if (origin != "lower" && origin != "upper")
        throw std::invalid_argument(w + ": origin must be \"lower\" or \"upper\", not \""
                                    + origin + "\"");
}

// Contour levels sorted and de-duplicated once. Finiteness first (NaN doesn't sort).
void sort_contour_levels(std::vector<double>& levels, const std::string& w) {
    for (double level : levels)
        if (!std::isfinite(level))
            throw std::invalid_argument(w + ": contour levels must be finite");
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
}

} // namespace

// Vector-shaped ingests, shared by Axes and Plane2D. `who` names the caller in
// error messages.
void Axes::Impl::ingest_line(std::span<const double> x, std::span<const double> y,
                             const ErrorBar& eb, LineOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    if (x.size() != y.size())
        throw std::invalid_argument(std::string(who) + ": x and y must have the same length");
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    lines.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::move(err),
        std::move(opts),
        next_snapshot_generation(),
        new_id(),
    });
}

void Axes::Impl::ingest_scatter(std::span<const double> x, std::span<const double> y,
                                const ErrorBar& eb, ScatterOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    if (x.size() != y.size())
        throw std::invalid_argument(std::string(who) + ": x and y must have the same length");
    read_back::check_sizes(opts.sizes, x.size(), who);
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    std::vector<double> sizes = std::move(opts.sizes);
    opts.sizes.clear();
    scatters.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::move(err),
        std::move(opts),
        next_snapshot_generation(),
        new_id(),
        std::move(sizes),
    });
}

void Axes::Impl::ingest_scatter_z(std::span<const double> x, std::span<const double> y,
                                  std::span<const double> z, const ErrorBar& eb,
                                  ScatterZOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    if (x.size() != y.size() || x.size() != z.size())
        throw std::invalid_argument(std::string(who) + ": x, y, and z must have the same length");
    check_norm(opts.norm, opts.vmin, opts.vmax, who);
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    warn_math(mathtext, who, opts.name);
    read_back::check_sizes(opts.sizes, x.size(), who);
    std::vector<double> sizes = std::move(opts.sizes);
    opts.sizes.clear();
    scatter_z.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::vector<double>(z.begin(), z.end()),
        std::move(err),
        std::move(opts),
        next_snapshot_generation(),
        new_id(),
        std::move(sizes),
    });
}

void Axes::Impl::ingest_bar(std::span<const double> x, std::span<const double> height,
                            const ErrorBar& eb, BarOptions opts, const char* who) {
    if (x.size() != height.size())
        throw std::invalid_argument(std::string(who) + ": x and height must have the same length");
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    const double width = bar_width_for(x, opts);
    push_bars(std::vector<double>(x.begin(), x.end()),
              std::vector<double>(height.begin(), height.end()),
              width, std::move(err), std::move(opts), who);
}

// bar() and hist(): the bases are taken out of the options and kept as data.
void Axes::Impl::push_bars(std::vector<double> centers, std::vector<double> heights, double width,
                           ErrorBarData err, BarOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    std::vector<double> bottom = read_back::take_bottom(std::move(opts.bottom), centers.size(), who);
    opts.bottom.clear();
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    bars.push_back({
        std::move(centers),
        std::move(heights),
        width,
        std::move(err),
        std::move(opts),
        next_snapshot_generation(),
        new_id(),
        std::move(bottom),
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

    // bar_opts passes through (orientation and bottom included); `width` is
    // relative to the bin width. No error bars for histograms.
    const double width = bin_w * static_cast<double>(bar_opts.width);
    d->push_bars(std::move(centers), std::move(counts), width, ErrorBarData{}, std::move(bar_opts), "hist");
    return *this;
}

void Axes::Impl::ingest_heatmap(std::span<const double> data, int rows, int cols,
                                Range xrange, Range yrange, HeatmapOptions opts,
                                const char* who) {
    read_back::check_zorder(opts.zorder, who);
    const std::string w = who;
    std::vector<float> cells = heatmap_cells(data, rows, cols, xrange, yrange, w);
    check_origin(opts.origin, w);
    check_norm(opts.norm, opts.vmin, opts.vmax, who);
    sort_contour_levels(opts.contours, w);

    warn_math(mathtext, who, opts.name);
    heatmaps.push_back({
        std::move(cells),
        rows, cols,
        xrange, yrange,
        std::move(opts),
        next_snapshot_generation(),
        new_id(),
    });
}

Axes& Axes::heatmap(std::span<const double> data, int rows, int cols,
                    Range xrange, Range yrange, HeatmapOptions opts) {
    d->ingest_heatmap(data, rows, cols, xrange, yrange, std::move(opts), "heatmap");
    return *this;
}

// heatmap() over the index extent: one unit per cell, origin at (0,0).
Axes& Axes::imshow(std::span<const double> data, int rows, int cols,
                   HeatmapOptions opts) {
    // Clamp so an invalid shape reports heatmap()'s rows/cols error rather than
    // a range error.
    const Range xr{ 0.0, static_cast<double>(std::max(cols, 1)) };
    const Range yr{ 0.0, static_cast<double>(std::max(rows, 1)) };
    return heatmap(data, rows, cols, xr, yr, std::move(opts));
}

// An image is a heatmap whose cells carry their own colour: the options it
// shares with one (origin, visible, zorder, hint labels) go to a HeatmapOptions.
void Axes::Impl::ingest_image(std::span<const double> data, int rows, int cols, int channels,
                              ImageOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    const std::string w = who;
    if (channels != 3 && channels != 4)
        throw std::invalid_argument(w + ": channels must be 3 (RGB) or 4 (RGBA), not "
                                    + std::to_string(channels));
    // Clamped as imshow()'s, so a bad shape reports the rows/cols error.
    const Range xr = opts.xrange.value_or(Range{ 0.0, static_cast<double>(std::max(cols, 1)) });
    const Range yr = opts.yrange.value_or(Range{ 0.0, static_cast<double>(std::max(rows, 1)) });
    std::vector<float> cells = heatmap_cells(data, rows, cols, xr, yr, w, channels);
    check_origin(opts.origin, w);

    HeatmapOptions ho;
    ho.origin = std::move(opts.origin);
    ho.visible = opts.visible;
    ho.zorder = opts.zorder;
    ho.hint_labels = std::move(opts.hint_labels);
    HeatmapPlot p{ std::move(cells), rows, cols, xr, yr, std::move(ho),
                   next_snapshot_generation(), new_id() };
    p.channels = channels;
    heatmaps.push_back(std::move(p));
}

std::vector<double> Axes::Impl::image_bytes(std::span<const std::uint8_t> data) {
    std::vector<double> out(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) out[i] = data[i] / 255.0;
    return out;
}

Axes& Axes::imshow_rgb(std::span<const double> data, int rows, int cols, int channels,
                       ImageOptions opts) {
    d->ingest_image(data, rows, cols, channels, std::move(opts), "imshow_rgb");
    return *this;
}

Axes& Axes::imshow_rgb(std::span<const std::uint8_t> data, int rows, int cols, int channels,
                       ImageOptions opts) {
    d->ingest_image(Impl::image_bytes(data), rows, cols, channels, std::move(opts), "imshow_rgb");
    return *this;
}

// A mesh is a heatmap with edges: its options go to a HeatmapOptions with
// origin "lower" (row 0 at the first y edge).
void Axes::Impl::ingest_heatmap_rec(std::span<const double> data, int rows, int cols,
                                    std::span<const double> x_edges, std::span<const double> y_edges,
                                    HeatmapRecOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    const std::string w = who;
    const auto [xr, yr] = mesh_extent(x_edges, y_edges, rows, cols, w);
    std::vector<float> cells = heatmap_cells(data, rows, cols, xr, yr, w);
    check_norm(opts.norm, opts.vmin, opts.vmax, who);
    sort_contour_levels(opts.contours, w);

    HeatmapOptions ho;
    ho.cmap = opts.cmap;
    ho.vmin = opts.vmin;
    ho.vmax = opts.vmax;
    ho.norm = opts.norm;
    ho.colorbar = opts.colorbar;
    ho.name = std::move(opts.name);
    ho.visible = opts.visible;
    ho.zorder = opts.zorder;
    ho.contours = std::move(opts.contours);
    ho.contour_color = opts.contour_color;
    ho.contour_linewidth = opts.contour_linewidth;
    ho.contour_labels = opts.contour_labels;
    ho.contour_fontsize = opts.contour_fontsize;
    ho.hint_labels = std::move(opts.hint_labels);
    warn_math(mathtext, who, ho.name);
    HeatmapPlot p{ std::move(cells), rows, cols, xr, yr, std::move(ho),
                   next_snapshot_generation(), new_id() };
    p.x_edges = std::vector<double>(x_edges.begin(), x_edges.end());
    p.y_edges = std::vector<double>(y_edges.begin(), y_edges.end());
    heatmaps.push_back(std::move(p));
}

Axes& Axes::heatmap_rec(std::span<const double> data, int rows, int cols,
                        std::span<const double> x_edges, std::span<const double> y_edges,
                        HeatmapRecOptions opts) {
    d->ingest_heatmap_rec(data, rows, cols, x_edges, y_edges, std::move(opts), "heatmap_rec");
    return *this;
}

// Binned here; the result is an ordinary heatmap (even bins) or mesh, which
// does the drawing checks (the norm against the resolved limits among them).
Axes& Axes::hist2d(std::span<const double> x, std::span<const double> y, Hist2dOptions opts) {
    const Hist2dBins b = hist2d_bins(x, y, opts, "hist2d");
    if (b.even) {
        HeatmapOptions ho;
        ho.cmap = opts.cmap;
        ho.vmin = b.vmin;
        ho.vmax = b.vmax;
        ho.norm = opts.norm;
        ho.colorbar = opts.colorbar;
        ho.name = std::move(opts.name);
        ho.visible = opts.visible;
        ho.zorder = opts.zorder;
        d->ingest_heatmap(b.values, b.rows(), b.cols(), Range{ b.x_edges.front(), b.x_edges.back() },
                          Range{ b.y_edges.front(), b.y_edges.back() }, std::move(ho), "hist2d");
    } else {
        HeatmapRecOptions ro;
        ro.cmap = opts.cmap;
        ro.vmin = b.vmin;
        ro.vmax = b.vmax;
        ro.norm = opts.norm;
        ro.colorbar = opts.colorbar;
        ro.name = std::move(opts.name);
        ro.visible = opts.visible;
        ro.zorder = opts.zorder;
        d->ingest_heatmap_rec(b.values, b.rows(), b.cols(), b.x_edges, b.y_edges, std::move(ro), "hist2d");
    }
    return *this;
}

// Values are kept as given (NaN too); the triangles are checked, or derived
// once from (x, y) in data units.
void Axes::Impl::ingest_heatmap_tri(std::span<const double> x, std::span<const double> y,
                                    std::span<const double> values, std::span<const std::uint32_t> tri,
                                    bool derive, HeatmapTriOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    read_back::check_tri_vertices(x, y, values, who);
    check_norm(opts.norm, opts.vmin, opts.vmax, who);
    std::vector<std::uint32_t> t;
    if (derive) {
        if (!delaunay_triangulate(x, y, t))
            throw std::invalid_argument(std::string(who) + ": these points have no triangulation -- they are "
                                        "collinear, or fewer than three of them are distinct");
    } else {
        read_back::check_tri_indices(tri, x.size(), who);
        t = read_back::own(tri);
    }
    warn_math(mathtext, who, opts.name);
    heatmap_tris.push_back({ read_back::own(x), read_back::own(y), read_back::own(values), std::move(t),
                             std::move(opts), next_snapshot_generation(), new_id() });
}

Axes& Axes::heatmap_tri(std::span<const double> x, std::span<const double> y, std::span<const double> values,
                        std::span<const std::uint32_t> tri, HeatmapTriOptions opts) {
    d->ingest_heatmap_tri(x, y, values, tri, false, std::move(opts), "heatmap_tri");
    return *this;
}

Axes& Axes::heatmap_tri(std::span<const double> x, std::span<const double> y, std::span<const double> values,
                        HeatmapTriOptions opts) {
    d->ingest_heatmap_tri(x, y, values, {}, true, std::move(opts), "heatmap_tri");
    return *this;
}

void Axes::Impl::ingest_contour(std::span<const double> x, std::span<const double> y, std::span<const double> z,
                                ContourOptions opts, bool filled, const char* who) {
    if (filled) opts.filled = true;
    read_back::check_zorder(opts.zorder, who);
    check_contour(x, y, z, opts, who);
    warn_math(mathtext, who, opts.name);
    ContourPlot p;
    p.x = read_back::own(x);
    p.y = read_back::own(y);
    p.z = read_back::own(z);
    p.opts = std::move(opts);
    p.data_stamp = next_snapshot_generation();
    p.id = new_id();
    contour_rebuild(p);
    contours.push_back(std::move(p));
}

Axes& Axes::contour(std::span<const double> x, std::span<const double> y, std::span<const double> z,
                    ContourOptions opts) {
    d->ingest_contour(x, y, z, std::move(opts), false, "contour");
    return *this;
}

Axes& Axes::contourf(std::span<const double> x, std::span<const double> y, std::span<const double> z,
                     ContourOptions opts) {
    d->ingest_contour(x, y, z, std::move(opts), true, "contourf");
    return *this;
}

void Axes::Impl::ingest_axline(double value, AxLineOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    read_back::check_reference({value, opts.from, opts.to}, who);
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    axlines.push_back({ value, std::move(opts), next_snapshot_generation(), new_id() });
}

// A sloped line: the options' orientation, from and to are not read. It keeps
// no `value`.
void Axes::Impl::ingest_axline_sloped(AxLineData d, AxLineOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    read_back::check_axline_sloped(d, who);
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    axlines.push_back({ 0.0, std::move(opts), next_snapshot_generation(), new_id() });
    AxLinePlot& p = axlines.back();
    p.mode = d.mode;
    p.x = d.x; p.y = d.y; p.x2 = d.x2; p.y2 = d.y2;
    p.slope = d.slope;
}

// The cycle is not taken (matplotlib's axhspan is the patch default, C0).
void Axes::Impl::ingest_axspan(double lo, double hi, AxSpanOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    read_back::check_reference({lo, hi, opts.from, opts.to}, who);
    warn_math(mathtext, who, opts.name);
    if (!opts.color) opts.color = Color::Blue;
    if (lo > hi) std::swap(lo, hi);
    axspans.push_back({ lo, hi, std::move(opts), next_snapshot_generation(), new_id() });
}

// The mask is taken out of the options and kept as data, as bar bases are.
void Axes::Impl::ingest_fill(std::span<const double> x, std::span<const double> y1,
                             std::span<const double> y2, FillOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    read_back::check_fill(x.size(), y1.size(), y2.size(), who);
    std::vector<double> exclude = read_back::take_exclude(opts.exclude, x.size(), who);
    opts.exclude.clear();
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    fills.push_back({
        read_back::own(x),
        read_back::own(y1),
        read_back::own(y2),
        std::move(opts),
        next_snapshot_generation(),
        new_id(),
        std::move(exclude),
    });
}

Axes& Axes::fill(std::span<const double> x, std::span<const double> y1,
                         std::span<const double> y2, FillOptions opts) {
    d->ingest_fill(x, y1, y2, std::move(opts), "fill");
    return *this;
}

Axes& Axes::fill(std::span<const double> x, std::span<const double> y1, double y2,
                         FillOptions opts) {
    const std::vector<double> base(x.size(), y2);
    d->ingest_fill(x, y1, base, std::move(opts), "fill");
    return *this;
}


void Axes::Impl::ingest_poly(std::span<const double> x, std::span<const double> y, PolyOptions opts,
                             const char* who) {
    read_back::check_zorder(opts.zorder, who);
    read_back::check_poly(x, y, who);
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    polys.push_back({ read_back::own(x), read_back::own(y), std::move(opts),
                      next_snapshot_generation(), new_id() });
}

void Axes::Impl::ingest_circle(Pos cx, Pos cy, Pos r, CircleOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    read_back::check_round(cx, cy, r, who);
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    circles.push_back({ cx, cy, r, std::move(opts), next_snapshot_generation(), new_id() });
}

// The hole is taken out of the options and kept as data, as a fill's mask is.
void Axes::Impl::ingest_wedge(Pos cx, Pos cy, Pos r, double theta1, double theta2, WedgeOptions opts,
                              const char* who) {
    read_back::check_zorder(opts.zorder, who);
    const double inner = opts.inner;
    opts.inner = 0.0;
    read_back::check_wedge(cx, cy, r, theta1, theta2, inner, who);
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    wedges.push_back({ cx, cy, r, theta1, theta2, inner, std::move(opts),
                       next_snapshot_generation(), new_id() });
}

Axes& Axes::shape_poly(std::span<const double> x, std::span<const double> y, PolyOptions opts) {
    d->ingest_poly(x, y, std::move(opts), "shape_poly");
    return *this;
}

Axes& Axes::shape_circle(Pos cx, Pos cy, Pos r, CircleOptions opts) {
    d->ingest_circle(cx, cy, r, std::move(opts), "shape_circle");
    return *this;
}

Axes& Axes::shape_wedge(Pos cx, Pos cy, Pos r, double theta1, double theta2, WedgeOptions opts) {
    d->ingest_wedge(cx, cy, r, theta1, theta2, std::move(opts), "shape_wedge");
    return *this;
}

void Axes::Impl::set_poly_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                               const char* who) {
    PolyPlot& p = read_back::at(polys, i, who);
    read_back::check_poly(x, y, who);
    p.x = read_back::own(x);
    p.y = read_back::own(y);
    p.data_stamp = next_snapshot_generation();
}

void Axes::Impl::set_circle_data(std::size_t i, const CircleData& c, const char* who) {
    CirclePlot& p = read_back::at(circles, i, who);
    read_back::check_round(c.cx, c.cy, c.r, who);
    p.cx = c.cx;
    p.cy = c.cy;
    p.r = c.r;
    p.data_stamp = next_snapshot_generation();
}

void Axes::Impl::set_wedge_data(std::size_t i, const WedgeData& w, const char* who) {
    WedgePlot& p = read_back::at(wedges, i, who);
    read_back::check_wedge(w.cx, w.cy, w.r, w.theta1, w.theta2, w.inner, who);
    p.cx = w.cx; p.cy = w.cy; p.r = w.r;
    p.theta1 = w.theta1; p.theta2 = w.theta2; p.inner = w.inner;
    p.data_stamp = next_snapshot_generation();
}

void Axes::Impl::ingest_stack(std::span<const double> x, std::span<const std::vector<double>> ys,
                              const StackOptions& opts, const char* who) {
    for (const std::vector<double>& y : ys)
        if (y.size() != x.size())
            throw std::invalid_argument(std::string(who) + ": every layer needs one value per x");
    if (!opts.colors.empty() && opts.colors.size() != ys.size())
        throw std::invalid_argument(std::string(who) + ": colors needs one per layer");
    if (!opts.names.empty() && opts.names.size() != ys.size())
        throw std::invalid_argument(std::string(who) + ": names needs one per layer");
    const std::vector<std::vector<double>> curves = stack_curves(ys, x.size(), opts.baseline);
    for (std::size_t k = 0; k < ys.size(); ++k) {
        FillOptions f;
        f.orientation = opts.orientation;
        if (!opts.colors.empty()) f.color = opts.colors[k];
        f.alpha = opts.alpha;
        f.edgecolor = opts.edgecolor;
        f.linewidth = opts.linewidth;
        f.linestyle = opts.linestyle;
        f.drawstyle = opts.drawstyle;
        if (!opts.names.empty()) f.name = opts.names[k];
        f.show_legend = opts.show_legend;
        f.visible = opts.visible;
        f.zorder = opts.zorder;
        ingest_fill(x, curves[k], curves[k + 1], std::move(f), who);
    }
}

Axes& Axes::stack(std::span<const double> x, std::span<const std::vector<double>> ys,
                  StackOptions opts) {
    d->ingest_stack(x, ys, opts, "stack");
    return *this;
}

// An unset amp is chosen from the data here and kept, as matplotlib's.
// A colormapped quiver keeps a cycle color without using one up, as the 3D
// kinds' colormapped objects do.
void Axes::Impl::ingest_quiver(std::span<const double> x, std::span<const double> y,
                               std::span<const double> u, std::span<const double> v,
                               const std::span<const double>* c, QuiverOptions opts, const char* who) {
    read_back::check_zorder(opts.zorder, who);
    read_back::check_quiver(x.size(), y.size(), u.size(), v.size(), who);
    if (c && c->size() != x.size())
        throw std::invalid_argument(std::string(who) + ": c must have one value per arrow");
    if (c) check_norm(opts.norm, opts.vmin, opts.vmax, who);
    if (opts.amp && !(std::isfinite(*opts.amp) && *opts.amp > 0.0))
        throw std::invalid_argument(std::string(who) + ": amp must be finite and > 0");
    if (!opts.amp) opts.amp = quiver_auto_amp(x, y, u, v);
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color, !c);
    quivers.push_back({
        read_back::own(x),
        read_back::own(y),
        read_back::own(u),
        read_back::own(v),
        std::move(opts),
        next_snapshot_generation(),
        new_id(),
        c ? CowVec<double>(read_back::own(*c)) : CowVec<double>{},
    });
}

Axes& Axes::quiver(std::span<const double> x, std::span<const double> y, std::span<const double> u,
                   std::span<const double> v, QuiverOptions opts) {
    d->ingest_quiver(x, y, u, v, nullptr, std::move(opts), "quiver");
    return *this;
}

Axes& Axes::quiver(std::span<const double> x, std::span<const double> y, std::span<const double> u,
                   std::span<const double> v, std::span<const double> c, QuiverOptions opts) {
    d->ingest_quiver(x, y, u, v, &c, std::move(opts), "quiver");
    return *this;
}

Axes& Axes::axline(double value, AxLineOptions opts) {
    d->ingest_axline(value, std::move(opts), "axline");
    return *this;
}

Axes& Axes::axline(Pos x, Pos y, double slope, AxLineOptions opts) {
    AxLineData d;
    d.mode = AxLineMode::PointSlope;
    d.x = x; d.y = y; d.slope = slope;
    this->d->ingest_axline_sloped(d, std::move(opts), "axline");
    return *this;
}

Axes& Axes::axline(Pos x1, Pos y1, Pos x2, Pos y2, AxLineOptions opts) {
    AxLineData d;
    d.mode = AxLineMode::TwoPoints;
    d.x = x1; d.y = y1; d.x2 = x2; d.y2 = y2;
    this->d->ingest_axline_sloped(d, std::move(opts), "axline");
    return *this;
}

Axes& Axes::axspan(double lo, double hi, AxSpanOptions opts) {
    d->ingest_axspan(lo, hi, std::move(opts), "axspan");
    return *this;
}

void Axes::Impl::ingest_text(TextContent c, TextStyle style, const char* who) {
    read_back::check_text(c, who);
    warn_math(mathtext && style.text.parse_math, who, c.text);
    texts.push_back({ std::move(c), std::move(style), next_snapshot_generation(), new_id() });
}

Axes& Axes::text(std::string_view s, Pos x, Pos y, TextOptions opts) {
    TextContent c;
    c.text = s;
    c.x = x;
    c.y = y;
    d->ingest_text(std::move(c), { std::move(opts), ArrowOptions{} }, "text");
    return *this;
}

Axes& Axes::annotate(double px, double py, std::string_view s, Pos tx, Pos ty,
                     TextOptions opts, ArrowOptions arrow) {
    TextContent c;
    c.text = s;
    c.x = tx;
    c.y = ty;
    c.arrow = true;
    c.px = px;
    c.py = py;
    d->ingest_text(std::move(c), { std::move(opts), std::move(arrow) }, "annotate");
    return *this;
}

Axes& Axes::set_title(std::string_view text, float fontsize) {
    warn_math(d->mathtext, "set_title", text);
    Impl& c = d->cell();
    c.title = text; c.title_stamps.title = next_snapshot_generation();
    c.axes_style.title_fontsize = fontsize; return *this;
}
Axes& Axes::set_xtitle(std::string_view text, float fontsize) {
    warn_math(d->mathtext, "set_xtitle", text);
    Impl& a = d->axis_owner(true);
    a.xtitle = text; a.title_stamps.xtitle = next_snapshot_generation();
    a.axes_style.xtitle_fontsize = fontsize; return *this;
}
Axes& Axes::set_ytitle(std::string_view text, float fontsize) {
    warn_math(d->mathtext, "set_ytitle", text);
    Impl& a = d->axis_owner(false);
    a.ytitle = text; a.title_stamps.ytitle = next_snapshot_generation();
    a.axes_style.ytitle_fontsize = fontsize; return *this;
}
Axes& Axes::set_xlim(double lo, double hi) {
    check_limits_fit(d->xscale, lo, hi, "set_xlim");
    d->xmin = lo; d->xmax = hi; d->xlim_auto = false;
    d->limit_stamps.x = next_snapshot_generation();
    d->sync_shared_x();
    return *this;
}
Axes& Axes::set_ylim(double lo, double hi) {
    check_limits_fit(d->yscale, lo, hi, "set_ylim");
    d->ymin = lo; d->ymax = hi; d->ylim_auto = false;
    d->limit_stamps.y = next_snapshot_generation();
    d->sync_shared_y();
    return *this;
}
Axes& Axes::set_xscale(Scale scale, ScaleOptions opts) {
    // The scale is the group's: every member's ticks lane takes the new stamp.
    set_scale(d->xscale, d->xmin, d->xmax, d->xlim_auto, d->limit_stamps.x,
              d->style_stamps.xticks, scale, opts, "set_xscale");
    d->sync_shared_x();
    if (d->xshare)
        for (Impl* m : d->xshare->members) m->style_stamps.xticks = d->style_stamps.xticks;
    return *this;
}
Axes& Axes::set_yscale(Scale scale, ScaleOptions opts) {
    set_scale(d->yscale, d->ymin, d->ymax, d->ylim_auto, d->limit_stamps.y,
              d->style_stamps.yticks, scale, opts, "set_yscale");
    d->sync_shared_y();
    if (d->yshare)
        for (Impl* m : d->yshare->members) m->style_stamps.yticks = d->style_stamps.yticks;
    return *this;
}
Axes& Axes::grid(bool enable, GridOptions opts) {
    d->grid_enabled = enable; d->grid_opts = opts;
    d->style_stamps.grid = next_snapshot_generation(); return *this;
}
Axes& Axes::set_axes_style(AxesStyle opts) {
    if (d->host) { d->set_twin_style(opts); return *this; }
    d->axes_style = opts;
    d->style_stamps.style = next_snapshot_generation(); return *this;
}
Axes& Axes::legend(LegendOptions opts) {
    Impl& c = d->cell();
    c.legend_enabled = true; c.legend_opts = opts;
    c.style_stamps.legend = next_snapshot_generation(); return *this;
}
Axes& Axes::set_colorbar_style(ColorbarOptions opts) {
    Impl& c = d->cell();
    c.colorbar_opts = opts;
    c.style_stamps.colorbar = next_snapshot_generation(); return *this;
}

Axes& Axes::set_xticks(std::span<const double> pos, std::vector<std::string> labels) {
    for (const auto& l : labels) warn_math(d->mathtext, "set_xticks", l);
    Impl& a = d->axis_owner(true);
    if (pos.empty()) a.xticks_override.reset();
    else             a.xticks_override = make_tick_override(pos, labels);
    a.style_stamps.xticks = next_snapshot_generation();
    return *this;
}
Axes& Axes::set_yticks(std::span<const double> pos, std::vector<std::string> labels) {
    for (const auto& l : labels) warn_math(d->mathtext, "set_yticks", l);
    Impl& a = d->axis_owner(false);
    if (pos.empty()) a.yticks_override.reset();
    else             a.yticks_override = make_tick_override(pos, labels);
    a.style_stamps.yticks = next_snapshot_generation();
    return *this;
}
Axes& Axes::set_xtick_format(std::string_view fmt) {
    if (!fmt.empty() && !tick_format_ok(fmt))
        throw std::invalid_argument("set_xtick_format: \"" + std::string(fmt)
                                    + "\" is not one %f/%e/%g conversion and text");
    warn_math(d->mathtext, "set_xtick_format", fmt);
    Impl& a = d->axis_owner(true);
    a.xtick_format = fmt;
    a.style_stamps.xticks = next_snapshot_generation();
    return *this;
}
Axes& Axes::set_ytick_format(std::string_view fmt) {
    if (!fmt.empty() && !tick_format_ok(fmt))
        throw std::invalid_argument("set_ytick_format: \"" + std::string(fmt)
                                    + "\" is not one %f/%e/%g conversion and text");
    warn_math(d->mathtext, "set_ytick_format", fmt);
    Impl& a = d->axis_owner(false);
    a.ytick_format = fmt;
    a.style_stamps.yticks = next_snapshot_generation();
    return *this;
}
Axes& Axes::set_color_cycle(std::vector<Color> colors) {
    d->cycle->set(std::move(colors));
    return *this;
}
Axes& Axes::cla() {
    // Reset to defaults. Panel edits made before it name objects by id, and the
    // objects plotted after it get new ones, so none of those edits lands. The
    // figure's setting stays.
    // Membership stays, and so do the shared limits and scale: they are the
    // group's, taken back from another member (as matplotlib re-takes the
    // sibling's).
    // Twins stay too: a host keeps its twins and resets the color cycle they
    // share in place; a twin keeps its host, the host's cycle and its defaults.
    const bool mathtext = d->mathtext;
    const std::uint64_t owner = d->owner;
    auto xs = std::move(d->xshare), ys = std::move(d->yshare);
    auto tx = std::move(d->twin_x), ty = std::move(d->twin_y);
    auto cycle = std::move(d->cycle);
    Impl* host = d->host;
    const int layer = d->layer;
    *d = Impl{};
    d->mathtext = mathtext;
    d->owner = owner;
    d->xshare = std::move(xs);
    d->yshare = std::move(ys);
    d->twin_x = std::move(tx);
    d->twin_y = std::move(ty);
    d->host = host;
    d->layer = layer;
    if (!host) *cycle = ColorCycle{};
    d->cycle = std::move(cycle);
    d->twin_defaults();
    d->adopt_shared_x();
    d->adopt_shared_y();
    return *this;
}

} // namespace sextant

namespace sextant {

std::string Axes::title() const  { return d->cell().title; }
std::string Axes::xtitle() const { return d->axis_owner(true).xtitle; }
std::string Axes::ytitle() const { return d->axis_owner(false).ytitle; }

// resolve_figure_limits()'s rule, from the live members.
ResolvedLimits Axes::Impl::resolve_live() const {
    const RenderSnapshot s = build_snapshot();
    if (!xshare && !yshare) return resolve_limits(s);
    const auto group = [&](const ShareGroup* g, bool x) -> std::optional<SharedAuto> {
        if (!g) return std::nullopt;
        SharedAuto a;
        for (const Impl* m : g->members) {
            const RenderSnapshot ms = m == this ? s : m->build_snapshot();
            add_to_shared(a, ms, auto_bounds(ms.all(), ms.xscale, ms.yscale), x);
        }
        return a;
    };
    const std::optional<SharedAuto> gx = group(xshare.get(), true);
    const std::optional<SharedAuto> gy = group(yshare.get(), false);
    return resolve_limits(s, nullptr, gx ? &*gx : nullptr, gy ? &*gy : nullptr);
}

Range Axes::xlim() const {
    const ResolvedLimits l = d->resolve_live();
    return { l.xmin, l.xmax };
}

Range Axes::ylim() const {
    const ResolvedLimits l = d->resolve_live();
    return { l.ymin, l.ymax };
}

Scale Axes::xscale() const { return d->xscale.kind; }
Scale Axes::yscale() const { return d->yscale.kind; }

double Axes::aspect() const         { return d->aspect.ratio; }
Adjustable Axes::adjustable() const { return d->aspect.adjustable; }

ObjectId Axes::last_object() const { return d->last_id; }
bool Axes::set_zorder(ObjectId id, double z) {
    return read_back::set_zorder(*d, id, z, "Axes::set_zorder");
}
std::optional<double> Axes::zorder(ObjectId id) const {
    return read_back::zorder(*d, id, "Axes::zorder");
}
bool Axes::remove(ObjectId id) { return object_remove::remove(*d, id, "Axes::remove"); }
std::optional<ObjectLocation> Axes::find_object(ObjectId id) const {
    return to_location(d->find_object(id));
}

std::size_t Axes::line_count() const      { return d->lines.size(); }
std::size_t Axes::scatter_count() const   { return d->scatters.size(); }
std::size_t Axes::scatter_z_count() const { return d->scatter_z.size(); }
std::size_t Axes::bar_count() const       { return d->bars.size(); }
std::size_t Axes::heatmap_count() const   { return d->heatmaps.size(); }

LineData Axes::line_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->lines, i, "line_data"));
}
ScatterData Axes::scatter_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->scatters, i, "scatter_data"));
}
ScatterZData Axes::scatter_z_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->scatter_z, i, "scatter_z_data"));
}
BarData Axes::bar_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->bars, i, "bar_data"));
}
HeatmapData Axes::heatmap_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->heatmaps, i, "heatmap_data"));
}

std::size_t Axes::text_count() const      { return d->texts.size(); }

TextData Axes::text_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->texts, i, "text_data"));
}

std::size_t Axes::fill_count() const      { return d->fills.size(); }

FillData Axes::fill_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->fills, i, "fill_data"));
}


std::size_t Axes::shape_poly_count() const   { return d->polys.size(); }
std::size_t Axes::shape_circle_count() const { return d->circles.size(); }
std::size_t Axes::shape_wedge_count() const  { return d->wedges.size(); }

PolyData Axes::shape_poly_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->polys, i, "shape_poly_data"));
}
CircleData Axes::shape_circle_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->circles, i, "shape_circle_data"));
}
WedgeData Axes::shape_wedge_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->wedges, i, "shape_wedge_data"));
}

Axes& Axes::set_shape_poly_data(std::size_t i, const PolyData& data) {
    d->set_poly_data(i, data.x, data.y, "set_shape_poly_data");
    return *this;
}
Axes& Axes::set_shape_poly_data(std::size_t i, std::span<const double> x, std::span<const double> y) {
    d->set_poly_data(i, x, y, "set_shape_poly_data");
    return *this;
}
Axes& Axes::set_shape_circle_data(std::size_t i, const CircleData& data) {
    d->set_circle_data(i, data, "set_shape_circle_data");
    return *this;
}
Axes& Axes::set_shape_wedge_data(std::size_t i, const WedgeData& data) {
    d->set_wedge_data(i, data, "set_shape_wedge_data");
    return *this;
}

std::size_t Axes::contour_count() const { return d->contours.size(); }
ContourData Axes::contour_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->contours, i, "contour_data"));
}
Axes& Axes::set_contour_data(std::size_t i, const ContourData& data) {
    d->set_contour_data(i, data.x, data.y, data.z, &data.levels, &data.filled, "set_contour_data");
    return *this;
}
Axes& Axes::set_contour_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                             std::span<const double> z) {
    d->set_contour_data(i, x, y, z, nullptr, nullptr, "set_contour_data");
    return *this;
}
std::size_t Axes::heatmap_tri_count() const { return d->heatmap_tris.size(); }
HeatmapTriData Axes::heatmap_tri_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->heatmap_tris, i, "heatmap_tri_data"));
}
Axes& Axes::set_heatmap_tri_data(std::size_t i, const HeatmapTriData& data) {
    d->set_heatmap_tri_data(i, data.x, data.y, data.values, &data.tri, "set_heatmap_tri_data");
    return *this;
}
Axes& Axes::set_heatmap_tri_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                 std::span<const double> values, std::span<const std::uint32_t> tri) {
    const std::vector<std::uint32_t> t = read_back::own(tri);
    d->set_heatmap_tri_data(i, x, y, values, &t, "set_heatmap_tri_data");
    return *this;
}
Axes& Axes::set_heatmap_tri_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                 std::span<const double> values) {
    d->set_heatmap_tri_data(i, x, y, values, nullptr, "set_heatmap_tri_data");
    return *this;
}

std::size_t Axes::quiver_count() const    { return d->quivers.size(); }

QuiverData Axes::quiver_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->quivers, i, "quiver_data"));
}

std::size_t Axes::axline_count() const    { return d->axlines.size(); }
std::size_t Axes::axspan_count() const    { return d->axspans.size(); }

AxLineData Axes::axline_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->axlines, i, "axline_data"));
}
AxSpanData Axes::axspan_data(std::size_t i) const {
    return read_back::to_data(read_back::at(d->axspans, i, "axspan_data"));
}

Axes& Axes::set_axline_data(std::size_t i, const AxLineData& data) {
    AxLinePlot& p = read_back::at(d->axlines, i, "set_axline_data");
    if (data.mode == AxLineMode::Value) {
        read_back::check_reference({data.value}, "set_axline_data");
        p.value = data.value;
    } else {
        read_back::check_axline_sloped(data, "set_axline_data");
        p.x = data.x; p.y = data.y; p.x2 = data.x2; p.y2 = data.y2;
        p.slope = data.slope;
    }
    p.mode = data.mode;
    p.data_stamp = next_snapshot_generation();
    return *this;
}

Axes& Axes::set_axspan_data(std::size_t i, const AxSpanData& data) {
    AxSpanPlot& p = read_back::at(d->axspans, i, "set_axspan_data");
    read_back::check_reference({data.lo, data.hi}, "set_axspan_data");
    p.lo = std::min(data.lo, data.hi);
    p.hi = std::max(data.lo, data.hi);
    p.data_stamp = next_snapshot_generation();
    return *this;
}

// The content is replaced whole; the style stays.
void Axes::Impl::set_text_data(std::size_t i, TextContent c, const char* who) {
    TextPlot& p = read_back::at(texts, i, who);
    read_back::check_text(c, who);
    warn_math(mathtext && p.opts.text.parse_math, who, c.text);
    p.content = std::move(c);
    p.data_stamp = next_snapshot_generation();
}

Axes& Axes::set_text_data(std::size_t i, const TextData& data) {
    d->set_text_data(i, read_back::content_of(data), "set_text_data");
    return *this;
}

Axes& Axes::set_text_data(std::size_t i, std::string_view s, Pos x, Pos y) {
    TextContent c = read_back::at(d->texts, i, "set_text_data").content;
    c.text = s;
    c.x = x;
    c.y = y;
    d->set_text_data(i, std::move(c), "set_text_data");
    return *this;
}

// --- Hiding plot objects ----------------------------------------------------

Axes& Axes::set_line_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->lines, i, visible, "set_line_visible");
    return *this;
}
Axes& Axes::set_scatter_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->scatters, i, visible, "set_scatter_visible");
    return *this;
}
Axes& Axes::set_scatter_z_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->scatter_z, i, visible, "set_scatter_z_visible");
    return *this;
}
Axes& Axes::set_bar_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->bars, i, visible, "set_bar_visible");
    return *this;
}
Axes& Axes::set_heatmap_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->heatmaps, i, visible, "set_heatmap_visible");
    return *this;
}
Axes& Axes::set_fill_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->fills, i, visible, "set_fill_visible");
    return *this;
}
Axes& Axes::set_shape_poly_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->polys, i, visible, "set_shape_poly_visible");
    return *this;
}
Axes& Axes::set_shape_circle_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->circles, i, visible, "set_shape_circle_visible");
    return *this;
}
Axes& Axes::set_shape_wedge_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->wedges, i, visible, "set_shape_wedge_visible");
    return *this;
}
Axes& Axes::set_contour_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->contours, i, visible, "set_contour_visible");
    return *this;
}
bool Axes::contour_visible(std::size_t i) const { return read_back::visible(d->contours, i, "contour_visible"); }
Axes& Axes::set_heatmap_tri_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->heatmap_tris, i, visible, "set_heatmap_tri_visible");
    return *this;
}
Axes& Axes::set_quiver_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->quivers, i, visible, "set_quiver_visible");
    return *this;
}
Axes& Axes::set_axline_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->axlines, i, visible, "set_axline_visible");
    return *this;
}
Axes& Axes::set_axspan_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->axspans, i, visible, "set_axspan_visible");
    return *this;
}
Axes& Axes::set_text_visible(std::size_t i, bool visible) {
    read_back::set_visible(d->texts, i, visible, "set_text_visible");
    return *this;
}

bool Axes::line_visible(std::size_t i) const      { return read_back::visible(d->lines, i, "line_visible"); }
bool Axes::scatter_visible(std::size_t i) const   { return read_back::visible(d->scatters, i, "scatter_visible"); }
bool Axes::scatter_z_visible(std::size_t i) const { return read_back::visible(d->scatter_z, i, "scatter_z_visible"); }
bool Axes::bar_visible(std::size_t i) const       { return read_back::visible(d->bars, i, "bar_visible"); }
bool Axes::heatmap_visible(std::size_t i) const   { return read_back::visible(d->heatmaps, i, "heatmap_visible"); }
bool Axes::fill_visible(std::size_t i) const      { return read_back::visible(d->fills, i, "fill_visible"); }
bool Axes::shape_poly_visible(std::size_t i) const   { return read_back::visible(d->polys, i, "shape_poly_visible"); }
bool Axes::shape_circle_visible(std::size_t i) const { return read_back::visible(d->circles, i, "shape_circle_visible"); }
bool Axes::shape_wedge_visible(std::size_t i) const  { return read_back::visible(d->wedges, i, "shape_wedge_visible"); }
bool Axes::heatmap_tri_visible(std::size_t i) const  { return read_back::visible(d->heatmap_tris, i, "heatmap_tri_visible"); }
bool Axes::quiver_visible(std::size_t i) const    { return read_back::visible(d->quivers, i, "quiver_visible"); }
bool Axes::axline_visible(std::size_t i) const    { return read_back::visible(d->axlines, i, "axline_visible"); }
bool Axes::axspan_visible(std::size_t i) const    { return read_back::visible(d->axspans, i, "axspan_visible"); }
bool Axes::text_visible(std::size_t i) const      { return read_back::visible(d->texts, i, "text_visible"); }

// set_*_data(): find object i, validate the new data as plotting it would, then
// swap it in. Nothing changes if either throws.
void Axes::Impl::set_line_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                               const char* who) {
    LinePlot& p = read_back::at(lines, i, who);
    if (x.size() != y.size())
        throw std::invalid_argument(std::string(who) + ": x and y must have the same length");
    read_back::keep_aligned(p, x.size() == p.x.size());
    p.x = read_back::own(x);
    p.y = read_back::own(y);
}

void Axes::Impl::set_scatter_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                  const std::vector<double>* sizes, const char* who) {
    ScatterPlot& p = read_back::at(scatters, i, who);
    if (x.size() != y.size())
        throw std::invalid_argument(std::string(who) + ": x and y must have the same length");
    if (sizes) read_back::check_sizes(*sizes, x.size(), who);
    read_back::keep_aligned(p, x.size() == p.x.size());
    p.x = read_back::own(x);
    p.y = read_back::own(y);
    if (sizes) p.sizes = *sizes;
}

void Axes::Impl::set_scatter_z_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                    std::span<const double> z, const std::vector<double>* sizes,
                                    const char* who) {
    ScatterZPlot& p = read_back::at(scatter_z, i, who);
    if (x.size() != y.size() || x.size() != z.size())
        throw std::invalid_argument(std::string(who) + ": x, y, and z must have the same length");
    if (sizes) read_back::check_sizes(*sizes, x.size(), who);
    read_back::keep_aligned(p, x.size() == p.x.size());
    p.x = read_back::own(x);
    p.y = read_back::own(y);
    p.z = read_back::own(z);
    if (sizes) p.sizes = *sizes;
}

void Axes::Impl::set_bar_data(std::size_t i, std::span<const double> x, std::span<const double> height,
                              const std::vector<double>* bottom, const char* who) {
    BarPlot& p = read_back::at(bars, i, who);
    if (x.size() != height.size())
        throw std::invalid_argument(std::string(who) + ": x and height must have the same length");
    std::vector<double> bases;
    if (bottom) bases = read_back::take_bottom(*bottom, x.size(), who);
    // The width stays (the panel may have set it) unless the bars moved.
    if (!read_back::same(p.centers.get(), x)) p.bar_width = bar_width_for(x, p.opts);
    read_back::keep_aligned(p, x.size() == p.centers.size());
    p.centers = read_back::own(x);
    p.heights = read_back::own(height);
    if (bottom) p.bottom = std::move(bases);
}

// The mask stays while the point count does (a span overload), else it goes.
void Axes::Impl::set_fill_data(std::size_t i, std::span<const double> x, std::span<const double> y1,
                               std::span<const double> y2, const std::vector<bool>* exclude,
                               const char* who) {
    FillPlot& p = read_back::at(fills, i, who);
    read_back::check_fill(x.size(), y1.size(), y2.size(), who);
    std::vector<double> mask;
    if (exclude) mask = read_back::take_exclude(*exclude, x.size(), who);
    if (exclude) p.exclude = std::move(mask);
    else if (x.size() != p.x.size()) p.exclude = std::vector<double>{};
    p.x = read_back::own(x);
    p.y1 = read_back::own(y1);
    p.y2 = read_back::own(y2);
    p.data_stamp = next_snapshot_generation();
}

void Axes::Impl::set_quiver_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                 std::span<const double> u, std::span<const double> v,
                                 const std::vector<double>* c, const char* who) {
    QuiverPlot& p = read_back::at(quivers, i, who);
    read_back::check_quiver(x.size(), y.size(), u.size(), v.size(), who);
    if (c) read_back::check_quiver_c(c->size(), x.size(), who);
    if (c) p.c = read_back::own(std::span<const double>(*c));
    else if (x.size() != p.x.size()) p.c = CowVec<double>{};
    p.x = read_back::own(x);
    p.y = read_back::own(y);
    p.u = read_back::own(u);
    p.v = read_back::own(v);
    p.data_stamp = next_snapshot_generation();
}

void Axes::Impl::set_heatmap_data(std::size_t i, std::span<const double> data, int rows, int cols,
                                  Range xrange, Range yrange, std::optional<int> channels,
                                  const std::vector<double>* x_edges, const std::vector<double>* y_edges,
                                  const char* who) {
    HeatmapPlot& p = read_back::at(heatmaps, i, who);
    const std::string w = who;
    // A heatmap stays a heatmap and an image keeps its channel count.
    if (channels && *channels != p.channels)
        throw std::invalid_argument(w + ": heatmap " + std::to_string(i) + " has "
                                    + std::to_string(p.channels) + " channel(s), the data "
                                    + std::to_string(*channels));
    // A mesh takes edges, an even grid ranges.
    const bool edges_given = x_edges && (!x_edges->empty() || !y_edges->empty());
    if (p.mesh() && !edges_given)
        throw std::invalid_argument(w + ": heatmap " + std::to_string(i) + " is a heatmap_rec(); "
                                    "give its x_edges and y_edges through HeatmapData");
    if (!p.mesh() && edges_given)
        throw std::invalid_argument(w + ": heatmap " + std::to_string(i) + " is an even grid; "
                                    "it takes xrange and yrange, not edges");
    if (p.mesh()) std::tie(xrange, yrange) = mesh_extent(*x_edges, *y_edges, rows, cols, w);
    std::vector<float> cells = heatmap_cells(data, rows, cols, xrange, yrange, w, p.channels);
    read_back::keep_aligned(p, rows == p.rows && cols == p.cols);
    p.data = std::move(cells);
    p.rows = rows;
    p.cols = cols;
    p.xrange = xrange;
    p.yrange = yrange;
    if (p.mesh()) {
        p.x_edges = *x_edges;
        p.y_edges = *y_edges;
    }
}

void Axes::Impl::set_contour_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                  std::span<const double> z, const std::vector<double>* levels, const bool* filled,
                                  const char* who) {
    ContourPlot& p = read_back::at(contours, i, who);
    ContourOptions o = p.opts;
    if (levels) o.levels = *levels;
    if (filled) o.filled = *filled;
    check_contour(x, y, z, o, who);
    p.x = read_back::own(x);
    p.y = read_back::own(y);
    p.z = read_back::own(z);
    p.opts = std::move(o);
    p.data_stamp = next_snapshot_generation();
    contour_rebuild(p);
}

void Axes::Impl::set_heatmap_tri_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                      std::span<const double> values, const std::vector<std::uint32_t>* tri,
                                      const char* who) {
    HeatmapTriPlot& p = read_back::at(heatmap_tris, i, who);
    read_back::check_tri_vertices(x, y, values, who);
    if (tri) {
        read_back::check_tri_indices(*tri, x.size(), who);
    } else if (x.size() != p.x.size()) {
        throw std::invalid_argument(std::string(who) + ": without tri the vertex count must stay ("
                                    + std::to_string(p.x.size()) + "), as the triangles are kept");
    }
    read_back::keep_aligned(p, x.size() == p.x.size());
    p.x = read_back::own(x);
    p.y = read_back::own(y);
    p.values = read_back::own(values);
    if (tri) p.tri = *tri;
}

Axes& Axes::set_line_data(std::size_t i, const LineData& data) {
    d->set_line_data(i, data.x, data.y, "set_line_data");
    return *this;
}
Axes& Axes::set_line_data(std::size_t i, std::span<const double> x, std::span<const double> y) {
    d->set_line_data(i, x, y, "set_line_data");
    return *this;
}
Axes& Axes::set_scatter_data(std::size_t i, const ScatterData& data) {
    d->set_scatter_data(i, data.x, data.y, &data.sizes, "set_scatter_data");
    return *this;
}
Axes& Axes::set_scatter_data(std::size_t i, std::span<const double> x, std::span<const double> y) {
    d->set_scatter_data(i, x, y, nullptr, "set_scatter_data");
    return *this;
}
Axes& Axes::set_scatter_z_data(std::size_t i, const ScatterZData& data) {
    d->set_scatter_z_data(i, data.x, data.y, data.z, &data.sizes, "set_scatter_z_data");
    return *this;
}
Axes& Axes::set_scatter_z_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                               std::span<const double> z) {
    d->set_scatter_z_data(i, x, y, z, nullptr, "set_scatter_z_data");
    return *this;
}
Axes& Axes::set_bar_data(std::size_t i, const BarData& data) {
    d->set_bar_data(i, data.x, data.height, &data.bottom, "set_bar_data");
    return *this;
}
Axes& Axes::set_bar_data(std::size_t i, std::span<const double> x, std::span<const double> height) {
    d->set_bar_data(i, x, height, nullptr, "set_bar_data");
    return *this;
}
Axes& Axes::set_fill_data(std::size_t i, const FillData& data) {
    d->set_fill_data(i, data.x, data.y1, data.y2, &data.exclude, "set_fill_data");
    return *this;
}
Axes& Axes::set_fill_data(std::size_t i, std::span<const double> x, std::span<const double> y1,
                          std::span<const double> y2) {
    d->set_fill_data(i, x, y1, y2, nullptr, "set_fill_data");
    return *this;
}
Axes& Axes::set_quiver_data(std::size_t i, const QuiverData& data) {
    d->set_quiver_data(i, data.x, data.y, data.u, data.v, &data.c, "set_quiver_data");
    return *this;
}
Axes& Axes::set_quiver_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                            std::span<const double> u, std::span<const double> v) {
    d->set_quiver_data(i, x, y, u, v, nullptr, "set_quiver_data");
    return *this;
}
Axes& Axes::set_heatmap_data(std::size_t i, const HeatmapData& data) {
    d->set_heatmap_data(i, data.data, data.rows, data.cols, data.xrange, data.yrange,
                        data.channels, &data.x_edges, &data.y_edges, "set_heatmap_data");
    return *this;
}
Axes& Axes::set_heatmap_data(std::size_t i, std::span<const double> data, int rows, int cols,
                             Range xrange, Range yrange) {
    d->set_heatmap_data(i, data, rows, cols, xrange, yrange, std::nullopt, nullptr, nullptr,
                        "set_heatmap_data");
    return *this;
}

} // namespace sextant
