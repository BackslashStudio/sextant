#include "nvg_renderer.h"
#include "coord_transform.h"  // for Tick definition
#include "../colormaps.h"
#include "../font_discovery.h"
#include "../line_dash.h"
#include "../contour.h"
#include "nanovg.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sextant {

// Begins a path for one straight segment, split into the "on" runs of its
// LineStyle. The caller sets stroke colour and width and calls nvgStroke().
//
// NanoVG has no stroke-dasharray, so the runs are emitted as separate
// subpaths. That is only tractable because both callers -- grid lines and the
// legend swatch -- draw a *single straight segment*; dashing an arbitrary
// NanoVG path would need arc-length parameterisation of its beziers. Data
// lines are dashed in the stroke shader instead.
//
// LineStyle::None emits an empty path, so nvgStroke() draws nothing.
static void begin_styled_segment(NVGcontext* vg, float x0, float y0,
                                 float x1, float y1, LineStyle ls) {
    nvgBeginPath(vg);
    if (ls == LineStyle::None) return;

    const DashPattern d = dash_pattern(ls);
    const float len = std::hypot(x1 - x0, y1 - y0);
    if (!d.dashed() || len <= 0.0f) {
        nvgMoveTo(vg, x0, y0);
        nvgLineTo(vg, x1, y1);
        return;
    }

    const float ux = (x1 - x0) / len, uy = (y1 - y0) / len;
    // `on` tracks arm parity alongside the index; count is always even, so
    // wrapping the index keeps the two in step.
    int  i  = 0;
    bool on = true;
    float t = 0.0f;
    while (t < len) {
        const float end = std::min(t + d.seg[i], len);
        if (on && end > t) {
            nvgMoveTo(vg, x0 + ux * t,   y0 + uy * t);
            nvgLineTo(vg, x0 + ux * end, y0 + uy * end);
        }
        t  = end;
        i  = (i + 1) % d.count;
        on = !on;
    }
}

static int load_default_font(NVGcontext* vg) {
    const FontEntry* pick = pick_default_font();
    if (!pick) return -1;
    return nvgCreateFont(vg, "default", pick->path.c_str());
}

NvgRenderer::NvgRenderer(NVGcontext* vg) : vg_(vg) {
    font_ = load_default_font(vg);
}

NvgRenderer::~NvgRenderer() {
    for (const auto& [name, image] : colorbar_images_)
        nvgDeleteImage(vg_, image);
}

int NvgRenderer::font_for_path(const std::string& path) {
    if (path.empty()) return font_;

    auto it = font_cache_.find(path);
    if (it != font_cache_.end()) return it->second;

    int h = nvgCreateFont(vg_, path.c_str(), path.c_str());
    if (h == -1) h = font_;
    font_cache_[path] = h;
    return h;
}

void NvgRenderer::draw_axes_background(const PlotRect& r) {
    nvgBeginPath(vg_);
    nvgRect(vg_, r.x, r.y, r.w, r.h);
    nvgFillColor(vg_, nvgRGBf(1.0f, 1.0f, 1.0f));
    nvgFill(vg_);
}

void NvgRenderer::draw_axes_border(const PlotRect& r, const AxesStyle& style) {
    nvgBeginPath(vg_);
    nvgRect(vg_, r.x, r.y, r.w, r.h);
    const auto& sc = style.spine_color;
    nvgStrokeColor(vg_, nvgRGBAf(sc.r, sc.g, sc.b, sc.a));
    nvgStrokeWidth(vg_, style.spine_linewidth);
    nvgStroke(vg_);
}

