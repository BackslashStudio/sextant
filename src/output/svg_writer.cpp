#include "svg_writer.h"
#include "../colormaps.h"
#include "../font_discovery.h"
#include "../line_dash.h"
#include "../line_steps.h"
#include "../reference_lines.h"
#include "../rich_text.h"
#include "../tick_style.h"
#include "../contour.h"
#include "../fill_geometry.h"
#include "../heatmap_pixels.h"
#include "../heatmap_tri_geometry.h"
#include "../contour_plot.h"
#include "../quiver_geometry.h"
#include "../shape_geometry.h"
#include "../pie_geometry.h"
#include "../renderer/marker_shape.h"
#include "../renderer/error_bar_shape.h"
#include "png_writer.h"
#include "file_write.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <string>
#include <cmath>
#include <cerrno>
#include <limits>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <span>
#include <filesystem>

namespace sextant {

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
static std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

// CSS font-family for a font_path: the real family name from
// discover_system_fonts() (same list as the Font combo), else the filename
// stem. "" resolves to pick_default_font(), matching the on-screen renderer.
static std::string svg_font_family_for(const std::string& font_path) {
    static const char* kDefaultFamily = "sans-serif,Arial,Helvetica";
    if (!font_path.empty()) {
        // Compare as paths: separators may differ from a hand-written path.
        const std::filesystem::path wanted(font_path);
        for (const auto& f : discover_system_fonts()) {
            if (std::filesystem::path(f.path) == wanted)
                return "'" + xml_escape(f.name) + "'," + kDefaultFamily;
        }
        std::string stem = wanted.stem().string();
        return "'" + xml_escape(stem) + "'," + kDefaultFamily;
    }
    if (const FontEntry* def = pick_default_font())
        return "'" + xml_escape(def->name) + "'," + kDefaultFamily;
    return kDefaultFamily;
}

static std::string svg_font_family(const AxesStyle& style) {
    return svg_font_family_for(style.font_path);
}

// A user's string as <text> content: escaped, or for a string with math
// (rich_text.h) one <tspan> per run. Each run moves by dx/dy from where the
// previous one ended rather than to an absolute x, so text-anchor still places
// the whole line; a run in a script carries its own font-size, a math letter
// font-style="italic" (a viewer slants the family when it has no italic face),
// a \mathbf run font-weight="bold".
// `size` is the size the enclosing element sets.
static std::string text_body(const std::string& text, const std::string& font_path, float size) {
    if (!is_rich(text)) return xml_escape(text);
    const RichLine line = layout_rich(font_path, size, text);
    std::ostringstream o;
    float pen = 0.0f, dy = 0.0f;
    for (const RichRun& r : line.runs) {
        o << "<tspan";
        if (std::fabs(r.x - pen) > 1e-3f) o << " dx=\"" << r.x - pen << "\"";
        if (r.dy != dy) o << " dy=\"" << r.dy - dy << "\"";
        if (r.size != size) o << " font-size=\"" << r.size << "\"";
        if (r.italic) o << " font-style=\"italic\"";
        if (r.bold) o << " font-weight=\"bold\"";
        if (!r.font.empty()) o << " font-family=\"" << svg_font_family_for(r.font) << "\"";
        o << ">" << xml_escape(r.text) << "</tspan>";
        pen = r.x + r.width;
        dy = r.dy;
    }
    return o.str();
}

// One label: `<text x y[ text-anchor] attrs>body</text>` (text_body()), or, for a
// line with fractions or roots (RichLine::placed), a <g> carrying `attrs`
// around a <text> whose runs sit at absolute positions from its left end, and
// the line's bars and radicals as <rect>/<path>: a denominator steps back under
// its numerator, and text-anchor over such a line is each viewer's guess.
// `anchor` null = no attribute (start). `attrs` begins with a space.
static void emit_text(std::ostringstream& o, const char* indent, float x, float y, const char* anchor,
                      const std::string& attrs, const std::string& text, const std::string& font_path,
                      float size) {
    RichLine line;
    if (is_rich(text)) line = layout_rich(font_path, size, text);
    if (!line.placed) {
        o << indent << "<text x=\"" << x << "\" y=\"" << y << "\"";
        if (anchor) o << " text-anchor=\"" << anchor << "\"";
        o << attrs << ">" << text_body(text, font_path, size) << "</text>\n";
        return;
    }
    float x0 = x;
    if (anchor && std::string_view(anchor) == "middle") x0 -= line.width * 0.5f;
    else if (anchor && std::string_view(anchor) == "end") x0 -= line.width;
    o << indent << "<g" << attrs << "><text x=\"" << x0 << "\" y=\"" << y << "\" text-anchor=\"start\">";
    for (const RichRun& r : line.runs) {
        o << "<tspan x=\"" << x0 + r.x << "\" y=\"" << y + r.dy << "\"";
        if (r.size != size) o << " font-size=\"" << r.size << "\"";
        if (r.italic) o << " font-style=\"italic\"";
        if (r.bold) o << " font-weight=\"bold\"";
        if (!r.font.empty()) o << " font-family=\"" << svg_font_family_for(r.font) << "\"";
        o << ">" << xml_escape(r.text) << "</tspan>";
    }
    o << "</text>";
    for (const RichRule& r : line.rules)
        o << "<rect x=\"" << x0 + r.x << "\" y=\"" << y + r.y << "\" width=\"" << r.w << "\" height=\"" << r.h
          << "\"/>";
    for (const RichPath& p : line.paths) {
        o << "<path d=\"";
        for (std::size_t k = 0; k < p.pts.size(); ++k)
            o << (k == 0 ? "M" : " L") << x0 + p.pts[k].x << " " << y + p.pts[k].y;
        o << " Z\"/>";
    }
    o << "</g>\n";
}

// ` fill="..."` for a background rect, plus its opacity when not opaque. Opaque
// white stays `white` and other opaque colors `#rrggbb`, as the files always had.
static std::string background_fill(const Color& c) {
    auto q = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
    char buf[32];
    if (q(c.r) == 255 && q(c.g) == 255 && q(c.b) == 255) std::snprintf(buf, sizeof(buf), "white");
    else std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", q(c.r), q(c.g), q(c.b));
    std::string s = std::string(" fill=\"") + buf + "\"";
    if (c.a < 1.0f) {
        std::snprintf(buf, sizeof(buf), "%g", std::max(c.a, 0.0f));
        s += std::string(" fill-opacity=\"") + buf + "\"";
    }
    return s;
}

static std::string rgb(const Color& c) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)",
                  static_cast<int>(std::clamp(c.r, 0.f, 1.f) * 255.f),
                  static_cast<int>(std::clamp(c.g, 0.f, 1.f) * 255.f),
                  static_cast<int>(std::clamp(c.b, 0.f, 1.f) * 255.f));
    return buf;
}

// ` stroke-dasharray="..."` for a dashed style (from line_dash.h), else nothing.
static std::string dash_attr(LineStyle ls) {
    const std::string da = svg_dasharray(ls);
    return da.empty() ? std::string{} : " stroke-dasharray=\"" + da + "\"";
}

// Standard base64 (RFC 4648), for heatmap PNG data: URIs.
static std::string base64_encode(std::span<const uint8_t> data) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    std::size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const uint32_t n = (static_cast<uint32_t>(data[i])     << 16)
                          | (static_cast<uint32_t>(data[i + 1]) << 8)
                          |  static_cast<uint32_t>(data[i + 2]);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6)  & 0x3F];
        out += table[n & 0x3F];
    }
    const std::size_t rem = data.size() - i;
    if (rem == 1) {
        const uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16)
                          | (static_cast<uint32_t>(data[i + 1]) << 8);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6)  & 0x3F];
        out += "=";
    }
    return out;
}

// -------------------------------------------------------------------------
// Per-element emitters
// -------------------------------------------------------------------------
static void emit_grid(std::ostringstream& o, const SvgAxesData& d) {
    if (!d.grid_enabled) return;
    const auto& g = d.grid_opts;
    // LineStyle::None draws no grid line, as for data lines.
    if (g.linestyle == LineStyle::None) return;
    // A twin draws its own axis' lines only (CellLayout::xgrid).
    const bool gx = d.layout.xgrid, gy = d.layout.ygrid;
    // Minor lines first, under the major ones (as NanoVG); only those in the frame.
    if (g.minor && ((gx && !d.layout.xminor.empty()) || (gy && !d.layout.yminor.empty()))) {
        const PlotRect& f = d.layout.frame;
        o << "  <g stroke=\"" << rgb(g.minor_color) << "\" stroke-opacity=\"" << g.minor_color.a
          << "\" stroke-width=\"" << g.linewidth << "\"" << dash_attr(g.linestyle) << ">\n";
        for (const double v : d.layout.xminor) {
            if (!gx) break;
            const float px = d.layout.tr.to_px(v);
            if (px < f.x || px > f.x + f.w) continue;
            o << "    <line x1=\"" << px << "\" y1=\"" << f.y
              <<         "\" x2=\"" << px << "\" y2=\"" << f.y + f.h << "\"/>\n";
        }
        for (const double v : d.layout.yminor) {
            if (!gy) break;
            const float py = d.layout.tr.to_py(v);
            if (py < f.y || py > f.y + f.h) continue;
            o << "    <line x1=\"" << f.x << "\" y1=\"" << py
              <<         "\" x2=\"" << f.x + f.w << "\" y2=\"" << py << "\"/>\n";
        }
        o << "  </g>\n";
    }
    o << "  <g stroke=\"" << rgb(g.color) << "\" stroke-opacity=\"" << g.color.a
      << "\" stroke-width=\"" << g.linewidth << "\"";
    o << dash_attr(g.linestyle) << ">\n";
    for (const auto& t : d.layout.xticks) {
        if (!gx) break;
        float px = d.layout.tr.to_px(t.value);
        o << "    <line x1=\"" << px << "\" y1=\"" << d.layout.frame.y
          <<         "\" x2=\"" << px << "\" y2=\"" << d.layout.frame.y + d.layout.frame.h << "\"/>\n";
    }
    for (const auto& t : d.layout.yticks) {
        if (!gy) break;
        float py = d.layout.tr.to_py(t.value);
        o << "    <line x1=\"" << d.layout.frame.x      << "\" y1=\"" << py
          <<         "\" x2=\"" << d.layout.frame.x + d.layout.frame.w << "\" y2=\"" << py << "\"/>\n";
    }
    o << "  </g>\n";
}

// A stroke path in pixels, as the stroke shader draws it.
struct PixelPath {
    std::vector<float> x, y;
    bool loop = false;
};

// The joints of `p` the stroke shader bevels and an SVG viewer would miter
// (stroke_short_bevel()), ascending.
static std::vector<std::size_t> short_bevels(const PixelPath& p, float width) {
    std::vector<std::size_t> out;
    const std::size_t n = p.x.size();
    if (n < 3) return out;
    for (std::size_t j = p.loop ? 0 : 1; j < (p.loop ? n : n - 1); ++j) {
        const std::size_t a = j == 0 ? n - 1 : j - 1, b = j + 1 == n ? 0 : j + 1;
        if (stroke_short_bevel(p.x[a], p.y[a], p.x[j], p.y[j], p.x[b], p.y[b], 0.5f * width))
            out.push_back(j);
    }
    return out;
}

// `p` stroked in `color` at `opacity` and `width`, with the joints in `bevels`
// beveled: polylines from cut to cut (a loop is also cut at its first point),
// each cut's corner a polyline of its own with 0.05 px arms carrying the join,
// beveled or SVG's default miter. The dash phase follows the shader's arc
// length from the first point, which a loop's closing segment continues. The
// pieces overlap, so a translucent stroke is an opaque one in a <g> carrying
// the opacity: composited once, as one path is (and the shader's PaintOnce).
static void emit_beveled_stroke(std::ostringstream& o, const PixelPath& p,
                                const std::vector<std::size_t>& bevels,
                                const std::string& color, float opacity, float width,
                                LineStyle ls) {
    std::ostringstream st;
    st << " stroke=\"" << color << "\" stroke-width=\"" << width << "\"";
    const std::string stroke = st.str();
    if (opacity < 1.0f) o << "  <g opacity=\"" << opacity << "\">\n";
    const std::size_t n = p.x.size();
    auto len = [&](std::size_t a, std::size_t b) {
        return std::hypot(static_cast<double>(p.x[b]) - p.x[a], static_cast<double>(p.y[b]) - p.y[a]);
    };
    std::vector<double> arc(n, 0.0);
    for (std::size_t i = 1; i < n; ++i) arc[i] = arc[i - 1] + len(i - 1, i);
    const std::string dash = dash_attr(ls);

    auto open = [&](double offset) {
        o << "  <polyline fill=\"none\"" << stroke << dash;
        if (!dash.empty()) o << " stroke-dashoffset=\"" << offset << "\"";
    };
    auto piece = [&](std::size_t from, std::size_t points) {
        open(arc[from]);
        o << " points=\"";
        for (std::size_t k = 0; k < points; ++k) {
            const std::size_t i = (from + k) % n;
            o << p.x[i] << "," << p.y[i] << " ";
        }
        o << "\"/>\n";
    };

    std::vector<std::size_t> cuts = bevels;
    if (p.loop && (cuts.empty() || cuts.front() != 0)) cuts.insert(cuts.begin(), 0);
    if (p.loop) {
        for (std::size_t c = 0; c < cuts.size(); ++c) {
            const std::size_t from = cuts[c], to = cuts[(c + 1) % cuts.size()];
            piece(from, (to + n - from - 1) % n + 2);
        }
    } else {
        std::size_t from = 0;
        for (const std::size_t j : cuts) {
            piece(from, j - from + 1);
            from = j;
        }
        piece(from, n - from);
    }

    constexpr float arm = 0.05f;
    for (const std::size_t j : cuts) {
        const std::size_t a = j == 0 ? n - 1 : j - 1, b = j + 1 == n ? 0 : j + 1;
        const double la = len(a, j), lb = len(j, b);
        if (!(la > 1e-6) || !(lb > 1e-6)) continue;
        const double at = j == 0 ? arc[n - 1] + la : arc[j];
        open(at - arm);
        if (std::binary_search(bevels.begin(), bevels.end(), j)) o << " stroke-linejoin=\"bevel\"";
        o << " points=\""
          << p.x[j] - arm * (p.x[j] - p.x[a]) / la << "," << p.y[j] - arm * (p.y[j] - p.y[a]) / la << " "
          << p.x[j] << "," << p.y[j] << " "
          << p.x[j] + arm * (p.x[b] - p.x[j]) / lb << "," << p.y[j] + arm * (p.y[b] - p.y[j]) / lb
          << "\"/>\n";
    }
    if (opacity < 1.0f) o << "  </g>\n";
}

