#pragma once
#include "sextant/axes3d.h"
#include "coord_transform.h"
#include "plot_objects.h"
#include "renderer/plot_rect.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace sextant {

// -------------------------------------------------------------------------
// Vec3 arithmetic
// -------------------------------------------------------------------------
// Vec3 itself is public (Camera3D carries one); this is the only place that
// does anything with it.
inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*(Vec3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const double n = length(a);
    return n > 0.0 ? a * (1.0 / n) : Vec3{ 0.0, 0.0, 0.0 };
}

// -------------------------------------------------------------------------
// Transform3D: data space -> box space
// -------------------------------------------------------------------------
// The 3D analogue of CoordTransform's first half, and the reason the chain is
// a chain rather than one matrix: this is where the caller's units go away.
// Each axis's resolved limits map onto a box centred on the origin with side
// lengths BoxAspect, so everything downstream -- the camera, the fit, the
// depth -- works in one scale-free space. See BoxAspect for why.
//
// A degenerate axis (lo == hi) would divide by zero; Axes3D's limit setters
// reject one, and this collapses it to the box centre rather than producing
// an infinity if one is built directly.
struct Transform3D {
    double xmin = 0.0, xmax = 1.0;
    double ymin = 0.0, ymax = 1.0;
    double zmin = 0.0, zmax = 1.0;
    BoxAspect aspect;

    static double axis_to_box(double v, double lo, double hi, double side) {
        const double span = hi - lo;
        return span != 0.0 ? ((v - lo) / span - 0.5) * side : 0.0;
    }
    static double box_to_axis(double b, double lo, double hi, double side) {
        return side != 0.0 ? lo + (b / side + 0.5) * (hi - lo) : lo;
    }

    double box_x(double x) const { return axis_to_box(x, xmin, xmax, aspect.x); }
    double box_y(double y) const { return axis_to_box(y, ymin, ymax, aspect.y); }
    double box_z(double z) const { return axis_to_box(z, zmin, zmax, aspect.z); }

    Vec3 to_box(double x, double y, double z) const {
        return { box_x(x), box_y(y), box_z(z) };
    }

    // The inverse, for hit-testing and for placing something known in box
    // coordinates (a pane, a box edge) back on an axis.
    double data_x(double bx) const { return box_to_axis(bx, xmin, xmax, aspect.x); }
    double data_y(double by) const { return box_to_axis(by, ymin, ymax, aspect.y); }
    double data_z(double bz) const { return box_to_axis(bz, zmin, zmax, aspect.z); }

    // Half the box's side lengths -- the coordinate of its +x/+y/+z faces.
    Vec3 half_extent() const { return { aspect.x * 0.5, aspect.y * 0.5, aspect.z * 0.5 }; }
};

// -------------------------------------------------------------------------
// auto_scale3d: three padded intervals, the 3D counterpart of auto_scale()
// -------------------------------------------------------------------------
// Written against box *axes* rather than against a plot kind's own names,
// because a Bar3DPlot's u/v/h are two of x/y/z and the third in an order its
// `orient` decides -- so the loop below fills a three-element array through
// Axis3Map and never has to branch on the orientation. A plane's two spanned
// axes and its offset go through the same map, which is why adding them below
// is four lines rather than a second traversal. The 2D auto_scale() is
// untouched: it answers a different question about different objects, and this
// one *calls* it, per plane, to ask that question about the plane's own sheet.
struct DataBounds3D {
    double xmin = 0.0, xmax = 1.0;
    double ymin = 0.0, ymax = 1.0;
    double zmin = 0.0, zmax = 1.0;
};

// True when a plane carries anything at all to scale to. An empty plane must
// not contribute its offset either: "auto" means "from the data", and a plane
// with nothing on it has none -- the same rule that keeps an axes with no data
// on its declared 0..1.
inline bool plane_has_data(const PlaneSnapshot& p) {
    const AllPlotData all = p.sheet.all();
    return !all.lines.empty() || !all.scatters.empty() || !all.bars.empty()
        || !all.heatmaps.empty() || !all.scatter_z.empty();
}

// True when a plane is drawn into the scene this frame. Every path that puts
// ink in the box asks this -- the GPU quad, the SVG plan, the contours -- and
// nothing that decides *layout* does: Plane2DOptions::visible hides the
// drawing without moving the limits, the colorbar or the legend, so a slice
// can be switched off to look behind it without the rest of the picture
// rearranging itself in the same instant.
inline bool plane_drawn(const PlaneSnapshot& p) {
    return p.opts.visible && plane_has_data(p);
}

