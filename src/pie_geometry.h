#pragma once
// Axes::pie() as slices: each slice's sweep, its explode offset, and where its
// label and percent sit, shared by the GL path, the SVG writer, the hit test,
// the limits, the legend and the tests (the shape_geometry.h pattern). A slice
// is drawn as a wedge_geometry() in data units, so a pie is an ellipse where
// the axes' units differ, as matplotlib's.
//
// The slices themselves (pie_slices()) and the limits are in shape_limits.h,
// which autoscale reads.
#include "plot_objects.h"
#include "shape_geometry.h"
#include "shape_limits.h"

#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sextant {

// Slice k's fill color (opts.colors is never empty on a stored pie).
inline Color pie_color(const PieOptions& o, std::size_t k) {
    return o.colors.empty() ? Color::Blue : o.colors[k % o.colors.size()];
}

// Slice k's outline color: `edgecolor`, else the slice's, faded by `alpha`
// (shape_edge() for a pie).
inline Color pie_edge(const PieOptions& o, std::size_t k) {
    Color c = o.edgecolor.value_or(pie_color(o, k));
    c.a *= std::clamp(o.alpha, 0.0f, 1.0f);
    return c;
}

inline ShapeGeom pie_slice_geometry(const PieSlice& s, const PieOptions& o, const CoordTransform& tr) {
    return wedge_geometry(Pos(s.cx), Pos(s.cy), Pos(o.radius), s.theta1, s.theta2, Pos(o.inner), tr);
}

// The topmost slice under pixel (x, y) (the last drawn is on top), or nothing.
inline std::optional<std::size_t> pie_slice_at(const PiePlot& p, const CoordTransform& tr, float x, float y) {
    const std::vector<PieSlice> slices = pie_slices(p);
    for (std::size_t i = slices.size(); i-- > 0;)
        if (shape_contains(pie_slice_geometry(slices[i], p.opts, tr), x, y)) return slices[i].k;
    return std::nullopt;
}

// --- autopct ---------------------------------------------------------------

// A parsed PieOptions::autopct: the text around its one conversion (each "%%"
// already a '%'), and the conversion as a printf spec ("" = no percents).
struct PieFormat {
    std::string head, tail, spec;
    char conv = 0;   // f F g G e E d i; 0 = none
};

// Parses an autopct: nothing for one with a conversion other than the one
// printf form PieOptions names, a stray '%', or not exactly one conversion.
inline std::optional<PieFormat> parse_autopct(std::string_view s) {
    PieFormat f;
    if (s.empty()) return f;
    std::string* text = &f.head;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '%') {
            *text += s[i++];
            continue;
        }
        if (i + 1 < s.size() && s[i + 1] == '%') {
            *text += '%';
            i += 2;
            continue;
        }
        if (f.conv) return std::nullopt;   // a second conversion
        std::size_t j = i + 1;
        while (j < s.size() && std::string_view("-+ 0#").find(s[j]) != std::string_view::npos) ++j;
        const auto digits = [&] {
            const std::size_t from = j;
            while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
            return j - from;
        };
        if (digits() > 2) return std::nullopt;   // a width past 99
        if (j < s.size() && s[j] == '.') {
            ++j;
            if (digits() > 2) return std::nullopt;
        }
        if (j >= s.size() || std::string_view("fFgGeEdi").find(s[j]) == std::string_view::npos)
            return std::nullopt;
        f.conv = s[j];
        f.spec.assign(s.substr(i, j - i));
        // printf's integer conversions take an int: the spec ends in 'd' and the
        // percent is truncated, as Python's '%d' % 37.5 is "37".
        f.spec += f.conv == 'i' ? 'd' : f.conv;
        text = &f.tail;
        i = j + 1;
    }
    if (!f.conv) return std::nullopt;
    return f;
}

// `pct` (0..100) as `f` writes it.
inline std::string format_pct(const PieFormat& f, double pct) {
    if (!f.conv) return {};
    char buf[160];
    if (f.conv == 'd' || f.conv == 'i')
        std::snprintf(buf, sizeof(buf), f.spec.c_str(), static_cast<int>(pct));
    else
        std::snprintf(buf, sizeof(buf), f.spec.c_str(), pct);
    return f.head + buf + f.tail;
}

// --- texts -----------------------------------------------------------------

// A pie's labels (at labeldistance, aligned away from the centre) and percents
// (at pctdistance, centred) as texts in data units, drawn with the axes' own
// (plan_texts()). Labels of empty name and zero-width slices still draw, as
// matplotlib's. `fontsize` is the size an unset PieOptions::fontsize takes.
inline std::vector<TextPlot> pie_texts(const PiePlot& p, float fontsize) {
    std::vector<TextPlot> out;
    const PieOptions& o = p.opts;
    const std::optional<PieFormat> pct = parse_autopct(o.autopct);
    const bool labels = o.labeldistance.has_value() && !p.labels.empty();
    if (!labels && !(pct && pct->conv)) return out;
    TextOptions t;
    t.fontsize = o.fontsize.value_or(fontsize);
    t.color = o.textcolor;
    t.va = VAlign::Center;
    for (const PieSlice& s : pie_slices(p)) {
        const double c = std::cos(s.mid), sn = std::sin(s.mid);
        if (labels && !p.labels[s.k].empty()) {
            TextPlot tp;
            const double d = *o.labeldistance * o.radius;
            tp.content.text = p.labels[s.k];
            tp.content.x = Pos(s.cx + d * c);
            tp.content.y = Pos(s.cy + d * sn);
            tp.opts.text = t;
            tp.opts.text.ha = c >= 0.0 ? HAlign::Left : HAlign::Right;
            out.push_back(std::move(tp));
        }
        if (pct && pct->conv) {
            TextPlot tp;
            const double d = o.pctdistance * o.radius;
            tp.content.text = format_pct(*pct, 100.0 * s.frac);
            tp.content.x = Pos(s.cx + d * c);
            tp.content.y = Pos(s.cy + d * sn);
            tp.opts.text = t;
            tp.opts.text.ha = HAlign::Center;
            out.push_back(std::move(tp));
        }
    }
    return out;
}

// Every shown pie's texts in `pies`, in call order (render pass 3 and SVG).
inline std::vector<TextPlot> pie_texts(const std::vector<PiePlot>& pies, float fontsize) {
    std::vector<TextPlot> out;
    for (const PiePlot& p : pies) {
        if (!object_shown(p)) continue;
        std::vector<TextPlot> t = pie_texts(p, fontsize);
        out.insert(out.end(), std::make_move_iterator(t.begin()), std::make_move_iterator(t.end()));
    }
    return out;
}

} // namespace sextant
