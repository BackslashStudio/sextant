#include "figure_layout.h"
#include "../axis_placement.h"
#include "../text_metrics.h"
#include <algorithm>
#include <cmath>
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
        // Unpadded, so that an origin component can be folded in before the
        // padding rather than after it (v1.0 step 19). Widening the padded
        // bounds instead would put the pinned value exactly on the frame's
        // edge, where it reads as Low and draws no line of its own -- the
        // opposite of what pinning it asked for. With no pin set the two are
        // arithmetically identical, since auto_scale's degenerate-range
        // fixups all happen before it pads.
        const auto& st = snap.axes_style;
        auto b = auto_scale(snap.all(), 0.0);
        widen_for_origin(st.origin_x, snap.xlim_auto, b.xmin, b.xmax);
        widen_for_origin(st.origin_y, snap.ylim_auto, b.ymin, b.ymax);
        const double dx = (b.xmax - b.xmin) * kAutoScalePad;
        const double dy = (b.ymax - b.ymin) * kAutoScalePad;
        if (snap.xlim_auto) { r.xmin = b.xmin - dx; r.xmax = b.xmax + dx; }
        if (snap.ylim_auto) { r.ymin = b.ymin - dy; r.ymax = b.ymax + dy; }
    }
    return r;
}

struct CellPrep {
    ResolvedLimits    limits;
    std::vector<Tick> xticks, yticks;
    float             max_ylabel_w = 0.0f;

    // Where the two axis lines sit (v1.0 step 19). Resolved here rather than
    // in the cell pass because insets_for() needs them: which band an axis'
    // labels occupy -- bottom, top, or none at all -- is what it reserves.
    AxisPlacement     xaxis, yaxis;

    // 3D only. The z ticks and the data->box transform, resolved here for the
    // same reason the 2D limits are: the cell pass must not redo work the
    // inset pass already did.
    bool              is_3d = false;
    std::vector<Tick> zticks;
    Transform3D       tf3;
};

CellPrep prepare(const RenderSnapshot& snap) {
    CellPrep p;
    p.limits = resolve_limits(snap);
    p.xticks = ticks_for(snap.xticks_override, p.limits.xmin, p.limits.xmax);
    p.yticks = ticks_for(snap.yticks_override, p.limits.ymin, p.limits.ymax);
    p.max_ylabel_w = widest_label(p.yticks, snap.axes_style.font_path,
                                  snap.axes_style.label_fontsize);

    const auto& st = snap.axes_style;
    p.xaxis = place_axis(st.xaxis_y, st.origin_y, p.limits.ymin, p.limits.ymax);
    p.yaxis = place_axis(st.yaxis_x, st.origin_x, p.limits.xmin, p.limits.xmax);
    return p;
}

// The 3D counterpart of resolve_limits(), and here for the same reason: the
// ticks are needed before anything can be sized, and auto_scale3d() is O(N) in
// the bar count, so it must not run once here and again in the cell pass.
// Three axes rather than two, resolved independently -- a caller who pinned z
// and left x and y automatic gets exactly that.
CellPrep prepare(const RenderSnapshot3D& snap) {
    CellPrep p;
    p.is_3d = true;
    double xmin = snap.xmin, xmax = snap.xmax;
    double ymin = snap.ymin, ymax = snap.ymax;
    double zmin = snap.zmin, zmax = snap.zmax;
    // Only when there is something to scale *to*. "Auto" means "from the
    // data", and an axes with no data has none to derive from -- so the
    // declared 0..1 stands rather than becoming the padded -0.05..1.05 an
    // empty auto_scale3d() would hand back. That also keeps an empty box the
    // picture it has been since step 1.
    bool has_data = !snap.bars3d.empty() || !snap.surfaces.empty() ||
                    !snap.scatter3d.empty() || !snap.lines3d.empty() ||
                    !snap.surface_tri.empty();
    for (const auto& pl : snap.planes) has_data = has_data || plane_has_data(pl);
    // An origin component widens automatic limits to reach it, the 3D reading
    // of step 19's rule (v1.0 step 20). Unpadded bounds, re-padded below, for
    // the reason resolve_limits() gives: folded in after the padding, a pinned
    // value would land exactly on a box face, where it reads as Low. The
    // unpadded call is asked for *only* when there is a pin to fold, so a box
    // with none is arithmetically what it was before pins existed.
    const auto& st = snap.axes_style;
    const bool pinned = st.origin_x || st.origin_y || st.origin_z;
    bool scaled = false;
    if (has_data && (snap.xlim_auto || snap.ylim_auto || snap.zlim_auto)) {
        const DataBounds3D b = auto_scale3d(snap.bars3d, snap.planes, snap.surfaces, snap.scatter3d,
                                            snap.lines3d, snap.surface_tri,
                                            pinned ? 0.0 : kAutoScalePad);
        if (snap.xlim_auto) { xmin = b.xmin; xmax = b.xmax; }
        if (snap.ylim_auto) { ymin = b.ymin; ymax = b.ymax; }
        if (snap.zlim_auto) { zmin = b.zmin; zmax = b.zmax; }
        scaled = true;
    }
    if (pinned) {
        struct AutoAxis { bool automatic; const std::optional<double>* pin; double* lo; double* hi; };
        const AutoAxis ax[3] = {
            { snap.xlim_auto, &st.origin_x, &xmin, &xmax },
            { snap.ylim_auto, &st.origin_y, &ymin, &ymax },
            { snap.zlim_auto, &st.origin_z, &zmin, &zmax },
        };
        for (const AutoAxis& a : ax) {
            // A fixed limit is never widened and never padded -- the caller
            // said what range they wanted, and place_axis3d() clamps the pin
            // into it rather than moving it. An axes with no data keeps its
            // declared 0..1 too, unless this is the axis the pin is on: there
            // is nothing to scale to, so padding would move a box that has
            // been the same picture since step 1.
            if (!a.automatic) continue;
            if (!scaled && !a.pin->has_value()) continue;
            widen_for_origin(*a.pin, true, *a.lo, *a.hi);
            const double d = (*a.hi - *a.lo) * kAutoScalePad;
            *a.lo -= d;
            *a.hi += d;
        }
    }
    p.limits = { xmin, xmax, ymin, ymax };
    p.xticks = ticks_for(snap.xticks_override, xmin, xmax);
    p.yticks = ticks_for(snap.yticks_override, ymin, ymax);
    p.zticks = ticks_for(snap.zticks_override, zmin, zmax);
    p.tf3 = { xmin, xmax, ymin, ymax, zmin, zmax, snap.aspect };
    return p;
}

