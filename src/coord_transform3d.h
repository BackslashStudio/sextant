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
#include <optional>
#include <vector>

namespace sextant {
    // -------------------------------------------------------------------------
    // Vec3 arithmetic
    // -------------------------------------------------------------------------
    inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
    inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    inline Vec3 cross(Vec3 a, Vec3 b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }

    inline Vec3 normalize(Vec3 a) {
        const double n = length(a);
        return n > 0.0 ? a * (1.0 / n) : Vec3{0.0, 0.0, 0.0};
    }

    // -------------------------------------------------------------------------
    // Transform3D: data space -> box space
    // -------------------------------------------------------------------------
    // Maps each axis's resolved limits onto the origin-centred box (BoxAspect side
    // lengths). A degenerate axis collapses to the box centre.
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
            return {box_x(x), box_y(y), box_z(z)};
        }

        // Inverse, for hit-testing and placing box-space features on an axis.
        double data_x(double bx) const { return box_to_axis(bx, xmin, xmax, aspect.x); }
        double data_y(double by) const { return box_to_axis(by, ymin, ymax, aspect.y); }
        double data_z(double bz) const { return box_to_axis(bz, zmin, zmax, aspect.z); }

        // Half the side lengths: the coordinate of the +x/+y/+z faces.
        Vec3 half_extent() const { return {aspect.x * 0.5, aspect.y * 0.5, aspect.z * 0.5}; }
    };

    // -------------------------------------------------------------------------
    // auto_scale3d: three padded intervals, the 3D counterpart of auto_scale()
    // -------------------------------------------------------------------------
    // Written per box axis via Axis3Map, so orientations need no branching. Planes
    // reuse the 2D auto_scale() on their sheet.
    struct DataBounds3D {
        double xmin = 0.0, xmax = 1.0;
        double ymin = 0.0, ymax = 1.0;
        double zmin = 0.0, zmax = 1.0;
    };

    // True when a plane has plot objects. An empty plane contributes nothing to
    // limits, not even its offset.
    inline bool plane_has_data(const PlaneSnapshot& p) {
        const AllPlotData all = p.sheet.all();
        return !all.lines.empty() || !all.scatters.empty() || !all.bars.empty()
               || !all.heatmaps.empty() || !all.scatter_z.empty();
    }

    // True when a plane is drawn this frame. Drawing paths check this; layout does
    // not (hiding a plane never changes limits, colorbars or legend).
    inline bool plane_drawn(const PlaneSnapshot& p) {
        return p.opts.visible && plane_has_data(p);
    }

    // `pins` (origin pins, per box axis) are folded in before padding, like data.
    // Heatmaps on planes are not padded along their plane's two axes; see
    // AutoAxis.
    inline DataBounds3D auto_scale3d(const std::vector<Bar3DPlot>& bars,
                                     const std::vector<PlaneSnapshot>& planes,
                                     const std::vector<SurfacePlot>& surfaces,
                                     const std::vector<Scatter3DPlot>& points,
                                     const std::vector<Line3DPlot>& lines,
                                     const std::vector<SurfaceTriPlot>& meshes,
                                     double pad = kAutoScalePad,
                                     const std::array<std::optional<double>, 3>& pins = {}) {
        double lo[3] = {
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()
        };
        double hi[3] = {
            -std::numeric_limits<double>::max(),
            -std::numeric_limits<double>::max(),
            -std::numeric_limits<double>::max()
        };

        for (const Bar3DPlot& b: bars) {
            const Axis3Map m = axis_map(b.orient);
            // The footprint, not just the grid coordinate.
            const double hu = b.u_width * 0.5, hv = b.v_width * 0.5;
            for (std::size_t i = 0; i < b.u.size(); ++i) {
                lo[m.u] = std::min(lo[m.u], b.u[i] - hu);
                hi[m.u] = std::max(hi[m.u], b.u[i] + hu);
            }
            for (std::size_t j = 0; j < b.v.size(); ++j) {
                lo[m.v] = std::min(lo[m.v], b.v[j] - hv);
                hi[m.v] = std::max(hi[m.v], b.v[j] + hv);
            }
            // Base to tip; unlike 2D bars, zero is not forced in.
            for (std::size_t k = 0; k < b.count(); ++k) {
                lo[m.h] = std::min(lo[m.h], b.h_lo(k));
                hi[m.h] = std::max(hi[m.h], b.h_hi(k));
            }
        }

        // A surface's samples are its extent (no footprint); heights don't force zero.
        for (const SurfacePlot& s: surfaces) {
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

        // Scatter points are their extent (pixel-sized markers add nothing). Error
        // bars extend it by their data lengths; pixel-sized parts (caps, boxwidth)
        // are not included since they depend on the box being computed.
        auto grow_err = [&](const CowVec<double>& xs, const CowVec<double>& ys,
                            const CowVec<double>& zs, const ErrorBar3DData& err) {
            if (err.empty()) return;
            const CowVec<double>* ps[3] = {&xs, &ys, &zs};
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

        for (const Scatter3DPlot& s: points) {
            grow_err(s.x, s.y, s.z, s.err);
            for (std::size_t i = 0; i < s.count(); ++i) {
                lo[0] = std::min(lo[0], s.x[i]);
                hi[0] = std::max(hi[0], s.x[i]);
                lo[1] = std::min(lo[1], s.y[i]);
                hi[1] = std::max(hi[1], s.y[i]);
                lo[2] = std::min(lo[2], s.z[i]);
                hi[2] = std::max(hi[2], s.z[i]);
            }
        }

        // A path's points are its extent. Its width depends on the box, so it is
        // not included.
        for (const Line3DPlot& l: lines) {
            grow_err(l.x, l.y, l.z, l.err);
            for (std::size_t i = 0; i < l.count(); ++i) {
                lo[0] = std::min(lo[0], l.x[i]);
                hi[0] = std::max(hi[0], l.x[i]);
                lo[1] = std::min(lo[1], l.y[i]);
                hi[1] = std::max(hi[1], l.y[i]);
                lo[2] = std::min(lo[2], l.z[i]);
                hi[2] = std::max(hi[2], l.z[i]);
            }
        }

        // A mesh's vertices are its extent, including ones no triangle uses.
        for (const SurfaceTriPlot& m: meshes) {
            for (std::size_t i = 0; i < m.count(); ++i) {
                lo[0] = std::min(lo[0], m.x[i]);
                hi[0] = std::max(hi[0], m.x[i]);
                lo[1] = std::min(lo[1], m.y[i]);
                hi[1] = std::max(hi[1], m.y[i]);
                lo[2] = std::min(lo[2], m.z[i]);
                hi[2] = std::max(hi[2], m.z[i]);
            }
        }

        AutoAxis axes[3];
        for (int a = 0; a < 3; ++a) axes[a].loose = {lo[a], hi[a]};

        // A plane's 2D extent lands on the two axes it spans (its heatmaps
        // unpadded, the rest padded) and its offset on the third. The final
        // pass pads once.
        for (const PlaneSnapshot& p: planes) {
            if (!plane_has_data(p)) continue;
            const Axis3Map m = axis_map(p.orient);
            const AutoBounds b = auto_bounds(p.sheet.all());
            axes[m.u].loose.add(b.x.loose);
            axes[m.u].tight.add(b.x.tight);
            axes[m.v].loose.add(b.y.loose);
            axes[m.v].tight.add(b.y.tight);
            axes[m.h].loose.add(p.offset);
        }

        DataBounds3D out;
        double* omin[3] = {&out.xmin, &out.ymin, &out.zmin};
        double* omax[3] = {&out.xmax, &out.ymax, &out.zmax};
        for (int a = 0; a < 3; ++a) {
            // Settled first (a single plane of bars spans +-0.5), so a pin
            // widens a usable range.
            settle_axis(axes[a]);
            if (pins[a]) axes[a].loose.add(*pins[a]);
            pad_axis(axes[a], pad, *omin[a], *omax[a]);
        }
        return out;
    }

    // -------------------------------------------------------------------------
    // Projector3D: box space -> pixels
    // -------------------------------------------------------------------------
    // Shared by the CPU annotation paths and the GPU data pass, so annotation sits
    // exactly on the geometry.
    struct Px3 {
        float x = 0.0f, y = 0.0f; // pixels, in the figure's own coordinates
        // Distance from the camera along the view direction, in box units (larger
        // is further). Not normalized; used for sorting.
        float depth = 0.0f;
        // Perspective divisor (eye-space depth); 1 under orthographic. w <= 0 means
        // behind the eye, where x/y are meaningless -- CPU consumers must clip at
        // the near plane (see the helpers below).
        float w = 1.0f;

        bool in_front() const { return w > 0.0f; }
    };

    // Allowed perspective field of view, degrees.
    inline constexpr double kMinFov = 5.0;
    inline constexpr double kMaxFov = 120.0;

    // Orthographic: rotation, uniform scale and y flip. Perspective adds the divide,
    // with the eye distance derived in the constructor.
    class Projector3D {
    public:
        Projector3D() = default;

        // `margin`: fraction of the frame left empty on each side for annotation.
        Projector3D(const Transform3D& tf, const Camera3D& cam,
                    const PlotRect& frame, float margin)
            : tf_(tf), frame_(frame), target_(cam.target) {
            const double az = cam.azimuth * kDeg2Rad;
            const double el = std::clamp(cam.elevation, -89.0, 89.0) * kDeg2Rad;

            // From the target toward the eye; z is up.
            eye_dir_ = {
                std::cos(el) * std::cos(az),
                std::cos(el) * std::sin(az),
                std::sin(el)
            };
            fwd_ = eye_dir_ * -1.0;
            right_ = normalize(cross(fwd_, Vec3{0.0, 0.0, 1.0}));
            up_ = cross(right_, fwd_);

            const float m = std::clamp(margin, 0.0f, 0.45f);
            const double aw = std::max(1.0, static_cast<double>(frame.w) * (1.0 - 2.0 * m));
            const double ah = std::max(1.0, static_cast<double>(frame.h) * (1.0 - 2.0 * m));
            zoom_ = (cam.zoom > 0.0 && std::isfinite(cam.zoom)) ? cam.zoom : 1.0;

            // Fit: the box's projected silhouette is sized to fill the frame at any
            // angle; `zoom` then magnifies the result. Measured about the box
            // centre, not the target, so panning doesn't shrink the picture and the
            // perspective dolly isn't undone by the fit.
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
            // Half-angles: vertical from fov, horizontal from the frame's aspect.
            // fov is clamped here too so a directly built Camera3D can't yield NaN.
            const double fov = std::isfinite(cam.fov)
                                   ? std::clamp(cam.fov, kMinFov, kMaxFov)
                                   : 45.0;
            const double tv = std::tan(fov * 0.5 * kDeg2Rad);
            const double tu = tv * aw / ah;
            s_ = ah * 0.5 / tv; // pixels per unit of (lateral / eye depth)

            // Eye distance: the smallest that keeps all eight corners inside the
            // frame (one closed-form bound per corner).
            double d = 0.0;
            for (int i = 0; i < 8; ++i) {
                const Vec3 c = corner(h, i);
                const double t = dot(c, fwd_);
                d = std::max(d, std::max(std::fabs(dot(c, right_)) / tu,
                                         std::fabs(dot(c, up_)) / tv) - t);
            }
            // A fully degenerate box leaves d at 0; avoid dividing by zero.
            dist_ = d > 0.0 ? d : 1.0;
            near_ = dist_ * kNearFraction;
            set_depth_range(h);
        }

        // Eye-space depth along the view direction (Px3::w); 1 under orthographic,
        // which makes every clip a no-op.
        double eye_depth(Vec3 p) const {
            return persp_ ? dot(p - target_, fwd_) + dist_ : 1.0;
        }

        Px3 project_box(Vec3 p) const {
            const Vec3 d = p - target_;
            const double u = dot(d, right_), v = dot(d, up_), t = dot(d, fwd_);
            if (!persp_)
                return {
                    static_cast<float>(cx() + u * k_),
                    // Pixel y grows downward, the box's up does not.
                    static_cast<float>(cy() - v * k_),
                    static_cast<float>(t), 1.0f
                };

            const double z = t + dist_;
            // Behind the eye x/y are meaningless; the divisor is floored only to
            // keep them finite. Use the clipping helpers.
            const double zz = std::fabs(z) > near_ ? z : (z < 0.0 ? -near_ : near_);
            const double f = s_ * zoom_ / zz;
            return {
                static_cast<float>(cx() + u * f),
                static_cast<float>(cy() - v * f),
                static_cast<float>(z), static_cast<float>(z)
            };
        }

        Px3 project(double x, double y, double z) const {
            return project_box(tf_.to_box(x, y, z));
        }

        // ---- Near-plane clipping ------------------------------------------
        // A segment with an end behind the eye projects the wrong way across the
        // figure, so CPU consumers use these instead of projecting endpoints.
        // Pass-throughs under orthographic.

        bool in_front(Vec3 p) const { return eye_depth(p) > near_; }

        // Projects `a`-`b`, cut at the near plane. False if entirely behind the eye.
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

        // Sutherland-Hodgman against the near plane. `out` is empty if the polygon
        // is entirely behind the eye.
        void project_polygon(const std::vector<Vec3>& pts, std::vector<Px3>& out) const {
            out.clear();
            if (pts.empty()) return;
            if (!persp_) {
                out.reserve(pts.size());
                for (const Vec3& p: pts) out.push_back(project_box(p));
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

        // ---- Inverse: pixel -> box-space ray ------------------------------
        // `dir` is scaled so the ray parameter is eye depth (perspective) or view
        // distance (orthographic).
        struct Ray3 {
            Vec3 origin, dir;
        };

        Ray3 ray_from_pixel(float px, float py) const {
            const double dx = static_cast<double>(px) - cx();
            const double dy = cy() - static_cast<double>(py); // pixel y grows down
            if (!persp_) {
                const double s = k_ > 0.0 ? 1.0 / k_ : 0.0;
                return {target_ + right_ * (dx * s) + up_ * (dy * s), fwd_};
            }
            const double s = (s_ * zoom_) > 0.0 ? 1.0 / (s_ * zoom_) : 0.0;
            return {eye_point(), fwd_ + right_ * (dx * s) + up_ * (dy * s)};
        }

        // Direction from the box toward the camera.
        Vec3 eye_dir() const { return eye_dir_; }
        Vec3 forward() const { return fwd_; }
        Vec3 right() const { return right_; }
        Vec3 up() const { return up_; }

        // The eye position, box space; perspective only.
        bool has_eye_point() const { return persp_; }
        Vec3 eye_point() const { return target_ + eye_dir_ * dist_; }

        // Whether a face with outward normal `n` through `p` faces the camera.
        bool faces_camera(Vec3 p, Vec3 n) const {
            return dot(n, persp_ ? eye_point() - p : eye_dir_) > 0.0;
        }

        // ---- The same projection as a 4x4, for the GPU ---------------------
        // Column-major, box space -> clip space, derived from the same basis and
        // fit as project_box() (tests assert they agree). Data -> box is
        // box_affine(), applied first in the shader. Depth is linear (ortho) or
        // hyperbolic (perspective) over a generous range: the box plus four radii.
        std::array<float, 16> clip_matrix(double win_w, double win_h) const {
            const double W = win_w > 0.0 ? win_w : 1.0;
            const double H = win_h > 0.0 ? win_h : 1.0;

            // Basis, and the box origin relative to the target.
            const Vec3 q0{-target_.x, -target_.y, -target_.z};
            const double u0 = dot(q0, right_), v0 = dot(q0, up_), t0 = dot(q0, fwd_);

            const double ox = 2.0 * cx() / W - 1.0; // where the frame centre is in NDC
            const double oy = 1.0 - 2.0 * cy() / H;
            const double n = depth_near_, f = depth_far_;
            const double idz = (f - n) != 0.0 ? 1.0 / (f - n) : 1.0;

            double row[4][4];
            if (!persp_) {
                const double sx = 2.0 * k_ / W, sy = 2.0 * k_ / H;
                set_row(row[0], right_ * sx, u0 * sx + ox);
                set_row(row[1], up_ * sy, v0 * sy + oy);
                set_row(row[2], fwd_ * (2.0 * idz), t0 * 2.0 * idz - 2.0 * n * idz - 1.0);
                set_row(row[3], Vec3{0.0, 0.0, 0.0}, 1.0);
            } else {
                const double sx = 2.0 * s_ * zoom_ / W, sy = 2.0 * s_ * zoom_ / H;
                const double D = t0 + dist_; // eye depth at the box origin
                const double za = (f + n) * idz, zb = -2.0 * f * n * idz;
                set_row(row[0], right_ * sx + fwd_ * ox, u0 * sx + D * ox);
                set_row(row[1], up_ * sy + fwd_ * oy, v0 * sy + D * oy);
                set_row(row[2], fwd_ * za, D * za + zb);
                set_row(row[3], fwd_, D);
            }

            // Row-major above; GL wants column-major.
            std::array < float, 16 > m{};
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    m[static_cast<std::size_t>(c * 4 + r)] = static_cast<float>(row[r][c]);
            return m;
        }

        // The data -> box step as a shader-side scale/offset:
        //
        //     box = scale * (data - anchor_in_the_buffer) + offset
        //
        // Anchored buffers keep float precision relative to the data's span.
        // Computed in double, then narrowed.
        void box_affine(Vec3 anchor, float scale[3], float offset[3]) const {
            double A[3], b[3];
            axis_affine(tf_.xmin, tf_.xmax, tf_.aspect.x, A[0], b[0]);
            axis_affine(tf_.ymin, tf_.ymax, tf_.aspect.y, A[1], b[1]);
            axis_affine(tf_.zmin, tf_.zmax, tf_.aspect.z, A[2], b[2]);
            const double a[3] = {anchor.x, anchor.y, anchor.z};
            for (int i = 0; i < 3; ++i) {
                scale[i] = static_cast<float>(A[i]);
                offset[i] = static_cast<float>(A[i] * a[i] + b[i]);
            }
        }

        // Box units per logical pixel at `p` (depth-dependent under perspective).
        // Used to turn pixel widths into fixed scene widths.
        double box_units_per_pixel(Vec3 p) const {
            if (!persp_) return k_ > 0.0 ? 1.0 / k_ : 0.0;
            const double z = eye_depth(p);
            const double denom = s_ * zoom_;
            return denom > 0.0 ? std::max(z, near_) / denom : 0.0;
        }

        bool is_perspective() const { return persp_; }
        // Derived eye distance in box units; 0 under orthographic.
        double eye_distance() const { return persp_ ? dist_ : 0.0; }

        // Orthographic pixels per box unit; 0 under perspective.
        double pixels_per_box_unit() const { return persp_ ? 0.0 : k_; }

        const Transform3D& transform() const { return tf_; }
        const PlotRect& frame() const { return frame_; }

    private:
        static constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
        // Points nearer than this fraction of the eye distance are clipped.
        static constexpr double kNearFraction = 1e-3;

        static Vec3 corner(Vec3 h, int i) {
            return {(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z};
        }

        static Vec3 lerp(Vec3 a, Vec3 b, double t) { return a + (b - a) * t; }

        // One axis of the data -> box map, as scale and offset.
        static void axis_affine(double lo, double hi, double side, double& a, double& b) {
            const double span = hi - lo;
            if (span == 0.0) {
                a = 0.0;
                b = 0.0;
                return;
            } // matches Transform3D
            a = side / span;
            b = -(lo * side / span + 0.5 * side);
        }

        static void set_row(double* r, Vec3 v, double w) {
            r[0] = v.x;
            r[1] = v.y;
            r[2] = v.z;
            r[3] = w;
        }

        // Depth range the GPU matrix maps to [-1, 1]: the box plus four radii of
        // headroom, so data outside the limits isn't clipped.
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
                depth_near_ = near_; // the plane the CPU clips at
                depth_far_ = tmax + dist_ + pad;
                if (depth_far_ <= depth_near_) depth_far_ = depth_near_ * 1000.0;
            } else {
                depth_near_ = tmin - pad;
                depth_far_ = tmax + pad;
            }
        }

        float cx() const { return frame_.x + frame_.w * 0.5f; }
        float cy() const { return frame_.y + frame_.h * 0.5f; }

        Transform3D tf_{};
        PlotRect frame_{0.0f, 0.0f, 1.0f, 1.0f};
        Vec3 target_{};
        Vec3 eye_dir_{1.0, 0.0, 0.0}; // box -> camera
        Vec3 fwd_{-1.0, 0.0, 0.0}; // camera -> box
        Vec3 right_{0.0, 1.0, 0.0};
        Vec3 up_{0.0, 0.0, 1.0};
        double zoom_ = 1.0;
        bool persp_ = false;
        double k_ = 1.0; // orthographic: pixels per box unit
        double s_ = 1.0; // perspective: pixels per unit of lateral/depth
        double dist_ = 1.0; // perspective: derived eye distance
        double near_ = 1e-3; // perspective: the near plane, in eye depth
        double depth_near_ = 0.0, depth_far_ = 1.0; // what clip_matrix() maps to [-1, 1]
    };

    // A point on an axis-aligned plane from its in-plane coordinates.
    inline Vec3 plane_point(PlaneOrientation orient, double u, double v, double offset) {
        const Axis3Map m = axis_map(orient);
        double c[3];
        c[m.u] = u;
        c[m.v] = v;
        c[m.h] = offset;
        return {c[0], c[1], c[2]};
    }

    // Vec3 component by box-axis index.
    inline double vec3_axis(Vec3 v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }

    // Where a pixel's ray meets an axis-aligned plane, as the plane's in-plane
    // (parent data) coordinates. Exact: one divide. False when edge-on or behind a
    // perspective eye. `depth` is Px3::depth, comparable across planes.
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
        const double data[3] = {tf.data_x(hit.x), tf.data_y(hit.y), tf.data_z(hit.z)};
        u = data[m.u];
        v = data[m.v];
        depth = proj.project_box(hit).depth;
        return true;
    }

    // Where a pixel's ray enters an axis-aligned data-space box (three slab tests).
    // `lo`/`hi` are per axis and need not be ordered. `depth` is Px3::depth,
    // comparable with plane_ray_hit(); with the eye inside the box, the far face
    // answers.
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
        double t_hi = std::numeric_limits<double>::max();
        for (int i = 0; i < 3; ++i) {
            const double o = vec3_axis(r.origin, i), d = vec3_axis(r.dir, i);
            if (std::fabs(d) < 1e-12) {
                // Parallel to this slab: miss unless inside it (inclusive, so a
                // zero-height bar can still be hit).
                if (o < b_lo[i] || o > b_hi[i]) return false;
                continue;
            }
            double ta = (b_lo[i] - o) / d, tb = (b_hi[i] - o) / d;
            if (ta > tb) {
                const double t = ta;
                ta = tb;
                tb = t;
            }
            t_lo = std::max(t_lo, ta);
            t_hi = std::min(t_hi, tb);
            if (t_lo > t_hi) return false;
        }

        // Near face first; the far one only if the near one is behind the eye.
        for (const double t: {t_lo, t_hi}) {
            const Vec3 hit = r.origin + r.dir * t;
            if (!proj.in_front(hit)) continue;
            depth = proj.project_box(hit).depth;
            return true;
        }
        return false;
    }

    // The eye position in box space; under orthographic, a far point along the
    // view direction (same ordering). The shared distance for depth heuristics.
    inline Vec3 eye_coord(const Projector3D& proj) {
        return proj.has_eye_point() ? proj.eye_point() : proj.eye_dir() * 1e4;
    }

    // ---------------------------------------------------------------------------
    // The box's own depth extent (via project_box()), used to normalize depth cues.
    // The box, not the data, so every series shades on one scale.
    inline void box_depth_range(const Projector3D& proj, float& dmin, float& dmax) {
        const Vec3 h = proj.transform().half_extent();
        dmin = std::numeric_limits<float>::max();
        dmax = -std::numeric_limits<float>::max();
        for (int i = 0; i < 8; ++i) {
            const Vec3 c{(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z};
            const float d = proj.project_box(c).depth;
            dmin = std::min(dmin, d);
            dmax = std::max(dmax, d);
        }
    }

    // Px3::depth as an affine function of a box point, for shaders:
    // depth(p) = dot(p, dir) + base.
    inline void depth_affine(const Projector3D& proj, Vec3& dir, float& base) {
        dir = proj.forward();
        base = proj.project_box(Vec3{0.0, 0.0, 0.0}).depth;
    }

    // Darken toward black by `depthshade * t` (t: 0 at the box's near face, 1 at
    // the far face). Alpha is untouched so shaded series stay opaque.
    inline Color depth_shade(Color c, float depthshade, float t) {
        const float k = 1.0f - std::clamp(depthshade, 0.0f, 1.0f) * std::clamp(t, 0.0f, 1.0f);
        return {c.r * k, c.g * k, c.b * k, c.a};
    }

    // -------------------------------------------------------------------------
    // Navigation: pure camera-in, camera-out arithmetic (window-free, testable).
    // Unreal-style: LMB drag to look, WASD to move, Q/E up/down, scroll to zoom,
    // double-click to reset.
    // -------------------------------------------------------------------------
    // Degrees of rotation per pixel dragged.
    inline constexpr double kOrbitSensitivity = 0.4;
    // Box units per second of held key (the box is ~1 unit across).
    inline constexpr double kFlySpeed = 1.2;
    // Zoom factor per wheel notch, and per second of held W/S under orthographic.
    inline constexpr double kZoomPerNotch = 1.1;
    inline constexpr double kZoomPerSecond = 2.0;

    inline Camera3D clamp_camera(Camera3D cam) {
        cam.elevation = std::clamp(cam.elevation, -89.0, 89.0);
        if (!(cam.zoom > 0.0) || !std::isfinite(cam.zoom)) cam.zoom = 1.0;
        // Keep zoom bounded so a slip doesn't need a reset to recover.
        cam.zoom = std::clamp(cam.zoom, 0.05, 100.0);
        if (!std::isfinite(cam.fov)) cam.fov = 45.0;
        cam.fov = std::clamp(cam.fov, kMinFov, kMaxFov);
        return cam;
    }

    // Drag to orbit; the content follows the cursor.
    inline Camera3D orbit_camera(Camera3D cam, float ddx_px, float ddy_px) {
        cam.azimuth -= static_cast<double>(ddx_px) * kOrbitSensitivity;
        cam.elevation += static_cast<double>(ddy_px) * kOrbitSensitivity;
        return clamp_camera(cam);
    }

    // One frame of held fly keys. A/D strafe along the camera's `right`; Q/E move
    // along world z. W/S dolly the target along `forward` under perspective, and
    // zoom under orthographic (where a dolly would be invisible).
    struct FlyInput {
        bool forward = false, back = false;
        bool left = false, right = false;
        bool up = false, down = false;
        double dt = 0.0; // seconds since the last frame
    };

    inline Camera3D fly_camera(Camera3D cam, Vec3 right, Vec3 forward, const FlyInput& in) {
        const double step = kFlySpeed * in.dt;
        if (in.right) cam.target = cam.target + right * step;
        if (in.left) cam.target = cam.target - right * step;
        if (in.up) cam.target.z += step;
        if (in.down) cam.target.z -= step;

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
    inline Camera3D zoom_camera(Camera3D cam, float wheel) {
        cam.zoom *= std::pow(kZoomPerNotch, static_cast<double>(wheel));
        return clamp_camera(cam);
    }
} // namespace sextant