inline DataBounds3D auto_scale3d(const std::vector<Bar3DPlot>& bars,
                                 const std::vector<PlaneSnapshot>& planes,
                                 const std::vector<SurfacePlot>& surfaces,
                                 const std::vector<Scatter3DPlot>& points,
                                 const std::vector<Line3DPlot>& lines,
                                 const std::vector<SurfaceTriPlot>& meshes,
                                 double pad = 0.05) {
    double lo[3] = {  std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max() };
    double hi[3] = { -std::numeric_limits<double>::max(),
                     -std::numeric_limits<double>::max(),
                     -std::numeric_limits<double>::max() };

    for (const Bar3DPlot& b : bars) {
        const Axis3Map m = axis_map(b.orient);
        // A bar's *footprint*, not its grid coordinate: a bar centred on the
        // last u still has half its width beyond it, and a limit that cut
        // through it would read as a clipped chart rather than as a limit.
        const double hu = b.u_width * 0.5, hv = b.v_width * 0.5;
        for (std::size_t i = 0; i < b.u.size(); ++i) {
            lo[m.u] = std::min(lo[m.u], b.u[i] - hu);
            hi[m.u] = std::max(hi[m.u], b.u[i] + hu);
        }
        for (std::size_t j = 0; j < b.v.size(); ++j) {
            lo[m.v] = std::min(lo[m.v], b.v[j] - hv);
            hi[m.v] = std::max(hi[m.v], b.v[j] + hv);
        }
        // Base to tip, and *not* forced to include zero the way a 2D bar's
        // does: `bottom` says where these bars stand, so a chart of bars from
        // 100 to 105 is about that range, not about the distance to the origin.
        for (std::size_t k = 0; k < b.count(); ++k) {
            lo[m.h] = std::min(lo[m.h], b.h_lo(k));
            hi[m.h] = std::max(hi[m.h], b.h_hi(k));
        }
    }

    // A surface's samples *are* its extent, with no footprint to widen them:
    // a bar centred on the last u still has half its width beyond it, while a
    // surface simply stops at its last sample. So the grid coordinates go in
    // unmodified, and the heights go in as themselves -- not forced to include
    // zero, for the same reason a bar chart's base-to-tip span is not.
    for (const SurfacePlot& s : surfaces) {
        if (s.cell_count() == 0 || s.heights.size() < s.count()) continue;
        const Axis3Map m = axis_map(s.orient);
        for (std::size_t i = 0; i < s.u.size(); ++i) {
            lo[m.u] = std::min(lo[m.u], s.u[i]);
            hi[m.u] = std::max(hi[m.u], s.u[i]);
        }
        for (std::size_t j = 0; j < s.v.size(); ++j) {
            lo[m.v] = std::min(lo[m.v], s.v[j]);
            hi[m.v] = std::max(hi[m.v], s.v[j]);
        }
        for (std::size_t k = 0; k < s.count(); ++k) {
            lo[m.h] = std::min(lo[m.h], s.height_at(k));
            hi[m.h] = std::max(hi[m.h], s.height_at(k));
        }
    }

    // A scatter's points are its extent, and the one kind here that needs no
    // Axis3Map at all: three independent coordinates go straight onto the
    // three axes. Nothing widens them -- a marker's size is in pixels, so it
    // has no data-space footprint to include the way a bar's width has, and
    // the pad below is what keeps a point off the box face. That is the same
    // bargain the 2D auto_scale() strikes for a 2D scatter.
    // A point's error bars reach as far as their data does (v1.0 step 17), the
    // 2D grow_err()'s rule. The pixel lengths -- `boxwidth` on an axis with no
    // box data, the caps -- are not included, for the reason a path's width is
    // not: they are resolved from the box this function is computing.
    auto grow_err = [&](const CowVec<double>& xs, const CowVec<double>& ys,
                        const CowVec<double>& zs, const ErrorBar3DData& err) {
        if (err.empty()) return;
        const CowVec<double>* ps[3] = { &xs, &ys, &zs };
        for (int a = 0; a < 3; ++a) {
            const CowVec<double>& p = *ps[a];
            const bool cap = err.has_cap(a), box = err.has_box(a);
            if (!cap && !box) continue;
            for (std::size_t i = 0; i < p.size(); ++i) {
                ErrOffsets e{};
                if (cap) e = err.cap(a, i);
                if (box) {
                    const ErrOffsets b = err.box(a, i);
                    e.lo = std::max(e.lo, b.lo);
                    e.hi = std::max(e.hi, b.hi);
                }
                lo[a] = std::min(lo[a], p[i] - e.lo);
                hi[a] = std::max(hi[a], p[i] + e.hi);
            }
        }
    };

    for (const Scatter3DPlot& s : points) {
        grow_err(s.x, s.y, s.z, s.err);
        for (std::size_t i = 0; i < s.count(); ++i) {
            lo[0] = std::min(lo[0], s.x[i]);  hi[0] = std::max(hi[0], s.x[i]);
            lo[1] = std::min(lo[1], s.y[i]);  hi[1] = std::max(hi[1], s.y[i]);
            lo[2] = std::min(lo[2], s.z[i]);  hi[2] = std::max(hi[2], s.z[i]);
        }
    }

    // A path's points are its extent, on exactly a cloud's terms -- the
    // segments between them are straight, so they go nowhere its endpoints do
    // not, and `loop` closes the path without adding a point. Its `linewidth`
    // is a world length and so does have a data-space footprint, unlike a
    // marker's pixel size; it is deliberately not included, because the width
    // is resolved from the *box*, which is what this function is computing --
    // folding it in would make the limits depend on themselves.
    for (const Line3DPlot& l : lines) {
        grow_err(l.x, l.y, l.z, l.err);
        for (std::size_t i = 0; i < l.count(); ++i) {
            lo[0] = std::min(lo[0], l.x[i]);  hi[0] = std::max(hi[0], l.x[i]);
            lo[1] = std::min(lo[1], l.y[i]);  hi[1] = std::max(hi[1], l.y[i]);
            lo[2] = std::min(lo[2], l.z[i]);  hi[2] = std::max(hi[2], l.z[i]);
        }
    }

    // A mesh's vertices are its extent, on the cloud's terms and with the
    // path's caveat. The triangles between them are flat, so they go nowhere
    // the vertices do not; and a vertex named by no triangle is *still*
    // included, because it is data the caller gave and the limits are about
    // the data rather than about what the topology happened to reach. The
    // wireframe's width is a world length resolved from the box, so folding it
    // in would make the limits depend on themselves -- line3d's caveat exactly.
    for (const SurfaceTriPlot& m : meshes) {
        for (std::size_t i = 0; i < m.count(); ++i) {
            lo[0] = std::min(lo[0], m.x[i]);  hi[0] = std::max(hi[0], m.x[i]);
            lo[1] = std::min(lo[1], m.y[i]);  hi[1] = std::max(hi[1], m.y[i]);
            lo[2] = std::min(lo[2], m.z[i]);  hi[2] = std::max(hi[2], m.z[i]);
        }
    }

    // Planes feed the parent's limits directly, which is the whole consequence
    // of plane-coordinates-being-parent-coordinates (spec_3d.md §6): the 2D
    // extent of what is on the plane lands on the two axes the plane spans,
    // through the same Axis3Map a bar's u/v go through, and the offset lands
    // on the third -- so a slice at z = 0.5 pulls the box out to include 0.5,
    // and a heatmap over x 400..700 makes the box's x axis about that.
    //
    // Unpadded (pad = 0): the padding belongs to this function's own final
    // pass, and applying auto_scale()'s as well would compound to 1.05^2.
    for (const PlaneSnapshot& p : planes) {
        if (!plane_has_data(p)) continue;
        const Axis3Map m = axis_map(p.orient);
        const DataBounds b = auto_scale(p.sheet.all(), 0.0);
        lo[m.u] = std::min(lo[m.u], b.xmin);  hi[m.u] = std::max(hi[m.u], b.xmax);
        lo[m.v] = std::min(lo[m.v], b.ymin);  hi[m.v] = std::max(hi[m.v], b.ymax);
        lo[m.h] = std::min(lo[m.h], p.offset);
        hi[m.h] = std::max(hi[m.h], p.offset);
    }

    DataBounds3D out;
    double* omin[3] = { &out.xmin, &out.ymin, &out.zmin };
    double* omax[3] = { &out.xmax, &out.ymax, &out.zmax };
    for (int a = 0; a < 3; ++a) {
        double l = lo[a], h = hi[a];
        if (l > h)  { l = 0.0; h = 1.0; }        // nothing on this axis at all
        if (l == h) { l -= 0.5; h += 0.5; }      // a single plane of bars
        const double d = (h - l) * pad;
        *omin[a] = l - d;
        *omax[a] = h + d;
    }
    return out;
}