static void emit_lines(std::ostringstream& o, const SvgAxesData& d,
                       std::span<const std::size_t> which) {
    for (const std::size_t k : which) {
        const auto& lp = d.lines[k];
        if (lp.x.empty() || lp.opts.linestyle == LineStyle::None) continue;

        PixelPath path;
        path.loop = lp.opts.loop && lp.x.size() >= 2;
        StrokePath sp;
        sp.build(lp.x.data(), lp.y.data(), lp.x.size(), lp.opts.drawstyle, lp.opts.loop);
        for (std::size_t i = 0; i < sp.n; ++i) {
            path.x.push_back(d.layout.tr.to_px(sp.x[i]));
            path.y.push_back(d.layout.tr.to_py(sp.y[i]));
        }
        const float a = lp.opts.color->a * lp.opts.alpha;
        std::ostringstream stroke;
        stroke << " stroke=\"" << rgb(*lp.opts.color) << "\" stroke-opacity=\"" << a
               << "\" stroke-width=\"" << lp.opts.linewidth << "\"";

        // Where the shader bevels a joint SVG would miter, the path is cut there.
        const std::vector<std::size_t> bevels = short_bevels(path, lp.opts.linewidth);
        if (!bevels.empty()) {
            emit_beveled_stroke(o, path, bevels, rgb(*lp.opts.color), a, lp.opts.linewidth,
                                lp.opts.linestyle);
            continue;
        }
        // A closed path is a <polygon> (fill="none") so the seam gets a join;
        // SVG's default miterlimit 4 matches the stroke shader.
        o << "  <" << (path.loop ? "polygon" : "polyline") << " fill=\"none\"" << stroke.str()
          << dash_attr(lp.opts.linestyle) << " points=\"";
        for (std::size_t i = 0; i < path.x.size(); ++i) o << path.x[i] << "," << path.y[i] << " ";
        o << "\"/>\n";
    }
}

// Error bars for all four kinds: whisker and caps as <line>s from
// whisker_segments() (shared with the raster path), plus a box <rect>.
// capsize/boxwidth are total pixel lengths; linewidth is the stroke width.
static void emit_whisker(std::ostringstream& o, float cx, float cy,
                         float lo, float hi, bool vertical,
                         const ErrorBarOptions& style, const std::string& stroke, float opacity)
{
    whisker_segments(cx, cy, lo, hi, vertical, 1.0, 1.0, style.capsize, style.capstyle,
                     [&](double ax, double ay, double bx, double by) {
        o << "    <line x1=\"" << static_cast<float>(ax) << "\" y1=\"" << static_cast<float>(ay)
          << "\" x2=\"" << static_cast<float>(bx) << "\" y2=\"" << static_cast<float>(by)
          << "\" stroke=\"" << stroke << "\" stroke-opacity=\"" << opacity
          << "\" stroke-width=\"" << style.linewidth << "\"/>\n";
    });
}

static void emit_error_bars_for(std::ostringstream& o, const SvgAxesData& d,
                                const CowVec<double>& xs, const CowVec<double>& ys,
                                const ErrorBarData& err, const ErrorBarOptions& style,
                                const Color& fallback, float alpha)
{
    if (err.empty() || style.linewidth <= 0.0f) return;
    const auto& tr = d.layout.tr;
    // Faded by the series' `alpha`.
    Color c = style.color.value_or(fallback);
    c.a *= std::clamp(alpha, 0.0f, 1.0f);
    const std::string stroke = rgb(c);
    const float halfbox = std::max(style.boxwidth, 0.0f) * 0.5f;
    const std::size_t n = std::min(xs.size(), ys.size());

    for (std::size_t i = 0; i < n; ++i) {
        const float cx = tr.to_px(xs[i]);
        const float cy = tr.to_py(ys[i]);
        if (err.has_y_cap()) {
            const ErrOffsets e = err.y_cap(i);
            emit_whisker(o, cx, cy, tr.to_py(ys[i] + -e.lo), tr.to_py(ys[i] + e.hi),
                         true, style, stroke, c.a);
        }
        if (err.has_x_cap()) {
            const ErrOffsets e = err.x_cap(i);
            emit_whisker(o, cx, cy, tr.to_px(xs[i] + -e.lo), tr.to_px(xs[i] + e.hi),
                         false, style, stroke, c.a);
        }

        if (!err.has_y_box() && !err.has_x_box()) continue;
        float bx0, bx1, by0, by1;
        if (err.has_x_box()) {
            const ErrOffsets e = err.x_box(i);
            bx0 = tr.to_px(xs[i] + -e.lo);
            bx1 = tr.to_px(xs[i] +  e.hi);
        } else { bx0 = cx - halfbox; bx1 = cx + halfbox; }
        if (err.has_y_box()) {
            const ErrOffsets e = err.y_box(i);
            by0 = tr.to_py(ys[i] + -e.lo);
            by1 = tr.to_py(ys[i] +  e.hi);
        } else { by0 = cy - halfbox; by1 = cy + halfbox; }
        if (bx0 == bx1 || by0 == by1) continue;
        if (bx0 > bx1) std::swap(bx0, bx1);
        if (by0 > by1) std::swap(by0, by1);
        o << "    <rect x=\"" << bx0 << "\" y=\"" << by0
          << "\" width=\"" << (bx1 - bx0) << "\" height=\"" << (by1 - by0)
          << "\" fill=\"" << stroke << "\" fill-opacity=\"" << (c.a * style.box_alpha)
          << "\" stroke=\"" << stroke << "\" stroke-opacity=\"" << c.a
          << "\" stroke-width=\"" << style.linewidth << "\"/>\n";
    }
}

// One object's error bars.
static void emit_error_bars(std::ostringstream& o, const SvgAxesData& d, ObjectRef r) {
    switch (r.kind) {
        case PlotKind::Line: {
            const auto& lp = d.lines[r.index];
            emit_error_bars_for(o, d, lp.x, lp.y, lp.err, lp.opts.errorbar, *lp.opts.color, lp.opts.alpha);
            break;
        }
        case PlotKind::Bar: {
            // Error bars hang off the tip, not the base.
            const auto& bp = d.bars[r.index];
            if (bp.err.empty()) break;
            const XYColumns a = bar_anchors(bp);
            emit_error_bars_for(o, d, a.x, a.y, bp.err, bp.opts.errorbar, bp.opts.edgecolor, bp.opts.alpha);
            break;
        }
        case PlotKind::Scatter: {
            const auto& sp = d.scatters[r.index];
            emit_error_bars_for(o, d, sp.x, sp.y, sp.err, sp.opts.errorbar, *sp.opts.color, sp.opts.alpha);
            break;
        }
        case PlotKind::ScatterZ: {
            // scatter_z has no single color, so black (as in the raster path).
            const auto& sp = d.scatter_z[r.index];
            emit_error_bars_for(o, d, sp.x, sp.y, sp.err, sp.opts.errorbar, Color::Black, sp.opts.alpha);
            break;
        }
        default: break;
    }
}

// Markers from marker_shape() (shared with both legends), emitted as their
// natural element (<circle>, <rect>, ...). Empty `stroke` = no outline. A data
// marker's outline (`inside`) fills the band of `stroke_w` just within its
// boundary, as on the GPU, so the shape is drawn inset by half the width; a
// legend key's is a 1 px line centred on the boundary. A line marker is its
// stroke alone: the fill's color, `stroke_w` thick for a data marker (see
// line_marker_width()), no outline.
static void emit_scatter_marker(std::ostringstream& o,
                                float cx, float cy, float r,
                                const std::string& fill, float alpha,
                                MarkerStyle marker,
                                const std::string& stroke = {},
                                float stroke_alpha = 1.0f, float stroke_w = 1.0f,
                                bool inside = false)
{
    const bool line = is_line_marker(marker);
    const MarkerShape s = line
        ? marker_shape(marker, cx, cy, r, 0.0f, inside ? line_marker_width(stroke_w) : kLineMarkerWidth)
        : marker_shape(marker, cx, cy, r, !stroke.empty() && inside ? stroke_w * 0.5f : 0.0f);
    std::ostringstream eo;
    if (!stroke.empty() && !line)
        eo << " stroke=\"" << stroke << "\" stroke-opacity=\"" << stroke_alpha
           << "\" stroke-width=\"" << stroke_w << "\"";
    const std::string edge = eo.str();
    switch (s.form) {
        case MarkerShape::Form::Disc:
            o << "    <circle cx=\"" << s.cx << "\" cy=\"" << s.cy << "\" r=\"" << s.radius
              << "\" fill=\"" << fill << "\" fill-opacity=\"" << alpha << "\"" << edge << "/>\n";
            break;
        case MarkerShape::Form::Rect:
            o << "    <rect x=\"" << s.cx - s.radius << "\" y=\"" << s.cy - s.radius
              << "\" width=\"" << 2*s.radius << "\" height=\"" << 2*s.radius
              << "\" fill=\"" << fill << "\" fill-opacity=\"" << alpha << "\"" << edge << "/>\n";
            break;
        case MarkerShape::Form::Polygon:
            o << "    <polygon points=\"";
            for (int i = 0; i < s.count; ++i)
                o << (i ? " " : "") << s.pts[i][0] << "," << s.pts[i][1];
            o << "\" fill=\"" << fill << "\" fill-opacity=\"" << alpha << "\"" << edge << "/>\n";
            break;
        case MarkerShape::Form::None:
        default: break;
    }
}

static void emit_scatter(std::ostringstream& o, const SvgAxesData& d,
                         std::span<const std::size_t> which) {
    for (const std::size_t k : which) {
        const auto& sp = d.scatters[k];
        if (sp.x.empty()) continue;
        const std::string fill = rgb(*sp.opts.color);
        // Unset edgecolor = the fill's color; each part's alpha is its color's own
        // times `alpha`, as on the GPU.
        const float a = std::clamp(sp.opts.alpha, 0.0f, 1.0f);
        const bool edged = sp.opts.edge_linewidth > 0.0f;
        const Color ec = sp.opts.edgecolor ? *sp.opts.edgecolor : *sp.opts.color;
        const std::string stroke = edged ? rgb(ec) : std::string();
        const float stroke_alpha = ec.a * a;
        for (std::size_t i = 0; i < sp.x.size(); ++i) {
            if (marker_hidden(sp, i)) continue;
            emit_scatter_marker(o, d.layout.tr.to_px(sp.x[i]), d.layout.tr.to_py(sp.y[i]),
                                marker_size_at(sp, i) * 0.5f, fill, sp.opts.color->a * marker_face_alpha(sp.opts),
                                sp.opts.marker,
                                stroke, stroke_alpha, sp.opts.edge_linewidth, true);
        }
    }
}

// Continuous-color scatter: per-point fill via the same colormap LUT as the
// raster path.
static void emit_scatter_z(std::ostringstream& o, const SvgAxesData& d,
                           std::span<const std::size_t> which) {
    for (const std::size_t k : which) {
        const auto& sp = d.scatter_z[k];
        if (sp.x.empty()) continue;
        const uint8_t* lut   = colormaps::get(sp.opts.cmap);
        const float    vmin  = sp.opts.vmin, vrange = sp.opts.vmax - sp.opts.vmin;
        const ColorNorm::Kind kind = norm_in_force(sp.opts.norm, sp.opts.vmin, sp.opts.vmax);
        for (std::size_t i = 0; i < sp.x.size(); ++i) {
            if (marker_hidden(sp, i)) continue;
            const float r = marker_size_at(sp, i) * 0.5f;
            const double t = kind == ColorNorm::Kind::Linear
                                 ? ((vrange != 0.0f) ? (sp.z[i] - vmin) / vrange : 0.0)
                                 : norm_t(kind, sp.opts.norm, sp.opts.vmin, sp.opts.vmax, sp.z[i]);
            const uint8_t* c = &lut[colormaps::index(t, sp.opts.cmap.is_cyclic()) * 4];
            char buf[32];
            std::snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)", c[0], c[1], c[2]);
            std::string stroke;
            if (sp.opts.edge_linewidth > 0.0f)
                stroke = sp.opts.edgecolor ? rgb(*sp.opts.edgecolor) : std::string(buf);
            const float stroke_alpha = std::clamp(
                (sp.opts.edgecolor ? sp.opts.edgecolor->a : 1.0f) * sp.opts.alpha, 0.0f, 1.0f);
            emit_scatter_marker(o, d.layout.tr.to_px(sp.x[i]), d.layout.tr.to_py(sp.y[i]),
                                r, buf, marker_face_alpha(sp.opts), sp.opts.marker, stroke, stroke_alpha,
                                sp.opts.edge_linewidth, true);
        }
    }
}

