#include "hint.h"
#include "heatmap_tri_geometry.h"
#include "renderer/surface_tri.h"
#include "renderer/bar3d.h"
#include "renderer/surface.h"
#include "fill_geometry.h"
#include "shape_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sextant {

namespace {

std::string append_label(std::string base, const std::vector<std::string>& labels, std::size_t idx) {
    if (idx < labels.size() && !labels[idx].empty()) {
        base += '\n';
        base += labels[idx];
    }
    return base;
}

std::string fmt_point(double x, double y) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g", x, y);
    return buf;
}

// "y=2.5" becomes "y=2.5 box ±0.3 cap +0.6/-0.5": "±v" when both sides agree,
// "+hi/-lo" otherwise; parts that are absent or zero are dropped. Placed beside
// each coordinate. ASCII minus (U+2212 isn't in the hover font).
void fmt_offsets(char* buf, std::size_t size, int& n, const char* part,
                 bool has, ErrOffsets e) {
    if (!has || !e.any() || n < 0 || static_cast<std::size_t>(n) >= size) return;
    const std::size_t left = size - static_cast<std::size_t>(n);
    int k = e.lo == e.hi
        ? std::snprintf(buf + n, left, " %s \xC2\xB1%.4g", part, e.hi)
        : std::snprintf(buf + n, left, " %s +%.4g/-%.4g", part, e.hi, e.lo);
    if (k > 0) n += k;
}

std::string fmt_value(const char* name, double v,
                      bool has_box, ErrOffsets box,
                      bool has_cap, ErrOffsets cap) {
    char buf[160];
    int n = std::snprintf(buf, sizeof(buf), "%s=%.4g", name, v);
    fmt_offsets(buf, sizeof(buf), n, "box", has_box, box);
    fmt_offsets(buf, sizeof(buf), n, "cap", has_cap, cap);
    return buf;
}

std::string fmt_err_pair(const char* xname, double x, const char* yname, double y,
                         const ErrorBarData& err, std::size_t i) {
    return fmt_value(xname, x, err.has_x_box(), err.x_box(i), err.has_x_cap(), err.x_cap(i))
         + ", "
         + fmt_value(yname, y, err.has_y_box(), err.y_box(i), err.has_y_cap(), err.y_cap(i));
}

std::string fmt_point_err(double x, double y, const ErrorBarData& err, std::size_t i) {
    if (err.empty()) return fmt_point(x, y);
    return fmt_err_pair("x", x, "y", y, err, i);
}

// No fast path needed: without error data this prints plain "x=%.4g". A lying
// bar reads "y=.., width=.." (its value errors are the x ones); a set base adds
// "bottom=.." ("left=..").
std::string fmt_bar(const BarPlot& bp, std::size_t i) {
    const ErrorBarData& err = bp.err;
    std::string s = bp.horizontal()
        ? fmt_value("y", bp.centers[i], err.has_y_box(), err.y_box(i), err.has_y_cap(), err.y_cap(i))
          + ", "
          + fmt_value("width", bp.heights[i], err.has_x_box(), err.x_box(i), err.has_x_cap(), err.x_cap(i))
        : fmt_err_pair("x", bp.centers[i], "height", bp.heights[i], err, i);
    if (!bp.bottom.empty()) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), ", %s=%.4g", bp.horizontal() ? "left" : "bottom", bp.base_at(i));
        s += buf;
    }
    return s;
}

std::string fmt_z(double x, double y, double z) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, z=%.4g", x, y, z);
    return buf;
}

// z is the colormapped value and carries no error bar.
std::string fmt_z_err(double x, double y, double z,
                      const ErrorBarData& err, std::size_t i) {
    if (err.empty()) return fmt_z(x, y, z);
    char buf[48];
    std::snprintf(buf, sizeof(buf), ", z=%.4g", z);
    return fmt_err_pair("x", x, "y", y, err, i) + buf;
}

// A NaN cell (drawn transparent) has no value.
std::string fmt_heatmap(int row, int col, float value) {
    char buf[64];
    if (std::isnan(value)) std::snprintf(buf, sizeof(buf), "row=%d, col=%d, no value", row, col);
    else std::snprintf(buf, sizeof(buf), "row=%d, col=%d, value=%.4g", row, col, value);
    return buf;
}