// -------------------------------------------------------------------------
// Projector3D: box space -> pixels
// -------------------------------------------------------------------------
// One object, two consumers -- the NanoVG/SVG annotation paths that project on
// the CPU, and (from step 4) the data pass that hands the same view to the
// GPU. They must come from here rather than from two transcriptions, for the
// reason compute_figure_layout() exists: the raster and vector outputs drifted
// exactly where each carried its own copy of shared arithmetic. Here the
// failure would be worse than a drift -- annotation that does not sit on the
// geometry it annotates.
struct Px3 {
    float x = 0.0f, y = 0.0f;   // pixels, in the figure's own coordinates
    // Distance from the camera along the view direction, in box units.
    // Larger is further away. Not a GL depth value: nothing normalizes it,
    // because its only consumers sort with it.
    float depth = 0.0f;
    // The perspective divisor -- eye-space depth under a perspective camera,
    // and exactly 1 under an orthographic one, where there is no divide.
    //
    // w <= 0 means the point is *behind* the eye, where x and y are not
    // merely off-screen but meaningless: the projection of a point behind
    // the camera lands on the opposite side of the picture from where it
    // belongs. This is the one piece of arithmetic in 3D with no 2D
    // precedent, and the reason for the clipping helpers below -- every CPU
    // consumer has to cut its geometry at the near plane before it can
    // draw it.
    float w = 1.0f;

    bool in_front() const { return w > 0.0f; }
};

// The range a perspective field of view is held inside, in degrees. Below the
// lower bound the picture is indistinguishable from orthographic while the
// derived eye distance runs away; above the upper one the near corners of the
// box tear across the frame. Shared by the projector, the clamp and the panel
// so there is one answer to "how wide can it get".
inline constexpr double kMinFov = 5.0;
inline constexpr double kMaxFov = 120.0;

// Orthographic is written out as a rotation, one uniform scale and a y flip
// rather than as a 4x4, which is not a shortcut: it is what makes a test able
// to state the expected pixel of a corner in one line of arithmetic.
// Perspective adds the divide, and the eye distance it needs is *derived*
// rather than stored -- see the constructor.
class Projector3D {
public:
    Projector3D() = default;

    // `margin` is the fraction of the frame left empty on each side for the
    // annotation to sit in (Box3DStyle::margin), so the box is fitted into
    // (1 - 2*margin) of the frame in each direction.
    Projector3D(const Transform3D& tf, const Camera3D& cam,
                const PlotRect& frame, float margin)
        : tf_(tf), frame_(frame), target_(cam.target)
    {
        const double az = cam.azimuth * kDeg2Rad;
        const double el = std::clamp(cam.elevation, -89.0, 89.0) * kDeg2Rad;

        // From the target toward the eye. z is up: azimuth turns about it,
        // elevation lifts off the xy plane.
        eye_dir_ = { std::cos(el) * std::cos(az),
                     std::cos(el) * std::sin(az),
                     std::sin(el) };
        fwd_   = eye_dir_ * -1.0;
        right_ = normalize(cross(fwd_, Vec3{ 0.0, 0.0, 1.0 }));
        up_    = cross(right_, fwd_);

        const float m  = std::clamp(margin, 0.0f, 0.45f);
        const double aw = std::max(1.0, static_cast<double>(frame.w) * (1.0 - 2.0 * m));
        const double ah = std::max(1.0, static_cast<double>(frame.h) * (1.0 - 2.0 * m));
        zoom_ = (cam.zoom > 0.0 && std::isfinite(cam.zoom)) ? cam.zoom : 1.0;

        // Fit: the box's own projected silhouette is measured every frame and
        // sized to fill the cell, so the picture fills its frame at any camera
        // angle and any BoxAspect with nothing for the caller to tune. `zoom`
        // then multiplies the finished picture, which is what the scroll wheel
        // drives -- in both modes, so a magnification never changes how much
        // perspective there is.
        //
        // Measured about the box's own centre, and *not* about the camera
        // target -- the two are the same until something moves the target,
        // and then they are very different. The target belongs in the
        // centring (see project_box), where it translates the picture; in the
        // scale it would shrink the picture every time the view was panned,
        // since sliding the target sideways makes one side of the box the
        // furthest thing from it. That reasoning is what makes the eye
        // distance below a function of the camera *angle* alone, and so what
        // makes the perspective dolly (which moves the target along the view
        // direction) a real approach rather than something the fit undoes.
        const Vec3 h = tf_.half_extent();
        persp_ = (cam.projection == Projection::Perspective);

        if (!persp_) {
            double half_u = 0.0, half_v = 0.0;
            for (int i = 0; i < 8; ++i) {
                const Vec3 c = corner(h, i);
                half_u = std::max(half_u, std::fabs(dot(c, right_)));
                half_v = std::max(half_v, std::fabs(dot(c, up_)));
            }
            // Pixels per box unit: whichever direction runs out of room first.
            double k = 1.0;
            if (half_u > 0.0 || half_v > 0.0)
                k = std::min(half_u > 0.0 ? aw * 0.5 / half_u : 1e30,
                             half_v > 0.0 ? ah * 0.5 / half_v : 1e30);
            k_ = k * zoom_;
            set_depth_range(h);
            return;
        }

        // ---- Perspective ------------------------------------------------
        // The half-angles the frame subtends: vertical from the fov, and
        // horizontal from the frame's own proportions, so a wide cell shows
        // more rather than stretching what it shows.
        // Held inside the range here as well as in clamp_camera(): every
        // public path clamps before this is reached, but a Camera3D built
        // directly must not be able to turn the whole projection into NaN --
        // the same bargain Transform3D strikes with a degenerate axis.
        const double fov = std::isfinite(cam.fov)
                               ? std::clamp(cam.fov, kMinFov, kMaxFov) : 45.0;
        const double tv = std::tan(fov * 0.5 * kDeg2Rad);
        const double tu = tv * aw / ah;
        s_ = ah * 0.5 / tv;   // pixels per unit of (lateral / eye depth)

        // The eye distance is derived, not stored: the smallest distance that
        // still puts every box corner inside the reserved area. Each corner
        // gives one closed-form bound -- |u| / tan(half_fov_u) <= dist + t --
        // so the fit is one pass over eight corners with no search, and it is
        // exactly tight at whichever corner binds. Distances shorter than
        // this would crop the box; longer ones would shrink it.
        double d = 0.0;
        for (int i = 0; i < 8; ++i) {
            const Vec3 c = corner(h, i);
            const double t = dot(c, fwd_);
            d = std::max(d, std::max(std::fabs(dot(c, right_)) / tu,
                                     std::fabs(dot(c, up_))    / tv) - t);
        }
        // A box with no extent at all (every side degenerate) leaves d at 0,
        // where the divide below would be by zero. Nothing else can.
        dist_ = d > 0.0 ? d : 1.0;
        near_ = dist_ * kNearFraction;
        set_depth_range(h);
    }

