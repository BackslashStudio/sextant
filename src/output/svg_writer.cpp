#include "svg_writer.h"
#include "../colormaps.h"
#include "../font_discovery.h"
#include "../line_dash.h"
#include "../contour.h"
#include "png_writer.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <string>
#include <cmath>
#include <cerrno>
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

// SVG names a font-family for the viewer to resolve locally. AxesStyle::
// font_path is a file path, not a family name, so it is looked up against
// discover_system_fonts() (which reads the real family name out of each font's
// own 'name' table, e.g. "times.ttf" -> "Times New Roman") to get a CSS-usable
// name -- the same list the Controls panel's Font combo populates from, so an
// explicit selection round-trips. Falls back to the filename stem for a path
// that was not auto-detected. An unset path resolves to pick_default_font(),
// the same font the live NanoVG renderer defaults to, so headless SVG output
// never silently diverges from what is on screen. The legend, colorbar and
// suptitle route through here too, via their own font_path fields.
static std::string svg_font_family_for(const std::string& font_path) {
    static const char* kDefaultFamily = "sans-serif,Arial,Helvetica";
    if (!font_path.empty()) {
        // Path comparison (not string equality) since discover_system_fonts()
        // paths use the platform's native separators, which may not match a
        // path set by hand (e.g. "C:/Windows/Fonts/arial.ttf" vs the
        // "C:\Windows\Fonts\arial.ttf" the scan produced on Windows).
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

static std::string rgb(const Color& c) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)",
                  static_cast<int>(std::clamp(c.r, 0.f, 1.f) * 255.f),
                  static_cast<int>(std::clamp(c.g, 0.f, 1.f) * 255.f),
                  static_cast<int>(std::clamp(c.b, 0.f, 1.f) * 255.f));
    return buf;
}

// Emits ` stroke-dasharray="..."` when the style calls for one, and nothing at
// all otherwise. The run lengths come from the shared table in
// src/line_dash.h, so this cannot drift from what the window and PNG draw.
static std::string dash_attr(LineStyle ls) {
    const std::string da = svg_dasharray(ls);
    return da.empty() ? std::string{} : " stroke-dasharray=\"" + da + "\"";
}

// Standard base64 (RFC 4648), used to embed heatmap PNGs as data: URIs —
// SVG has no native raster-image primitive of its own.
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
    // LineStyle::None means "no stroke" for a data line; a grid line is a
    // line, so it means the same thing here rather than silently drawing
    // solid, which is what both writers used to do.
    if (g.linestyle == LineStyle::None) return;
    o << "  <g stroke=\"" << rgb(g.color) << "\" stroke-opacity=\"" << g.color.a
      << "\" stroke-width=\"" << g.linewidth << "\"";
    o << dash_attr(g.linestyle) << ">\n";
    for (const auto& t : d.layout.xticks) {
        float px = d.layout.tr.to_px(t.value);
        o << "    <line x1=\"" << px << "\" y1=\"" << d.layout.frame.y
          <<         "\" x2=\"" << px << "\" y2=\"" << d.layout.frame.y + d.layout.frame.h << "\"/>\n";
    }
    for (const auto& t : d.layout.yticks) {
        float py = d.layout.tr.to_py(t.value);
        o << "    <line x1=\"" << d.layout.frame.x      << "\" y1=\"" << py
          <<         "\" x2=\"" << d.layout.frame.x + d.layout.frame.w << "\" y2=\"" << py << "\"/>\n";
    }
    o << "  </g>\n";
}

static void emit_lines(std::ostringstream& o, const SvgAxesData& d) {
    for (const auto& lp : d.lines) {
        if (lp.x.empty() || lp.opts.linestyle == LineStyle::None) continue;

        o << "  <polyline fill=\"none\" stroke=\"" << rgb(lp.opts.color)
          << "\" stroke-opacity=\"" << lp.opts.alpha
          << "\" stroke-width=\"" << lp.opts.linewidth << "\""
          << dash_attr(lp.opts.linestyle)
          << " points=\"";
        for (std::size_t i = 0; i < lp.x.size(); ++i)
            o << d.layout.tr.to_px(lp.x[i]) << "," << d.layout.tr.to_py(lp.y[i]) << " ";
        o << "\"/>\n";
    }
}