// What one axes' furniture takes around its frame: tick marks, tick labels and
// the x/y titles. The axes title is not here -- it sits above everything the
// cell reserves, legends and colorbars included, so it is its own band (see
// title_band()).
PlotInsets insets_for(const RenderSnapshot& snap, const CellPrep& prep) {
    const auto& st = snap.axes_style;
    const float label_lh = font_vmetrics(st.font_path, st.label_fontsize).line_height;

    PlotInsets in;

    // The tick band -- marks plus their numbers -- goes on whichever side of
    // the frame its axis' labels fall (v1.0 step 19). An interior axis draws
    // its band *inside* the frame and so reserves nothing; a High axis moves
    // the band to the opposite side rather than giving it back. The titles do
    // not move with it, so their bands are added unconditionally below.
    const float x_band = st.tick_length + kTickLabelGap + label_lh;
    const float y_band = st.tick_length + kTickLabelGap + prep.max_ylabel_w;

    if (!prep.xaxis.interior) (prep.xaxis.high ? in.top  : in.bottom) = x_band;
    if (!prep.yaxis.interior) (prep.yaxis.high ? in.right : in.left)  = y_band;

    if (!snap.ytitle.empty())
        in.left += kTitleGap + font_vmetrics(st.font_path, st.ytitle_fontsize).line_height;
    if (!snap.xtitle.empty())
        in.bottom += kTitleGap + font_vmetrics(st.font_path, st.xtitle_fontsize).line_height;

    // The x tick labels at the very ends of the axis are centred on the
    // frame's corners, so half of each hangs outside it. Reserving for that
    // is what stops the first and last number being clipped by the figure
    // edge — the fixed margins used to cover it by being generous.
    //
    // This is independent of where the axis *line* went: a Mid x axis still
    // spans the frame's full width, so its end labels straddle the same two
    // corners they always did.
    in.left  = std::max(in.left,  half_label_width(prep.xticks, 0, st.font_path, st.label_fontsize));
    in.right = std::max(in.right, prep.xticks.empty()
        ? 0.0f
        : half_label_width(prep.xticks, prep.xticks.size() - 1, st.font_path, st.label_fontsize));

    // Likewise the outermost y labels straddle the frame's top and bottom
    // edges. The bottom half only shows when nothing else reserves there --
    // an interior x axis, with no x title -- which is exactly the case the
    // fixed bottom band used to hide.
    in.top    = std::max(in.top,    label_lh * 0.5f);
    in.bottom = std::max(in.bottom, label_lh * 0.5f);

    return in;
}

// A 3D cell's furniture takes nothing around its frame. Tick labels and axis
// titles sit *inside* the frame for 3D, in the fraction Box3DStyle::margin
// holds back. They have to: where a 3D label lands depends on the camera, the
// camera's fit on the frame, and the frame would then depend on the label.
// See spec_3d.md §5 for why that circularity is real where the 2D one only
// looked it.
PlotInsets insets_for(const RenderSnapshot3D&, const CellPrep&) { return {}; }

float title_band(const std::string& title, const AxesStyle& st) {
    return title.empty()
        ? 0.0f
        : font_vmetrics(st.font_path, st.title_fontsize).line_height + kTitleGap;
}

std::vector<CellPrep> prepare_all(const FigureSnapshot& fsnap) {
    std::vector<CellPrep> prep;
    prep.reserve(fsnap.axes.size());
    for (const auto& fa : fsnap.axes)
        prep.push_back(std::visit([](const auto& s) { return prepare(s); }, fa.snap));
    return prep;
}

// Every request measured and priced, in one place: the 2D and 3D bodies below
// differ only in where the requests and the styling come from, and two
// transcriptions of the block arithmetic would put a cell's frame and its
// inverse a few pixels apart.
//
// One ColorbarOptions for every bar of a cell. The numbers beside each bar are
// what its block has to be wide enough for, so a bar explaining 0..1 is
// narrower than one explaining 0..1000 and the blocks are summed rather than
// multiplied out from one width. Beside a horizontal bar the numbers sit on one
// line under or over it, so there they cost a line height, not a width.
void fill_colorbars(CellDecorations& d, const std::vector<ColorbarRequest>& reqs,
                    const ColorbarOptions& cb) {
    const bool  horizontal = cb.anchor == ColorbarAnchor::Top
                          || cb.anchor == ColorbarAnchor::Bottom;
    const float lh = font_vmetrics(cb.font_path, cb.fontsize).line_height;
    d.colorbars.reserve(reqs.size());
    for (const ColorbarRequest& r : reqs) {
        const float num = horizontal
            ? lh
            : std::max(text_width(cb.font_path, cb.fontsize, colorbar_number(r.vmax)),
                       text_width(cb.font_path, cb.fontsize, colorbar_number(r.vmin)));
        // The name costs a *line height*, not a text width, beside a vertical
        // bar too: it is drawn rotated along the bar, so its length runs down
        // the frame -- which is also why a long one needs no more room than a
        // short one, and why it goes outboard of the numbers rather than over
        // the bar, where it would collide with the next bar along.
        const float name = r.name.empty() ? 0.0f : kColorbarLabelGap + lh;
        const float block = std::max(0.0f, cb.margin) + std::max(0.0f, cb.width)
                          + kColorbarLabelGap + num + name;
        d.colorbars.push_back({ r.cmap, r.vmin, r.vmax, r.name, block });
        d.colorbar_block += block;
    }
}

void fill_legend(CellDecorations& d, std::vector<LegendEntry> entries, const LegendOptions& lo) {
    d.legend_entries = std::move(entries);
    if (d.legend_entries.empty()) return;
    const float row_h = legend_row_height(lo.fontsize);
    float text_w = 0.0f;
    d.legend_entry_w.reserve(d.legend_entries.size());
    for (const auto& e : d.legend_entries) {
        const float w = text_width(lo.font_path, lo.fontsize, e.name);
        text_w = std::max(text_w, w);
        d.legend_entry_w.push_back(kLegendSwatchW + kLegendGap + w);
    }
    d.legend_box_w = kLegendPad * 2.0f + kLegendSwatchW + kLegendGap + text_w;
    d.legend_box_h = kLegendPad * 2.0f + row_h * static_cast<float>(d.legend_entries.size());
    d.legend_block = std::max(0.0f, lo.margin) + d.legend_box_w;
}