// Up to this many cells, a mesh or a heatmap on a scaled axis is written as
// one <rect> per cell; above it, as one <image> rasterized at the plot area's
// pixel size.
constexpr std::size_t kSvgHeatmapCellCap = 4096;

// A mesh, or a heatmap on a Log/Symlog axis: uneven cells, which one affine
// <image> can't hold. Edges placed as DataRenderer::draw_heatmap_grid() places
// them (pulled in to just outside the frame).
static void emit_heatmap_cells(std::ostringstream& o, const SvgAxesData& d, const HeatmapPlot& hp) {
    const auto& tr = d.layout.tr;
    const HeatmapPixels px(hp);
    const bool lower = (hp.opts.origin == "lower");
    const std::size_t cells = static_cast<std::size_t>(hp.rows) * hp.cols;

    if (cells <= kSvgHeatmapCellCap) {
        const float xlo = tr.px - 1.0f, xhi = tr.px + tr.pw + 1.0f;
        const float ylo = tr.py - 1.0f, yhi = tr.py + tr.ph + 1.0f;
        std::vector<float> ex(static_cast<std::size_t>(hp.cols) + 1), ey(static_cast<std::size_t>(hp.rows) + 1);
        for (int c = 0; c <= hp.cols; ++c)
            ex[static_cast<std::size_t>(c)] = std::clamp(tr.to_px(hp.x_edge(c)), xlo, xhi);
        for (int r = 0; r <= hp.rows; ++r)
            ey[static_cast<std::size_t>(r)] = std::clamp(tr.to_py(hp.y_edge(r)), ylo, yhi);

        // crispEdges, so neighbouring cells leave no hairline seam.
        o << "    <g shape-rendering=\"crispEdges\">\n";
        for (int r = 0; r < hp.rows; ++r) {
            const std::size_t band = static_cast<std::size_t>(lower ? r : hp.rows - 1 - r);
            const float y0 = std::min(ey[band], ey[band + 1]), y1 = std::max(ey[band], ey[band + 1]);
            if (!(y1 > y0)) continue;
            for (int c = 0; c < hp.cols; ++c) {
                const std::size_t cc = static_cast<std::size_t>(c);
                const float x0 = std::min(ex[cc], ex[cc + 1]), x1 = std::max(ex[cc], ex[cc + 1]);
                if (!(x1 > x0)) continue;
                const Color fill = px.color(static_cast<std::size_t>(r) * hp.cols + cc);
                if (fill.a <= 0.0f) continue;
                o << "      <rect x=\"" << x0 << "\" y=\"" << y0 << "\" width=\"" << x1 - x0
                  << "\" height=\"" << y1 - y0 << "\" fill=\"" << rgb(fill) << "\"";
                if (fill.a < 1.0f) o << " fill-opacity=\"" << fill.a << "\"";
                o << "/>\n";
            }
        }
        o << "    </g>\n";
        return;
    }

    // The raster: the extent's part of the plot area, whole pixels, each pixel
    // the cell under its centre (as GL's rasterization picks it). Columns and
    // rows separate, so each pixel column and row is looked up once.
    const float ex0 = tr.to_px(hp.x_edge(0)), ex1 = tr.to_px(hp.x_edge(hp.cols));
    const float ey0 = tr.to_py(hp.y_edge(0)), ey1 = tr.to_py(hp.y_edge(hp.rows));
    const int X0 = static_cast<int>(std::floor(std::max(std::min(ex0, ex1), tr.px)));
    const int X1 = static_cast<int>(std::ceil(std::min(std::max(ex0, ex1), tr.px + tr.pw)));
    const int Y0 = static_cast<int>(std::floor(std::max(std::min(ey0, ey1), tr.py)));
    const int Y1 = static_cast<int>(std::ceil(std::min(std::max(ey0, ey1), tr.py + tr.ph)));
    if (X1 <= X0 || Y1 <= Y0) return;
    const int W = X1 - X0, H = Y1 - Y0;

    std::vector<int> col_of(static_cast<std::size_t>(W)), row_of(static_cast<std::size_t>(H));
    for (int i = 0; i < W; ++i) {
        const double f = hp.col_at(tr.to_data_x(static_cast<float>(X0 + i) + 0.5f));
        col_of[static_cast<std::size_t>(i)] = (f >= 0.0 && f < hp.cols) ? static_cast<int>(f) : -1;
    }
    for (int j = 0; j < H; ++j) {
        const double f = hp.row_at(tr.to_data_y(static_cast<float>(Y0 + j) + 0.5f));
        const int band = (f >= 0.0 && f < hp.rows) ? static_cast<int>(f) : -1;
        row_of[static_cast<std::size_t>(j)] = band < 0 ? -1 : (lower ? band : hp.rows - 1 - band);
    }
    std::vector<uint8_t> rgba(static_cast<std::size_t>(W) * H * 4, 0);
    for (int j = 0; j < H; ++j) {
        const int r = row_of[static_cast<std::size_t>(j)];
        if (r < 0) continue;
        for (int i = 0; i < W; ++i) {
            const int c = col_of[static_cast<std::size_t>(i)];
            if (c < 0) continue;
            px.rgba(static_cast<std::size_t>(r) * hp.cols + static_cast<std::size_t>(c),
                    &rgba[(static_cast<std::size_t>(j) * W + static_cast<std::size_t>(i)) * 4]);
        }
    }
    const std::string b64 = base64_encode(write_png_to_memory(W, H, rgba));
    o << "    <image x=\"" << X0 << "\" y=\"" << Y0 << "\" width=\"" << W << "\" height=\"" << H
      << "\" preserveAspectRatio=\"none\""
      << " style=\"image-rendering: optimizeSpeed;"
      <<        " image-rendering: -moz-crisp-edges;"
      <<        " image-rendering: -webkit-optimize-contrast;"
      <<        " image-rendering: crisp-edges;"
      <<        " image-rendering: pixelated;\""
      << " xlink:href=\"data:image/png;base64," << b64 << "\"/>\n";
}

// A heatmap_tri: no SVG primitive interpolates a colour over a triangle, so it
// is one <image> rasterized at the plot area's pixel size over its triangles'
// box, each pixel the colormap position under its centre
// (rasterize_heatmap_tri(), as GL interpolates in screen space).
static void emit_heatmap_tri(std::ostringstream& o, const SvgAxesData& d, const HeatmapTriPlot& p) {
    const auto& tr = d.layout.tr;
    const std::vector<TriPx> tris = heatmap_tri_pixels(p, tr);
    if (tris.empty()) return;
    double lx = tr.px + tr.pw, hx = tr.px, ly = tr.py + tr.ph, hy = tr.py;
    for (const TriPx& q : tris)
        for (int k = 0; k < 3; ++k) {
            lx = std::min(lx, q.x[k]);
            hx = std::max(hx, q.x[k]);
            ly = std::min(ly, q.y[k]);
            hy = std::max(hy, q.y[k]);
        }
    const int X0 = static_cast<int>(std::floor(std::max<double>(lx, tr.px)));
    const int X1 = static_cast<int>(std::ceil(std::min<double>(hx, tr.px + tr.pw)));
    const int Y0 = static_cast<int>(std::floor(std::max<double>(ly, tr.py)));
    const int Y1 = static_cast<int>(std::ceil(std::min<double>(hy, tr.py + tr.ph)));
    if (X1 <= X0 || Y1 <= Y0) return;
    const int W = X1 - X0, H = Y1 - Y0;

    const std::vector<float> t = rasterize_heatmap_tri(p, tr, X0, Y0, W, H);
    const std::uint8_t* lut = colormaps::get(p.opts.cmap);
    const bool cyclic = p.opts.cmap.is_cyclic();
    std::vector<uint8_t> rgba(static_cast<std::size_t>(W) * H * 4, 0);
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (std::isnan(t[i])) continue;
        std::memcpy(&rgba[i * 4], &lut[colormaps::index(t[i], cyclic) * 4], 4);
    }
    const std::string b64 = base64_encode(write_png_to_memory(W, H, rgba));
    o << "    <image x=\"" << X0 << "\" y=\"" << Y0 << "\" width=\"" << W << "\" height=\"" << H
      << "\" preserveAspectRatio=\"none\""
      << " style=\"image-rendering: optimizeSpeed;"
      <<        " image-rendering: -moz-crisp-edges;"
      <<        " image-rendering: -webkit-optimize-contrast;"
      <<        " image-rendering: crisp-edges;"
      <<        " image-rendering: pixelated;\""
      << " xlink:href=\"data:image/png;base64," << b64 << "\"/>\n";
}

// Heatmap as a base64 PNG <image>, via the same colormap LUT and origin flip
// as DataRenderer::draw_heatmap, plus mirroring for reversed ranges.
static void emit_heatmap(std::ostringstream& o, const SvgAxesData& d,
                         std::span<const std::size_t> which) {
    for (const std::size_t k : which) {
        const auto& hp = d.heatmaps[k];
        if (hp.rows <= 0 || hp.cols <= 0) continue;
        if (hp.mesh() || !d.layout.tr.xs.linear() || !d.layout.tr.ys.linear()) {
            emit_heatmap_cells(o, d, hp);
            continue;
        }

        const HeatmapPixels px(hp);
        const std::size_t n = static_cast<std::size_t>(hp.rows) * hp.cols;
        std::vector<uint8_t> rgba(n * 4);
        for (std::size_t i = 0; i < n; ++i) px.rgba(i, &rgba[i * 4]);

        // An <image> can't have a reversed box, so reversed ranges are
        // mirrored in the pixels (a reversed yrange cancels the origin flip).
        const bool mirror_x = (hp.xrange.hi < hp.xrange.lo);
        const bool flip_y   = (hp.opts.origin == "lower") != (hp.yrange.hi < hp.yrange.lo);

        std::vector<uint8_t> shuffled;
        if (flip_y || mirror_x) {
            shuffled.resize(rgba.size());
            for (int r = 0; r < hp.rows; ++r) {
                const int src_r = flip_y ? (hp.rows - 1 - r) : r;
                for (int c = 0; c < hp.cols; ++c) {
                    const int src_c = mirror_x ? (hp.cols - 1 - c) : c;
                    std::memcpy(&shuffled[(static_cast<std::size_t>(r) * hp.cols + c) * 4],
                                &rgba[(static_cast<std::size_t>(src_r) * hp.cols + src_c) * 4], 4);
                }
            }
        }
        const std::vector<uint8_t>& png_src = shuffled.empty() ? rgba : shuffled;

        const auto png_bytes = write_png_to_memory(hp.cols, hp.rows, png_src);
        const std::string b64 = base64_encode(png_bytes);

        // The plot's xrange x yrange, through the same CoordTransform as GL.
        const float x0 = d.layout.tr.to_px(hp.xrange.lo), x1 = d.layout.tr.to_px(hp.xrange.hi);
        const float y0 = d.layout.tr.to_py(hp.yrange.lo), y1 = d.layout.tr.to_py(hp.yrange.hi);
        const float ix = std::min(x0, x1), iw = std::abs(x1 - x0);
        const float iy = std::min(y0, y1), ih = std::abs(y1 - y0);

        // Nearest-neighbour scaling to match GL_NEAREST; the style fallback
        // chain lets each renderer pick the value it understands.
        o << "    <image x=\"" << ix << "\" y=\"" << iy
          << "\" width=\"" << iw << "\" height=\"" << ih
          << "\" preserveAspectRatio=\"none\""
          << " style=\"image-rendering: optimizeSpeed;"
          <<        " image-rendering: -moz-crisp-edges;"
          <<        " image-rendering: -webkit-optimize-contrast;"
          <<        " image-rendering: crisp-edges;"
          <<        " image-rendering: pixelated;\""
          << " xlink:href=\"data:image/png;base64," << b64 << "\"/>\n";
    }
}

static void emit_bars(std::ostringstream& o, const SvgAxesData& d,
                      std::span<const std::size_t> which) {
    const auto& tr = d.layout.tr;
    for (const std::size_t k : which) {
        const auto& bp = d.bars[k];
        const float half = static_cast<float>(bp.bar_width * 0.5);
        const std::string fill  = rgb(*bp.opts.color);
        const Color ec = bar_edge(bp);
        const std::string edge  = rgb(ec);
        for (std::size_t i = 0; i < bp.centers.size(); ++i) {
            // Standing: x from the category edges, y from base to tip. Lying: x
            // from base to tip, y from the category edges.
            float xl, bw, ytop, bh;
            if (!bp.horizontal()) {
                xl = tr.to_px(bp.centers[i] - half);
                bw = tr.to_px(bp.centers[i] + half) - xl;
                const float yb = tr.to_py(bp.base_at(i)), yt = tr.to_py(bp.tip_at(i));
                ytop = std::min(yb, yt);
                bh = std::max(yb, yt) - ytop;
            } else {
                const float xb = tr.to_px(bp.base_at(i)), xt = tr.to_px(bp.tip_at(i));
                xl = std::min(xb, xt);
                bw = std::max(xb, xt) - xl;
                const float y0 = tr.to_py(bp.centers[i] - half);
                const float y1 = tr.to_py(bp.centers[i] + half);
                ytop = std::min(y0, y1);
                bh = std::max(y0, y1) - ytop;
            }
            o << "    <rect x=\"" << xl << "\" y=\"" << ytop
              << "\" width=\"" << bw << "\" height=\"" << bh
              << "\" fill=\"" << fill << "\" fill-opacity=\"" << bp.opts.color->a * bp.opts.alpha << "\"";
            if (bp.opts.linewidth > 0.f)
                o << " stroke=\"" << edge << "\" stroke-opacity=\"" << ec.a
                  << "\" stroke-width=\"" << bp.opts.linewidth << "\"";
            o << "/>\n";
        }
    }
}