    // Eye-space depth: how far in front of the eye `p` is, along the view
    // direction. This is Px3::w, and the quantity every near-plane decision
    // is made on. Under an orthographic camera there is no eye, so it is 1 --
    // a constant that makes every point "in front" and every clip a no-op,
    // which is why the ortho path is untouched by any of the clipping below.
    double eye_depth(Vec3 p) const {
        return persp_ ? dot(p - target_, fwd_) + dist_ : 1.0;
    }

    Px3 project_box(Vec3 p) const {
        const Vec3 d = p - target_;
        const double u = dot(d, right_), v = dot(d, up_), t = dot(d, fwd_);
        if (!persp_)
            return { static_cast<float>(cx() + u * k_),
                     // Pixel y grows downward, the box's up does not.
                     static_cast<float>(cy() - v * k_),
                     static_cast<float>(t), 1.0f };

        const double z = t + dist_;
        // Behind the eye, x and y are meaningless whatever we put in them, so
        // the divisor is floored only to keep them finite; `w` carries the
        // truth and the clipping helpers below are the supported way to ask.
        const double zz = std::fabs(z) > near_ ? z : (z < 0.0 ? -near_ : near_);
        const double f = s_ * zoom_ / zz;
        return { static_cast<float>(cx() + u * f),
                 static_cast<float>(cy() - v * f),
                 static_cast<float>(z), static_cast<float>(z) };
    }

    Px3 project(double x, double y, double z) const {
        return project_box(tf_.to_box(x, y, z));
    }

    // ---- Near-plane clipping ------------------------------------------
    // A perspective camera can put part of the box behind the eye -- fly into
    // it and it will -- and a segment with one end behind projects to a line
    // that runs the wrong way across the whole figure. Every CPU consumer of
    // this projector therefore goes through the two helpers below rather than
    // projecting endpoints itself. Under orthographic they are pass-throughs,
    // which is why an ortho picture is unchanged to the bit by their arrival.

    bool in_front(Vec3 p) const { return eye_depth(p) > near_; }

    // Projects `a`-`b`, cutting it at the near plane. False if the whole
    // segment is behind the eye and nothing should be drawn.
    bool project_segment(Vec3 a, Vec3 b, Px3& pa, Px3& pb) const {
        if (persp_) {
            const double za = eye_depth(a), zb = eye_depth(b);
            if (za <= near_ && zb <= near_) return false;
            if (za <= near_) a = lerp(a, b, (near_ - za) / (zb - za));
            else if (zb <= near_) b = lerp(b, a, (near_ - zb) / (za - zb));
        }
        pa = project_box(a);
        pb = project_box(b);
        return true;
    }

