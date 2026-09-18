#include "svg_writer.h"
#include "../colormaps.h"
#include "../font_discovery.h"
#include "../line_dash.h"
#include "../contour.h"
#include "../renderer/marker_shape.h"
#include "../renderer/error_bar_shape.h"
#include "png_writer.h"
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

// SVG names a font-family for the viewer to resolve locally. AxesStyle::
// font_path is a file path, not a family name, so it is looked up against
// discover_system_fonts() (which reads the real family name out of each font's
// own 'name' table, e.g. "times.ttf" -> "Times New Roman") to get a CSS-usable
// name -- the same list the Cosmetic panel's Font combo populates from, so an
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

        // A closed path is a <polygon>, not a <polyline> with its first point
        // written again: the element is what closes it, so the seam gets a
        // join rather than two butt caps crossing, and SVG's own default
        // stroke-miterlimit is 4 -- the same clamp the stroke shader applies.
        // `fill="none"` keeps it a path; a polygon's fill is the one thing
        // that would otherwise differ from the raster picture.
        const char* tag = lp.opts.loop && lp.x.size() >= 2 ? "polygon" : "polyline";
        o << "  <" << tag << " fill=\"none\" stroke=\"" << rgb(lp.opts.color)
          << "\" stroke-opacity=\"" << lp.opts.alpha
          << "\" stroke-width=\"" << lp.opts.linewidth << "\""
          << dash_attr(lp.opts.linestyle)
          << " points=\"";
        for (std::size_t i = 0; i < lp.x.size(); ++i)
            o << d.layout.tr.to_px(lp.x[i]) << "," << d.layout.tr.to_py(lp.y[i]) << " ";
        o << "\"/>\n";
    }
}

