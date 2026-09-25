#pragma once
// The limits an axes is drawn with: explicit ones, or the padded auto scale of
// its data (heatmap extents unpadded, see AutoAxis). One definition for layout
// and for Axes::xlim()/Axes3D::xlim().
#include "axis_placement.h"
#include "coord_transform.h"
#include "coord_transform3d.h"
#include "plot_objects.h"
#include <optional>

namespace sextant {
    struct ResolvedLimits {
        double xmin, xmax, ymin, ymax;
    };

    struct ResolvedLimits3D {
        double xmin, xmax, ymin, ymax, zmin, zmax;
    };

    inline ResolvedLimits resolve_limits(const RenderSnapshot& snap) {
        ResolvedLimits r{snap.xmin, snap.xmax, snap.ymin, snap.ymax};
        if (snap.xlim_auto || snap.ylim_auto) {
            // An origin pin is folded in before padding, as data (otherwise it
            // would land exactly on the frame edge and read as Low), after the
            // axis is settled: with no data it widens 0..1.
            const auto& st = snap.axes_style;
            AutoBounds b = auto_bounds(snap.all());
            settle_axis(b.x);
            settle_axis(b.y);
            widen_for_origin(st.origin_x, snap.xlim_auto, b.x.loose.lo, b.x.loose.hi);
            widen_for_origin(st.origin_y, snap.ylim_auto, b.y.loose.lo, b.y.loose.hi);
            if (snap.xlim_auto) pad_axis(b.x, kAutoScalePad, r.xmin, r.xmax);
            if (snap.ylim_auto) pad_axis(b.y, kAutoScalePad, r.ymin, r.ymax);
        }
        return r;
    }

    // Each axis independent.
    inline ResolvedLimits3D resolve_limits(const RenderSnapshot3D& snap) {
        double xmin = snap.xmin, xmax = snap.xmax;
        double ymin = snap.ymin, ymax = snap.ymax;
        double zmin = snap.zmin, zmax = snap.zmax;
        // Only with data: an empty axes keeps its declared 0..1.
        bool has_data = !snap.bars3d.empty() || !snap.surfaces.empty() ||
                        !snap.scatter3d.empty() || !snap.lines3d.empty() ||
                        !snap.surface_tri.empty();
        for (const auto& pl: snap.planes) has_data = has_data || plane_has_data(pl);
        // Origin pins on automatic axes are folded in before padding, as data
        // (as in 2D).
        const auto& st = snap.axes_style;
        if (has_data && (snap.xlim_auto || snap.ylim_auto || snap.zlim_auto)) {
            const DataBounds3D b = auto_scale3d(snap.bars3d, snap.planes, snap.surfaces, snap.scatter3d,
                                                snap.lines3d, snap.surface_tri, kAutoScalePad,
                                                {snap.xlim_auto ? st.origin_x : std::nullopt,
                                                 snap.ylim_auto ? st.origin_y : std::nullopt,
                                                 snap.zlim_auto ? st.origin_z : std::nullopt});
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