    // Sutherland-Hodgman against the one plane that matters. `out` is left
    // empty when the polygon is entirely behind the eye.
    void project_polygon(const std::vector<Vec3>& pts, std::vector<Px3>& out) const {
        out.clear();
        if (pts.empty()) return;
        if (!persp_) {
            out.reserve(pts.size());
            for (const Vec3& p : pts) out.push_back(project_box(p));
            return;
        }
        const std::size_t n = pts.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Vec3& cur = pts[i];
            const Vec3& nxt = pts[(i + 1) % n];
            const double zc = eye_depth(cur), zn = eye_depth(nxt);
            const bool in_c = zc > near_, in_n = zn > near_;
            if (in_c) out.push_back(project_box(cur));
            if (in_c != in_n)
                out.push_back(project_box(
                    lerp(cur, nxt, (near_ - zc) / (zn - zc))));
        }
    }

    // ---- The inverse: a pixel back to a ray -----------------------------
    // What replaces CoordTransform's to_data_x/to_data_y in 3D. A pixel does
    // not name a point in a scene, it names a line through it, so the answer
    // is a ray in *box* space and the caller supplies the surface -- which is
    // exactly why hit-testing is planes-only (spec_3d.md §11): a plane gives
    // the one intersection a closed form.
    //
    // `dir` is not normalized. It is scaled so that the parameter along it is
    // eye depth under perspective and distance along the view direction under
    // orthographic, which is what makes the two branches answer in the same
    // quantity -- the same reason eye_coord() is one function.
    struct Ray3 { Vec3 origin, dir; };

    Ray3 ray_from_pixel(float px, float py) const {
        const double dx = static_cast<double>(px) - cx();
        const double dy = cy() - static_cast<double>(py);   // pixel y grows down
        if (!persp_) {
            const double s = k_ > 0.0 ? 1.0 / k_ : 0.0;
            return { target_ + right_ * (dx * s) + up_ * (dy * s), fwd_ };
        }
        const double s = (s_ * zoom_) > 0.0 ? 1.0 / (s_ * zoom_) : 0.0;
        return { eye_point(), fwd_ + right_ * (dx * s) + up_ * (dy * s) };
    }

    // Direction from the box toward the camera. A face whose outward normal
    // has a non-positive dot with this is facing away -- which is how the
    // three drawn back panes and the labelled box edges are chosen.
    Vec3 eye_dir() const { return eye_dir_; }
    Vec3 forward() const { return fwd_; }
    Vec3 right()   const { return right_; }
    Vec3 up()      const { return up_; }

    // Where the eye is, in box space. Only a perspective camera has one; the
    // orthographic answer is a direction, which is eye_dir() above.
    bool has_eye_point() const { return persp_; }
    Vec3 eye_point() const { return target_ + eye_dir_ * dist_; }

    // Whether a face with outward normal `n`, containing box point `p`, is
    // turned toward the camera. The one place the rule lives: the SVG path
    // uses it to drop the three faces of a bar nobody can see, and it is the
    // same question `plan_box3d()` asks about the back panes -- which is
    // exactly the kind of sign that is invisible when wrong, so it has one
    // implementation and a test that asks the projector for depth instead.
    bool faces_camera(Vec3 p, Vec3 n) const {
        return dot(n, persp_ ? eye_point() - p : eye_dir_) > 0.0;
    }

    // ---- The same projection as a 4x4, for the GPU ---------------------
    // Column-major, ready for glUniformMatrix4fv, and mapping *box* space to
    // clip space -- the same space and the same arithmetic project_box() does,
    // so the statement "the GPU and the CPU project alike" is one the test can
    // make directly rather than through a change of coordinates. The data ->
    // box step is box_affine() below, which a shader applies first.
    //
    // This is what makes the CPU and GPU paths one projection rather than two
    // transcriptions: the matrix is derived from the same basis, fit and
    // frame that project_box() uses, and the test asserts the two agree pixel
    // for pixel rather than trusting that they do.
    //
    // Depth: linear in view distance under orthographic, and the standard
    // hyperbolic (A*z + B)/z under perspective, which is what a 4x4 can
    // express -- a linear-in-eye-depth NDC z would be quadratic in z and is
    // simply not available here. Its only consumer is the depth buffer within
    // one cell, so the range is chosen generously rather than tightly: the
    // box's own depth extent plus four box radii, so geometry a caller has
    // put outside the limits is still drawn rather than silently clipped.
    std::array<float, 16> clip_matrix(double win_w, double win_h) const {
        const double W = win_w > 0.0 ? win_w : 1.0;
        const double H = win_h > 0.0 ? win_h : 1.0;

        // The basis, and where the box origin sits relative to the target --
        // the linear and constant halves of every row below.
        const Vec3 q0{ -target_.x, -target_.y, -target_.z };
        const double u0 = dot(q0, right_), v0 = dot(q0, up_), t0 = dot(q0, fwd_);

        const double ox = 2.0 * cx() / W - 1.0;   // where the frame centre is in NDC
        const double oy = 1.0 - 2.0 * cy() / H;
        const double n = depth_near_, f = depth_far_;
        const double idz = (f - n) != 0.0 ? 1.0 / (f - n) : 1.0;

        double row[4][4];
        if (!persp_) {
            const double sx = 2.0 * k_ / W, sy = 2.0 * k_ / H;
            set_row(row[0], right_ * sx, u0 * sx + ox);
            set_row(row[1], up_    * sy, v0 * sy + oy);
            set_row(row[2], fwd_ * (2.0 * idz), t0 * 2.0 * idz - 2.0 * n * idz - 1.0);
            set_row(row[3], Vec3{ 0.0, 0.0, 0.0 }, 1.0);
        } else {
            const double sx = 2.0 * s_ * zoom_ / W, sy = 2.0 * s_ * zoom_ / H;
            const double D  = t0 + dist_;          // eye depth at the box origin
            const double za = (f + n) * idz, zb = -2.0 * f * n * idz;
            set_row(row[0], right_ * sx + fwd_ * ox, u0 * sx + D * ox);
            set_row(row[1], up_    * sy + fwd_ * oy, v0 * sy + D * oy);
            set_row(row[2], fwd_ * za,               D * za + zb);
            set_row(row[3], fwd_,                    D);
        }

        // Row-major above, column-major out -- GL wants columns.
        std::array<float, 16> m{};
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                m[static_cast<std::size_t>(c * 4 + r)] = static_cast<float>(row[r][c]);
        return m;
    }

    // The data -> box step the matrix above deliberately does not include, as
    // the scale and offset a shader applies first:
    //
    //     box = scale * (data - anchor_in_the_buffer) + offset
    //
    // Split out rather than folded in so a vertex buffer can hold data-space
    // corners offset by an anchor -- float precision measured against the
    // data's own span rather than against its distance from the origin, the
    // same bargain the 2D scatter path strikes -- while the matrix stays a
    // statement about the camera alone. Computed in double, including the
    // cancellation at the anchor, and only then narrowed.
    void box_affine(Vec3 anchor, float scale[3], float offset[3]) const {
        double A[3], b[3];
        axis_affine(tf_.xmin, tf_.xmax, tf_.aspect.x, A[0], b[0]);
        axis_affine(tf_.ymin, tf_.ymax, tf_.aspect.y, A[1], b[1]);
        axis_affine(tf_.zmin, tf_.zmax, tf_.aspect.z, A[2], b[2]);
        const double a[3] = { anchor.x, anchor.y, anchor.z };
        for (int i = 0; i < 3; ++i) {
            scale[i]  = static_cast<float>(A[i]);
            offset[i] = static_cast<float>(A[i] * a[i] + b[i]);
        }
    }

    // How many box units one logical pixel covers at `p`. Constant under an
    // orthographic camera and depth-dependent under a perspective one, which
    // is what turns a width given in pixels into a width fixed in the scene:
    // a world-space stroke set from this at one point keeps that thickness in
    // the box and so thins with distance, while an annotation stroke measured
    // in pixels does not (spec_3d.md §4).
    double box_units_per_pixel(Vec3 p) const {
        if (!persp_) return k_ > 0.0 ? 1.0 / k_ : 0.0;
        const double z = eye_depth(p);
        const double denom = s_ * zoom_;
        return denom > 0.0 ? std::max(z, near_) / denom : 0.0;
    }

    bool is_perspective() const { return persp_; }
    // The derived eye distance, in box units. 0 under an orthographic camera,
    // where there is no eye to be at a distance -- the fit is a scale.
    double eye_distance() const { return persp_ ? dist_ : 0.0; }

    // Orthographic only: pixels per box unit, the whole of the projection's
    // scale. Under perspective there is no such number (that is what the
    // divide means), so it reports 0 rather than a plausible-looking average.
    double pixels_per_box_unit() const { return persp_ ? 0.0 : k_; }

    const Transform3D& transform() const { return tf_; }
    const PlotRect&    frame()     const { return frame_; }

