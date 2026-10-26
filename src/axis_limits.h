#pragma once
// The limits an axes is drawn with: explicit ones, or the padded auto scale of
// its data (heatmap extents unpadded, see AutoAxis). One definition for layout
// and for Axes::xlim()/Axes3D::xlim().
#include "axis_placement.h"
#include "coord_transform.h"
#include "coord_transform3d.h"
#include "plot_objects.h"
#include "polar_geometry.h"
#include <optional>
#include <utility>
#include <vector>

namespace sextant {
    struct ResolvedLimits {
        double xmin, xmax, ymin, ymax;
    };

    struct ResolvedLimits3D {
        double xmin, xmax, ymin, ymax, zmin, zmax;
    };

    // One axis of a share group (Axes::sharex()): the union of its members'
    // unpadded bounds and origin pins, in scale space. resolve_limits() settles,
    // widens and pads it as it would one axes' own.
    struct SharedAuto {
        AutoAxis bounds;
        AxisSpan pins;
    };

    // Adds one member: its raw bounds `b` (auto_bounds() of its data) and its
    // origin pin on that axis.
    inline void add_to_shared(SharedAuto& g, const RenderSnapshot& m, const AutoBounds& b, bool x) {
        const AutoAxis& a = x ? b.x : b.y;
        g.bounds.loose.add(a.loose);
        g.bounds.tight.add(a.tight);
        const std::optional<double>& pin = x ? m.axes_style.origin_x : m.axes_style.origin_y;
        const AxisScale& s = x ? m.xscale : m.yscale;
        if (pin && s.valid(*pin)) g.pins.add(s.fwd(*pin));
    }

    // Automatic limits are worked out in each axis' scale space (settled,
    // widened, padded) and mapped back, so a log axis pads by a share of its
    // decades and an empty one reads 1..10. `own`, if given, is
    // auto_bounds() of this axes' data, already computed; `gx`/`gy` replace an
    // axis' own bounds and pin with its share group's.
    inline ResolvedLimits resolve_limits(const RenderSnapshot& snap, const AutoBounds* own,
                                         const SharedAuto* gx, const SharedAuto* gy) {
        ResolvedLimits r{snap.xmin, snap.xmax, snap.ymin, snap.ymax};
        const AxisScale& xs = snap.xscale;
        const AxisScale& ys = snap.yscale;
        const bool xauto = snap.xlim_auto || !limits_fit(xs, snap.xmin, snap.xmax);
        const bool yauto = snap.ylim_auto || !limits_fit(ys, snap.ymin, snap.ymax);
        if (xauto || yauto) {
            // An origin pin is folded in before padding, as data (otherwise it
            // would land exactly on the frame edge and read as Low), after the
            // axis is settled: with no data it widens 0..1. A pin the scale
            // can't place (<= 0 on log) widens nothing.
            const auto& st = snap.axes_style;
            AutoBounds b = own ? *own : auto_bounds(snap.all(), xs, ys);
            if (gx) b.x = gx->bounds;
            if (gy) b.y = gy->bounds;
            settle_axis(b.x);
            settle_axis(b.y);
            const auto in_scale = [](const std::optional<double>& pin, const AxisScale& s) {
                return pin && s.valid(*pin) ? std::optional<double>(s.fwd(*pin)) : std::nullopt;
            };
            const auto widen = [](const SharedAuto* g, const std::optional<double>& pin, bool on,
                                  AxisSpan& span) {
                if (!g) {
                    widen_for_origin(pin, on, span.lo, span.hi);
                } else if (!g->pins.empty()) {
                    widen_for_origin(g->pins.lo, on, span.lo, span.hi);
                    widen_for_origin(g->pins.hi, on, span.lo, span.hi);
                }
            };
            widen(gx, in_scale(st.origin_x, xs), xauto, b.x.loose);
            widen(gy, in_scale(st.origin_y, ys), yauto, b.y.loose);
            if (xauto) {
                pad_axis(b.x, kAutoScalePad, r.xmin, r.xmax);
                r.xmin = xs.inv(r.xmin);
                r.xmax = xs.inv(r.xmax);
            }
            if (yauto) {
                pad_axis(b.y, kAutoScalePad, r.ymin, r.ymax);
                r.ymin = ys.inv(r.ymin);
                r.ymax = ys.inv(r.ymax);
            }
        }
        return r;
    }

    inline ResolvedLimits resolve_limits(const RenderSnapshot& snap) {
        return resolve_limits(snap, nullptr, nullptr, nullptr);
    }

    // A polar axes: x is θ's span (radians), y is r's (polar_rlim()).
    inline ResolvedLimits resolve_limits(const RenderSnapshotPolar& s) {
        const Range rl = polar_rlim(s);
        return {s.view.thetamin * kPolarDeg, s.view.thetamax * kPolarDeg, rl.lo, rl.hi};
    }

