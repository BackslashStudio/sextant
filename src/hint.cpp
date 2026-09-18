#include "hint.h"
#include "renderer/surface_tri.h"
#include "renderer/bar3d.h"
#include "renderer/surface.h"
#include <algorithm>
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

// No fast path needed: without error data this prints plain "x=%.4g".
std::string fmt_bar_err(double x, double h, const ErrorBarData& err, std::size_t i) {
    return fmt_err_pair("x", x, "height", h, err, i);
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

std::string fmt_heatmap(int row, int col, float value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "row=%d, col=%d, value=%.4g", row, col, value);
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

std::optional<HintResult> find_hint(const RenderSnapshot& snap, const HintProjector& tr,
                                    float cursor_x, float cursor_y,
                                    HintIndexCache* index) {
    // Runs every hovered frame, in two phases: find the nearest point without
    // any string work, then format only the winner. Candidates come from the
    // data-space bucket grid (hint_index.h). A surface with no candidate box
    // (a plane seen edge-on) has nothing to find.
    double x_lo = 0.0, x_hi = 0.0, y_lo = 0.0, y_hi = 0.0;
    if (!tr.data_box(cursor_x, cursor_y, kHintHitRadiusPx, x_lo, x_hi, y_lo, y_hi))
        return std::nullopt;

    float best_d2 = kHintHitRadiusPx * kHintHitRadiusPx;
    int         best_kind = -1;          // 0 line, 1 scatter, 2 scatter_z, 3 bar
    std::size_t best_obj  = 0, best_i = 0;
    float       best_px   = 0.0f, best_py = 0.0f;

    // The exact pixel-space circle test; candidates were only conservative.
    auto consider = [&](float px, float py,
                        int kind, std::size_t obj, std::size_t i) {
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

    auto sweep = [&](PlotKind kind, std::size_t o, int kind_id,
                     const CowVec<double>& xs, const CowVec<double>& ys) {
        const PointGrid& g = index ? index->grid(kind, o, xs, ys) : kScan;
        g.for_each_in(xs, ys, x_lo, x_hi, y_lo, y_hi,
                      [&](std::size_t i) {
                          // Behind a perspective eye: not a candidate.
                          const HintProjector::Pt p = tr.at(xs[i], ys[i]);
                          if (p.in_front) consider(p.x, p.y, kind_id, o, i);
                      });
    };

    for (std::size_t o = 0; o < snap.lines.size(); ++o)
        sweep(PlotKind::Line, o, 0, snap.lines[o].x, snap.lines[o].y);
    for (std::size_t o = 0; o < snap.scatters.size(); ++o)
        sweep(PlotKind::Scatter, o, 1, snap.scatters[o].x, snap.scatters[o].y);
    for (std::size_t o = 0; o < snap.scatter_z.size(); ++o)
        sweep(PlotKind::ScatterZ, o, 2, snap.scatter_z[o].x, snap.scatter_z[o].y);
    for (std::size_t o = 0; o < snap.bars.size(); ++o)
        sweep(PlotKind::Bar, o, 3, snap.bars[o].centers, snap.bars[o].heights);

    // Phase 2: build the text for the winning point only.
    if (best_kind >= 0) {
        std::string text;
        switch (best_kind) {
            case 0: {
                const auto& lp = snap.lines[best_obj];
                text = append_label(fmt_point_err(lp.x[best_i], lp.y[best_i], lp.err, best_i),
                                    lp.opts.hint_labels, best_i);
                break;
            }
            case 1: {
                const auto& sp = snap.scatters[best_obj];
                text = append_label(fmt_point_err(sp.x[best_i], sp.y[best_i], sp.err, best_i),
                                    sp.opts.hint_labels, best_i);
                break;
            }
            case 2: {
                const auto& sp = snap.scatter_z[best_obj];
                text = append_label(fmt_z_err(sp.x[best_i], sp.y[best_i], sp.z[best_i], sp.err, best_i),
                                    sp.opts.hint_labels, best_i);
                break;
            }
            default: {
                const auto& bp = snap.bars[best_obj];
                text = append_label(fmt_bar_err(bp.centers[best_i], bp.heights[best_i], bp.err, best_i),
                                    bp.opts.hint_labels, best_i);
                break;
            }
        }
        return HintResult{ std::move(text), best_px, best_py };
    }

    // Heatmap fallback: the cell under the cursor, in cell-index space via
    // col_at()/row_at(). Storage is row 0 first regardless of origin, so undo
    // the origin=="lower" flip to find the displayed row.
    double dx = 0.0, dy = 0.0;
    if (!tr.at_pixel(cursor_x, cursor_y, dx, dy)) return std::nullopt;
    for (const auto& hp : snap.heatmaps) {
        if (hp.rows <= 0 || hp.cols <= 0) continue;
        const double fc = hp.col_at(dx), fr = hp.row_at(dy);
        if (fc < 0.0 || fc >= hp.cols || fr < 0.0 || fr >= hp.rows) continue;

        const int col = std::clamp(static_cast<int>(fc), 0, hp.cols - 1);
        const int row_from_bottom = std::clamp(static_cast<int>(fr), 0, hp.rows - 1);
        const int row = (hp.opts.origin == "lower") ? row_from_bottom : (hp.rows - 1 - row_from_bottom);
        const std::size_t idx = static_cast<std::size_t>(row) * static_cast<std::size_t>(hp.cols)
                               + static_cast<std::size_t>(col);
        const float value = idx < hp.data.size() ? hp.data[idx] : 0.0f;
        return HintResult{ append_label(fmt_heatmap(row, col, value), hp.opts.hint_labels, idx),
                            cursor_x, cursor_y };
    }

    return std::nullopt;
}

std::optional<HintResult> find_hint3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                                      float cursor_x, float cursor_y,
                                      HintIndexCache* index) {
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
        for (std::size_t k = 0; k < b.count(); ++k) {
            float depth = 0.0f;
            if (bar3d_ray_hit(b, k, proj, cursor_x, cursor_y, depth))
                hits.push_back({ What::Bar, o, k, depth });
        }
    }

    // Every surface cell (two triangle tests each).
    for (std::size_t o = 0; o < snap.surfaces.size(); ++o) {
        const SurfacePlot& s = snap.surfaces[o];
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
        for (std::size_t f = 0; f < s.face_count(); ++f) {
            float depth = 0.0f;
            std::size_t vertex = 0;
            if (surface_tri_ray_hit(s, f, proj, cursor_x, cursor_y, depth, vertex))
                hits.push_back({ What::Mesh, o, vertex, depth });
        }
    }

    // Every cloud marker, tested in screen space (markers have no surface) at
    // the point's depth, so occlusion matches the picture. Radius: the
    // marker's half size, at least kHintHitRadiusPx.
    for (std::size_t o = 0; o < snap.scatter3d.size(); ++o) {
        const Scatter3DPlot& s = snap.scatter3d[o];
        if (s.opts.marker == MarkerStyle::None) continue;
        const float r = std::max(s.opts.size * 0.5f, kHintHitRadiusPx);
        const Transform3D& tf = proj.transform();
        for (std::size_t i = 0; i < s.count(); ++i) {
            const Vec3 b = tf.to_box(s.x[i], s.y[i], s.z[i]);
            if (!proj.in_front(b)) continue;
            const Px3 q = proj.project_box(b);
            const float dx = q.x - cursor_x, dy = q.y - cursor_y;
            if (dx * dx + dy * dy > r * r) continue;
            hits.push_back({ What::Marker, o, i, q.depth });
        }
    }

    // Every path vertex, in screen space. Radius: the drawn half width at the
    // box centre, at least kHintHitRadiusPx.
    for (std::size_t o = 0; o < snap.lines3d.size(); ++o) {
        const Line3DPlot& l = snap.lines3d[o];
        if (l.opts.linewidth <= 0.0f) continue;
        const float r = std::max(l.opts.linewidth * 0.5f, kHintHitRadiusPx);
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
        if (c.what == What::Bar) {
            const Bar3DPlot& b = snap.bars3d[c.object];
            return HintResult{ append_label(fmt_bar3d(b, c.element),
                                            b.opts.hint_labels, c.element),
                               cursor_x, cursor_y };
        }
        if (c.what == What::Surface) {
            const SurfacePlot& s = snap.surfaces[c.object];
            return HintResult{ append_label(fmt_surface(s, c.element),
                                            s.opts.hint_labels, c.element),
                               cursor_x, cursor_y };
        }
        if (c.what == What::Mesh) {
            const SurfaceTriPlot& s = snap.surface_tri[c.object];
            return HintResult{ append_label(fmt_surface_tri(s, c.element),
                                            s.opts.hint_labels, c.element),
                               cursor_x, cursor_y };
        }
        if (c.what == What::Marker) {
            const Scatter3DPlot& s = snap.scatter3d[c.object];
            return HintResult{ append_label(fmt_scatter3d(s, c.element),
                                            s.opts.hint_labels, c.element),
                               cursor_x, cursor_y };
        }
        if (c.what == What::Vertex) {
            const Line3DPlot& l = snap.lines3d[c.object];
            return HintResult{ append_label(fmt_line3d(l, c.element),
                                            l.opts.hint_labels, c.element),
                               cursor_x, cursor_y };
        }
        const PlaneSnapshot& pl = snap.planes[c.object];
        if (index) index->set_plane(static_cast<int>(c.object));
        if (auto r = find_hint(pl.sheet, HintProjector(proj, pl.orient, pl.offset),
                               cursor_x, cursor_y, index))
            return r;
    }
    return std::nullopt;
}

} // namespace sextant