// A reference line as a <line>, a span as a <rect> (its outline the rect's
// stroke), from reference_lines.h's geometry, as the raster path.
static void emit_axline(std::ostringstream& o, const SvgAxesData& d, const AxLinePlot& p) {
    if (p.opts.linestyle == LineStyle::None || !(p.opts.linewidth > 0.0f)) return;
    if (p.sloped()) {
        const Color& c = *p.opts.color;
        for (const RefPath& run : axline_paths(p, d.layout.tr)) {
            o << "    <polyline fill=\"none\" stroke=\"" << rgb(c) << "\" stroke-opacity=\"" << c.a * p.opts.alpha
              << "\" stroke-width=\"" << p.opts.linewidth << "\"" << dash_attr(p.opts.linestyle) << " points=\"";
            for (const RefPt& q : run) o << q.x << "," << q.y << " ";
            o << "\"/>\n";
        }
        return;
    }
    const std::optional<RefSegment> g = axline_segment(p, d.layout.tr);
    if (!g) return;
    const Color& c = *p.opts.color;
    o << "    <line x1=\"" << g->x0 << "\" y1=\"" << g->y0 << "\" x2=\"" << g->x1 << "\" y2=\"" << g->y1
      << "\" stroke=\"" << rgb(c) << "\" stroke-opacity=\"" << c.a * p.opts.alpha
      << "\" stroke-width=\"" << p.opts.linewidth << "\"" << dash_attr(p.opts.linestyle) << "/>\n";
}

static void emit_axspan(std::ostringstream& o, const SvgAxesData& d, const AxSpanPlot& p) {
    const std::optional<RefRect> r = axspan_rect(p, d.layout.tr, axspan_reach(p));
    if (!r) return;
    const Color& c = *p.opts.color;
    o << "    <rect x=\"" << r->x0 << "\" y=\"" << r->y0 << "\" width=\"" << r->x1 - r->x0
      << "\" height=\"" << r->y1 - r->y0 << "\" fill=\"" << rgb(c) << "\" fill-opacity=\""
      << c.a * p.opts.alpha << "\"";
    if (p.opts.linewidth > 0.0f) {
        const Color ec = axspan_edge(p);
        o << " stroke=\"" << rgb(ec) << "\" stroke-opacity=\"" << ec.a << "\" stroke-width=\""
          << p.opts.linewidth << "\"";
    }
    o << "/>\n";
}

// A fill as one <polygon> per region (fill_geometry.h, as the raster path),
// its outline the polygon's stroke. A region whose curves cross pinches
// there; the nonzero rule fills both lobes, as the raster's triangles do.
static void emit_fill(std::ostringstream& o, const SvgAxesData& d, const FillPlot& p) {
    const CoordTransform& tr = d.layout.tr;
    const bool vert = p.vertical();
    const std::vector<FillRun> runs = fill_runs(p, vert ? tr.ys : tr.xs, vert ? tr.xs : tr.ys);
    const Color& c = *p.opts.color;
    const float a = c.a * p.opts.alpha;
    const bool edged = p.opts.linewidth > 0.0f && p.opts.linestyle != LineStyle::None;
    std::string stroke;
    if (edged) {
        const Color ec = fill_edge(p);
        std::ostringstream s;
        s << " stroke=\"" << rgb(ec) << "\" stroke-opacity=\"" << ec.a << "\" stroke-width=\""
          << p.opts.linewidth << "\"";
        stroke = s.str();
    }
    for (const FillRun& run : runs) {
        PixelPath ring;
        ring.loop = true;
        fill_outline(run, [&](double r, double v) {
            ring.x.push_back(tr.scale_to_px(vert ? v : r));
            ring.y.push_back(tr.scale_to_py(vert ? r : v));
        });
        // The outline as the raster strokes it (draw_fill): repeated points
        // dropped. Where it bevels a joint SVG would miter, the polygon is fill
        // only and the outline is drawn cut at those joints.
        std::vector<std::size_t> bevels;
        PixelPath outline;
        if (edged) {
            outline.loop = true;
            for (std::size_t i = 0; i < ring.x.size(); ++i) {
                if (!outline.x.empty() && outline.x.back() == ring.x[i] && outline.y.back() == ring.y[i])
                    continue;
                outline.x.push_back(ring.x[i]);
                outline.y.push_back(ring.y[i]);
            }
            while (outline.x.size() >= 2 && outline.x.front() == outline.x.back()
                   && outline.y.front() == outline.y.back()) {
                outline.x.pop_back();
                outline.y.pop_back();
            }
            bevels = short_bevels(outline, p.opts.linewidth);
        }

        o << "    <polygon fill=\"" << rgb(c) << "\"";
        if (a < 1.0f) o << " fill-opacity=\"" << a << "\"";
        if (edged && bevels.empty()) o << stroke << dash_attr(p.opts.linestyle);
        o << " points=\"";
        for (std::size_t i = 0; i < ring.x.size(); ++i) o << ring.x[i] << "," << ring.y[i] << " ";
        o << "\"/>\n";
        if (!bevels.empty()) {
            const Color ec = fill_edge(p);
            emit_beveled_stroke(o, outline, bevels, rgb(ec), ec.a, p.opts.linewidth, p.opts.linestyle);
        }
    }
}

// A shape (shape_geometry.h, as the raster path): one <path> for the fill, its
// rings under the even-odd rule so an annulus keeps its hole, then each ring's
// outline, cut at the joints the raster bevels (as emit_fill).
static void emit_shape(std::ostringstream& o, const ShapeGeom& g, const Color& c, float alpha, float linewidth,
                       LineStyle linestyle, const Color& ec) {
    if (g.empty()) return;
    const float a = c.a * alpha;
    if (a > 0.0f) {
        o << "    <path fill=\"" << rgb(c) << "\"";
        if (a < 1.0f) o << " fill-opacity=\"" << a << "\"";
        o << " fill-rule=\"evenodd\" d=\"";
        for (const std::vector<ShapePt>& ring : g.rings) {
            for (std::size_t i = 0; i < ring.size(); ++i)
                o << (i == 0 ? "M" : "L") << ring[i].x << "," << ring[i].y << " ";
            o << "Z ";
        }
        o << "\"/>\n";
    }
    if (!(linewidth > 0.0f) || linestyle == LineStyle::None) return;
    for (const std::vector<ShapePt>& ring : g.rings) {
        PixelPath outline;
        outline.loop = true;
        for (const ShapePt& p : ring) {
            if (!outline.x.empty() && outline.x.back() == p.x && outline.y.back() == p.y) continue;
            outline.x.push_back(p.x);
            outline.y.push_back(p.y);
        }
        while (outline.x.size() >= 2 && outline.x.front() == outline.x.back()
               && outline.y.front() == outline.y.back()) {
            outline.x.pop_back();
            outline.y.pop_back();
        }
        if (outline.x.size() < 2) continue;
        const std::vector<std::size_t> bevels = short_bevels(outline, linewidth);
        if (!bevels.empty()) {
            emit_beveled_stroke(o, outline, bevels, rgb(ec), ec.a, linewidth, linestyle);
            continue;
        }
        o << "    <polygon fill=\"none\" stroke=\"" << rgb(ec) << "\" stroke-opacity=\"" << ec.a
          << "\" stroke-width=\"" << linewidth << "\"" << dash_attr(linestyle) << " points=\"";
        for (std::size_t i = 0; i < outline.x.size(); ++i) o << outline.x[i] << "," << outline.y[i] << " ";
        o << "\"/>\n";
    }
}

template <class Shape>   // PolyPlot, CirclePlot or WedgePlot
static void emit_shape_of(std::ostringstream& o, const SvgAxesData& d, const Shape& p) {
    emit_shape(o, shape_geometry(p, d.layout.tr), *p.opts.color, p.opts.alpha, p.opts.linewidth, p.opts.linestyle,
               shape_edge(p));
}

// A quiver as one <polygon> per arrow (quiver_geometry.h, as the raster path),
// in a <g> carrying the fill; a colormapped quiver's fill is on each polygon,
// the group keeping the quiver's opacity.
static void emit_quiver(std::ostringstream& o, const SvgAxesData& d, const QuiverPlot& p) {
    const CoordTransform& tr = d.layout.tr;
    const bool mapped = p.colormapped();
    const Color& c = *p.opts.color;
    const float a = mapped ? p.opts.alpha : c.a * p.opts.alpha;
    bool open = false;
    for_each_quiver_arrow(p, tr.xs, tr.ys, [&](std::size_t i, double tx, double ty, double hx, double hy) {
        float pts[14];
        const int n = arrow_outline(tr.scale_to_px(tx), tr.scale_to_py(ty), tr.scale_to_px(hx),
                                    tr.scale_to_py(hy), p.opts, pts);
        if (n == 0) return;
        if (!open) {
            o << "    <g";
            if (!mapped) o << " fill=\"" << rgb(c) << "\"";
            if (a < 1.0f) o << " fill-opacity=\"" << a << "\"";
            o << ">\n";
            open = true;
        }
        o << "      <polygon ";
        if (mapped) {
            // A map's own alpha (all 1 for the built-ins) overrides the group's.
            const Color ci = quiver_arrow_color(p, i);
            o << "fill=\"" << rgb(ci) << "\" ";
            if (ci.a < 1.0f) o << "fill-opacity=\"" << ci.a * p.opts.alpha << "\" ";
        }
        o << "points=\"";
        for (int k = 0; k < n; ++k) o << pts[2 * k] << "," << pts[2 * k + 1] << " ";
        o << "\"/>\n";
    });
    if (open) o << "    </g>\n";
}

// The plot objects in call order, each with its error bars, as
// DataRenderer::draw_plots().
static void emit_contour_plot(std::ostringstream& o, const SvgAxesData& d, const ContourPlot& p);
static void emit_contour_draw(std::ostringstream& o, const SvgAxesData& d, const ContourDraw& cd,
                              const Color& color, float linewidth, float fontsize);

static void emit_object(std::ostringstream& o, const SvgAxesData& d, const ObjectRef& r) {
    const std::size_t one[1] = {r.index};
    switch (r.kind) {
        case PlotKind::Heatmap:
            emit_heatmap(o, d, one);
            break;
        case PlotKind::Bar:
            emit_bars(o, d, one);
            emit_error_bars(o, d, r);
            break;
        case PlotKind::Line:
            emit_lines(o, d, one);
            emit_error_bars(o, d, r);
            break;
        case PlotKind::Scatter:
            emit_error_bars(o, d, r);
            emit_scatter(o, d, one);
            break;
        case PlotKind::ScatterZ:
            emit_error_bars(o, d, r);
            emit_scatter_z(o, d, one);
            break;
        case PlotKind::AxLine:
            emit_axline(o, d, d.axlines[r.index]);
            break;
        case PlotKind::AxSpan:
            emit_axspan(o, d, d.axspans[r.index]);
            break;
        case PlotKind::Fill:
            emit_fill(o, d, d.fills[r.index]);
            break;
        case PlotKind::Quiver:
            emit_quiver(o, d, d.quivers[r.index]);
            break;
        case PlotKind::Poly:
            emit_shape_of(o, d, d.polys[r.index]);
            break;
        case PlotKind::Circle:
            emit_shape_of(o, d, d.circles[r.index]);
            break;
        case PlotKind::Wedge:
            emit_shape_of(o, d, d.wedges[r.index]);
            break;
        case PlotKind::Pie: {
            // One shape per slice, as the raster path draws them.
            const PiePlot& p = d.pies[r.index];
            for (const PieSlice& s : pie_slices(p))
                emit_shape(o, pie_slice_geometry(s, p.opts, d.layout.tr), pie_color(p.opts, s.k), p.opts.alpha,
                           p.opts.linewidth, LineStyle::Solid, pie_edge(p.opts, s.k));
            break;
        }
        case PlotKind::HeatmapTri:
            emit_heatmap_tri(o, d, d.heatmap_tris[r.index]);
            break;
        case PlotKind::Contour:
            emit_contour_plot(o, d, d.contours[r.index]);
            break;
        default: break;
    }
}

// A twinned cell: every layer's objects in one call order, as
// DataRenderer::draw_layers().
static void emit_plots(std::ostringstream& o, const SvgAxesData& d) {
    if (d.twins.empty()) {
        for (const ObjectRef& r : draw_order(d)) emit_object(o, d, r);
        return;
    }
    std::vector<const SvgAxesData*> layers{&d};
    for (const SvgAxesData& t : d.twins) layers.push_back(&t);
    for (const LayerRef& r : layered_draw_order<SvgAxesData>(layers))
        emit_object(o, *layers[r.layer], r.ref);
}

// Contours via the shared trace_contours()/plan_contours() (no cache: one
// trace per export). Emitted in their own clipped group after the data, below
// border and grid, as in render_frame().
static bool has_contours(const SvgAxesData& d) {
    for (const auto& hp : d.heatmaps)
        if (object_shown(hp) && !hp.opts.contours.empty() && hp.rows > 0 && hp.cols > 0) return true;
    return false;
}