// Error bars for all four kinds that carry them: a capped whisker as <line>
// elements plus a variance box as one <rect>.
//
// Geometry agrees with the raster path because the two agree on the
// definitions rather than on the code: `capsize` is a total length in pixels
// (the cap reaches half of it either side), `boxwidth` likewise, and
// `linewidth` is a stroke width here where build_whisker() has to inset a
// rectangle by half of it.
static void emit_whisker(std::ostringstream& o,
                         float x0, float y0, float x1, float y1,
                         float capsize, bool vertical,
                         const std::string& stroke, float lw)
{
    if (x0 == x1 && y0 == y1) return;
    auto seg = [&](float ax, float ay, float bx, float by) {
        o << "    <line x1=\"" << ax << "\" y1=\"" << ay
          << "\" x2=\"" << bx << "\" y2=\"" << by
          << "\" stroke=\"" << stroke << "\" stroke-width=\"" << lw << "\"/>\n";
    };
    seg(x0, y0, x1, y1);
    const float cap = std::max(capsize, 0.0f) * 0.5f;
    if (cap <= 0.0f) return;
    if (vertical) {
        seg(x0 - cap, y0, x0 + cap, y0);
        seg(x1 - cap, y1, x1 + cap, y1);
    } else {
        seg(x0, y0 - cap, x0, y0 + cap);
        seg(x1, y1 - cap, x1, y1 + cap);
    }
}

static void emit_error_bars_for(std::ostringstream& o, const SvgAxesData& d,
                                const CowVec<double>& xs, const CowVec<double>& ys,
                                const ErrorBarData& err, const ErrorBarOptions& style,
                                const Color& fallback)
{
    if (err.empty() || style.linewidth <= 0.0f) return;
    const auto& tr = d.layout.tr;
    const Color c = style.color.value_or(fallback);
    const std::string stroke = rgb(c);
    const float halfbox = std::max(style.boxwidth, 0.0f) * 0.5f;
    const std::size_t n = std::min(xs.size(), ys.size());

    for (std::size_t i = 0; i < n; ++i) {
        const float cx = tr.to_px(xs[i]);
        const float cy = tr.to_py(ys[i]);
        if (err.has_y_span())
            emit_whisker(o, cx, tr.to_py(err.y_lo(i, ys[i])),
                            cx, tr.to_py(err.y_hi(i, ys[i])),
                         style.capsize, true, stroke, style.linewidth);
        if (err.has_x_span())
            emit_whisker(o, tr.to_px(err.x_lo(i, xs[i])), cy,
                            tr.to_px(err.x_hi(i, xs[i])), cy,
                         style.capsize, false, stroke, style.linewidth);

        if (!err.has_y_box() && !err.has_x_box()) continue;
        float bx0, bx1, by0, by1;
        if (err.has_x_box()) {
            bx0 = tr.to_px(xs[i] - err.x_var(i));
            bx1 = tr.to_px(xs[i] + err.x_var(i));
        } else { bx0 = cx - halfbox; bx1 = cx + halfbox; }
        if (err.has_y_box()) {
            by0 = tr.to_py(ys[i] - err.y_var(i));
            by1 = tr.to_py(ys[i] + err.y_var(i));
        } else { by0 = cy - halfbox; by1 = cy + halfbox; }
        if (bx0 == bx1 || by0 == by1) continue;
        if (bx0 > bx1) std::swap(bx0, bx1);
        if (by0 > by1) std::swap(by0, by1);
        o << "    <rect x=\"" << bx0 << "\" y=\"" << by0
          << "\" width=\"" << (bx1 - bx0) << "\" height=\"" << (by1 - by0)
          << "\" fill=\"" << stroke << "\" fill-opacity=\"" << (c.a * style.box_alpha)
          << "\" stroke=\"" << stroke << "\" stroke-width=\"" << style.linewidth
          << "\"/>\n";
    }
}

static void emit_error_bars(std::ostringstream& o, const SvgAxesData& d) {
    for (const auto& lp : d.lines)
        emit_error_bars_for(o, d, lp.x, lp.y, lp.err, lp.opts.errorbar, lp.opts.color);
    // Heights, not the zero baseline: an error bar measures the bar's tip.
    for (const auto& bp : d.bars)
        emit_error_bars_for(o, d, bp.centers, bp.heights, bp.err,
                            bp.opts.errorbar, bp.opts.edgecolor);
    for (const auto& sp : d.scatters)
        emit_error_bars_for(o, d, sp.x, sp.y, sp.err, sp.opts.errorbar, sp.opts.color);
    // Scatter_z has no single color of its own, so black rather than a
    // per-point colormap value the bar cannot have — same rule as the raster.
    for (const auto& sp : d.scatter_z)
        emit_error_bars_for(o, d, sp.x, sp.y, sp.err, sp.opts.errorbar, Color::Black);
}

