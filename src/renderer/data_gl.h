#pragma once
// Internal header: the GL pieces DataRenderer (2D axes) and PolarRenderer
// (polar axes) share. The line stroke and marker shaders, with their point map
// and clip pasted in by token (GLSL has no include), the program helpers and
// the stencil that paints translucent strokes once.
#include "sextant/style.h"
#include <glad/glad.h>
#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

namespace sextant::data_gl {
    // Line strokes: one instance per segment, expanded to a quad here. The buffer
    // holds only the data points (view-independent). Each instance reads
    // prev/p0/p1/next from the same buffer at four offsets; the buffer is padded
    // with the first and last point, so the ends degenerate to butt caps.
    //
    // Joins are mitered up to kMiterLimit (SVG's default stroke-miterlimit, which
    // svg_writer relies on), and beveled past it or where the inner miter corner
    // would run past half of either segment (a short segment between sharp turns,
    // whose quad would otherwise fold over). A bevel ends both segments square
    // on their own normals and the segment before the joint also draws the
    // outer triangle between the two ends. Both segments decide the joint from
    // the same three points, so they agree. (Before, past the limit both took the
    // next segment's normal, which skewed the earlier segment into a wedge.)
    // The TO_PX line is the point map: `vec2 to_px(vec2 p)` (data to logical
    // pixels, y down) and `void stroke_out(vec2 px, float t)` (any varyings the
    // fragment clip needs; t runs 0 at p0 to 1 at p1). k_lineseg_px_2d for a 2D
    // axes, PolarRenderer's for a polar one.
    inline constexpr char k_lineseg_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aCorner;   // quad: x 0 at p0, 1 at p1, y -1/+1 across;
                                        // x = 2: the bevel at p1, y picks the vertex
layout(location = 1) in vec2 aPrev;     // instance: the four consecutive points
layout(location = 2) in vec2 aP0;
layout(location = 3) in vec2 aP1;
layout(location = 4) in vec2 aNext;
layout(location = 5) in float aDist;    // instance: arc length at p0 (see below)
out float vDist;                        // logical px along the polyline
uniform vec2  uResolution;
TO_PX
uniform float uHalfWidth;   // logical pixels
uniform float uDistScale;   // aDist was measured at a different zoom; rescale
const float kMiterLimit = 4.0;

// The joint at j of segments a->j and j->b: true for a miter, with `off` the
// offset of the left corners (1/cos(theta/2) along the bisector); false for a
// bevel or a path end (a zero-length neighbour), where each side keeps its own
// normal.
bool miter_joint(vec2 a, vec2 j, vec2 b, out vec2 off) {
    off = vec2(0.0);
    vec2 da = j - a, db = b - j;
    float la = length(da), lb = length(db);
    if (la <= 1e-6 || lb <= 1e-6) return false;
    vec2 nb = vec2(-db.y, db.x) / lb;
    vec2 m = vec2(-da.y, da.x) / la + nb;
    float ml = length(m);
    if (ml < 1e-3) return false;           // ~180 degree reversal
    m /= ml;
    float den = dot(m, nb);
    if (den <= 1.0 / kMiterLimit) return false;   // too sharp, or den gone negative
    // The inner corner slides tan(theta/2) half widths along both segments; past
    // half the shorter one a quad would fold over itself.
    if (uHalfWidth * sqrt(max(1.0 - den * den, 0.0)) > 0.5 * min(la, lb) * den) return false;
    off = m / den;                                // 1/cos(theta/2)
    return true;
}

void main() {
    vec2 pp = to_px(aPrev);
    vec2 p0 = to_px(aP0);
    vec2 p1 = to_px(aP1);
    vec2 pn = to_px(aNext);

    vec2  d  = p1 - p0;
    float dl = length(d);
    vec2 nb = vec2(-d.y, d.x) / max(dl, 1e-6);   // this segment's left normal

    vec2 o0, o1;
    if (!miter_joint(pp, p0, p1, o0)) o0 = nb;
    bool bevel = !miter_joint(p0, p1, pn, o1);
    if (bevel) o1 = nb;

    float t = min(aCorner.x, 1.0);
    vec2 quad = mix(p0, p1, t) + mix(o0, o1, t) * (aCorner.y * uHalfWidth);

    // The bevel: p1 and the two segments' corners on the outer side. None for
    // a miter, at the end of the path, or at an exact reversal (no area).
    vec2 dn = pn - p1;
    float dnl = length(dn);
    float turn = (d.x * dn.y - d.y * dn.x) / (max(dl, 1e-6) * max(dnl, 1e-6));   // sin of the turn
    float side = turn > 0.0 ? -1.0 : 1.0;
    bool beveled = bevel && dnl > 1e-6 && abs(turn) > 1e-4;
    float open = aCorner.y > 0.5 ? side * uHalfWidth : 0.0;
    vec2 n = aCorner.y < 1.5 ? nb : vec2(-dn.y, dn.x) / max(dnl, 1e-6);
    vec2 px = aCorner.x > 1.5 ? p1 + n * open : quad;

    vec2 ndc = (px / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    // A zero-length segment is culled (its quad would have no direction), and
    // so is a bevel that is not drawn: left as a triangle collapsed onto p1,
    // Mesa's llvmpipe dropped the whole draw (no line at all; CI, 2026-10-19).
    bool cull = dl < 1e-6 || (aCorner.x > 1.5 && !beveled);
    gl_Position = cull ? vec4(2.0, 2.0, 2.0, 1.0) : vec4(ndc, 0.0, 1.0);

    // Distance along the centreline (so dashes don't shear across the width);
    // only the accumulated prefix needs the uDistScale correction.
    float d0 = aDist * uDistScale;
    vDist = d0 + dl * min(aCorner.x, 1.0);   // the bevel sits at p1
    stroke_out(px, t);
}
)";