// Contour lines over a heatmap, plus their inline level labels.
//
// This sits in the NanoVG pass rather than in DataRenderer because the labels
// are text, and text is NanoVG's alone; splitting the two would mean
// projecting and breaking the same polylines twice per frame and caching the
// result twice. Called first in pass 3, so contours land above every plot the
// data pass drew and below the axes furniture -- the same slot the SVG writer
// emits them in.
void NvgRenderer::draw_contours(const CellLayout& cell, const RenderSnapshot& snap,
                                unsigned long long data_generation, int axes_index)
{
    bool any = false;
    for (const auto& hp : snap.heatmaps)
        if (!hp.opts.contours.empty()) { any = true; break; }
    if (!any) return;

    const PlotRect& r = cell.frame;
    nvgSave(vg_);
    nvgScissor(vg_, r.x, r.y, r.w, r.h);

    for (std::size_t i = 0; i < snap.heatmaps.size(); ++i) {
        const auto& hp = snap.heatmaps[i];
        if (hp.opts.contours.empty() || hp.rows <= 0 || hp.cols <= 0) continue;

        const ContourSet& set = contour_cache_.get(axes_index, static_cast<int>(i),
                                                   data_generation, hp);
        const ContourDraw d = plan_contours(set, cell.tr, hp.opts,
                                            snap.axes_style.font_path);
        if (d.runs.empty()) continue;

        const auto& cc = hp.opts.contour_color;

        // Every run of one heatmap's contours as subpaths of a single path:
        // they share a colour and width, so this is one stroke however many
        // levels were asked for.
        nvgBeginPath(vg_);
        for (const auto& run : d.runs) {
            nvgMoveTo(vg_, run.px[0], run.py[0]);
            for (std::size_t k = 1; k < run.px.size(); ++k)
                nvgLineTo(vg_, run.px[k], run.py[k]);
        }
        nvgStrokeColor(vg_, nvgRGBAf(cc.r, cc.g, cc.b, cc.a));
        nvgStrokeWidth(vg_, hp.opts.contour_linewidth);
        nvgStroke(vg_);

        if (d.labels.empty()) continue;
        nvgFontFaceId(vg_, font_for_path(snap.axes_style.font_path));
        nvgFontSize(vg_, hp.opts.contour_fontsize);
        nvgFillColor(vg_, nvgRGBAf(cc.r, cc.g, cc.b, cc.a));
        nvgTextAlign(vg_, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        for (const auto& lb : d.labels) {
            nvgSave(vg_);
            nvgTranslate(vg_, lb.x, lb.y);
            nvgRotate(vg_, lb.angle);
            nvgText(vg_, 0.0f, 0.0f, lb.text.c_str(), nullptr);
            nvgRestore(vg_);
        }
    }

    nvgRestore(vg_);
}

void NvgRenderer::draw_ticks(const CellLayout& cell, const AxesStyle& style,
                              bool grid_enabled, const GridOptions& grid_opts)
{
    const PlotRect& r = cell.frame;
    const auto& xticks = cell.xticks;
    const auto& yticks = cell.yticks;
    const double xmin = cell.tr.xmin, xmax = cell.tr.xmax;
    const double ymin = cell.tr.ymin, ymax = cell.tr.ymax;

    const float tick_len  = style.tick_length;
    const float font_size = style.label_fontsize;

    const auto& tc = style.tick_color;
    nvgStrokeColor(vg_, nvgRGBAf(tc.r, tc.g, tc.b, tc.a));
    nvgStrokeWidth(vg_, style.tick_linewidth);

    if (font_ != -1) {
        nvgFontFaceId(vg_, font_for_path(style.font_path));
        nvgFontSize(vg_, font_size);
        const auto& tlc = style.label_color;
        nvgFillColor(vg_, nvgRGBAf(tlc.r, tlc.g, tlc.b, tlc.a));
    }

    // X ticks — bottom edge of plot area
    for (const auto& t : xticks) {
        const float px = r.x + static_cast<float>((t.value - xmin) / (xmax - xmin)) * r.w;
        if (px < r.x || px > r.x + r.w) continue;

        // Tick mark
        nvgBeginPath(vg_);
        nvgMoveTo(vg_, px, r.y + r.h);
        nvgLineTo(vg_, px, r.y + r.h + tick_len);
        nvgStroke(vg_);

        // Grid line — dashed by hand via begin_styled_segment(), since
        // NanoVG has no stroke-dasharray of its own.
        if (grid_enabled) {
            begin_styled_segment(vg_, px, r.y, px, r.y + r.h, grid_opts.linestyle);
            const auto& gc = grid_opts.color;
            nvgStrokeColor(vg_, nvgRGBAf(gc.r, gc.g, gc.b, gc.a));
            nvgStrokeWidth(vg_, grid_opts.linewidth);
            nvgStroke(vg_);
            nvgStrokeColor(vg_, nvgRGBAf(tc.r, tc.g, tc.b, tc.a));
            nvgStrokeWidth(vg_, style.tick_linewidth);
        }

        // Label
        if (font_ != -1) {
            nvgTextAlign(vg_, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgText(vg_, px, cell.xlabel_top, t.label.c_str(), nullptr);
        }
    }

    // Y ticks — left edge of plot area
    for (const auto& t : yticks) {
        const float py = r.y + r.h - static_cast<float>((t.value - ymin) / (ymax - ymin)) * r.h;
        if (py < r.y || py > r.y + r.h) continue;

        // Tick mark
        nvgBeginPath(vg_);
        nvgMoveTo(vg_, r.x - tick_len, py);
        nvgLineTo(vg_, r.x,            py);
        nvgStroke(vg_);

        // Grid line
        if (grid_enabled) {
            begin_styled_segment(vg_, r.x, py, r.x + r.w, py, grid_opts.linestyle);
            const auto& gc = grid_opts.color;
            nvgStrokeColor(vg_, nvgRGBAf(gc.r, gc.g, gc.b, gc.a));
            nvgStrokeWidth(vg_, grid_opts.linewidth);
            nvgStroke(vg_);
            nvgStrokeColor(vg_, nvgRGBAf(tc.r, tc.g, tc.b, tc.a));
            nvgStrokeWidth(vg_, style.tick_linewidth);
        }

        // Label
        if (font_ != -1) {
            nvgTextAlign(vg_, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgText(vg_, cell.ylabel_right, py, t.label.c_str(), nullptr);
        }
    }
}

// Every anchor here comes from the layout, which sized the band each title
// sits in from that title's own font metrics. The three fixed offsets this
// replaced (-20 above the frame, +42 below it, -45 to its left) were tuned
// for the default font sizes and were wrong for any other: a 40 px title
// drew over the row above, an 8 px one floated in whitespace.
void NvgRenderer::draw_titles(const CellLayout& cell, const RenderSnapshot& snap) {
    if (font_ == -1) return;

    nvgFontFaceId(vg_, font_for_path(snap.axes_style.font_path));
    nvgTextAlign(vg_, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    const auto& style = snap.axes_style;

    if (!snap.title.empty()) {
        const auto& c = style.title_color;
        nvgFillColor(vg_, nvgRGBAf(c.r, c.g, c.b, c.a));
        nvgFontSize(vg_, style.title_fontsize);
        nvgText(vg_, cell.title_x, cell.title_y, snap.title.c_str(), nullptr);
    }

    if (!snap.xtitle.empty()) {
        const auto& c = style.xtitle_color;
        nvgFillColor(vg_, nvgRGBAf(c.r, c.g, c.b, c.a));
        nvgFontSize(vg_, style.xtitle_fontsize);
        nvgText(vg_, cell.xtitle_x, cell.xtitle_y, snap.xtitle.c_str(), nullptr);
    }

    if (!snap.ytitle.empty()) {
        const auto& c = style.ytitle_color;
        nvgFillColor(vg_, nvgRGBAf(c.r, c.g, c.b, c.a));
        nvgFontSize(vg_, style.ytitle_fontsize);
        nvgSave(vg_);
        nvgTranslate(vg_, cell.ytitle_x, cell.ytitle_y);
        nvgRotate(vg_, -NVG_PI / 2.0f);
        nvgText(vg_, 0, 0, snap.ytitle.c_str(), nullptr);
        nvgRestore(vg_);
    }
}

// The entry list, the box size and the layout constants live in
// figure_layout.h, shared with the SVG writer.
void NvgRenderer::draw_legend(const CellLayout& cell, const LegendOptions& opts) {
    if (font_ == -1 || cell.legend_entries.empty()) return;

    const PlotRect& box = cell.legend;
    const auto& entries = cell.legend_entries;
    const float fsz   = opts.fontsize;
    const float row_h = legend_row_height(fsz);

    if (opts.frameon) {
        const auto& fc = opts.frame_color;
        const auto& bc = opts.border_color;
        nvgBeginPath(vg_);
        nvgRoundedRect(vg_, box.x, box.y, box.w, box.h, 3.0f);
        nvgFillColor(vg_, nvgRGBAf(fc.r, fc.g, fc.b, fc.a));
        nvgFill(vg_);
        nvgStrokeColor(vg_, nvgRGBAf(bc.r, bc.g, bc.b, bc.a));
        nvgStrokeWidth(vg_, opts.border_linewidth);
        nvgStroke(vg_);
    }

    const int legend_font = font_for_path(opts.font_path);
    nvgFontFaceId(vg_, legend_font);
    nvgFontSize(vg_, fsz);

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        const float cy  = box.y + kLegendPad + row_h * static_cast<float>(i) + row_h * 0.5f;
        const float sx0 = box.x + kLegendPad, sx1 = sx0 + kLegendSwatchW;

        nvgStrokeColor(vg_, nvgRGBAf(e.color.r, e.color.g, e.color.b, e.color.a));
        nvgFillColor(vg_,   nvgRGBAf(e.color.r, e.color.g, e.color.b, e.color.a));

        switch (e.kind) {
            case LegendKind::Line:
                begin_styled_segment(vg_, sx0, cy, sx1, cy, e.style);
                nvgStrokeWidth(vg_, 2.0f);
                nvgStroke(vg_);
                break;
            case LegendKind::Marker:
                nvgBeginPath(vg_);
                nvgCircle(vg_, (sx0 + sx1) * 0.5f, cy, 4.5f);
                nvgFill(vg_);
                break;
            case LegendKind::Bar:
                nvgBeginPath(vg_);
                nvgRect(vg_, sx0, cy - 5.0f, kLegendSwatchW, 10.0f);
                nvgFill(vg_);
                break;
        }

        const auto& tc = opts.text_color;
        nvgFillColor(vg_, nvgRGBAf(tc.r, tc.g, tc.b, tc.a));
        nvgFontFaceId(vg_, legend_font);
        nvgFontSize(vg_, fsz);
        nvgTextAlign(vg_, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg_, sx1 + kLegendGap, cy, e.label.c_str(), nullptr);
    }
}

void NvgRenderer::draw_colorbar(const CellLayout& cell, const ColorbarOptions& opts) {
    const PlotRect& r  = cell.colorbar;
    const Colormap cmap = cell.colorbar_cmap;
    const float vmin = cell.colorbar_vmin, vmax = cell.colorbar_vmax;

    // NanoVG batches draw calls and executes them at nvgEndFrame(), so an
    // image deleted right after nvgFill() leaves the queued fill referencing a
    // destroyed texture and renders black. Cache the gradient image per
    // colormap instead, kept alive for this renderer's lifetime.
    auto it = colorbar_images_.find(cmap);
    int image;
    if (it != colorbar_images_.end()) {
        image = it->second;
    } else {
        const uint8_t* lut = colormaps::get(cmap);  // 256 RGBA entries, index0=vmin..index255=vmax
        // Build a 1-wide, 256-tall image so it renders as a vertical
        // gradient with no rotation needed; row 0 (top) = vmax, row 255
        // (bottom) = vmin.
        uint8_t img[256 * 4];
        for (int i = 0; i < 256; ++i)
            std::memcpy(&img[static_cast<std::size_t>(i) * 4], &lut[(255 - i) * 4], 4);
        image = nvgCreateImageRGBA(vg_, 1, 256, NVG_IMAGE_NEAREST, img);
        colorbar_images_.emplace(cmap, image);
    }

    NVGpaint paint = nvgImagePattern(vg_, r.x, r.y, r.w, r.h, 0.0f, image, 1.0f);

    nvgBeginPath(vg_);
    nvgRect(vg_, r.x, r.y, r.w, r.h);
    nvgFillPaint(vg_, paint);
    nvgFill(vg_);

    const auto& bc = opts.border_color;
    nvgBeginPath(vg_);
    nvgRect(vg_, r.x, r.y, r.w, r.h);
    nvgStrokeColor(vg_, nvgRGBAf(bc.r, bc.g, bc.b, bc.a));
    nvgStrokeWidth(vg_, opts.border_linewidth);
    nvgStroke(vg_);

    if (font_ != -1) {
        const auto& tc = opts.text_color;
        nvgFontFaceId(vg_, font_for_path(opts.font_path));
        nvgFontSize(vg_, opts.fontsize);
        nvgFillColor(vg_, nvgRGBAf(tc.r, tc.g, tc.b, tc.a));
        nvgTextAlign(vg_, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(vmax));
        nvgText(vg_, r.x + r.w + kColorbarLabelGap, r.y, buf, nullptr);
        std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(vmin));
        nvgText(vg_, r.x + r.w + kColorbarLabelGap, r.y + r.h, buf, nullptr);
    }
}

void NvgRenderer::draw_suptitle(int fig_w, float top_offset,
                                const std::string& text, const SuptitleOptions& opts) {
    if (text.empty() || font_ == -1) return;
    const auto& c = opts.color;
    nvgFontFaceId(vg_, font_for_path(opts.font_path));
    nvgFontSize(vg_, opts.fontsize);
    nvgFillColor(vg_, nvgRGBAf(c.r, c.g, c.b, c.a));

    int halign = NVG_ALIGN_CENTER;
    if      (opts.align == HAlign::Left)  halign = NVG_ALIGN_LEFT;
    else if (opts.align == HAlign::Right) halign = NVG_ALIGN_RIGHT;
    nvgTextAlign(vg_, halign | NVG_ALIGN_MIDDLE);

    nvgText(vg_, suptitle_anchor_x(static_cast<float>(fig_w), opts),
            suptitle_center_y(top_offset, opts),
            text.c_str(), nullptr);
}

namespace {
std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos) { lines.push_back(text.substr(start)); break; }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

constexpr float kHintPad      = 6.0f;
constexpr float kHintFontSize = 12.0f;
constexpr float kHintOffset   = 12.0f;  // offset from the anchor point
} // namespace

void NvgRenderer::draw_hint(int fig_w, int fig_h, float anchor_x, float anchor_y,
                            const std::string& text) {
    if (text.empty() || font_ == -1) return;

    const auto lines = split_lines(text);
    nvgFontFaceId(vg_, font_);
    nvgFontSize(vg_, kHintFontSize);

    float max_line_w = 0.0f;
    float line_h = kHintFontSize;
    {
        float asc, desc, lh;
        nvgTextMetrics(vg_, &asc, &desc, &lh);
        line_h = lh;
    }
    for (const auto& line : lines) {
        float bounds[4];
        nvgTextBounds(vg_, 0, 0, line.c_str(), nullptr, bounds);
        max_line_w = std::max(max_line_w, bounds[2] - bounds[0]);
    }

    BoxSize box{ kHintPad * 2.0f + max_line_w,
                 kHintPad * 2.0f + line_h * static_cast<float>(lines.size()) };

    float bx = anchor_x + kHintOffset;
    float by = anchor_y + kHintOffset;
    bx = std::clamp(bx, 0.0f, std::max(0.0f, static_cast<float>(fig_w) - box.w));
    by = std::clamp(by, 0.0f, std::max(0.0f, static_cast<float>(fig_h) - box.h));

    nvgBeginPath(vg_);
    nvgRoundedRect(vg_, bx, by, box.w, box.h, 3.0f);
    nvgFillColor(vg_, nvgRGBAf(0.05f, 0.05f, 0.05f, 0.85f));
    nvgFill(vg_);
    nvgStrokeColor(vg_, nvgRGBAf(1.0f, 1.0f, 1.0f, 0.3f));
    nvgStrokeWidth(vg_, 1.0f);
    nvgStroke(vg_);

    nvgFontFaceId(vg_, font_);
    nvgFontSize(vg_, kHintFontSize);
    nvgFillColor(vg_, nvgRGBAf(1.0f, 1.0f, 1.0f, 0.95f));
    nvgTextAlign(vg_, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        nvgText(vg_, bx + kHintPad, by + kHintPad + line_h * static_cast<float>(i),
                lines[i].c_str(), nullptr);
    }
}

} // namespace sextant