// ---------------------------------------------------------------------------
// The grid solve
// ---------------------------------------------------------------------------
DecorSide legend_side(LegendAnchor a) {
    switch (a) {
        case LegendAnchor::OutsideTL: case LegendAnchor::OutsideTR: return DecorSide::Top;
        case LegendAnchor::OutsideBL: case LegendAnchor::OutsideBR: return DecorSide::Bottom;
        case LegendAnchor::OutsideLT: case LegendAnchor::OutsideLB: return DecorSide::Left;
        case LegendAnchor::OutsideRT: case LegendAnchor::OutsideRB: return DecorSide::Right;
        default: return DecorSide::None;
    }
}

DecorSide colorbar_side(ColorbarAnchor a) {
    switch (a) {
        case ColorbarAnchor::Left:   return DecorSide::Left;
        case ColorbarAnchor::Top:    return DecorSide::Top;
        case ColorbarAnchor::Bottom: return DecorSide::Bottom;
        default:                     return DecorSide::Right;
    }
}

CellMeasure measure(const FigureAxesSnapshot& fa, const CellPrep& prep) {
    CellMeasure m;
    m.slot = fa.slot;
    m.is3d = fa.snap3d() != nullptr;
    std::visit([&](const auto& s) {
        m.axis         = insets_for(s, prep);
        m.title        = title_band(s.title, s.axes_style);
        m.frame_margin = std::max(0.0f, s.axes_style.frame_margin);
        m.dec          = compute_cell_decorations(s);
        m.legend_margin   = std::max(0.0f, s.legend_opts.margin);
        m.legend_fontsize = s.legend_opts.fontsize;
        if (!m.dec.legend_entries.empty()) m.legend = legend_side(s.legend_opts.anchor);
        m.bars = colorbar_side(s.colorbar_opts.anchor);
    }, fa.snap);
    return m;
}

// The grid is one shape for every slot (find_or_reserve_slot() enforces it),
// so any slot's rows/cols is the grid's.
//
// What lines up across the grid, and only that: 2D axis furniture per column
// (left/right) and per row (top/bottom), so axis titles and frames line up the
// way they always have, and the axes title band per row. A 3D cell adds no
// furniture of its own -- its labels are inside its frame -- but still takes
// its row's and column's, so its frame lines up with its neighbours'; only a
// 3D cell with no 2D cell in its row or column gets the space back.
//
// Nothing a cell opts into by itself is here -- its legend, its colorbars and
// its frame_margin are carved out of that cell alone, so styling one subplot
// can never move another.
GridMeasure solve_grid(const std::vector<CellMeasure>& ms) {
    GridMeasure g;
    if (!ms.empty()) {
        g.rows = std::max(1, ms.front().slot.rows);
        g.cols = std::max(1, ms.front().slot.cols);
    }
    g.ax_l.assign(g.cols, 0.0f); g.ax_r.assign(g.cols, 0.0f);
    g.ax_t.assign(g.rows, 0.0f); g.ax_b.assign(g.rows, 0.0f);
    g.title.assign(g.rows, 0.0f);

    for (const CellMeasure& m : ms) {
        const AxesSlot& s = m.slot;
        g.title[s.row0()] = std::max(g.title[s.row0()], m.title);
        if (m.is3d) continue;
        g.ax_l[s.col0()] = std::max(g.ax_l[s.col0()], m.axis.left);
        g.ax_r[s.col1()] = std::max(g.ax_r[s.col1()], m.axis.right);
        g.ax_t[s.row0()] = std::max(g.ax_t[s.row0()], m.axis.top);
        g.ax_b[s.row1()] = std::max(g.ax_b[s.row1()], m.axis.bottom);
    }
    return g;
}

float aligned(const std::vector<float>& v, bool is3d, int i) { return is3d ? 0.0f : v[i]; }

FigureMeasure measure_with(const FigureSnapshot& fsnap, const std::vector<CellPrep>& prep,
                           const FigureMeasure* frozen) {
    FigureMeasure out;
    out.suptitle_band = suptitle_band_height(fsnap.suptitle, fsnap.suptitle_opts);
    out.cells.reserve(fsnap.axes.size());
    for (std::size_t i = 0; i < fsnap.axes.size(); ++i) {
        CellMeasure m = measure(fsnap.axes[i], prep[i]);
        if (frozen && !m.is3d)
            for (const CellMeasure& f : frozen->cells)
                if (f.slot.index == m.slot.index && !f.is3d) { m.axis = f.axis; break; }
        out.cells.push_back(std::move(m));
    }
    out.grid = solve_grid(out.cells);
    return out;
}

// One axis of the grid: each track's position and length, the extent left
// after the outer margins and the gaps shared out by weight (v1.0 step 15.3;
// equal shares before it).
void tracks_1d(float extent, float lead, float trail, float gap, const std::vector<float>& w,
               std::vector<float>& pos, std::vector<float>& len) {
    const int n = static_cast<int>(w.size());
    float total = 0.0f;
    for (float x : w) total += x;
    const float unit = (extent - lead - trail - static_cast<float>(n - 1) * gap) / total;
    pos.assign(n, 0.0f);
    len.assign(n, 0.0f);
    float before = 0.0f;
    for (int k = 0; k < n; ++k) {
        pos[k] = lead + before * unit + static_cast<float>(k) * gap;
        len[k] = w[k] * unit;
        before += w[k];
    }
}

float weight_sum(const std::vector<float>& w, int first, int last) {
    float s = 0.0f;
    for (int k = first; k <= last; ++k) s += w[k];
    return s;
}

// The extended frame's width for a frame `frame_w` wide.
float extended_width(const GridMeasure& g, const CellMeasure& m, const AxesSlot& s, float frame_w) {
    return frame_w + aligned(g.ax_l, m.is3d, s.col0()) + aligned(g.ax_r, m.is3d, s.col1())
           + 2.0f * m.frame_margin;
}

LegendRows legend_rows_for(const CellMeasure& m, float extended_w) {
    if (m.dec.legend_entries.empty()) return {};
    if (m.legend == DecorSide::Top || m.legend == DecorSide::Bottom)
        return layout_legend_rows(m.dec, m.legend_fontsize, extended_w - 2.0f * m.legend_margin);
    // Every other anchor is one column, however wide the frame is.
    LegendRows out;
    const float row_h = legend_row_height(m.legend_fontsize);
    out.w = m.dec.legend_box_w;
    out.h = m.dec.legend_box_h;
    out.slots.reserve(m.dec.legend_entries.size());
    for (std::size_t k = 0; k < m.dec.legend_entries.size(); ++k)
        out.slots.push_back({ kLegendPad,
                              kLegendPad + row_h * static_cast<float>(k) + row_h * 0.5f });
    return out;
}