// Emit one planned contour set (2D heatmaps and planes alike).
static void emit_contour_draw(std::ostringstream& o, const SvgAxesData& d,
                              const ContourDraw& cd, const Color& color,
                              float linewidth, float fontsize) {
        const std::string stroke = rgb(color);

        for (const auto& run : cd.runs) {
            o << "    <polyline fill=\"none\" stroke=\"" << stroke
              << "\" stroke-opacity=\"" << color.a
              << "\" stroke-width=\"" << linewidth
              << "\" points=\"";
            for (std::size_t k = 0; k < run.px.size(); ++k) {
                if (k) o << " ";
                o << run.px[k] << "," << run.py[k];
            }
            o << "\"/>\n";
        }

        if (cd.labels.empty()) return;
        // Rotate about the anchor, with the baseline offset inside, so text is
        // vertically centred like NVG_ALIGN_MIDDLE.
        const float base = middle_baseline_offset(d.axes_style.font_path,
                                                  fontsize);
        for (const auto& lb : cd.labels) {
            o << "    <text x=\"0\" y=\"" << base
              << "\" transform=\"translate(" << lb.x << "," << lb.y << ") rotate("
              << lb.angle * 57.2957795f << ")\" text-anchor=\"middle\""
              << " font-family=\"" << svg_font_family(d.axes_style) << "\""
              << " font-size=\"" << fontsize << "\""
              << " fill=\"" << stroke << "\" fill-opacity=\"" << color.a
              << "\">" << xml_escape(lb.text) << "</text>\n";
        }
}

// A contour() or contourf() in its place among the data: a contourf's
// triangles as heatmap_tri's raster, a contour's lines and labels level by
// level, each in its colour.
static void emit_contour_plot(std::ostringstream& o, const SvgAxesData& d, const ContourPlot& p) {
    if (!object_shown(p) || !p.geom) return;
    if (p.opts.filled) {
        if (p.geom->levels.size() >= 2 && !p.geom->fill.tri.empty()) emit_heatmap_tri(o, d, p.geom->fill);
        return;
    }
    if (!(p.opts.linewidth > 0.0f) || p.geom->lines.empty()) return;
    const ContourDraw cd = plan_contours(p.geom->lines, d.layout.tr, p.opts.labels, p.opts.fontsize,
                                         d.axes_style.font_path);
    std::vector<ContourDraw> by_level(p.geom->levels.size());
    for (const auto& r : cd.runs) by_level[p.geom->line_level[r.line]].runs.push_back(r);
    for (const auto& l : cd.labels) by_level[p.geom->line_level[l.line]].labels.push_back(l);
    for (std::size_t k = 0; k < by_level.size(); ++k) {
        if (by_level[k].runs.empty()) continue;
        const std::optional<Color> c = contour_line_color(p, k);
        if (c) emit_contour_draw(o, d, by_level[k], *c, p.opts.linewidth, p.opts.fontsize);
    }
}

static void emit_contours(std::ostringstream& o, const SvgAxesData& d) {
    for (const auto& hp : d.heatmaps) {
        if (!object_shown(hp) || hp.opts.contours.empty() || hp.rows <= 0 || hp.cols <= 0) continue;
        const ContourSet  set = trace_contours(hp);
        const ContourDraw cd  = plan_contours(set, d.layout.tr, hp.opts,
                                              d.axes_style.font_path);
        emit_contour_draw(o, d, cd, hp.opts.contour_color,
                          hp.opts.contour_linewidth, hp.opts.contour_fontsize);
    }
}

// A 3D axes' plane contours, pre-planned in figure_export.cpp.
static void emit_contours3d(std::ostringstream& o, const SvgAxesData& d) {
    for (const auto& p : d.contours3d)
        emit_contour_draw(o, d, p.draw, p.color, p.linewidth, p.fontsize);
}

// The frame outline: one <path> of the visible edges in spine_color, then one
// per edge in an axis color of its own (a twinned cell, SvgAxesData::edges).
static void emit_spines(std::ostringstream& o, const SvgAxesData& d) {
    const AxesStyle& st = d.axes_style;
    const PlotRect&  r  = d.layout.frame;
    struct Edge { bool on; const std::optional<Color>* own; char dir; float x, y, to; };
    const Edge all[4] = {
        {st.spine_bottom, &d.edges.bottom, 'H', r.x, r.y + r.h, r.x + r.w},
        {st.spine_top, &d.edges.top, 'H', r.x, r.y, r.x + r.w},
        {st.spine_left, &d.edges.left, 'V', r.x, r.y, r.y + r.h},
        {st.spine_right, &d.edges.right, 'V', r.x + r.w, r.y, r.y + r.h},
    };
    const auto end_path = [&](const Color& c) {
        o << "\" fill=\"none\" stroke=\"" << rgb(c)
          << "\" stroke-opacity=\"" << c.a
          << "\" stroke-width=\"" << st.spine_linewidth << "\"/>\n";
    };

    bool any = false;
    for (const Edge& e : all)
        if (e.on && !e.own->has_value()) {
            if (!any) o << "  <path d=\"";
            any = true;
            o << "M" << e.x << " " << e.y << e.dir << e.to << " ";
        }
    if (any) end_path(st.spine_color);
    for (const Edge& e : all)
        if (e.on && e.own->has_value()) {
            o << "  <path d=\"M" << e.x << " " << e.y << e.dir << e.to << " ";
            end_path(**e.own);
        }
}

// An axis that landed off the frame's edges, so no spine draws it.
static void emit_interior_axes(std::ostringstream& o, const SvgAxesData& d) {
    const AxesStyle& st = d.axes_style;
    const PlotRect&  r  = d.layout.frame;
    if (!d.layout.xaxis_interior && !d.layout.yaxis_interior) return;

    o << "  <path d=\"";
    if (d.layout.xaxis_interior)
        o << "M" << r.x << " " << d.layout.xaxis_y << "H" << r.x + r.w << " ";
    if (d.layout.yaxis_interior)
        o << "M" << d.layout.yaxis_x << " " << r.y << "V" << r.y + r.h << " ";
    o << "\" fill=\"none\" stroke=\"" << rgb(st.spine_color)
      << "\" stroke-opacity=\"" << st.spine_color.a
      << "\" stroke-width=\"" << st.spine_linewidth << "\"/>\n";
}

// Each axis in its own look (tick_look()); axes that look alike share one
// group, marks and labels apart.
static void emit_ticks_and_labels(std::ostringstream& o, const SvgAxesData& d) {
    const AxesStyle& st = d.axes_style;
    const TickLook xk = tick_look(st, TickAxis::X);
    const TickLook yk = tick_look(st, TickAxis::Y);
    const auto open_marks = [&](const TickLook& k) {
        o << "  <g stroke=\"" << rgb(k.color) << "\" stroke-opacity=\""
          << k.color.a << "\" stroke-width=\"" << k.linewidth << "\">\n";
    };

    // Along the axis lines, in the layout's direction (matches NanoVG).
    const float xa = d.layout.xaxis_y;
    const float ya = d.layout.yaxis_x;
    const PlotRect& f = d.layout.frame;
    const auto x_marks = [&] {
        for (const auto& t : d.layout.xticks) {
            if (!st.show_xticks) break;
            float px = d.layout.tr.to_px(t.value);
            o << "    <line x1=\"" << px << "\" y1=\"" << xa
              <<        "\" x2=\"" << px << "\" y2=\"" << xa + d.layout.xtick_dir * xk.length << "\"/>\n";
        }
    };
    const auto y_marks = [&] {
        for (const auto& t : d.layout.yticks) {
            if (!st.show_yticks) break;
            float py = d.layout.tr.to_py(t.value);
            o << "    <line x1=\"" << ya + d.layout.ytick_dir * yk.length << "\" y1=\"" << py
              <<        "\" x2=\"" << ya << "\" y2=\"" << py << "\"/>\n";
        }
    };
    // Minor marks, only those in the frame (as NanoVG).
    const auto x_minor = [&] {
        for (const double v : d.layout.xminor) {
            if (!st.show_xticks) break;
            const float px = d.layout.tr.to_px(v);
            if (px < f.x || px > f.x + f.w) continue;
            o << "    <line x1=\"" << px << "\" y1=\"" << xa
              <<        "\" x2=\"" << px << "\" y2=\"" << xa + d.layout.xtick_dir * xk.minor_length << "\"/>\n";
        }
    };
    const auto y_minor = [&] {
        for (const double v : d.layout.yminor) {
            if (!st.show_yticks) break;
            const float py = d.layout.tr.to_py(v);
            if (py < f.y || py > f.y + f.h) continue;
            o << "    <line x1=\"" << ya + d.layout.ytick_dir * yk.minor_length << "\" y1=\"" << py
              <<        "\" x2=\"" << ya << "\" y2=\"" << py << "\"/>\n";
        }
    };
    if (same_marks(xk, yk)) {
        open_marks(xk);
        x_marks();
        y_marks();
        x_minor();
        y_minor();
        o << "  </g>\n";
    } else {
        open_marks(xk);
        x_marks();
        x_minor();
        o << "  </g>\n";
        open_marks(yk);
        y_marks();
        y_minor();
        o << "  </g>\n";
    }

    const auto open_labels = [&](const TickLook& k) {
        o << "  <g font-family=\"" << svg_font_family(st) << "\" font-size=\"" << k.label_fontsize
          << "\" fill=\"" << rgb(k.label_color) << "\">\n";
    };
    // Baseline offsets from the font's metrics (a label's own with math),
    // matching NanoVG's top-hung x labels and centred y labels.
    const std::string& fp = st.font_path;
    const auto x_labels = [&] {
        const float fsz = xk.label_fontsize;
        for (const auto& t : d.layout.xticks) {
            if (!st.show_xticks || !d.layout.xlabels) break;
            float px = d.layout.tr.to_px(t.value);
            emit_text(o, "    ", px, d.layout.xlabel_top + top_baseline_offset(fp, fsz, t.label), "middle", "",
                      t.label, fp, fsz);
        }
    };
    const auto y_labels = [&] {
        const float fsz = yk.label_fontsize;
        const char* y_anchor = d.layout.ylabel_align == HAlign::Left ? "start" : "end";
        for (const auto& t : d.layout.yticks) {
            if (!st.show_yticks || !d.layout.ylabels) break;
            float py = d.layout.tr.to_py(t.value);
            emit_text(o, "    ", d.layout.ylabel_x, py + middle_baseline_offset(fp, fsz, t.label), y_anchor, "",
                      t.label, fp, fsz);
        }
    };
    if (same_labels(xk, yk)) {
        open_labels(xk);
        x_labels();
        y_labels();
        o << "  </g>\n";
    } else {
        open_labels(xk);
        x_labels();
        o << "  </g>\n";
        open_labels(yk);
        y_labels();
        o << "  </g>\n";
    }
}

// Anchors come from the layout; only the centre-to-baseline conversion is here.
static void emit_titles(std::ostringstream& o, const SvgAxesData& d) {
    const std::string& fp = d.axes_style.font_path;

    const auto font_attrs = [&](float fsz, const Color& c) {
        std::ostringstream a;
        a << " font-family=\"" << svg_font_family(d.axes_style) << "\""
          << " font-size=\"" << fsz << "\" fill=\"" << rgb(c) << "\"";
        return a.str();
    };
    if (!d.title.empty()) {
        const float fsz = d.axes_style.title_fontsize;
        emit_text(o, "  ", d.layout.title_x, d.layout.title_y + middle_baseline_offset(fp, fsz, d.title), "middle",
                  font_attrs(fsz, d.axes_style.title_color), d.title, fp, fsz);
    }
    if (!d.xtitle.empty()) {
        const float fsz = d.axes_style.xtitle_fontsize;
        emit_text(o, "  ", d.layout.xtitle_x, d.layout.xtitle_y + middle_baseline_offset(fp, fsz, d.xtitle),
                  "middle", font_attrs(fsz, d.axes_style.xtitle_color), d.xtitle, fp, fsz);
    }
    if (!d.ytitle.empty()) {
        const float fsz = d.axes_style.ytitle_fontsize;
        const float cx = d.layout.ytitle_x;
        const float cy = d.layout.ytitle_y;
        // Baseline shift in the rotated frame (as NanoVG does after
        // nvgRotate): under rotate(-90) local +y is global +x.
        std::ostringstream a;
        a << font_attrs(fsz, d.axes_style.ytitle_color) << " transform=\"rotate(-90," << cx << "," << cy << ")"
          << " translate(" << cx << "," << cy << ")\"";
        emit_text(o, "  ", 0.0f, middle_baseline_offset(fp, fsz, d.ytitle), "middle", a.str(), d.ytitle, fp, fsz);
    }
}

// Texts from the plan (positions, baselines and arrow already in pixels), in
// render_frame()'s order: over the frame furniture, under the legend.
static void emit_texts(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    for (const TextDraw& t : d.texts) {
        const MathTextScope math(t.math, mathtext_font());
        o << (t.clip ? "  <g clip-path=\"url(#plotArea" + std::to_string(idx) + ")\">\n" : "  <g>\n");
        if (t.shaft.size() >= 4 && t.linewidth > 0.0f) {
            o << "    <polyline fill=\"none\" stroke=\"" << rgb(t.arrow_color)
              << "\" stroke-opacity=\"" << t.arrow_color.a << "\" stroke-width=\"" << t.linewidth
              << "\"" << dash_attr(t.linestyle) << " points=\"";
            for (std::size_t i = 0; i + 1 < t.shaft.size(); i += 2)
                o << (i ? " " : "") << t.shaft[i] << "," << t.shaft[i + 1];
            o << "\"/>\n";
        }
        for (const TextDraw::Head& h : t.heads) {
            const int n = h.kind == ArrowHead::Bar ? 2 : 3;
            if (h.kind == ArrowHead::Filled)
                o << "    <polygon fill=\"" << rgb(t.arrow_color) << "\" fill-opacity=\"" << t.arrow_color.a
                  << "\" points=\"";
            else
                o << "    <polyline fill=\"none\" stroke=\"" << rgb(t.arrow_color) << "\" stroke-opacity=\""
                  << t.arrow_color.a << "\" stroke-width=\"" << std::max(t.linewidth, 1.0f) << "\" points=\"";
            for (int k = 0; k < n; ++k)
                o << (k ? " " : "") << h.xy[2 * k] << "," << h.xy[2 * k + 1];
            o << "\"/>\n";
        }
        o << "    <g transform=\"translate(" << t.ax << "," << t.ay << ")";
        if (t.angle != 0.0f) o << " rotate(" << t.angle * 57.2957795f << ")";
        o << "\">\n";
        if (t.has_box) {
            o << "      <rect x=\"" << t.box.x << "\" y=\"" << t.box.y << "\" width=\"" << t.box.w
              << "\" height=\"" << t.box.h << "\"";
            if (t.fill.a > 0.0f) o << " fill=\"" << rgb(t.fill) << "\" fill-opacity=\"" << t.fill.a << "\"";
            else o << " fill=\"none\"";
            if (t.edge_width > 0.0f)
                o << " stroke=\"" << rgb(t.edge) << "\" stroke-opacity=\"" << t.edge.a
                  << "\" stroke-width=\"" << t.edge_width << "\"";
            o << "/>\n";
        }
        const char* anchor = t.ha == HAlign::Left ? "start" : t.ha == HAlign::Right ? "end" : "middle";
        for (const TextDraw::Line& l : t.lines) {
            if (l.text.empty()) continue;
            std::ostringstream a;
            a << " font-family=\"" << svg_font_family_for(t.font_path) << "\" font-size=\"" << t.fontsize
              << "\" fill=\"" << rgb(t.color) << "\" fill-opacity=\"" << t.color.a << "\"";
            emit_text(o, "      ", l.x, l.y, anchor, a.str(), l.text, t.font_path, t.fontsize);
        }
        o << "    </g>\n  </g>\n";
    }
}