static void emit_scatter_marker(std::ostringstream& o,
                                float cx, float cy, float r,
                                const std::string& fill, float alpha,
                                MarkerStyle marker)
{
    switch (marker) {
        case MarkerStyle::Circle:
            o << "    <circle cx=\"" << cx << "\" cy=\"" << cy << "\" r=\"" << r
              << "\" fill=\"" << fill << "\" fill-opacity=\"" << alpha << "\"/>\n";
            break;
        case MarkerStyle::Square:
            o << "    <rect x=\"" << cx - r << "\" y=\"" << cy - r
              << "\" width=\"" << 2*r << "\" height=\"" << 2*r
              << "\" fill=\"" << fill << "\" fill-opacity=\"" << alpha << "\"/>\n";
            break;
        case MarkerStyle::Triangle:
            // Upward-pointing triangle
            o << "    <polygon points=\""
              << cx << "," << cy - r << " "
              << cx + r << "," << cy + r << " "
              << cx - r << "," << cy + r
              << "\" fill=\"" << fill << "\" fill-opacity=\"" << alpha << "\"/>\n";
            break;
        case MarkerStyle::Diamond:
            o << "    <polygon points=\""
              << cx << "," << cy - r << " "
              << cx + r << "," << cy << " "
              << cx << "," << cy + r << " "
              << cx - r << "," << cy
              << "\" fill=\"" << fill << "\" fill-opacity=\"" << alpha << "\"/>\n";
            break;
        case MarkerStyle::Cross: {
            float s = r * 0.707f;  // diagonal half-length
            o << "    <line x1=\"" << cx-s << "\" y1=\"" << cy-s
              <<        "\" x2=\"" << cx+s << "\" y2=\"" << cy+s
              << "\" stroke=\"" << fill << "\" stroke-opacity=\"" << alpha
              << "\" stroke-width=\"" << std::max(1.0f, r*0.35f) << "\"/>\n";
            o << "    <line x1=\"" << cx+s << "\" y1=\"" << cy-s
              <<        "\" x2=\"" << cx-s << "\" y2=\"" << cy+s
              << "\" stroke=\"" << fill << "\" stroke-opacity=\"" << alpha
              << "\" stroke-width=\"" << std::max(1.0f, r*0.35f) << "\"/>\n";
            break;
        }
        case MarkerStyle::Plus:
            o << "    <line x1=\"" << cx << "\" y1=\"" << cy - r
              <<        "\" x2=\"" << cx << "\" y2=\"" << cy + r
              << "\" stroke=\"" << fill << "\" stroke-opacity=\"" << alpha
              << "\" stroke-width=\"" << std::max(1.0f, r*0.35f) << "\"/>\n";
            o << "    <line x1=\"" << cx - r << "\" y1=\"" << cy
              <<        "\" x2=\"" << cx + r << "\" y2=\"" << cy
              << "\" stroke=\"" << fill << "\" stroke-opacity=\"" << alpha
              << "\" stroke-width=\"" << std::max(1.0f, r*0.35f) << "\"/>\n";
            break;
        default: break;
    }
}

static void emit_scatter(std::ostringstream& o, const SvgAxesData& d) {
    for (const auto& sp : d.scatters) {
        if (sp.x.empty()) continue;
        const std::string fill = rgb(sp.opts.color);
        const float r = sp.opts.size * 0.5f;
        for (std::size_t i = 0; i < sp.x.size(); ++i)
            emit_scatter_marker(o, d.layout.tr.to_px(sp.x[i]), d.layout.tr.to_py(sp.y[i]),
                                r, fill, sp.opts.alpha, sp.opts.marker);
    }
}

