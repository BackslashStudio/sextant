#pragma once
#include "plot_objects.h"
#include "coord_transform.h"
#include "coord_transform3d.h"
#include "hint_index.h"
#include "render_frame.h"
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace sextant {

// Pixel hit radius for the nearest-point hover search.
constexpr float kHintHitRadiusPx = 12.0f;

struct HintResult {
    std::string text;                  // may contain embedded '\n'
    float       anchor_x = 0, anchor_y = 0;  // physical-pixel anchor point
};

// The axes cell under the cursor (physical pixels), or nullptr.
const AxesLayout* find_hint_cell(const std::vector<AxesLayout>& layout,
                                  float cursor_x, float cursor_y);

// The data <-> pixel map for find_hint(), over a 2D axes or a plane in a 3D
// scene. Implicit from CoordTransform.
class HintProjector {
public:
    HintProjector(const CoordTransform& tr) : tr_(&tr) {}
    HintProjector(const Projector3D& proj, PlaneOrientation orient, double offset)
        : proj_(&proj), orient_(orient), offset_(offset) {}

    struct Pt { float x = 0.0f, y = 0.0f; bool in_front = true; };

    Pt at(double u, double v) const {
        if (tr_) return { tr_->to_px(u), tr_->to_py(v), true };
        const Vec3 p = plane_point(orient_, u, v, offset_);
        const Vec3 b = proj_->transform().to_box(p.x, p.y, p.z);
        const Px3  q = proj_->project_box(b);
        return { q.x, q.y, proj_->in_front(b) };
    }

    // Data coordinates at a pixel; false if it hits no point on the surface.
    bool at_pixel(float px, float py, double& u, double& v) const {
        if (tr_) { u = tr_->to_data_x(px); v = tr_->to_data_y(py); return true; }
        float depth = 0.0f;
        return plane_ray_hit(*proj_, orient_, offset_, px, py, u, v, depth);
    }

    // A data-space box containing the preimage of the `cursor +- r` square,
    // used as a conservative candidate filter.
    bool data_box(float cx, float cy, float r,
                  double& x_lo, double& x_hi, double& y_lo, double& y_hi) const {
        if (tr_) {
            x_lo = tr_->to_data_x(cx - r);
            x_hi = tr_->to_data_x(cx + r);
            if (x_lo > x_hi) { const double t = x_lo; x_lo = x_hi; x_hi = t; }
            // Screen y grows downward, so the y pair is swapped.
            y_lo = tr_->to_data_y(cy + r);
            y_hi = tr_->to_data_y(cy - r);
            if (y_lo > y_hi) { const double t = y_lo; y_lo = y_hi; y_hi = t; }
            return true;
        }
        bool first = true;
        for (int i = 0; i < 4; ++i) {
            const float px = cx + ((i & 1) ? r : -r);
            const float py = cy + ((i & 2) ? r : -r);
            double u = 0.0, v = 0.0;
            // A missed corner makes the preimage unbounded: no box.
            if (!at_pixel(px, py, u, v)) return false;
            if (first) { x_lo = x_hi = u; y_lo = y_hi = v; first = false; }
            else {
                x_lo = std::min(x_lo, u); x_hi = std::max(x_hi, u);
                y_lo = std::min(y_lo, v); y_hi = std::max(y_hi, v);
            }
        }
        return true;
    }

private:
    const CoordTransform* tr_   = nullptr;
    const Projector3D*    proj_ = nullptr;
    PlaneOrientation      orient_ = PlaneOrientation::XY;
    double                offset_ = 0.0;
};

// Nearest point across line/scatter/scatter_z/bar within kHintHitRadiusPx,
// else the heatmap cell under the cursor. nullopt = nothing. `index`
// (optional, set_frame_key() already called) only speeds up the search.
std::optional<HintResult> find_hint(const RenderSnapshot& snap,
                                    const HintProjector& proj,
                                    float cursor_x, float cursor_y,
                                    HintIndexCache* index = nullptr);

// 3D: casts the cursor ray against planes and bars, nearest first. A bar
// answers by being hit; a plane via find_hint(). `index`, if given, must have
// set_frame_key() called for this snapshot and axes.
std::optional<HintResult> find_hint3d(const RenderSnapshot3D& snap,
                                      const Projector3D& proj,
                                      float cursor_x, float cursor_y,
                                      HintIndexCache* index = nullptr);

} // namespace sextant
