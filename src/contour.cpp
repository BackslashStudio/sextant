#include "contour.h"
#include "text_metrics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace sextant {
namespace {

struct Pt { float x, y; };

// Pixels of clear space either side of a label, inside the gap cut into the
// line. Small: the text already carries its own side bearings.
constexpr float kLabelPad = 3.0f;

// A line must be this many times the gap before it is worth breaking. Below
// it the two remaining stubs are too short to read as a line at all, so the
// label is dropped and the line drawn whole.
constexpr float kMinLabelledLength = 1.8f;

constexpr float kPi     = 3.14159265358979f;
constexpr float kHalfPi = kPi * 0.5f;

// One level's worth of marching squares, appended to `out`.
void trace_level(const HeatmapPlot& hp, double level, ContourSet& out) {
    const int  nx    = hp.cols, ny = hp.rows;
    const bool lower = (hp.opts.origin == "lower");

    auto val = [&](int i, int j) -> double {
        const int row = lower ? i : (ny - 1 - i);
        return static_cast<double>(hp.data[static_cast<std::size_t>(row) * nx + j]);
    };

    // Edge identity, so crossings shared by two neighbouring cells are the
    // same entry rather than two floating-point-equal-ish points that have to
    // be matched by distance. Chaining below is therefore exact.
    const int h_count = ny * (nx - 1);
    auto h_id = [&](int i, int j) { return i * (nx - 1) + j; };
    auto v_id = [&](int i, int j) { return h_count + i * nx + j; };

    // Where the level crosses an edge, linearly between its two samples.
    // Computed identically from either adjoining cell (same two values, same
    // endpoints), so both cells agree on the point bit for bit.
    auto hpt = [&](int i, int j) {
        const double a = val(i, j), b = val(i, j + 1);
        const double t = (b != a) ? std::clamp((level - a) / (b - a), 0.0, 1.0) : 0.5;
        return Pt{ static_cast<float>(j + 0.5 + t), static_cast<float>(i + 0.5) };
    };
    auto vpt = [&](int i, int j) {
        const double a = val(i, j), b = val(i + 1, j);
        const double t = (b != a) ? std::clamp((level - a) / (b - a), 0.0, 1.0) : 0.5;
        return Pt{ static_cast<float>(j + 0.5), static_cast<float>(i + 0.5 + t) };
    };

    std::unordered_map<int, Pt>               pts;
    std::vector<std::pair<int, int>>          segs;   // (edge, edge)
    std::unordered_map<int, std::vector<int>> at;     // edge -> segment indices

    auto add_seg = [&](int ea, Pt pa, int eb, Pt pb) {
        pts.emplace(ea, pa);
        pts.emplace(eb, pb);
        const int s = static_cast<int>(segs.size());
        segs.emplace_back(ea, eb);
        at[ea].push_back(s);
        at[eb].push_back(s);
    };

    for (int i = 0; i + 1 < ny; ++i) {
        for (int j = 0; j + 1 < nx; ++j) {
            const double bl = val(i, j),     br = val(i, j + 1);
            const double tl = val(i + 1, j), tr = val(i + 1, j + 1);

            const int idx = (bl >= level ? 1 : 0) | (br >= level ? 2 : 0)
                          | (tr >= level ? 4 : 0) | (tl >= level ? 8 : 0);
            if (idx == 0 || idx == 15) continue;   // wholly in or wholly out

            const int eB = h_id(i, j),     eT = h_id(i + 1, j);
            const int eL = v_id(i, j),     eR = v_id(i, j + 1);

            auto LB = [&] { add_seg(eL, vpt(i, j),     eB, hpt(i, j)); };
            auto BR = [&] { add_seg(eB, hpt(i, j),     eR, vpt(i, j + 1)); };
            auto LR = [&] { add_seg(eL, vpt(i, j),     eR, vpt(i, j + 1)); };
            auto RT = [&] { add_seg(eR, vpt(i, j + 1), eT, hpt(i + 1, j)); };
            auto BT = [&] { add_seg(eB, hpt(i, j),     eT, hpt(i + 1, j)); };
            auto LT = [&] { add_seg(eL, vpt(i, j),     eT, hpt(i + 1, j)); };

            switch (idx) {
                case 1:  case 14: LB(); break;
                case 2:  case 13: BR(); break;
                case 3:  case 12: LR(); break;
                case 4:  case 11: RT(); break;
                case 6:  case 9:  BT(); break;
                case 7:  case 8:  LT(); break;

                // Saddles. The centre value says whether the two diagonally
                // opposite "inside" corners are joined through the middle
                // (so the line goes around the other two separately) or are
                // two islands (so it goes around each of them).
                case 5: {
                    const double c = (bl + br + tr + tl) * 0.25;
                    if (c >= level) { BR(); LT(); } else { LB(); RT(); }
                    break;
                }
                case 10: {
                    const double c = (bl + br + tr + tl) * 0.25;
                    if (c >= level) { LB(); RT(); } else { BR(); LT(); }
                    break;
                }
                default: break;
            }
        }
    }

    if (segs.empty()) return;

    // Chain the loose segments into polylines. Walking from an edge only one
    // segment touches (a line running off the sample grid) first matters:
    // start in the middle of an open chain instead and its other half is
    // stranded, coming out as a second line that a label would label twice.
    std::vector<bool> used(segs.size(), false);

    auto walk = [&](std::size_t start_seg, int start_edge) {
        ContourLine line;
        line.level = level;
        int         e = start_edge;
        std::size_t s = start_seg;

        const Pt p0 = pts[e];
        line.x.push_back(p0.x);
        line.y.push_back(p0.y);
        for (;;) {
            used[s] = true;
            const int other = (segs[s].first == e) ? segs[s].second : segs[s].first;
            const Pt  p     = pts[other];
            line.x.push_back(p.x);
            line.y.push_back(p.y);
            e = other;

            std::size_t next = segs.size();
            for (int cand : at[e]) {
                if (!used[static_cast<std::size_t>(cand)]) {
                    next = static_cast<std::size_t>(cand);
                    break;
                }
            }
            if (next == segs.size()) break;
            s = next;
        }
        line.closed = (e == start_edge);
        out.push_back(std::move(line));
    };

    // Both passes iterate `segs` in index order (cell order), not the hash
    // maps, so the traced lines come out in the same order every run — the
    // SVG writer emits them in this order and that output is compared byte
    // for byte by the tests.
    for (std::size_t s = 0; s < segs.size(); ++s) {
        if (used[s]) continue;
        if (at[segs[s].first].size() == 1)       walk(s, segs[s].first);
        else if (at[segs[s].second].size() == 1) walk(s, segs[s].second);
    }
    for (std::size_t s = 0; s < segs.size(); ++s)
        if (!used[s]) walk(s, segs[s].first);   // what is left is closed loops
}

} // namespace