// One cell's space between its edge and its frame, left and right. Nothing on
// either side depends on the figure's size.
void reserve_horizontal(const GridMeasure& g, const CellMeasure& m, const AxesSlot& s, PlotInsets& res) {
    res.left  = g.ax_l[s.col0()] + m.frame_margin;
    res.right = g.ax_r[s.col1()] + m.frame_margin;
    if (m.legend == DecorSide::Left)  res.left  += m.dec.legend_block;
    if (m.legend == DecorSide::Right) res.right += m.dec.legend_block;
    if (m.bars == DecorSide::Left)    res.left  += m.dec.colorbar_block;
    if (m.bars == DecorSide::Right)   res.right += m.dec.colorbar_block;
}

// Top and bottom, given the cell's own frame width -- the only size-dependent
// input, through a legend wrapped into rows above or below it.
void reserve_vertical(const GridMeasure& g, const CellMeasure& m, const AxesSlot& s, float frame_w,
                      PlotInsets& res, LegendRows& rows) {
    rows = legend_rows_for(m, extended_width(g, m, s, frame_w));
    res.top    = g.title[s.row0()] + g.ax_t[s.row0()] + m.frame_margin;
    res.bottom = g.ax_b[s.row1()] + m.frame_margin;
    if (m.legend == DecorSide::Top)    res.top    += m.legend_margin + rows.h;
    if (m.legend == DecorSide::Bottom) res.bottom += m.legend_margin + rows.h;
    if (m.bars == DecorSide::Top)      res.top    += m.dec.colorbar_block;
    if (m.bars == DecorSide::Bottom)   res.bottom += m.dec.colorbar_block;
}


} // namespace

LegendRows layout_legend_rows(const CellDecorations& dec, float fontsize, float max_w) {
    LegendRows out;
    if (dec.legend_entries.empty()) return out;
    const float row_h = legend_row_height(fontsize);
    float cur = 0.0f, content_w = 0.0f;
    int   row = 0;
    out.slots.reserve(dec.legend_entries.size());
    for (std::size_t k = 0; k < dec.legend_entries.size(); ++k) {
        const float ew = k < dec.legend_entry_w.size() ? dec.legend_entry_w[k] : 0.0f;
        if (cur > 0.0f && cur + kLegendColGap + ew + 2.0f * kLegendPad > max_w) {
            ++row;
            cur = 0.0f;
        }
        const float x = cur > 0.0f ? cur + kLegendColGap : 0.0f;
        out.slots.push_back({ kLegendPad + x,
                              kLegendPad + row_h * static_cast<float>(row) + row_h * 0.5f });
        cur = x + ew;
        content_w = std::max(content_w, cur);
    }
    out.w = kLegendPad * 2.0f + content_w;
    out.h = kLegendPad * 2.0f + row_h * static_cast<float>(row + 1);
    return out;
}

std::vector<LegendEntry> collect_legend_entries(const RenderSnapshot& snap) {
    std::vector<LegendEntry> entries;
    for (const auto& lp : snap.lines) {
        // A LineStyle::None line draws no stroke anywhere, so a swatch for it
        // would be a key to something that isn't in the figure.
        if (!lp.opts.show_legend || lp.opts.name.empty()
            || lp.opts.linestyle == LineStyle::None) continue;
        entries.push_back({ lp.opts.color, lp.opts.name, LegendKind::Line, lp.opts.linestyle });
    }
    for (const auto& sp : snap.scatters)
        if (sp.opts.show_legend && !sp.opts.name.empty())
            entries.push_back({ sp.opts.color, sp.opts.name, LegendKind::Marker,
                                LineStyle::Solid, sp.opts.marker });
    // `scatter_z` was absent here until v1.0 step 11.5, because it had no
    // name field at all: the argument was that a continuous-colour series is
    // keyed by the colorbar it asks for, since a swatch could only show one of
    // its colours. Half of that still holds and is why its key is **white with
    // a black edge** rather than a colour -- the key says which *shape* is
    // which series, and the bar beside it says what the colours mean. What did
    // not hold is the conclusion: with several series on one axes, an unkeyed
    // one is unidentifiable, and a colorbar names a scale rather than a series.
    for (const auto& zp : snap.scatter_z)
        if (zp.opts.show_legend && !zp.opts.name.empty())
            entries.push_back({ Color::White, zp.opts.name, LegendKind::Marker,
                                LineStyle::Solid, zp.opts.marker, Color::Black });
    for (const auto& bp : snap.bars)
        if (bp.opts.show_legend && !bp.opts.name.empty())
            entries.push_back({ bp.opts.color, bp.opts.name, LegendKind::Bar, LineStyle::Solid });
    return entries;
}

