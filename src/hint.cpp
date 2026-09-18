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

// "y=2.5" becomes "y=2.5 box ±0.3 cap +0.6/-0.5": each part as the offsets
// it was given, "±v" when the two sides agree and "+hi/-lo" when they do not.
// A part is dropped when the series does not carry it or it is zero on both
// sides at this point. Written per value rather than appended to the end of
// the line, so an x uncertainty sits next to x and a y one next to y. An ASCII
// minus, since U+2212 is outside the hover font's range.
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

// No no-error fast path here, unlike fmt_point_err: with nothing to report
// fmt_value emits exactly "x=%.4g" / "height=%.4g", so this reproduces the
// old fmt_bar() text character for character and that function is gone.
std::string fmt_bar_err(double x, double h, const ErrorBarData& err, std::size_t i) {
    return fmt_err_pair("x", x, "height", h, err, i);
}

std::string fmt_z(double x, double y, double z) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, z=%.4g", x, y, z);
    return buf;
}

// Z is the colormapped value, which carries no error bar of its own — only
// the position does, so it is appended plain.
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

// "x=400, y=1.5, height=3.42". The two grid axes are named by the letters the
// orientation maps them to, because a bar's u and v *are* two of x/y/z -- so
// the tooltip reads in the same vocabulary as the axis titles beside it, and a
// reader can find the bar in the Data panel, whose grid headers carry those
// same two coordinates.
//
// The grid indices are deliberately absent, which is where this differs from
// the heatmap above: a heatmap cell has no coordinates of its own to report,
// and a bar has two real ones.
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
    // The base only when there is one worth reporting: bars standing on zero
    // are the common case, and a ", base=0" on every one of them is noise
    // that makes the two numbers that matter harder to find.
    const double base = b.bottom_at(k);
    if (base != 0.0)
        std::snprintf(buf + n, sizeof(buf) - static_cast<std::size_t>(n),
                      ", base=%.4g", base);
    return buf;
}

// A surface's sample, in the same shape. The third coordinate is `z=` rather
// than `height=` because a surface has no base to measure a height from: a bar
// stands somewhere and rises, a sheet simply passes through a point, and
// calling that a height would invite the reader to look for the bottom of it.
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

// A *vertex* of a mesh. Three independent coordinates and no Axis3Map -- a
// mesh stands on no pair of axes, exactly as a cloud and a path do not -- and a
// fourth line when the mesh has a `colors` vector, since that value is the one
// thing about the vertex the picture encodes as colour.
//
// A vertex and not a face, on §7b's terms with three for four: a face has three
// samples and no identity of its own, so what is reported is the nearest of the
// three -- which is also the index a caller's `hint_labels` entry is written
// against. line3d's vertex rule, arrived at from the other direction.
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

// A marker of a cloud. Three independent coordinates and no Axis3Map, since a
// scatter3d has no orientation to map through -- and a fourth line when the
// series has a `colors` vector, because that value is the one thing about the
// point the picture encodes as colour and so is the one the reader cannot read
// off the position.
// "x=1 box ±0.2, y=2, z=3 cap +0.5/-0.1": a point's position with each axis's
// error offsets beside its own coordinate -- fmt_value()'s form, in three
// directions (v1.0 step 17). The colour line, when there is one, follows it.
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

