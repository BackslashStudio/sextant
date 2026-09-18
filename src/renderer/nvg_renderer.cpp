#include "nvg_renderer.h"
#include "coord_transform.h"  // for Tick definition
#include "../colormaps.h"
#include "../font_discovery.h"
#include "../line_dash.h"
#include "../contour.h"
#include "marker_shape.h"
#include "nanovg.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sextant {
    // Begins a path for one straight segment, split into the "on" runs of its
    // LineStyle (NanoVG has no dasharray; data lines dash in the shader). The
    // caller strokes. LineStyle::None emits an empty path.
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
        // `on` tracks arm parity; count is even, so wrapping keeps them in step.
        int i = 0;
        bool on = true;
        float t = 0.0f;
        while (t < len) {
            const float end = std::min(t + d.seg[i], len);
            if (on && end > t) {
                nvgMoveTo(vg, x0 + ux * t, y0 + uy * t);
                nvgLineTo(vg, x0 + ux * end, y0 + uy * end);
            }
            t = end;
            i = (i + 1) % d.count;
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
        for (const auto& [name, image]: colorbar_images_)
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

    // Four segments (each edge has its own visibility flag), one stroke.
    void NvgRenderer::draw_axes_border(const PlotRect& r, const AxesStyle& style) {
        const bool any = style.spine_bottom || style.spine_left
                         || style.spine_top || style.spine_right;
        if (!any) return;

        nvgBeginPath(vg_);
        if (style.spine_bottom) {
            nvgMoveTo(vg_, r.x, r.y + r.h);
            nvgLineTo(vg_, r.x + r.w, r.y + r.h);
        }
        if (style.spine_top) {
            nvgMoveTo(vg_, r.x, r.y);
            nvgLineTo(vg_, r.x + r.w, r.y);
        }
        if (style.spine_left) {
            nvgMoveTo(vg_, r.x, r.y);
            nvgLineTo(vg_, r.x, r.y + r.h);
        }
        if (style.spine_right) {
            nvgMoveTo(vg_, r.x + r.w, r.y);
            nvgLineTo(vg_, r.x + r.w, r.y + r.h);
        }
        const auto& sc = style.spine_color;
        nvgStrokeColor(vg_, nvgRGBAf(sc.r, sc.g, sc.b, sc.a));
        nvgStrokeWidth(vg_, style.spine_linewidth);
        nvgStroke(vg_);
    }

    namespace {
        // A Box3DPlan::Poly as one subpath; `close` = pane, open = grid line.
        void path_poly(NVGcontext* vg, const Box3DPlan::Poly& p, bool close) {
            if (p.xy.size() < 4) return;
            nvgMoveTo(vg, p.xy[0], p.xy[1]);
            for (std::size_t i = 2; i + 1 < p.xy.size(); i += 2)
                nvgLineTo(vg, p.xy[i], p.xy[i + 1]);
            if (close) nvgClosePath(vg);
        }

        // A whole group as subpaths of one path (shared color and width).
        void stroke_group(NVGcontext* vg, const std::vector<Box3DPlan::Poly>& polys,
                          Color c, float width, bool close) {
            if (polys.empty() || width <= 0.0f) return;
            nvgBeginPath(vg);
            for (const auto& p: polys) path_poly(vg, p, close);
            nvgStrokeColor(vg, nvgRGBAf(c.r, c.g, c.b, c.a));
            nvgStrokeWidth(vg, width);
            nvgStroke(vg);
        }
    } // namespace

    void NvgRenderer::draw_box3d_panes(const Box3DPlan& plan, const RenderSnapshot3D& snap,
                                       const PlotRect& frame) {
        // Scissor to the frame so panes never spill into a neighbouring subplot.
        nvgSave(vg_);
        nvgScissor(vg_, frame.x, frame.y, frame.w, frame.h);

        if (!plan.panes.empty()) {
            const auto& pc = snap.box_style.pane_color;
            nvgBeginPath(vg_);
            for (const auto& p: plan.panes) path_poly(vg_, p, true);
            nvgFillColor(vg_, nvgRGBAf(pc.r, pc.g, pc.b, pc.a));
            nvgFill(vg_);
            stroke_group(vg_, plan.pane_edges, snap.box_style.pane_edge_color, 1.0f, true);
        }

        stroke_group(vg_, plan.grid, snap.grid_opts.color, snap.grid_opts.linewidth, false);
        nvgRestore(vg_);
    }

    void NvgRenderer::draw_box3d_frame(const Box3DPlan& plan, const RenderSnapshot3D& snap) {
        const auto& st = snap.axes_style;

        // Unscissored: labels and titles sit in the frame's margin.
        stroke_group(vg_, plan.axis_lines, st.spine_color, st.spine_linewidth, false);
        stroke_group(vg_, plan.tick_marks, st.tick_color, st.tick_linewidth, false);

        if (font_ == -1) return;
        nvgTextAlign(vg_, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        for (const auto* group: {&plan.tick_labels, &plan.axis_titles})
            for (const auto& l: *group) {
                if (l.text.empty()) continue;
                nvgFontFaceId(vg_, font_for_path(l.font_path));
                nvgFontSize(vg_, l.fontsize);
                nvgFillColor(vg_, nvgRGBAf(l.color.r, l.color.g, l.color.b, l.color.a));
                nvgText(vg_, l.x, l.y, l.text.c_str(), nullptr);
            }
    }

    // The axes title only; the 3D axis titles come from the plan.
    void NvgRenderer::draw_title3d(const CellLayout& cell, const RenderSnapshot3D& snap) {
        if (font_ == -1 || snap.title.empty()) return;
        const auto& style = snap.axes_style;
        const auto& c = style.title_color;
        nvgFontFaceId(vg_, font_for_path(style.font_path));
        nvgTextAlign(vg_, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg_, nvgRGBAf(c.r, c.g, c.b, c.a));
        nvgFontSize(vg_, style.title_fontsize);
        nvgText(vg_, cell.title_x, cell.title_y, snap.title.c_str(), nullptr);
    }

    // Heatmap contours with inline labels (in NanoVG because labels are text).
    // Called first in pass 3: above the data, below the furniture.
    void NvgRenderer::draw_contours(const CellLayout& cell, const RenderSnapshot& snap,
                                    unsigned long long data_generation, int axes_index) {
        bool any = false;
        for (const auto& hp: snap.heatmaps)
            if (!hp.opts.contours.empty()) {
                any = true;
                break;
            }
        if (!any) return;

        const PlotRect& r = cell.frame;
        nvgSave(vg_);
        nvgScissor(vg_, r.x, r.y, r.w, r.h);

        for (std::size_t i = 0; i < snap.heatmaps.size(); ++i) {
            const auto& hp = snap.heatmaps[i];
            if (hp.opts.contours.empty() || hp.rows <= 0 || hp.cols <= 0) continue;

            const ContourSet& set = contour_cache_.get(axes_index, -1, static_cast<int>(i),
                                                       data_generation, hp);
            const ContourDraw d = plan_contours(set, cell.tr, hp.opts,
                                                snap.axes_style.font_path);
            stroke_contours(d, hp.opts.contour_color, hp.opts.contour_linewidth,
                            hp.opts.contour_fontsize, snap.axes_style.font_path);
        }

        nvgRestore(vg_);
    }

    // Stroke and label one planned contour set (2D and plane contours alike).
    void NvgRenderer::stroke_contours(const ContourDraw& d, const Color& cc,
                                      float linewidth, float fontsize,
                                      const std::string& font_path) {
        if (d.runs.empty()) return;

        // All runs as subpaths of one path: one stroke.
        nvgBeginPath(vg_);
        for (const auto& run: d.runs) {
            nvgMoveTo(vg_, run.px[0], run.py[0]);
            for (std::size_t k = 1; k < run.px.size(); ++k)
                nvgLineTo(vg_, run.px[k], run.py[k]);
        }
        nvgStrokeColor(vg_, nvgRGBAf(cc.r, cc.g, cc.b, cc.a));
        nvgStrokeWidth(vg_, linewidth);
        nvgStroke(vg_);

        if (d.labels.empty()) return;
        nvgFontFaceId(vg_, font_for_path(font_path));
        nvgFontSize(vg_, fontsize);
        nvgFillColor(vg_, nvgRGBAf(cc.r, cc.g, cc.b, cc.a));
        nvgTextAlign(vg_, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        for (const auto& lb: d.labels) {
            nvgSave(vg_);
            nvgTranslate(vg_, lb.x, lb.y);
            nvgRotate(vg_, lb.angle);
            nvgText(vg_, 0.0f, 0.0f, lb.text.c_str(), nullptr);
            nvgRestore(vg_);
        }
    }

    // Plane contours, drawn in pass 3 as annotation (fixed size, not occluded),
    // clipped to the cell frame. Already in pixels.
    void NvgRenderer::draw_contours3d(const CellLayout& cell, const RenderSnapshot3D& snap,
                                      unsigned long long data_generation, int axes_index) {
        if (!cell.box3d) return;
        const std::vector<PlaneContourDraw> plans =
                plan_plane_contours(cell.box3d->proj, snap.planes, snap.axes_style.font_path,
                                    &contour_cache_, data_generation, axes_index);
        if (plans.empty()) return;

        const PlotRect& r = cell.frame;
        nvgSave(vg_);
        nvgScissor(vg_, r.x, r.y, r.w, r.h);
        for (const auto& p: plans)
            stroke_contours(p.draw, p.color, p.linewidth, p.fontsize,
                            snap.axes_style.font_path);
        nvgRestore(vg_);
    }

    void NvgRenderer::draw_ticks(const CellLayout& cell, const AxesStyle& style,
                                 bool grid_enabled, const GridOptions& grid_opts) {
        const PlotRect& r = cell.frame;
        const auto& xticks = cell.xticks;
        const auto& yticks = cell.yticks;
        const double xmin = cell.tr.xmin, xmax = cell.tr.xmax;
        const double ymin = cell.tr.ymin, ymax = cell.tr.ymax;

        const float tick_len = style.tick_length;
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

        // X ticks — along the axis line, wherever the layout put it
        const float xa = cell.xaxis_y;
        for (const auto& t: xticks) {
            const float px = r.x + static_cast<float>((t.value - xmin) / (xmax - xmin)) * r.w;
            if (px < r.x || px > r.x + r.w) continue;

            // Tick mark
            nvgBeginPath(vg_);
            nvgMoveTo(vg_, px, xa);
            nvgLineTo(vg_, px, xa + cell.xtick_dir * tick_len);
            nvgStroke(vg_);

            // Grid line, dashed via begin_styled_segment().
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

        // Y ticks — along the axis line, wherever the layout put it
        const float ya = cell.yaxis_x;
        for (const auto& t: yticks) {
            const float py = r.y + r.h - static_cast<float>((t.value - ymin) / (ymax - ymin)) * r.h;
            if (py < r.y || py > r.y + r.h) continue;

            // Tick mark
            nvgBeginPath(vg_);
            nvgMoveTo(vg_, ya + cell.ytick_dir * tick_len, py);
            nvgLineTo(vg_, ya, py);
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
                const int halign = cell.ylabel_align == HAlign::Left ? NVG_ALIGN_LEFT : NVG_ALIGN_RIGHT;
                nvgTextAlign(vg_, halign | NVG_ALIGN_MIDDLE);
                nvgText(vg_, cell.ylabel_x, py, t.label.c_str(), nullptr);
            }
        }

        // An interior axis line gets its own stroke, over the grid lines.
        if (cell.xaxis_interior || cell.yaxis_interior) {
            nvgBeginPath(vg_);
            if (cell.xaxis_interior) {
                nvgMoveTo(vg_, r.x, xa);
                nvgLineTo(vg_, r.x + r.w, xa);
            }
            if (cell.yaxis_interior) {
                nvgMoveTo(vg_, ya, r.y);
                nvgLineTo(vg_, ya, r.y + r.h);
            }
            const auto& sc = style.spine_color;
            nvgStrokeColor(vg_, nvgRGBAf(sc.r, sc.g, sc.b, sc.a));
            nvgStrokeWidth(vg_, style.spine_linewidth);
            nvgStroke(vg_);
        }
    }

    // Anchors come from the layout, sized from each title's font metrics.
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

    // Entries, box size and constants come from figure_layout.h.
    void NvgRenderer::draw_legend(const CellLayout& cell, const LegendOptions& opts) {
        if (font_ == -1 || cell.legend_entries.empty()) return;

        const PlotRect& box = cell.legend;
        const auto& entries = cell.legend_entries;
        const float fsz = opts.fontsize;

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

        for (std::size_t i = 0; i < entries.size() && i < cell.legend_slots.size(); ++i) {
            const auto& e = entries[i];
            const float cy = cell.legend_slots[i].cy;
            const float sx0 = cell.legend_slots[i].x, sx1 = sx0 + kLegendSwatchW;

            nvgStrokeColor(vg_, nvgRGBAf(e.color.r, e.color.g, e.color.b, e.color.a));
            nvgFillColor(vg_, nvgRGBAf(e.color.r, e.color.g, e.color.b, e.color.a));

            switch (e.kind) {
                case LegendKind::Line:
                    if (e.swept) {
                        // Colormap along the swatch in 16 short steps (a NanoVG
                        // two-color gradient would leave the colormap).
                        constexpr int kSteps = 16;
                        const uint8_t* lut = colormaps::get(e.cmap);
                        for (int k = 0; k < kSteps; ++k) {
                            const float f0 = static_cast<float>(k) / kSteps;
                            const float f1 = static_cast<float>(k + 1) / kSteps;
                            const int idx = static_cast<int>(
                                ((f0 + f1) * 0.5f) * 255.0f);
                            const uint8_t* c = lut + idx * 4;
                            nvgBeginPath(vg_);
                            nvgMoveTo(vg_, sx0 + (sx1 - sx0) * f0, cy);
                            // Half a pixel of overlap, so steps leave no seam.
                            nvgLineTo(vg_, sx0 + (sx1 - sx0) * f1 + 0.5f, cy);
                            nvgStrokeColor(vg_, nvgRGBAf(c[0] / 255.0f, c[1] / 255.0f,
                                                         c[2] / 255.0f, e.color.a));
                            nvgStrokeWidth(vg_, 2.0f);
                            nvgStroke(vg_);
                        }
                        break;
                    }
                    begin_styled_segment(vg_, sx0, cy, sx1, cy, e.style);
                    nvgStrokeWidth(vg_, 2.0f);
                    nvgStroke(vg_);
                    break;
                case LegendKind::Marker: {
                    // The series' marker shape at a fixed swatch radius (not its
                    // `size`), as matplotlib does.
                    const MarkerShape ms =
                            marker_shape(e.marker, (sx0 + sx1) * 0.5f, cy, 4.5f);
                    // Alpha 0 means no edge (every kind but scatter_z).
                    const bool edged = e.edge.a > 0.0f;
                    auto outline = [&] {
                        if (!edged) return;
                        nvgStrokeColor(vg_, nvgRGBAf(e.edge.r, e.edge.g, e.edge.b, e.edge.a));
                        nvgStrokeWidth(vg_, 1.0f);
                        nvgStroke(vg_);
                    };
                    switch (ms.form) {
                        case MarkerShape::Form::Disc:
                            nvgBeginPath(vg_);
                            nvgCircle(vg_, ms.cx, ms.cy, ms.radius);
                            nvgFill(vg_);
                            outline();
                            break;
                        case MarkerShape::Form::Rect:
                            nvgBeginPath(vg_);
                            nvgRect(vg_, ms.cx - ms.radius, ms.cy - ms.radius,
                                    ms.radius * 2.0f, ms.radius * 2.0f);
                            nvgFill(vg_);
                            outline();
                            break;
                        case MarkerShape::Form::Polygon:
                            nvgBeginPath(vg_);
                            nvgMoveTo(vg_, ms.pts[0][0], ms.pts[0][1]);
                            for (int i = 1; i < ms.count; ++i)
                                nvgLineTo(vg_, ms.pts[i][0], ms.pts[i][1]);
                            nvgClosePath(vg_);
                            nvgFill(vg_);
                            outline();
                            break;
                        case MarkerShape::Form::Strokes:
                            // No interior: an edged one is stroked in the edge color.
                            if (edged)
                                nvgStrokeColor(vg_, nvgRGBAf(e.edge.r, e.edge.g, e.edge.b, e.edge.a));
                            else
                                nvgStrokeColor(vg_, nvgRGBAf(e.color.r, e.color.g, e.color.b, e.color.a));
                            nvgStrokeWidth(vg_, ms.width);
                            for (int i = 0; i + 1 < ms.count; i += 2) {
                                nvgBeginPath(vg_);
                                nvgMoveTo(vg_, ms.pts[i][0], ms.pts[i][1]);
                                nvgLineTo(vg_, ms.pts[i + 1][0], ms.pts[i + 1][1]);
                                nvgStroke(vg_);
                            }
                            break;
                        case MarkerShape::Form::None:
                        default: break;
                    }
                    break;
                }
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
            nvgText(vg_, sx1 + kLegendGap, cy, e.name.c_str(), nullptr);
        }
    }

    void NvgRenderer::draw_colorbar(const ColorbarBox& box, const ColorbarOptions& opts) {
        const PlotRect& r = box.rect;
        const Colormap cmap = box.cmap;
        const float vmin = box.vmin, vmax = box.vmax;

        // NanoVG executes draws at nvgEndFrame(), so a gradient image deleted after
        // nvgFill() would render black; cache it per colormap for the renderer's
        // lifetime. Keyed by orientation too (nvgImagePattern rotates about the
        // corner, not the centre).
        const auto key = std::make_pair(cmap, box.horizontal);
        auto it = colorbar_images_.find(key);
        int image;
        if (it != colorbar_images_.end()) {
            image = it->second;
        } else {
            const uint8_t* lut = colormaps::get(cmap); // 256 RGBA entries, index0=vmin..index255=vmax
            uint8_t img[256 * 4];
            if (box.horizontal) {
                // 256x1: column 0 (left) = vmin.
                std::memcpy(img, lut, sizeof img);
                image = nvgCreateImageRGBA(vg_, 256, 1, NVG_IMAGE_NEAREST, img);
            } else {
                // 1x256: row 0 (top) = vmax, row 255 (bottom) = vmin.
                for (int i = 0; i < 256; ++i)
                    std::memcpy(&img[static_cast<std::size_t>(i) * 4], &lut[(255 - i) * 4], 4);
                image = nvgCreateImageRGBA(vg_, 1, 256, NVG_IMAGE_NEAREST, img);
            }
            colorbar_images_.emplace(key, image);
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
            const int halign = box.num_align == HAlign::Left
                                   ? NVG_ALIGN_LEFT
                                   : box.num_align == HAlign::Right
                                         ? NVG_ALIGN_RIGHT
                                         : NVG_ALIGN_CENTER;
            nvgTextAlign(vg_, halign | NVG_ALIGN_MIDDLE);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(vmax));
            nvgText(vg_, box.vmax_x, box.vmax_y, buf, nullptr);
            std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(vmin));
            nvgText(vg_, box.vmin_x, box.vmin_y, buf, nullptr);

            // The bar's name: rotated beside a vertical bar (as the axis titles),
            // level beyond a horizontal one.
            if (!box.name.empty()) {
                nvgTextAlign(vg_, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                if (box.horizontal) {
                    nvgText(vg_, box.name_x, box.name_y, box.name.c_str(), nullptr);
                } else {
                    nvgSave(vg_);
                    nvgTranslate(vg_, box.name_x, box.name_y);
                    nvgRotate(vg_, -NVG_PI / 2.0f);
                    nvgText(vg_, 0, 0, box.name.c_str(), nullptr);
                    nvgRestore(vg_);
                }
            }
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
        if (opts.align == HAlign::Left) halign = NVG_ALIGN_LEFT;
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
                if (nl == std::string::npos) {
                    lines.push_back(text.substr(start));
                    break;
                }
                lines.push_back(text.substr(start, nl - start));
                start = nl + 1;
            }
            return lines;
        }

        constexpr float kHintPad = 6.0f;
        constexpr float kHintFontSize = 12.0f;
        constexpr float kHintOffset = 12.0f; // offset from the anchor point
    } // namespace

    void NvgRenderer::draw_hint(int fig_w, int fig_h, float anchor_x, float anchor_y,
                                const std::string& text) {
        if (text.empty() || font_ == -1) return;

        const auto lines = split_lines(text);
        nvgFontFaceId(vg_, font_);
        nvgFontSize(vg_, kHintFontSize);

        float max_line_w = 0.0f;
        float line_h = kHintFontSize; {
            float asc, desc, lh;
            nvgTextMetrics(vg_, &asc, &desc, &lh);
            line_h = lh;
        }
        for (const auto& line: lines) {
            float bounds[4];
            nvgTextBounds(vg_, 0, 0, line.c_str(), nullptr, bounds);
            max_line_w = std::max(max_line_w, bounds[2] - bounds[0]);
        }

        BoxSize box{
            kHintPad * 2.0f + max_line_w,
            kHintPad * 2.0f + line_h * static_cast<float>(lines.size())
        };

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
