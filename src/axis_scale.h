#pragma once
// One 2D axis' scale as a map between data and scale space, where the axis is
// linear. Everything that turns data into pixels goes through it (via
// CoordTransform), so a drawn object needs no scale logic of its own.
#include "sextant/style.h"
#include <cmath>
#include <limits>

namespace sextant {
    struct AxisScale {
        Scale kind = Scale::Linear;
        // Only the fields `kind` reads are meaningful (see ScaleOptions).
        double base = 10.0, linthresh = 1.0, linscale = 1.0;

        bool linear() const { return kind == Scale::Linear; }

        // Can v be placed on this axis? Log has no place for v <= 0.
        bool valid(double v) const { return kind != Scale::Log || v > 0.0; }

        // Data -> scale space, for drawing. Linear is the identity, so linear
        // output is bit-identical to a transform without scales. Log clips
        // v <= 0 to kLogFloor (matplotlib's nonpositive='clip'); NaN stays NaN.
        double fwd(double v) const {
            switch (kind) {
                case Scale::Linear: return v;
                case Scale::Log: return log_b(v > 0.0 || std::isnan(v) ? v : kLogFloor);
                case Scale::Symlog: return symlog(v);
            }
            return v;
        }

        // As fwd(), but NaN where the scale has no value: what bounds take, so
        // a nonpositive value on a log axis widens nothing (std::min/max skip
        // a NaN second argument).
        double fwd_strict(double v) const {
            if (kind == Scale::Log && !(v > 0.0)) return std::numeric_limits<double>::quiet_NaN();
            return fwd(v);
        }

        // Scale space -> data.
        double inv(double s) const {
            switch (kind) {
                case Scale::Linear: return s;
                case Scale::Log: return pow_b(s);
                case Scale::Symlog: {
                    const double adj = linscale_adj();
                    const double a = std::fabs(s);
                    const double r = a > linthresh * adj ? linthresh * pow_b(a / linthresh - adj)
                                                         : a / adj;
                    return std::signbit(s) ? -r : r;
                }
            }
            return s;
        }

        friend bool operator==(const AxisScale&, const AxisScale&) = default;

        // Where Log puts v <= 0: far below any view, finite.
        static constexpr double kLogFloor = 1e-300;

    private:
        // Exact at the common bases, so a decade tick lands on an integer.
        double log_b(double v) const {
            if (base == 10.0) return std::log10(v);
            if (base == 2.0) return std::log2(v);
            return std::log(v) / std::log(base);
        }

        double pow_b(double s) const {
            if (base == 2.0) return std::exp2(s);
            return std::pow(base, s);
        }

        // matplotlib's SymmetricalLogTransform: the linear part spans `linscale`
        // decades.
        double linscale_adj() const { return linscale / (1.0 - 1.0 / base); }

        double symlog(double v) const {
            const double adj = linscale_adj();
            const double a = std::fabs(v);
            if (!(a > linthresh)) return v * adj; // NaN too
            const double r = linthresh * (adj + log_b(a / linthresh));
            return v < 0.0 ? -r : r;
        }
    };

    // Set limits the scale can place (both ends > 0 on log); others read as
    // automatic.
    inline bool limits_fit(const AxisScale& s, double lo, double hi) {
        return s.valid(lo) && s.valid(hi);
    }

    inline AxisScale make_axis_scale(Scale s, const ScaleOptions& o) {
        return {s, o.base, o.linthresh, o.linscale};
    }
} // namespace sextant
