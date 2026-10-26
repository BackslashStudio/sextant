#pragma once
// A polar cell's frame in pixels (PolarAxes): what layout places once and
// both drawers (NanoVG and SVG) draw. See polar_geometry.h for the data side.
#include "plot_rect.h"
#include "../coord_transform.h"
#include "../polar_geometry.h"
#include "../tick.h"
#include <string>
#include <vector>

namespace sextant {
    // A ring (r grid line) or a spoke (θ grid line), and its label centred on
    // (lx, ly).
    struct PolarGridLine {
        double value = 0.0;   // r, or θ in degrees
        std::string label;
        float lx = 0.0f, ly = 0.0f;
    };

    struct PolarPlan {
        PolarMap map;
        // The centre (r = rmin) and the frame's radius, in pixels.
        float cx = 0.0f, cy = 0.0f, radius = 0.0f;
        std::vector<PolarGridLine> rings, spokes;
        // AxesStyle::show_yticks / show_xticks and the axis being on.
        bool rlabels = true, tlabels = true;

        float px(double ux) const { return cx + static_cast<float>(ux) * radius; }
        float py(double uy) const { return cy - static_cast<float>(uy) * radius; }
        // A point (θ radians, r) in pixels.
        float px(double theta, double r) const { return px(map.unit_x(theta, r)); }
        float py(double theta, double r) const { return py(map.unit_y(theta, r)); }

        // The frame outline (closed polygon) in pixels.
        void outline(std::vector<float>& x, std::vector<float>& y) const {
            std::vector<double> ux, uy;
            polar_outline(map, ux, uy);
            x.resize(ux.size());
            y.resize(uy.size());
            for (std::size_t i = 0; i < ux.size(); ++i) {
                x[i] = px(ux[i]);
                y[i] = py(uy[i]);
            }
        }
        // One ring in pixels (open for a sector).
        void ring(double r, std::vector<float>& x, std::vector<float>& y) const {
            std::vector<double> ux, uy;
            polar_ring(map, r, ux, uy);
            x.resize(ux.size());
            y.resize(uy.size());
            for (std::size_t i = 0; i < ux.size(); ++i) {
                x[i] = px(ux[i]);
                y[i] = py(uy[i]);
            }
        }
    };

    // The gap between the frame and the θ labels, pixels.
    inline constexpr float kPolarLabelGap = 6.0f;

    // How far a w×h box centred on a point reaches along the screen direction
    // `a` (radians, counterclockwise from east).
    inline float polar_box_reach(float w, float h, double a) {
        return static_cast<float>(std::fabs(std::cos(a))) * w * 0.5f
               + static_cast<float>(std::fabs(std::sin(a))) * h * 0.5f;
    }

    // The frame's box (unit space) fitted, centred, into `f` at one scale.
    inline PlotRect polar_frame(const PlotRect& f, const PolarMap& m) {
        const PolarBox b = polar_box(m);
        const double bw = b.xmax - b.xmin, bh = b.ymax - b.ymin;
        if (!(bw > 0.0) || !(bh > 0.0)) return f;
        const double want = bh / bw;   // frame h / w
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

    // The frame's box in unit space onto the frame: polar_sheet()'s objects
    // (SVG) and PolarRenderer's view both read it.
    inline CoordTransform polar_transform(const PlotRect& frame, const PolarMap& m, float fw, float fh) {
        const PolarBox b = polar_box(m);
        return {b.xmin, b.xmax, b.ymin, b.ymax, frame.x, frame.y, frame.w, frame.h, fw, fh,
                AxisScale{}, AxisScale{}};
    }
} // namespace sextant
