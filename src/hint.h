#pragma once
#include "sextant/events.h"
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

// Pixel hit radius for the nearest-point hover search: the default of
// FigureOptions::pick_radius, which callers pass on as `radius`.
constexpr float kHintHitRadiusPx = 12.0f;

// How far outside a bar a press still counts as on it, so a thin or
// zero-height bar stays hittable.
constexpr float kBarHitMarginPx = 1.5f;

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

    // The scales between data and the drawing: the axes' own; identities on a
    // plane, which has none.
    AxisScale x_scale() const { return tr_ ? tr_->xs : AxisScale{}; }
    AxisScale y_scale() const { return tr_ ? tr_->ys : AxisScale{}; }

    // The 2D transform, null on a plane (seen through a projector).
    const CoordTransform* transform() const { return tr_; }

private:
    const CoordTransform* tr_   = nullptr;
    const Projector3D*    proj_ = nullptr;
    PlaneOrientation      orient_ = PlaneOrientation::XY;
    double                offset_ = 0.0;
};

// What a hover or a click found, before any text is made of it. `object` indexes
// the snapshot vector of its kind (creation order, the read-back index);
// `element` is the point, bar, cell, sample, marker or vertex; `plane` is -1 for
// an object of the axes itself, else the index into RenderSnapshot3D::planes
// (a 2D kind on that plane's sheet). `row`/`col`: heatmap cells, else -1.
// An area hit (pick_is_area()) also has the cursor's data position, `data_x`/
// `data_y` (on a plane, in the plane's own coordinates).
// A heatmap_tri hit: `element` the triangle, `value` the value interpolated at
// the cursor and `vertex` the triangle's nearest vertex (for hint_labels).
struct PickHit {
    PickKind    kind = PickKind::None;
    int         plane = -1;
    std::size_t object = 0, element = 0;
    int         row = -1, col = -1;
    float       anchor_x = 0, anchor_y = 0;   // physical pixels
    double      data_x = 0.0, data_y = 0.0;
    double      value = 0.0;
    std::size_t vertex = 0;
    bool        area = false; // a contourf's band (a contour's line is a point hit)
};

// The 2D kinds hit by the area they cover rather than at a point.
inline bool pick_is_area(PickKind k) {
    return k == PickKind::Bar || k == PickKind::Fill || k == PickKind::Heatmap
           || k == PickKind::Poly || k == PickKind::Circle || k == PickKind::Wedge
           || k == PickKind::HeatmapTri || k == PickKind::Pie;
}

// The same for a hit: a contour is an area when filled.
inline bool pick_is_area(const PickHit& h) {
    return h.kind == PickKind::Contour ? h.area : pick_is_area(h.kind);
}

// The hit find_hint() words. First the nearest point across line/scatter/
// scatter_z/quiver within `radius` pixels (of equal distances, the later
// call's), whatever the draw order; else the topmost bar (within
// kBarHitMarginPx), fill or heatmap cell the cursor is inside. `index` as for
// find_hint().
std::optional<PickHit> find_pick(const RenderSnapshot& snap, const HintProjector& proj,
                                 float cursor_x, float cursor_y,
                                 HintIndexCache* index = nullptr,
                                 float radius = kHintHitRadiusPx);

// A 2D cell's layers (Axes::twinx()) as drawn: its own axes, then its twins,
// each with its transform, edit key (axes_key()) and layer number.
struct CellLayers {
    std::vector<const RenderSnapshot*> snaps;
    std::vector<const CoordTransform*> trs;
    std::vector<int> keys, layers;
};
CellLayers cell_layers(const FigureAxesSnapshot& fa, const AxesLayout& cell);

// find_pick() over every layer of a 2D cell, each through its own transform,
// as within one axes: a point beats an area; the nearest point in pixels, the
// later object on a tie; of two areas the later object. `layer` receives the
// winner's index in `l`. `index`, if given, is keyed per layer on
// `data_generation`.
std::optional<PickHit> find_pick_layers(const CellLayers& l, float cursor_x, float cursor_y,
                                        std::size_t& layer, HintIndexCache* index = nullptr,
                                        unsigned long long data_generation = 0,
                                        float radius = kHintHitRadiusPx);

// 3D: the nearest thing the cursor ray meets, as find_hint3d() searches it.
// `radius`: the least a marker or path vertex is hit within, and the radius of
// the 2D search on a plane.
std::optional<PickHit> find_pick3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                                   float cursor_x, float cursor_y,
                                   HintIndexCache* index = nullptr,
                                   float radius = kHintHitRadiusPx);

// The hint text for a hit found in `snap` (2D kinds), or in a 3D snapshot (any
// kind, planes included). What find_hint()/find_hint3d() return.
// The ObjectId of the object a hit names, in the snapshot the hit was found in
// (0 for a kind the snapshot has none of).
ObjectId pick_object_id(const RenderSnapshot& snap, PickKind kind, std::size_t object);
ObjectId pick_object_id(const RenderSnapshot3D& snap, PickKind kind, std::size_t object);

std::string format_pick(const RenderSnapshot& snap, const PickHit& hit);
std::string format_pick3d(const RenderSnapshot3D& snap, const PickHit& hit);

// The text for find_pick()'s hit. nullopt = nothing. `index` (optional,
// set_frame_key() already called) only speeds up the search.
std::optional<HintResult> find_hint(const RenderSnapshot& snap,
                                    const HintProjector& proj,
                                    float cursor_x, float cursor_y,
                                    HintIndexCache* index = nullptr,
                                    float radius = kHintHitRadiusPx);

// 3D: casts the cursor ray against planes and bars, nearest first. A bar
// answers by being hit; a plane via find_hint(). `index`, if given, must have
// set_frame_key() called for this snapshot and axes.
std::optional<HintResult> find_hint3d(const RenderSnapshot3D& snap,
                                      const Projector3D& proj,
                                      float cursor_x, float cursor_y,
                                      HintIndexCache* index = nullptr,
                                      float radius = kHintHitRadiusPx);

} // namespace sextant
