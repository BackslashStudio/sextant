#pragma once
#include "sextant/style.h"
#include <algorithm>
#include <optional>

namespace sextant {
    // Where an axis line sits along one other coordinate. In data space, because
    // an interior axis reserves no label room and must be known before sizing.
    struct AxisPlacement {
        // The value on the other coordinate.
        double pos = 0.0;

        // Strictly inside the limits: ticks and labels go inside the frame.
        bool interior = false;

        // At the high end: ticks and labels go on the far side (up/right).
        bool high = false;
    };

    // `lo`/`hi` are resolved limits (pin already folded in when auto). A pin
    // outside them clamps to the frame edge.
    inline AxisPlacement place_axis(AxisPosition p, const std::optional<double>& pin,
                                    double lo, double hi) {
        AxisPlacement a;
        if (pin) {
            a.pos = std::clamp(*pin, lo, hi);
        } else {
            switch (p) {
                // Assumes a linear scale.
                case AxisPosition::Mid: a.pos = lo + (hi - lo) * 0.5;
                    break;
                case AxisPosition::High: a.pos = hi;
                    break;
                // Auto == Low in 2D.
                default: a.pos = lo;
                    break;
            }
        }
        // A degenerate range leaves both flags false (Low).
        a.interior = a.pos > lo && a.pos < hi;
        a.high = hi > lo && !a.interior && a.pos >= hi;
        return a;
    }

    // One coordinate of a 3D axis line's position; two place an axis.
    struct AxisCoord3D {
        // Data-space position; meaningless when `camera` is true.
        AxisPlacement at;

        // Auto without a pin: plan_box3d() picks the silhouette edge.
        bool camera = false;
    };

    // Low/High are absolute (a fixed box face) and don't follow the camera. The
    // caller converts to box space with its Transform3D.
    inline AxisCoord3D place_axis3d(AxisPosition p, const std::optional<double>& pin,
                                    double lo, double hi) {
        AxisCoord3D c;
        // A pin wins over the enum and clamps per coordinate.
        if (p == AxisPosition::Auto && !pin) {
            c.camera = true;
            return c;
        }
        c.at = place_axis(p, pin, lo, hi);
        return c;
    }

    // Fold an origin pin into automatic (unpadded) bounds, so the view widens to
    // show it with the usual padding.
    inline void widen_for_origin(const std::optional<double>& pin, bool limits_auto,
                                 double& lo, double& hi) {
        if (!limits_auto || !pin) return;
        lo = std::min(lo, *pin);
        hi = std::max(hi, *pin);
    }
} // namespace sextant