// The 3D overload: a 3D axes' keys are its own kinds' first and then its
// planes', each plane in order and then in each plane's own kind order. One
// list rather than one per plane, because a legend keys the *cell* -- so the
// plane half is the same function asked about each plane's sheet, not a second
// enumeration of the kinds.
//
// The axes' own before the planes' (v1.0 step 11.5, with the axes-own arm
// itself): a plane is decoration carried into someone else's scene. The
// colorbar lookup orders itself the same way and for the same reason.
//
// A `surface` is keyed only when it is *flat*: a colormapped one has no one
// colour a swatch could show and is explained by the bar it asks for instead,
// so `colormap` is a third gate on top of the two every kind has. The same
// `name` serves whichever of the two applies, which is what keeps a caller
// from having to know which one their surface will get.
std::vector<LegendEntry> collect_legend_entries(const RenderSnapshot3D& snap) {
    std::vector<LegendEntry> entries;
    for (const auto& bp : snap.bars3d)
        if (bp.opts.show_legend && !bp.opts.name.empty())
            entries.push_back({ bp.opts.color, bp.opts.name, LegendKind::Bar, LineStyle::Solid });
    for (const auto& sp : snap.surfaces)
        if (sp.opts.show_legend && !sp.opts.name.empty() && !sp.opts.colormap)
            entries.push_back({ sp.opts.color, sp.opts.name, LegendKind::Bar, LineStyle::Solid });
    // A cloud, keyed by the *shape* either way and by the colour only when it
    // has one. The two forms are exactly the two `scatter_z` established in
    // 11.5, reused rather than re-derived: a flat series is a swatch of its
    // own colour, and a colormapped one is the marker filled white with a
    // black edge, because a continuous-colour series has no one colour a
    // swatch could honestly show. What the key says there is which shape is
    // which series; the bar beside it says what the colours mean.
    //
    // Unlike a surface, a colormapped cloud is *not* dropped from the legend:
    // a sheet has a shape in the picture to recognize it by, and a cloud has
    // only its marker.
    for (const auto& sc : snap.scatter3d)
        if (sc.opts.show_legend && !sc.opts.name.empty()
            && sc.opts.marker != MarkerStyle::None)
            entries.push_back({ sc.colormapped() ? Color::White : sc.opts.color,
                                sc.opts.name, LegendKind::Marker, LineStyle::Solid,
                                sc.opts.marker,
                                sc.colormapped() ? Color::Black
                                                 : Color{ 0.0f, 0.0f, 0.0f, 0.0f } });
    // A path, keyed by a stroke either way -- its own colour when it is flat,
    // and the colormap swept along the stroke when it is not. See
    // LegendEntry::swept for why a line's colormapped key differs from a
    // marker's, and why it is keyed at all where a colormapped surface is not.
    //
    // No `linestyle` gate of the kind a 2D line has: a 3D path cannot be
    // LineStyle::None because it has no LineStyle at all (spec_3d.md §4 --
    // a world-space ribbon has no pixel arc length to dash along).
    for (const auto& lp : snap.lines3d)
        if (lp.opts.show_legend && !lp.opts.name.empty() && lp.opts.linewidth > 0.0f)
            entries.push_back({ lp.opts.color, lp.opts.name, LegendKind::Line,
                                LineStyle::Solid, MarkerStyle::Circle,
                                Color{ 0.0f, 0.0f, 0.0f, 0.0f },
                                lp.colormapped(), lp.opts.cmap });
    // A mesh, keyed on a *surface's* terms and deliberately not on a path's,
    // which is the pair most easily copied wrong -- the two rules now sit a
    // few lines apart in this very function. A flat mesh has exactly one
    // colour, so a swatch is the whole truth about it; a colormapped one has
    // no one colour a swatch could honestly show, and unlike a path it *has* a
    // shape in the picture to be recognized by (§7c), so it drops out of the
    // legend and is explained by the bar it asks for instead.
    for (const auto& sm : snap.surface_tri)
        if (sm.opts.show_legend && !sm.opts.name.empty() && !sm.colormapped())
            entries.push_back({ sm.opts.color, sm.opts.name, LegendKind::Bar,
                                LineStyle::Solid });
    for (const auto& pl : snap.planes) {
        auto one = collect_legend_entries(pl.sheet);
        entries.insert(entries.end(), one.begin(), one.end());
    }
    return entries;
}

CellDecorations compute_cell_decorations(const RenderSnapshot& snap) {
    CellDecorations d;
    fill_colorbars(d, find_colorbar_requests(snap), snap.colorbar_opts);
    if (snap.legend_enabled)
        fill_legend(d, collect_legend_entries(snap), snap.legend_opts);
    return d;
}

// Decoration hoisting (spec_3d.md §6). A colorbar asked for by a heatmap on a
// plane belongs to the *cell*, not to the plane: it is measured and drawn in
// pixels beside the frame, so a bar put in the scene would be foreshortened,
// would turn with the camera, and would be occluded by the very geometry it
// explains. Carving it here means the projector fits the box into what is left
// -- the box shrinks to make room, exactly as a 2D frame does -- and nothing
// below the layout ever learns a plane asked for one.
//
// The block width is computed by the same arithmetic as the 2D overload, and
// deliberately so: the inverse (figure_size_for_frame) subtracts what this
// adds, and two transcriptions of "gap + bar + label gap + widest number"
// would put a 3D cell's frame and its inverse a few pixels apart.
//
// The legend is hoisted on exactly the same terms, from step 6 -- once there
// were kinds on a plane that could produce an entry. Its keys come from every
// plane in order through the same collect_legend_entries(), and its box is
// measured and carved by the same arithmetic as a 2D axes'. The switch is the
// *axes'* own `legend_enabled` rather than a per-plane flag, because a legend
// keys the cell: two planes asking for two boxes is not a thing a figure can
// lay out.
//
// What is *not* hoisted, since step 11.2, is either one's styling: both come
// from the axes, exactly as a 2D cell's do. A colorbar's used to be read off
// whichever plane had asked for it, which could only ever be the default (a
// Plane2D has no set_colorbar_style()) and had nothing to answer with once a
// bar could be requested by something that is not on a plane.
CellDecorations compute_cell_decorations(const RenderSnapshot3D& snap) {
    CellDecorations d;
    fill_colorbars(d, find_colorbar_requests(snap), snap.colorbar_opts);
    if (snap.legend_enabled)
        fill_legend(d, collect_legend_entries(snap), snap.legend_opts);
    return d;
}

FigureMeasure measure_figure(const FigureSnapshot& fsnap, const FigureMeasure* frozen) {
    return measure_with(fsnap, prepare_all(fsnap), frozen);
}

bool measure_fits(const FigureMeasure& m, const FigureSnapshot& fsnap) {
    if (m.cells.size() != fsnap.axes.size()) return false;
    for (std::size_t i = 0; i < m.cells.size(); ++i) {
        const CellMeasure&        c  = m.cells[i];
        const FigureAxesSnapshot& fa = fsnap.axes[i];
        if (c.slot.index != fa.slot.index || c.slot.last != fa.slot.last
            || c.slot.rows != fa.slot.rows || c.slot.cols != fa.slot.cols
            || c.is3d != fa.is_3d())
            return false;
        // Counts only, which is what the drawing indexes by. Names and scales
        // are measured contents; a changed one lays out with the stored sizes
        // until the next refit, the same as a changed tick label.
        const bool counts_match = std::visit([&](const auto& s) {
            const std::size_t entries = s.legend_enabled ? collect_legend_entries(s).size() : 0;
            return entries == c.dec.legend_entries.size()
                && find_colorbar_requests(s).size() == c.dec.colorbars.size();
        }, fa.snap);
        if (!counts_match) return false;
    }
    return true;
}

LayoutSize figure_size_for_frame(const FigureSnapshot& fsnap, int slot_index,
                                 float frame_w, float frame_h) {
    if (fsnap.axes.empty()) return {};
    return figure_size_for_frame(fsnap, measure_figure(fsnap), slot_index, frame_w, frame_h);
}