// Continuous-color scatter: each point gets its own fill, computed the same
// way as draw_scatter_z's CPU colormap-LUT lookup (data_renderer.cpp) — no
// colorbar box here, only the SVG-writer-side pre-existing limitation (see
// SvgAxesData's comment); the per-point colors themselves need no image.
static void emit_scatter_z(std::ostringstream& o, const SvgAxesData& d) {
    for (const auto& sp : d.scatter_z) {
        if (sp.x.empty()) continue;
        const uint8_t* lut   = colormaps::get(sp.opts.cmap);
        const float    vmin  = sp.opts.vmin, vrange = sp.opts.vmax - sp.opts.vmin;
        const float    r     = sp.opts.size * 0.5f;
        for (std::size_t i = 0; i < sp.x.size(); ++i) {
            float t = (vrange != 0.0f)
                    ? static_cast<float>((sp.z[i] - vmin) / vrange) : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            const uint8_t* c = &lut[static_cast<int>(t * 255.0f) * 4];
            char buf[32];
            std::snprintf(buf, sizeof(buf), "rgb(%d,%d,%d)", c[0], c[1], c[2]);
            emit_scatter_marker(o, d.layout.tr.to_px(sp.x[i]), d.layout.tr.to_py(sp.y[i]),
                                r, buf, sp.opts.alpha, sp.opts.marker);
        }
    }
}