private:
    static constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
    // Nearer than this fraction of the eye distance and a point is clipped.
    // It bounds the magnification a near-plane sliver can reach (~1000x)
    // rather than expressing any view of what is worth drawing.
    static constexpr double kNearFraction = 1e-3;

    static Vec3 corner(Vec3 h, int i) {
        return { (i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z };
    }
    static Vec3 lerp(Vec3 a, Vec3 b, double t) { return a + (b - a) * t; }

    // One axis of the data -> box map, as scale and offset.
    static void axis_affine(double lo, double hi, double side, double& a, double& b) {
        const double span = hi - lo;
        if (span == 0.0) { a = 0.0; b = 0.0; return; }   // matches Transform3D
        a = side / span;
        b = -(lo * side / span + 0.5 * side);
    }
    static void set_row(double* r, Vec3 v, double w) {
        r[0] = v.x; r[1] = v.y; r[2] = v.z; r[3] = w;
    }

    // The depth range the GPU matrix maps onto [-1, 1]. Generous rather than
    // tight: nothing else shares this buffer (cells are scissored and do not
    // overlap), so the only thing precision has to separate is one cell's own
    // geometry, and four box radii of headroom keeps data a caller has put
    // outside the limits visible instead of clipped away.
    void set_depth_range(Vec3 h) {
        const double R = length(h);
        double tmin = 0.0, tmax = 0.0;
        for (int i = 0; i < 8; ++i) {
            const double t = dot(corner(h, i) - target_, fwd_);
            tmin = i ? std::min(tmin, t) : t;
            tmax = i ? std::max(tmax, t) : t;
        }
        const double pad = 4.0 * (R > 0.0 ? R : 1.0);
        if (persp_) {
            depth_near_ = near_;                       // the plane the CPU clips at
            depth_far_  = tmax + dist_ + pad;
            if (depth_far_ <= depth_near_) depth_far_ = depth_near_ * 1000.0;
        } else {
            depth_near_ = tmin - pad;
            depth_far_  = tmax + pad;
        }
    }

    float cx() const { return frame_.x + frame_.w * 0.5f; }
    float cy() const { return frame_.y + frame_.h * 0.5f; }

    Transform3D tf_{};
    PlotRect    frame_{ 0.0f, 0.0f, 1.0f, 1.0f };
    Vec3        target_{};
    Vec3        eye_dir_{ 1.0, 0.0, 0.0 };   // box -> camera
    Vec3        fwd_{ -1.0, 0.0, 0.0 };     // camera -> box
    Vec3        right_{ 0.0, 1.0, 0.0 };
    Vec3        up_{ 0.0, 0.0, 1.0 };
    double      zoom_ = 1.0;
    bool        persp_ = false;
    double      k_ = 1.0;      // orthographic: pixels per box unit
    double      s_ = 1.0;      // perspective: pixels per unit of lateral/depth
    double      dist_ = 1.0;   // perspective: derived eye distance
    double      near_ = 1e-3;  // perspective: the near plane, in eye depth
    double      depth_near_ = 0.0, depth_far_ = 1.0;   // what clip_matrix() maps to [-1, 1]
};

// One point of an axis-aligned plane, from its two in-plane coordinates. The
// offset goes on the axis the orientation is normal to, through the same
// Axis3Map a bar3d's u/v/h go through -- one switch in the codebase, not one
// per consumer.
//
// Here rather than beside the plane renderer because it is a coordinate
// function: the contour planner needs it too, and a plot-kind header is the
// wrong thing for that to have to include.
inline Vec3 plane_point(PlaneOrientation orient, double u, double v, double offset) {
    const Axis3Map m = axis_map(orient);
    double c[3];
    c[m.u] = u;
    c[m.v] = v;
    c[m.h] = offset;
    return { c[0], c[1], c[2] };
}

// One component of a Vec3 by box-axis index, so a loop written against
// Axis3Map never has to branch on which of x/y/z it is holding.
inline double vec3_axis(Vec3 v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }

