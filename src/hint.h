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

// Fixed pixel-distance hit threshold for point-based nearest search (Step
// 19 mouse hint) — not exposed as a configurable option in v1.
constexpr float kHintHitRadiusPx = 12.0f;

struct HintResult {
    std::string text;                  // may contain embedded '\n'
    float       anchor_x = 0, anchor_y = 0;  // physical-pixel anchor point
};

// Cursor (physical pixels) -> which axes cell it's over, by testing against
// each entry's tr.px/py/pw/ph rect. nullptr if over no cell (e.g. margins).
const AxesLayout* find_hint_cell(const std::vector<AxesLayout>& layout,
                                  float cursor_x, float cursor_y);

// Within one axes (snap/tr must correspond to the same axes), finds the single
// globally-nearest point across line/scatter/scatter_z/bar within
// kHintHitRadiusPx of the cursor, falling back to heatmap cell containment.
// nullopt = nothing under the cursor.
//
// `index` is an optional spatial index (hint_index.h) with set_frame_key()
// already called for this snapshot and axes. Passing nullptr simply scans; the
// result is identical either way.
// Both directions of the one projection find_hint() needs, over either a 2D
// axes or a plane in a 3D scene -- ContourProjector's shape, and here for the
// same reason: the search itself is identical, and only the map between data
// and pixels differs. Implicit from a CoordTransform, so every 2D call site
// reads as though this class were not here and runs the same arithmetic in
// the same order.
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

    // Data-space coordinates of the pixel itself. False when the pixel names
    // no point on this surface (a plane seen edge-on, or behind the eye).
    bool at_pixel(float px, float py, double& u, double& v) const {
        if (tr_) { u = tr_->to_data_x(px); v = tr_->to_data_y(py); return true; }
        float depth = 0.0f;
        return plane_ray_hit(*proj_, orient_, offset_, px, py, u, v, depth);
    }

    // A data-space box that certainly contains the preimage of the
    // `cursor +- r` pixel square -- the conservative candidate filter the
    // point search narrows down with. Exact for 2D. For a plane it is the
    // bounding box of the square's four corners, which is exact for the
    // quadrilateral they span (the map is a homography, so straight edges
    // stay straight) and so conservative for the disc inside it.
    bool data_box(float cx, float cy, float r,
                  double& x_lo, double& x_hi, double& y_lo, double& y_hi) const {
        if (tr_) {
            x_lo = tr_->to_data_x(cx - r);
            x_hi = tr_->to_data_x(cx + r);
            if (x_lo > x_hi) { const double t = x_lo; x_lo = x_hi; x_hi = t; }
            // Screen y grows downward and data y upward, so this pair always
            // needs the swap the x pair only needs on an inverted axis.
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
            // One corner that misses makes the preimage unbounded, and a
            // filter that is not conservative is worse than none: report no
            // box and let the caller skip the surface.
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

std::optional<HintResult> find_hint(const RenderSnapshot& snap,
                                    const HintProjector& proj,
                                    float cursor_x, float cursor_y,
                                    HintIndexCache* index = nullptr);

// The 3D counterpart: the cursor's ray against every drawn surface in the
// scene -- each plane, and each bar of each `bar3d` grid -- nearest hit first,
// then find_hint() above on the first *plane* that answers. A bar answers by
// being hit at all, since a bar is opaque geometry rather than a sheet with
// points scattered on it.
//
// Planes-only was step 6b's honest first version, on the grounds that a box of
// six faces is a different problem from one plane. It is a different problem
// and not a harder one: a bar is axis-aligned, so the inverse is three slab
// tests (box_ray_hit()) and just as exact. Step 6c closes it.
//
// `index`, when given, must already have had set_frame_key() called for this
// snapshot and axes; this stamps the plane onto it per plane.
std::optional<HintResult> find_hint3d(const RenderSnapshot3D& snap,
                                      const Projector3D& proj,
                                      float cursor_x, float cursor_y,
                                      HintIndexCache* index = nullptr);

} // namespace sextant