    // Every 2D axes' limits in a figure, flat: each cell's own axes, then its
    // twins (FigureAxesSnapshot::twins), cell after cell; a polar cell has its
    // own (never shared) and a 3D cell one default entry. One auto_bounds() per axes, and share groups resolved over
    // all their members, twins included. Layout uses it.
    inline std::vector<ResolvedLimits> resolve_figure_limits(const FigureSnapshot& fs) {
        std::size_t n = 0;
        for (const FigureAxesSnapshot& fa: fs.axes) n += 1 + fa.twins.size();
        std::vector<ResolvedLimits> out(n, ResolvedLimits{0.0, 1.0, 0.0, 1.0});
        // Every 2D axes in one list, and where its result goes.
        struct Layer {
            const RenderSnapshot* s;
            std::size_t out;
        };
        std::vector<Layer> layers;
        layers.reserve(n);
        bool shared = false;
        std::size_t at = 0;
        for (const FigureAxesSnapshot& fa: fs.axes) {
            if (const RenderSnapshotPolar* p = fa.snappolar()) out[at] = resolve_limits(*p);
            else if (const RenderSnapshot* s = fa.snap2d()) {
                layers.push_back({s, at});
                shared = shared || s->xshare || s->yshare;
                for (std::size_t k = 0; k < fa.twins.size(); ++k) {
                    layers.push_back({&fa.twins[k].snap, at + 1 + k});
                    shared = shared || fa.twins[k].snap.xshare || fa.twins[k].snap.yshare;
                }
            }
            at += 1 + fa.twins.size();
        }
        if (!shared) {
            for (const Layer& l: layers) out[l.out] = resolve_limits(*l.s);
            return out;
        }

        // A member's auto flags are its group's, so only automatic axes need bounds.
        std::vector<std::optional<AutoBounds>> own(layers.size());
        std::vector<std::pair<unsigned long long, SharedAuto>> gx, gy;
        const auto group = [](std::vector<std::pair<unsigned long long, SharedAuto>>& v,
                              unsigned long long id) -> SharedAuto& {
            for (auto& [k, g]: v) if (k == id) return g;
            return v.emplace_back(id, SharedAuto{}).second;
        };
        for (std::size_t i = 0; i < layers.size(); ++i) {
            const RenderSnapshot* s = layers[i].s;
            const bool xauto = s->xlim_auto || !limits_fit(s->xscale, s->xmin, s->xmax);
            const bool yauto = s->ylim_auto || !limits_fit(s->yscale, s->ymin, s->ymax);
            if (!xauto && !yauto) continue;
            own[i] = auto_bounds(s->all(), s->xscale, s->yscale);
            if (s->xshare) add_to_shared(group(gx, s->xshare), *s, *own[i], true);
            if (s->yshare) add_to_shared(group(gy, s->yshare), *s, *own[i], false);
        }
        for (std::size_t i = 0; i < layers.size(); ++i) {
            const RenderSnapshot* s = layers[i].s;
            const SharedAuto* x = s->xshare && own[i] ? &group(gx, s->xshare) : nullptr;
            const SharedAuto* y = s->yshare && own[i] ? &group(gy, s->yshare) : nullptr;
            out[layers[i].out] = resolve_limits(*s, own[i] ? &*own[i] : nullptr, x, y);
        }
        return out;
    }

    // Each axis independent.
    inline ResolvedLimits3D resolve_limits(const RenderSnapshot3D& snap) {
        double xmin = snap.xmin, xmax = snap.xmax;
        double ymin = snap.ymin, ymax = snap.ymax;
        double zmin = snap.zmin, zmax = snap.zmax;
        // Only with shown data: an empty axes keeps its declared 0..1.
        bool has_data = any_shown(snap.bars3d) || any_shown(snap.surfaces) ||
                        any_shown(snap.scatter3d) || any_shown(snap.lines3d) ||
                        any_shown(snap.surface_tri) || any_shown(snap.quivers3d);
        for (const auto& pl: snap.planes) has_data = has_data || plane_has_data(pl);
        // Origin pins on automatic axes are folded in before padding, as data
        // (as in 2D).
        const auto& st = snap.axes_style;
        if (has_data && (snap.xlim_auto || snap.ylim_auto || snap.zlim_auto)) {
            const DataBounds3D b = auto_scale3d(snap.bars3d, snap.planes, snap.surfaces, snap.scatter3d,
                                                snap.lines3d, snap.surface_tri, kAutoScalePad,
                                                {snap.xlim_auto ? st.origin_x : std::nullopt,
                                                 snap.ylim_auto ? st.origin_y : std::nullopt,
                                                 snap.zlim_auto ? st.origin_z : std::nullopt},
                                                snap.quivers3d);
            if (snap.xlim_auto) {
                xmin = b.xmin;
                xmax = b.xmax;
            }
            if (snap.ylim_auto) {
                ymin = b.ymin;
                ymax = b.ymax;
            }
            if (snap.zlim_auto) {
                zmin = b.zmin;
                zmax = b.zmax;
            }
        } else if (st.origin_x || st.origin_y || st.origin_z) {
            // No data: the declared limits, widened to a pin on an automatic
            // axis and padded.
            struct PinnedAxis {
                bool automatic;
                const std::optional<double>* pin;
                double* lo;
                double* hi;
            };
            const PinnedAxis ax[3] = {
                {snap.xlim_auto, &st.origin_x, &xmin, &xmax},
                {snap.ylim_auto, &st.origin_y, &ymin, &ymax},
                {snap.zlim_auto, &st.origin_z, &zmin, &zmax},
            };
            for (const PinnedAxis& a: ax) {
                // Fixed limits are never widened or padded (the pin is clamped
                // instead); an axes with no data keeps 0..1 unless the pin is on
                // this axis.
                if (!a.automatic || !a.pin->has_value()) continue;
                widen_for_origin(*a.pin, true, *a.lo, *a.hi);
                const double d = (*a.hi - *a.lo) * kAutoScalePad;
                *a.lo -= d;
                *a.hi += d;
            }
        }
        return {xmin, xmax, ymin, ymax, zmin, zmax};
    }
} // namespace sextant