// Heatmap: the same CPU colormap-LUT conversion DataRenderer::draw_heatmap
// does, PNG-encoded and embedded as a base64 data: URI <image>, since SVG has
// no native raster primitive. PNG rows are top-down and hp.data's row 0 is the
// image's top row only for origin="upper", so the same flip draw_heatmap
// applies before its texture upload is replicated here.
static void emit_heatmap(std::ostringstream& o, const SvgAxesData& d) {
    for (const auto& hp : d.heatmaps) {
        if (hp.rows <= 0 || hp.cols <= 0) continue;

        const uint8_t* lut = colormaps::get(hp.opts.cmap);
        const int n = hp.rows * hp.cols;
        std::vector<uint8_t> rgba(static_cast<std::size_t>(n) * 4);
        const float vmin = hp.opts.vmin, vrange = hp.opts.vmax - hp.opts.vmin;
        for (int i = 0; i < n; ++i) {
            float t = (vrange != 0.0f) ? (hp.data[i] - vmin) / vrange : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            const int idx = static_cast<int>(t * 255.0f);
            std::memcpy(&rgba[static_cast<std::size_t>(i) * 4], &lut[idx * 4], 4);
        }

        const bool flip = (hp.opts.origin == "lower");
        std::vector<uint8_t> flipped;
        if (flip) {
            flipped.resize(rgba.size());
            const std::size_t row_bytes = static_cast<std::size_t>(hp.cols) * 4;
            for (int r = 0; r < hp.rows; ++r)
                std::memcpy(&flipped[static_cast<std::size_t>(r) * row_bytes],
                           &rgba[static_cast<std::size_t>(hp.rows - 1 - r) * row_bytes],
                           row_bytes);
        }
        const std::vector<uint8_t>& png_src = flip ? flipped : rgba;

        const auto png_bytes = write_png_to_memory(hp.cols, hp.rows, png_src);
        const std::string b64 = base64_encode(png_bytes);

        // Same quad the GL texture upload maps its own quad to (see
        // DataRenderer::draw_heatmap's `verts`): [0,cols]x[0,rows] in data
        // space, mapped through the same CoordTransform.
        const float x0 = d.layout.tr.to_px(0.0), x1 = d.layout.tr.to_px(static_cast<double>(hp.cols));
        const float y0 = d.layout.tr.to_py(0.0), y1 = d.layout.tr.to_py(static_cast<double>(hp.rows));
        const float ix = std::min(x0, x1), iw = std::abs(x1 - x0);
        const float iy = std::min(y0, y1), ih = std::abs(y1 - y0);

        // DataRenderer::draw_heatmap filters its texture with GL_NEAREST,
        // while an SVG <image> defaults to smooth bilinear upscaling, which
        // looks blurry for a small heatmap blown up. The `style` fallback
        // chain is the standard cross-renderer trick: each consumer keeps the
        // last declaration whose value it understands and ignores the rest.
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

static void emit_bars(std::ostringstream& o, const SvgAxesData& d) {
    const float py0 = d.layout.tr.to_py(0.0);
    for (const auto& bp : d.bars) {
        const float half = static_cast<float>(bp.bar_width * 0.5);
        const std::string fill  = rgb(bp.opts.color);
        const std::string edge  = rgb(bp.opts.edgecolor);
        for (std::size_t i = 0; i < bp.centers.size(); ++i) {
            const float xl = d.layout.tr.to_px(bp.centers[i] - half);
            const float xr = d.layout.tr.to_px(bp.centers[i] + half);
            const float yt = d.layout.tr.to_py(bp.heights[i]);
            const float ytop  = std::min(py0, yt);
            const float ybot  = std::max(py0, yt);
            const float bh    = ybot - ytop;
            const float bw    = xr - xl;
            o << "    <rect x=\"" << xl << "\" y=\"" << ytop
              << "\" width=\"" << bw << "\" height=\"" << bh
              << "\" fill=\"" << fill << "\" fill-opacity=\"" << bp.opts.alpha << "\"";
            if (bp.opts.linewidth > 0.f)
                o << " stroke=\"" << edge << "\" stroke-width=\"" << bp.opts.linewidth << "\"";
            o << "/>\n";
        }
    }
}

// Contour lines over a heatmap, from the same trace_contours()/plan_contours()
// the window path runs -- this writer derives no contour geometry of its own.
// There is no ContourCache here: an export traces once and exits.
//
// Emitted into a clipped group of its own *after* the data group, matching
// where render_frame() calls draw_contours(): above every plot, below the
// border and grid.
static bool has_contours(const SvgAxesData& d) {
    for (const auto& hp : d.heatmaps)
        if (!hp.opts.contours.empty() && hp.rows > 0 && hp.cols > 0) return true;
    return false;
}

static void emit_contours(std::ostringstream& o, const SvgAxesData& d) {
    for (const auto& hp : d.heatmaps) {
        if (hp.opts.contours.empty() || hp.rows <= 0 || hp.cols <= 0) continue;

        const ContourSet  set = trace_contours(hp);
        const ContourDraw cd  = plan_contours(set, d.layout.tr, hp.opts,
                                              d.axes_style.font_path);
        const std::string stroke = rgb(hp.opts.contour_color);

        for (const auto& run : cd.runs) {
            o << "    <polyline fill=\"none\" stroke=\"" << stroke
              << "\" stroke-opacity=\"" << hp.opts.contour_color.a
              << "\" stroke-width=\"" << hp.opts.contour_linewidth
              << "\" points=\"";
            for (std::size_t k = 0; k < run.px.size(); ++k) {
                if (k) o << " ";
                o << run.px[k] << "," << run.py[k];
            }
            o << "\"/>\n";
        }

        if (cd.labels.empty()) continue;
        // The rotation is applied about the anchor via transform, and the
        // baseline offset inside it, so the text is vertically centred on the
        // line exactly as NVG_ALIGN_MIDDLE centres it in the window.
        const float base = middle_baseline_offset(d.axes_style.font_path,
                                                  hp.opts.contour_fontsize);
        for (const auto& lb : cd.labels) {
            o << "    <text x=\"0\" y=\"" << base
              << "\" transform=\"translate(" << lb.x << "," << lb.y << ") rotate("
              << lb.angle * 57.2957795f << ")\" text-anchor=\"middle\""
              << " font-family=\"" << svg_font_family(d.axes_style) << "\""
              << " font-size=\"" << hp.opts.contour_fontsize << "\""
              << " fill=\"" << stroke << "\" fill-opacity=\"" << hp.opts.contour_color.a
              << "\">" << xml_escape(lb.text) << "</text>\n";
        }
    }
}

static void emit_ticks_and_labels(std::ostringstream& o, const SvgAxesData& d) {
    const float tick_len = d.axes_style.tick_length;
    const float fsz      = d.axes_style.label_fontsize;

    o << "  <g stroke=\"" << rgb(d.axes_style.tick_color) << "\" stroke-opacity=\""
      << d.axes_style.tick_color.a << "\" stroke-width=\"" << d.axes_style.tick_linewidth << "\">\n";
    for (const auto& t : d.layout.xticks) {
        float px = d.layout.tr.to_px(t.value);
        o << "    <line x1=\"" << px << "\" y1=\"" << d.layout.frame.y + d.layout.frame.h
          <<        "\" x2=\"" << px << "\" y2=\"" << d.layout.frame.y + d.layout.frame.h + tick_len << "\"/>\n";
    }
    for (const auto& t : d.layout.yticks) {
        float py = d.layout.tr.to_py(t.value);
        o << "    <line x1=\"" << d.layout.frame.x - tick_len << "\" y1=\"" << py
          <<        "\" x2=\"" << d.layout.frame.x << "\" y2=\"" << py << "\"/>\n";
    }
    o << "  </g>\n";

    o << "  <g font-family=\"" << svg_font_family(d.axes_style) << "\" font-size=\"" << fsz
      << "\" fill=\"" << rgb(d.axes_style.label_color) << "\">\n";
    // NanoVG hangs the x labels from their top edge and centres the y labels
    // on the tick; SVG wants a baseline for both. The offsets come from the
    // font's own metrics rather than the "ascent == font size" and 0.35 *
    // font size approximations used here before, which put the two outputs a
    // couple of pixels apart vertically at every tick.
    const float x_base = d.layout.xlabel_top + top_baseline_offset(d.axes_style.font_path, fsz);
    const float y_base = middle_baseline_offset(d.axes_style.font_path, fsz);
    for (const auto& t : d.layout.xticks) {
        float px = d.layout.tr.to_px(t.value);
        o << "    <text x=\"" << px << "\" y=\"" << x_base
          << "\" text-anchor=\"middle\">" << xml_escape(t.label) << "</text>\n";
    }
    for (const auto& t : d.layout.yticks) {
        float py = d.layout.tr.to_py(t.value);
        o << "    <text x=\"" << d.layout.ylabel_right
          << "\" y=\"" << py + y_base
          << "\" text-anchor=\"end\">" << xml_escape(t.label) << "</text>\n";
    }
    o << "  </g>\n";
}

// Anchors come from the layout; only the centre-to-baseline conversion is
// this writer's own. The three offsets that used to live here (-20, +42, -45
// px) were a transcription of NanoVG's, and the pair had to be edited
// together.
static void emit_titles(std::ostringstream& o, const SvgAxesData& d) {
    const std::string& fp = d.axes_style.font_path;

    if (!d.title.empty()) {
        const float fsz = d.axes_style.title_fontsize;
        o << "  <text x=\"" << d.layout.title_x
          << "\" y=\"" << d.layout.title_y + middle_baseline_offset(fp, fsz)
          << "\" text-anchor=\"middle\" font-family=\"" << svg_font_family(d.axes_style) << "\""
          << " font-size=\"" << fsz << "\" fill=\"" << rgb(d.axes_style.title_color) << "\">"
          << xml_escape(d.title) << "</text>\n";
    }
    if (!d.xtitle.empty()) {
        const float fsz = d.axes_style.xtitle_fontsize;
        o << "  <text x=\"" << d.layout.xtitle_x
          << "\" y=\"" << d.layout.xtitle_y + middle_baseline_offset(fp, fsz)
          << "\" text-anchor=\"middle\" font-family=\"" << svg_font_family(d.axes_style) << "\""
          << " font-size=\"" << fsz << "\" fill=\"" << rgb(d.axes_style.xtitle_color) << "\">"
          << xml_escape(d.xtitle) << "</text>\n";
    }
    if (!d.ytitle.empty()) {
        const float fsz = d.axes_style.ytitle_fontsize;
        const float cx = d.layout.ytitle_x;
        const float cy = d.layout.ytitle_y;
        // The baseline shift is applied in the *rotated* frame, exactly as
        // NanoVG applies its own after nvgRotate: under rotate(-90) a local +y
        // maps to a global +x, so writing it into the y attribute rather than
        // into cx is what makes the two agree.
        o << "  <text x=\"0\" y=\"" << middle_baseline_offset(fp, fsz) << "\""
          << " text-anchor=\"middle\" font-family=\"" << svg_font_family(d.axes_style) << "\""
          << " font-size=\"" << fsz << "\" fill=\"" << rgb(d.axes_style.ytitle_color) << "\""
          << " transform=\"rotate(-90," << cx << "," << cy << ")"
          <<   " translate(" << cx << "," << cy << ")\">"
          << xml_escape(d.ytitle) << "</text>\n";
    }
}

// The entry list and the box are the layout's, not this writer's — see
// collect_legend_entries() in figure_layout.h, which replaced a near-copy of
// this file's own collector.
static void emit_legend(std::ostringstream& o, const SvgAxesData& d) {
    if (!d.layout.has_legend()) return;

    const auto& entries = d.layout.legend_entries;
    const auto& opts  = d.legend_opts;
    const float fsz   = opts.fontsize;
    const float row_h = legend_row_height(fsz);
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

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        const float cy  = by + kLegendPad + row_h * static_cast<float>(i) + row_h * 0.5f;
        const float sx0 = bx + kLegendPad, sx1 = sx0 + kLegendSwatchW;
        const std::string fill = rgb(e.color);

        if (e.kind == LegendKind::Line) {
            o << "    <line x1=\"" << sx0 << "\" y1=\"" << cy
              << "\" x2=\"" << sx1 << "\" y2=\"" << cy
              << "\" stroke=\"" << fill << "\" stroke-width=\"2\""
              << dash_attr(e.style) << "/>\n";
        } else if (e.kind == LegendKind::Marker) {
            o << "    <circle cx=\"" << (sx0 + sx1) * 0.5f << "\" cy=\"" << cy
              << "\" r=\"4.5\" fill=\"" << fill << "\"/>\n";
        } else {
            o << "    <rect x=\"" << sx0 << "\" y=\"" << cy - 5.f
              << "\" width=\"" << kLegendSwatchW << "\" height=\"10\" fill=\"" << fill << "\"/>\n";
        }

        o << "    <text x=\"" << sx1 + kLegendGap
          << "\" y=\"" << cy + middle_baseline_offset(opts.font_path, fsz)
          << "\" font-family=\"" << svg_font_family_for(opts.font_path)
          << "\" font-size=\"" << fsz
          << "\" fill=\"" << rgb(opts.text_color)
          << "\" fill-opacity=\"" << opts.text_color.a << "\">"
          << xml_escape(e.label) << "</text>\n";
    }
}

// Gradient bar + border + vmin/vmax labels. The <linearGradient> itself is
// emitted by emit_defs(), since SVG gradients must be defined once and
// referenced by id; this only draws the rect that references it, matching
// NvgRenderer::draw_colorbar's layout.
static void emit_colorbar(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    if (!d.layout.has_colorbar()) return;
    const auto& r = d.layout.colorbar;

    const auto& cb = d.colorbar_opts;
    o << "  <rect x=\"" << r.x << "\" y=\"" << r.y
      << "\" width=\"" << r.w << "\" height=\"" << r.h
      << "\" fill=\"url(#colorbarGrad" << idx << ")\""
      << " stroke=\"" << rgb(cb.border_color)
      << "\" stroke-opacity=\"" << cb.border_color.a << "\""
      << " stroke-width=\"" << cb.border_linewidth << "\"/>\n";

    // NanoVG centres these on the bar's top and bottom edges; the baseline
    // shift was a literal 3.5 px here, i.e. right only for the default 10 px
    // font. The gap to the bar is the same constant the layout reserved room
    // for, so the numbers cannot overflow the space measured for them.
    const float base = middle_baseline_offset(cb.font_path, cb.fontsize);
    const float tx   = r.x + r.w + kColorbarLabelGap;

    char buf[32];
    o << "  <g font-family=\"" << svg_font_family_for(cb.font_path)
      << "\" font-size=\"" << cb.fontsize
      << "\" fill=\"" << rgb(cb.text_color)
      << "\" fill-opacity=\"" << cb.text_color.a << "\">\n";
    std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(d.layout.colorbar_vmax));
    o << "    <text x=\"" << tx << "\" y=\"" << r.y + base
      << "\">" << xml_escape(buf) << "</text>\n";
    std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(d.layout.colorbar_vmin));
    o << "    <text x=\"" << tx << "\" y=\"" << r.y + r.h + base
      << "\">" << xml_escape(buf) << "</text>\n";
    o << "  </g>\n";
}