ContourSet trace_contours(const HeatmapPlot& hp) {
    ContourSet out;
    if (hp.rows < 2 || hp.cols < 2 || hp.opts.contours.empty()) return out;
    if (hp.data.size() < static_cast<std::size_t>(hp.rows) * hp.cols) return out;
    for (double level : hp.opts.contours) trace_level(hp, level, out);
    return out;
}

std::string format_contour_level(double level) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", level);
    return buf;
}

ContourDraw plan_contours(const ContourSet& set, const CoordTransform& tr,
                          const HeatmapOptions& opts,
                          const std::string& font_path)
{
    ContourDraw out;
    std::vector<float> px, py, arc;

    for (const auto& line : set) {
        const std::size_t n = line.x.size();
        if (n < 2) continue;

        px.resize(n);
        py.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            px[i] = tr.to_px(static_cast<double>(line.x[i]));
            py[i] = tr.to_py(static_cast<double>(line.y[i]));
        }

        // Cheap whole-line reject. Everything downstream is clipped to the
        // frame anyway, but a zoomed-in view can leave most of a large grid's
        // lines entirely offscreen, and this skips their arc-length pass.
        const auto [xlo, xhi] = std::minmax_element(px.begin(), px.end());
        const auto [ylo, yhi] = std::minmax_element(py.begin(), py.end());
        if (*xhi < tr.px || *xlo > tr.px + tr.pw ||
            *yhi < tr.py || *ylo > tr.py + tr.ph) continue;

        auto push_whole = [&] {
            ContourRun r;
            r.px.assign(px.begin(), px.begin() + static_cast<std::ptrdiff_t>(n));
            r.py.assign(py.begin(), py.begin() + static_cast<std::ptrdiff_t>(n));
            out.runs.push_back(std::move(r));
        };

        if (!opts.contour_labels) { push_whole(); continue; }

        // Arc length in *pixels*, because that is what the label's width is
        // measured in — the gap has to be as wide as the text on screen, not
        // as wide as some data-space equivalent that changes with the zoom.
        arc.resize(n);
        arc[0] = 0.0f;
        for (std::size_t i = 1; i < n; ++i)
            arc[i] = arc[i - 1] + std::hypot(px[i] - px[i - 1], py[i] - py[i - 1]);
        const float total = arc[n - 1];

        const std::string text = format_contour_level(line.level);
        const float gap = text_width(font_path, opts.contour_fontsize, text)
                        + 2.0f * kLabelPad;
        if (total < gap * kMinLabelledLength) { push_whole(); continue; }

        auto at_arc = [&](float s) -> Pt {
            if (s <= 0.0f)    return { px[0], py[0] };
            if (s >= total)   return { px[n - 1], py[n - 1] };
            const std::size_t k = static_cast<std::size_t>(
                std::lower_bound(arc.begin(), arc.begin() + static_cast<std::ptrdiff_t>(n), s)
                - arc.begin());
            const float seg = arc[k] - arc[k - 1];
            const float t   = seg > 0.0f ? (s - arc[k - 1]) / seg : 0.0f;
            return { px[k - 1] + (px[k] - px[k - 1]) * t,
                     py[k - 1] + (py[k] - py[k - 1]) * t };
        };

        auto push_range = [&](float s0, float s1) {
            ContourRun r;
            const Pt a = at_arc(s0);
            r.px.push_back(a.x);
            r.py.push_back(a.y);
            for (std::size_t i = 0; i < n; ++i)
                if (arc[i] > s0 && arc[i] < s1) { r.px.push_back(px[i]); r.py.push_back(py[i]); }
            const Pt b = at_arc(s1);
            r.px.push_back(b.x);
            r.py.push_back(b.y);
            if (r.px.size() >= 2) out.runs.push_back(std::move(r));
        };

        const float mid = total * 0.5f;
        const float s0  = mid - gap * 0.5f;
        const float s1  = mid + gap * 0.5f;
        push_range(0.0f, s0);
        push_range(s1, total);

        const Pt anchor = at_arc(mid);
        const Pt a = at_arc(s0), b = at_arc(s1);
        // The chord across the gap, not the tangent at one vertex: it is the
        // direction the text has to span to fit in the hole. A line has no
        // direction of its own, so the angle is folded into the half-open
        // [-pi/2, pi/2) that keeps the text upright rather than clamped by a
        // pair of `if`s -- the fold is exact at the vertical case, where a
        // clamp depends on whether atan2f lands a hair either side of pi/2.
        // Both verticals come out as -pi/2, text reading bottom to top.
        float angle = std::atan2(b.y - a.y, b.x - a.x);
        angle = std::fmod(angle + kHalfPi, kPi);
        if (angle < 0.0f) angle += kPi;
        angle -= kHalfPi;

        out.labels.push_back({ anchor.x, anchor.y, angle, text });
    }

    return out;
}

const ContourSet& ContourCache::get(int axes_index, int plot_index,
                                    unsigned long long data_generation,
                                    const HeatmapPlot& hp)
{
    const long long key = (static_cast<long long>(axes_index) << 32)
                        | static_cast<long long>(static_cast<unsigned>(plot_index));
    Entry& e = entries_[key];

    const bool usable = data_generation != 0
                     && e.generation == data_generation
                     && e.levels == hp.opts.contours
                     && e.origin == hp.opts.origin
                     && e.rows == hp.rows && e.cols == hp.cols;
    if (!usable) {
        e.set        = trace_contours(hp);
        e.generation = data_generation;
        e.levels     = hp.opts.contours;
        e.origin     = hp.opts.origin;
        e.rows       = hp.rows;
        e.cols       = hp.cols;
    }
    return e.set;
}

} // namespace sextant
