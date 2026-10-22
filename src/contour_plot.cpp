#include "contour_plot.h"
#include "colormaps.h"
#include "coord_transform.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace sextant {
namespace {

// The largest nlevels taken: its nice levels stay within a boundary norm's limit.
constexpr int kMaxNLevels = static_cast<int>(kMaxNormBounds) - 2;

void sort_levels(std::vector<double>& lv) {
    lv.erase(std::remove_if(lv.begin(), lv.end(), [](double v) { return !std::isfinite(v); }), lv.end());
    std::sort(lv.begin(), lv.end());
    lv.erase(std::unique(lv.begin(), lv.end()), lv.end());
}

// x or y of a contour: at least two, finite, strictly monotone either way.
void check_axis(std::span<const double> v, const std::string& w, const char* name) {
    if (v.size() < 2) throw std::invalid_argument(w + ": " + name + " needs at least 2 values");
    for (double a : v)
        if (!std::isfinite(a)) throw std::invalid_argument(w + ": " + name + " must be finite");
    const bool up = v[1] > v[0];
    for (std::size_t i = 1; i < v.size(); ++i)
        if (up ? !(v[i] > v[i - 1]) : !(v[i] < v[i - 1]))
            throw std::invalid_argument(w + ": " + name + " must be strictly increasing or decreasing");
}

// One level's crossings of the triangles, chained into polylines. A crossing
// is keyed by its edge (both triangles sharing the edge find the same key), or
// by the vertex when it lies on one, so lines through grid points stay whole.
void trace_tri_level(const HeatmapTriPlot& m, double level, std::size_t level_index, ContourGeometry& g) {
    struct Pt { float x, y; };
    const std::size_t nv = m.values.size();
    std::unordered_map<long long, Pt> pts;
    std::vector<std::pair<long long, long long>> segs;
    std::unordered_map<long long, std::vector<std::size_t>> at;

    auto crossing = [&](std::uint32_t a, std::uint32_t b) -> long long {
        const std::uint32_t lo = std::min(a, b), hi = std::max(a, b);
        const double va = m.values[lo], vb = m.values[hi];
        const double t = vb != va ? std::clamp((level - va) / (vb - va), 0.0, 1.0) : 0.5;
        long long key;
        if (t == 0.0) key = -1 - static_cast<long long>(lo);
        else if (t == 1.0) key = -1 - static_cast<long long>(hi);
        else key = static_cast<long long>(lo) * static_cast<long long>(nv) + hi;
        if (!pts.count(key))
            pts.emplace(key, Pt{ static_cast<float>(m.x[lo] + t * (m.x[hi] - m.x[lo])),
                                 static_cast<float>(m.y[lo] + t * (m.y[hi] - m.y[lo])) });
        return key;
    };

    for (std::size_t t = 0; t + 2 < m.tri.size(); t += 3) {
        const std::uint32_t v[3] = { m.tri[t], m.tri[t + 1], m.tri[t + 2] };
        const bool up[3] = { m.values[v[0]] >= level, m.values[v[1]] >= level, m.values[v[2]] >= level };
        if (up[0] == up[1] && up[1] == up[2]) continue;
        long long e[2];
        int n = 0;
        for (int k = 0; k < 3; ++k) {
            const int k2 = (k + 1) % 3;
            if (up[k] != up[k2]) e[n++] = crossing(v[k], v[k2]);
        }
        if (n != 2 || e[0] == e[1]) continue;
        const std::size_t s = segs.size();
        segs.emplace_back(e[0], e[1]);
        at[e[0]].push_back(s);
        at[e[1]].push_back(s);
    }
    if (segs.empty()) return;

    std::vector<bool> used(segs.size(), false);
    auto walk = [&](std::size_t start_seg, long long start) {
        ContourLine line;
        line.level = level;
        long long e = start;
        std::size_t s = start_seg;
        line.x.push_back(pts[e].x);
        line.y.push_back(pts[e].y);
        for (;;) {
            used[s] = true;
            const long long other = segs[s].first == e ? segs[s].second : segs[s].first;
            line.x.push_back(pts[other].x);
            line.y.push_back(pts[other].y);
            e = other;
            std::size_t next = segs.size();
            for (std::size_t c : at[e])
                if (!used[c]) {
                    next = c;
                    break;
                }
            if (next == segs.size()) break;
            s = next;
        }
        line.closed = e == start;
        g.lines.push_back(std::move(line));
        g.line_level.push_back(level_index);
    };
    // Open chains from their ends first (in triangle order: deterministic), then loops.
    for (std::size_t s = 0; s < segs.size(); ++s) {
        if (used[s]) continue;
        if (at[segs[s].first].size() == 1) walk(s, segs[s].first);
        else if (at[segs[s].second].size() == 1) walk(s, segs[s].second);
    }
    for (std::size_t s = 0; s < segs.size(); ++s)
        if (!used[s]) walk(s, segs[s].first);
}

} // namespace