    // Line strokes, dashed. The pattern is on/off run lengths in logical pixels
    // from line_dash.h, zero-padded to four. uDashPeriod <= 0 = solid.
    // STROKE_CLIP defines `bool stroke_clipped()` (k_stroke_clip_2d: never).
    inline constexpr char k_lineseg_frag[] = R"(
#version 410 core
in float vDist;
uniform vec4  uColor;
uniform vec4  uDash;        // on, off, on, off — logical px
uniform float uDashPeriod;  // sum of uDash; <= 0 for a solid stroke
out vec4 FragColor;
STROKE_CLIP
void main() {
    if (stroke_clipped()) discard;
    if (uDashPeriod > 0.0) {
        float t = mod(vDist, uDashPeriod);
        float a = uDash.x, b = a + uDash.y, c = b + uDash.z;
        bool on = (t < a) || (t >= b && t < c);
        if (!on) discard;
    }
    FragColor = uColor;
}
)";

    // k_lineseg_vert's aCorner per instance, drawn as GL_TRIANGLES: the
    // segment's quad, then the bevel at p1.
    inline constexpr float k_lineseg_corners[] = {
        0.f, -1.f, 0.f, 1.f, 1.f, -1.f,
        0.f, 1.f, 1.f, -1.f, 1.f, 1.f,
        2.f, 0.f, 2.f, 1.f, 2.f, 2.f};
    inline constexpr int k_lineseg_vertices = sizeof(k_lineseg_corners) / (2 * sizeof(float));

    // TO_PX for a 2D axes: data minus anchor to pixels by scale and offset.
    inline constexpr char k_lineseg_px_2d[] = R"(
uniform vec2  uScale;       // data -> logical pixels (identity in the fallback)
uniform vec2  uOffset;
vec2 to_px(vec2 p) { return uScale * p + uOffset; }
void stroke_out(vec2 px, float t) {}
)";

    // STROKE_CLIP for a 2D axes: the scissor is the clip.
    inline constexpr char k_stroke_clip_2d[] = R"(
bool stroke_clipped() { return false; }
)";

    // The marker shapes as one signed distance in quad units (negative inside; the
    // quad is [-1,1]^2, so the outline band is `edge_px / half_size` wide), pasted
    // into every scatter fragment shader by the MARKER_SDF token. `m` is the
    // MarkerStyle enum: 0=None 1=Circle 2=Square 3=Triangle 4=Cross 5=Plus
    // 6=Diamond 7=TriangleDown 8=TriangleLeft 9=TriangleRight 10=Star
    // 11=Pentagon 12=Hexagon 13=ThinCross 14=ThinPlus 15=VLine 16=HLine. Cross
    // and Plus are two crossing bars (half-width 0.3 along the axes, 0.3/sqrt(2)
    // across the diagonals) clipped by the unit circle. The line markers (13 and
    // up, marker_is_line()) are bars `hw` half-wide (marker_line_hw(), from the
    // edge width) and take no outline band. Pentagon, Hexagon and Star are exact
    // distances (Inigo Quilez's); y grows down, so a shape's top is at -y.
    inline constexpr char k_marker_sdf[] = R"(