// -------------------------------------------------------------------------
// Public entry points
// -------------------------------------------------------------------------
static void emit_defs(std::ostringstream& o, const std::vector<SvgAxesData>& axes) {
    o << "<defs>\n";
    for (std::size_t i = 0; i < axes.size(); ++i) {
        const auto& d = axes[i];
        o << "  <clipPath id=\"plotArea" << i << "\">"
          << "<rect x=\"" << d.layout.frame.x << "\" y=\"" << d.layout.frame.y
          << "\" width=\"" << d.layout.frame.w << "\" height=\"" << d.layout.frame.h << "\"/>"
          << "</clipPath>\n";

        if (!d.layout.has_colorbar()) continue;
        // Vertical gradient: SVG y increases downward, same as pixel space
        // here, so offset 0% (top) = vmax and 100% (bottom) = vmin, matching
        // draw_colorbar's NanoVG image. All 256 LUT entries become stops, for
        // exact fidelity with the raster path's gradient curve -- viridis is
        // not perceptually linear between its endpoints.
        const uint8_t* lut = colormaps::get(d.layout.colorbar_cmap);
        o << "  <linearGradient id=\"colorbarGrad" << i << "\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">\n";
        for (int s = 0; s < 256; ++s) {
            const uint8_t* c = &lut[(255 - s) * 4];
            o << "    <stop offset=\"" << (s / 255.0f * 100.0f) << "%\" stop-color=\"rgb("
              << static_cast<int>(c[0]) << "," << static_cast<int>(c[1]) << "," << static_cast<int>(c[2])
              << ")\"/>\n";
        }
        o << "  </linearGradient>\n";
    }
    o << "</defs>\n";
}

