#include "axes_impl.h"
#include "axis_limits.h"
#include "read_back.h"
#include "rich_text.h"
#include "ticker.h"
#include <stdexcept>
#include <string>
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

bool Axes::Impl::group_has_heatmap_x() const {
    if (!xshare) return !heatmaps.empty();
    for (const Impl* m : xshare->members)
        if (!m->heatmaps.empty()) return true;
    return false;
}
bool Axes::Impl::group_has_heatmap_y() const {
    if (!yshare) return !heatmaps.empty();
    for (const Impl* m : yshare->members)
        if (!m->heatmaps.empty()) return true;
    return false;
}

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

    // Everyone joining takes other's scale: refuse a log or symlog over a heatmap.
    const AxisScale& to = x ? other.xscale : other.yscale;
    const std::vector<Impl*> joining = og ? og->members : std::vector<Impl*>{this};
    // An axes new to sharing on this axis whose other axis is shared too can
    // no longer widen one under Datalim (set_aspect()).
    const auto boxed_in = [&](const Impl& m) { return m.aspect.datalim() && (x ? m.yshare : m.xshare); };
    if ((!og && boxed_in(*this)) || (!tg && boxed_in(other)))
        throw std::invalid_argument(
            std::string(who) + ": both axes of an axes with a Datalim aspect would be shared, "
            "leaving none to widen");
    if (!to.linear())
        for (const Impl* m : joining)
            if (!m->heatmaps.empty())
                throw std::invalid_argument(
                    std::string(who) + ": an axes holding a heatmap stays linear, and the other "
                    "axes' scale is not");

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
    // A fresh axes holds no heatmap; this throws only for a host with a
    // Datalim aspect whose other axis is shared already.
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

// One axis' set_*scale(). Validates what the scale reads, refuses a heatmap
// (see ingest_heatmap()), and drops set limits the new scale can't place.
// Takes fresh limit and tick stamps either way: a pan, zoom or panel scale
// made over the old scale is not replayed onto the new one.
void set_scale(bool has_heatmap, AxisScale& slot, double lo, double hi, bool& lim_auto,
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
        if (has_heatmap)
            throw std::invalid_argument(w + ": an axes holding a heatmap stays linear");
    }
    if (scale != Scale::Linear && scale != Scale::Log && scale != Scale::Symlog)
        throw std::invalid_argument(w + ": unknown scale");
    slot = make_axis_scale(scale, o);
    if (!lim_auto && !limits_fit(slot, lo, hi)) lim_auto = true;
    lim_stamp = next_snapshot_generation();
    tick_stamp = next_snapshot_generation();
}

// Checks a heatmap's shape and ranges and narrows the cells to float (they are
// uploaded as a float texture, and the copy is made here anyway).
std::vector<float> heatmap_cells(std::span<const double> data, int rows, int cols,
                                 Range xrange, Range yrange, const std::string& w) {
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

    std::vector<float> cells(static_cast<std::size_t>(rows) * cols);
    for (std::size_t i = 0; i < cells.size(); ++i)
        cells[i] = static_cast<float>(data[i]);
    return cells;
}

} // namespace

// Vector-shaped ingests, shared by Axes and Plane2D. `who` names the caller in
// error messages.
void Axes::Impl::ingest_line(std::span<const double> x, std::span<const double> y,
                             const ErrorBar& eb, LineOptions opts, const char* who) {
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
        next_object_id(),
    });
}

void Axes::Impl::ingest_scatter(std::span<const double> x, std::span<const double> y,
                                const ErrorBar& eb, ScatterOptions opts, const char* who) {
    if (x.size() != y.size())
        throw std::invalid_argument(std::string(who) + ": x and y must have the same length");
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    scatters.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::move(err),
        std::move(opts),
        next_snapshot_generation(),
        next_object_id(),
    });
}