// An imshow_rgb() pixel: its channels as stored, in [0, 1] unless the caller
// passed values outside it (they are clamped only when drawn).
std::string fmt_image(int row, int col, const float* c, int channels) {
    char buf[128];
    if (channels == 4)
        std::snprintf(buf, sizeof(buf), "row=%d, col=%d, rgba=(%.4g, %.4g, %.4g, %.4g)", row, col,
                      c[0], c[1], c[2], c[3]);
    else
        std::snprintf(buf, sizeof(buf), "row=%d, col=%d, rgb=(%.4g, %.4g, %.4g)", row, col,
                      c[0], c[1], c[2]);
    return buf;
}

// "x=400, y=1.5, height=3.42", with u/v named by the axes they map to (as in
// the Data panel headers). No grid indices.
std::string fmt_bar3d(const Bar3DPlot& b, std::size_t k) {
    static const char* kAxis[3] = { "x", "y", "z" };
    const Axis3Map m = axis_map(b.orient);
    const std::size_t nv = b.v.size();
    const std::size_t i = nv ? k / nv : 0;
    const std::size_t j = nv ? k % nv : 0;

    char buf[160];
    int n = std::snprintf(buf, sizeof(buf), "%s=%.4g, %s=%.4g, height=%.4g",
                          kAxis[m.u], i < b.u.size() ? b.u[i] : 0.0,
                          kAxis[m.v], j < b.v.size() ? b.v[j] : 0.0,
                          b.height_at(k));
    // Report the base only when non-zero.
    const double base = b.bottom_at(k);
    if (base != 0.0)
        std::snprintf(buf + n, sizeof(buf) - static_cast<std::size_t>(n),
                      ", base=%.4g", base);
    return buf;
}

// A surface sample; `z=` rather than `height=` (a sheet has no base).
std::string fmt_surface(const SurfacePlot& s, std::size_t sample) {
    static const char* kAxis[3] = { "x", "y", "z" };
    const Axis3Map m = axis_map(s.orient);
    const std::size_t nv = s.v.size();
    const std::size_t i = nv ? sample / nv : 0;
    const std::size_t j = nv ? sample % nv : 0;

    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s=%.4g, %s=%.4g, %s=%.4g",
                  kAxis[m.u], i < s.u.size() ? s.u[i] : 0.0,
                  kAxis[m.v], j < s.v.size() ? s.v[j] : 0.0,
                  kAxis[m.h], s.height_at(sample));
    return buf;
}

// A mesh vertex (the nearest of the face's three), plus the `colors` value
// when the mesh has one.
std::string fmt_surface_tri(const SurfaceTriPlot& s, std::size_t i) {
    char buf[200];
    const Vec3 p = s.vertex(i);
    if (s.colormapped())
        std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, z=%.4g\nc=%.4g",
                      p.x, p.y, p.z, s.color_at(i));
    else
        std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, z=%.4g", p.x, p.y, p.z);
    return buf;
}

// A cloud marker: "x=1 box ±0.2, y=2, z=3 cap +0.5/-0.1", plus the `colors`
// value when the series has one.
std::string fmt_xyz_err(double x, double y, double z, const ErrorBar3DData& err,
                        std::size_t i, bool colormapped, double c) {
    const double v[3] = { x, y, z };
    const char* name[3] = { "x", "y", "z" };
    std::string s;
    for (int a = 0; a < 3; ++a) {
        if (a) s += ", ";
        s += fmt_value(name[a], v[a], err.has_box(a), err.box(a, i),
                       err.has_cap(a), err.cap(a, i));
    }
    if (colormapped) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "\nc=%.4g", c);
        s += buf;
    }
    return s;
}

std::string fmt_scatter3d(const Scatter3DPlot& s, std::size_t i) {
    char buf[200];
    const double x = i < s.x.size() ? s.x[i] : 0.0;
    const double y = i < s.y.size() ? s.y[i] : 0.0;
    const double z = i < s.z.size() ? s.z[i] : 0.0;
    if (!s.err.empty())
        return fmt_xyz_err(x, y, z, s.err, i, s.colormapped(), s.color_at(i));
    if (s.colormapped())
        std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, z=%.4g\nc=%.4g",
                      x, y, z, s.color_at(i));
    else
        std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, z=%.4g", x, y, z);
    return buf;
}

// A path vertex, in the cloud's format. Paths are hovered at vertices only.
std::string fmt_line3d(const Line3DPlot& l, std::size_t i) {
    char buf[200];
    const double x = i < l.x.size() ? l.x[i] : 0.0;
    const double y = i < l.y.size() ? l.y[i] : 0.0;
    const double z = i < l.z.size() ? l.z[i] : 0.0;
    if (!l.err.empty())
        return fmt_xyz_err(x, y, z, l.err, i, l.colormapped(), l.color_at(i));
    if (l.colormapped())
        std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, z=%.4g\nc=%.4g",
                      x, y, z, l.color_at(i));
    else
        std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, z=%.4g", x, y, z);
    return buf;
}

} // namespace