// One axes' worth of markup. The clip id is per-axes so multiple axes in one
// SVG do not collide.
//
// Emission order is painter's order and mirrors render_frame()'s three
// passes: background, data, then border followed by grid and ticks. The grid
// is emitted *after* the data group so it draws on top, as it does in the
// window and in PNG.
static void emit_one_axes(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    o << "  <rect x=\"" << d.layout.frame.x << "\" y=\"" << d.layout.frame.y
      << "\" width=\"" << d.layout.frame.w << "\" height=\"" << d.layout.frame.h
      << "\" fill=\"white\"/>\n";

    o << "  <g clip-path=\"url(#plotArea" << idx << ")\">\n";
    emit_heatmap(o, d);
    emit_bars(o, d);
    emit_lines(o, d);
    emit_error_bars(o, d);
    emit_scatter(o, d);
    emit_scatter_z(o, d);
    o << "  </g>\n";

    // Its own clipped group, so contours land above every plot and
    // below the border/grid — the slot render_frame() draws them in.
    if (has_contours(d)) {
        o << "  <g clip-path=\"url(#plotArea" << idx << ")\">\n";
        emit_contours(o, d);
        o << "  </g>\n";
    }

    o << "  <rect x=\"" << d.layout.frame.x << "\" y=\"" << d.layout.frame.y
      << "\" width=\"" << d.layout.frame.w << "\" height=\"" << d.layout.frame.h
      << "\" fill=\"none\" stroke=\"" << rgb(d.axes_style.spine_color)
      << "\" stroke-opacity=\"" << d.axes_style.spine_color.a
      << "\" stroke-width=\"" << d.axes_style.spine_linewidth << "\"/>\n";

    emit_grid(o, d);
    emit_ticks_and_labels(o, d);
    emit_titles(o, d);
    emit_legend(o, d);
    emit_colorbar(o, d, idx);
}

