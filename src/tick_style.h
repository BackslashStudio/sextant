#pragma once
// One axis' tick and label look: AxesStyle's per-axis fields
// over its shared ones. Layout, both outputs and the 3D box read ticks only
// through tick_look(), so an unset field draws exactly as before.
#include "sextant/style.h"

namespace sextant {

enum class TickAxis { X, Y, Z };

struct TickLook {
    Color color;
    float length = 0.0f;
    float linewidth = 0.0f;
    float minor_length = 0.0f;
    Color label_color;
    float label_fontsize = 0.0f;
};

inline TickLook tick_look(const AxesStyle& s, TickAxis a) {
    const auto pick = [a](const auto& x, const auto& y, const auto& z) -> const auto& {
        return a == TickAxis::X ? x : a == TickAxis::Y ? y : z;
    };
    const std::optional<float> no_minor;
    TickLook t;
    t.color = pick(s.xtick_color, s.ytick_color, s.ztick_color).value_or(s.tick_color);
    t.length = pick(s.xtick_length, s.ytick_length, s.ztick_length).value_or(s.tick_length);
    t.linewidth = pick(s.xtick_linewidth, s.ytick_linewidth, s.ztick_linewidth).value_or(s.tick_linewidth);
    t.minor_length = pick(s.minor_xtick_length, s.minor_ytick_length, no_minor).value_or(s.minor_tick_length);
    t.label_color = pick(s.xlabel_color, s.ylabel_color, s.zlabel_color).value_or(s.label_color);
    t.label_fontsize = pick(s.xlabel_fontsize, s.ylabel_fontsize, s.zlabel_fontsize).value_or(s.label_fontsize);
    return t;
}

inline bool same_color(const Color& a, const Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// A twinned cell's frame edges in an axis color (AxesStyle::xaxis_color /
// yaxis_color); unset edges keep the host's spine_color.
struct EdgeColors {
    std::optional<Color> bottom, top, left, right;
};

// The host's style and its twins' (null for none).
inline EdgeColors twin_edge_colors(const AxesStyle& host, const AxesStyle* twinx, const AxesStyle* twiny) {
    EdgeColors e;
    if (twinx) {
        e.left = host.yaxis_color;
        e.right = twinx->yaxis_color;
    }
    if (twiny) {
        e.bottom = host.xaxis_color;
        e.top = twiny->xaxis_color;
    }
    return e;
}

// Whether two axes' marks stroke alike (one group in an output).
inline bool same_marks(const TickLook& a, const TickLook& b) {
    return same_color(a.color, b.color) && a.linewidth == b.linewidth;
}

// Whether two axes' labels fill alike.
inline bool same_labels(const TickLook& a, const TickLook& b) {
    return same_color(a.label_color, b.label_color) && a.label_fontsize == b.label_fontsize;
}

} // namespace sextant