// Entries and box come from the layout (collect_legend_entries()).
static void emit_legend(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    if (!d.layout.has_legend()) return;

    const auto& entries = d.layout.legend_entries;
    const auto& opts  = d.legend_opts;
    const float fsz   = opts.fontsize;
    const float bx = d.layout.legend.x, by = d.layout.legend.y;

    if (opts.frameon) {
        o << "  <rect x=\"" << bx << "\" y=\"" << by
          << "\" width=\"" << d.layout.legend.w << "\" height=\"" << d.layout.legend.h
          << "\" rx=\"3\" fill=\"" << rgb(opts.frame_color)
          << "\" fill-opacity=\"" << opts.frame_color.a << "\""
          << " stroke=\"" << rgb(opts.border_color)
          << "\" stroke-opacity=\"" << opts.border_color.a << "\""
          << " stroke-width=\"" << opts.border_linewidth << "\"/>\n";
    }

    for (std::size_t i = 0; i < entries.size() && i < d.layout.legend_slots.size(); ++i) {
        const auto& e = entries[i];
        const float cy  = d.layout.legend_slots[i].cy;
        const float sx0 = d.layout.legend_slots[i].x, sx1 = sx0 + kLegendSwatchW;
        const std::string fill = rgb(e.color);

        if (e.kind == LegendKind::Line && e.swept) {
            // Colormap swept along the swatch, with stops sampled from the map
            // (a two-stop gradient would leave the colormap).
            constexpr int kStops = 9;
            const std::string gid = "legendGrad" + std::to_string(idx) + "_"
                                  + std::to_string(i);
            o << "    <linearGradient id=\"" << gid
              << "\" gradientUnits=\"userSpaceOnUse\" x1=\"" << sx0 << "\" y1=\"" << cy
              << "\" x2=\"" << sx1 << "\" y2=\"" << cy << "\">\n";
            const uint8_t* lut = colormaps::get(e.cmap);
            for (int k = 0; k < kStops; ++k) {
                const float f = static_cast<float>(k) / (kStops - 1);
                const int idx2 = static_cast<int>(f * 255.0f);
                const Color c{ lut[idx2 * 4]     / 255.0f,
                               lut[idx2 * 4 + 1] / 255.0f,
                               lut[idx2 * 4 + 2] / 255.0f, 1.0f };
                o << "      <stop offset=\"" << f << "\" stop-color=\"" << rgb(c)
                  << "\"/>\n";
            }
            o << "    </linearGradient>\n";
            o << "    <line x1=\"" << sx0 << "\" y1=\"" << cy
              << "\" x2=\"" << sx1 << "\" y2=\"" << cy
              << "\" stroke=\"url(#" << gid << ")\" stroke-width=\"2\"/>\n";
        } else if (e.kind == LegendKind::Line) {
            o << "    <line x1=\"" << sx0 << "\" y1=\"" << cy
              << "\" x2=\"" << sx1 << "\" y2=\"" << cy
              << "\" stroke=\"" << fill << "\" stroke-width=\"2\""
              << dash_attr(e.style) << "/>\n";
        } else if (e.kind == LegendKind::Arrow) {
            // A quiver's key, as the raster's (legend_arrow_outline()).
            float pts[14];
            const int n = legend_arrow_outline(sx0, sx1, cy, pts);
            o << "    <polygon fill=\"" << fill << "\"";
            if (e.color.a < 1.0f) o << " fill-opacity=\"" << e.color.a << "\"";
            if (e.edge.a > 0.0f)
                o << " stroke=\"" << rgb(e.edge) << "\" stroke-opacity=\"" << e.edge.a
                  << "\" stroke-width=\"1\"";
            o << " points=\"";
            for (int k = 0; k < n; ++k) o << pts[2 * k] << "," << pts[2 * k + 1] << " ";
            o << "\"/>\n";
        } else if (e.kind == LegendKind::Marker) {
            // Same emitter as the data markers. Alpha 0 edge = none (all but
            // scatter_z).
            emit_scatter_marker(o, (sx0 + sx1) * 0.5f, cy, 4.5f, fill, e.color.a, e.marker,
                                e.edge.a > 0.0f ? rgb(e.edge) : std::string(), e.edge.a);
        } else {
            // A translucent patch (a span's key) at its opacity, as the raster's.
            o << "    <rect x=\"" << sx0 << "\" y=\"" << cy - 5.f
              << "\" width=\"" << kLegendSwatchW << "\" height=\"10\" fill=\"" << fill << "\"";
            if (e.color.a < 1.0f) o << " fill-opacity=\"" << e.color.a << "\"";
            // A fill's outline (alpha 0 = none), 1 px as a marker key's.
            if (e.edge.a > 0.0f)
                o << " stroke=\"" << rgb(e.edge) << "\" stroke-opacity=\"" << e.edge.a
                  << "\" stroke-width=\"1\"";
            o << "/>\n";
        }

        std::ostringstream a;
        a << " font-family=\"" << svg_font_family_for(opts.font_path)
          << "\" font-size=\"" << fsz
          << "\" fill=\"" << rgb(opts.text_color)
          << "\" fill-opacity=\"" << opts.text_color.a << "\"";
        emit_text(o, "    ", sx1 + kLegendGap, cy + middle_baseline_offset(opts.font_path, fsz, e.name), nullptr,
                  a.str(), e.name, opts.font_path, fsz);
    }
}

// Gradient bar, border and vmin/vmax labels per colorbar, matching
// NvgRenderer::draw_colorbar. Gradients are defined in emit_defs(); ids carry
// the axes and bar index.
static void emit_colorbar(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    if (!d.layout.has_colorbar()) return;

    const auto& cb = d.colorbar_opts;
    for (std::size_t b = 0; b < d.layout.colorbars.size(); ++b) {
        const auto& r = d.layout.colorbars[b].rect;
        if (d.layout.colorbars[b].stepped()) {
            // A boundary norm's bar: a flat rect per band, then the border alone.
            const auto& sb = d.layout.colorbars[b];
            const uint8_t* lut = colormaps::get(sb.cmap);
            const std::size_t n = sb.band_t.size();
            for (std::size_t i = 0; i < n; ++i) {
                const uint8_t* c = &lut[colormaps::index(sb.band_t[i], sb.cmap.is_cyclic()) * 4];
                const float f0 = static_cast<float>(i) / static_cast<float>(n);
                const float f1 = static_cast<float>(i + 1) / static_cast<float>(n);
                const float bx = sb.horizontal ? r.x + f0 * r.w : r.x;
                const float by = sb.horizontal ? r.y : r.y + r.h - f1 * r.h;
                const float bw = sb.horizontal ? (f1 - f0) * r.w : r.w;
                const float bh = sb.horizontal ? r.h : (f1 - f0) * r.h;
                o << "  <rect x=\"" << bx << "\" y=\"" << by << "\" width=\"" << bw << "\" height=\"" << bh
                  << "\" fill=\"rgb(" << static_cast<int>(c[0]) << "," << static_cast<int>(c[1]) << ","
                  << static_cast<int>(c[2]) << ")\"/>\n";
            }
            o << "  <rect x=\"" << r.x << "\" y=\"" << r.y
              << "\" width=\"" << r.w << "\" height=\"" << r.h
              << "\" fill=\"none\""
              << " stroke=\"" << rgb(cb.border_color)
              << "\" stroke-opacity=\"" << cb.border_color.a << "\""
              << " stroke-width=\"" << cb.border_linewidth << "\"/>\n";
        } else {
            o << "  <rect x=\"" << r.x << "\" y=\"" << r.y
              << "\" width=\"" << r.w << "\" height=\"" << r.h
              << "\" fill=\"url(#colorbarGrad" << idx << "_" << b << ")\""
              << " stroke=\"" << rgb(cb.border_color)
              << "\" stroke-opacity=\"" << cb.border_color.a << "\""
              << " stroke-width=\"" << cb.border_linewidth << "\"/>\n";
        }

        // Anchors from the layout, centred on the line.
        const auto& cbx  = d.layout.colorbars[b];
        const float base = middle_baseline_offset(cb.font_path, cb.fontsize);
        const char* anchor = cbx.num_align == HAlign::Left  ? "start"
                           : cbx.num_align == HAlign::Right ? "end"
                                                            : "middle";

        char buf[32];
        o << "  <g font-family=\"" << svg_font_family_for(cb.font_path)
          << "\" font-size=\"" << cb.fontsize
          << "\" fill=\"" << rgb(cb.text_color)
          << "\" fill-opacity=\"" << cb.text_color.a << "\">\n";
        if (cbx.stepped()) {
            for (const auto& l: cbx.labels)
                o << "    <text x=\"" << l.x << "\" y=\"" << l.y + base
                  << "\" text-anchor=\"" << anchor << "\">" << xml_escape(l.text) << "</text>\n";
        } else {
            std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(cbx.vmax));
            o << "    <text x=\"" << cbx.vmax_x << "\" y=\"" << cbx.vmax_y + base
              << "\" text-anchor=\"" << anchor << "\">" << xml_escape(buf) << "</text>\n";
            std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(cbx.vmin));
            o << "    <text x=\"" << cbx.vmin_x << "\" y=\"" << cbx.vmin_y + base
              << "\" text-anchor=\"" << anchor << "\">" << xml_escape(buf) << "</text>\n";
        }

        // The bar's name: beyond a horizontal bar, rotated along a vertical
        // one (baseline shift in the rotated frame, as for the y title).
        const float name_base = middle_baseline_offset(cb.font_path, cb.fontsize, cbx.name);
        if (!cbx.name.empty() && cbx.horizontal) {
            emit_text(o, "    ", cbx.name_x, cbx.name_y + name_base, "middle", "", cbx.name, cb.font_path,
                      cb.fontsize);
        } else if (!cbx.name.empty()) {
            std::ostringstream a;
            a << " transform=\"rotate(-90," << cbx.name_x << "," << cbx.name_y << ")"
              << " translate(" << cbx.name_x << "," << cbx.name_y << ")\"";
            emit_text(o, "    ", 0.0f, name_base, "middle", a.str(), cbx.name, cb.font_path, cb.fontsize);
        }
        o << "  </g>\n";
    }
}