// Where a pixel's ray meets an axis-aligned plane, as that plane's own two
// in-plane coordinates -- which, by §6's decision, are the parent's data
// coordinates along the two axes it spans. This is the 3D inverse the hover
// hint needs, and it is exact: the data -> box map is per-axis, so a plane at
// constant `offset` in data space is a plane at a constant box coordinate,
// and the intersection is one divide.
//
// False when the ray runs parallel to the plane (edge-on, where a pixel names
// no single point on it) or when the hit is behind a perspective eye.
// `depth` comes back as Px3::depth, so hits on different planes compare
// against each other -- larger is further away.
inline bool plane_ray_hit(const Projector3D& proj, PlaneOrientation orient,
                          double offset, float px, float py,
                          double& u, double& v, float& depth) {
    const Axis3Map m = axis_map(orient);
    const Vec3 on_plane = plane_point(orient, 0.0, 0.0, offset);
    const Vec3 pb = proj.transform().to_box(on_plane.x, on_plane.y, on_plane.z);
    const double h = vec3_axis(pb, m.h);

    const Projector3D::Ray3 r = proj.ray_from_pixel(px, py);
    const double dh = vec3_axis(r.dir, m.h);
    if (std::fabs(dh) < 1e-12) return false;
    const double t = (h - vec3_axis(r.origin, m.h)) / dh;
    const Vec3 hit = r.origin + r.dir * t;
    if (!proj.in_front(hit)) return false;

    const Transform3D& tf = proj.transform();
    const double data[3] = { tf.data_x(hit.x), tf.data_y(hit.y), tf.data_z(hit.z) };
    u = data[m.u];
    v = data[m.v];
    depth = proj.project_box(hit).depth;
    return true;
}

// Where a pixel's ray meets an axis-aligned box given in *data* space, as the
// depth of the nearest face it enters through. `plane_ray_hit()`'s sibling,
// and exact for the very same reason: the data -> box map is per-axis, so a
// box with data-space limits is still an axis-aligned box in box space and the
// intersection is three slab tests.
//
// `lo`/`hi` are per box axis, in x/y/z order, and need not be ordered -- a
// reversed limit mirrors that axis, which swaps which face is the near one,
// and that is settled here rather than at the caller.
//
// `depth` comes back as Px3::depth, so a hit on a box compares against a hit
// on a plane -- which is what lets one hover hint order bars and planes
// together. When the eye is *inside* the box the far face answers, since the
// near one is behind it.
inline bool box_ray_hit(const Projector3D& proj,
                        const double lo[3], const double hi[3],
                        float px, float py, float& depth) {
    const Transform3D& tf = proj.transform();
    const Vec3 a = tf.to_box(lo[0], lo[1], lo[2]);
    const Vec3 b = tf.to_box(hi[0], hi[1], hi[2]);
    double b_lo[3], b_hi[3];
    for (int i = 0; i < 3; ++i) {
        const double p = vec3_axis(a, i), q = vec3_axis(b, i);
        b_lo[i] = std::min(p, q);
        b_hi[i] = std::max(p, q);
    }

    const Projector3D::Ray3 r = proj.ray_from_pixel(px, py);
    double t_lo = -std::numeric_limits<double>::max();
    double t_hi =  std::numeric_limits<double>::max();
    for (int i = 0; i < 3; ++i) {
        const double o = vec3_axis(r.origin, i), d = vec3_axis(r.dir, i);
        if (std::fabs(d) < 1e-12) {
            // Parallel to this pair of faces: a miss unless the ray already
            // runs between them. A zero-thickness box (a bar of no height)
            // is legal and lands here, which is why the test is inclusive.
            if (o < b_lo[i] || o > b_hi[i]) return false;
            continue;
        }
        double ta = (b_lo[i] - o) / d, tb = (b_hi[i] - o) / d;
        if (ta > tb) { const double t = ta; ta = tb; tb = t; }
        t_lo = std::max(t_lo, ta);
        t_hi = std::min(t_hi, tb);
        if (t_lo > t_hi) return false;
    }

    // Near face first; the far one answers only when the near one is behind a
    // perspective eye, which is what standing inside the box looks like.
    for (const double t : { t_lo, t_hi }) {
        const Vec3 hit = r.origin + r.dir * t;
        if (!proj.in_front(hit)) continue;
        depth = proj.project_box(hit).depth;
        return true;
    }
    return false;
}

// Where the eye is, as a box-space coordinate on every axis at once: its own
// position under perspective, and a point far along the view direction under
// orthographic, where there is no eye but the *ordering* it induces is the
// same. "Far" only has to beat the box, which is O(1) across.
//
// Here rather than in one of its callers because every "how far away is this
// object" heuristic in the scene has to answer in the *same* quantity -- a
// bar3d plot's and a plane's are compared against each other when the SVG
// writer merges the two plans, and two definitions of distance would interleave
// them by a number that means something different on each side.
inline Vec3 eye_coord(const Projector3D& proj) {
    return proj.has_eye_point() ? proj.eye_point() : proj.eye_dir() * 1e4;
}

