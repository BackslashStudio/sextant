#include "figure_layout.h"
#include "../text_metrics.h"
#include <algorithm>
#include <cstdio>

namespace sextant {

namespace {

// Widest label in a tick list, at the axes' own label font.
float widest_label(const std::vector<Tick>& ticks, const std::string& font_path, float fontsize) {
    float w = 0.0f;
    for (const auto& t : ticks)
        w = std::max(w, text_width(font_path, fontsize, t.label));
    return w;
}

float half_label_width(const std::vector<Tick>& ticks, std::size_t i,
                       const std::string& font_path, float fontsize) {
    if (i >= ticks.size()) return 0.0f;
    return text_width(font_path, fontsize, ticks[i].label) * 0.5f;
}

std::string colorbar_number(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(v));
    return buf;
}

// Ticks actually drawn for one axes: an explicit override when set, otherwise
// generated. Both paths route through here, which is required rather than
// merely tidy -- the left inset is the width of the widest y label, so layout
// has to measure the labels that will actually be drawn.
std::vector<Tick> ticks_for(const std::optional<std::vector<Tick>>& override_,
                            double lo, double hi) {
    if (override_) return *override_;
    return generate_ticks(lo, hi);
}

// Limits, resolved the same way make_transform() resolves them. Kept here
// because layout needs the tick values before it can size anything, and
// auto_scale() is O(N) in the point count — running it here and again in
// make_transform() would double that cost on every frame.
struct ResolvedLimits { double xmin, xmax, ymin, ymax; };

ResolvedLimits resolve_limits(const RenderSnapshot& snap) {
    ResolvedLimits r{ snap.xmin, snap.xmax, snap.ymin, snap.ymax };
    if (snap.xlim_auto || snap.ylim_auto) {
        const auto b = auto_scale(snap.all());
        if (snap.xlim_auto) { r.xmin = b.xmin; r.xmax = b.xmax; }
        if (snap.ylim_auto) { r.ymin = b.ymin; r.ymax = b.ymax; }
    }
    return r;
}

struct CellPrep {
    ResolvedLimits    limits;
    std::vector<Tick> xticks, yticks;
    float             max_ylabel_w = 0.0f;
};

CellPrep prepare(const RenderSnapshot& snap) {
    CellPrep p;
    p.limits = resolve_limits(snap);
    p.xticks = ticks_for(snap.xticks_override, p.limits.xmin, p.limits.xmax);
    p.yticks = ticks_for(snap.yticks_override, p.limits.ymin, p.limits.ymax);
    p.max_ylabel_w = widest_label(p.yticks, snap.axes_style.font_path,
                                  snap.axes_style.label_fontsize);
    return p;
}

// What one axes needs on each side. compute_uniform_insets() takes the
// per-side maximum of these across the grid.
PlotInsets insets_for(const RenderSnapshot& snap, const CellPrep& prep) {
    const auto& st = snap.axes_style;
    const float label_lh = font_vmetrics(st.font_path, st.label_fontsize).line_height;

    PlotInsets in;

    in.left = st.tick_length + kTickLabelGap + prep.max_ylabel_w;
    if (!snap.ytitle.empty())
        in.left += kTitleGap + font_vmetrics(st.font_path, st.ytitle_fontsize).line_height;

    in.bottom = st.tick_length + kTickLabelGap + label_lh;
    if (!snap.xtitle.empty())
        in.bottom += kTitleGap + font_vmetrics(st.font_path, st.xtitle_fontsize).line_height;

    in.top = snap.title.empty()
        ? 0.0f
        : font_vmetrics(st.font_path, st.title_fontsize).line_height + kTitleGap;

    // The x tick labels at the very ends of the axis are centred on the
    // frame's corners, so half of each hangs outside it. Reserving for that
    // is what stops the first and last number being clipped by the figure
    // edge — the fixed margins used to cover it by being generous.
    in.left  = std::max(in.left,  half_label_width(prep.xticks, 0, st.font_path, st.label_fontsize));
    in.right = std::max(in.right, prep.xticks.empty()
                                    ? 0.0f
                                    : half_label_width(prep.xticks, prep.xticks.size() - 1,
                                                       st.font_path, st.label_fontsize));

    // Likewise the topmost y label straddles the frame's top edge.
    in.top = std::max(in.top, label_lh * 0.5f);

    return in;
}

std::vector<CellPrep> prepare_all(const FigureSnapshot& fsnap) {
    std::vector<CellPrep> prep;
    prep.reserve(fsnap.axes.size());
    for (const auto& fa : fsnap.axes) prep.push_back(prepare(fa.snap));
    return prep;
}

PlotInsets max_insets(const FigureSnapshot& fsnap, const std::vector<CellPrep>& prep) {
    PlotInsets out;
    for (std::size_t i = 0; i < fsnap.axes.size(); ++i) {
        const PlotInsets in = insets_for(fsnap.axes[i].snap, prep[i]);
        out.left   = std::max(out.left,   in.left);
        out.right  = std::max(out.right,  in.right);
        out.top    = std::max(out.top,    in.top);
        out.bottom = std::max(out.bottom, in.bottom);
    }
    return out;
}

} // namespace