std::vector<double> contour_levels(const ContourOptions& o, std::span<const double> z) {
    if (!o.levels.empty()) {
        std::vector<double> lv = o.levels;
        sort_levels(lv);
        return lv;
    }
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (double v : z)
        if (std::isfinite(v)) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    if (!(lo <= hi)) return {};
    if (lo == hi) return { lo };
    const int n = std::clamp(o.nlevels, 1, kMaxNLevels);
    const double step = nice_step((hi - lo) / n);
    if (!(step > 0.0) || !std::isfinite(step)) return { lo, hi };
    const double first = std::floor(lo / step) * step;
    std::vector<double> lv;
    // first + k * step, not accumulated (as generate_ticks()); enough steps to
    // pass hi, never more than a boundary norm takes.
    const double steps = std::ceil((hi - first) / step) + 1.0;
    const int kmax = static_cast<int>(std::min(steps, static_cast<double>(kMaxNormBounds) - 1.0));
    for (int k = 0; k <= kmax; ++k) {
        const double v = first + k * step;
        lv.push_back(std::abs(v) < step * 1e-9 ? 0.0 : v);
        if (v >= hi - step * 1e-9) break;
    }
    return lv;
}

void contour_limits(const ContourOptions& o, const std::vector<double>& levels, float& vmin, float& vmax) {
    vmin = o.vmin ? *o.vmin : levels.empty() ? 0.0f : static_cast<float>(levels.front());
    vmax = o.vmax ? *o.vmax : levels.empty() ? 1.0f : static_cast<float>(levels.back());
}

void check_contour(std::span<const double> x, std::span<const double> y, std::span<const double> z,
                   ContourOptions& o, const char* who) {
    const std::string w = who;
    check_axis(x, w, "x");
    check_axis(y, w, "y");
    if (z.size() != x.size() * y.size())
        throw std::invalid_argument(w + ": z needs x.size() * y.size() values (row-major, one row per y)");
    for (double v : o.levels)
        if (!std::isfinite(v)) throw std::invalid_argument(w + ": contour levels must be finite");
    sort_levels(o.levels);
    if (o.levels.size() > kMaxNormBounds) throw std::invalid_argument(w + ": at most 16384 contour levels");
    if (o.nlevels < 1 || o.nlevels > kMaxNLevels)
        throw std::invalid_argument(w + ": nlevels must be 1 to 16382");
    float vmin = 0.0f, vmax = 1.0f;
    contour_limits(o, contour_levels(o, z), vmin, vmax);
    check_norm(o.norm, vmin, vmax, who);
}