void write_svg(std::string_view path, const SvgFigureData& fd) {
    std::ostringstream o;

    o << "<svg xmlns=\"http://www.w3.org/2000/svg\""
      << " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
      << " width=\"" << fd.width << "\" height=\"" << fd.height << "\""
      << " viewBox=\"0 0 " << fd.width << " " << fd.height << "\">\n";

    emit_defs(o, fd.axes);

    // Figure background
    o << "  <rect width=\"" << fd.width << "\" height=\"" << fd.height
      << "\" fill=\"#ededed\"/>\n";

    if (!fd.suptitle.empty()) {
        const auto& so  = fd.suptitle_opts;
        const float fsz = so.fontsize;
        // Band centre, then the middle-of-line -> SVG-baseline correction
        // (NanoVG uses ALIGN_MIDDLE; SVG's y is the baseline). The band
        // centre used to be a literal 18.f here — a third, disguised copy
        // of the 36 px reserved band.
        const float band = suptitle_band_height(fd.suptitle, so);
        const float cy   = suptitle_center_y(band, so);
        const char* anchor = so.align == HAlign::Left  ? "start"
                           : so.align == HAlign::Right ? "end"
                                                       : "middle";
        o << "  <text x=\"" << suptitle_anchor_x(static_cast<float>(fd.width), so)
          << "\" y=\"" << cy + middle_baseline_offset(so.font_path, fsz)
          << "\" text-anchor=\"" << anchor << "\" font-family=\"" << svg_font_family_for(so.font_path) << "\""
          << " font-size=\"" << fsz << "\" fill=\"" << rgb(so.color)
          << "\" fill-opacity=\"" << so.color.a << "\">"
          << xml_escape(fd.suptitle) << "</text>\n";
    }

    for (std::size_t i = 0; i < fd.axes.size(); ++i)
        emit_one_axes(o, fd.axes[i], i);

    o << "</svg>\n";

    std::ofstream f{std::string(path)};
    if (!f) throw std::system_error(errno, std::system_category(),
                                    "write_svg: cannot open '" + std::string(path) + "'");
    f << o.str();
}

void write_svg(std::string_view path, const SvgAxesData& d) {
    SvgFigureData fd;
    fd.width  = d.width;
    fd.height = d.height;
    fd.axes   = { d };
    write_svg(path, fd);
}

} // namespace sextant
