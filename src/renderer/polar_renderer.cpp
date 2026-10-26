#include "polar_renderer.h"
#include "data_gl.h"
#include "marker_shape.h"
#include "../color_norm.h"
#include "../colormaps.h"
#include "../fill_geometry.h"
#include "../line_dash.h"
#include "../shape_geometry.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace sextant {
    using namespace data_gl;

    namespace {
        constexpr double kTwoPi = 2.0 * std::numbers::pi;

        // The map from (θ, r - r_ref) to logical pixels, pasted into each vertex
        // shader: rho the r fraction (0 at rmin, 1 at rmax), the angle
        // uPolarOffset + uPolarDir θ counter-clockwise on screen, uPolarScale
        // the unit disc's pixels (y negative: pixels run down).
        constexpr char k_polar_map[] = R"(
uniform float uPolarOffset;
uniform float uPolarDir;
uniform float uPolarRShift;   // r_ref - rmin
uniform float uPolarRSpan;    // rmax - rmin
uniform vec2  uPolarCentre;
uniform vec2  uPolarScale;
float polar_rho(vec2 p) { return (p.y + uPolarRShift) / uPolarRSpan; }
vec2 polar_px(vec2 p, float rho) {
    float a = uPolarOffset + uPolarDir * p.x;
    return uPolarCentre + uPolarScale * (rho * vec2(cos(a), sin(a)));
}
)";

        // The frame, per pixel: outside the rim, or outside the sector's
        // screen angles [uPolarStart, uPolarStart + uPolarSweep] (radians
        // counter-clockwise from +x; a sweep of 2 pi or more is the disc).
        constexpr char k_polar_frame_clip[] = R"(
uniform vec2  uPolarCentre;
uniform vec2  uPolarScale;
uniform float uPolarStart;
uniform float uPolarSweep;
bool polar_clipped(vec2 px) {
    vec2 u = (px - uPolarCentre) / uPolarScale;
    if (dot(u, u) > 1.0) return true;
    if (uPolarSweep >= 6.2831853) return false;
    return mod(atan(u.y, u.x) - uPolarStart, 6.2831853) > uPolarSweep;
}
)";

        // Strokes: the shared line shader with the polar map; vRho (signed,
        // linear along the segment) cuts the stroke where r passes rmin.
        std::string line_px() {
            return std::string(k_polar_map) + R"(
out float vRho;
out vec2 vPx;
vec2 to_px(vec2 p) { return polar_px(p, polar_rho(p)); }
void stroke_out(vec2 px, float t) {
    vRho = mix(polar_rho(aP0), polar_rho(aP1), t);
    vPx = px;
}
)";
        }

        std::string line_clip() {
            return std::string("in float vRho;\nin vec2 vPx;\n") + k_polar_frame_clip
                   + "bool stroke_clipped() { return vRho < -1e-6 || polar_clipped(vPx); }\n";
        }

        // Areas: rho clamped at 0, which is the cut at rmin (r = rmin maps to
        // the centre); rim and sector cut per pixel.
        constexpr char k_area_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aPos;   // (θ, r - r_ref)