std::vector<LegendEntry> collect_legend_entries(const RenderSnapshot& snap) {
    std::vector<LegendEntry> entries;
    for (const auto& lp : snap.lines) {
        // A LineStyle::None line draws no stroke anywhere, so a swatch for it
        // would be a key to something that isn't in the figure.
        if (lp.opts.label.empty() || lp.opts.linestyle == LineStyle::None) continue;
        entries.push_back({ lp.opts.color, lp.opts.label, LegendKind::Line, lp.opts.linestyle });
    }
    for (const auto& sp : snap.scatters)
        if (!sp.opts.label.empty())
            entries.push_back({ sp.opts.color, sp.opts.label, LegendKind::Marker, LineStyle::Solid });
    for (const auto& bp : snap.bars)
        if (!bp.opts.label.empty())
            entries.push_back({ bp.opts.color, bp.opts.label, LegendKind::Bar, LineStyle::Solid });
    return entries;
}

CellDecorations compute_cell_decorations(const RenderSnapshot& snap) {
    CellDecorations d;

    if (const auto req = find_colorbar_request(snap)) {
        const auto& cb = snap.colorbar_opts;
        const float label_w = std::max(
            text_width(cb.font_path, cb.fontsize, colorbar_number(req->vmax)),
            text_width(cb.font_path, cb.fontsize, colorbar_number(req->vmin)));
        d.has_colorbar   = true;
        d.colorbar_cmap  = req->cmap;
        d.colorbar_vmin  = req->vmin;
        d.colorbar_vmax  = req->vmax;
        d.colorbar_block = kColorbarGap + kColorbarWidth + kColorbarLabelGap + label_w;
    }

    if (snap.legend_enabled) {
        d.legend_entries = collect_legend_entries(snap);
        if (!d.legend_entries.empty()) {
            const auto& lo    = snap.legend_opts;
            const float row_h = legend_row_height(lo.fontsize);
            float text_w = 0.0f;
            for (const auto& e : d.legend_entries)
                text_w = std::max(text_w, text_width(lo.font_path, lo.fontsize, e.label));

            d.legend_box_w = kLegendPad * 2.0f + kLegendSwatchW + kLegendGap + text_w;
            d.legend_box_h = kLegendPad * 2.0f
                             + row_h * static_cast<float>(d.legend_entries.size());
            d.legend_block = d.legend_box_w + std::max(0.0f, lo.offset_x);
        }
    }

    return d;
}

LayoutSize figure_size_for_frame(const FigureSnapshot& fsnap, int slot_index,
                                 float frame_w, float frame_h) {
    if (fsnap.axes.empty()) return {};

    const FigureAxesSnapshot* fa = nullptr;
    for (const auto& a : fsnap.axes)
        if (a.slot.index == slot_index) { fa = &a; break; }
    if (!fa) fa = &fsnap.axes.front();

    const int rows = std::max(1, fa->slot.rows);
    const int cols = std::max(1, fa->slot.cols);

    const PlotInsets in    = compute_uniform_insets(fsnap);
    const float      carve = compute_cell_decorations(fa->snap).total_carve();
    const float      band  = suptitle_band_height(fsnap.suptitle, fsnap.suptitle_opts);
    const auto&      m     = fsnap.margins;

    const float cell_w = std::max(kMinFrameSize, frame_w) + in.left + in.right + carve;
    const float cell_h = std::max(kMinFrameSize, frame_h) + in.top  + in.bottom;

    return { cell_w * static_cast<float>(cols) + m.left + m.right
                 + static_cast<float>(cols - 1) * fsnap.col_gap,
             cell_h * static_cast<float>(rows) + band + m.top + m.bottom
                 + static_cast<float>(rows - 1) * fsnap.row_gap };
}

float middle_baseline_offset(const std::string& font_path, float fontsize) {
    // Fontstash's FONS_ALIGN_MIDDLE shifts the baseline by (ascender +
    // descender) / 2 — descender being negative — so this is the distance
    // from a centred anchor down to the baseline SVG wants.
    const auto vm = font_vmetrics(font_path, fontsize);
    return (vm.ascent + vm.descent) * 0.5f;
}

float top_baseline_offset(const std::string& font_path, float fontsize) {
    return font_vmetrics(font_path, fontsize).ascent;
}

PlotInsets compute_uniform_insets(const FigureSnapshot& fsnap) {
    return max_insets(fsnap, prepare_all(fsnap));
}