// A *vertex* of a path, and a cloud's format exactly -- because what it names
// is the same thing: one of the points the caller gave. A path is hovered at
// its vertices rather than along its segments for that reason. The stretch
// between two of them is drawn, but no value was measured there, so a tooltip
// over it could only report a position the reader can already see, and the
// `hint_labels` a caller wrote are per point and would have nothing to attach
// to.
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
        // A 3D cell's `tr` is left default -- there are no 2D limits to put
        // in it -- so its rect comes from the projector, which was fitted to
        // that very frame. Same rectangle either way, from whichever of the
        // two actually knows it.
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
    // Runs on every frame the cursor is over the plot, so it is written in two
    // phases. Phase 1 finds the nearest point and does *no* string work --
    // formatting a candidate that is then discarded costs one allocation per
    // point examined, which at 200k points dominated everything else. Phase 2
    // formats the winner alone.
    //
    // Candidates come from a data-space bucket grid (hint_index.h) rather than
    // a scan of every point: the box below is the hit radius mapped back into
    // data space, and only points in the overlapping cells are transformed.
    // Without an index PointGrid falls back to a linear scan applying the same
    // box test inline.
    // A surface the cursor names no point on -- a plane seen edge-on -- has
    // no candidate box and so nothing on it to find.
    double x_lo = 0.0, x_hi = 0.0, y_lo = 0.0, y_hi = 0.0;
    if (!tr.data_box(cursor_x, cursor_y, kHintHitRadiusPx, x_lo, x_hi, y_lo, y_hi))
        return std::nullopt;

    float best_d2 = kHintHitRadiusPx * kHintHitRadiusPx;
    int         best_kind = -1;          // 0 line, 1 scatter, 2 scatter_z, 3 bar
    std::size_t best_obj  = 0, best_i = 0;
    float       best_px   = 0.0f, best_py = 0.0f;

    // The candidate set is only conservative — the true (circular, pixel-space)
    // test is here, and is what makes the indexed and unindexed paths agree.
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

    // The no-index fallback is a shared *const* pass-through grid rather than
    // a dummy cache object: nothing mutates it, so no thread-visible state is
    // introduced by taking this branch.
    static const PointGrid kScan{};

    auto sweep = [&](PlotKind kind, std::size_t o, int kind_id,
                     const CowVec<double>& xs, const CowVec<double>& ys) {
        const PointGrid& g = index ? index->grid(kind, o, xs, ys) : kScan;
        g.for_each_in(xs, ys, x_lo, x_hi, y_lo, y_hi,
                      [&](std::size_t i) {
                          // A point behind a perspective eye projects to the
                          // wrong side of the picture; it is not a candidate.
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

    // Heatmap fallback: cursor inside one of a heatmap's cells. The test runs
    // in *cell index* space, not data space — col_at()/row_at() map the
    // cursor through the plot's own extent, so a reversed or offset range
    // needs no special case here and the bounds stay the plain [0,cols) x
    // [0,rows). Storage (RenderSnapshot::heatmaps[i].data) is always
    // row-major with row 0 first, regardless of origin — only the GPU texture
    // upload is vertically flipped for origin=="lower" (see
    // DataRenderer::draw_heatmap in data_renderer.cpp). To find the storage
    // row actually displayed at a given data-y, reverse that same flip here.
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
    // Nearest surface first, by the depth of the cursor's own ray hit -- not
    // by the object's distance as a whole, which is a different question the
    // moment a plane is tilted or a bar is tall. A surface the ray misses
    // (a plane seen edge-on, a bar beside the cursor, anything behind the
    // eye) drops out here rather than inside find_hint().
    //
    // Three kinds share one list, because "which of these is drawn in front of
    // the cursor" is one question. `what` says which vector `object` indexes;
    // `element` is the flat bar index, the nearest sample of a surface cell, or
    // unused for a plane.
    enum class What { Plane, Bar, Surface, Mesh, Marker, Vertex };
    struct Candidate { What what; std::size_t object, element; float depth; };
    std::vector<Candidate> hits;

    for (std::size_t i = 0; i < snap.planes.size(); ++i) {
        const PlaneSnapshot& pl = snap.planes[i];
        // Not drawn, not hinted: a tooltip about something invisible is a
        // tooltip about nothing the reader can see.
        if (!plane_drawn(pl)) continue;
        double u = 0.0, v = 0.0;
        float  depth = 0.0f;
        if (!plane_ray_hit(proj, pl.orient, pl.offset, cursor_x, cursor_y, u, v, depth))
            continue;
        hits.push_back({ What::Plane, i, 0, depth });
    }

    // Every bar, tested individually. There is no spatial index here, unlike
    // the point search find_hint() runs: the slab test is a couple of dozen
    // flops and nothing is projected until it hits, so a grid large enough for
    // this to matter is already too large to render. The narrowing that would
    // apply, if it ever does, is a DDA over the grid's own u and v lines
    // rather than a bucket grid, since the bars *are* a grid.
    for (std::size_t o = 0; o < snap.bars3d.size(); ++o) {
        const Bar3DPlot& b = snap.bars3d[o];
        for (std::size_t k = 0; k < b.count(); ++k) {
            float depth = 0.0f;
            if (bar3d_ray_hit(b, k, proj, cursor_x, cursor_y, depth))
                hits.push_back({ What::Bar, o, k, depth });
        }
    }

    // Every cell of every surface, on the same argument -- two triangle tests
    // per cell, nothing projected until one hits. A surface is more cells than
    // a bar grid is bars, so this is where the DDA narrowing above would earn
    // its keep first; it is still a few thousand flops on a grid dense enough
    // to be worth drawing.
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

    // Every face of every mesh, on the grid surface's terms exactly: one
    // triangle test per face through the same tri_ray_t(), nothing projected
    // until one hits. A mesh is the kind most likely to make this the hot loop
    // -- it has no grid structure to narrow by at all, so the DDA over u and v
    // lines that would rescue a big surface has no counterpart here. A BVH is
    // the narrowing that would apply, if it ever does.
    for (std::size_t o = 0; o < snap.surface_tri.size(); ++o) {
        const SurfaceTriPlot& s = snap.surface_tri[o];
        for (std::size_t f = 0; f < s.face_count(); ++f) {
            float depth = 0.0f;
            std::size_t vertex = 0;
            if (surface_tri_ray_hit(s, f, proj, cursor_x, cursor_y, depth, vertex))
                hits.push_back({ What::Mesh, o, vertex, depth });
        }
    }

    // Every marker of every cloud, and the one kind here that is *not* a ray
    // cast: a marker is a symbol drawn at a pixel with a pixel size, so what
    // the cursor is over is a screen-space question and casting a ray at a
    // thing with no surface would have nothing to intersect. What it
    // contributes to the list is the point's own depth, which is what the
    // billboard gave the depth buffer -- so a marker competes with the
    // geometry on the same terms the picture resolved them on, and one behind
    // an opaque bar loses to it here exactly as it is hidden there.
    //
    // The radius is the marker's own half-extent, floored at the hit radius a
    // 2D point search uses: a 4 px marker would otherwise be almost
    // unhoverable, and the floor is what makes a small symbol a target rather
    // than a test of aim.
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

    // Every vertex of every path, on the marker's terms rather than the ray
    // cast's: a vertex has no surface either, and what the cursor is over is
    // again a screen-space question. The hit radius is the ribbon's *drawn*
    // half width where that is the larger -- a thick path should be hoverable
    // over the ink it actually puts on the screen -- floored at the same
    // kHintHitRadiusPx a thin one would otherwise be unhoverable below.
    //
    // Measured at the box centre, which is where `linewidth` is defined
    // (line3d_half_width()), rather than per vertex: the alternative makes a
    // path's near end a larger target than its far end, which is true of the
    // ink and is not a property a reader can aim by.
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

    // First answer wins, which is what makes the tooltip agree with the
    // picture: the nearest thing is the one drawn over the others there.
    // A bar or a surface answers by being *hit* -- both are geometry rather
    // than a sheet with points on it, so there is nothing further to search
    // and nothing behind them to fall through to. Only a plane can decline,
    // by having nothing near the cursor.
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