void Axes::Impl::ingest_scatter_z(std::span<const double> x, std::span<const double> y,
                                  std::span<const double> z, const ErrorBar& eb,
                                  ScatterZOptions opts, const char* who) {
    if (x.size() != y.size() || x.size() != z.size())
        throw std::invalid_argument(std::string(who) + ": x, y, and z must have the same length");
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    warn_math(mathtext, who, opts.name);
    scatter_z.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(y.begin(), y.end()),
        std::vector<double>(z.begin(), z.end()),
        std::move(err),
        std::move(opts),
        next_snapshot_generation(),
        next_object_id(),
    });
}

void Axes::Impl::ingest_bar(std::span<const double> x, std::span<const double> height,
                            const ErrorBar& eb, BarOptions opts, const char* who) {
    if (x.size() != height.size())
        throw std::invalid_argument(std::string(who) + ": x and height must have the same length");
    ErrorBarData err = take_error_bars(eb, x.size(), who);
    warn_math(mathtext, who, opts.name);
    cycle->resolve(opts.color);
    bars.push_back({
        std::vector<double>(x.begin(), x.end()),
        std::vector<double>(height.begin(), height.end()),
        bar_width_for(x, opts),
        std::move(err),
        std::move(opts),
        next_snapshot_generation(),
        next_object_id(),
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
    warn_math(d->mathtext, "hist", bar_opts.name);
    d->cycle->resolve(bar_opts.color);

    d->bars.push_back({
        std::move(centers),
        std::move(counts),
        bin_w * static_cast<double>(bar_opts.width),
        ErrorBarData{},
        std::move(bar_opts),
        next_snapshot_generation(),
        next_object_id(),
    });
    return *this;
}

void Axes::Impl::ingest_heatmap(std::span<const double> data, int rows, int cols,
                                Range xrange, Range yrange, HeatmapOptions opts,
                                const char* who) {
    const std::string w = who;
    // Cells are even in data units, so they cannot follow a scale; a mesh kind
    // with its own cell edges is the way to scaled cells.
    if (!xscale.linear() || !yscale.linear())
        throw std::invalid_argument(w + ": a heatmap needs linear axes; this axes has a "
                                    + std::string(!xscale.linear() ? "x" : "y") + " scale set");
    std::vector<float> cells = heatmap_cells(data, rows, cols, xrange, yrange, w);

    // Every reader tests == "lower", so anything else would silently mean upper.
    if (opts.origin != "lower" && opts.origin != "upper")
        throw std::invalid_argument(w + ": origin must be \"lower\" or \"upper\", not \""
                                    + opts.origin + "\"");

    // Sort and de-duplicate once. Check finiteness first (NaN doesn't sort).
    for (double level : opts.contours)
        if (!std::isfinite(level))
            throw std::invalid_argument(w + ": contour levels must be finite");
    std::sort(opts.contours.begin(), opts.contours.end());
    opts.contours.erase(std::unique(opts.contours.begin(), opts.contours.end()),
                        opts.contours.end());

    warn_math(mathtext, who, opts.name);
    heatmaps.push_back({
        std::move(cells),
        rows, cols,
        xrange, yrange,
        std::move(opts),
        next_snapshot_generation(),
        next_object_id(),
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

void Axes::Impl::ingest_text(TextContent c, TextStyle style, const char* who) {
    read_back::check_text(c, who);
    warn_math(mathtext && style.text.parse_math, who, c.text);
    texts.push_back({ std::move(c), std::move(style), next_snapshot_generation(), next_object_id() });
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
    // The scale is the group's: a heatmap on any member refuses it, and every
    // member's ticks lane takes the new stamp.
    set_scale(d->group_has_heatmap_x(), d->xscale, d->xmin, d->xmax, d->xlim_auto, d->limit_stamps.x,
              d->style_stamps.xticks, scale, opts, "set_xscale");
    d->sync_shared_x();
    if (d->xshare)
        for (Impl* m : d->xshare->members) m->style_stamps.xticks = d->style_stamps.xticks;
    return *this;
}
Axes& Axes::set_yscale(Scale scale, ScaleOptions opts) {
    set_scale(d->group_has_heatmap_y(), d->yscale, d->ymin, d->ymax, d->ylim_auto, d->limit_stamps.y,
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
                                  const char* who) {
    ScatterPlot& p = read_back::at(scatters, i, who);
    if (x.size() != y.size())
        throw std::invalid_argument(std::string(who) + ": x and y must have the same length");
    read_back::keep_aligned(p, x.size() == p.x.size());
    p.x = read_back::own(x);
    p.y = read_back::own(y);
}

void Axes::Impl::set_scatter_z_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                                    std::span<const double> z, const char* who) {
    ScatterZPlot& p = read_back::at(scatter_z, i, who);
    if (x.size() != y.size() || x.size() != z.size())
        throw std::invalid_argument(std::string(who) + ": x, y, and z must have the same length");
    read_back::keep_aligned(p, x.size() == p.x.size());
    p.x = read_back::own(x);
    p.y = read_back::own(y);
    p.z = read_back::own(z);
}

void Axes::Impl::set_bar_data(std::size_t i, std::span<const double> x, std::span<const double> height,
                              const char* who) {
    BarPlot& p = read_back::at(bars, i, who);
    if (x.size() != height.size())
        throw std::invalid_argument(std::string(who) + ": x and height must have the same length");
    // The width stays (the panel may have set it) unless the bars moved.
    if (!read_back::same(p.centers.get(), x)) p.bar_width = bar_width_for(x, p.opts);
    read_back::keep_aligned(p, x.size() == p.centers.size());
    p.centers = read_back::own(x);
    p.heights = read_back::own(height);
}

void Axes::Impl::set_heatmap_data(std::size_t i, std::span<const double> data, int rows, int cols,
                                  Range xrange, Range yrange, const char* who) {
    HeatmapPlot& p = read_back::at(heatmaps, i, who);
    std::vector<float> cells = heatmap_cells(data, rows, cols, xrange, yrange, who);
    read_back::keep_aligned(p, rows == p.rows && cols == p.cols);
    p.data = std::move(cells);
    p.rows = rows;
    p.cols = cols;
    p.xrange = xrange;
    p.yrange = yrange;
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
    d->set_scatter_data(i, data.x, data.y, "set_scatter_data");
    return *this;
}
Axes& Axes::set_scatter_data(std::size_t i, std::span<const double> x, std::span<const double> y) {
    d->set_scatter_data(i, x, y, "set_scatter_data");
    return *this;
}
Axes& Axes::set_scatter_z_data(std::size_t i, const ScatterZData& data) {
    d->set_scatter_z_data(i, data.x, data.y, data.z, "set_scatter_z_data");
    return *this;
}
Axes& Axes::set_scatter_z_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                               std::span<const double> z) {
    d->set_scatter_z_data(i, x, y, z, "set_scatter_z_data");
    return *this;
}
Axes& Axes::set_bar_data(std::size_t i, const BarData& data) {
    d->set_bar_data(i, data.x, data.height, "set_bar_data");
    return *this;
}
Axes& Axes::set_bar_data(std::size_t i, std::span<const double> x, std::span<const double> height) {
    d->set_bar_data(i, x, height, "set_bar_data");
    return *this;
}
Axes& Axes::set_heatmap_data(std::size_t i, const HeatmapData& data) {
    d->set_heatmap_data(i, data.data, data.rows, data.cols, data.xrange, data.yrange,
                        "set_heatmap_data");
    return *this;
}
Axes& Axes::set_heatmap_data(std::size_t i, std::span<const double> data, int rows, int cols,
                             Range xrange, Range yrange) {
    d->set_heatmap_data(i, data, rows, cols, xrange, yrange, "set_heatmap_data");
    return *this;
}

} // namespace sextant