FigureLayout compute_figure_layout(const FigureSnapshot& fsnap, int fig_w, int fig_h) {
    FigureLayout out;
    out.suptitle_band = suptitle_band_height(fsnap.suptitle, fsnap.suptitle_opts);

    // One prepare() per axes, reused for both the inset pass and the cell
    // pass — it resolves the limits (auto_scale over the whole dataset) and
    // generates the ticks, neither of which should happen twice per frame.
    const std::vector<CellPrep> prep = prepare_all(fsnap);
    out.insets = max_insets(fsnap, prep);

    const auto& m  = fsnap.margins;
    const float fw = static_cast<float>(fig_w), fh = static_cast<float>(fig_h);

    out.cells.reserve(fsnap.axes.size());
    for (std::size_t i = 0; i < fsnap.axes.size(); ++i) {
        const auto& fa   = fsnap.axes[i];
        const auto& snap = fa.snap;
        const int   rows = std::max(1, fa.slot.rows);
        const int   cols = std::max(1, fa.slot.cols);
        const int   row  = (fa.slot.index - 1) / cols;
        const int   col  = (fa.slot.index - 1) % cols;

        // Cell = the whole subplot including its decorations. The gaps are
        // therefore between complete subplots, not between plot frames —
        // that is what a margin means here.
        const float cell_w = (fw - m.left - m.right
                              - static_cast<float>(cols - 1) * fsnap.col_gap)
                             / static_cast<float>(cols);
        const float cell_h = (fh - out.suptitle_band - m.top - m.bottom
                              - static_cast<float>(rows - 1) * fsnap.row_gap)
                             / static_cast<float>(rows);
        const float cell_x = m.left + static_cast<float>(col) * (cell_w + fsnap.col_gap);
        const float cell_y = out.suptitle_band + m.top
                             + static_cast<float>(row) * (cell_h + fsnap.row_gap);

        CellLayout c;
        c.slot   = fa.slot;
        c.xticks = prep[i].xticks;
        c.yticks = prep[i].yticks;

        c.frame = { cell_x + out.insets.left,
                    cell_y + out.insets.top,
                    std::max(kMinFrameSize, cell_w - out.insets.left - out.insets.right),
                    std::max(kMinFrameSize, cell_h - out.insets.top  - out.insets.bottom) };

        // Anchors are taken before the colorbar/legend carve below, so an
        // axes title stays centred on the *cell* rather than sliding left
        // as a legend eats into the frame. Recomputing them afterwards is
        // the obvious alternative and looks wrong: two subplots in a row,
        // one with a legend, would have their titles at different offsets.
        const auto& st = snap.axes_style;
        const float frame_cx = c.frame.x + c.frame.w * 0.5f;

        c.title_x = frame_cx;
        c.title_y = cell_y + font_vmetrics(st.font_path, st.title_fontsize).line_height * 0.5f;

        c.xlabel_top   = c.frame.y + c.frame.h + st.tick_length + kTickLabelGap;
        c.ylabel_right = c.frame.x - st.tick_length - kTickLabelGap;

        const float xtitle_lh = font_vmetrics(st.font_path, st.xtitle_fontsize).line_height;
        c.xtitle_x = frame_cx;
        c.xtitle_y = cell_y + cell_h - xtitle_lh * 0.5f;

        const float ytitle_lh = font_vmetrics(st.font_path, st.ytitle_fontsize).line_height;
        c.ytitle_x = cell_x + ytitle_lh * 0.5f;
        c.ytitle_y = c.frame.y + c.frame.h * 0.5f;

        // Colorbar first, then legend — the order matters only in that both
        // paths must carve in the same one, or an axes with both would place
        // them differently in PNG and SVG. Widths come from
        // compute_cell_decorations(), so the inverse (figure_size_for_frame)
        // subtracts exactly what this subtracts.
        const CellDecorations dec = compute_cell_decorations(snap);

        if (dec.has_colorbar) {
            const float new_w = std::max(kMinFrameSize, c.frame.w - dec.colorbar_block);
            c.colorbar_cmap = dec.colorbar_cmap;
            c.colorbar_vmin = dec.colorbar_vmin;
            c.colorbar_vmax = dec.colorbar_vmax;
            c.colorbar = { c.frame.x + new_w + kColorbarGap, c.frame.y,
                           kColorbarWidth, c.frame.h };
            c.frame.w = new_w;
        }

        if (!dec.legend_entries.empty()) {
            const auto& lo = snap.legend_opts;
            const float new_w = std::max(kMinFrameSize, c.frame.w - dec.legend_block);
            c.legend_entries = dec.legend_entries;
            c.legend = { c.frame.x + new_w + lo.offset_x, c.frame.y + lo.offset_y,
                         dec.legend_box_w, dec.legend_box_h };
            c.frame.w = new_w;
        }

        c.tr = { prep[i].limits.xmin, prep[i].limits.xmax,
                 prep[i].limits.ymin, prep[i].limits.ymax,
                 c.frame.x, c.frame.y, c.frame.w, c.frame.h, fw, fh };

        out.cells.push_back(std::move(c));
    }

    return out;
}

} // namespace sextant