const AxesLayout* find_hint_cell(const std::vector<AxesLayout>& layout,
                                  float cursor_x, float cursor_y) {
    for (const auto& al : layout) {
        // A 3D cell's `tr` is default; its rect comes from the projector.
        const float x = al.proj3d ? al.proj3d->frame().x : al.tr.px;
        const float y = al.proj3d ? al.proj3d->frame().y : al.tr.py;
        const float w = al.proj3d ? al.proj3d->frame().w : al.tr.pw;
        const float h = al.proj3d ? al.proj3d->frame().h : al.tr.ph;
        if (cursor_x >= x && cursor_x < x + w &&
            cursor_y >= y && cursor_y < y + h)
            return &al;
    }
    return nullptr;
}

namespace {

// The geometry of shape (kind, o) in `tr`'s pixels.
ShapeGeom shape_geom_of(const RenderSnapshot& snap, PlotKind kind, std::size_t o, const CoordTransform& tr) {
    switch (kind) {
        case PlotKind::Poly: return shape_geometry(snap.polys[o], tr);
        case PlotKind::Circle: return shape_geometry(snap.circles[o], tr);
        default: return shape_geometry(snap.wedges[o], tr);
    }
}

// Whether the cursor (pixel, and data (cx, cy)) is inside shape (kind, o). On a
// plane only a shape in plane units has a place: it is laid out on a stand-in
// transform fitted to its own box, where the cursor's data position lands.
bool shape_covers(const RenderSnapshot& snap, const HintProjector& proj, PlotKind kind, std::size_t o,
                  float px, float py, double cx, double cy) {
    if (const CoordTransform* tr = proj.transform())
        return shape_contains(shape_geom_of(snap, kind, o, *tr), px, py);
    double lo_x = 1e300, hi_x = -1e300, lo_y = 1e300, hi_y = -1e300;
    const auto grow = [&](double x, double y) {
        lo_x = std::min(lo_x, x); hi_x = std::max(hi_x, x);
        lo_y = std::min(lo_y, y); hi_y = std::max(hi_y, y);
    };
    switch (kind) {
        case PlotKind::Poly: shape_limit_points(snap.polys[o], grow); break;
        case PlotKind::Circle: shape_limit_points(snap.circles[o], grow); break;
        default: shape_limit_points(snap.wedges[o], grow); break;
    }
    if (!(lo_x < hi_x) || !(lo_y < hi_y)) return false;
    CoordTransform st{};
    st.xmin = lo_x; st.xmax = hi_x; st.ymin = lo_y; st.ymax = hi_y;
    st.px = 0.0f; st.py = 0.0f; st.pw = 1000.0f; st.ph = 1000.0f;
    return shape_contains(shape_geom_of(snap, kind, o, st), st.to_px(cx), st.to_py(cy));
}

// The topmost bar, fill or heatmap cell under the cursor (draw order, last
// first), at the cursor; nullopt if none, or the cursor misses the surface.
std::optional<PickHit> find_area(const RenderSnapshot& snap, const HintProjector& tr,
                                 const std::vector<ObjectRef>& order,
                                 float cursor_x, float cursor_y) {
    double cx = 0.0, cy = 0.0;
    if (!tr.at_pixel(cursor_x, cursor_y, cx, cy)) return std::nullopt;
    // A bar's margin, as the data box around the cursor; with a corner off the
    // plane, none.
    double bx_lo = cx, bx_hi = cx, by_lo = cy, by_hi = cy;
    if (!tr.data_box(cursor_x, cursor_y, kBarHitMarginPx, bx_lo, bx_hi, by_lo, by_hi)) {
        bx_lo = bx_hi = cx;
        by_lo = by_hi = cy;
    }
    const AxisScale xs = tr.x_scale(), ys = tr.y_scale();

    PickHit hit;
    hit.anchor_x = cursor_x;
    hit.anchor_y = cursor_y;
    hit.data_x = cx;
    hit.data_y = cy;
    for (std::size_t rank = order.size(); rank-- > 0;) {
        const std::size_t o = order[rank].index;
        switch (order[rank].kind) {
            case PlotKind::Bar: {
                // In data space: a scale keeps the rectangle's corners in order,
                // and a base at 0 on a log axis reaches below any view, as drawn.
                const BarPlot& bp = snap.bars[o];
                const bool lying = bp.horizontal();
                const double a_lo = lying ? by_lo : bx_lo, a_hi = lying ? by_hi : bx_hi;
                const double v_lo = lying ? bx_lo : by_lo, v_hi = lying ? bx_hi : by_hi;
                const double half = std::abs(bp.bar_width) * 0.5;
                const std::size_t n = std::min(bp.centers.size(), bp.heights.size());
                for (std::size_t i = n; i-- > 0;) {
                    const double c = bp.centers[i], b = bp.base_at(i), t = bp.tip_at(i);
                    if (!std::isfinite(c) || !std::isfinite(b) || !std::isfinite(t)) continue;
                    if (c + half < a_lo || c - half > a_hi) continue;
                    if (std::max(b, t) < v_lo || std::min(b, t) > v_hi) continue;
                    hit.kind = PickKind::Bar;
                    hit.object = o;
                    hit.element = i;
                    return hit;
                }
                break;
            }
            case PlotKind::Fill: {
                // In scale space, where the outputs draw it (fill_geometry.h).
                const FillPlot& fp = snap.fills[o];
                const bool vert = fp.vertical();
                const AxisScale& rs = vert ? ys : xs;
                const AxisScale& vs = vert ? xs : ys;
                const long seg = fill_segment_at(fp, rs, vs, rs.fwd(vert ? cy : cx),
                                                 vs.fwd(vert ? cx : cy));
                if (seg < 0) break;
                hit.kind = PickKind::Fill;
                hit.object = o;
                hit.element = static_cast<std::size_t>(seg);
                return hit;
            }
            case PlotKind::Poly: case PlotKind::Circle: case PlotKind::Wedge: {
                if (!shape_covers(snap, tr, order[rank].kind, o, cursor_x, cursor_y, cx, cy)) break;
                hit.kind = order[rank].kind == PlotKind::Poly     ? PickKind::Poly
                           : order[rank].kind == PlotKind::Circle ? PickKind::Circle
                                                                  : PickKind::Wedge;
                hit.object = o;
                hit.element = 0;
                return hit;
            }
            case PlotKind::HeatmapTri: {
                // In scale space (heatmap_tri_at()), so the drawn weights.
                const auto t = heatmap_tri_at(snap.heatmap_tris[o], xs, ys, cx, cy);
                if (!t) break;
                hit.kind = PickKind::HeatmapTri;
                hit.object = o;
                hit.element = t->triangle;
                hit.value = t->value;
                hit.vertex = t->nearest;
                return hit;
            }
            case PlotKind::Heatmap: {
                // The cell, in cell-index space via col_at()/row_at(). Storage
                // is row 0 first regardless of origin, so undo the
                // origin=="lower" flip to find the displayed row.
                const auto& hp = snap.heatmaps[o];
                if (hp.rows <= 0 || hp.cols <= 0) break;
                const double fc = hp.col_at(cx), fr = hp.row_at(cy);
                if (fc < 0.0 || fc >= hp.cols || fr < 0.0 || fr >= hp.rows) break;
                const int col = std::clamp(static_cast<int>(fc), 0, hp.cols - 1);
                const int row_from_bottom = std::clamp(static_cast<int>(fr), 0, hp.rows - 1);
                const int row = (hp.opts.origin == "lower") ? row_from_bottom
                                                            : (hp.rows - 1 - row_from_bottom);
                hit.kind = PickKind::Heatmap;
                hit.object = o;
                hit.row = row;
                hit.col = col;
                hit.element = static_cast<std::size_t>(row) * static_cast<std::size_t>(hp.cols)
                              + static_cast<std::size_t>(col);
                return hit;
            }
            default: break;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<PickHit> find_pick(const RenderSnapshot& snap, const HintProjector& tr,
                                 float cursor_x, float cursor_y,
                                 HintIndexCache* index, float radius) {
    // Runs every hovered frame. Points first, without any string work (only
    // the winner is worded, by the caller); candidates come from the data-space
    // bucket grid (hint_index.h). Objects are visited in draw order, so of
    // equal distances the one drawn on top wins (a higher zorder, else the later
    // call). A point found beats any area,
    // whatever the draw order; only without one do the areas answer.
    const std::vector<ObjectRef> order = draw_order(snap);

    // A surface with no candidate box (a plane seen edge-on) has no point to find.
    double x_lo = 0.0, x_hi = 0.0, y_lo = 0.0, y_hi = 0.0;
    if (!tr.data_box(cursor_x, cursor_y, radius, x_lo, x_hi, y_lo, y_hi))
        return find_area(snap, tr, order, cursor_x, cursor_y);

    float best_d2 = radius * radius;
    int         best_kind = -1;          // 0 line, 1 scatter, 2 scatter_z, 3 quiver
    std::size_t best_obj  = 0, best_i = 0;
    float       best_px   = 0.0f, best_py = 0.0f;

    // The exact pixel-space circle test; candidates were only conservative.
    auto consider = [&](float px, float py, int kind, std::size_t obj, std::size_t i) {
        const float dx = px - cursor_x, dy = py - cursor_y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= best_d2) {
            best_d2 = d2;
            best_kind = kind; best_obj = obj; best_i = i;
            best_px = px; best_py = py;
        }
    };

    // Without an index, use a shared const pass-through grid.
    static const PointGrid kScan{};

    // `hidden`: a scatter whose point i its own size hides (null = none).
    auto sweep = [&](PlotKind kind, std::size_t o, int kind_id,
                     const CowVec<double>& xs, const CowVec<double>& ys,
                     const auto* hidden) {
        const PointGrid& g = index ? index->grid(kind, o, xs, ys) : kScan;
        g.for_each_in(xs, ys, x_lo, x_hi, y_lo, y_hi,
                      [&](std::size_t i) {
                          if (hidden && marker_hidden(*hidden, i)) return;
                          // Behind a perspective eye: not a candidate.
                          const HintProjector::Pt p = tr.at(xs[i], ys[i]);
                          if (p.in_front) consider(p.x, p.y, kind_id, o, i);
                      });
    };
    const ScatterPlot* const no_hiding = nullptr;

    for (const ObjectRef& r : order) {
        const std::size_t o = r.index;
        switch (r.kind) {
            case PlotKind::Line:
                sweep(PlotKind::Line, o, 0, snap.lines[o].x, snap.lines[o].y, no_hiding);
                break;
            case PlotKind::Scatter:
                sweep(PlotKind::Scatter, o, 1, snap.scatters[o].x, snap.scatters[o].y,
                      &snap.scatters[o]);
                break;
            case PlotKind::ScatterZ:
                sweep(PlotKind::ScatterZ, o, 2, snap.scatter_z[o].x, snap.scatter_z[o].y,
                      &snap.scatter_z[o]);
                break;
            case PlotKind::Quiver:
                // At the arrow's (x, y), whatever its pivot.
                sweep(PlotKind::Quiver, o, 3, snap.quivers[o].x, snap.quivers[o].y, no_hiding);
                break;
            default: break;
        }
    }

    if (best_kind < 0) return find_area(snap, tr, order, cursor_x, cursor_y);

    static const PickKind kKinds[4] = { PickKind::Line, PickKind::Scatter,
                                        PickKind::ScatterZ, PickKind::Quiver };
    PickHit hit;
    hit.kind = kKinds[best_kind];
    hit.object = best_obj;
    hit.element = best_i;
    hit.anchor_x = best_px;
    hit.anchor_y = best_py;
    return hit;
}

CellLayers cell_layers(const FigureAxesSnapshot& fa, const AxesLayout& cell) {
    CellLayers l;
    const RenderSnapshot* s = fa.snap2d();
    if (!s) return l;
    l.snaps.push_back(s);
    l.trs.push_back(&cell.tr);
    l.keys.push_back(fa.slot.index);
    l.layers.push_back(0);
    for (const AxesLayout::Twin& t : cell.twins)
        if (const RenderSnapshot* ts = fa.layer2d(t.layer)) {
            l.snaps.push_back(ts);
            l.trs.push_back(&t.tr);
            l.keys.push_back(axes_key(fa.slot.index, t.layer));
            l.layers.push_back(t.layer);
        }
    return l;
}

ObjectId pick_object_id(const RenderSnapshot& s, PickKind kind, std::size_t i) {
    switch (kind) {
        case PickKind::Line:     return s.lines[i].id;
        case PickKind::Scatter:  return s.scatters[i].id;
        case PickKind::ScatterZ: return s.scatter_z[i].id;
        case PickKind::Bar:      return s.bars[i].id;
        case PickKind::Heatmap:  return s.heatmaps[i].id;
        case PickKind::Quiver:   return s.quivers[i].id;
        case PickKind::Fill:     return s.fills[i].id;
        case PickKind::Poly:     return s.polys[i].id;
        case PickKind::Circle:   return s.circles[i].id;
        case PickKind::Wedge:    return s.wedges[i].id;
        case PickKind::HeatmapTri: return s.heatmap_tris[i].id;
        default: return 0;
    }
}

ObjectId pick_object_id(const RenderSnapshot3D& s, PickKind kind, std::size_t i) {
    switch (kind) {
        case PickKind::Bar3D:      return s.bars3d[i].id;
        case PickKind::Surface:    return s.surfaces[i].id;
        case PickKind::SurfaceTri: return s.surface_tri[i].id;
        case PickKind::Scatter3D:  return s.scatter3d[i].id;
        case PickKind::Line3D:     return s.lines3d[i].id;
        default: return 0;
    }
}

std::optional<PickHit> find_pick_layers(const CellLayers& l, float cursor_x, float cursor_y,
                                        std::size_t& layer, HintIndexCache* index,
                                        unsigned long long data_generation, float radius) {
    std::optional<PickHit> best;
    ObjectId best_id = 0;
    float best_d2 = 0.0f;
    for (std::size_t k = 0; k < l.snaps.size(); ++k) {
        if (index) index->set_frame_key(data_generation, l.keys[k]);
        const auto hit = find_pick(*l.snaps[k], HintProjector(*l.trs[k]), cursor_x, cursor_y, index,
                                   radius);
        if (!hit) continue;
        const ObjectId id = pick_object_id(*l.snaps[k], hit->kind, hit->object);
        const float dx = hit->anchor_x - cursor_x, dy = hit->anchor_y - cursor_y;
        const float d2 = dx * dx + dy * dy;
        const bool area = pick_is_area(hit->kind);
        bool wins = true;
        if (best) {
            const bool best_area = pick_is_area(best->kind);
            if (area != best_area) wins = !area;   // a point beats an area
            else if (area) wins = id >= best_id;
            else wins = d2 < best_d2 || (d2 == best_d2 && id >= best_id);
        }
        if (wins) {
            best = hit;
            best_id = id;
            best_d2 = d2;
            layer = k;
        }
    }
    return best;
}

std::string format_pick(const RenderSnapshot& snap, const PickHit& hit) {
    const std::size_t i = hit.element;
    switch (hit.kind) {
        case PickKind::Line: {
            const auto& lp = snap.lines[hit.object];
            return append_label(fmt_point_err(lp.x[i], lp.y[i], lp.err, i), lp.opts.hint_labels, i);
        }
        case PickKind::Scatter: {
            const auto& sp = snap.scatters[hit.object];
            return append_label(fmt_point_err(sp.x[i], sp.y[i], sp.err, i), sp.opts.hint_labels, i);
        }
        case PickKind::ScatterZ: {
            const auto& sp = snap.scatter_z[hit.object];
            return append_label(fmt_z_err(sp.x[i], sp.y[i], sp.z[i], sp.err, i),
                                sp.opts.hint_labels, i);
        }
        case PickKind::Bar: {
            const auto& bp = snap.bars[hit.object];
            return append_label(fmt_bar(bp, i), bp.opts.hint_labels, i);
        }
        case PickKind::Heatmap: {
            const auto& hp = snap.heatmaps[hit.object];
            const std::size_t ch = static_cast<std::size_t>(hp.channels);
            if (!hp.colormapped()) {
                if ((i + 1) * ch > hp.data.size()) return {};
                return append_label(fmt_image(hit.row, hit.col, hp.data.data() + i * ch, hp.channels),
                                    hp.opts.hint_labels, i);
            }
            const float value = i < hp.data.size() ? hp.data[i] : 0.0f;
            return append_label(fmt_heatmap(hit.row, hit.col, value), hp.opts.hint_labels, i);
        }
        case PickKind::Quiver: {
            const auto& qp = snap.quivers[hit.object];
            char buf[160];
            if (qp.colormapped() && i < qp.c.size())
                std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, u=%.4g, v=%.4g, c=%.4g", qp.x[i], qp.y[i],
                              qp.u[i], qp.v[i], qp.c[i]);
            else
                std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, u=%.4g, v=%.4g", qp.x[i], qp.y[i],
                              qp.u[i], qp.v[i]);
            return append_label(buf, qp.opts.hint_labels, i);
        }
        case PickKind::Fill: {
            // Where the cursor is, then the fill's name (a stack's layer).
            std::string s = fmt_point(hit.data_x, hit.data_y);
            const std::string& name = snap.fills[hit.object].opts.name;
            if (!name.empty()) s += '\n' + name;
            return s;
        }
        case PickKind::HeatmapTri: {
            // Where the cursor is, the value there, then the nearest vertex's label.
            const auto& tp = snap.heatmap_tris[hit.object];
            char buf[96];
            std::snprintf(buf, sizeof(buf), ", value=%.4g", hit.value);
            return append_label(fmt_point(hit.data_x, hit.data_y) + buf, tp.opts.hint_labels, hit.vertex);
        }
        case PickKind::Poly: case PickKind::Circle: case PickKind::Wedge: {
            // Where the cursor is, then the shape's name.
            std::string s = fmt_point(hit.data_x, hit.data_y);
            const std::string& name = hit.kind == PickKind::Poly     ? snap.polys[hit.object].opts.name
                                      : hit.kind == PickKind::Circle ? snap.circles[hit.object].opts.name
                                                                     : snap.wedges[hit.object].opts.name;
            if (!name.empty()) s += '\n' + name;
            return s;
        }
        default:
            return {};
    }
}

std::optional<HintResult> find_hint(const RenderSnapshot& snap, const HintProjector& tr,
                                    float cursor_x, float cursor_y,
                                    HintIndexCache* index, float radius) {
    const auto hit = find_pick(snap, tr, cursor_x, cursor_y, index, radius);
    if (!hit) return std::nullopt;
    return HintResult{ format_pick(snap, *hit), hit->anchor_x, hit->anchor_y };
}

std::optional<PickHit> find_pick3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                                   float cursor_x, float cursor_y,
                                   HintIndexCache* index, float radius) {
    // Nearest surface first, by the depth of the cursor ray's own hit; misses
    // drop out here. `what` says which vector `object` indexes; `element` is
    // the bar index or nearest surface sample (unused for planes).
    enum class What { Plane, Bar, Surface, Mesh, Marker, Vertex };
    struct Candidate { What what; std::size_t object, element; float depth; };
    std::vector<Candidate> hits;

    for (std::size_t i = 0; i < snap.planes.size(); ++i) {
        const PlaneSnapshot& pl = snap.planes[i];
        // Hidden planes aren't hinted.
        if (!plane_drawn(pl)) continue;
        double u = 0.0, v = 0.0;
        float  depth = 0.0f;
        if (!plane_ray_hit(proj, pl.orient, pl.offset, cursor_x, cursor_y, u, v, depth))
            continue;
        hits.push_back({ What::Plane, i, 0, depth });
    }

    // Every bar, tested individually (cheap slab tests; no index).
    for (std::size_t o = 0; o < snap.bars3d.size(); ++o) {
        const Bar3DPlot& b = snap.bars3d[o];
        if (!object_shown(b)) continue;
        for (std::size_t k = 0; k < b.count(); ++k) {
            float depth = 0.0f;
            if (bar3d_ray_hit(b, k, proj, cursor_x, cursor_y, depth))
                hits.push_back({ What::Bar, o, k, depth });
        }
    }

    // Every surface cell (two triangle tests each).
    for (std::size_t o = 0; o < snap.surfaces.size(); ++o) {
        const SurfacePlot& s = snap.surfaces[o];
        if (!object_shown(s)) continue;
        if (s.heights.size() < s.count()) continue;
        for (std::size_t k = 0; k < s.cell_count(); ++k) {
            float depth = 0.0f;
            std::size_t sample = 0;
            if (surface_ray_hit(s, k, proj, cursor_x, cursor_y, depth, sample))
                hits.push_back({ What::Surface, o, sample, depth });
        }
    }

    // Every mesh face (one triangle test each; no acceleration structure).
    for (std::size_t o = 0; o < snap.surface_tri.size(); ++o) {
        const SurfaceTriPlot& s = snap.surface_tri[o];
        if (!object_shown(s)) continue;
        for (std::size_t f = 0; f < s.face_count(); ++f) {
            float depth = 0.0f;
            std::size_t vertex = 0;
            if (surface_tri_ray_hit(s, f, proj, cursor_x, cursor_y, depth, vertex))
                hits.push_back({ What::Mesh, o, vertex, depth });
        }
    }

    // Every cloud marker, tested in screen space (markers have no surface) at
    // the point's depth, so occlusion matches the picture. Radius: the
    // marker's own half size (per point with `sizes`), at least `radius`;
    // a point hidden by its size is not hit.
    for (std::size_t o = 0; o < snap.scatter3d.size(); ++o) {
        const Scatter3DPlot& s = snap.scatter3d[o];
        if (!object_shown(s)) continue;
        if (s.opts.marker == MarkerStyle::None) continue;
        const Transform3D& tf = proj.transform();
        for (std::size_t i = 0; i < s.count(); ++i) {
            if (marker_hidden(s, i)) continue;
            const float r = std::max(marker_size_at(s, i) * 0.5f, radius);
            const Vec3 b = tf.to_box(s.x[i], s.y[i], s.z[i]);
            if (!proj.in_front(b)) continue;
            const Px3 q = proj.project_box(b);
            const float dx = q.x - cursor_x, dy = q.y - cursor_y;
            if (dx * dx + dy * dy > r * r) continue;
            hits.push_back({ What::Marker, o, i, q.depth });
        }
    }

    // Every path vertex, in screen space. Radius: the drawn half width at the
    // box centre, at least `radius`.
    for (std::size_t o = 0; o < snap.lines3d.size(); ++o) {
        const Line3DPlot& l = snap.lines3d[o];
        if (!object_shown(l)) continue;
        if (l.opts.linewidth <= 0.0f) continue;
        const float r = std::max(l.opts.linewidth * 0.5f, radius);
        const Transform3D& tf = proj.transform();
        for (std::size_t i = 0; i < l.count(); ++i) {
            const Vec3 b = tf.to_box(l.x[i], l.y[i], l.z[i]);
            if (!proj.in_front(b)) continue;
            const Px3 q = proj.project_box(b);
            const float dx = q.x - cursor_x, dy = q.y - cursor_y;
            if (dx * dx + dy * dy > r * r) continue;
            hits.push_back({ What::Vertex, o, i, q.depth });
        }
    }

    std::stable_sort(hits.begin(), hits.end(),
                     [](const Candidate& a, const Candidate& b) { return a.depth < b.depth; });

    // First answer wins (nearest = drawn on top). Bars and surfaces answer by
    // being hit; a plane may decline if nothing is near the cursor.
    for (const Candidate& c : hits) {
        if (c.what != What::Plane) {
            PickHit hit;
            switch (c.what) {
                case What::Bar:     hit.kind = PickKind::Bar3D; break;
                case What::Surface: hit.kind = PickKind::Surface; break;
                case What::Mesh:    hit.kind = PickKind::SurfaceTri; break;
                case What::Marker:  hit.kind = PickKind::Scatter3D; break;
                default:            hit.kind = PickKind::Line3D; break;
            }
            hit.object = c.object;
            hit.element = c.element;
            hit.anchor_x = cursor_x;
            hit.anchor_y = cursor_y;
            // A bar's or a surface sample's grid position, as the hint words it.
            if (c.what == What::Bar) {
                const std::size_t nv = snap.bars3d[c.object].v.size();
                hit.row = nv ? static_cast<int>(c.element / nv) : -1;
                hit.col = nv ? static_cast<int>(c.element % nv) : -1;
            } else if (c.what == What::Surface) {
                const std::size_t nv = snap.surfaces[c.object].v.size();
                hit.row = nv ? static_cast<int>(c.element / nv) : -1;
                hit.col = nv ? static_cast<int>(c.element % nv) : -1;
            }
            return hit;
        }
        const PlaneSnapshot& pl = snap.planes[c.object];
        if (index) index->set_plane(static_cast<int>(c.object));
        if (auto r = find_pick(pl.sheet, HintProjector(proj, pl.orient, pl.offset),
                               cursor_x, cursor_y, index, radius)) {
            r->plane = static_cast<int>(c.object);
            return r;
        }
    }
    return std::nullopt;
}

