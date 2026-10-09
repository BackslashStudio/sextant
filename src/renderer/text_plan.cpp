#include "text_plan.h"
#include "../rich_text.h"
#include <algorithm>
#include <cmath>

namespace sextant {

namespace {

constexpr float kDegToRad = 0.017453292519943295f;

struct Px { float x = 0.0f, y = 0.0f; };

// Whether data value `v` is inside a view running lo..hi (either way round).
bool in_view(double v, double lo, double hi) {
    const double a = std::min(lo, hi), b = std::max(lo, hi);
    const double eps = (b - a) * 1e-9;
    return v >= a - eps && v <= b + eps;
}

Color with_alpha(Color c, float alpha) {
    c.a *= std::clamp(alpha, 0.0f, 1.0f);
    return c;
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
}

// Lays out the lines and the box of `p` against anchor (ax, ay); returns the
// rectangle the arrow leaves from, relative to the anchor (the box when there
// is one, else the lines' extent).
PlotRect lay_out_block(const TextPlot& p, float ax, float ay, TextDraw& d) {
    const TextOptions& o = p.opts.text;
    d.ax = ax + o.dx;
    d.ay = ay - o.dy;
    d.angle = -o.rotation * kDegToRad;
    d.ha = o.ha;
    d.fontsize = o.fontsize;
    d.font_path = o.font_path;
    d.color = with_alpha(o.color, o.alpha);
    d.clip = o.clip_to_frame;

    if (p.content.text.empty() || !(o.fontsize > 0.0f)) return {0.0f, 0.0f, 0.0f, 0.0f};

    // Each line is parsed on its own, so a math span never crosses a newline. The
    // block reaches from the first line's top to the last line's bottom (a
    // line's own extent when it has math).
    const std::vector<std::string> lines = split_lines(p.content.text);
    const float ascent = label_vmetrics(o.font_path, o.fontsize, lines.front()).ascent;
    const float descent = label_vmetrics(o.font_path, o.fontsize, lines.back()).descent;
    const float adv = std::max(0.0f, o.linespacing) * o.fontsize;
    const float n1 = static_cast<float>(lines.size() - 1);
    float w = 0.0f;
    for (const auto& l : lines) w = std::max(w, label_width(o.font_path, o.fontsize, l));
    const float h = ascent - descent + n1 * adv;

    float left = 0.0f;
    if (o.ha == HAlign::Center) left = -w * 0.5f;
    else if (o.ha == HAlign::Right) left = -w;
    float top = 0.0f;
    switch (o.va) {
        case VAlign::Top:      top = 0.0f; break;
        case VAlign::Center:   top = -h * 0.5f; break;
        case VAlign::Bottom:   top = -h; break;
        case VAlign::Baseline: top = -(ascent + n1 * adv); break;
    }
    const float lx = o.ha == HAlign::Left ? left : (o.ha == HAlign::Center ? left + w * 0.5f : left + w);
    for (std::size_t i = 0; i < lines.size(); ++i)
        d.lines.push_back({lines[i], lx, top + ascent + static_cast<float>(i) * adv});

    const float pad = std::max(0.0f, o.pad);
    const PlotRect block{left, top, w, h};
    const PlotRect padded{left - pad, top - pad, w + 2.0f * pad, h + 2.0f * pad};
    d.fill = with_alpha(o.background, o.alpha);
    d.edge = with_alpha(o.edgecolor.value_or(o.color), o.alpha);
    d.edge_width = o.edge_linewidth > 0.0f ? o.edge_linewidth : 0.0f;
    d.has_box = d.fill.a > 0.0f || (d.edge_width > 0.0f && d.edge.a > 0.0f);
    d.box = padded;
    return d.has_box ? padded : block;
}

// ---- The arrow --------------------------------------------------------------

float seg_len(const std::vector<Px>& pts, std::size_t i) {
    return std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
}

// Cuts `len` pixels of path off its end (or its start); false if nothing is left.
bool trim_end(std::vector<Px>& pts, float len) {
    while (len > 0.0f && pts.size() >= 2) {
        const std::size_t i = pts.size() - 2;
        const float l = seg_len(pts, i);
        if (l > len) {
            const float t = (l - len) / l;
            pts.back() = {pts[i].x + (pts.back().x - pts[i].x) * t,
                          pts[i].y + (pts.back().y - pts[i].y) * t};
            return true;
        }
        len -= l;
        pts.pop_back();
    }
    return pts.size() >= 2;
}

bool trim_start(std::vector<Px>& pts, float len) {
    std::reverse(pts.begin(), pts.end());
    const bool ok = trim_end(pts, len);
    std::reverse(pts.begin(), pts.end());
    return ok;
}

float path_len(const std::vector<Px>& pts) {
    float l = 0.0f;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) l += seg_len(pts, i);
    return l;
}

// The head at `tip`, pointing along (ux, uy).
TextDraw::Head make_head(ArrowHead kind, Px tip, float ux, float uy, float length, float width) {
    TextDraw::Head h;
    h.kind = kind;
    const float bx = tip.x - ux * length, by = tip.y - uy * length;
    const float nx = -uy * width * 0.5f, ny = ux * width * 0.5f;
    switch (kind) {
        case ArrowHead::Filled:
        case ArrowHead::Open: {
            const float pts[6] = {bx + nx, by + ny, tip.x, tip.y, bx - nx, by - ny};
            std::copy(pts, pts + 6, h.xy);
            break;
        }
        case ArrowHead::Bar: {
            const float pts[4] = {tip.x + nx, tip.y + ny, tip.x - nx, tip.y - ny};
            std::copy(pts, pts + 4, h.xy);
            break;
        }
        case ArrowHead::None: break;
    }
    return h;
}

// The arrow of `d` from its text to `target` (figure pixels). `from` is the
// rectangle (relative to the anchor, before rotation) it leaves.
void plan_arrow(const TextPlot& p, const PlotRect& from, Px target, TextDraw& d) {
    const ArrowOptions& a = p.opts.arrow;
    const float ca = std::cos(d.angle), sa = std::sin(d.angle);
    auto to_figure = [&](float lx, float ly) {
        return Px{d.ax + lx * ca - ly * sa, d.ay + lx * sa + ly * ca};
    };
    const float g = std::max(0.0f, a.gap_text);
    auto inside = [&](Px q) {
        const float rx = q.x - d.ax, ry = q.y - d.ay;
        const float lx = rx * ca + ry * sa, ly = -rx * sa + ry * ca;
        return lx >= from.x - g && lx <= from.x + from.w + g && ly >= from.y - g && ly <= from.y + from.h + g;
    };

    const Px c = to_figure(from.x + from.w * 0.5f, from.y + from.h * 0.5f);
    std::vector<Px> pts;
    if (a.arc == 0.0f) {
        pts = {c, target};
    } else {
        // Quadratic through a control point `arc` x the length off the midpoint.
        const float dx = target.x - c.x, dy = target.y - c.y;
        const Px q{(c.x + target.x) * 0.5f - a.arc * dy, (c.y + target.y) * 0.5f + a.arc * dx};
        constexpr int kSteps = 32;
        for (int i = 0; i <= kSteps; ++i) {
            const float t = static_cast<float>(i) / kSteps, u = 1.0f - t;
            pts.push_back({u * u * c.x + 2.0f * u * t * q.x + t * t * target.x,
                           u * u * c.y + 2.0f * u * t * q.y + t * t * target.y});
        }
    }

    // Leave the text: the path starts where it first crosses out of the box.
    std::size_t k = 0;
    while (k < pts.size() && inside(pts[k])) ++k;
    if (k == pts.size()) return;   // the point is under the text
    if (k > 0) {
        Px lo = pts[k - 1], hi = pts[k];
        for (int it = 0; it < 24; ++it) {
            const Px mid{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f};
            (inside(mid) ? lo : hi) = mid;
        }
        pts.erase(pts.begin(), pts.begin() + static_cast<std::ptrdiff_t>(k - 1));
        pts.front() = hi;
    }
    if (!trim_end(pts, std::max(0.0f, a.gap_point))) return;
    if (path_len(pts) <= 0.0f) return;

    const float len = std::max(0.0f, a.head_length), wid = std::max(0.0f, a.head_width);
    auto unit = [](Px from_pt, Px to_pt, float& ux, float& uy) {
        const float l = std::hypot(to_pt.x - from_pt.x, to_pt.y - from_pt.y);
        ux = l > 0.0f ? (to_pt.x - from_pt.x) / l : 1.0f;
        uy = l > 0.0f ? (to_pt.y - from_pt.y) / l : 0.0f;
    };
    // A head is never longer than the shaft leaves room for.
    const int ends = (a.head != ArrowHead::None ? 1 : 0) + (a.tail != ArrowHead::None ? 1 : 0);
    const float hl = std::min(len, path_len(pts) / static_cast<float>(std::max(ends, 1)));
    if (a.head != ArrowHead::None) {
        float ux, uy;
        unit(pts[pts.size() - 2], pts.back(), ux, uy);
        d.heads.push_back(make_head(a.head, pts.back(), ux, uy, hl, wid));
    }
    if (a.tail != ArrowHead::None) {
        float ux, uy;
        unit(pts[1], pts[0], ux, uy);
        d.heads.push_back(make_head(a.tail, pts.front(), ux, uy, hl, wid));
    }
    // The shaft stops at a filled head's base, so it never pokes through its tip.
    if (a.head == ArrowHead::Filled && !trim_end(pts, hl)) pts.clear();
    if (a.tail == ArrowHead::Filled && pts.size() >= 2 && !trim_start(pts, hl)) pts.clear();

    for (const Px& q : pts) {
        d.shaft.push_back(q.x);
        d.shaft.push_back(q.y);
    }
    d.linestyle = a.linestyle;
    d.linewidth = std::max(0.0f, a.linewidth);
    d.arrow_color = with_alpha(a.color.value_or(p.opts.text.color), p.opts.text.alpha);
}

} // namespace