uniform vec2 uResolution;
POLAR_MAP
out vec2 vPx;
void main() {
    vec2 px = polar_px(aPos, max(polar_rho(aPos), 0.0));
    vPx = px;
    vec2 ndc = (px / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

        constexpr char k_area_frag[] = R"(
#version 410 core
in vec2 vPx;
uniform vec4 uColor;
out vec4 FragColor;
POLAR_CLIP
void main() {
    if (polar_clipped(vPx)) discard;
    FragColor = uColor;
}
)";

        // Markers: the centre mapped here, a point whose centre doesn't show
        // (r outside the limits, outside the sector; polar_point_shown())
        // culled; the marker's area clipped per pixel.
        constexpr char k_marker_vert_head[] = R"(
#version 410 core
layout(location = 0) in vec2 aQuadPos;   // unit quad [-1,1]x[-1,1]
layout(location = 1) in vec2 aCenter;    // instance: (θ, r - r_ref)
layout(location = 2) in float aSize;     // instance: pixel diameter
uniform vec2 uResolution;
uniform float uPolarStart;
uniform float uPolarSweep;
POLAR_MAP
out vec2 vUV;
out float vHalf;
out vec2 vPx;
bool centre_shown(float rho) {
    if (rho < -1e-5 || rho > 1.0 + 1e-5) return false;
    if (rho <= 1e-6 || uPolarSweep >= 6.2831853) return true;
    float d = mod(uPolarOffset + uPolarDir * aCenter.x - uPolarStart, 6.2831853);
    return d <= uPolarSweep + 1e-5 || d >= 6.2831853 - 1e-5;
}
void place() {
    float rho = polar_rho(aCenter);
    vec2 px = polar_px(aCenter, rho) + aQuadPos * (aSize * 0.5);
    vPx = px;
    vUV = aQuadPos;
    vHalf = aSize * 0.5;
    vec2 ndc = (px / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = centre_shown(rho) ? vec4(ndc, 0.0, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}
)";

        std::string marker_vert(bool colored) {
            std::string s = expand_shader(k_marker_vert_head, "POLAR_MAP", k_polar_map);
            if (colored) s += "layout(location = 3) in vec4 aColor;\nout vec4 vColor;\n"
                              "void main() { place(); vColor = aColor; }\n";
            else s += "void main() { place(); }\n";
            return s;
        }

        // θ to [-π, π) and r about the object's r_ref, in double, then float.
        void put_point(std::vector<float>& out, double t, double r, double r_ref) {
            const double w = t - kTwoPi * std::floor((t + std::numbers::pi) / kTwoPi);
            out.push_back(static_cast<float>(w));
            out.push_back(static_cast<float>(r - r_ref));
        }

        // The middle of the finite values' range (0 for none): the r
        // subtracted before float, so an r far from 0 keeps its digits.
        template <class It>
        double mid_of(It first, It last) {
            double lo = std::numeric_limits<double>::infinity(), hi = -lo;
            for (; first != last; ++first)
                if (std::isfinite(*first)) {
                    lo = std::min(lo, *first);
                    hi = std::max(hi, *first);
                }
            return lo <= hi ? 0.5 * (lo + hi) : 0.0;
        }

        double strokes_mid(const std::vector<PolarStroke>& s) {
            double lo = std::numeric_limits<double>::infinity(), hi = -lo;
            for (const PolarStroke& k: s)
                for (const double r: k.r) {
                    lo = std::min(lo, r);
                    hi = std::max(hi, r);
                }
            return lo <= hi ? 0.5 * (lo + hi) : 0.0;
        }

        void upload(unsigned int& vbo, const void* data, std::size_t bytes) {
            if (vbo == 0) glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), data, GL_STATIC_DRAW);
        }
    } // namespace

    struct PolarRenderer::View {
        const PolarMap* m = nullptr;
        float res[2] = {1.0f, 1.0f};
        float centre[2] = {0.0f, 0.0f};
        float scale[2] = {1.0f, -1.0f};
        float offset = 0.0f, dir = 1.0f, start = 0.0f, sweep = 0.0f;
        double rmin = 0.0, rmax = 1.0;
        float radius = 1.0f;   // px per unit, for the dash lengths
        PlotRect pr{};
        float win_h = 0.0f;
    };

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------
    PolarRenderer::Uniforms PolarRenderer::locate(unsigned int p) {
        const auto loc = [p](const char* n) { return glGetUniformLocation(p, n); };
        Uniforms u;
        u.resolution = loc("uResolution");
        u.offset = loc("uPolarOffset");
        u.dir = loc("uPolarDir");
        u.rshift = loc("uPolarRShift");
        u.rspan = loc("uPolarRSpan");
        u.centre = loc("uPolarCentre");
        u.scale = loc("uPolarScale");
        u.start = loc("uPolarStart");
        u.sweep = loc("uPolarSweep");
        u.color = loc("uColor");
        u.half_width = loc("uHalfWidth");
        u.dash = loc("uDash");
        u.dash_period = loc("uDashPeriod");
        u.dist_scale = loc("uDistScale");
        u.edge = loc("uEdge");
        u.edge_w = loc("uEdgeW");
        u.edge_face = loc("uEdgeFace");
        u.marker = loc("uMarker");
        return u;
    }

    PolarRenderer::PolarRenderer() {
        const std::string lv = expand_shader(k_lineseg_vert, "TO_PX", line_px().c_str());
        const std::string lf = expand_shader(k_lineseg_frag, "STROKE_CLIP", line_clip().c_str());
        line_program_ = build_program(lv.c_str(), lf.c_str());
        line_u_ = locate(line_program_);
        glGenVertexArrays(1, &line_vao_);
        glBindVertexArray(line_vao_);
        glGenBuffers(1, &corner_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, corner_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(k_lineseg_corners), k_lineseg_corners, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

        const std::string av = expand_shader(k_area_vert, "POLAR_MAP", k_polar_map);
        const std::string af = expand_shader(k_area_frag, "POLAR_CLIP", k_polar_frame_clip);
        area_program_ = build_program(av.c_str(), af.c_str());
        area_u_ = locate(area_program_);
        glGenVertexArrays(1, &area_vao_);

        static const float quad[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
        glGenBuffers(1, &quad_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        const auto marker = [&](bool colored, const char* frag, unsigned int& prog, unsigned int& vao,
                                Uniforms& u) {
            const std::string v = marker_vert(colored);
            const std::string f = marker_frag(frag, k_polar_frame_clip);
            prog = build_program(v.c_str(), f.c_str());
            u = locate(prog);
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        };
        marker(false, k_scatter_frag, marker_program_, marker_vao_, marker_u_);
        marker(true, k_scatterz_frag, markerz_program_, markerz_vao_, markerz_u_);
        glBindVertexArray(0);
    }

    void PolarRenderer::release(Entry& e) {
        for (unsigned int* b: {&e.strokes.vbo, &e.strokes.dist_vbo, &e.area.vbo, &e.markers.vbo})
            if (*b) {
                glDeleteBuffers(1, b);
                *b = 0;
            }
    }

    PolarRenderer::~PolarRenderer() {
        for (auto& [id, e]: cache_) release(e);
        for (unsigned int p: {line_program_, area_program_, marker_program_, markerz_program_})
            if (p) glDeleteProgram(p);
        for (unsigned int v: {line_vao_, area_vao_, marker_vao_, markerz_vao_})
            if (v) glDeleteVertexArrays(1, &v);
        for (unsigned int b: {corner_vbo_, quad_vbo_})
            if (b) glDeleteBuffers(1, &b);
    }

    // -------------------------------------------------------------------------
    // Frame
    // -------------------------------------------------------------------------
    PolarRenderer::Entry& PolarRenderer::entry(ObjectId id, std::vector<double> key, bool& fresh) {
        Entry& e = cache_[id];
        fresh = e.frame == 0 || e.key != key;
        if (fresh) {
            release(e);
            e = Entry{};
            e.key = std::move(key);
        }
        e.frame = frame_;
        return e;
    }

    void PolarRenderer::end_frame() {
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->second.frame != frame_) {
                release(it->second);
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
        ++frame_;
    }

    // As DataRenderer::begin_pass(): scissor to the plot rect, straight-alpha
    // blending.
    void PolarRenderer::begin_pass(const PlotRect& pr, float win_h) const {
        const float s = pixel_ratio_;
        const float x0 = pr.x * s;
        const float y0 = (win_h - pr.y - pr.h) * s;
        const float x1 = (pr.x + pr.w) * s;
        const float y1 = (win_h - pr.y) * s;
        glEnable(GL_SCISSOR_TEST);
        glScissor(static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0)),
                  static_cast<int>(std::ceil(x1) - std::floor(x0)),
                  static_cast<int>(std::ceil(y1) - std::floor(y0)));
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
    }

    void PolarRenderer::set_view(const Uniforms& u, const View& v, double r_ref) const {
        glUniform2fv(u.resolution, 1, v.res);
        glUniform1f(u.offset, v.offset);
        glUniform1f(u.dir, v.dir);
        glUniform1f(u.rshift, static_cast<float>(r_ref - v.rmin));
        glUniform1f(u.rspan, static_cast<float>(v.rmax - v.rmin));
        glUniform2fv(u.centre, 1, v.centre);
        glUniform2fv(u.scale, 1, v.scale);
        glUniform1f(u.start, v.start);
        glUniform1f(u.sweep, v.sweep);
    }

    void PolarRenderer::draw(const RenderSnapshotPolar& snap, const PolarMap& m, const CoordTransform& tr,
                             const PlotRect& pr) {
        View v;
        v.m = &m;
        v.res[0] = tr.win_w;
        v.res[1] = tr.win_h;
        v.centre[0] = tr.to_px(0.0);
        v.centre[1] = tr.to_py(0.0);
        v.scale[0] = tr.to_px(1.0) - v.centre[0];
        v.scale[1] = tr.to_py(1.0) - v.centre[1];
        v.radius = std::fabs(v.scale[0]);
        v.offset = static_cast<float>(std::fmod(m.offset, kTwoPi));
        v.dir = static_cast<float>(m.dir);
        double start = std::min(m.angle(m.tmin), m.angle(m.tmax));
        start -= kTwoPi * std::floor(start / kTwoPi);
        v.start = static_cast<float>(start);
        v.sweep = m.full() ? static_cast<float>(kTwoPi) : static_cast<float>(m.tmax - m.tmin);
        v.rmin = m.rmin;
        v.rmax = m.rmax;
        v.pr = pr;
        v.win_h = tr.win_h;
        if (!(v.radius > 0.0f) || m.rmax == m.rmin) return;

        const RenderSnapshot& s = snap.objects;
        begin_pass(pr, tr.win_h);
        for (const ObjectRef& r: draw_order(s)) {
            switch (r.kind) {
                case PlotKind::Line:     draw_line(s.lines[r.index], v); break;
                case PlotKind::Scatter:  draw_scatter(s.scatters[r.index], v); break;
                case PlotKind::ScatterZ: draw_scatter_z(s.scatter_z[r.index], v); break;
                case PlotKind::Bar:      draw_bar(s.bars[r.index], v); break;
                case PlotKind::Fill:     draw_fill(s.fills[r.index], v); break;
                case PlotKind::Poly:     draw_poly(s.polys[r.index], v); break;
                default: break;   // no other kind on a polar axes
            }
        }
        glBindVertexArray(0);
        glDisable(GL_SCISSOR_TEST);
    }

    // -------------------------------------------------------------------------
    // Strokes
    // -------------------------------------------------------------------------
    // Each stroke padded as DataRenderer's rings are, so instance i reads
    // points [i, i + 3]: a closed one [p(m-1), p0 .. p(m-1), p0, p1], an open
    // one [p0, p0 .. p(m-1), p(m-1)]. Strokes are drawn one by one (a run
    // joined to the next would bridge them).
    void PolarRenderer::build_strokes(Strokes& s, std::vector<PolarStroke> src) {
        s.runs.clear();
        s.r_ref = strokes_mid(src);
        std::vector<float> pts;
        for (const PolarStroke& k: src) {
            const std::size_t n = k.t.size();
            if (n < 2) continue;
            Run run;
            run.first = static_cast<int>(pts.size() / 2);
            const auto emit = [&](std::size_t i) { put_point(pts, k.t[i % n], k.r[i % n], s.r_ref); };
            if (k.closed && n >= 3) {
                emit(n - 1);
                for (std::size_t i = 0; i < n; ++i) emit(i);
                emit(0);
                emit(1);
                run.segs = static_cast<int>(n);
            } else {
                emit(0);
                for (std::size_t i = 0; i < n; ++i) emit(i);
                emit(n - 1);
                run.segs = static_cast<int>(n - 1);
            }
            s.runs.push_back(run);
        }
        if (!pts.empty()) upload(s.vbo, pts.data(), pts.size() * sizeof(float));
        s.src = std::move(src);
        s.dist_valid = false;
    }

    void PolarRenderer::draw_strokes(Strokes& s, const View& v, float linewidth, LineStyle ls, const Color& c) {
        if (s.runs.empty() || !(linewidth > 0.0f) || ls == LineStyle::None) return;
        const DashPattern dash = dash_pattern(ls);
        // Arc length at each segment's start, in unit space at the view's r
        // limits (θ offset and direction don't change it); kept while
        // navigating, see set_nav_active().
        if (dash.dashed()
            && (!s.dist_valid || (!nav_active_ && (s.dist_rmin != v.rmin || s.dist_rmax != v.rmax)))) {
            PolarMap um = *v.m;
            um.offset = 0.0;
            um.dir = 1.0;
            std::vector<float> dists;
            for (std::size_t q = 0; q < s.src.size(); ++q) {
                const PolarStroke& k = s.src[q];
                const std::size_t n = k.t.size();
                if (n < 2) continue;
                const std::size_t segs = k.closed && n >= 3 ? n : n - 1;
                double acc = 0.0;
                for (std::size_t i = 0; i < segs; ++i) {
                    dists.push_back(static_cast<float>(acc));
                    const std::size_t j = (i + 1) % n;
                    const double dx = um.unit_x(k.t[j], k.r[j]) - um.unit_x(k.t[i], k.r[i]);
                    const double dy = um.unit_y(k.t[j], k.r[j]) - um.unit_y(k.t[i], k.r[i]);
                    acc += std::sqrt(dx * dx + dy * dy);
                }
            }
            upload(s.dist_vbo, dists.data(), dists.size() * sizeof(float));
            s.dist_valid = true;
            s.dist_rmin = v.rmin;
            s.dist_rmax = v.rmax;
        }

        glUseProgram(line_program_);
        set_view(line_u_, v, s.r_ref);
        glUniform1f(line_u_.half_width, linewidth * 0.5f);
        if (dash.dashed()) {
            glUniform4fv(line_u_.dash, 1, dash.seg);
            glUniform1f(line_u_.dash_period, dash.period);
            // Unit-space lengths to pixels; while the r limits moved under
            // a gesture the measure is the old one's, so the dashes drift.
            glUniform1f(line_u_.dist_scale, v.radius);
        } else {
            glUniform1f(line_u_.dash_period, 0.0f);
        }
        glUniform4f(line_u_.color, c.r, c.g, c.b, c.a);
        glBindVertexArray(line_vao_);
        PaintOnce once;
        int dist_at = 0;
        for (const Run& run: s.runs) {
            glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
            for (int a = 1; a <= 4; ++a) {
                glEnableVertexAttribArray(static_cast<unsigned>(a));
                glVertexAttribPointer(static_cast<unsigned>(a), 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                                      reinterpret_cast<void*>(
                                          static_cast<std::uintptr_t>((run.first + a - 1) * 2 * sizeof(float))));
                glVertexAttribDivisor(static_cast<unsigned>(a), 1);
            }
            if (dash.dashed()) {
                glBindBuffer(GL_ARRAY_BUFFER, s.dist_vbo);
                glEnableVertexAttribArray(5);
                glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(float),
                                      reinterpret_cast<void*>(
                                          static_cast<std::uintptr_t>(dist_at * sizeof(float))));
                glVertexAttribDivisor(5, 1);
            } else {
                glDisableVertexAttribArray(5);
                glVertexAttrib1f(5, 0.0f);
            }
            once.stroke(c.a < 1.0f);
            glDrawArraysInstanced(GL_TRIANGLES, 0, k_lineseg_vertices, run.segs);
            dist_at += run.segs;
        }
        once.finish();
    }

    // -------------------------------------------------------------------------
    // Areas
    // -------------------------------------------------------------------------
    void PolarRenderer::build_area(Area& a, const std::vector<double>& tris) {
        std::vector<double> strips;
        polar_area_strips(tris, strips);
        std::vector<double> rs;
        for (std::size_t i = 1; i < strips.size(); i += 2) rs.push_back(strips[i]);
        a.r_ref = mid_of(rs.begin(), rs.end());
        std::vector<float> pts;
        pts.reserve(strips.size());
        for (std::size_t i = 0; i + 1 < strips.size(); i += 2) put_point(pts, strips[i], strips[i + 1], a.r_ref);
        a.verts = static_cast<int>(pts.size() / 2);
        if (a.verts > 0) upload(a.vbo, pts.data(), pts.size() * sizeof(float));
    }

    void PolarRenderer::draw_area(const Area& a, const View& v, const Color& c) {
        if (a.verts == 0 || !(c.a > 0.0f)) return;
        glUseProgram(area_program_);
        set_view(area_u_, v, a.r_ref);
        glUniform4f(area_u_.color, c.r, c.g, c.b, c.a);
        glBindVertexArray(area_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, a.vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        // Pieces cut at rmin collapse onto the centre and may overlap there;
        // a translucent area paints each pixel once.
        PaintOnce once;
        once.stroke(c.a < 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, a.verts);
        once.finish();
    }

    // -------------------------------------------------------------------------
    // Kinds
    // -------------------------------------------------------------------------
    void PolarRenderer::draw_line(const LinePlot& p, const View& v) {
        if (p.x.size() < 2 || p.opts.linestyle == LineStyle::None) return;
        bool fresh = false;
        Entry& e = entry(p.id, {static_cast<double>(p.data_stamp), static_cast<double>(p.opts.drawstyle),
                                p.opts.loop ? 1.0 : 0.0}, fresh);
        if (fresh) {
            std::vector<PolarStroke> st;
            polar_line_strokes(p.x.data(), p.y.data(), std::min(p.x.size(), p.y.size()), p.opts.drawstyle,
                               p.opts.loop, st);
            build_strokes(e.strokes, std::move(st));
        }
        Color c = *p.opts.color;
        c.a *= p.opts.alpha;
        draw_strokes(e.strokes, v, p.opts.linewidth, p.opts.linestyle, c);
    }

    void PolarRenderer::draw_scatter(const ScatterPlot& p, const View& v) {
        const std::size_t n = std::min(p.x.size(), p.y.size());
        if (n == 0) return;
        bool fresh = false;
        Entry& e = entry(p.id, {static_cast<double>(p.data_stamp), p.opts.size}, fresh);
        Markers& mk = e.markers;
        if (fresh) {
            mk.r_ref = mid_of(p.y.begin(), p.y.end());
            std::vector<float> inst;
            for (std::size_t i = 0; i < n; ++i) {
                if (!std::isfinite(p.x[i]) || !std::isfinite(p.y[i])) continue;
                put_point(inst, p.x[i], p.y[i], mk.r_ref);
                inst.push_back(marker_size_at(p, i));
            }
            mk.instances = static_cast<int>(inst.size() / 3);
            if (mk.instances) upload(mk.vbo, inst.data(), inst.size() * sizeof(float));
        }
        if (mk.instances == 0) return;
        glUseProgram(marker_program_);
        set_view(marker_u_, v, mk.r_ref);
        const Color& c = *p.opts.color;
        glUniform4f(marker_u_.color, c.r, c.g, c.b, c.a * marker_face_alpha(p.opts));
        glUniform1i(marker_u_.marker, static_cast<int>(p.opts.marker));
        set_marker_edge(marker_u_.edge, marker_u_.edge_w, marker_u_.edge_face,
                        marker_edge(p.opts.edgecolor, p.opts.alpha, p.opts.edge_linewidth, c));
        glBindVertexArray(marker_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, mk.vbo);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
        glVertexAttribDivisor(2, 1);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, mk.instances);
    }

    void PolarRenderer::draw_scatter_z(const ScatterZPlot& p, const View& v) {
        const std::size_t n = std::min({p.x.size(), p.y.size(), p.z.size()});
        if (n == 0) return;
        const ScatterZOptions& o = p.opts;
        std::vector<double> key = {static_cast<double>(p.data_stamp), o.size, static_cast<double>(o.cmap.id()),
                                   o.cmap.is_reversed() ? 1.0 : 0.0,
                                   o.vmin, o.vmax, o.alpha, o.filled ? 1.0 : 0.0,
                                   static_cast<double>(o.marker), static_cast<double>(o.norm.kind), o.norm.center};
        key.insert(key.end(), o.norm.bounds.begin(), o.norm.bounds.end());
        bool fresh = false;
        Entry& e = entry(p.id, std::move(key), fresh);
        Markers& mk = e.markers;
        if (fresh) {
            mk.r_ref = mid_of(p.y.begin(), p.y.end());
            const std::uint8_t* lut = colormaps::get(o.cmap);
            const ColorNorm::Kind kind = norm_in_force(o.norm, o.vmin, o.vmax);
            const float vrange = o.vmax - o.vmin;
            const float face = marker_face_alpha(o);
            std::vector<float> inst;
            for (std::size_t i = 0; i < n; ++i) {
                if (!std::isfinite(p.x[i]) || !std::isfinite(p.y[i])) continue;
                const double z = p.z[i];
                const double t = kind == ColorNorm::Kind::Linear
                                     ? (vrange != 0.0f ? (z - o.vmin) / vrange : 0.0)
                                     : norm_t(kind, o.norm, o.vmin, o.vmax, z);
                const std::uint8_t* col = &lut[colormaps::index(t, o.cmap.is_cyclic()) * 4];
                put_point(inst, p.x[i], p.y[i], mk.r_ref);
                inst.push_back(marker_size_at(p, i));
                inst.push_back(col[0] / 255.0f);
                inst.push_back(col[1] / 255.0f);
                inst.push_back(col[2] / 255.0f);
                inst.push_back(col[3] / 255.0f * face);
            }
            mk.instances = static_cast<int>(inst.size() / 7);
            if (mk.instances) upload(mk.vbo, inst.data(), inst.size() * sizeof(float));
        }
        if (mk.instances == 0) return;
        glUseProgram(markerz_program_);
        set_view(markerz_u_, v, mk.r_ref);
        glUniform1i(markerz_u_.marker, static_cast<int>(o.marker));
        set_marker_edge(markerz_u_.edge, markerz_u_.edge_w, markerz_u_.edge_face,
                        marker_edge(o.edgecolor, o.alpha, o.edge_linewidth, Color{0.0f, 0.0f, 0.0f, 1.0f}));
        glBindVertexArray(markerz_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, mk.vbo);
        constexpr GLsizei stride = 7 * sizeof(float);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
        glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
        glVertexAttribDivisor(3, 1);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, mk.instances);
    }

    void PolarRenderer::draw_bar(const BarPlot& p, const View& v) {
        bool fresh = false;
        Entry& e = entry(p.id, {static_cast<double>(p.data_stamp)}, fresh);
        if (fresh) {
            std::vector<double> tris;
            std::vector<PolarStroke> edges;
            for (std::size_t i = 0; i < p.centers.size() && i < p.heights.size(); ++i)
                polar_bar_parts(p.centers[i], p.bar_width, p.base_at(i), p.tip_at(i), tris, edges);
            build_area(e.area, tris);
            build_strokes(e.strokes, std::move(edges));
        }
        Color face = *p.opts.color;
        face.a *= std::clamp(p.opts.alpha, 0.0f, 1.0f);
        draw_area(e.area, v, face);
        // The edge as polar_sheet()'s wedge has it: edgecolor, solid.
        Color ec = p.opts.edgecolor;
        ec.a *= std::clamp(p.opts.alpha, 0.0f, 1.0f);
        draw_strokes(e.strokes, v, p.opts.linewidth, LineStyle::Solid, ec);
    }

    void PolarRenderer::draw_fill(const FillPlot& p, const View& v) {
        bool fresh = false;
        Entry& e = entry(p.id, {static_cast<double>(p.data_stamp), static_cast<double>(p.opts.orientation),
                                static_cast<double>(p.opts.drawstyle), p.opts.interpolate ? 1.0 : 0.0}, fresh);
        if (fresh) {
            std::vector<double> tris;
            std::vector<PolarStroke> edges, st;
            polar_fill_parts(p, [&](const std::vector<double>& tri, const std::vector<double>& th,
                                    const std::vector<double>& rr) {
                tris.insert(tris.end(), tri.begin(), tri.end());
                polar_line_strokes(th.data(), rr.data(), std::min(th.size(), rr.size()), DrawStyle::Default, true,
                                   st);
                for (PolarStroke& k: st) edges.push_back(std::move(k));
            });
            build_area(e.area, tris);
            build_strokes(e.strokes, std::move(edges));
        }
        Color face = *p.opts.color;
        face.a *= std::clamp(p.opts.alpha, 0.0f, 1.0f);
        draw_area(e.area, v, face);
        draw_strokes(e.strokes, v, p.opts.linewidth, p.opts.linestyle, fill_edge(p));
    }

    void PolarRenderer::draw_poly(const PolyPlot& p, const View& v) {
        bool fresh = false;
        Entry& e = entry(p.id, {static_cast<double>(p.data_stamp)}, fresh);
        if (fresh) {
            std::vector<double> th, rr, tri;
            std::vector<PolarStroke> edges;
            if (polar_poly_parts(p, th, rr, tri))
                polar_line_strokes(th.data(), rr.data(), th.size(), DrawStyle::Default, true, edges);
            build_area(e.area, tri);
            build_strokes(e.strokes, std::move(edges));
        }
        Color face = *p.opts.color;
        face.a *= std::clamp(p.opts.alpha, 0.0f, 1.0f);
        draw_area(e.area, v, face);
        Color ec = p.opts.edgecolor.value_or(*p.opts.color);
        ec.a *= std::clamp(p.opts.alpha, 0.0f, 1.0f);
        draw_strokes(e.strokes, v, p.opts.linewidth, p.opts.linestyle, ec);
    }

} // namespace sextant