// ---------------------------------------------------------------------------
// The box's own depth extent, in Px3::depth -- the range a per-point depth
// cue is normalized over (v1.0 step 12.4).
//
// **The box and not the data**, which is the decision this function exists to
// hold in one place: normalizing a cue over each series' own points would
// shade two clouds in one scene on two different scales, and would have no
// answer at all for a series of one point. The box always has eight corners.
//
// Measured through project_box(), so it is the same number the SVG writer
// sorts on and the same quantity the shader reconstructs below -- rather than
// a second expression of "distance along the view direction".
inline void box_depth_range(const Projector3D& proj, float& dmin, float& dmax) {
    const Vec3 h = proj.transform().half_extent();
    dmin =  std::numeric_limits<float>::max();
    dmax = -std::numeric_limits<float>::max();
    for (int i = 0; i < 8; ++i) {
        const Vec3 c{ (i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z };
        const float d = proj.project_box(c).depth;
        dmin = std::min(dmin, d);
        dmax = std::max(dmax, d);
    }
}

// Px3::depth as an affine function of a box point, for a shader that has the
// point but not the projector: depth(p) = dot(p, dir) + base.
//
// It is exactly affine in both projections -- orthographic depth is
// dot(p - target, fwd) and perspective adds the constant eye distance -- so
// `base` is read off the projector rather than re-derived: it is simply the
// depth of the box origin. That is what keeps this from being a third
// transcription of the depth formula.
inline void depth_affine(const Projector3D& proj, Vec3& dir, float& base) {
    dir  = proj.forward();
    base = proj.project_box(Vec3{ 0.0, 0.0, 0.0 }).depth;
}

// The other half of the depth cue, and its one definition: mix a colour toward
// black by `depthshade * t`, where `t` is 0 at the near face of the box and 1
// at the far one -- exactly what SurfaceOptions::shading does to a cell, keyed
// on distance rather than on a normal. Here rather than beside either kind
// that uses it, because the two numbers it is computed from are here and
// because a second transcription is how two kinds in one scene start shading
// on two scales.
//
// Alpha is untouched. Fading it instead would be matplotlib's depthshade and
// would put every shaded series into the translucent pass -- a cost paid for a
// cue rather than for a picture (see memory/spec_impl.md step 12.4).
inline Color depth_shade(Color c, float depthshade, float t) {
    const float k = 1.0f - std::clamp(depthshade, 0.0f, 1.0f) * std::clamp(t, 0.0f, 1.0f);
    return { c.r * k, c.g * k, c.b * k, c.a };
}

// ---------------------------------------------------------------------------

// -------------------------------------------------------------------------
// Navigation: pure camera-in, camera-out arithmetic
// -------------------------------------------------------------------------
// The 3D counterpart of pan_limits()/zoom_limits(), and here for the same
// reason: kept independent of ImGui so it can be reasoned about, and asserted
// on, without a window or a render loop. The panel supplies the gestures; every
// rule about what a gesture *means* is below.
//
// Unreal Engine conventions, as the plan asks: hold LMB and move to look, WASD
// to move around, Q/E for up and down, scroll to zoom, double-click to reset.

// Degrees of rotation per pixel dragged.
inline constexpr double kOrbitSensitivity = 0.4;
// Box units per second of held key. The box is ~1 unit across whatever the
// data's units are (see BoxAspect), so this is scale-free like every other
// camera number: it crosses the picture in about a second for any dataset.
inline constexpr double kFlySpeed = 1.2;
// Multiplier per wheel notch, and per second of held W/S under an
// orthographic camera where a dolly would be invisible.
inline constexpr double kZoomPerNotch  = 1.1;
inline constexpr double kZoomPerSecond = 2.0;

inline Camera3D clamp_camera(Camera3D cam) {
    cam.elevation = std::clamp(cam.elevation, -89.0, 89.0);
    if (!(cam.zoom > 0.0) || !std::isfinite(cam.zoom)) cam.zoom = 1.0;
    // A camera cannot usefully be flown or zoomed past these, and letting
    // either run away turns a slipped keypress into a picture that has to be
    // reset to recover.
    cam.zoom = std::clamp(cam.zoom, 0.05, 100.0);
    if (!std::isfinite(cam.fov)) cam.fov = 45.0;
    cam.fov = std::clamp(cam.fov, kMinFov, kMaxFov);
    return cam;
}

// Drag to orbit. The content follows the cursor -- dragging right turns the
// box as though it had been grabbed and pulled right -- which is the same
// convention pan_limits() states for 2D, and the reason both signs are
// negated relative to a first-person "look" control.
inline Camera3D orbit_camera(Camera3D cam, float ddx_px, float ddy_px) {
    cam.azimuth   -= static_cast<double>(ddx_px) * kOrbitSensitivity;
    cam.elevation += static_cast<double>(ddy_px) * kOrbitSensitivity;
    return clamp_camera(cam);
}

// One frame of held fly keys. `right` and `forward` are the camera's own basis
// (Projector3D::right()/forward()), so A/D strafes across the screen whatever
// the camera is doing; Q/E is world up, not camera up, because "up" in a plot
// means the z axis and a tilted camera should not tilt what that means.
//
// W/S is the odd one, and it splits by projection mode -- here rather than at
// the call site, so the panel never has to know which projection it is
// driving:
//
//  - *Orthographic*: translating along the view direction changes nothing at
//    all under a parallel projection, so W and S would be dead keys that read
//    as a bug. They drive the zoom instead.
//  - *Perspective*: a real dolly, moving the target (and with it the eye,
//    which sits a derived distance behind it) along the view direction. This
//    is a translation the fit does not undo, because the fit answers to the
//    camera *angle* and never to the target -- the same property that makes
//    A/D a pan rather than a pan-and-shrink. Flying far enough puts the eye
//    inside the box and then through it, which is what the projector's near
//    plane is for.
//
// A dolly is not a zoom and the two are not interchangeable: a dolly changes
// how much perspective there is (the near face grows against the far one),
// while `zoom` magnifies the finished picture and leaves the ratio alone.
struct FlyInput {
    bool forward = false, back = false;
    bool left = false, right = false;
    bool up = false, down = false;
    double dt = 0.0;    // seconds since the last frame
};

inline Camera3D fly_camera(Camera3D cam, Vec3 right, Vec3 forward, const FlyInput& in) {
    const double step = kFlySpeed * in.dt;
    if (in.right) cam.target = cam.target + right * step;
    if (in.left)  cam.target = cam.target - right * step;
    if (in.up)    cam.target.z += step;
    if (in.down)  cam.target.z -= step;

    if (in.forward != in.back) {
        if (cam.projection == Projection::Perspective) {
            cam.target = cam.target + forward * (in.forward ? step : -step);
        } else {
            const double f = std::pow(kZoomPerSecond, in.dt);
            cam.zoom *= in.forward ? f : 1.0 / f;
        }
    }
    return clamp_camera(cam);
}

// Scroll magnifies, in both projection modes.
//
// The design sketch had the wheel drive the field of view under perspective,
// on the reading that a parallel-projection zoom has no perspective analogue.
// It does have one: with the eye distance derived from the fit, `zoom` is a
// magnification of the finished picture and means exactly the same thing
// either way, while fov and the W/S dolly are the controls that change how
// much perspective there is. Putting fov on the wheel would have left zoom
// unreachable by mouse under perspective and made one gesture mean two
// different things depending on a mode the user cannot see in the cursor.
inline Camera3D zoom_camera(Camera3D cam, float wheel) {
    cam.zoom *= std::pow(kZoomPerNotch, static_cast<double>(wheel));
    return clamp_camera(cam);
}

} // namespace sextant