bool marker_is_line(int m) { return m >= 13; }
float marker_line_hw(float edgeW, float halfSize) {
    return 0.5 * (edgeW > 0.0 ? edgeW : 1.5) / max(halfSize, 1e-3);
}
float marker_tri(vec2 p) {
    return max((2.0 * abs(p.x) - p.y - 1.0) * 0.4472136, p.y - 1.0);
}
float marker_pentagon(vec2 p) {
    const vec3 k = vec3(0.809016994, 0.587785252, 0.726542528);
    const float r = 0.809016994;
    p.x = abs(p.x);
    p -= 2.0 * min(dot(vec2(-k.x, k.y), p), 0.0) * vec2(-k.x, k.y);
    p -= 2.0 * min(dot(vec2(k.x, k.y), p), 0.0) * vec2(k.x, k.y);
    p -= vec2(clamp(p.x, -r * k.z, r * k.z), r);
    return length(p) * sign(p.y);
}
float marker_hexagon(vec2 p) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    const float r = 0.866025404;
    p = abs(p.yx);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}
float marker_star(vec2 p) {
    const vec2 k1 = vec2(0.809016994375, -0.587785252292);
    const vec2 k2 = vec2(-k1.x, k1.y);
    const float rf = 0.381966;
    p = vec2(abs(p.x), -p.y);
    p -= 2.0 * max(dot(k1, p), 0.0) * k1;
    p -= 2.0 * max(dot(k2, p), 0.0) * k2;
    p.x = abs(p.x);
    p.y -= 1.0;
    vec2 ba = rf * vec2(-k1.y, k1.x) - vec2(0.0, 1.0);
    float h = clamp(dot(p, ba) / dot(ba, ba), 0.0, 1.0);
    return length(p - ba * h) * sign(p.y * ba.x - p.x * ba.y);
}
float marker_sd(int m, vec2 p, float hw) {
    if (m == 0) return 1e9;
    if (m == 1) return length(p) - 1.0;
    if (m == 2) return max(abs(p.x), abs(p.y)) - 1.0;
    if (m == 3) return marker_tri(p);
    if (m == 6) return (abs(p.x) + abs(p.y) - 1.0) * 0.70710678;
    if (m == 7) return marker_tri(vec2(p.x, -p.y));
    if (m == 8) return marker_tri(p.yx);
    if (m == 9) return marker_tri(vec2(p.y, -p.x));
    if (m == 10) return marker_star(p);
    if (m == 11) return marker_pentagon(p);
    if (m == 12) return marker_hexagon(p);
    if (m == 15) return max(abs(p.x) - hw, abs(p.y) - 1.0);
    if (m == 16) return max(abs(p.y) - hw, abs(p.x) - 1.0);
    bool line = marker_is_line(m);
    vec2 q = (m == 4 || m == 13) ? vec2(p.x + p.y, p.x - p.y) * 0.70710678 : p;
    float t = line ? hw : (m == 4) ? 0.2121320 : 0.3;
    float d = min(max(abs(q.y) - t, abs(q.x) - 1.0), max(abs(q.x) - t, abs(q.y) - 1.0));
    return line ? d : max(d, length(p) - 1.0);
}
)";

    // POLAR_CLIP for a 2D axes' markers: none (the scissor clips).
    inline constexpr char k_polar_clip_none[] = R"(
bool polar_clipped(vec2 p) { return false; }
)";

    // uEdge is the outline's final RGBA (`alpha` applied); uEdgeW its width in
    // pixels, drawn inside the boundary. 0 = no outline.
    inline constexpr char k_scatter_frag[] = R"(
#version 410 core
in vec2 vUV;
in float vHalf;
in vec2 vPx;
uniform vec4 uColor;
uniform vec4 uEdge;
uniform float uEdgeW;
uniform int  uMarker;
out vec4 FragColor;
MARKER_SDF
POLAR_CLIP
void main() {
    if (polar_clipped(vPx)) discard;
    float d = marker_sd(uMarker, vUV, marker_line_hw(uEdgeW, vHalf));
    if (d > 0.0) discard;
    bool edge = uEdgeW > 0.0 && !marker_is_line(uMarker) && d > -uEdgeW / max(vHalf, 1e-3);
    vec4 c = edge ? uEdge : uColor;
    if (c.a <= 0.0) discard;
    FragColor = c;
}
)";


    // The outline is uEdge, or with uEdgeFace each point's own RGB at uEdge.a.
    inline constexpr char k_scatterz_frag[] = R"(