// -------------------------------------------------------------------------
// Public entry points
// -------------------------------------------------------------------------
static void emit_defs(std::ostringstream& o, const std::vector<SvgAxesData>& axes) {
    o << "<defs>\n";
    for (std::size_t i = 0; i < axes.size(); ++i) {
        const auto& d = axes[i];
        if (d.layout.polar) {
            std::vector<float> x, y;
            d.layout.polar->outline(x, y);
            o << "  <clipPath id=\"plotArea" << i << "\"><polygon points=\"";
            for (std::size_t k = 0; k < x.size(); ++k) o << x[k] << "," << y[k] << " ";
            o << "\"/></clipPath>\n";
            continue;
        }
        o << "  <clipPath id=\"plotArea" << i << "\">"
          << "<rect x=\"" << d.layout.frame.x << "\" y=\"" << d.layout.frame.y
          << "\" width=\"" << d.layout.frame.w << "\" height=\"" << d.layout.frame.h << "\"/>"
          << "</clipPath>\n";

        if (!d.layout.has_colorbar()) continue;
        // Vertical: offset 0% (top) = vmax. All 256 LUT entries become stops
        // for fidelity. One gradient per bar (not deduplicated per colormap).
        // Horizontal: left to right, 0% = vmin.
        for (std::size_t b = 0; b < d.layout.colorbars.size(); ++b) {
            if (d.layout.colorbars[b].stepped()) continue; // flat rects, no gradient
            const bool     horiz = d.layout.colorbars[b].horizontal;
            const uint8_t* lut = colormaps::get(d.layout.colorbars[b].cmap);
            o << "  <linearGradient id=\"colorbarGrad" << i << "_" << b
              << (horiz ? "\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\">\n"
                        : "\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">\n");
            for (int s = 0; s < 256; ++s) {
                const uint8_t* c = &lut[(horiz ? s : 255 - s) * 4];
                o << "    <stop offset=\"" << (s / 255.0f * 100.0f) << "%\" stop-color=\"rgb("
                  << static_cast<int>(c[0]) << "," << static_cast<int>(c[1]) << "," << static_cast<int>(c[2])
                  << ")\"/>\n";
            }
            o << "  </linearGradient>\n";
        }
    }
    o << "</defs>\n";
}

// One axes' markup, in render_frame()'s order: background, data, border, then
// grid and ticks over the data. Clip ids are per axes.
// One group of Box3DPlan polylines: `close` = filled pane, else an open line
// (as NvgRenderer's stroke_group()).
static void emit_polys(std::ostringstream& o, const std::vector<Box3DPlan::Poly>& polys,
                       const std::string& fill, const Color* stroke, float stroke_w, bool close) {
    for (const auto& p : polys) {
        if (p.xy.size() < 4) continue;
        o << "  <" << (close ? "polygon" : "polyline") << " points=\"";
        for (std::size_t i = 0; i + 1 < p.xy.size(); i += 2) {
            if (i) o << ' ';
            o << p.xy[i] << ',' << p.xy[i + 1];
        }
        o << "\" fill=\"" << fill << "\"";
        if (stroke && stroke_w > 0.0f)
            o << " stroke=\"" << rgb(*stroke) << "\" stroke-opacity=\"" << stroke->a
              << "\" stroke-width=\"" << stroke_w << "\"";
        o << "/>\n";
    }
}

static void emit_box3d_labels(std::ostringstream& o,
                              const std::vector<Box3DPlan::Label>& labels) {
    for (const auto& l : labels) {
        if (l.text.empty()) continue;
        // Centred on the anchor in both outputs.
        std::ostringstream a;
        a << " font-family=\"" << svg_font_family_for(l.font_path)
          << "\" font-size=\"" << l.fontsize << "\" fill=\"" << rgb(l.color)
          << "\" fill-opacity=\"" << l.color.a << "\"";
        emit_text(o, "  ", l.x, l.y + middle_baseline_offset(l.font_path, l.fontsize, l.text), "middle", a.str(),
                  l.text, l.font_path, l.fontsize);
    }
}

// One layer of one plane, from plan_planes3d(), already in pixels. The affine
// <image> (orthographic heatmap) is exact: its unit square is mapped by
// matrix(...) alone.
static void emit_plane3d(std::ostringstream& o, const PlanePlanItem& p) {
    if (!p.warning.empty())
        o << "  <!-- sextant: " << xml_escape(p.warning) << " -->\n";

    if (p.form == PlanePlanItem::Form::Image) {
        if (p.rows <= 0 || p.cols <= 0 || p.rgba.empty()) return;
        const auto png_bytes = write_png_to_memory(p.cols, p.rows, p.rgba);
        const std::string b64 = base64_encode(png_bytes);
        o << "  <image x=\"0\" y=\"0\" width=\"1\" height=\"1\""
          << " preserveAspectRatio=\"none\""
          << " transform=\"matrix(" << p.matrix[0] << ' ' << p.matrix[1] << ' '
          <<                           p.matrix[2] << ' ' << p.matrix[3] << ' '
          <<                           p.matrix[4] << ' ' << p.matrix[5] << ")\"";
        if (p.alpha < 1.0f) o << " opacity=\"" << p.alpha << "\"";
        // Nearest-neighbour, as for the 2D <image>.
        o << " style=\"image-rendering: optimizeSpeed;"
          <<        " image-rendering: -moz-crisp-edges;"
          <<        " image-rendering: -webkit-optimize-contrast;"
          <<        " image-rendering: crisp-edges;"
          <<        " image-rendering: pixelated;\""
          << " xlink:href=\"data:image/png;base64," << b64 << "\"/>\n";
        return;
    }

    const bool any = !p.polys.empty() || !p.strokes.empty() || !p.marks.empty();
    if (!any) return;

    // Whole-plane opacity on the group; it multiplies per-primitive alpha.
    o << "  <g";
    if (p.alpha < 1.0f) o << " opacity=\"" << p.alpha << "\"";
    o << ">\n";

    // Polygons (perspective heatmap cells, filled areas), each stroked in its
    // own fill to hide antialiasing seams.
    for (const auto& poly : p.polys) {
        if (poly.xy.size() < 6) continue;
        const std::string fill = rgb(poly.fill);
        o << "    <polygon points=\"";
        for (std::size_t i = 0; i + 1 < poly.xy.size(); i += 2) {
            if (i) o << ' ';
            o << poly.xy[i] << ',' << poly.xy[i + 1];
        }
        o << "\" fill=\"" << fill << "\"";
        if (poly.fill.a < 1.0f) o << " fill-opacity=\"" << poly.fill.a << "\"";
        o << " stroke=\"" << fill << "\" stroke-width=\"0.5\"";
        if (poly.fill.a < 1.0f) o << " stroke-opacity=\"" << poly.fill.a << "\"";
        o << "/>\n";
    }

    // Strokes: one <polyline> per segment with its own width, so lines thin
    // with distance under perspective.
    for (const auto& st : p.strokes) {
        if (st.xy.size() < 4 || st.width <= 0.0f) continue;
        o << "    <polyline fill=\"none\" points=\"";
        for (std::size_t i = 0; i + 1 < st.xy.size(); i += 2) {
            if (i) o << ' ';
            o << st.xy[i] << ',' << st.xy[i + 1];
        }
        o << "\" stroke=\"" << rgb(st.color) << "\" stroke-opacity=\"" << st.color.a
          << "\" stroke-width=\"" << st.width << "\"/>\n";
    }

    // Markers: same shapes and pixel size as on a 2D axes.
    for (const auto& m : p.marks)
        emit_scatter_marker(o, m.x, m.y, m.size * 0.5f, rgb(m.color), m.color.a,
                            m.marker,
                            m.edge_width > 0.0f ? rgb(m.edge) : std::string(),
                            m.edge.a, m.edge_width, true);

    o << "  </g>\n";
}

// 3D cell: panes and grid clipped to the frame, then the frame and annotation
// unclipped (they sit in the margin).
static void emit_box3d(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    const Box3DPlan& plan = *d.box3d;

    o << "  <g clip-path=\"url(#plotArea" << idx << ")\">\n";
    if (!plan.panes.empty()) {
        emit_polys(o, plan.panes, rgb(d.box3d_style.pane_color), nullptr, 0.0f, true);
        emit_polys(o, plan.pane_edges, "none", &d.box3d_style.pane_edge_color, 1.0f, true);
    }
    emit_polys(o, plan.grid, "none", &d.box3d_grid_opts.color,
               d.box3d_grid_opts.linewidth, false);

    // The scene, over the panes, in `scene3d` order: Newell's algorithm from
    // the plan layer, with polygons split where no order exists. Splits are
    // done there because pieces must be re-projected.
    auto emit_ring = [&](const std::vector<float>& xy, bool filled,
                         const Color& fill, const Color& stroke, float stroke_width) {
        if (xy.size() < (filled ? 6u : 4u)) return;
        o << (filled ? "  <polygon points=\"" : "  <polyline points=\"");
        for (std::size_t i = 0; i + 1 < xy.size(); i += 2) {
            if (i) o << ' ';
            o << xy[i] << ',' << xy[i + 1];
        }
        if (filled) {
            o << "\" fill=\"" << rgb(fill) << "\"";
            if (fill.a < 1.0f) o << " fill-opacity=\"" << fill.a << "\"";
        } else {
            o << "\" fill=\"none\"";
        }
        if (stroke_width > 0.0f)
            o << " stroke=\"" << rgb(stroke) << "\" stroke-opacity=\"" << stroke.a
              << "\" stroke-width=\"" << stroke_width << "\"";
        o << "/>\n";
    };

    if (!d.scene3d_warning.empty())
        o << "  <!-- " << d.scene3d_warning << " -->\n";

    std::vector<char> plane_done(d.planes3d.size(), 0);
    std::size_t clip_seq = 0;
    for (const ScenePaint& s : d.scene3d) {
        switch (s.kind) {
            case ScenePaint::Kind::Bar: {
                if (s.index >= d.bars3d.size()) break;
                const Bar3DPolygon& p = d.bars3d[s.index];
                 // Filled face = closed ring; a translucent bar's bare edge is
                 // an open line and never split.
                emit_ring(s.xy.empty() ? p.xy : s.xy, p.filled,
                          p.fill, p.stroke, p.stroke_width);
                break;
            }
            case ScenePaint::Kind::Surface: {
                if (s.index >= d.surfaces3d.size()) break;
                const Surface3DPolygon& p = d.surfaces3d[s.index];
                 // Use `p.filled`: wireframe edges have no fill color.
                emit_ring(s.xy.empty() ? p.xy : s.xy, p.filled,
                          p.fill, p.stroke, p.stroke_width);
                break;
            }
            case ScenePaint::Kind::Mesh: {
                if (s.index >= d.meshes3d.size()) break;
                const SurfaceTriPolygon& p = d.meshes3d[s.index];
                const std::vector<float>& ring = s.xy.empty() ? p.xy : s.xy;
                 // Wireframe edge or flat mesh: the ordinary ring, `p.filled`.
                if (!p.filled || !p.colormapped || (p.gx == 0.0f && p.gy == 0.0f)) {
                    emit_ring(ring, p.filled, p.fill, p.stroke, p.stroke_width);
                    break;
                }
                if (ring.size() < 6) break;

                 // Re-derive the gradient axis from the ring being drawn, so
                 // split pieces get the right part of the ramp.
                float tmin = std::numeric_limits<float>::max();
                float tmax = -std::numeric_limits<float>::max();
                for (std::size_t i = 0; i + 1 < ring.size(); i += 2) {
                    const float t = ring[i] * p.gx + ring[i + 1] * p.gy;
                    tmin = std::min(tmin, t);
                    tmax = std::max(tmax, t);
                }
                 // No extent along the axis: nothing to ramp.
                if (!(tmax > tmin)) {
                    emit_ring(ring, true, p.fill, p.stroke, p.stroke_width);
                    break;
                }
                 // Any point on the axis works as origin; use the first vertex.
                const float ox = ring[0], oy = ring[1];
                const float t0 = ox * p.gx + oy * p.gy;
                const float x1 = ox + p.gx * (tmin - t0);
                const float y1 = oy + p.gy * (tmin - t0);
                const float x2 = ox + p.gx * (tmax - t0);
                const float y2 = oy + p.gy * (tmax - t0);

                 // Nine stops sampled from the colormap (a two-stop ramp would
                 // leave the map).
                constexpr int kStops = 9;
                const std::string gid = "meshGrad" + std::to_string(idx) + "_"
                                      + std::to_string(clip_seq++);
                o << "  <linearGradient id=\"" << gid
                  << "\" gradientUnits=\"userSpaceOnUse\""
                  << " x1=\"" << x1 << "\" y1=\"" << y1
                  << "\" x2=\"" << x2 << "\" y2=\"" << y2 << "\">\n";
                const uint8_t* lut = colormaps::get(p.cmap);
                for (int k = 0; k < kStops; ++k) {
                    const float f = static_cast<float>(k) / (kStops - 1);
                    const float sx = x1 + (x2 - x1) * f;
                    const float sy = y1 + (y2 - y1) * f;
                     // Value at that pixel via the projective form (exact along
                     // the axis under both cameras). See SurfaceTriPolygon.
                    const float num = p.na + p.nx * sx + p.ny * sy;
                    const float den = p.wa + p.wx * sx + p.wy * sy;
                    const float t = den != 0.0f ? num / den : 0.0f;
                    const int e = colormaps::index(t, p.cmap.is_cyclic());
                     // Look up, then shade (the shader's order).
                    const Color c{ lut[e * 4]     / 255.0f * p.shade,
                                   lut[e * 4 + 1] / 255.0f * p.shade,
                                   lut[e * 4 + 2] / 255.0f * p.shade, 1.0f };
                    o << "    <stop offset=\"" << f << "\" stop-color=\""
                      << rgb(c) << "\"/>\n";
                }
                o << "  </linearGradient>\n";

                o << "  <polygon points=\"";
                for (std::size_t i = 0; i + 1 < ring.size(); i += 2) {
                    if (i) o << ' ';
                    o << ring[i] << ',' << ring[i + 1];
                }
                o << "\" fill=\"url(#" << gid << ")\"";
                if (p.alpha < 1.0f) o << " fill-opacity=\"" << p.alpha << "\"";
                o << "/>\n";
                break;
            }
            case ScenePaint::Kind::ErrorBar: {
                if (s.index >= d.errbars3d.size()) break;
                const ErrorBar3DPolygon& p = d.errbars3d[s.index];
                const std::vector<float>& xy = s.xy.empty() ? p.xy : s.xy;
                if (p.filled) {
                    emit_ring(xy, true, p.color, p.color, 0.0f);
                    break;
                }
                 // Butt-ended, so block edges don't bulge past the corners.
                if (xy.size() < 4 || !(p.width > 0.0f)) break;
                o << "  <line x1=\"" << xy[0] << "\" y1=\"" << xy[1]
                  << "\" x2=\"" << xy[2] << "\" y2=\"" << xy[3]
                  << "\" stroke=\"" << rgb(p.color) << "\"";
                if (p.color.a < 1.0f) o << " stroke-opacity=\"" << p.color.a << "\"";
                o << " stroke-width=\"" << p.width << "\"/>\n";
                break;
            }
            case ScenePaint::Kind::Quiver: {
                if (s.index >= d.quivers3d.size()) break;
                const Quiver3DPolygon& p = d.quivers3d[s.index];
                emit_ring(s.xy.empty() ? p.xy : s.xy, true, p.color, p.color, 0.0f);
                break;
            }
            case ScenePaint::Kind::Scatter: {
                if (s.index >= d.markers3d.size()) break;
                const Scatter3DMarker& m = d.markers3d[s.index];
                 // Same emitter as 2D and the legends. `s.xy` is empty here.
                emit_scatter_marker(o, m.cx, m.cy, m.radius, rgb(m.color),
                                    m.color.a, m.marker,
                                    m.edge_width > 0.0f ? rgb(m.edge) : std::string(),
                                    m.edge.a, m.edge_width, true);
                break;
            }
            case ScenePaint::Kind::Line: {
                if (s.index >= d.lines3d.size()) break;
                const Line3DSegment& g = d.lines3d[s.index];
                if (g.width <= 0.0f) break;
                 // A split piece carries its sub-range of the parent's ramp,
                 // found from where `s.xy` falls along the parent.
                float ax = g.x0, ay = g.y0, bx = g.x1, by = g.y1;
                float t0 = 0.0f, t1 = 1.0f;
                if (s.xy.size() >= 4) {
                    ax = s.xy[0]; ay = s.xy[1];
                    bx = s.xy[2]; by = s.xy[3];
                    const float dx = g.x1 - g.x0, dy = g.y1 - g.y0;
                    const float len2 = dx * dx + dy * dy;
                    if (len2 > 1e-12f) {
                        t0 = ((ax - g.x0) * dx + (ay - g.y0) * dy) / len2;
                        t1 = ((bx - g.x0) * dx + (by - g.y0) * dy) / len2;
                        t0 = std::clamp(t0, 0.0f, 1.0f);
                        t1 = std::clamp(t1, 0.0f, 1.0f);
                    }
                }
                auto lerp = [](float u, float v, float t) { return u + (v - u) * t; };
                auto mix_color = [&](Color u, Color v, float t) {
                    return Color{ lerp(u.r, v.r, t), lerp(u.g, v.g, t),
                                  lerp(u.b, v.b, t), lerp(u.a, v.a, t) };
                };

                std::string paint;
                if (g.colormapped && g.v0 != g.v1) {
                     // Stops sampled from the colormap (eight intervals), not a
                     // two-stop RGB chord.
                    constexpr int kStops = 9;
                    const std::string gid = "lineGrad" + std::to_string(idx) + "_"
                                          + std::to_string(clip_seq++);
                    o << "  <linearGradient id=\"" << gid
                      << "\" gradientUnits=\"userSpaceOnUse\""
                      << " x1=\"" << ax << "\" y1=\"" << ay
                      << "\" x2=\"" << bx << "\" y2=\"" << by << "\">\n";
                    const uint8_t* lut = colormaps::get(g.cmap);
                    for (int k = 0; k < kStops; ++k) {
                        const float f = static_cast<float>(k) / (kStops - 1);
                         // Position along the parent segment (f when unsplit).
                        const float t = lerp(t0, t1, f);
                        const int e = colormaps::index(lerp(g.v0, g.v1, t), g.cmap.is_cyclic());
                         // Look up, then darken (the shader's order).
                        const float sh = lerp(g.k0, g.k1, t);
                        const Color c{ lut[e * 4]     / 255.0f * sh,
                                       lut[e * 4 + 1] / 255.0f * sh,
                                       lut[e * 4 + 2] / 255.0f * sh, 1.0f };
                        o << "    <stop offset=\"" << f << "\" stop-color=\""
                          << rgb(c) << "\"/>\n";
                    }
                    o << "  </linearGradient>\n";
                    paint = "url(#" + gid + ")";
                }

                const Color ca = mix_color(g.c0, g.c1, t0);
                const Color cb = mix_color(g.c0, g.c1, t1);
                const Color flat = mix_color(ca, cb, 0.5f);
                o << "  <line x1=\"" << ax << "\" y1=\"" << ay
                  << "\" x2=\"" << bx << "\" y2=\"" << by << "\" stroke=\""
                  << (paint.empty() ? rgb(flat) : paint) << "\"";
                if (flat.a < 1.0f) o << " stroke-opacity=\"" << flat.a << "\"";
                 // Round caps fill the wedge at bends. See Line3DSegment.
                o << " stroke-width=\"" << g.width
                  << "\" stroke-linecap=\"round\"/>\n";
                break;
            }
            case ScenePaint::Kind::Plane: {
                 // All layers of one plane in 2D painter order. Split pieces
                 // are clipped rather than cut (an <image> can't be cut).
                std::string clip;
                if (!s.xy.empty() && s.xy.size() >= 6) {
                    clip = "sceneClip" + std::to_string(idx) + "_"
                         + std::to_string(clip_seq++);
                    o << "  <clipPath id=\"" << clip << "\"><polygon points=\"";
                    for (std::size_t i = 0; i + 1 < s.xy.size(); i += 2) {
                        if (i) o << ' ';
                        o << s.xy[i] << ',' << s.xy[i + 1];
                    }
                    o << "\"/></clipPath>\n";
                    o << "  <g clip-path=\"url(#" << clip << ")\">\n";
                }
                for (std::size_t i = 0; i < d.planes3d.size(); ++i)
                    if (d.planes3d[i].plane == s.index) {
                        emit_plane3d(o, d.planes3d[i]);
                        plane_done[i] = 1;
                    }
                if (!clip.empty()) o << "  </g>\n";
                break;
            }
        }
    }
    // A plane whose quad degenerated (edge-on or near-clipped) is not in the
    // order, but its contents are still emitted.
    for (std::size_t i = 0; i < d.planes3d.size(); ++i)
        if (!plane_done[i]) emit_plane3d(o, d.planes3d[i]);
    o << "  </g>\n";

    // Plane contours: over the scene, under the box furniture.
    if (!d.contours3d.empty()) {
        o << "  <g clip-path=\"url(#plotArea" << idx << ")\">\n";
        emit_contours3d(o, d);
        o << "  </g>\n";
    }

    emit_polys(o, plan.axis_lines, "none", &d.axes_style.spine_color,
               d.axes_style.spine_linewidth, false);
    for (const TickRun& run : tick_runs(plan, d.axes_style))
        emit_polys(o, run.marks, "none", &run.color, run.linewidth, false);
    emit_box3d_labels(o, plan.tick_labels);
    emit_box3d_labels(o, plan.axis_titles);
}

// A 2D cell's layers: its own axes, then its twins.
static std::vector<const SvgAxesData*> layers_of(const SvgAxesData& d) {
    std::vector<const SvgAxesData*> out{&d};
    for (const SvgAxesData& t : d.twins) out.push_back(&t);
    return out;
}

// A polar cell (PolarAxes), as NvgRenderer: its disc or sector filled, the
// lines clipped to it, then rings, spokes, the outline and their labels.
static void emit_polar(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    const PolarPlan& plan = *d.layout.polar;
    const AxesStyle& st = d.axes_style;
    std::vector<float> x, y;
    const auto points = [&](const char* tag, const std::string& attrs) {
        o << "  <" << tag << attrs << " points=\"";
        for (std::size_t k = 0; k < x.size(); ++k) o << x[k] << "," << y[k] << " ";
        o << "\"/>\n";
    };
    if (d.axis_on && st.background.a > 0.0f) {
        plan.outline(x, y);
        points("polygon", background_fill(st.background));
    }

    o << "  <g clip-path=\"url(#plotArea" << idx << ")\">\n";
    emit_plots(o, d);
    o << "  </g>\n";

    if (d.axis_on) {
        if (d.grid_enabled) {
            const GridOptions& g = d.grid_opts;
            std::ostringstream a;
            a << " fill=\"none\" stroke=\"" << rgb(g.color) << "\" stroke-opacity=\"" << g.color.a
              << "\" stroke-width=\"" << g.linewidth << "\"" << dash_attr(g.linestyle);
            if (g.linestyle != LineStyle::None) {
                o << "  <g" << a.str() << ">\n";
                for (const PolarGridLine& r : plan.rings) {
                    if (r.value >= plan.map.rmax) continue;
                    plan.ring(r.value, x, y);
                    points("polyline", "");
                }
                for (const PolarGridLine& s : plan.spokes) {
                    const double t = s.value * kPolarDeg;
                    o << "  <line x1=\"" << plan.cx << "\" y1=\"" << plan.cy << "\" x2=\""
                      << plan.px(t, plan.map.rmax) << "\" y2=\"" << plan.py(t, plan.map.rmax) << "\"/>\n";
                }
                o << "  </g>\n";
            }
        }
        if (st.spine_linewidth > 0.0f && st.spine_color.a > 0.0f) {
            plan.outline(x, y);
            std::ostringstream a;
            a << " fill=\"none\" stroke=\"" << rgb(st.spine_color) << "\" stroke-opacity=\""
              << st.spine_color.a << "\" stroke-width=\"" << st.spine_linewidth << "\"";
            points("polygon", a.str());
        }
        const std::string& fp = st.font_path;
        const auto labels = [&](const std::vector<PolarGridLine>& v, const TickLook& k) {
            o << "  <g font-family=\"" << svg_font_family(st) << "\" font-size=\"" << k.label_fontsize
              << "\" fill=\"" << rgb(k.label_color) << "\">\n";
            for (const PolarGridLine& g : v)
                if (!g.label.empty())
                    emit_text(o, "    ", g.lx, g.ly + middle_baseline_offset(fp, k.label_fontsize, g.label),
                              "middle", "", g.label, fp, k.label_fontsize);
            o << "  </g>\n";
        };
        if (plan.tlabels && !plan.spokes.empty()) labels(plan.spokes, tick_look(st, TickAxis::X));
        if (plan.rlabels && !plan.rings.empty()) labels(plan.rings, tick_look(st, TickAxis::Y));
    }
    emit_titles(o, d);
    emit_legend(o, d, idx);
}

static void emit_one_axes(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    if (d.layout.polar) {
        emit_polar(o, d, idx);
        return;
    }
    if (d.box3d) {
        emit_box3d(o, d, idx);
        // The axes title uses the shared 2D path; axis titles are in the plan.
        emit_titles(o, d);
        emit_texts(o, d, idx);
        // Legend and colorbar, as in 2D.
        emit_legend(o, d, idx);
        emit_colorbar(o, d, idx);
        return;
    }

    if (d.axis_on && d.axes_style.background.a > 0.0f)
        o << "  <rect x=\"" << d.layout.frame.x << "\" y=\"" << d.layout.frame.y
          << "\" width=\"" << d.layout.frame.w << "\" height=\"" << d.layout.frame.h
          << "\"" << background_fill(d.axes_style.background) << "/>\n";

    o << "  <g clip-path=\"url(#plotArea" << idx << ")\">\n";
    emit_plots(o, d);
    o << "  </g>\n";

    // Contours in their own clipped group: above plots, below border/grid.
    // A twin's over its frame (the cell's), so the same clip.
    for (const SvgAxesData* l : layers_of(d))
        if (has_contours(*l)) {
            o << "  <g clip-path=\"url(#plotArea" << idx << ")\">\n";
            emit_contours(o, *l);
            o << "  </g>\n";
        }

    // A twin's frame is its host's: drawn once.
    if (d.axis_on) emit_spines(o, d);
    for (const SvgAxesData* l : layers_of(d)) {
        if (!l->axis_on) continue;
        emit_grid(o, *l);
        emit_ticks_and_labels(o, *l);
        // After the grid, as in NanoVG: an interior axis line goes over the grid.
        emit_interior_axes(o, *l);
    }
    for (const SvgAxesData* l : layers_of(d)) emit_titles(o, *l);
    for (const SvgAxesData* l : layers_of(d)) emit_texts(o, *l, idx);
    emit_legend(o, d, idx);
    emit_colorbar(o, d, idx);
}

std::string svg_document(const SvgFigureData& fd) {
    std::ostringstream o;

    o << "<svg xmlns=\"http://www.w3.org/2000/svg\""
      << " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
      << " width=\"" << fd.width << "\" height=\"" << fd.height << "\""
      << " viewBox=\"0 0 " << fd.width << " " << fd.height << "\">\n";

    emit_defs(o, fd.axes);

    // Figure background (nothing at alpha 0: the file stays transparent).
    if (fd.background.a > 0.0f)
        o << "  <rect width=\"" << fd.width << "\" height=\"" << fd.height
          << "\"" << background_fill(fd.background) << "/>\n";

    if (!fd.suptitle.empty()) {
        const auto& so  = fd.suptitle_opts;
        const float fsz = so.fontsize;
        // Band centre, then middle-of-line -> SVG baseline correction.
        const float band = suptitle_band_height(fd.suptitle, so);
        const float cy   = suptitle_center_y(band, so);
        const char* anchor = so.align == HAlign::Left  ? "start"
                           : so.align == HAlign::Right ? "end"
                                                       : "middle";
        std::ostringstream a;
        a << " font-family=\"" << svg_font_family_for(so.font_path) << "\""
          << " font-size=\"" << fsz << "\" fill=\"" << rgb(so.color)
          << "\" fill-opacity=\"" << so.color.a << "\"";
        emit_text(o, "  ", suptitle_anchor_x(static_cast<float>(fd.width), so),
                  cy + middle_baseline_offset(so.font_path, fsz, fd.suptitle), anchor, a.str(), fd.suptitle,
                  so.font_path, fsz);
    }

    for (std::size_t i = 0; i < fd.axes.size(); ++i)
        emit_one_axes(o, fd.axes[i], i);

    o << "</svg>\n";
    return std::move(o).str();
}

void write_svg(std::string_view path, const SvgFigureData& fd) {
    write_file(path, svg_document(fd), "write_svg");
}

void write_svg(std::string_view path, const SvgAxesData& d) {
    SvgFigureData fd;
    fd.width  = d.width;
    fd.height = d.height;
    fd.axes   = { d };
    write_svg(path, fd);
}

} // namespace sextant