void contour_rebuild(ContourPlot& p) {
    auto g = std::make_shared<ContourGeometry>();
    const std::span<const double> z(p.z.data(), p.z.size());
    g->levels = contour_levels(p.opts, z);
    contour_limits(p.opts, g->levels, g->vmin, g->vmax);
    g->norm = norm_in_force(p.opts.norm, g->vmin, g->vmax);
    auto pos = [&](double v) {
        if (g->norm == ColorNorm::Kind::Linear) {
            const double span = static_cast<double>(g->vmax) - g->vmin;
            return span != 0.0 ? (v - g->vmin) / span : 0.0;
        }
        return norm_t(g->norm, p.opts.norm, g->vmin, g->vmax, v);
    };
    for (double l : g->levels) g->level_t.push_back(pos(l));
    for (std::size_t i = 0; i + 1 < g->levels.size(); ++i)
        g->band_t.push_back(pos(0.5 * (g->levels[i] + g->levels[i + 1])));

    // The triangles: grid points, then one centre per square drawn.
    const int rows = p.rows(), cols = p.cols();
    if (rows >= 2 && cols >= 2 && p.z.size() == static_cast<std::size_t>(rows) * cols) {
        const std::size_t n = static_cast<std::size_t>(rows) * cols;
        std::vector<double> vx(n), vy(n), vv(n);
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) {
                const std::size_t k = static_cast<std::size_t>(i) * cols + j;
                vx[k] = p.x[static_cast<std::size_t>(j)];
                vy[k] = p.y[static_cast<std::size_t>(i)];
                vv[k] = p.z[k];
            }
        std::vector<std::uint32_t> tri;
        for (int i = 0; i + 1 < rows; ++i)
            for (int j = 0; j + 1 < cols; ++j) {
                const auto id = [cols](int r, int c) { return static_cast<std::uint32_t>(r * cols + c); };
                const std::uint32_t bl = id(i, j), br = id(i, j + 1), tl = id(i + 1, j), tr = id(i + 1, j + 1);
                if (std::isnan(vv[bl]) || std::isnan(vv[br]) || std::isnan(vv[tl]) || std::isnan(vv[tr])) continue;
                const auto c = static_cast<std::uint32_t>(vx.size());
                vx.push_back(0.5 * (vx[bl] + vx[br]));
                vy.push_back(0.5 * (vy[bl] + vy[tl]));
                vv.push_back(0.25 * (vv[bl] + vv[br] + vv[tl] + vv[tr]));
                tri.insert(tri.end(), { bl, br, c, br, tr, c, tr, tl, c, tl, bl, c });
            }
        g->fill.x = std::move(vx);
        g->fill.y = std::move(vy);
        g->fill.values = std::move(vv);
        g->fill.tri = std::move(tri);
    }
    g->fill.opts.cmap = p.opts.cmap;
    g->fill.data_stamp = p.data_stamp;
    if (p.opts.filled && g->levels.size() >= 2) {
        g->fill.opts.norm = ColorNorm::boundaries(g->levels);
        g->fill.band_t = g->band_t;
    }
    if (!p.opts.filled)
        for (std::size_t k = 0; k < g->levels.size(); ++k) trace_tri_level(g->fill, g->levels[k], k, *g);
    p.geom = std::move(g);
}

ColorbarRequest contour_colorbar_request(const ContourPlot& p) {
    const ContourGeometry& g = *p.geom;
    ColorbarRequest q{ p.opts.cmap, g.vmin, g.vmax, p.opts.name, {}, {} };
    if (p.opts.filled && g.levels.size() >= 2) {
        q.bounds = g.levels;
        q.band_t = g.band_t;
    } else if (g.norm == ColorNorm::Kind::Boundaries) {
        q.bounds = p.opts.norm.bounds;
        q.band_t = norm_band_positions(p.opts.norm);
    }
    if (!q.bounds.empty()) {
        q.vmin = static_cast<float>(q.bounds.front());
        q.vmax = static_cast<float>(q.bounds.back());
    }
    return q;
}

std::optional<Color> contour_line_color(const ContourPlot& p, std::size_t level) {
    if (p.opts.line_color) return *p.opts.line_color;
    if (!p.geom || level >= p.geom->level_t.size()) return std::nullopt;
    const double t = p.geom->level_t[level];
    if (std::isnan(t)) return std::nullopt;
    const std::uint8_t* c = &colormaps::get(p.opts.cmap)[colormaps::index(t, p.opts.cmap.is_cyclic()) * 4];
    return Color{ c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, 1.0f };
}

std::optional<ContourFillHit> contour_fill_at(const ContourPlot& p, const AxisScale& xs, const AxisScale& ys,
                                              double x, double y) {
    if (!p.geom || !p.opts.filled || p.geom->levels.size() < 2 || p.geom->fill.tri.empty()) return std::nullopt;
    const auto h = heatmap_tri_at(p.geom->fill, xs, ys, x, y);
    if (!h) return std::nullopt;
    const std::ptrdiff_t b = norm_band(p.geom->levels, h->value);
    if (b < 0 || std::isnan(p.geom->band_t[static_cast<std::size_t>(b)])) return std::nullopt;
    return ContourFillHit{ static_cast<std::size_t>(b), h->value };
}

} // namespace sextant