#version 410 core
in vec2 vUV;
in vec4 vColor;
in float vHalf;
in vec2 vPx;
uniform vec4 uEdge;
uniform float uEdgeW;
uniform int  uEdgeFace;
uniform int  uMarker;
out vec4 FragColor;
MARKER_SDF
POLAR_CLIP
void main() {
    if (polar_clipped(vPx)) discard;
    float d = marker_sd(uMarker, vUV, marker_line_hw(uEdgeW, vHalf));
    if (d > 0.0) discard;
    vec4 c = vColor;
    if (uEdgeW > 0.0 && !marker_is_line(uMarker) && d > -uEdgeW / max(vHalf, 1e-3))
        c = vec4(uEdgeFace != 0 ? vColor.rgb : uEdge.rgb, uEdge.a);
    if (c.a <= 0.0) discard;
    FragColor = c;
}
)";


    // -------------------------------------------------------------------------
    // Programs
    // -------------------------------------------------------------------------
    inline unsigned int compile_shader(GLenum type, const char* src) {
        unsigned int s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        int ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            glDeleteShader(s);
            throw std::runtime_error(std::string("shader compile error: ") + log);
        }
        return s;
    }

    inline unsigned int link_program(unsigned int vert, unsigned int frag) {
        unsigned int p = glCreateProgram();
        glAttachShader(p, vert);
        glAttachShader(p, frag);
        glLinkProgram(p);
        int ok;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetProgramInfoLog(p, sizeof(log), nullptr, log);
            glDeleteProgram(p);
            throw std::runtime_error(std::string("program link error: ") + log);
        }
        return p;
    }

    inline unsigned int build_program(const char* vert_src, const char* frag_src) {
        unsigned int v = compile_shader(GL_VERTEX_SHADER, vert_src);
        unsigned int f = compile_shader(GL_FRAGMENT_SHADER, frag_src);
        unsigned int p = link_program(v, f);
        glDeleteShader(v);
        glDeleteShader(f);
        return p;
    }

    // GLSL has no include: a line containing only RIBBON is replaced with
    // k_ribbon_expand.
    inline std::string expand_shader(const char* src, const char* token,
                                     const char* body) {
        std::string s(src);
        const std::size_t at = s.find(token);
        if (at != std::string::npos) s.replace(at, std::strlen(token), body);
        return s;
    }

    // A marker outline as the scatter shaders take it: final RGBA, width in logical
    // pixels (0 = none), and whether the RGB is the fill's own (no `edgecolor`).
    struct MarkerEdge {
        float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float width = 0.0f;
        bool face = false;
    };

    inline MarkerEdge marker_edge(const std::optional<Color>& edgecolor, float alpha,
                                  float edge_linewidth, const Color& fill) {
        MarkerEdge e;
        if (!(edge_linewidth > 0.0f)) return e;
        const Color& c = edgecolor ? *edgecolor : fill;
        e.rgba[0] = c.r;
        e.rgba[1] = c.g;
        e.rgba[2] = c.b;
        e.rgba[3] = c.a * std::clamp(alpha, 0.0f, 1.0f);
        e.width = edge_linewidth;
        e.face = !edgecolor;
        return e;
    }

    inline void set_marker_edge(int u_edge, int u_width, int u_face, const MarkerEdge& e) {
        if (u_edge >= 0) glUniform4fv(u_edge, 1, e.rgba);
        if (u_width >= 0) glUniform1f(u_width, e.width);
        if (u_face >= 0) glUniform1i(u_face, e.face ? 1 : 0);
    }

    // A scatter fragment shader with its marker SDF and the clip `clip` (which
    // defines `bool polar_clipped(vec2 px)`) pasted in.
    inline std::string marker_frag(const char* src, const char* clip = k_polar_clip_none) {
        return expand_shader(expand_shader(src, "MARKER_SDF", k_marker_sdf).c_str(),
                             "POLAR_CLIP", clip);
    }

    // Translucent strokes paint each pixel once, as an SVG path does: a
    // stroke's own quads overlap inside a beveled turn and where it crosses
    // itself, and would blend twice there. Each translucent stroke tests and
    // writes a stencil value of its own, so it skips what it already painted
    // while other strokes still blend over it. The (scissored) stencil is
    // cleared before the first value and after the last, since NanoVG expects
    // zeros, and again when the 255 values run out. A target without a stencil
    // passes every test: the overlaps then blend twice, as before.
    class PaintOnce {
    public:
        PaintOnce() = default;
        PaintOnce(const PaintOnce&) = delete;
        PaintOnce& operator=(const PaintOnce&) = delete;

        // Before each stroke's draw.
        void stroke(bool translucent) {
            if (!translucent) {
                glDisable(GL_STENCIL_TEST);
                return;
            }
            if (ref_ == 0 || ref_ == 255) {
                clear();
                ref_ = 0;
            }
            ++ref_;
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_NOTEQUAL, ref_, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        }

        // Before end_pass(), while the scissor still holds the plot rect.
        void finish() {
            if (ref_ == 0) return;
            clear();
            glDisable(GL_STENCIL_TEST);
            glStencilFunc(GL_ALWAYS, 0, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            ref_ = 0;
        }

    private:
        static void clear() {
            glStencilMask(0xFF);
            glClearStencil(0);
            glClear(GL_STENCIL_BUFFER_BIT);
        }

        int ref_ = 0;
    };
} // namespace sextant::data_gl