// Error bars for all four kinds that carry them: a whisker and its caps as
// <line> elements, whisker_segments() being the definition the raster path
// draws too, plus a box as one <rect>.
//
// Geometry agrees with the raster path because the two agree on the
// definitions rather than on the code: `capsize` and `boxwidth` are total
// lengths in pixels, and `linewidth` is a stroke width here where
// push_px_segment() has to offset by half of it.
static void emit_whisker(std::ostringstream& o, float cx, float cy,
                         float lo, float hi, bool vertical,
                         const ErrorBarOptions& style, const std::string& stroke)
{
    whisker_segments(cx, cy, lo, hi, vertical, 1.0, 1.0, style.capsize, style.capstyle,
                     [&](double ax, double ay, double bx, double by) {
        o << "    <line x1=\"" << static_cast<float>(ax) << "\" y1=\"" << static_cast<float>(ay)
          << "\" x2=\"" << static_cast<float>(bx) << "\" y2=\"" << static_cast<float>(by)
          << "\" stroke=\"" << stroke << "\" stroke-width=\"" << style.linewidth << "\"/>\n";
    });
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
        if (err.has_y_cap()) {
            const ErrOffsets e = err.y_cap(i);
            emit_whisker(o, cx, cy, tr.to_py(ys[i] + -e.lo), tr.to_py(ys[i] + e.hi),
                         true, style, stroke);
        }
        if (err.has_x_cap()) {
            const ErrOffsets e = err.x_cap(i);
            emit_whisker(o, cx, cy, tr.to_px(xs[i] + -e.lo), tr.to_px(xs[i] + e.hi),
                         false, style, stroke);
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

// The shapes come from marker_shape(), so a marker in the picture, a marker in
// this file's legend and a marker in the window's legend are one definition.
// Each form still emits the SVG element it *is* -- a disc is a <circle> and a
// square a <rect>, not four polygon points -- which is both smaller output and
// why routing this through the shared shape changed no byte of any existing
// file.
// `stroke` empty means no outline, which is every data marker and every legend
// key but scatter_z's. A Strokes form has no interior, so an outlined one is
// drawn *in* the stroke colour rather than filled and then outlined.
static void emit_scatter_marker(std::ostringstream& o,
                                float cx, float cy, float r,
                                const std::string& fill, float alpha,
                                MarkerStyle marker,
                                const std::string& stroke = {})
{
    const MarkerShape s = marker_shape(marker, cx, cy, r);
    const std::string edge = stroke.empty() ? std::string()
        : " stroke=\"" + stroke + "\" stroke-width=\"1\"";
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
        case MarkerShape::Form::Strokes: {
            const std::string& c = stroke.empty() ? fill : stroke;
            for (int i = 0; i + 1 < s.count; i += 2)
                o << "    <line x1=\"" << s.pts[i][0] << "\" y1=\"" << s.pts[i][1]
                  <<        "\" x2=\"" << s.pts[i+1][0] << "\" y2=\"" << s.pts[i+1][1]
                  << "\" stroke=\"" << c << "\" stroke-opacity=\"" << alpha
                  << "\" stroke-width=\"" << s.width << "\"/>\n";
            break;
        }
        case MarkerShape::Form::None:
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
// applies before its texture upload is replicated here -- plus, for a
// reversed range, the mirroring the GL quad gets from its own corners.
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

        // An <image> is placed by a positive-extent box, so unlike the GL
        // quad it cannot express a reversed range by folding its own corners
        // -- the mirroring has to happen in the pixels instead. Vertically
        // that composes with the origin flip (a reversed yrange cancels it);
        // horizontally it is a column reversal the upright case never needs.
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

        // Same quad the GL texture upload maps its own quad to (see
        // DataRenderer::draw_heatmap's `verts`): the plot's own xrange x
        // yrange in data space, mapped through the same CoordTransform.
        const float x0 = d.layout.tr.to_px(hp.xrange.lo), x1 = d.layout.tr.to_px(hp.xrange.hi);
        const float y0 = d.layout.tr.to_py(hp.yrange.lo), y1 = d.layout.tr.to_py(hp.yrange.hi);
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

// One planned contour set. Split out when planes gained contours: a plane's
// arrive already planned in `d.contours3d`, and the geometry is pixel-space
// either way, so the emission is one function rather than two.
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
        // The rotation is applied about the anchor via transform, and the
        // baseline offset inside it, so the text is vertically centred on the
        // line exactly as NVG_ALIGN_MIDDLE centres it in the window.
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

static void emit_contours(std::ostringstream& o, const SvgAxesData& d) {
    for (const auto& hp : d.heatmaps) {
        if (hp.opts.contours.empty() || hp.rows <= 0 || hp.cols <= 0) continue;
        const ContourSet  set = trace_contours(hp);
        const ContourDraw cd  = plan_contours(set, d.layout.tr, hp.opts,
                                              d.axes_style.font_path);
        emit_contour_draw(o, d, cd, hp.opts.contour_color,
                          hp.opts.contour_linewidth, hp.opts.contour_fontsize);
    }
}

// A 3D axes' plane contours, already planned by plan_plane_contours() in
// figure_export.cpp -- this writer projects nothing, here as everywhere.
static void emit_contours3d(std::ostringstream& o, const SvgAxesData& d) {
    for (const auto& p : d.contours3d)
        emit_contour_draw(o, d, p.draw, p.color, p.linewidth, p.fontsize);
}

// The frame outline, one <path> of up to four visible edges since v1.0 step
// 19 (it was a <rect>, which cannot express three of them). One stroke group,
// so a full box is still a single element.
static void emit_spines(std::ostringstream& o, const SvgAxesData& d) {
    const AxesStyle& st = d.axes_style;
    const PlotRect&  r  = d.layout.frame;
    if (!(st.spine_bottom || st.spine_left || st.spine_top || st.spine_right)) return;

    o << "  <path d=\"";
    if (st.spine_bottom) o << "M" << r.x << " " << r.y + r.h << "H" << r.x + r.w << " ";
    if (st.spine_top)    o << "M" << r.x << " " << r.y        << "H" << r.x + r.w << " ";
    if (st.spine_left)   o << "M" << r.x << " " << r.y        << "V" << r.y + r.h << " ";
    if (st.spine_right)  o << "M" << r.x + r.w << " " << r.y  << "V" << r.y + r.h << " ";
    o << "\" fill=\"none\" stroke=\"" << rgb(st.spine_color)
      << "\" stroke-opacity=\"" << st.spine_color.a
      << "\" stroke-width=\"" << st.spine_linewidth << "\"/>\n";
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

static void emit_ticks_and_labels(std::ostringstream& o, const SvgAxesData& d) {
    const float tick_len = d.axes_style.tick_length;
    const float fsz      = d.axes_style.label_fontsize;

    o << "  <g stroke=\"" << rgb(d.axes_style.tick_color) << "\" stroke-opacity=\""
      << d.axes_style.tick_color.a << "\" stroke-width=\"" << d.axes_style.tick_linewidth << "\">\n";
    // Along the axis lines, wherever the layout put them (v1.0 step 19); the
    // direction is the layout's too, so this path cannot disagree with
    // NanoVG's about which side of a High axis the marks fall on.
    const float xa = d.layout.xaxis_y;
    const float ya = d.layout.yaxis_x;
    for (const auto& t : d.layout.xticks) {
        float px = d.layout.tr.to_px(t.value);
        o << "    <line x1=\"" << px << "\" y1=\"" << xa
          <<        "\" x2=\"" << px << "\" y2=\"" << xa + d.layout.xtick_dir * tick_len << "\"/>\n";
    }
    for (const auto& t : d.layout.yticks) {
        float py = d.layout.tr.to_py(t.value);
        o << "    <line x1=\"" << ya + d.layout.ytick_dir * tick_len << "\" y1=\"" << py
          <<        "\" x2=\"" << ya << "\" y2=\"" << py << "\"/>\n";
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
    const char* y_anchor = d.layout.ylabel_align == HAlign::Left ? "start" : "end";
    for (const auto& t : d.layout.yticks) {
        float py = d.layout.tr.to_py(t.value);
        o << "    <text x=\"" << d.layout.ylabel_x
          << "\" y=\"" << py + y_base
          << "\" text-anchor=\"" << y_anchor << "\">" << xml_escape(t.label) << "</text>\n";
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
            // The colormap swept along the swatch. Stops sampled from the map,
            // for the reason a path's own segments are (step 13.2b): a
            // two-stop gradient is the RGB chord, and a key drawn in colours
            // the bar beside it does not contain is worse than no key.
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
        } else if (e.kind == LegendKind::Marker) {
            // The same emitter the data markers go through, so a key and the
            // points it keys cannot come out as different shapes.
            // Alpha 0 on the edge means none, which is every kind but scatter_z.
            emit_scatter_marker(o, (sx0 + sx1) * 0.5f, cy, 4.5f, fill, 1.0f, e.marker,
                                e.edge.a > 0.0f ? rgb(e.edge) : std::string());
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
          << xml_escape(e.name) << "</text>\n";
    }
}

// Gradient bar + border + vmin/vmax labels, one per entry of cell.colorbars.
// The <linearGradient>s themselves are emitted by emit_defs(), since SVG
// gradients must be defined once and referenced by id; this only draws the
// rects that reference them, matching NvgRenderer::draw_colorbar's layout.
//
// The id carries both indices (axes, then bar within it). It used to be the
// axes index alone, which was unique only while a cell could hold one bar.
static void emit_colorbar(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    if (!d.layout.has_colorbar()) return;

    const auto& cb = d.colorbar_opts;
    for (std::size_t b = 0; b < d.layout.colorbars.size(); ++b) {
        const auto& r = d.layout.colorbars[b].rect;
        o << "  <rect x=\"" << r.x << "\" y=\"" << r.y
          << "\" width=\"" << r.w << "\" height=\"" << r.h
          << "\" fill=\"url(#colorbarGrad" << idx << "_" << b << ")\""
          << " stroke=\"" << rgb(cb.border_color)
          << "\" stroke-opacity=\"" << cb.border_color.a << "\""
          << " stroke-width=\"" << cb.border_linewidth << "\"/>\n";

        // Both anchors are the layout's, vertically centred on the line; the
        // baseline shift was a literal 3.5 px here once, i.e. right only for
        // the default 10 px font.
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
        std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(cbx.vmax));
        o << "    <text x=\"" << cbx.vmax_x << "\" y=\"" << cbx.vmax_y + base
          << "\" text-anchor=\"" << anchor << "\">" << xml_escape(buf) << "</text>\n";
        std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(cbx.vmin));
        o << "    <text x=\"" << cbx.vmin_x << "\" y=\"" << cbx.vmin_y + base
          << "\" text-anchor=\"" << anchor << "\">" << xml_escape(buf) << "</text>\n";

        // The bar's name: level beyond a horizontal bar, rotated along the
        // outer side of a vertical one. There the baseline shift goes in the
        // *rotated* frame for the reason the y-axis title's does: under
        // rotate(-90) a local +y maps to a global +x, so writing it into y
        // rather than into the anchor is what makes this agree with NanoVG.
        if (!cbx.name.empty() && cbx.horizontal) {
            o << "    <text x=\"" << cbx.name_x << "\" y=\"" << cbx.name_y + base
              << "\" text-anchor=\"middle\">" << xml_escape(cbx.name) << "</text>\n";
        } else if (!cbx.name.empty()) {
            o << "    <text x=\"0\" y=\"" << base << "\" text-anchor=\"middle\""
              << " transform=\"rotate(-90," << cbx.name_x << "," << cbx.name_y << ")"
              <<   " translate(" << cbx.name_x << "," << cbx.name_y << ")\">"
              << xml_escape(cbx.name) << "</text>\n";
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
        //
        // One per bar rather than one per colormap: two bars sharing a cmap
        // would share a gradient, but de-duplicating them would make the id a
        // function of the colormap rather than of the bar, and every emitter
        // would then have to agree on that mapping. NanoVG's image cache does
        // dedupe, because there the key is the colormap already.
        //
        // A horizontal bar runs left to right, offset 0% = vmin.
        for (std::size_t b = 0; b < d.layout.colorbars.size(); ++b) {
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

// One axes' worth of markup. The clip id is per-axes so multiple axes in one
// SVG do not collide.
//
// Emission order is painter's order and mirrors render_frame()'s three
// passes: background, data, then border followed by grid and ticks. The grid
// is emitted *after* the data group so it draws on top, as it does in the
// window and in PNG.
// One group of Box3DPlan polylines. `close` distinguishes a pane (a filled
// polygon) from a grid line or axis edge (an open polyline), matching the
// same split in NvgRenderer's stroke_group().
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
        // Centred on the anchor in both outputs -- plan_box3d() pushes the
        // anchor far enough out that no other alignment is needed, and one
        // alignment is one fewer thing the two paths can disagree about.
        o << "  <text x=\"" << l.x
          << "\" y=\"" << l.y + middle_baseline_offset(l.font_path, l.fontsize)
          << "\" text-anchor=\"middle\" font-family=\"" << svg_font_family_for(l.font_path)
          << "\" font-size=\"" << l.fontsize << "\" fill=\"" << rgb(l.color)
          << "\" fill-opacity=\"" << l.color.a << "\">"
          << xml_escape(l.text) << "</text>\n";
    }
}

// One layer of one plane, in whichever of the four forms plan_planes3d()
// reduced it to. Nothing here projects anything: the plan arrives in pixels,
// which is what keeps the vector and raster paths from disagreeing about where
// a plane's contents are.
//
// The affine <image> is the orthographic heatmap and is exact: an orthographic
// projection maps a rectangle to a parallelogram, which is precisely what
// matrix(...) can express. The unit square is the image's own coordinate
// system, so `width`/`height` are 1 and the whole placement -- position,
// scale, shear and the y flip -- is in the six numbers.
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
        // Same nearest-neighbour intent as the 2D <image>: each consumer keeps
        // the last declaration whose value it understands.
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

    // The whole-plane opacity goes on the group, so a per-primitive alpha
    // still means the plot object's own -- the two multiply, as they do in the
    // raster path where one is baked into the vertex colour and the other is
    // the pass the plane is drawn in.
    o << "  <g";
    if (p.alpha < 1.0f) o << " opacity=\"" << p.alpha << "\"";
    o << ">\n";

    // Polygons: the per-cell perspective heatmap and the plane's own filled
    // areas both. Each carries a stroke of its own fill, which for the cells
    // closes the hairline of background an antialiasing renderer leaves along
    // every shared edge, and for a bar body is invisible.
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

    // Strokes: one <polyline> per segment with its own width, which is §4's
    // SVG concession spent per *segment* rather than per line -- finer than
    // the concession allows, and the reason a line on a plane thins with
    // distance under perspective as the raster ribbon does.
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

    // Markers: the same shapes emit_scatter_marker() draws on a 2D axes, at
    // the same pixel size, because a marker does not scale with distance.
    for (const auto& m : p.marks)
        emit_scatter_marker(o, m.x, m.y, m.size * 0.5f, rgb(m.color), m.color.a,
                            m.marker);

    o << "  </g>\n";
}

// The 3D counterpart of emit_one_axes()'s body: panes and grid clipped to the
// frame (matching NvgRenderer's scissor), then the frame and its annotation
// unclipped, since those deliberately sit in the frame's own margin.
static void emit_box3d(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    const Box3DPlan& plan = *d.box3d;

    o << "  <g clip-path=\"url(#plotArea" << idx << ")\">\n";
    if (!plan.panes.empty()) {
        emit_polys(o, plan.panes, rgb(d.box3d_style.pane_color), nullptr, 0.0f, true);
        emit_polys(o, plan.pane_edges, "none", &d.box3d_style.pane_edge_color, 1.0f, true);
    }
    emit_polys(o, plan.grid, "none", &d.box3d_grid_opts.color,
               d.box3d_grid_opts.linewidth, false);

    // The scene, inside the same clip and on top of the panes -- which is
    // where the raster path draws it too, since the panes are the box's *back*
    // walls and nothing in the scene can be behind them. Already sorted back
    // to front by plan_bars3d(), so emitting in order is the painter's
    // algorithm; SVG has no depth buffer to do it the other way.
    //
    // The two plans are merged rather than concatenated: both are ordered far
    // to near by the *same* object distance (see eye_coord()), so one pass with
    // a plane pointer interleaves whole objects correctly while leaving each
    // bar plot's own exact face order untouched.
    //
    // **Known flaw, recorded rather than fixed -- see memory/spec_3d.md §10.**
    // The granularity here is the whole *object*, and that is not fine enough
    // for a plane that lies among a bar grid rather than clear of it: the plane
    // is emitted entirely before or entirely after every bar, so a cut through
    // the middle of a grid paints over the near half that should occlude it.
    // For a plane that genuinely *intersects* a solid there is no whole-object
    // answer at all, and the same goes for two planes at right angles. The
    // raster path is unaffected -- it has a depth buffer and resolves this per
    // fragment -- so this is the one place PNG and SVG knowingly disagree about
    // something both can express. Fixing it needs geometry splitting, not a
    // better key; §10 records the two candidate mechanisms and why the general
    // one belongs with surfaces (plan item 2) rather than here.
    // **Step 9 answers the flaw above, and the paragraphs before this one are
    // the record of what it replaced.** `scene3d` is one emission order across
    // all three plans, produced by Newell's algorithm in the plan layer -- the
    // pairwise tests and, where they all fail, a split of one polygon along
    // the other's plane. Whole-object granularity is gone: a plane that lies
    // among a bar grid is now emitted in pieces, each on its own side of the
    // bars it cuts through.
    //
    // The split happens there rather than here for the reason this writer
    // takes plans at all: a piece has to be re-projected, and this writer
    // never sees a camera.
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
                // A filled face is a closed ring; a bare box edge, which only
                // a translucent bar emits, is an open two-point line and is
                // never split -- see prepare_paint_poly().
                emit_ring(s.xy.empty() ? p.xy : s.xy, p.filled,
                          p.fill, p.stroke, p.stroke_width);
                break;
            }
            case ScenePaint::Kind::Surface: {
                if (s.index >= d.surfaces3d.size()) break;
                const Surface3DPolygon& p = d.surfaces3d[s.index];
                // `p.filled`, as the bar case has it. A wireframe edge carries
                // no fill colour, so forcing `true` here painted every one of
                // them -- and every piece the painter cut one into -- as a
                // polygon filled with a default-constructed (black) Color.
                emit_ring(s.xy.empty() ? p.xy : s.xy, p.filled,
                          p.fill, p.stroke, p.stroke_width);
                break;
            }
            case ScenePaint::Kind::Mesh: {
                if (s.index >= d.meshes3d.size()) break;
                const SurfaceTriPolygon& p = d.meshes3d[s.index];
                const std::vector<float>& ring = s.xy.empty() ? p.xy : s.xy;
                // A wireframe edge, or a mesh with no colour scale: the
                // ordinary ring the bar and grid-surface cases emit, with
                // `p.filled` rather than a literal true -- post-step-10.1's
                // finding, where forcing it painted every wire edge as a
                // black polygon.
                if (!p.filled || !p.colormapped || (p.gx == 0.0f && p.gy == 0.0f)) {
                    emit_ring(ring, p.filled, p.fill, p.stroke, p.stroke_width);
                    break;
                }
                if (ring.size() < 6) break;

                // **The gradient axis is re-derived from the ring actually
                // being drawn**, which for a split piece is the piece's own.
                // The field is defined over the whole of the triangle's
                // plane, so a piece needs no sub-range recovery of the kind a
                // cut Line3DSegment needs -- but it does need this, or every
                // cut triangle would carry its parent's full ramp compressed
                // into a fragment of itself, which looks entirely plausible
                // and is wrong at every cut.
                float tmin = std::numeric_limits<float>::max();
                float tmax = -std::numeric_limits<float>::max();
                for (std::size_t i = 0; i + 1 < ring.size(); i += 2) {
                    const float t = ring[i] * p.gx + ring[i + 1] * p.gy;
                    tmin = std::min(tmin, t);
                    tmax = std::max(tmax, t);
                }
                // A ring with no extent along the axis has no ramp to paint.
                if (!(tmax > tmin)) {
                    emit_ring(ring, true, p.fill, p.stroke, p.stroke_width);
                    break;
                }
                // Any point on the axis will do as the origin, since only the
                // component along it is read; the ring's first vertex is one.
                const float ox = ring[0], oy = ring[1];
                const float t0 = ox * p.gx + oy * p.gy;
                const float x1 = ox + p.gx * (tmin - t0);
                const float y1 = oy + p.gy * (tmin - t0);
                const float x2 = ox + p.gx * (tmax - t0);
                const float y2 = oy + p.gy * (tmax - t0);

                // Stops sampled from the colormap, never a two-stop ramp
                // between two colours: the straight RGB line between two
                // distant entries leaves the map, which is the picture the
                // raster path rejects by looking up per fragment. Nine stops,
                // the count step 13.3 settled for a line, and the same reason
                // holds for a triangle.
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
                    // The value at that pixel, through the projective form --
                    // exact along the axis under both cameras, since under an
                    // orthographic one the denominator is constant. See
                    // SurfaceTriPolygon.
                    const float num = p.na + p.nx * sx + p.ny * sy;
                    const float den = p.wa + p.wx * sx + p.wy * sy;
                    const float t = den != 0.0f ? num / den : 0.0f;
                    const int e = static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 255.0f);
                    // Looked up, then shaded -- the shader's order, and the
                    // only one in which the light and the colormap mean what
                    // they mean.
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
                // A butt-ended <line>, as a 2D whisker is: a block's edges
                // meet at its corners, and a round cap would bulge past them.
                if (xy.size() < 4 || !(p.width > 0.0f)) break;
                o << "  <line x1=\"" << xy[0] << "\" y1=\"" << xy[1]
                  << "\" x2=\"" << xy[2] << "\" y2=\"" << xy[3]
                  << "\" stroke=\"" << rgb(p.color) << "\"";
                if (p.color.a < 1.0f) o << " stroke-opacity=\"" << p.color.a << "\"";
                o << " stroke-width=\"" << p.width << "\"/>\n";
                break;
            }
            case ScenePaint::Kind::Scatter: {
                if (s.index >= d.markers3d.size()) break;
                const Scatter3DMarker& m = d.markers3d[s.index];
                // Through the same emit_scatter_marker() a 2D series and both
                // legends use, so a marker in the scene, a marker in the key
                // beside it and a marker in the window are one shape. `s.xy`
                // is always empty here: a symbol has no ring to cut.
                emit_scatter_marker(o, m.cx, m.cy, m.radius, rgb(m.color),
                                    m.color.a, m.marker);
                break;
            }
            case ScenePaint::Kind::Line: {
                if (s.index >= d.lines3d.size()) break;
                const Line3DSegment& g = d.lines3d[s.index];
                if (g.width <= 0.0f) break;
                // A split piece is a sub-range of the segment, and its colours
                // have to be the sub-range of the ramp -- not the whole one
                // compressed into a fragment, which looks entirely plausible
                // and is wrong at every cut. `s.xy` is the piece's own two
                // pixels; where it falls along the parent is what says which
                // part of the ramp it carries.
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
                    // **Stops sampled from the colormap, not a two-stop ramp
                    // between the ends.** A two-stop gradient is the straight
                    // RGB chord, which between two distant entries leaves the
                    // colormap entirely -- the same picture the raster path
                    // rejects by looking up per fragment. Eight intervals is
                    // where a viridis ramp stops being distinguishable from
                    // the shader's continuous lookup at any width a line is
                    // drawn at.
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
                        // Where this stop is along the *parent* segment, which
                        // is what both the value and the depth cue are defined
                        // against. For an unsplit segment t0..t1 is 0..1 and
                        // this is just f.
                        const float t = lerp(t0, t1, f);
                        const int e = static_cast<int>(
                            std::clamp(lerp(g.v0, g.v1, t), 0.0f, 1.0f) * 255.0f);
                        // Looked up, then darkened -- the shader's order, and
                        // the only one in which the cue and the colormap mean
                        // what they mean.
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
                // Round, which is what joins a path at every bend: a cap of
                // exactly the half width fills the wedge two segments leave
                // between them. See Line3DSegment on what it costs at the two
                // open ends.
                o << " stroke-width=\"" << g.width
                  << "\" stroke-linecap=\"round\"/>\n";
                break;
            }
            case ScenePaint::Kind::Plane: {
                // Every layer of one plane, in the 2D painter order the plan
                // put them in. A piece of a split plane is drawn through a
                // clip rather than cut: an <image> cannot be cut, and the four
                // forms a plane can take are all coplanar with each other, so
                // one outline serves all of them.
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
    // A plane whose quad degenerated -- edge-on to the camera, or clipped away
    // by a perspective near plane -- never reaches the order, and dropping its
    // contents on that account would be a regression rather than a fix.
    for (std::size_t i = 0; i < d.planes3d.size(); ++i)
        if (!plane_done[i]) emit_plane3d(o, d.planes3d[i]);
    o << "  </g>\n";

    // A plane's contours, over the scene and under the box's own furniture --
    // the slot the 2D ones occupy, and where render_frame() draws them.
    if (!d.contours3d.empty()) {
        o << "  <g clip-path=\"url(#plotArea" << idx << ")\">\n";
        emit_contours3d(o, d);
        o << "  </g>\n";
    }

    emit_polys(o, plan.axis_lines, "none", &d.axes_style.spine_color,
               d.axes_style.spine_linewidth, false);
    emit_polys(o, plan.tick_marks, "none", &d.axes_style.tick_color,
               d.axes_style.tick_linewidth, false);
    emit_box3d_labels(o, plan.tick_labels);
    emit_box3d_labels(o, plan.axis_titles);
}

static void emit_one_axes(std::ostringstream& o, const SvgAxesData& d, std::size_t idx) {
    if (d.box3d) {
        emit_box3d(o, d, idx);
        // The axes title is the one piece of text a 3D cell positions the
        // same way a 2D one does, so it comes out of the shared path; the
        // three axis titles are on the box and are in the plan.
        emit_titles(o, d);
        // Hoisted decoration: the same emitters, the same boxes
        // compute_figure_layout() carved, the same styling, in the 2D order.
        emit_legend(o, d, idx);
        emit_colorbar(o, d, idx);
        return;
    }

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

    emit_spines(o, d);
    emit_grid(o, d);
    emit_ticks_and_labels(o, d);
    // After the grid, matching NanoVG's order: an interior axis line belongs
    // over the grid rules it crosses, not under them.
    emit_interior_axes(o, d);
    emit_titles(o, d);
    emit_legend(o, d, idx);
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