LayoutSize figure_size_for_frame(const FigureSnapshot& fsnap, const FigureMeasure& measure,
                                 int slot_index, float frame_w, float frame_h) {
    if (fsnap.axes.empty()) return {};
    if (!measure_fits(measure, fsnap))
        return figure_size_for_frame(fsnap, slot_index, frame_w, frame_h);

    const std::vector<CellMeasure>& ms = measure.cells;
    std::size_t si = 0;
    for (std::size_t i = 0; i < ms.size(); ++i)
        if (ms[i].slot.index == slot_index) { si = i; break; }
    const AxesSlot& s = ms[si].slot;

    const GridMeasure& g    = measure.grid;
    const float        band = measure.suptitle_band;
    const auto&        m    = fsnap.margins;

    // How many grid cells the slot covers each way; a span's cell takes in
    // the gaps between the cells it covers, which the forward pass adds back.
    // Clamped because gaps wider than the span itself leave no cell size that
    // reaches it.
    const int span_c = s.col1() - s.col0() + 1;
    const int span_r = s.row1() - s.row0() + 1;

    // The slot's own reservation. Its frame width is the one asked for, so a
    // legend wrapped above or below it is exactly as tall as it will be.
    const float fw_req = std::max(kMinFrameSize, frame_w);
    PlotInsets res;
    LegendRows rows;
    reserve_horizontal(g, ms[si], s, res);
    reserve_vertical(g, ms[si], s, fw_req, res, rows);

    // A span's tracks share its length by weight, so the length of one unit
    // of weight follows from the span, and the figure is that unit times all
    // the weights (v1.0 step 15.3; with equal weights, a cell's size times
    // the track count, as before).
    const std::vector<float> wc = grid_weights(fsnap.col_ratios, g.cols);
    const std::vector<float> wr = grid_weights(fsnap.row_ratios, g.rows);

    const float span_w = fw_req + res.left + res.right;
    const float unit_w = std::max(0.0f, (span_w - static_cast<float>(span_c - 1) * fsnap.col_gap)
                                        / weight_sum(wc, s.col0(), s.col1()));
    const float fig_w  = unit_w * weight_sum(wc, 0, g.cols - 1) + m.left + m.right
                         + static_cast<float>(g.cols - 1) * fsnap.col_gap;

    const float span_h = std::max(kMinFrameSize, frame_h) + res.top + res.bottom;
    const float unit_h = std::max(0.0f, (span_h - static_cast<float>(span_r - 1) * fsnap.row_gap)
                                        / weight_sum(wr, s.row0(), s.row1()));
    return { fig_w,
             unit_h * weight_sum(wr, 0, g.rows - 1) + band + m.top + m.bottom
                 + static_cast<float>(g.rows - 1) * fsnap.row_gap };
}

std::vector<float> grid_weights(const std::vector<float>& ratios, int n) {
    n = std::max(1, n);
    if (static_cast<int>(ratios.size()) == n) {
        bool ok = true;
        for (float r : ratios) ok = ok && std::isfinite(r) && r > 0.0f;
        if (ok) return ratios;
    }
    return std::vector<float>(static_cast<std::size_t>(n), 1.0f);
}