std::vector<TextDraw> plan_texts(const std::vector<TextPlot>& texts, const CoordTransform& tr,
                                 const PlotRect& frame) {
    std::vector<TextDraw> out;
    for (const TextPlot& p : texts) {
        const TextContent& c = p.content;
        // Hidden while a data coordinate (the text's, or the arrow's point) is
        // out of view.
        if (c.x.space == Coords::Data && !in_view(c.x.v, tr.xmin, tr.xmax)) continue;
        if (c.y.space == Coords::Data && !in_view(c.y.v, tr.ymin, tr.ymax)) continue;
        if (c.arrow && !(in_view(c.px, tr.xmin, tr.xmax) && in_view(c.py, tr.ymin, tr.ymax))) continue;

        const float ax = c.x.space == Coords::Data
                             ? tr.to_px(c.x.v)
                             : frame.x + static_cast<float>(c.x.v) * frame.w;
        const float ay = c.y.space == Coords::Data
                             ? tr.to_py(c.y.v)
                             : frame.y + frame.h - static_cast<float>(c.y.v) * frame.h;
        TextDraw d;
        const PlotRect from = lay_out_block(p, ax, ay, d);
        if (c.arrow) plan_arrow(p, from, {tr.to_px(c.px), tr.to_py(c.py)}, d);
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<TextDraw> plan_texts3d(const std::vector<TextPlot>& texts, const Projector3D& proj,
                                   const PlotRect& frame) {
    std::vector<TextDraw> out;
    const Transform3D& tf = proj.transform();
    for (const TextPlot& p : texts) {
        const TextContent& c = p.content;
        TextDraw d;
        if (c.x.space == Coords::Fraction && !c.arrow) {
            // text2d(): the frame, whatever the camera does.
            lay_out_block(p, frame.x + static_cast<float>(c.x.v) * frame.w,
                          frame.y + frame.h - static_cast<float>(c.y.v) * frame.h, d);
            out.push_back(std::move(d));
            continue;
        }
        // A point in the scene: hidden outside the limits or behind the eye.
        if (!in_view(c.x.v, tf.xmin, tf.xmax) || !in_view(c.y.v, tf.ymin, tf.ymax)
            || !in_view(c.z, tf.zmin, tf.zmax))
            continue;
        const Vec3 b = tf.to_box(c.x.v, c.y.v, c.z);
        if (!proj.in_front(b)) continue;
        const Px3 at = proj.project_box(b);
        if (!c.arrow) {
            lay_out_block(p, at.x, at.y, d);
        } else {
            const PlotRect from = lay_out_block(p, at.x + static_cast<float>(c.dx),
                                                at.y - static_cast<float>(c.dy), d);
            plan_arrow(p, from, {at.x, at.y}, d);
        }
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace sextant
