#include "figure_layout.h"
#include "../axis_limits.h"
#include "../axis_placement.h"
#include "../fill_geometry.h"
#include "../rich_text.h"
#include "../tick_style.h"
#include "../ticker.h"
#include "marker_shape.h"
#include "../pie_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sextant {
    namespace {
        // Widest label in a tick list, at the axes' own label font.
        float widest_label(const std::vector<Tick>& ticks, const std::string& font_path, float fontsize) {
            float w = 0.0f;
            for (const auto& t: ticks)
                w = std::max(w, label_width(font_path, fontsize, t.label));
            return w;
        }

        float half_label_width(const std::vector<Tick>& ticks, std::size_t i,
                               const std::string& font_path, float fontsize) {
            if (i >= ticks.size()) return 0.0f;
            return label_width(font_path, fontsize, ticks[i].label) * 0.5f;
        }

        // The tallest label in a tick list: the font's line height unless a label
        // has math reaching past it.
        float tallest_label(const std::vector<Tick>& ticks, const std::string& font_path, float fontsize) {
            float h = font_vmetrics(font_path, fontsize).line_height;
            for (const auto& t: ticks)
                h = std::max(h, label_vmetrics(font_path, fontsize, t.label).line_height);
            return h;
        }

        // A title's line height (its own extent when it has math).
        float title_height(const std::string& text, const std::string& font_path, float fontsize) {
            return label_vmetrics(font_path, fontsize, text).line_height;
        }

        std::string colorbar_number(float v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(v));
            return buf;
        }

        // The ticks actually drawn (override or generated); layout must measure these.
        std::vector<Tick> ticks_for(const std::optional<std::vector<Tick>>& override_,
                                    double lo, double hi) {
            if (override_) return *override_;
            return generate_ticks(lo, hi);
        }

        // A 2D axis' drawn ticks: set_xticks() ones (relabelled by a tick
        // format), or the scale's.
        std::vector<Tick> ticks_for(const std::optional<std::vector<Tick>>& override_,
                                    double lo, double hi, const AxisScale& s, const std::string& fmt) {
            if (override_) return relabel_ticks(*override_, fmt);
            return scale_ticks(lo, hi, s, fmt, mathtext_on());
        }

        // Minor positions, when the axis has them (AxesStyle::minor_xticks).
        std::vector<double> minors_for(const std::optional<bool>& on, const std::optional<std::vector<Tick>>& override_,
                                       double lo, double hi, const AxisScale& s) {
            if (!on.value_or(!s.linear())) return {};
            return scale_minor_ticks(lo, hi, s, override_.has_value());
        }

        struct CellPrep {
            ResolvedLimits limits;
            std::vector<Tick> xticks, yticks;
            // 2D only: minor tick positions.
            std::vector<double> xminor, yminor;
            float max_ylabel_w = 0.0f;

            // Axis line placement; add_furniture() needs it to know where labels reach.
            AxisPlacement xaxis, yaxis;

            // 2D: whether each axis draws its tick labels (AxesStyle::show_xtick_labels,
            // resolved against the share groups by prepare_all()).
            bool xlabels = true, ylabels = true;

            // 2D, a twinned cell: this layer (0 = the cell's own axes) and where its
            // axis titles go (a twinx's y title on the right, a twiny's x title on top).
            int layer = 0;
            bool ytitle_right = false, xtitle_top = false;
            // The cell's twins, on the cell's own prep only.
            std::vector<CellPrep> twins;

            // 3D only: z ticks and the data->box transform, resolved once.
            bool is_3d = false;
            std::vector<Tick> zticks;
            Transform3D tf3;

            // Polar only: the frame's map; xticks are the spokes (degrees),
            // yticks the rings.
            bool polar = false;
            PolarMap pmap;
        };

        // An axis forced to a frame edge, whatever its AxisPosition and pin say
        // (a twinned pair, see Axes::twinx()). `lo`/`hi`: the other axis' limits.
        AxisPlacement edge_axis(bool high, double lo, double hi) {
            AxisPlacement a;
            a.pos = high ? hi : lo;
            a.high = high;
            return a;
        }

        // `limits` from resolve_figure_limits(), which sees share groups whole.
        CellPrep prepare(const RenderSnapshot& snap, const ResolvedLimits& limits) {
            CellPrep p;
            p.limits = limits;
            p.xticks = ticks_for(snap.xticks_override, p.limits.xmin, p.limits.xmax, snap.xscale,
                                 snap.xtick_format);
            p.yticks = ticks_for(snap.yticks_override, p.limits.ymin, p.limits.ymax, snap.yscale,
                                 snap.ytick_format);
            const auto& sty = snap.axes_style;
            p.xminor = minors_for(sty.minor_xticks, snap.xticks_override, p.limits.xmin, p.limits.xmax,
                                  snap.xscale);
            p.yminor = minors_for(sty.minor_yticks, snap.yticks_override, p.limits.ymin, p.limits.ymax,
                                  snap.yscale);
            p.max_ylabel_w = widest_label(p.yticks, snap.axes_style.font_path,
                                          tick_look(snap.axes_style, TickAxis::Y).label_fontsize);

            const auto& st = snap.axes_style;
            p.xaxis = place_axis(st.xaxis_y, st.origin_y, p.limits.ymin, p.limits.ymax, snap.yscale);
            p.yaxis = place_axis(st.yaxis_x, st.origin_x, p.limits.xmin, p.limits.xmax, snap.xscale);
            return p;
        }

        // A layer of a twinned cell: as prepare(), with the twinned axes on fixed
        // edges -- the cell's own y on the left and its twinx's on the right, its
        // own x at the bottom and its twiny's on top.
        CellPrep prepare_layer(const RenderSnapshot& snap, const ResolvedLimits& limits, int layer,
                               bool has_twinx, bool has_twiny) {
            CellPrep p = prepare(snap, limits);
            p.layer = layer;
            const ResolvedLimits& l = p.limits;
            if (layer == kLayerTwinX || (layer == 0 && has_twinx))
                p.yaxis = edge_axis(layer == kLayerTwinX, l.xmin, l.xmax);
            if (layer == kLayerTwinY || (layer == 0 && has_twiny))
                p.xaxis = edge_axis(layer == kLayerTwinY, l.ymin, l.ymax);
            p.ytitle_right = layer == kLayerTwinX;
            p.xtitle_top = layer == kLayerTwinY;
            return p;
        }

        // The cell's own axes prepared again over other limits (Datalim's
        // widened ones), keeping what prepare_all() resolved across the grid.
        CellPrep reprepare(const FigureAxesSnapshot& fa, const RenderSnapshot& snap, const CellPrep& was,
                           const ResolvedLimits& limits) {
            bool tx = false, ty = false;
            for (const TwinSnapshot& t: fa.twins) {
                tx = tx || t.layer == kLayerTwinX;
                ty = ty || t.layer == kLayerTwinY;
            }
            CellPrep p = fa.twins.empty() ? prepare(snap, limits) : prepare_layer(snap, limits, 0, tx, ty);
            p.xlabels = was.xlabels;
            p.ylabels = was.ylabels;
            return p;
        }

        CellPrep prepare(const RenderSnapshotPolar& snap, const ResolvedLimits& limits) {
            CellPrep p;
            p.limits = limits;
            const Range rl{limits.ymin, limits.ymax};
            p.polar = true;
            p.pmap = polar_map(snap.view, rl);
            p.xticks = polar_theta_ticks(snap);
            p.yticks = polar_r_ticks(snap, rl);
            return p;
        }

        // Once per frame (auto_scale3d() is O(N)).
        CellPrep prepare(const RenderSnapshot3D& snap, const ResolvedLimits&) {
            CellPrep p;
            p.is_3d = true;
            const ResolvedLimits3D l = resolve_limits(snap);
            const double xmin = l.xmin, xmax = l.xmax, ymin = l.ymin, ymax = l.ymax;
            const double zmin = l.zmin, zmax = l.zmax;
            p.limits = {xmin, xmax, ymin, ymax};
            p.xticks = ticks_for(snap.xticks_override, xmin, xmax);
            p.yticks = ticks_for(snap.yticks_override, ymin, ymax);
            p.zticks = ticks_for(snap.zticks_override, zmin, zmax);
            p.tf3 = {xmin, xmax, ymin, ymax, zmin, zmax, snap.aspect};
            return p;
        }

        // Where an interior axis line sits across the frame, as a fraction from
        // the low edge (bottom for an x axis, left for a y one).
        float line_fraction(double pos, double lo, double hi, const AxisScale& s) {
            const double a = s.fwd(lo), b = s.fwd(hi);
            const double f = (s.fwd(pos) - a) / (b - a);
            return std::isfinite(f) ? static_cast<float>(std::clamp(f, 0.0, 1.0)) : 0.0f;
        }

        // One layer's furniture, added into its cell's measure: ticks and tick
        // labels as reach past the frame (cell_a), the x/y titles beyond them
        // (cell_o). The axes title is a separate band (title_band()). A 3D cell
        // has neither: its labels sit inside the frame, in the Box3DStyle::margin
        // fraction (their position depends on the camera fit).
        // A polar cell's: the θ labels all round the frame (the r labels
        // sit inside it).
        void add_polar_furniture(const RenderSnapshot& snap, const CellPrep& prep, CellMeasure& m) {
            const auto& st = snap.axes_style;
            if (!snap.axis_on || !st.show_xticks || prep.xticks.empty()) return;
            const TickLook k = tick_look(st, TickAxis::X);
            const float w = widest_label(prep.xticks, st.font_path, k.label_fontsize);
            const float h = tallest_label(prep.xticks, st.font_path, k.label_fontsize);
            const auto grow = [](float& to, float v) { to = std::max(to, v); };
            grow(m.reach_l.fixed, kPolarLabelGap + w);
            grow(m.reach_r.fixed, kPolarLabelGap + w);
            grow(m.reach_t.fixed, kPolarLabelGap + h);
            grow(m.reach_b.fixed, kPolarLabelGap + h);
        }

        void add_furniture(const RenderSnapshot& snap, const CellPrep& prep, CellMeasure& m) {
            if (prep.polar) {
                add_polar_furniture(snap, prep, m);
                return;
            }
            const auto& st = snap.axes_style;
            // Axes::set_axis_visible(false): no ticks, labels or x/y titles.
            const bool sx = snap.axis_on && st.show_xticks, sy = snap.axis_on && st.show_yticks;
            const bool xl = sx && prep.xlabels, yl = sy && prep.ylabels;
            const TickLook xk = tick_look(st, TickAxis::X), yk = tick_look(st, TickAxis::Y);
            const float xlabel_lh = xl ? tallest_label(prep.xticks, st.font_path, xk.label_fontsize) : 0.0f;
            const float ylabel_lh = yl ? tallest_label(prep.yticks, st.font_path, yk.label_fontsize) : 0.0f;
            const auto grow = [](float& to, float v) { to = std::max(to, v); };

            // The tick band goes on the side its labels fall. A hidden axis
            // (AxesStyle::show_xticks/show_yticks) has none, and hidden labels
            // (show_xtick_labels) leave the marks only.
            const float x_band = !sx ? 0.0f
                                 : xl ? xk.length + kTickLabelGap + xlabel_lh
                                      : xk.length;
            const float y_band = !sy ? 0.0f
                                 : yl ? yk.length + kTickLabelGap + prep.max_ylabel_w
                                      : yk.length;

            // On a frame edge the band lies outside it; an interior axis (always
            // labelled on its low side) reaches out only past what lies between
            // its line and that edge.
            const ResolvedLimits& l = prep.limits;
            if (x_band > 0.0f) {
                if (!prep.xaxis.interior) grow((prep.xaxis.high ? m.reach_t : m.reach_b).fixed, x_band);
                else m.reach_b.pokes.push_back({x_band, line_fraction(prep.xaxis.pos, l.ymin, l.ymax, snap.yscale)});
            }
            if (y_band > 0.0f) {
                if (!prep.yaxis.interior) grow((prep.yaxis.high ? m.reach_r : m.reach_l).fixed, y_band);
                else m.reach_l.pokes.push_back({y_band, line_fraction(prep.yaxis.pos, l.xmin, l.xmax, snap.xscale)});
            }

            // End x labels are centred on the frame corners and hang half outside
            // (wherever the axis line is); likewise the outermost y labels at the
            // top and bottom edges.
            if (xl && !prep.xticks.empty()) {
                grow(m.reach_l.fixed, half_label_width(prep.xticks, 0, st.font_path, xk.label_fontsize));
                grow(m.reach_r.fixed, half_label_width(prep.xticks, prep.xticks.size() - 1,
                                                       st.font_path, xk.label_fontsize));
            }
            if (yl) {
                grow(m.reach_t.fixed, ylabel_lh * 0.5f);
                grow(m.reach_b.fixed, ylabel_lh * 0.5f);
            }

            // Titles don't move with an interior axis: they stay outside cell_a.
            if (!snap.axis_on) return;
            if (!snap.ytitle.empty())
                grow(prep.ytitle_right ? m.titles.right : m.titles.left,
                     kTitleGap + title_height(snap.ytitle, st.font_path, st.ytitle_fontsize));
            if (!snap.xtitle.empty())
                grow(prep.xtitle_top ? m.titles.top : m.titles.bottom,
                     kTitleGap + title_height(snap.xtitle, st.font_path, st.xtitle_fontsize));
        }

        float title_band(const std::string& title, const AxesStyle& st) {
            return title.empty()
                       ? 0.0f
                       : title_height(title, st.font_path, st.title_fontsize) + kTitleGap;
        }

        // A 2D cell's share group id on one axis for layer k (0 = its own axes,
        // 1 + n its twin n); 0 = none or no such layer.
        unsigned long long layer_share(const FigureAxesSnapshot& fa, std::size_t k, bool x) {
            const RenderSnapshot* s = fa.snap2d();
            if (!s) return 0;
            if (k > 0) {
                if (k > fa.twins.size()) return 0;
                s = &fa.twins[k - 1].snap;
            }
            return x ? s->xshare : s->yshare;
        }

        // Whether any layer of the cell shares that axis.
        bool cell_shares(const FigureAxesSnapshot& fa, bool x) {
            for (std::size_t k = 0; k <= fa.twins.size(); ++k)
                if (layer_share(fa, k, x)) return true;
            return false;
        }

        bool holds_share(const FigureAxesSnapshot& fa, unsigned long long id, bool x) {
            if (!id) return false;
            for (std::size_t k = 0; k <= fa.twins.size(); ++k)
                if (layer_share(fa, k, x) == id) return true;
            return false;
        }

        // Whether cells i and j hold one share group on that axis.
        bool cells_share(const FigureSnapshot& fsnap, std::size_t i, std::size_t j, bool x) {
            const FigureAxesSnapshot& a = fsnap.axes[i];
            for (std::size_t k = 0; k <= a.twins.size(); ++k)
                if (holds_share(fsnap.axes[j], layer_share(a, k, x), x)) return true;
            return false;
        }

        // The grid line a cell's edge lies on: its first (`low`) or last column
        // (`x`), or row.
        int edge_line(const AxesSlot& s, bool x, bool low) {
            return x ? (low ? s.col0() : s.col1()) : (low ? s.row0() : s.row1());
        }

        // Whether a cell may align with others: 2D without set_aspect() (never
        // set on a twin).
        bool may_align(const FigureAxesSnapshot& fa) {
            const RenderSnapshot* s = fa.snap2d();
            return s && !(s->aspect.ratio > 0.0) && !fa.snappolar();
        }

        // Frames that keep one edge together, as a group id per cell (a cell
        // alone is its own group). Two cells join when all of these hold, and
        // groups join through a common member:
        //   - they hold one share group on that axis (twins included);
        //   - that edge lies on the same grid line (left: the first column;
        //     right: the last; top/bottom likewise by row);
        //   - neither has set_aspect() (may_align(); 3D never aligns).
        // Tick-label hiding (resolve_tick_labels()) follows the same groups.
        std::vector<std::size_t> edge_groups(const FigureSnapshot& fsnap, bool x, bool low) {
            const auto& cells = fsnap.axes;
            std::vector<std::size_t> up(cells.size());
            for (std::size_t i = 0; i < up.size(); ++i) up[i] = i;
            const auto find = [&](std::size_t i) {
                while (up[i] != i) i = up[i] = up[up[i]];
                return i;
            };
            for (std::size_t i = 0; i < cells.size(); ++i) {
                if (!may_align(cells[i])) continue;
                for (std::size_t j = i + 1; j < cells.size(); ++j) {
                    if (!may_align(cells[j]) || edge_line(cells[i].slot, x, low) != edge_line(cells[j].slot, x, low)
                        || !cells_share(fsnap, i, j, x))
                        continue;
                    up[find(j)] = find(i);
                }
            }
            for (std::size_t i = 0; i < up.size(); ++i) up[i] = find(i);
            return up;
        }

        // AxesStyle::show_xtick_labels/show_ytick_labels: unset hides an axis'
        // labels when they face another member of its share group across the
        // grid whose frame is aligned with this one (edge_groups(): an edge on
        // the same grid line, no set_aspect() on either) -- x labels at the
        // bottom (top) with a member in a lower (higher) row, y labels at the
        // left (right) with one in a column to the left (right). An interior
        // axis keeps its labels; axes in the same cell don't count.
        void resolve_tick_labels(const FigureSnapshot& fsnap, std::vector<CellPrep>& prep) {
            const auto& cells = fsnap.axes;
            const std::vector<std::size_t> xl = edge_groups(fsnap, true, true), xr = edge_groups(fsnap, true, false);
            const std::vector<std::size_t> yt = edge_groups(fsnap, false, true), yb = edge_groups(fsnap, false, false);
            for (std::size_t i = 0; i < cells.size(); ++i) {
                const RenderSnapshot* host = cells[i].snap2d();
                if (!host) continue;
                const AxesSlot& a = cells[i].slot;
                // Each layer on its own: a twin's axis faces the members of its own
                // groups in other cells.
                const auto resolve = [&](const RenderSnapshot& s, CellPrep& p) {
                    const auto faces_member = [&](bool x) {
                        const unsigned long long id = x ? s.xshare : s.yshare;
                        const AxisPlacement& pl = x ? p.xaxis : p.yaxis;
                        if (!id || pl.interior) return false;
                        for (std::size_t j = 0; j < cells.size(); ++j) {
                            if (j == i || !holds_share(cells[j], id, x)) continue;
                            const bool aligned = x ? (xl[i] == xl[j] && xr[i] == xr[j])
                                                   : (yt[i] == yt[j] && yb[i] == yb[j]);
                            if (!aligned) continue;
                            const AxesSlot& b = cells[j].slot;
                            if (x ? (pl.high ? b.row1() < a.row0() : b.row0() > a.row1())
                                  : (pl.high ? b.col0() > a.col1() : b.col1() < a.col0()))
                                return true;
                        }
                        return false;
                    };
                    const AxesStyle& st = s.axes_style;
                    p.xlabels = st.show_xtick_labels.value_or(!faces_member(true));
                    p.ylabels = st.show_ytick_labels.value_or(!faces_member(false));
                };
                resolve(*host, prep[i]);
                for (std::size_t k = 0; k < cells[i].twins.size(); ++k)
                    resolve(cells[i].twins[k].snap, prep[i].twins[k]);
            }
        }

        std::vector<CellPrep> prepare_all(const FigureSnapshot& fsnap) {
            std::vector<CellPrep> prep;
            prep.reserve(fsnap.axes.size());
            const std::vector<ResolvedLimits> limits = resolve_figure_limits(fsnap);
            std::size_t at = 0;   // cell i's own axes in `limits`; its twins follow
            for (std::size_t i = 0; i < fsnap.axes.size(); ++i) {
                const FigureAxesSnapshot& fa = fsnap.axes[i];
                const std::size_t first = at;
                at += 1 + fa.twins.size();
                if (fa.twins.empty()) {
                    prep.push_back(std::visit([&](const auto& s) { return prepare(s, limits[first]); },
                                              fa.snap));
                    continue;
                }
                bool tx = false, ty = false;
                for (const TwinSnapshot& t: fa.twins) {
                    tx = tx || t.layer == kLayerTwinX;
                    ty = ty || t.layer == kLayerTwinY;
                }
                CellPrep p = prepare_layer(*fa.snap2d(), limits[first], 0, tx, ty);
                for (std::size_t k = 0; k < fa.twins.size(); ++k)
                    p.twins.push_back(prepare_layer(fa.twins[k].snap, limits[first + 1 + k], fa.twins[k].layer,
                                                    tx, ty));
                prep.push_back(std::move(p));
            }
            resolve_tick_labels(fsnap, prep);
            return prep;
        }

        // Every colorbar request measured, for 2D and 3D alike (forward and inverse
        // layout must agree). Blocks are summed per bar since number widths differ;
        // beside a horizontal bar the numbers cost a line height.
        void fill_colorbars(CellDecorations& d, const std::vector<ColorbarRequest>& reqs,
                            const ColorbarOptions& cb) {
            const bool horizontal = cb.anchor == ColorbarAnchor::Top
                                    || cb.anchor == ColorbarAnchor::Bottom;
            const float lh = font_vmetrics(cb.font_path, cb.fontsize).line_height;
            d.colorbars.reserve(reqs.size());
            for (const ColorbarRequest& r: reqs) {
                float num = horizontal
                                ? lh
                                : std::max(text_width(cb.font_path, cb.fontsize, colorbar_number(r.vmax)),
                                           text_width(cb.font_path, cb.fontsize, colorbar_number(r.vmin)));
                // A stepped bar's labels are its boundaries: the widest of them.
                if (!horizontal)
                    for (double b: r.bounds)
                        num = std::max(num, text_width(cb.font_path, cb.fontsize,
                                                       colorbar_number(static_cast<float>(b))));
                // The name costs a line height beside a vertical bar too (drawn
                // rotated along it), outboard of the numbers.
                const float name = r.name.empty()
                                       ? 0.0f
                                       : kColorbarLabelGap + title_height(r.name, cb.font_path, cb.fontsize);
                const float block = std::max(0.0f, cb.margin) + std::max(0.0f, cb.width)
                                    + kColorbarLabelGap + num + name;
                d.colorbars.push_back({r.cmap, r.vmin, r.vmax, r.name, block, r.bounds, r.band_t});
                d.colorbar_block += block;
            }
        }

        void fill_legend(CellDecorations& d, std::vector<LegendEntry> entries, const LegendOptions& lo) {
            d.legend_entries = std::move(entries);
            if (d.legend_entries.empty()) return;
            float row_h = legend_row_height(lo.fontsize);
            float text_w = 0.0f;
            d.legend_entry_w.reserve(d.legend_entries.size());
            for (const auto& e: d.legend_entries) {
                const float w = label_width(lo.font_path, lo.fontsize, e.name);
                text_w = std::max(text_w, w);
                d.legend_entry_w.push_back(kLegendSwatchW + kLegendGap + w);
                // A name with a fraction or root: its ink plus 2 px each way.
                if (is_rich(e.name))
                    if (const RichLine l = layout_rich(lo.font_path, lo.fontsize, e.name); l.placed)
                        row_h = std::max(row_h, l.ink_top - l.ink_bottom + 4.0f);
            }
            d.legend_row_h = row_h;
            d.legend_box_w = kLegendPad * 2.0f + kLegendSwatchW + kLegendGap + text_w;
            d.legend_box_h = kLegendPad * 2.0f + row_h * static_cast<float>(d.legend_entries.size());
            d.legend_block = std::max(0.0f, lo.margin) + d.legend_box_w;
        }

        // ---------------------------------------------------------------------------
        // The grid solve
        // ---------------------------------------------------------------------------
        DecorSide legend_side(LegendAnchor a) {
            switch (a) {
                case LegendAnchor::OutsideTL:
                case LegendAnchor::OutsideTR: return DecorSide::Top;
                case LegendAnchor::OutsideBL:
                case LegendAnchor::OutsideBR: return DecorSide::Bottom;
                case LegendAnchor::OutsideLT:
                case LegendAnchor::OutsideLB: return DecorSide::Left;
                case LegendAnchor::OutsideRT:
                case LegendAnchor::OutsideRB: return DecorSide::Right;
                default: return DecorSide::None;
            }
        }

        DecorSide colorbar_side(ColorbarAnchor a) {
            switch (a) {
                case ColorbarAnchor::Left: return DecorSide::Left;
                case ColorbarAnchor::Top: return DecorSide::Top;
                case ColorbarAnchor::Bottom: return DecorSide::Bottom;
                default: return DecorSide::Right;
            }
        }

        CellMeasure measure(const FigureAxesSnapshot& fa, const CellPrep& prep) {
            CellMeasure m;
            m.slot = fa.slot;
            m.is3d = fa.snap3d() != nullptr;
            visit_cell(fa, [&](const auto& s) {
                m.title = title_band(s.title, s.axes_style);
                m.frame_margin = std::max(0.0f, s.axes_style.frame_margin);
                m.dec = compute_cell_decorations(fa);
                m.legend_margin = std::max(0.0f, s.legend_opts.margin);
                m.legend_fontsize = s.legend_opts.fontsize;
                if (!m.dec.legend_entries.empty()) m.legend = legend_side(s.legend_opts.anchor);
                m.bars = colorbar_side(s.colorbar_opts.anchor);
            });
            if (const RenderSnapshot* s2 = fa.snap2d()) {
                // A twinned cell: every layer's furniture, the largest per side.
                add_furniture(*s2, prep, m);
                for (std::size_t k = 0; k < fa.twins.size(); ++k)
                    add_furniture(fa.twins[k].snap, prep.twins[k], m);
            }
            return m;
        }

        FigureMeasure measure_with(const FigureSnapshot& fsnap, const std::vector<CellPrep>& prep,
                                   const FigureMeasure* frozen) {
            FigureMeasure out;
            out.suptitle_band = suptitle_band_height(fsnap.suptitle, fsnap.suptitle_opts);
            out.cells.reserve(fsnap.axes.size());
            for (std::size_t i = 0; i < fsnap.axes.size(); ++i) {
                CellMeasure m = measure(fsnap.axes[i], prep[i]);
                if (frozen && !m.is3d)
                    for (const CellMeasure& f: frozen->cells)
                        if (f.slot.index == m.slot.index && !f.is3d) {
                            m.reach_l = f.reach_l;
                            m.reach_r = f.reach_r;
                            m.reach_t = f.reach_t;
                            m.reach_b = f.reach_b;
                            m.titles = f.titles;
                            break;
                        }
                out.cells.push_back(std::move(m));
            }
            return out;
        }

        // One grid axis: each track's position and length, sharing the space left
        // after margins and gaps by weight.
        void tracks_1d(float extent, float lead, float trail, float gap, const std::vector<float>& w,
                       std::vector<float>& pos, std::vector<float>& len) {
            const int n = static_cast<int>(w.size());
            float total = 0.0f;
            for (float x: w) total += x;
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

        LegendRows legend_rows_for(const CellMeasure& m, float extended_w) {
            if (m.dec.legend_entries.empty()) return {};
            if (m.legend == DecorSide::Top || m.legend == DecorSide::Bottom)
                return layout_legend_rows(m.dec, m.legend_fontsize, extended_w - 2.0f * m.legend_margin);
            // Every other anchor is one column.
            LegendRows out;
            const float row_h = m.dec.legend_row_h;
            out.w = m.dec.legend_box_w;
            out.h = m.dec.legend_box_h;
            out.slots.reserve(m.dec.legend_entries.size());
            for (std::size_t k = 0; k < m.dec.legend_entries.size(); ++k)
                out.slots.push_back({
                    kLegendPad,
                    kLegendPad + row_h * static_cast<float>(k) + row_h * 0.5f
                });
            return out;
        }

        // ---------------------------------------------------------------------------
        // The reservation solve
        // ---------------------------------------------------------------------------
        // What lies outside cell_a on each side that doesn't depend on the frame:
        // the axis title, frame_margin, an outside legend and colorbars (and on
        // top, the title band). Top and bottom take the legend's rows, which wrap
        // to the frame's width.
        float outer_left(const CellMeasure& m) {
            return m.titles.left + m.frame_margin + (m.legend == DecorSide::Left ? m.dec.legend_block : 0.0f)
                   + (m.bars == DecorSide::Left ? m.dec.colorbar_block : 0.0f);
        }

        float outer_right(const CellMeasure& m) {
            return m.titles.right + m.frame_margin + (m.legend == DecorSide::Right ? m.dec.legend_block : 0.0f)
                   + (m.bars == DecorSide::Right ? m.dec.colorbar_block : 0.0f);
        }

        float top_decor(const CellMeasure& m, const LegendRows& rows) {
            return (m.legend == DecorSide::Top ? m.legend_margin + rows.h : 0.0f)
                   + (m.bars == DecorSide::Top ? m.dec.colorbar_block : 0.0f);
        }

        float bottom_decor(const CellMeasure& m, const LegendRows& rows) {
            return (m.legend == DecorSide::Bottom ? m.legend_margin + rows.h : 0.0f)
                   + (m.bars == DecorSide::Bottom ? m.dec.colorbar_block : 0.0f);
        }

        float outer_top(const CellMeasure& m, const LegendRows& rows) {
            return m.title + top_decor(m, rows) + m.frame_margin + m.titles.top;
        }

        float outer_bottom(const CellMeasure& m, const LegendRows& rows) {
            return bottom_decor(m, rows) + m.frame_margin + m.titles.bottom;
        }

        // The longest frame that fits `avail` with the reaches `a` and `b` beside
        // it. Each reach shrinks no faster than the frame grows, so the frame
        // plus both is monotonic in the frame: bisect it when a label pokes.
        float fit_len(float avail, const AxisReach& a, const AxisReach& b) {
            if (a.pokes.empty() && b.pokes.empty()) return std::max(kMinFrameSize, avail - a.fixed - b.fixed);
            const auto fits = [&](float len) { return len + a.at(len) + b.at(len) <= avail; };
            float lo = kMinFrameSize, hi = std::max(kMinFrameSize, avail);
            if (!fits(lo)) return kMinFrameSize;
            if (fits(hi)) return hi;
            for (int k = 0; k < 48 && hi - lo > 1e-4f; ++k) {
                const float mid = 0.5f * (lo + hi);
                (fits(mid) ? lo : hi) = mid;
            }
            return lo;
        }

        // One direction's frames. `len[i]` comes in as each cell's own fit in
        // `span[i]` (the cell's extent); every cell then reserves its group's
        // largest need before (`lo`) and after (`hi`) its frame. A poking label
        // needs more as its frame shrinks, so this repeats until no frame moves
        // (at once when nothing pokes). A cell alone keeps its own fit.
        template <class NeedLo, class NeedHi>
        void align_1d(const std::vector<std::size_t>& lo, const std::vector<std::size_t>& hi,
                      const std::vector<float>& span, NeedLo need_lo, NeedHi need_hi,
                      std::vector<float>& len, std::vector<float>& res_lo, std::vector<float>& res_hi) {
            const std::size_t n = len.size();
            std::vector<float> glo(n), ghi(n);
            for (int pass = 0; pass < 16; ++pass) {
                std::fill(glo.begin(), glo.end(), 0.0f);
                std::fill(ghi.begin(), ghi.end(), 0.0f);
                for (std::size_t i = 0; i < n; ++i) {
                    glo[lo[i]] = std::max(glo[lo[i]], need_lo(i, len[i]));
                    ghi[hi[i]] = std::max(ghi[hi[i]], need_hi(i, len[i]));
                }
                bool moved = false;
                for (std::size_t i = 0; i < n; ++i) {
                    res_lo[i] = glo[lo[i]];
                    res_hi[i] = ghi[hi[i]];
                    const float l = std::max(kMinFrameSize, span[i] - res_lo[i] - res_hi[i]);
                    moved = moved || std::fabs(l - len[i]) > 1e-3f;
                    len[i] = l;
                }
                if (!moved) break;
            }
        }

        // Every cell's reservation and frame size at the given tracks.
        struct CellFit {
            PlotInsets res;   // cell edge to frame (CellLayout::reserved)
            float frame_w = 0.0f, frame_h = 0.0f;
            LegendRows rows;  // the legend's, wrapped to the extended frame
        };

        std::vector<CellFit> fit_cells(const FigureSnapshot& fsnap, const FigureMeasure& measure,
                                       const GridTracks& t) {
            const std::vector<CellMeasure>& ms = measure.cells;
            const std::size_t n = ms.size();
            std::vector<CellFit> out(n);
            std::vector<float> cw(n), ch(n), fw(n), fh(n);
            std::vector<float> rl(n), rr(n), rt(n), rb(n);
            for (std::size_t i = 0; i < n; ++i) {
                const AxesSlot& s = ms[i].slot;
                cw[i] = t.col_x[s.col1()] + t.col_w[s.col1()] - t.col_x[s.col0()];
                ch[i] = t.row_y[s.row1()] + t.row_h[s.row1()] - t.row_y[s.row0()];
            }

            // Widths first: they don't depend on the heights.
            for (std::size_t i = 0; i < n; ++i)
                fw[i] = fit_len(cw[i] - outer_left(ms[i]) - outer_right(ms[i]), ms[i].reach_l, ms[i].reach_r);
            align_1d(edge_groups(fsnap, true, true), edge_groups(fsnap, true, false), cw,
                     [&](std::size_t i, float len) { return outer_left(ms[i]) + ms[i].reach_l.at(len); },
                     [&](std::size_t i, float len) { return outer_right(ms[i]) + ms[i].reach_r.at(len); },
                     fw, rl, rr);

            // Then heights: an Outside top/bottom legend wraps its rows to the
            // extended frame, known now that the width is.
            for (std::size_t i = 0; i < n; ++i) {
                const CellMeasure& m = ms[i];
                const float ext_w = fw[i] + m.reach_l.at(fw[i]) + m.reach_r.at(fw[i]) + m.titles.left
                                    + m.titles.right + 2.0f * m.frame_margin;
                out[i].rows = legend_rows_for(m, ext_w);
                fh[i] = fit_len(ch[i] - outer_top(m, out[i].rows) - outer_bottom(m, out[i].rows),
                                m.reach_t, m.reach_b);
            }
            align_1d(edge_groups(fsnap, false, true), edge_groups(fsnap, false, false), ch,
                     [&](std::size_t i, float len) { return outer_top(ms[i], out[i].rows) + ms[i].reach_t.at(len); },
                     [&](std::size_t i, float len) {
                         return outer_bottom(ms[i], out[i].rows) + ms[i].reach_b.at(len);
                     },
                     fh, rt, rb);

            for (std::size_t i = 0; i < n; ++i) {
                out[i].res = {rl[i], rr[i], rt[i], rb[i]};
                out[i].frame_w = fw[i];
                out[i].frame_h = fh[i];
            }
            return out;
        }
    } // namespace

    LegendRows layout_legend_rows(const CellDecorations& dec, float fontsize, float max_w) {
        LegendRows out;
        if (dec.legend_entries.empty()) return out;
        const float row_h = dec.legend_row_h > 0.0f ? dec.legend_row_h : legend_row_height(fontsize);
        float cur = 0.0f, content_w = 0.0f;
        int row = 0;
        out.slots.reserve(dec.legend_entries.size());
        for (std::size_t k = 0; k < dec.legend_entries.size(); ++k) {
            const float ew = k < dec.legend_entry_w.size() ? dec.legend_entry_w[k] : 0.0f;
            if (cur > 0.0f && cur + kLegendColGap + ew + 2.0f * kLegendPad > max_w) {
                ++row;
                cur = 0.0f;
            }
            const float x = cur > 0.0f ? cur + kLegendColGap : 0.0f;
            out.slots.push_back({
                kLegendPad + x,
                kLegendPad + row_h * static_cast<float>(row) + row_h * 0.5f
            });
            cur = x + ew;
            content_w = std::max(content_w, cur);
        }
        out.w = kLegendPad * 2.0f + content_w;
        out.h = kLegendPad * 2.0f + row_h * static_cast<float>(row + 1);
        return out;
    }

    // A scatter key keeps the series' color whatever its alpha (as always), except
    // a hollow series (alpha 0), whose key is hollow too.
    static Color marker_key_fill(Color color, float alpha) {
        if (alpha <= 0.0f) color.a = 0.0f;
        return color;
    }

    // A scatter key's outline (alpha 0 = none): the series' `edgecolor`, or its
    // color, at that color's own alpha (`alpha` 0 hides it, as the fill's key);
    // drawn 1 px wide whatever `edge_linewidth` is.
    static Color marker_key_edge(const Color& color, const std::optional<Color>& edgecolor,
                                 float alpha, float edge_linewidth) {
        if (!(edge_linewidth > 0.0f) || alpha <= 0.0f) return {0.0f, 0.0f, 0.0f, 0.0f};
        return edgecolor ? *edgecolor : color;
    }

    // A line marker's key is its stroke alone, in `ink` (the fill's color, or
    // black for a colormapped series, whose fill key is white), with no outline.
    static void line_marker_key(LegendEntry& e, Color ink) {
        if (!is_line_marker(e.marker)) return;
        e.color = ink;
        e.edge = {0.0f, 0.0f, 0.0f, 0.0f};
    }

    // One 2D object's key, if it has one.
    static void add_legend_entry(std::vector<LegendEntry>& entries, const RenderSnapshot& snap,
                                 const ObjectRef& r) {
        auto keyed = [](const auto& opts) { return opts.show_legend && !opts.name.empty(); };
        {
            switch (r.kind) {
                case PlotKind::Line: {
                    // A LineStyle::None line isn't drawn, so it gets no key.
                    const auto& lp = snap.lines[r.index];
                    if (keyed(lp.opts) && lp.opts.linestyle != LineStyle::None)
                        entries.push_back({*lp.opts.color, lp.opts.name, LegendKind::Line,
                                           lp.opts.linestyle});
                    break;
                }
                case PlotKind::Scatter: {
                    const auto& sp = snap.scatters[r.index];
                    if (keyed(sp.opts)) {
                        entries.push_back({
                            marker_key_fill(*sp.opts.color, marker_face_alpha(sp.opts)), sp.opts.name,
                            LegendKind::Marker, LineStyle::Solid, sp.opts.marker,
                            marker_key_edge(*sp.opts.color, sp.opts.edgecolor, sp.opts.alpha,
                                            sp.opts.edge_linewidth)
                        });
                        line_marker_key(entries.back(), entries.back().color);
                    }
                    break;
                }
                case PlotKind::ScatterZ: {
                    // Keyed white with a black edge: the key names the shape, the
                    // colorbar explains the colors.
                    const auto& zp = snap.scatter_z[r.index];
                    if (keyed(zp.opts)) {
                        entries.push_back({
                            marker_key_fill(Color::White, marker_face_alpha(zp.opts)), zp.opts.name,
                            LegendKind::Marker,
                            LineStyle::Solid, zp.opts.marker, Color::Black
                        });
                        line_marker_key(entries.back(), Color::Black);
                    }
                    break;
                }
                case PlotKind::Bar: {
                    const auto& bp = snap.bars[r.index];
                    if (keyed(bp.opts))
                        entries.push_back({*bp.opts.color, bp.opts.name, LegendKind::Bar,
                                           LineStyle::Solid});
                    break;
                }
                case PlotKind::AxLine: {
                    // As a line's key; LineStyle::None draws nothing, so no key.
                    const auto& ap = snap.axlines[r.index];
                    if (keyed(ap.opts) && ap.opts.linestyle != LineStyle::None)
                        entries.push_back({*ap.opts.color, ap.opts.name, LegendKind::Line,
                                           ap.opts.linestyle});
                    break;
                }
                case PlotKind::AxSpan: {
                    // A patch as a bar's, at the span's own opacity (a band drawn
                    // at 0.25 is keyed at 0.25, as matplotlib's patch keys are).
                    const auto& sp = snap.axspans[r.index];
                    if (keyed(sp.opts)) {
                        Color c = *sp.opts.color;
                        c.a *= sp.opts.alpha;
                        entries.push_back({c, sp.opts.name, LegendKind::Bar, LineStyle::Solid});
                    }
                    break;
                }
                case PlotKind::Fill: {
                    // A patch at the fill's opacity, as a span's; outlined when
                    // the fill is.
                    const auto& fp = snap.fills[r.index];
                    if (keyed(fp.opts)) {
                        Color c = *fp.opts.color;
                        c.a *= fp.opts.alpha;
                        LegendEntry e{c, fp.opts.name, LegendKind::Bar, LineStyle::Solid};
                        if (fp.opts.linewidth > 0.0f && fp.opts.linestyle != LineStyle::None)
                            e.edge = fill_edge(fp);
                        entries.push_back(std::move(e));
                    }
                    break;
                }
                case PlotKind::Poly: case PlotKind::Circle: case PlotKind::Wedge: {
                    // A patch at the shape's opacity, as a fill's; outlined when
                    // the shape is.
                    const auto key = [&](const auto& o) {
                        if (!keyed(o)) return;
                        Color c = *o.color;
                        c.a *= o.alpha;
                        LegendEntry e{c, o.name, LegendKind::Bar, LineStyle::Solid};
                        if (o.linewidth > 0.0f && o.linestyle != LineStyle::None) {
                            e.edge = o.edgecolor.value_or(*o.color);
                            e.edge.a *= o.alpha;
                        }
                        entries.push_back(std::move(e));
                    };
                    if (r.kind == PlotKind::Poly) key(snap.polys[r.index].opts);
                    else if (r.kind == PlotKind::Circle) key(snap.circles[r.index].opts);
                    else key(snap.wedges[r.index].opts);
                    break;
                }
                case PlotKind::Pie: {
                    // One patch per labelled slice, named by its label.
                    const PiePlot& p = snap.pies[r.index];
                    if (!p.opts.show_legend) break;
                    for (std::size_t k = 0; k < p.labels.size() && k < p.values.size(); ++k) {
                        if (p.labels[k].empty()) continue;
                        Color c = pie_color(p.opts, k);
                        c.a *= p.opts.alpha;
                        LegendEntry e{c, p.labels[k], LegendKind::Bar, LineStyle::Solid};
                        if (p.opts.linewidth > 0.0f) e.edge = pie_edge(p.opts, k);
                        entries.push_back(std::move(e));
                    }
                    break;
                }
                case PlotKind::Quiver: {
                    // An arrow at the quiver's color and opacity; colormapped,
                    // white with a black edge (the colorbar explains the colors),
                    // as scatter_z's key.
                    const auto& qp = snap.quivers[r.index];
                    if (keyed(qp.opts) && qp.colormapped()) {
                        entries.push_back({Color::White, qp.opts.name, LegendKind::Arrow, LineStyle::Solid,
                                           MarkerStyle::Circle, Color::Black});
                    } else if (keyed(qp.opts)) {
                        Color c = *qp.opts.color;
                        c.a *= qp.opts.alpha;
                        entries.push_back({c, qp.opts.name, LegendKind::Arrow, LineStyle::Solid});
                    }
                    break;
                }
                default: break; // heatmaps and heatmap_tri: explained by their colorbar
            }
        }
    }

    // In call order (call_order(), not draw_order(): zorder does not reorder it).
    std::vector<LegendEntry> collect_legend_entries(const RenderSnapshot& snap) {
        std::vector<LegendEntry> entries;
        for (const ObjectRef& r: call_order(snap)) add_legend_entry(entries, snap, r);
        return entries;
    }

    // The layers of a 2D cell: its own axes, then its twins.
    static std::vector<const RenderSnapshot*> cell_layers(const FigureAxesSnapshot& fa) {
        std::vector<const RenderSnapshot*> out;
        if (const RenderSnapshot* s = fa.snap2d()) out.push_back(s);
        for (const TwinSnapshot& t: fa.twins) out.push_back(&t.snap);
        return out;
    }

    std::vector<LegendEntry> collect_legend_entries(const FigureAxesSnapshot& fa) {
        if (const RenderSnapshot3D* s3 = fa.snap3d())
            return s3->legend_enabled ? collect_legend_entries(*s3) : std::vector<LegendEntry>{};
        const RenderSnapshot& s = *fa.snap2d();
        if (!s.legend_enabled) return {};
        if (fa.twins.empty()) return collect_legend_entries(s);
        const std::vector<const RenderSnapshot*> layers = cell_layers(fa);
        std::vector<LegendEntry> entries;
        for (const LayerRef& r: layered_call_order<RenderSnapshot>(layers))
            add_legend_entry(entries, *layers[r.layer], r.ref);
        return entries;
    }

    std::vector<ColorbarRequest> cell_colorbar_requests(const FigureAxesSnapshot& fa) {
        if (const RenderSnapshot3D* s3 = fa.snap3d()) return find_colorbar_requests(*s3);
        std::vector<ColorbarRequest> reqs = find_colorbar_requests(*fa.snap2d());
        for (const TwinSnapshot& t: fa.twins) {
            auto one = find_colorbar_requests(t.snap);
            reqs.insert(reqs.end(), one.begin(), one.end());
        }
        return reqs;
    }

    // 3D: the axes' own kinds first, in call order, then each plane's (same
    // function per sheet). A surface is keyed only when flat; a colormapped one
    // is explained by its colorbar.
    std::vector<LegendEntry> collect_legend_entries(const RenderSnapshot3D& snap) {
        std::vector<LegendEntry> entries;
        auto keyed = [](const auto& opts) { return opts.show_legend && !opts.name.empty(); };
        for (const ObjectRef& r: call_order(snap)) {
            switch (r.kind) {
                case PlotKind::Bar3D: {
                    const auto& bp = snap.bars3d[r.index];
                    if (keyed(bp.opts))
                        entries.push_back({*bp.opts.color, bp.opts.name, LegendKind::Bar,
                                           LineStyle::Solid});
                    break;
                }
                case PlotKind::Surface: {
                    const auto& sp = snap.surfaces[r.index];
                    if (keyed(sp.opts) && !sp.opts.colormap)
                        entries.push_back({*sp.opts.color, sp.opts.name, LegendKind::Bar,
                                           LineStyle::Solid});
                    break;
                }
                case PlotKind::Scatter3D: {
                    // A cloud: its color when flat, white with a black edge when
                    // colormapped. Kept in the legend either way (it has no other
                    // shape to recognize).
                    const auto& sc = snap.scatter3d[r.index];
                    if (keyed(sc.opts) && sc.opts.marker != MarkerStyle::None) {
                        entries.push_back({
                            marker_key_fill(sc.colormapped() ? Color::White : *sc.opts.color,
                                            marker_face_alpha(sc.opts)),
                            sc.opts.name, LegendKind::Marker, LineStyle::Solid,
                            sc.opts.marker,
                            sc.colormapped()
                                ? Color::Black
                                : marker_key_edge(*sc.opts.color, sc.opts.edgecolor,
                                                  sc.opts.alpha, sc.opts.edge_linewidth)
                        });
                        line_marker_key(entries.back(),
                                        sc.colormapped() ? Color::Black : entries.back().color);
                    }
                    break;
                }
                case PlotKind::Line3D: {
                    // A path: its color when flat, the colormap swept along the stroke
                    // when not (see LegendEntry::swept). No linestyle gate (paths have
                    // no LineStyle).
                    const auto& lp = snap.lines3d[r.index];
                    if (keyed(lp.opts) && lp.opts.linewidth > 0.0f)
                        entries.push_back({
                            *lp.opts.color, lp.opts.name, LegendKind::Line,
                            LineStyle::Solid, MarkerStyle::Circle,
                            Color{0.0f, 0.0f, 0.0f, 0.0f},
                            lp.colormapped(), lp.opts.cmap
                        });
                    break;
                }
                case PlotKind::SurfaceTri: {
                    // A mesh follows the surface rule: keyed only when flat.
                    const auto& sm = snap.surface_tri[r.index];
                    if (keyed(sm.opts) && !sm.colormapped())
                        entries.push_back({*sm.opts.color, sm.opts.name, LegendKind::Bar,
                                           LineStyle::Solid});
                    break;
                }
                case PlotKind::Quiver3D: {
                    // As the 2D quiver's: an arrow at the field's colour and
                    // opacity; colormapped, white with a black edge.
                    const auto& qp = snap.quivers3d[r.index];
                    if (keyed(qp.opts) && qp.colormapped()) {
                        entries.push_back({Color::White, qp.opts.name, LegendKind::Arrow, LineStyle::Solid,
                                           MarkerStyle::Circle, Color::Black});
                    } else if (keyed(qp.opts)) {
                        Color c = *qp.opts.color;
                        c.a *= qp.opts.alpha;
                        entries.push_back({c, qp.opts.name, LegendKind::Arrow, LineStyle::Solid});
                    }
                    break;
                }
                default: break;
            }
        }
        for (const auto& pl: snap.planes) {
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

    // Plane colorbars and the legend belong to the cell: carved beside the frame
    // (the box shrinks to fit) with the same arithmetic as 2D, so the inverse
    // matches. The legend switch and all styling are the axes'.
    CellDecorations compute_cell_decorations(const RenderSnapshot3D& snap) {
        CellDecorations d;
        fill_colorbars(d, find_colorbar_requests(snap), snap.colorbar_opts);
        if (snap.legend_enabled)
            fill_legend(d, collect_legend_entries(snap), snap.legend_opts);
        return d;
    }

    CellDecorations compute_cell_decorations(const FigureAxesSnapshot& fa) {
        if (fa.twins.empty())
            return visit_cell(fa, [](const auto& s) { return compute_cell_decorations(s); });
        const RenderSnapshot& s = *fa.snap2d();
        CellDecorations d;
        fill_colorbars(d, cell_colorbar_requests(fa), s.colorbar_opts);
        if (s.legend_enabled)
            fill_legend(d, collect_legend_entries(fa), s.legend_opts);
        return d;
    }

    FigureMeasure measure_figure(const FigureSnapshot& fsnap, const FigureMeasure* frozen) {
        // FigureOptions::mathtext for every label measured below.
        const MathTextScope math(fsnap.mathtext, fsnap.math_font);
        return measure_with(fsnap, prepare_all(fsnap), frozen);
    }

    bool measure_fits(const FigureMeasure& m, const FigureSnapshot& fsnap) {
        if (m.cells.size() != fsnap.axes.size()) return false;
        for (std::size_t i = 0; i < m.cells.size(); ++i) {
            const CellMeasure& c = m.cells[i];
            const FigureAxesSnapshot& fa = fsnap.axes[i];
            if (c.slot.index != fa.slot.index || c.slot.last != fa.slot.last
                || c.slot.rows != fa.slot.rows || c.slot.cols != fa.slot.cols
                || c.is3d != fa.is_3d())
                return false;
            // Counts only; changed names or scales keep the stored sizes until the
            // next refit.
            const bool counts_match = collect_legend_entries(fa).size() == c.dec.legend_entries.size()
                                      && cell_colorbar_requests(fa).size() == c.dec.colorbars.size();
            if (!counts_match) return false;
        }
        return true;
    }

    // grid_tracks() at a fractional figure size (the inverse solves between pixels).
    static GridTracks tracks_at(const FigureSnapshot& fsnap, float suptitle_band, float fig_w, float fig_h) {
        int rows = 1, cols = 1;
        if (!fsnap.axes.empty()) {
            rows = std::max(1, fsnap.axes.front().slot.rows);
            cols = std::max(1, fsnap.axes.front().slot.cols);
        }
        const auto& m = fsnap.margins;
        GridTracks t;
        tracks_1d(fig_w, m.left, m.right, fsnap.col_gap, grid_weights(fsnap.col_ratios, cols), t.col_x, t.col_w);
        tracks_1d(fig_h, suptitle_band + m.top, m.bottom, fsnap.row_gap, grid_weights(fsnap.row_ratios, rows),
                  t.row_y, t.row_h);
        return t;
    }

    // The smallest `x` at which `len(x)` reaches `want`, `len` non-decreasing.
    template <class Len>
    static float solve_size(float want, Len len) {
        float lo = 0.0f, hi = 256.0f;
        for (int k = 0; k < 40 && len(hi) < want; ++k) hi *= 2.0f;
        for (int k = 0; k < 64 && hi - lo > 1e-4f; ++k) {
            const float mid = 0.5f * (lo + hi);
            (len(mid) < want ? lo : hi) = mid;
        }
        return hi;
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

        std::size_t si = 0;
        for (std::size_t i = 0; i < measure.cells.size(); ++i)
            if (measure.cells[i].slot.index == slot_index) {
                si = i;
                break;
            }

        // Every frame grows with the figure (a group member's too), so each
        // side is a monotonic search: the width first, since the heights depend
        // on it (a wrapped legend) and not the other way round.
        const float band = measure.suptitle_band;
        const auto fit_at = [&](float w, float h) {
            return fit_cells(fsnap, measure, tracks_at(fsnap, band, w, h))[si];
        };
        const float w = solve_size(std::max(kMinFrameSize, frame_w),
                                   [&](float x) { return fit_at(x, 1.0f).frame_w; });
        const float h = solve_size(std::max(kMinFrameSize, frame_h),
                                   [&](float y) { return fit_at(w, y).frame_h; });
        return {w, h};
    }

    std::vector<float> grid_weights(const std::vector<float>& ratios, int n) {
        n = std::max(1, n);
        if (static_cast<int>(ratios.size()) == n) {
            bool ok = true;
            for (float r: ratios) ok = ok && std::isfinite(r) && r > 0.0f;
            if (ok) return ratios;
        }
        return std::vector<float>(static_cast<std::size_t>(n), 1.0f);
    }

    GridTracks grid_tracks(const FigureSnapshot& fsnap, float suptitle_band, int fig_w, int fig_h) {
        return tracks_at(fsnap, suptitle_band, static_cast<float>(fig_w), static_cast<float>(fig_h));
    }

    float middle_baseline_offset(const std::string& font_path, float fontsize) {
        // FONS_ALIGN_MIDDLE shifts the baseline by (ascender + descender) / 2;
        // this is the distance from a centred anchor to SVG's baseline.
        const auto vm = font_vmetrics(font_path, fontsize);
        return (vm.ascent + vm.descent) * 0.5f;
    }

    float top_baseline_offset(const std::string& font_path, float fontsize) {
        return font_vmetrics(font_path, fontsize).ascent;
    }

    float middle_baseline_offset(const std::string& font_path, float fontsize, std::string_view text) {
        const auto vm = label_vmetrics(font_path, fontsize, text);
        return (vm.ascent + vm.descent) * 0.5f;
    }

    float top_baseline_offset(const std::string& font_path, float fontsize, std::string_view text) {
        return label_vmetrics(font_path, fontsize, text).ascent;
    }

    namespace {
        // The legend box and swatches in figure pixels for the requested anchor. `mg`
        // is the distance from the anchor corner; the offset moves the box only.
        void place_legend(CellLayout& c, const LegendOptions& lo, const LegendRows& rows) {
            const PlotRect& f = c.frame;
            const PlotRect& e = c.extended;
            const float mg = std::max(0.0f, lo.margin);
            const float w = rows.w, h = rows.h;
            float x = 0.0f, y = 0.0f;
            switch (lo.anchor) {
                case LegendAnchor::InsideTL: x = f.x + mg;
                    y = f.y + mg;
                    break;
                case LegendAnchor::InsideTR: x = f.x + f.w - mg - w;
                    y = f.y + mg;
                    break;
                case LegendAnchor::InsideBL: x = f.x + mg;
                    y = f.y + f.h - mg - h;
                    break;
                case LegendAnchor::InsideBR: x = f.x + f.w - mg - w;
                    y = f.y + f.h - mg - h;
                    break;
                case LegendAnchor::OutsideTL: x = e.x + mg;
                    y = e.y - mg - h;
                    break;
                case LegendAnchor::OutsideTR: x = e.x + e.w - mg - w;
                    y = e.y - mg - h;
                    break;
                case LegendAnchor::OutsideBL: x = e.x + mg;
                    y = e.y + e.h + mg;
                    break;
                case LegendAnchor::OutsideBR: x = e.x + e.w - mg - w;
                    y = e.y + e.h + mg;
                    break;
                case LegendAnchor::OutsideLT: x = e.x - mg - w;
                    y = e.y + mg;
                    break;
                case LegendAnchor::OutsideLB: x = e.x - mg - w;
                    y = e.y + e.h - mg - h;
                    break;
                case LegendAnchor::OutsideRT: x = e.x + e.w + mg;
                    y = e.y + mg;
                    break;
                case LegendAnchor::OutsideRB: x = e.x + e.w + mg;
                    y = e.y + e.h - mg - h;
                    break;
            }
            x += lo.offset_x;
            y += lo.offset_y;
            c.legend = {x, y, w, h};
            c.legend_slots.reserve(rows.slots.size());
            for (const LegendSlot& s: rows.slots) c.legend_slots.push_back({x + s.x, y + s.cy});
        }

        // A stepped bar's boundary labels: boundary k of n bands sits k/n along
        // the bar from vmin's end, at the numbers' anchor line. Every k-th is
        // kept, the smallest stride whose neighbours don't overlap (a line
        // height apart beside a vertical bar, their half widths plus a gap
        // along a horizontal one); the last boundary always shows, replacing
        // the one before it when they would overlap.
        std::vector<ColorbarBox::Label> colorbar_bound_labels(const ColorbarBox& b, const std::vector<double>& bounds,
                                                              const ColorbarOptions& cbo, float lh) {
            const std::size_t n = bounds.size() - 1;
            std::vector<ColorbarBox::Label> all(bounds.size());
            std::vector<float> w(bounds.size(), 0.0f);
            for (std::size_t k = 0; k < bounds.size(); ++k) {
                const float f = static_cast<float>(k) / static_cast<float>(n);
                all[k].text = colorbar_number(static_cast<float>(bounds[k]));
                if (b.horizontal) {
                    all[k].x = b.rect.x + f * b.rect.w;
                    all[k].y = b.vmin_y;
                    w[k] = text_width(cbo.font_path, cbo.fontsize, all[k].text);
                } else {
                    all[k].x = b.vmin_x;
                    all[k].y = b.rect.y + b.rect.h - f * b.rect.h;
                }
            }
            auto apart = [&](std::size_t i, std::size_t j) {
                if (b.horizontal) return std::fabs(all[j].x - all[i].x) >= 0.5f * (w[i] + w[j]) + kColorbarLabelGap;
                return std::fabs(all[j].y - all[i].y) >= lh;
            };
            std::vector<std::size_t> keep;
            for (std::size_t stride = 1; stride <= n; ++stride) {
                keep.clear();
                for (std::size_t k = 0; k <= n; k += stride) keep.push_back(k);
                if (keep.back() != n) {
                    if (keep.size() > 1 && !apart(keep.back(), n)) keep.back() = n;
                    else keep.push_back(n);
                }
                bool ok = true;
                for (std::size_t i = 1; ok && i < keep.size(); ++i) ok = apart(keep[i - 1], keep[i]);
                if (ok) break;
            }
            std::vector<ColorbarBox::Label> out;
            out.reserve(keep.size());
            for (std::size_t k: keep) out.push_back(std::move(all[k]));
            return out;
        }

        // Each bar in its block, outward from `start` (extended frame edge, or beyond a
        // legend on that side). Blocks come from compute_cell_decorations().
        void place_colorbars(CellLayout& c, const CellDecorations& dec, const ColorbarOptions& cbo,
                             DecorSide side, float start) {
            const PlotRect& f = c.frame;
            const float mg = std::max(0.0f, cbo.margin);
            const float bw = std::max(0.0f, cbo.width);
            const float lh = font_vmetrics(cbo.font_path, cbo.fontsize).line_height;
            const float ox = cbo.offset_x, oy = cbo.offset_y;

            float s = start;
            c.colorbars.reserve(dec.colorbars.size());
            for (const ColorbarSpec& spec: dec.colorbars) {
                // The name's own height: math in it may reach past the font's.
                const float name_lh = title_height(spec.name, cbo.font_path, cbo.fontsize);
                ColorbarBox b;
                b.cmap = spec.cmap;
                b.vmin = spec.vmin;
                b.vmax = spec.vmax;
                b.name = spec.name;
                switch (side) {
                    case DecorSide::Left:
                        b.rect = {s - mg - bw, f.y, bw, f.h};
                        b.num_align = HAlign::Right;
                        b.vmin_x = b.vmax_x = b.rect.x - kColorbarLabelGap;
                        b.vmax_y = f.y;
                        b.vmin_y = f.y + f.h;
                        // Half a line inside the block's outer edge, centred down the
                        // frame: outboard of the numbers.
                        b.name_x = s - spec.block + name_lh * 0.5f;
                        b.name_y = f.y + f.h * 0.5f;
                        s -= spec.block;
                        break;
                    case DecorSide::Top:
                        b.rect = {f.x, s - mg - bw, f.w, bw};
                        b.horizontal = true;
                        b.num_align = HAlign::Center;
                        b.vmin_x = f.x;
                        b.vmax_x = f.x + f.w;
                        b.vmin_y = b.vmax_y = b.rect.y - kColorbarLabelGap - lh * 0.5f;
                        b.name_x = f.x + f.w * 0.5f;
                        b.name_y = s - spec.block + name_lh * 0.5f;
                        s -= spec.block;
                        break;
                    case DecorSide::Bottom:
                        b.rect = {f.x, s + mg, f.w, bw};
                        b.horizontal = true;
                        b.num_align = HAlign::Center;
                        b.vmin_x = f.x;
                        b.vmax_x = f.x + f.w;
                        b.vmin_y = b.vmax_y = b.rect.y + bw + kColorbarLabelGap + lh * 0.5f;
                        b.name_x = f.x + f.w * 0.5f;
                        b.name_y = s + spec.block - name_lh * 0.5f;
                        s += spec.block;
                        break;
                    default: // Right
                        b.rect = {s + mg, f.y, bw, f.h};
                        b.num_align = HAlign::Left;
                        b.vmin_x = b.vmax_x = b.rect.x + bw + kColorbarLabelGap;
                        b.vmax_y = f.y;
                        b.vmin_y = f.y + f.h;
                        b.name_x = s + spec.block - name_lh * 0.5f;
                        b.name_y = f.y + f.h * 0.5f;
                        s += spec.block;
                        break;
                }
                // Placed from the block (where the space was reserved); an unnamed bar
                // gets no anchor.
                b.rect.x += ox;
                b.rect.y += oy;
                b.vmin_x += ox;
                b.vmin_y += oy;
                b.vmax_x += ox;
                b.vmax_y += oy;
                if (b.name.empty()) {
                    b.name_x = 0.0f;
                    b.name_y = 0.0f;
                } else {
                    b.name_x += ox;
                    b.name_y += oy;
                }
                if (spec.band_t.size() + 1 == spec.bounds.size() && !spec.band_t.empty()) {
                    b.band_t = spec.band_t;
                    b.labels = colorbar_bound_labels(b, spec.bounds, cbo, lh);
                }
                c.colorbars.push_back(std::move(b));
            }
        }

        // An axis' span in scale units, or 0 when it has none to measure by.
        double scale_span(const AxisScale& s, double lo, double hi) {
            const double d = std::fabs(s.fwd(hi) - s.fwd(lo));
            return std::isfinite(d) ? d : 0.0;
        }

        // Axes::set_aspect() with Adjustable::Box: the frame shrunk, centred, to
        // the height-to-width ratio that draws a y unit `ratio` x units long.
        PlotRect aspect_frame(const PlotRect& f, const RenderSnapshot& s, const ResolvedLimits& l) {
            const double sx = scale_span(s.xscale, l.xmin, l.xmax);
            const double sy = scale_span(s.yscale, l.ymin, l.ymax);
            if (!(sx > 0.0) || !(sy > 0.0)) return f;
            const double want = s.aspect.ratio * sy / sx;   // frame h / w
            PlotRect r = f;
            if (static_cast<double>(f.h) > static_cast<double>(f.w) * want) {
                r.h = std::max(1.0f, static_cast<float>(static_cast<double>(f.w) * want));
                r.y += (f.h - r.h) * 0.5f;
            } else {
                r.w = std::max(1.0f, static_cast<float>(static_cast<double>(f.h) / want));
                r.x += (f.w - r.w) * 0.5f;
            }
            return r;
        }

        // Adjustable::Datalim: the cell's own axes' limits with one axis widened
        // about its centre (in scale space) so the frame `f` draws the ratio --
        // the unshared axis when the other is shared, else whichever must grow.
        // Both shared is refused by the setters; drawn unwidened.
        std::optional<ResolvedLimits> aspect_limits(const RenderSnapshot& s, const ResolvedLimits& l,
                                                    const PlotRect& f) {
            const bool xs = s.xshare != 0, ys = s.yshare != 0;
            if (xs && ys) return std::nullopt;
            const double sx = scale_span(s.xscale, l.xmin, l.xmax);
            const double sy = scale_span(s.yscale, l.ymin, l.ymax);
            if (!(sx > 0.0) || !(sy > 0.0) || !(f.w > 0.0f) || !(f.h > 0.0f)) return std::nullopt;
            const double r = s.aspect.ratio;
            const double need_y = static_cast<double>(f.h) * sx / (r * static_cast<double>(f.w));
            const double need_x = r * static_cast<double>(f.w) * sy / static_cast<double>(f.h);
            const bool widen_y = xs || (!ys && need_y > sy);
            // About the centre, keeping the limits' direction.
            const auto about = [](const AxisScale& sc, double& lo, double& hi, double span) {
                const double a = sc.fwd(lo), b = sc.fwd(hi);
                const double c = (a + b) * 0.5;
                const double half = (b < a ? -span : span) * 0.5;
                lo = sc.inv(c - half);
                hi = sc.inv(c + half);
            };
            ResolvedLimits out = l;
            if (widen_y) about(s.yscale, out.ymin, out.ymax, need_y);
            else about(s.xscale, out.xmin, out.xmax, need_x);
            return out;
        }

        // One 2D axes' part of a cell's layout, its frame already placed: transform,
        // ticks, axis lines and label and title anchors. `al`: the cell's own
        // furniture (reach + axis titles, cell_o); fw/fh the figure size.
        void layout_axes(CellLayout& c, const RenderSnapshot& s2, const CellPrep& p, const PlotInsets& al,
                         float fw, float fh) {
            const AxesStyle& st = s2.axes_style;
            c.xticks = p.xticks;
            c.yticks = p.yticks;
            c.xminor = p.xminor;
            c.yminor = p.yminor;
            c.xlabels = p.xlabels;
            c.ylabels = p.ylabels;

            c.tr = {
                p.limits.xmin, p.limits.xmax,
                p.limits.ymin, p.limits.ymax,
                c.frame.x, c.frame.y, c.frame.w, c.frame.h, fw, fh,
                s2.xscale, s2.yscale
            };

            // Axis lines through the transform, so Mid and pins need no special
            // case and Low/High land exactly on the edges.
            const AxisPlacement& xa = p.xaxis;
            const AxisPlacement& ya = p.yaxis;
            c.xaxis_y = c.tr.to_py(xa.pos);
            c.yaxis_x = c.tr.to_px(ya.pos);
            c.xaxis_interior = xa.interior;
            c.yaxis_interior = ya.interior;
            c.xtick_dir = xa.high ? -1.0f : 1.0f;
            c.ytick_dir = ya.high ? 1.0f : -1.0f;

            // x labels keep one top-aligned anchor; y labels flip alignment to
            // face their axis.
            const TickLook xk = tick_look(st, TickAxis::X), yk = tick_look(st, TickAxis::Y);
            const float xoff = xk.length + kTickLabelGap;
            const float label_lh = tallest_label(p.xticks, st.font_path, xk.label_fontsize);
            c.xlabel_top = c.xtick_dir > 0.0f
                               ? c.xaxis_y + xoff
                               : c.xaxis_y - xoff - label_lh;
            c.ylabel_x = c.yaxis_x + c.ytick_dir * (yk.length + kTickLabelGap);
            c.ylabel_align = c.ytick_dir > 0.0f ? HAlign::Left : HAlign::Right;

            // Just outside the cell's furniture (cell_a), hugging the frame (a
            // twin's on its own axis' side).
            const float frame_cx = c.frame.x + c.frame.w * 0.5f;
            const float xtitle_lh = title_height(s2.xtitle, st.font_path, st.xtitle_fontsize);
            c.xtitle_x = frame_cx;
            c.xtitle_y = p.xtitle_top ? c.frame.y - al.top + xtitle_lh * 0.5f
                                      : c.frame.y + c.frame.h + al.bottom - xtitle_lh * 0.5f;

            const float ytitle_lh = title_height(s2.ytitle, st.font_path, st.ytitle_fontsize);
            c.ytitle_x = p.ytitle_right ? c.frame.x + c.frame.w + al.right - ytitle_lh * 0.5f
                                        : c.frame.x - al.left + ytitle_lh * 0.5f;
            c.ytitle_y = c.frame.y + c.frame.h * 0.5f;
        }

        // A polar cell's part, its frame already placed: the transform for its
        // unit-space lines and the plan both drawers draw the frame from.
        void layout_polar(CellLayout& c, const RenderSnapshotPolar& sp, const CellPrep& p, float fw, float fh) {
            const RenderSnapshot& s2 = sp.objects;
            const AxesStyle& st = s2.axes_style;
            const PolarMap& m = p.pmap;
            c.tr = polar_transform(c.frame, m, fw, fh);
            PolarPlan pl;
            pl.map = m;
            pl.cx = static_cast<float>(c.tr.to_px(0.0));
            pl.cy = static_cast<float>(c.tr.to_py(0.0));
            const PolarBox b = polar_box(m);
            pl.radius = c.frame.w / static_cast<float>(b.xmax - b.xmin);
            pl.tlabels = s2.axis_on && st.show_xticks;
            pl.rlabels = s2.axis_on && st.show_yticks;

            const TickLook xk = tick_look(st, TickAxis::X);
            for (const Tick& t: p.xticks) {
                const double a = m.angle(t.value * kPolarDeg);
                const float w = label_width(st.font_path, xk.label_fontsize, t.label);
                const float h = label_vmetrics(st.font_path, xk.label_fontsize, t.label).line_height;
                const float d = pl.radius + kPolarLabelGap + polar_box_reach(w, h, a);
                pl.spokes.push_back({t.value, t.label, pl.cx + d * static_cast<float>(std::cos(a)),
                                     pl.cy - d * static_cast<float>(std::sin(a))});
            }
            // Along rlabel_position, kept inside a sector's span.
            double at = sp.style.rlabel_position;
            if (!m.full()) at = std::clamp(at, sp.view.thetamin, sp.view.thetamax);
            const double ra = m.angle(at * kPolarDeg);
            for (const Tick& t: p.yticks) {
                const float rr = pl.radius * static_cast<float>(m.rho(t.value));
                pl.rings.push_back({t.value, t.label, pl.cx + rr * static_cast<float>(std::cos(ra)),
                                    pl.cy - rr * static_cast<float>(std::sin(ra))});
            }
            c.polar = std::move(pl);
        }

        // Layout from stored measurements plus this frame's limits and ticks.
        FigureLayout layout_with(const FigureSnapshot& fsnap, const std::vector<CellPrep>& prep,
                                 const FigureMeasure& measure, int fig_w, int fig_h) {
            FigureLayout out;
            out.suptitle_band = measure.suptitle_band;

            const float fw = static_cast<float>(fig_w), fh = static_cast<float>(fig_h);

            const std::vector<CellMeasure>& ms = measure.cells;
            const GridTracks t = grid_tracks(fsnap, out.suptitle_band, fig_w, fig_h);

            // A cell is the whole subplot, so gaps are between subplots. A span
            // runs from its first cell to its last, including inner gaps. Track
            // sizes are weighted shares. Every frame is sized at once (aligned
            // ones depend on each other).
            const std::vector<CellFit> fits = fit_cells(fsnap, measure, t);

            out.cells.reserve(fsnap.axes.size());
            for (std::size_t i = 0; i < fsnap.axes.size(); ++i) {
                const auto& fa = fsnap.axes[i];
                const CellMeasure& me = ms[i];
                const AxesSlot& s = fa.slot;
                const CellFit& fit = fits[i];
                const LegendRows& rows = fit.rows;

                const float cx = t.col_x[s.col0()];
                const float cy = t.row_y[s.row0()];
                const float cw = t.col_x[s.col1()] + t.col_w[s.col1()] - cx;
                const float ch = t.row_y[s.row1()] + t.row_h[s.row1()] - cy;

                CellLayout c;
                c.slot = s;
                c.cell = {cx, cy, cw, ch};
                c.reserved = fit.res;
                c.frame = {cx + c.reserved.left, cy + c.reserved.top, fit.frame_w, fit.frame_h};
                // Axes::set_aspect(): Box shrinks the frame before anything is
                // placed around it, so all of it follows; Datalim widens the
                // limits below instead.
                const RenderSnapshot* own2d = fa.snap2d();
                if (own2d && own2d->aspect.ratio > 0.0 && own2d->aspect.adjustable == Adjustable::Box)
                    c.frame = aspect_frame(c.frame, *own2d, prep[i].limits);
                // A polar frame is as round as its box.
                if (prep[i].polar) c.frame = polar_frame(c.frame, prep[i].pmap);

                // cell_o around the frame, the cell's own: everything else hugs
                // it, so an aligned group's slack lies at the cell edge.
                const PlotInsets al = {
                    me.reach_l.at(c.frame.w) + me.titles.left, me.reach_r.at(c.frame.w) + me.titles.right,
                    me.reach_t.at(c.frame.h) + me.titles.top, me.reach_b.at(c.frame.h) + me.titles.bottom
                };
                const float fm = me.frame_margin;
                c.extended = {
                    c.frame.x - al.left - fm, c.frame.y - al.top - fm,
                    c.frame.w + al.left + al.right + 2.0f * fm, c.frame.h + al.top + al.bottom + 2.0f * fm
                };

                const AxesStyle& st = visit_cell(
                    fa, [](const auto& sn) -> const AxesStyle& { return sn.axes_style; });
                const LegendOptions& lo = visit_cell(
                    fa, [](const auto& sn) -> const LegendOptions& { return sn.legend_opts; });
                const ColorbarOptions& cbo = visit_cell(
                    fa, [](const auto& sn) -> const ColorbarOptions& { return sn.colorbar_opts; });

                // The axes title: atop cell_d (beyond a top legend and colorbars),
                // centred on the frame (as in 2D).
                const float frame_cx = c.frame.x + c.frame.w * 0.5f;
                c.title_x = frame_cx;
                const std::string& title = visit_cell(
                    fa, [](const auto& sn) -> const std::string& { return sn.title; });
                c.title_y = c.extended.y - top_decor(me, rows) - me.title
                            + title_height(title, st.font_path, st.title_fontsize) * 0.5f;

                // Legend first, then the bars outboard of it.
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
                        case DecorSide::Left: start = e.x - (me.legend == DecorSide::Left ? legend_w : 0.0f);
                            break;
                        case DecorSide::Top: start = e.y - (me.legend == DecorSide::Top ? legend_h : 0.0f);
                            break;
                        case DecorSide::Bottom: start = e.y + e.h + (me.legend == DecorSide::Bottom ? legend_h : 0.0f);
                            break;
                        default: start = e.x + e.w + (me.legend == DecorSide::Right ? legend_w : 0.0f);
                            break;
                    }
                    // A cell too narrow for its decorations lets the bars run past its
                    // edge rather than dropping them.
                    place_colorbars(c, me.dec, cbo, me.bars, start);
                }

                if (const RenderSnapshot3D* s3 = fa.snap3d()) {
                    // The projector last: it fits the box to the final frame.
                    c.box3d = Box3DLayout{
                        Projector3D(prep[i].tf3, s3->camera, c.frame, s3->box_style.margin),
                        prep[i].xticks, prep[i].yticks, prep[i].zticks
                    };
                } else if (const RenderSnapshotPolar* sp = fa.snappolar()) {
                    layout_polar(c, *sp, prep[i], fw, fh);
                } else {
                    // Empty for 3D: its ticks are in `box3d`. The transform
                    // carries Datalim's widened limits, so a pan keeps them.
                    std::optional<ResolvedLimits> wide;
                    if (own2d->aspect.datalim()) wide = aspect_limits(*own2d, prep[i].limits, c.frame);
                    if (wide) layout_axes(c, *own2d, reprepare(fa, *own2d, prep[i], *wide), al, fw, fh);
                    else layout_axes(c, *own2d, prep[i], al, fw, fh);
                    // Twins over the same frame; a twin's grid draws its own axis only.
                    for (std::size_t k = 0; k < fa.twins.size(); ++k) {
                        CellLayout tc;
                        tc.slot = s;
                        tc.cell = c.cell;
                        tc.frame = c.frame;
                        tc.extended = c.extended;
                        tc.reserved = c.reserved;
                        tc.layer = fa.twins[k].layer;
                        layout_axes(tc, fa.twins[k].snap, prep[i].twins[k], al, fw, fh);
                        (tc.layer == kLayerTwinX ? tc.xgrid : tc.ygrid) = false;
                        c.twins.push_back(std::move(tc));
                    }
                }

                out.cells.push_back(std::move(c));
            }

            return out;
        }
    } // namespace

    FigureLayout compute_figure_layout(const FigureSnapshot& fsnap, int fig_w, int fig_h,
                                       FigureMeasure* out_measure) {
        // FigureOptions::mathtext for every label measured below.
        const MathTextScope math(fsnap.mathtext, fsnap.math_font);
        // One prepare() per axes, shared by measure and cell pass (limits and
        // ticks once per frame).
        const std::vector<CellPrep> prep = prepare_all(fsnap);
        FigureMeasure measure = measure_with(fsnap, prep, nullptr);
        FigureLayout out = layout_with(fsnap, prep, measure, fig_w, fig_h);
        if (out_measure) *out_measure = std::move(measure);
        return out;
    }

    FigureLayout compute_figure_layout(const FigureSnapshot& fsnap, const FigureMeasure& measure,
                                       int fig_w, int fig_h) {
        // FigureOptions::mathtext for every label measured below.
        const MathTextScope math(fsnap.mathtext, fsnap.math_font);
        if (!measure_fits(measure, fsnap)) return compute_figure_layout(fsnap, fig_w, fig_h);
        return layout_with(fsnap, prepare_all(fsnap), measure, fig_w, fig_h);
    }

    FigureLayout LayoutStore::fit(const FigureSnapshot& fsnap, int fig_w, int fig_h) {
        // Generation 0 is "never stamped", never unchanged.
        const bool due = refit_ || !measure_ || fsnap.layout_generation == 0
                         || fsnap.layout_generation != generation_ || fig_w != w_ || fig_h != h_
                         || !measure_fits(*measure_, fsnap);
        if (!due) return layout_with(fsnap, prepare_all(fsnap), *measure_, fig_w, fig_h);

        auto fresh = std::make_shared<FigureMeasure>();
        FigureLayout out = compute_figure_layout(fsnap, fig_w, fig_h, fresh.get()); {
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