GridTracks grid_tracks(const FigureSnapshot& fsnap, float suptitle_band, int fig_w, int fig_h) {
    int rows = 1, cols = 1;
    if (!fsnap.axes.empty()) {
        rows = std::max(1, fsnap.axes.front().slot.rows);
        cols = std::max(1, fsnap.axes.front().slot.cols);
    }
    const auto& m = fsnap.margins;
    GridTracks t;
    tracks_1d(static_cast<float>(fig_w), m.left, m.right, fsnap.col_gap,
              grid_weights(fsnap.col_ratios, cols), t.col_x, t.col_w);
    tracks_1d(static_cast<float>(fig_h), suptitle_band + m.top, m.bottom, fsnap.row_gap,
              grid_weights(fsnap.row_ratios, rows), t.row_y, t.row_h);
    return t;
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

namespace {

// The legend's box and each entry's swatch, in figure pixels, for the anchor
// the cell asked for. `mg` is the box's distance from the anchor corner on
// both axes; the offset is added last and moves the box only.
void place_legend(CellLayout& c, const LegendOptions& lo, const LegendRows& rows) {
    const PlotRect& f = c.frame;
    const PlotRect& e = c.extended;
    const float mg = std::max(0.0f, lo.margin);
    const float w = rows.w, h = rows.h;
    float x = 0.0f, y = 0.0f;
    switch (lo.anchor) {
        case LegendAnchor::InsideTL:  x = f.x + mg;             y = f.y + mg;             break;
        case LegendAnchor::InsideTR:  x = f.x + f.w - mg - w;   y = f.y + mg;             break;
        case LegendAnchor::InsideBL:  x = f.x + mg;             y = f.y + f.h - mg - h;   break;
        case LegendAnchor::InsideBR:  x = f.x + f.w - mg - w;   y = f.y + f.h - mg - h;   break;
        case LegendAnchor::OutsideTL: x = e.x + mg;             y = e.y - mg - h;         break;
        case LegendAnchor::OutsideTR: x = e.x + e.w - mg - w;   y = e.y - mg - h;         break;
        case LegendAnchor::OutsideBL: x = e.x + mg;             y = e.y + e.h + mg;       break;
        case LegendAnchor::OutsideBR: x = e.x + e.w - mg - w;   y = e.y + e.h + mg;       break;
        case LegendAnchor::OutsideLT: x = e.x - mg - w;         y = e.y + mg;             break;
        case LegendAnchor::OutsideLB: x = e.x - mg - w;         y = e.y + e.h - mg - h;   break;
        case LegendAnchor::OutsideRT: x = e.x + e.w + mg;       y = e.y + mg;             break;
        case LegendAnchor::OutsideRB: x = e.x + e.w + mg;       y = e.y + e.h - mg - h;   break;
    }
    x += lo.offset_x;
    y += lo.offset_y;
    c.legend = { x, y, w, h };
    c.legend_slots.reserve(rows.slots.size());
    for (const LegendSlot& s : rows.slots) c.legend_slots.push_back({ x + s.x, y + s.cy });
}

// Each bar in its block, outward from `start` -- the extended frame's edge, or
// the far edge of a legend on the same side. Blocks come from
// compute_cell_decorations(), so the forward and inverse directions agree on
// every one of them.
void place_colorbars(CellLayout& c, const CellDecorations& dec, const ColorbarOptions& cbo,
                     DecorSide side, float start) {
    const PlotRect& f  = c.frame;
    const float     mg = std::max(0.0f, cbo.margin);
    const float     bw = std::max(0.0f, cbo.width);
    const float     lh = font_vmetrics(cbo.font_path, cbo.fontsize).line_height;
    const float     ox = cbo.offset_x, oy = cbo.offset_y;

    float s = start;
    c.colorbars.reserve(dec.colorbars.size());
    for (const ColorbarSpec& spec : dec.colorbars) {
        ColorbarBox b;
        b.cmap = spec.cmap; b.vmin = spec.vmin; b.vmax = spec.vmax;
        b.name = spec.name;
        switch (side) {
            case DecorSide::Left:
                b.rect = { s - mg - bw, f.y, bw, f.h };
                b.num_align = HAlign::Right;
                b.vmin_x = b.vmax_x = b.rect.x - kColorbarLabelGap;
                b.vmax_y = f.y;  b.vmin_y = f.y + f.h;
                // Flush against the outer edge of this bar's own block, half a
                // line in from it, and centred down the frame -- so the name
                // sits outboard of the numbers and inboard of the next bar.
                b.name_x = s - spec.block + lh * 0.5f;
                b.name_y = f.y + f.h * 0.5f;
                s -= spec.block;
                break;
            case DecorSide::Top:
                b.rect = { f.x, s - mg - bw, f.w, bw };
                b.horizontal = true;
                b.num_align = HAlign::Center;
                b.vmin_x = f.x;  b.vmax_x = f.x + f.w;
                b.vmin_y = b.vmax_y = b.rect.y - kColorbarLabelGap - lh * 0.5f;
                b.name_x = f.x + f.w * 0.5f;
                b.name_y = s - spec.block + lh * 0.5f;
                s -= spec.block;
                break;
            case DecorSide::Bottom:
                b.rect = { f.x, s + mg, f.w, bw };
                b.horizontal = true;
                b.num_align = HAlign::Center;
                b.vmin_x = f.x;  b.vmax_x = f.x + f.w;
                b.vmin_y = b.vmax_y = b.rect.y + bw + kColorbarLabelGap + lh * 0.5f;
                b.name_x = f.x + f.w * 0.5f;
                b.name_y = s + spec.block - lh * 0.5f;
                s += spec.block;
                break;
            default:   // Right
                b.rect = { s + mg, f.y, bw, f.h };
                b.num_align = HAlign::Left;
                b.vmin_x = b.vmax_x = b.rect.x + bw + kColorbarLabelGap;
                b.vmax_y = f.y;  b.vmin_y = f.y + f.h;
                b.name_x = s + spec.block - lh * 0.5f;
                b.name_y = f.y + f.h * 0.5f;
                s += spec.block;
                break;
        }
        // The name is placed from the block rather than from the numbers'
        // measured size, so the text lands where the space was reserved, which
        // is the only way the two can be checked against each other at all.
        // An unnamed bar reserved no room for one and gets no anchor.
        b.rect.x += ox;  b.rect.y += oy;
        b.vmin_x += ox;  b.vmin_y += oy;
        b.vmax_x += ox;  b.vmax_y += oy;
        if (b.name.empty()) { b.name_x = 0.0f; b.name_y = 0.0f; }
        else                { b.name_x += ox;  b.name_y += oy; }
        c.colorbars.push_back(std::move(b));
    }
}


// The layout proper: stored measurements and this frame's resolved limits and
// ticks, at one size.
FigureLayout layout_with(const FigureSnapshot& fsnap, const std::vector<CellPrep>& prep,
                         const FigureMeasure& measure, int fig_w, int fig_h) {
    FigureLayout out;
    out.suptitle_band = measure.suptitle_band;

    const float fw = static_cast<float>(fig_w), fh = static_cast<float>(fig_h);

    const std::vector<CellMeasure>& ms = measure.cells;
    const GridMeasure&              g  = measure.grid;
    const GridTracks                t  = grid_tracks(fsnap, out.suptitle_band, fig_w, fig_h);

    out.cells.reserve(fsnap.axes.size());
    for (std::size_t i = 0; i < fsnap.axes.size(); ++i) {
        const auto&        fa = fsnap.axes[i];
        const CellMeasure& me = ms[i];
        const AxesSlot&    s  = fa.slot;

        // Cell = the whole subplot including its decorations. The gaps are
        // therefore between complete subplots, not between plot frames —
        // that is what a margin means here. A span runs from its first cell's
        // left/top edge to its last cell's right/bottom one, so it takes in
        // the gaps between them.
        // A track's width is its column's weighted share, and its height its
        // row's (v1.0 step 15.3).
        const float cx = t.col_x[s.col0()];
        const float cy = t.row_y[s.row0()];
        const float cw = t.col_x[s.col1()] + t.col_w[s.col1()] - cx;
        const float ch = t.row_y[s.row1()] + t.row_h[s.row1()] - cy;

        CellLayout c;
        c.slot = s;
        c.cell = { cx, cy, cw, ch };

        // Widths first: nothing on the left or right of a frame depends on
        // the figure's size. Heights second, since a legend wrapped into rows
        // above or below the frame is as tall as the frame's width makes it.
        LegendRows rows;
        reserve_horizontal(g, me, s, c.reserved);
        const float frame_w = std::max(kMinFrameSize, cw - c.reserved.left - c.reserved.right);
        reserve_vertical(g, me, s, frame_w, c.reserved, rows);
        c.frame = { cx + c.reserved.left, cy + c.reserved.top, frame_w,
                    std::max(kMinFrameSize, ch - c.reserved.top - c.reserved.bottom) };

        const float ax_l = aligned(g.ax_l, me.is3d, s.col0());
        const float ax_r = aligned(g.ax_r, me.is3d, s.col1());
        const float ax_t = aligned(g.ax_t, me.is3d, s.row0());
        const float ax_b = aligned(g.ax_b, me.is3d, s.row1());
        const float fm   = me.frame_margin;
        c.extended = { c.frame.x - ax_l - fm, c.frame.y - ax_t - fm,
                       c.frame.w + ax_l + ax_r + 2.0f * fm, c.frame.h + ax_t + ax_b + 2.0f * fm };

        const AxesStyle& st = std::visit(
            [](const auto& sn) -> const AxesStyle& { return sn.axes_style; }, fa.snap);
        const LegendOptions& lo = std::visit(
            [](const auto& sn) -> const LegendOptions& { return sn.legend_opts; }, fa.snap);
        const ColorbarOptions& cbo = std::visit(
            [](const auto& sn) -> const ColorbarOptions& { return sn.colorbar_opts; }, fa.snap);

        // The axes title titles a 3D cell exactly as it titles a 2D one: at the
        // top of the cell, above whatever the cell reserves, so titles line up
        // across a row; centred on the frame it titles.
        const float frame_cx = c.frame.x + c.frame.w * 0.5f;
        c.title_x = frame_cx;
        c.title_y = cy + font_vmetrics(st.font_path, st.title_fontsize).line_height * 0.5f;

        // Legend first, then the bars outboard of it on the same side.
        if (!me.dec.legend_entries.empty()) {
            c.legend_entries = me.dec.legend_entries;
            place_legend(c, lo, rows);
        }
        if (!me.dec.colorbars.empty()) {
            const PlotRect& e = c.extended;
            float start = 0.0f;
            const float legend_w = me.dec.legend_block;
            const float legend_h = me.legend_margin + rows.h;
            switch (me.bars) {
                case DecorSide::Left:   start = e.x - (me.legend == DecorSide::Left ? legend_w : 0.0f); break;
                case DecorSide::Top:    start = e.y - (me.legend == DecorSide::Top ? legend_h : 0.0f); break;
                case DecorSide::Bottom: start = e.y + e.h + (me.legend == DecorSide::Bottom ? legend_h : 0.0f); break;
                default:           start = e.x + e.w + (me.legend == DecorSide::Right ? legend_w : 0.0f); break;
            }
            // Clamped frames can leave a cell too narrow for its own
            // decorations; the bars then run on past the cell's edge.
            // Deliberate -- a bar that was asked for and is ugly is better than
            // one silently dropped, which would leave a colour scale in the
            // picture with nothing explaining it.
            place_colorbars(c, me.dec, cbo, me.bars, start);
        }

        if (const RenderSnapshot3D* s3 = fa.snap3d()) {
            // The projector is built last because it fits the box to the
            // frame, and the frame is only final once the grid is solved.
            c.box3d = Box3DLayout{
                Projector3D(prep[i].tf3, s3->camera, c.frame, s3->box_style.margin),
                prep[i].xticks, prep[i].yticks, prep[i].zticks };
        } else {
            // Left empty for a 3D cell: its three tick lists live in `box3d`,
            // and a 2D drawing path handed a populated xticks would rule a
            // frame that has no axis along its edges.
            c.xticks = prep[i].xticks;
            c.yticks = prep[i].yticks;

            c.tr = { prep[i].limits.xmin, prep[i].limits.xmax,
                     prep[i].limits.ymin, prep[i].limits.ymax,
                     c.frame.x, c.frame.y, c.frame.w, c.frame.h, fw, fh };

            // The axis lines, from data space into pixels (v1.0 step 19). The
            // transform is used rather than the frame edges so that Mid and a
            // pinned origin need no arithmetic of their own, and so that the
            // Low/High cases land on the edge to the last bit.
            const AxisPlacement& xa = prep[i].xaxis;
            const AxisPlacement& ya = prep[i].yaxis;
            c.xaxis_y = c.tr.to_py(xa.pos);
            c.yaxis_x = c.tr.to_px(ya.pos);
            c.xaxis_interior = xa.interior;
            c.yaxis_interior = ya.interior;
            c.xtick_dir = xa.high ? -1.0f :  1.0f;
            c.ytick_dir = ya.high ?  1.0f : -1.0f;

            // The label anchors follow the tick direction. x keeps a single
            // top-aligned anchor in both cases -- above the line it is the
            // line height higher -- so neither renderer needs a vertical
            // alignment; y genuinely flips, since a column of numbers has to
            // line up on the edge that faces the axis.
            const float xoff = st.tick_length + kTickLabelGap;
            const float label_lh = font_vmetrics(st.font_path, st.label_fontsize).line_height;
            c.xlabel_top = c.xtick_dir > 0.0f ? c.xaxis_y + xoff
                                              : c.xaxis_y - xoff - label_lh;
            c.ylabel_x     = c.yaxis_x + c.ytick_dir * xoff;
            c.ylabel_align = c.ytick_dir > 0.0f ? HAlign::Left : HAlign::Right;

            // At the outer edge of the furniture, which is aligned per row and
            // per column -- so the titles of a row or column line up even when
            // one cell's labels are wider than its neighbour's.
            const float xtitle_lh = font_vmetrics(st.font_path, st.xtitle_fontsize).line_height;
            c.xtitle_x = frame_cx;
            c.xtitle_y = c.frame.y + c.frame.h + ax_b - xtitle_lh * 0.5f;

            const float ytitle_lh = font_vmetrics(st.font_path, st.ytitle_fontsize).line_height;
            c.ytitle_x = c.frame.x - ax_l + ytitle_lh * 0.5f;
            c.ytitle_y = c.frame.y + c.frame.h * 0.5f;
        }

        out.cells.push_back(std::move(c));
    }

    return out;
}

} // namespace

FigureLayout compute_figure_layout(const FigureSnapshot& fsnap, int fig_w, int fig_h,
                                   FigureMeasure* out_measure) {
    // One prepare() per axes, reused for the measure and the cell pass — it
    // resolves the limits (auto_scale over the whole dataset) and generates
    // the ticks, neither of which should happen twice per frame.
    const std::vector<CellPrep> prep = prepare_all(fsnap);
    FigureMeasure measure = measure_with(fsnap, prep, nullptr);
    FigureLayout  out     = layout_with(fsnap, prep, measure, fig_w, fig_h);
    if (out_measure) *out_measure = std::move(measure);
    return out;
}

FigureLayout compute_figure_layout(const FigureSnapshot& fsnap, const FigureMeasure& measure,
                                   int fig_w, int fig_h) {
    if (!measure_fits(measure, fsnap)) return compute_figure_layout(fsnap, fig_w, fig_h);
    return layout_with(fsnap, prepare_all(fsnap), measure, fig_w, fig_h);
}

FigureLayout LayoutStore::fit(const FigureSnapshot& fsnap, int fig_w, int fig_h) {
    // generation 0 is "never stamped", which never counts as unchanged.
    const bool due = refit_ || !measure_ || fsnap.layout_generation == 0
                  || fsnap.layout_generation != generation_ || fig_w != w_ || fig_h != h_
                  || !measure_fits(*measure_, fsnap);
    if (!due) return layout_with(fsnap, prepare_all(fsnap), *measure_, fig_w, fig_h);

    auto fresh = std::make_shared<FigureMeasure>();
    FigureLayout out = compute_figure_layout(fsnap, fig_w, fig_h, fresh.get());
    {
        std::scoped_lock lk(mutex_);
        measure_ = std::move(fresh);
    }
    generation_ = fsnap.layout_generation;
    w_ = fig_w;
    h_ = fig_h;
    refit_ = false;
    return out;
}

std::shared_ptr<const FigureMeasure> LayoutStore::load() const {
    std::scoped_lock lk(mutex_);
    return measure_;
}

} // namespace sextant