std::string format_pick3d(const RenderSnapshot3D& snap, const PickHit& hit) {
    if (hit.plane >= 0) return format_pick(snap.planes[hit.plane].sheet, hit);
    const std::size_t i = hit.element;
    switch (hit.kind) {
        case PickKind::Bar3D: {
            const Bar3DPlot& b = snap.bars3d[hit.object];
            return append_label(fmt_bar3d(b, i), b.opts.hint_labels, i);
        }
        case PickKind::Surface: {
            const SurfacePlot& s = snap.surfaces[hit.object];
            return append_label(fmt_surface(s, i), s.opts.hint_labels, i);
        }
        case PickKind::SurfaceTri: {
            const SurfaceTriPlot& s = snap.surface_tri[hit.object];
            return append_label(fmt_surface_tri(s, i), s.opts.hint_labels, i);
        }
        case PickKind::Scatter3D: {
            const Scatter3DPlot& s = snap.scatter3d[hit.object];
            return append_label(fmt_scatter3d(s, i), s.opts.hint_labels, i);
        }
        case PickKind::Line3D: {
            const Line3DPlot& l = snap.lines3d[hit.object];
            return append_label(fmt_line3d(l, i), l.opts.hint_labels, i);
        }
        default:
            return {};
    }
}

std::optional<HintResult> find_hint3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                                      float cursor_x, float cursor_y,
                                      HintIndexCache* index, float radius) {
    const auto hit = find_pick3d(snap, proj, cursor_x, cursor_y, index, radius);
    if (!hit) return std::nullopt;
    return HintResult{ format_pick3d(snap, *hit), hit->anchor_x, hit->anchor_y };
}

} // namespace sextant
