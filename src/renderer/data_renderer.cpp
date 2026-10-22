#include "data_renderer.h"
#include "coord_transform.h"
#include "../colormaps.h"
#include "bar3d.h"
#include "surface.h"
#include "surface_tri.h"
#include "scatter3d.h"
#include "line3d.h"
#include "error_bar3d.h"
#include "plane2d.h"
#include "../line_dash.h"
#include "../line_steps.h"
#include "../fill_geometry.h"
#include "../heatmap_tri_geometry.h"
#include "../contour_plot.h"
#include "../quiver_geometry.h"
#include "../reference_lines.h"
#include "../shape_geometry.h"
#include "error_bar_shape.h"
#include <glad/glad.h>
#include <vector>
#include <stdexcept>
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace sextant {
    // -------------------------------------------------------------------------
    // Shader sources
    // -------------------------------------------------------------------------

    // Shared by lines and bars, flat color. uScale/uOffset are an optional
    // data->pixel transform: bar fills use it (data-space buffer survives pan/zoom);
    // strokes and bar outlines are already pixel-space and pass identity.
    static constexpr char k_flat_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aPos;
uniform vec2 uResolution;
uniform vec2 uScale;
uniform vec2 uOffset;
void main() {
    vec2 px = uScale * aPos + uOffset;
    vec2 ndc = (px / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

    // Identity transform for the pixel-space users of k_flat_vert.
    static constexpr float k_identity_scale[2] = {1.0f, 1.0f};
    static constexpr float k_identity_offset[2] = {0.0f, 0.0f};

    static constexpr char k_flat_frag[] = R"(
#version 410 core
uniform vec4 uColor;
out vec4 FragColor;
void main() { FragColor = uColor; }
)";

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
    static constexpr char k_lineseg_vert[] = R"(
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
uniform vec2  uScale;       // data -> logical pixels (identity in the fallback)
uniform vec2  uOffset;
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
    vec2 pp = uScale * aPrev + uOffset;
    vec2 p0 = uScale * aP0   + uOffset;
    vec2 p1 = uScale * aP1   + uOffset;
    vec2 pn = uScale * aNext + uOffset;

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
}
)";

    // Line strokes, dashed. The pattern is on/off run lengths in logical pixels
    // from line_dash.h, zero-padded to four. uDashPeriod <= 0 = solid.
    static constexpr char k_lineseg_frag[] = R"(
#version 410 core
in float vDist;
uniform vec4  uColor;
uniform vec4  uDash;        // on, off, on, off — logical px
uniform float uDashPeriod;  // sum of uDash; <= 0 for a solid stroke
out vec4 FragColor;
void main() {
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
    static constexpr float k_lineseg_corners[] = {
        0.f, -1.f, 0.f, 1.f, 1.f, -1.f,
        0.f, 1.f, 1.f, -1.f, 1.f, 1.f,
        2.f, 0.f, 2.f, 1.f, 2.f, 2.f};
    static constexpr int k_lineseg_vertices = sizeof(k_lineseg_corners) / (2 * sizeof(float));

    // Quiver arrows: one instance per arrow, its tail and tip in scale space
    // minus an anchor (uScale/uOffset map them to pixels, so pan/zoom reuse the
    // buffer; at extreme zoom they are pixels with an identity map). The shape
    // is built here in pixels, as quiver_geometry.h's arrow_outline(): outline
    // points 0..6 (the shaft's corners at the tail, the shaft's and the head's at
    // the head's base, the tip, then back down the other side), drawn as the
    // head (2,3,4) and the shaft (0,1,5), (0,5,6). An arrow shorter than its
    // head is the head alone, scaled to its length; the shaft's six vertices
    // collapse off screen. The color is per instance for a colormapped quiver,
    // else a constant attribute (the attribute array disabled).
    static constexpr char k_quiver_vert[] = R"(
#version 410 core
layout(location = 0) in float aVertex;   // 0..8: which corner of the three triangles
layout(location = 1) in vec2 aTail;      // instance
layout(location = 2) in vec2 aTip;       // instance
layout(location = 3) in vec4 aColor;     // instance (colormapped), else constant
out vec4 vColor;
uniform float uAlpha;        // the quiver's alpha, on top of the color's own
uniform vec2  uResolution;
uniform vec2  uScale;
uniform vec2  uOffset;
uniform float uWidth;        // logical pixels
uniform float uHeadWidth;
uniform float uHeadLength;
const int kCorner[9] = int[9](2, 3, 4, 0, 1, 5, 0, 5, 6);
void main() {
    vec2 t = uScale * aTail + uOffset;
    vec2 h = uScale * aTip + uOffset;
    vec2 d = h - t;
    float len = length(d);
    int v = int(aVertex + 0.5);
    float hl = max(uHeadLength, 0.0);
    bool head_only = len < hl;
    if (!(len > 0.01) || (head_only && v >= 3)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    vec2 u = d / len;
    vec2 n = vec2(-u.y, u.x);
    float hw = 0.5 * max(uHeadWidth, 0.0);
    float sw = 0.5 * max(uWidth, 0.0);
    if (head_only) {
        hw *= len / hl;
        hl = len;
    }
    vec2 b = h - u * hl;
    int k = kCorner[v];
    vec2 px;
    if (k == 3) {
        px = h;
    } else {
        float side = k < 3 ? 1.0 : -1.0;
        vec2 base = (k == 0 || k == 6) ? t : b;
        float w = (k == 2 || k == 4) ? hw : sw;
        px = base + n * (side * w);
    }
    vec2 ndc = (px / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = vec4(aColor.rgb, aColor.a * uAlpha);
}
)";

    static constexpr char k_quiver_frag[] = R"(
#version 410 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
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
    static constexpr char k_marker_sdf[] = R"(
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

    // Scatter: instanced quads clipped by a marker-shape SDF. aCenter is in data
    // space minus an anchor with the transform in uScale/uOffset (survives
    // pan/zoom); the pixel half-extent is added after it, so markers don't scale.
    // At extreme zoom the CPU writes pixel-space centres instead (pick_anchor()).
    static constexpr char k_scatter_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aQuadPos;   // unit quad [-1,1]x[-1,1]
layout(location = 1) in vec2 aCenter;    // instance: data-space center (anchored)
layout(location = 2) in float aSize;     // instance: pixel diameter
out vec2 vUV;
out float vHalf;                          // half the marker's pixel size
uniform vec2 uResolution;
uniform vec2 uScale;
uniform vec2 uOffset;
void main() {
    vec2 px = uScale * aCenter + uOffset;
    px += aQuadPos * (aSize * 0.5);
    vec2 ndc = (px / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aQuadPos;
    vHalf = aSize * 0.5;
}
)";

    // uEdge is the outline's final RGBA (`alpha` applied); uEdgeW its width in
    // pixels, drawn inside the boundary. 0 = no outline.
    static constexpr char k_scatter_frag[] = R"(
#version 410 core
in vec2 vUV;
in float vHalf;
uniform vec4 uColor;
uniform vec4 uEdge;
uniform float uEdgeW;
uniform int  uMarker;
out vec4 FragColor;
MARKER_SDF
void main() {
    float d = marker_sd(uMarker, vUV, marker_line_hw(uEdgeW, vHalf));
    if (d > 0.0) discard;
    bool edge = uEdgeW > 0.0 && !marker_is_line(uMarker) && d > -uEdgeW / max(vHalf, 1e-3);
    vec4 c = edge ? uEdge : uColor;
    if (c.a <= 0.0) discard;
    FragColor = c;
}
)";

    // Continuous-color scatter: as above with a per-instance color. The marker SDF
    // is duplicated (GLSL has no include).
    static constexpr char k_scatterz_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aQuadPos;   // unit quad [-1,1]x[-1,1]
layout(location = 1) in vec2 aCenter;    // instance: data-space center (anchored)
layout(location = 2) in float aSize;     // instance: pixel diameter
layout(location = 3) in vec4 aColor;     // instance: per-point RGBA
out vec2 vUV;
out vec4 vColor;
out float vHalf;
uniform vec2 uResolution;
uniform vec2 uScale;
uniform vec2 uOffset;
void main() {
    vec2 px = uScale * aCenter + uOffset;
    px += aQuadPos * (aSize * 0.5);
    vec2 ndc = (px / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aQuadPos;
    vColor = aColor;
    vHalf = aSize * 0.5;
}
)";

    // The outline is uEdge, or with uEdgeFace each point's own RGB at uEdge.a.
    static constexpr char k_scatterz_frag[] = R"(
#version 410 core
in vec2 vUV;
in vec4 vColor;
in float vHalf;
uniform vec4 uEdge;
uniform float uEdgeW;
uniform int  uEdgeFace;
uniform int  uMarker;
out vec4 FragColor;
MARKER_SDF
void main() {
    float d = marker_sd(uMarker, vUV, marker_line_hw(uEdgeW, vHalf));
    if (d > 0.0) discard;
    vec4 c = vColor;
    if (uEdgeW > 0.0 && !marker_is_line(uMarker) && d > -uEdgeW / max(vHalf, 1e-3))
        c = vec4(uEdgeFace != 0 ? vColor.rgb : uEdge.rgb, uEdge.a);
    if (c.a <= 0.0) discard;
    FragColor = c;
}
)";

    // 3D scatter markers: billboards. Project the point, then add the pixel offset
    // in clip space premultiplied by w, so the marker is aSize pixels wherever it
    // is while keeping the point's depth. The color is baked per instance;
    // `depthshade` is applied here (camera-dependent), so orbits upload nothing.
    static constexpr char k_scatter3d_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aQuadPos;   // unit quad [-1,1]x[-1,1]
layout(location = 1) in vec3 aCenter;    // instance: data space, less the plot's anchor
layout(location = 2) in float aSize;     // instance: pixel diameter
layout(location = 3) in vec4 aColor;     // instance: RGBA, already alpha'd
out vec2 vUV;
out vec4 vColor;
out float vHalf;
out float vShade;                         // depthshade factor, for the outline too
uniform vec3 uBoxScale;
uniform vec3 uBoxOffset;
uniform mat4 uClip;
uniform vec2 uResolution;   // logical pixels, the units uClip's frame is in
// xyz = view direction, w = box-origin depth: Px3::depth as an affine function
// (see depth_affine()).
uniform vec4 uDepth;
// x = depthshade, y = the box's near depth, z = 1 / its depth extent.
uniform vec3 uShade;
void main() {
    vec3 box = uBoxScale * aCenter + uBoxOffset;
    vec4 clip = uClip * vec4(box, 1.0);
    vec2 off = aQuadPos * (aSize * 0.5) / uResolution * 2.0;
    clip.xy += off * clip.w;
    gl_Position = clip;
    vUV = aQuadPos;

    float t = clamp((dot(box, uDepth.xyz) + uDepth.w - uShade.y) * uShade.z, 0.0, 1.0);
    vShade = 1.0 - uShade.x * t;
    vColor = vec4(aColor.rgb * vShade, aColor.a);
    vHalf = aSize * 0.5;
}
)";

    // The fragment's color: fill, or the outline band (uEdge; with uEdgeFace the
    // fill's own shaded RGB at uEdge.a). Shared by the opaque and peeled shaders.
    static constexpr char k_scatter3d_shade[] = R"(
uniform vec4 uEdge;
uniform float uEdgeW;
uniform int  uEdgeFace;
vec4 marker_color(float d) {
    if (uEdgeW > 0.0 && !marker_is_line(uMarker) && d > -uEdgeW / max(vHalf, 1e-3))
        return vec4(uEdgeFace != 0 ? vColor.rgb : uEdge.rgb * vShade, uEdge.a);
    return vColor;
}
)";

    static constexpr char k_scatter3d_frag[] = R"(
#version 410 core
in vec2 vUV;
in vec4 vColor;
in float vHalf;
in float vShade;
uniform int  uMarker;
out vec4 FragColor;
MARKER_SDF
MARKER_COLOR
void main() {
    float d = marker_sd(uMarker, vUV, marker_line_hw(uEdgeW, vHalf));
    if (d > 0.0) discard;
    vec4 c = marker_color(d);
    if (c.a <= 0.0) discard;
    FragColor = c;
}
)";

    // Peeled marker: the shape test runs before peel_or_discard(), so a corner
    // outside the marker (or a fully transparent fill) never consumes a layer.
    static constexpr char k_peel_scatter3d_frag[] = R"(
#version 410 core
in vec2 vUV;
in vec4 vColor;
in float vHalf;
in float vShade;
uniform int  uMarker;
layout(location = 0) out vec4 FragColor;
PEEL
MARKER_SDF
MARKER_COLOR
void main() {
    float d = marker_sd(uMarker, vUV, marker_line_hw(uEdgeW, vHalf));
    if (d > 0.0) discard;
    vec4 c = marker_color(d);
    if (c.a <= 0.0) discard;
    peel_or_discard(gl_FragCoord.z);
    FragColor = vec4(c.rgb * c.a, c.a);
}
)";

    // Heatmap: textured quad, texture is already RGBA after CPU colormap.
    static constexpr char k_heatmap_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
uniform vec2 uResolution;
out vec2 vTC;
void main() {
    vec2 ndc = (aPos / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vTC = aTexCoord;
}
)";

    static constexpr char k_heatmap_frag[] = R"(
#version 410 core
in vec2 vTC;
uniform sampler2D uTex;
out vec4 FragColor;
void main() { FragColor = texture(uTex, vTC); }
)";

    // heatmap_tri: pixel-space triangles carrying the colormap position, looked
    // up per fragment (never interpolated colour), as surface_tri's path.
    static constexpr char k_heattri_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in float aT;
uniform vec2 uResolution;
out float vT;
void main() {
    vec2 ndc = (aPos / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vT = aT;
}
)";

    // uCyclic: wrap rather than clamp. The 256 entries are texel centres, so
    // 0 and 1 land on the first and last entry as the CPU lookup does.
    // uNorm 0: vT is the colormap position; else vT is the value, normalised
    // here as norm_t() does: 1 log (uN = log vmin, log vmax), 2 two-slope
    // (uN = vmin, centre, vmax), 3 bands: uBands holds uNB (boundary, band
    // position) texels (heatmap_band_texels()), the band found by binary
    // search, a value outside the boundaries discarded; the position is
    // already the entry, so it is not wrapped or clamped again (-1: no colour,
    // discarded).
    static constexpr char k_heattri_frag[] = R"(
#version 410 core
in float vT;
uniform sampler2D uCmap;
uniform sampler2D uBands;
uniform int uCyclic;
uniform int uNorm;
uniform int uNB;
uniform vec3 uN;
out vec4 FragColor;
void main() {
    float p = vT;
    if (uNorm == 1) {
        if (!(vT > 0.0)) discard;
        p = uN.y != uN.x ? (log(vT) - uN.x) / (uN.y - uN.x) : 0.0;
    } else if (uNorm == 2) {
        p = (vT - uN.y) * (uN.x - uN.y) > 0.0 ? 0.5 * (vT - uN.x) / (uN.y - uN.x)
                                             : 0.5 + 0.5 * (vT - uN.y) / (uN.z - uN.y);
    } else if (uNorm == 3) {
        if (!(vT >= texelFetch(uBands, ivec2(0, 0), 0).r && vT <= texelFetch(uBands, ivec2(uNB - 1, 0), 0).r))
            discard;
        int lo = 0, hi = uNB - 1;
        for (int k = 0; k < 16 && hi - lo > 1; ++k) {
            int mid = (lo + hi) / 2;
            if (texelFetch(uBands, ivec2(mid, 0), 0).r <= vT) lo = mid; else hi = mid;
        }
        p = texelFetch(uBands, ivec2(lo, 0), 0).g;
        if (p < 0.0) discard;
    }
    float t = uNorm == 3 ? p : uCyclic != 0 ? fract(p) : clamp(p, 0.0, 1.0);
    FragColor = vec4(texture(uCmap, vec2((t * 255.0 + 0.5) / 256.0, 0.5)).rgb, 1.0);
}
)";

    // A heatmap on a 3D plane: the quad above with a 3D matrix. Same cached
    // texture; GL's perspective-correct interpolation handles the warp.
    static constexpr char k_plane3d_vert[] = R"(
#version 410 core
layout(location = 0) in vec3 aPos;      // data space, less the plane's anchor
layout(location = 1) in vec2 aTexCoord;
uniform vec3 uBoxScale;
uniform vec3 uBoxOffset;
uniform mat4 uClip;
out vec2 vTC;
void main() {
    vec3 box = uBoxScale * aPos + uBoxOffset;
    gl_Position = uClip * vec4(box, 1.0);
    vTC = aTexCoord;
}
)";

    // The plane raster composited into the scene. The texture is premultiplied
    // (see begin_pass()), so uAlpha scales color and coverage and the blend is
    // GL_ONE.
    static constexpr char k_plane3d_frag[] = R"(
#version 410 core
in vec2 vTC;
uniform sampler2D uTex;
uniform float uAlpha;
out vec4 FragColor;
void main() {
    vec4 c = texture(uTex, vTC);
    // Empty texels aren't part of the plane: discard them so an opaque plane's
    // transparent margin doesn't write depth.
    if (c.a <= 0.0) discard;
    FragColor = c * uAlpha;
}
)";

    // ---------------------------------------------------------------------------
    // The translucent-plane composite
    // ---------------------------------------------------------------------------
    // K planes sorted per fragment. The plot rect is drawn once per depth slot;
    // the shader casts the pixel's ray (from Projector3D::ray_from_pixel(), the
    // same one the hover hint uses) at every plane and emits the slot-th hit from
    // the far end. The ray interpolates exactly across the quad in both
    // projections (origin/direction are affine in the pixel, and w = 1).
    static constexpr char k_plane_comp_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aNdc;
layout(location = 1) in vec3 aOrigin;   // box space
layout(location = 2) in vec3 aDir;      // box space, not normalized
out vec3 vOrigin;
out vec3 vDir;
void main() {
    gl_Position = vec4(aNdc, 0.0, 1.0);
    vOrigin = aOrigin;
    vDir    = aDir;
}
)";

    // One draw per slot so each sample gets its own gl_FragDepth: the depth test
    // handles opaque occlusion and draw order is the blend order. Sound: if the
    // slot-th sample passes, all nearer ones do too. Rasters are premultiplied,
    // so the blend is GL_ONE.
    static constexpr char k_plane_comp_frag[] = R"(
#version 410 core
#define KMAX 8
in vec3 vOrigin;
in vec3 vDir;
uniform mat4      uClip;
uniform int       uCount;
uniform int       uSlot;
uniform vec3      uOrigin[KMAX];   // the plane's p0 corner, box space
uniform vec3      uDU[KMAX];       // p1 - p0, so s = dot(d,du)/dot(du,du)
uniform vec3      uDV[KMAX];       // p3 - p0
uniform int       uAxis[KMAX];     // the plane's normal axis, 0/1/2
uniform float     uAlpha[KMAX];
uniform sampler2D uTex[KMAX];
out vec4 FragColor;

void main() {
    vec4  col[KMAX];
    float dep[KMAX];
    int   n = 0;

    for (int i = 0; i < KMAX; ++i) {
        if (i >= uCount) continue;
        int   ax = uAxis[i];
        float dn = vDir[ax];
        if (abs(dn) < 1e-9) continue;            // edge-on: no hit to speak of
        float t = (uOrigin[i][ax] - vOrigin[ax]) / dn;
        vec3  h = vOrigin + t * vDir;
        vec3  d = h - uOrigin[i];
        float lu = dot(uDU[i], uDU[i]), lv = dot(uDV[i], uDV[i]);
        if (lu <= 0.0 || lv <= 0.0) continue;    // a plane with no extent
        float su = dot(d, uDU[i]) / lu;
        float sv = dot(d, uDV[i]) / lv;
        if (su < 0.0 || su > 1.0 || sv < 0.0 || sv > 1.0) continue;

        // Depth from the same matrix the quad path uses, so the test against
        // other geometry is the same test.
        vec4 cl = uClip * vec4(h, 1.0);
        if (cl.w <= 0.0) continue;               // behind a perspective eye
        float z = (cl.z / cl.w) * 0.5 + 0.5;
        if (z < 0.0 || z > 1.0) continue;        // what GL's clip would drop

        // Empty texels are excluded, so a margin can't occupy a slot.
        vec4 c = texture(uTex[i], vec2(su, sv));
        if (c.a <= 0.0) continue;

        col[n] = c * uAlpha[i];
        dep[n] = z;
        ++n;
    }

    if (uSlot >= n) discard;

    // Farthest first; insertion sort (n <= KMAX, usually 2).
    for (int a = 1; a < KMAX; ++a) {
        if (a >= n) continue;
        vec4  c = col[a];
        float z = dep[a];
        int   b = a - 1;
        for (; b >= 0; --b) {
            if (dep[b] >= z) break;
            col[b + 1] = col[b];
            dep[b + 1] = dep[b];
        }
        col[b + 1] = c;
        dep[b + 1] = z;
    }

    FragColor    = col[uSlot];
    gl_FragDepth = dep[uSlot];
}
)";

    // 3D surface cells: data-space corners minus an anchor, camera in uniforms.
    // The per-vertex color is constant per cell (flat shading matching the SVG's
    // per-cell fill).
    static constexpr char k_surface_vert[] = R"(
#version 410 core
layout(location = 0) in vec3 aPos;      // data space, less the plot's anchor
layout(location = 1) in vec4 aColor;    // already shaded and alpha'd
uniform vec3 uBoxScale;
uniform vec3 uBoxOffset;
uniform mat4 uClip;
out vec4 vColor;
void main() {
    vec3 box = uBoxScale * aPos + uBoxOffset;
    gl_Position = uClip * vec4(box, 1.0);
    vColor = aColor;
}
)";

    static constexpr char k_surface_frag[] = R"(
#version 410 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)";

    // 3D mesh faces: the value is interpolated and the colormap sampled per
    // fragment (a per-vertex RGB blend would leave the colormap). The shade is
    // flat per face (equal on all three vertices).
    static constexpr char k_surface_tri_vert[] = R"(
#version 410 core
layout(location = 0) in vec3  aPos;      // data space, less the plot's anchor
layout(location = 1) in vec4  aColor;    // the flat colour, already alpha'd
layout(location = 2) in float aValue;    // normalized colormap value at this vertex
layout(location = 3) in float aShade;    // the face's flat shade
uniform vec3 uBoxScale;
uniform vec3 uBoxOffset;
uniform mat4 uClip;
out vec4  vColor;
out float vValue;
out float vShade;
void main() {
    vec3 box = uBoxScale * aPos + uBoxOffset;
    gl_Position = uClip * vec4(box, 1.0);
    vColor = aColor;
    vValue = aValue;
    vShade = aShade;
}
)";

    static constexpr char k_surface_tri_frag[] = R"(
#version 410 core
in vec4  vColor;
in float vValue;
in float vShade;
uniform sampler2D uCmap;
uniform int  uColormapped;
out vec4 FragColor;
void main() {
    // vColor.a carries the resolved alpha; only rgb comes from the map.
    vec3 rgb = uColormapped != 0
             ? texture(uCmap, vec2(uColormapped == 2 ? fract(vValue) : clamp(vValue, 0.0, 1.0), 0.5)).rgb
             : vColor.rgb;
    FragColor = vec4(rgb * vShade, vColor.a);
}
)";

    // 3D bar faces: data-space corners minus an anchor; uBoxScale/uBoxOffset map
    // data to box, uClip is the camera. The shade is per vertex but equal per face.
    static constexpr char k_bar3d_vert[] = R"(
#version 410 core
layout(location = 0) in vec3 aPos;     // data space, less the plot's anchor
layout(location = 1) in float aShade;
uniform vec3 uBoxScale;
uniform vec3 uBoxOffset;
uniform mat4 uClip;
out float vShade;
void main() {
    vec3 box = uBoxScale * aPos + uBoxOffset;
    gl_Position = uClip * vec4(box, 1.0);
    vShade = aShade;
}
)";

    static constexpr char k_bar3d_frag[] = R"(
#version 410 core
in float vShade;
uniform vec4 uColor;
out vec4 FragColor;
void main() { FragColor = vec4(uColor.rgb * vShade, uColor.a); }
)";

    // World-space ribbon expansion, shared textually (RIBBON token) by bar edges
    // and plane strokes.
    static constexpr char k_ribbon_expand[] = R"(
// The offset is perpendicular to the segment `a`-`b` (box space) and to `ref`:
// bar edges pass the eye direction (billboard); plane strokes pass the plane
// normal (the ribbon stays in the plane, avoiding z-fighting).
vec3 ribbon_offset(vec3 a, vec3 b, vec3 ref, float half_w) {
    vec3 side = cross(b - a, ref);
    float sl = length(side);
    side = sl > 1e-9 ? side / sl : vec3(0.0);
    return side * half_w;
}
)";

    // Bar outlines: expanded in box space then projected, so the width is a scene
    // length and distant edges are thinner (the 2D stroke shader does the reverse).
    static constexpr char k_bar3d_edge_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aCorner;   // x: 0 at a, 1 at b;  y: -1/+1 across
layout(location = 1) in vec3 aA;        // instance: the segment, in data space
layout(location = 2) in vec3 aB;
uniform vec3  uBoxScale;
uniform vec3  uBoxOffset;
uniform mat4  uClip;
uniform vec3  uEye;         // box space: a position, or a direction under ortho
uniform int   uPersp;
uniform float uHalfWidth;   // box units -- a length in the scene, not in pixels
RIBBON
void main() {
    vec3 a = uBoxScale * aA + uBoxOffset;
    vec3 b = uBoxScale * aB + uBoxOffset;
    vec3 p = mix(a, b, aCorner.x);
    vec3 to_eye = normalize(uPersp == 1 ? uEye - p : uEye);
    gl_Position = uClip * vec4(
        p + ribbon_offset(a, b, to_eye, aCorner.y * uHalfWidth), 1.0);
}
)";

    // A path's ribbon: the bar-edge program plus
    //   - a miter join: each end is offset along the bisector of its own and its
    //     neighbour's perpendicular, scaled by 1/cos(half the turn). An open end
    //     passes prev == a (or next == b), which degenerates to a square end.
    //   - a ramp between its ends. For a colormapped path the *value* ramps and is
    //     looked up per fragment (an RGB ramp would leave the colormap). A flat
    //     series ramps between two equal colors.
    // The miter is limited to 4x the half width, SVG's default stroke-miterlimit.
    static constexpr char k_line3d_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aCorner;   // x: 0 at a, 1 at b;  y: -1/+1 across
layout(location = 1) in vec3 aPrev;     // instance: the point before a (== a if none)
layout(location = 2) in vec3 aA;        // instance: the segment, in data space
layout(location = 3) in vec3 aB;
layout(location = 4) in vec3 aNext;     // instance: the point after b (== b if none)
layout(location = 5) in vec4 aColorA;   // instance: RGBA at a, already alpha'd
layout(location = 6) in vec4 aColorB;
layout(location = 7) in vec2 aValue;    // instance: the two ends' normalized colormap values
// Flat color, colormap value and depth ramp as varyings, all resolved per
// fragment.
out vec4  vColor;
out float vValue;
out float vShadeT;
uniform vec3  uBoxScale;
uniform vec3  uBoxOffset;
uniform mat4  uClip;
uniform vec3  uEye;         // box space: a position, or a direction under ortho
uniform int   uPersp;
uniform float uHalfWidth;   // box units -- a length in the scene, not in pixels
// xyz = view direction, w = box-origin depth (see depth_affine()).
uniform vec4  uDepth;
// x = depthshade, y = the box's near depth, z = 1 / its depth extent.
uniform vec3  uShade;
RIBBON
void main() {
    vec3 prv = uBoxScale * aPrev + uBoxOffset;
    vec3 a   = uBoxScale * aA    + uBoxOffset;
    vec3 b   = uBoxScale * aB    + uBoxOffset;
    vec3 nxt = uBoxScale * aNext + uBoxOffset;
    vec3 p   = mix(a, b, aCorner.x);
    vec3 to_eye = normalize(uPersp == 1 ? uEye - p : uEye);

    // This segment's perpendicular and the neighbour's at this end (zero for an
    // open end).
    vec3 here  = ribbon_offset(a, b, to_eye, 1.0);
    vec3 other = aCorner.x < 0.5 ? ribbon_offset(prv, a, to_eye, 1.0)
                                 : ribbon_offset(b, nxt, to_eye, 1.0);
    vec3 miter = here + other;
    float ml = length(miter);
    miter = ml > 1e-9 ? miter / ml : here;
    // 1/cos(half the turn), clamped at the miter limit.
    float scale = min(1.0 / max(dot(miter, here), 1e-4), 4.0);

    vec3 pos = p + miter * (aCorner.y * uHalfWidth * scale);
    gl_Position = uClip * vec4(pos, 1.0);

    vShadeT = clamp((dot(pos, uDepth.xyz) + uDepth.w - uShade.y) * uShade.z, 0.0, 1.0);
    vColor  = mix(aColorA, aColorB, aCorner.x);
    vValue  = mix(aValue.x, aValue.y, aCorner.x);
}
)";

    // A path's fragment: colormap lookup and depth shade per fragment.
    static constexpr char k_line3d_frag[] = R"(
#version 410 core
in vec4  vColor;
in float vValue;
in float vShadeT;
// The colormap as a 256x1 lookup, and how this path looks values up: 0 = none
// (flat), 1 = clamped to the ends, 2 = wrapped (a cyclic map).
uniform sampler2D uCmap;
uniform int  uColormapped;
// x = depthshade; y and z are the vertex shader's, unused here.
uniform vec3 uShade;
out vec4 FragColor;
void main() {
    vec4 c = vColor;
    // vColor.a carries the resolved alpha; only rgb comes from the map.
    if (uColormapped != 0)
        c = vec4(texture(uCmap, vec2(uColormapped == 2 ? fract(vValue) : clamp(vValue, 0.0, 1.0), 0.5)).rgb, c.a);
    FragColor = vec4(c.rgb * (1.0 - uShade.x * vShadeT), c.a);
}
)";

    // Peeled: the layer test plus the premultiply (see k_peel_surface_frag).
    static constexpr char k_peel_line3d_frag[] = R"(
#version 410 core
in vec4  vColor;
in float vValue;
in float vShadeT;
uniform sampler2D uCmap;
uniform int  uColormapped;
uniform vec3 uShade;
layout(location = 0) out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    vec4 c = vColor;
    if (uColormapped != 0)
        c = vec4(texture(uCmap, vec2(uColormapped == 2 ? fract(vValue) : clamp(vValue, 0.0, 1.0), 0.5)).rgb, c.a);
    vec3 rgb = c.rgb * (1.0 - uShade.x * vShadeT);
    FragColor = vec4(rgb * c.a, c.a);
}
)";

    // 3D error bars: finished box-space geometry from errorbar3d_pieces(); this
    // projects it and applies the depth shade.
    static constexpr char k_errbar3d_vert[] = R"(
#version 410 core
layout(location = 0) in vec3 aPos;     // box space
layout(location = 1) in vec4 aColor;   // alpha resolved
out vec4  vColor;
out float vShadeT;
uniform mat4 uClip;
uniform vec4 uDepth;   // see k_line3d_vert
uniform vec3 uShade;   // x = depthshade, y = near depth, z = 1 / depth extent
void main() {
    gl_Position = uClip * vec4(aPos, 1.0);
    vShadeT = clamp((dot(aPos, uDepth.xyz) + uDepth.w - uShade.y) * uShade.z, 0.0, 1.0);
    vColor  = aColor;
}
)";

    static constexpr char k_errbar3d_frag[] = R"(
#version 410 core
in vec4  vColor;
in float vShadeT;
uniform vec3 uShade;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor.rgb * (1.0 - uShade.x * vShadeT), vColor.a);
}
)";

    static constexpr char k_peel_errbar3d_frag[] = R"(
#version 410 core
in vec4  vColor;
in float vShadeT;
uniform vec3 uShade;
layout(location = 0) out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    vec3 rgb = vColor.rgb * (1.0 - uShade.x * vShadeT);
    FragColor = vec4(rgb * vColor.a, vColor.a);
}
)";

    // ---------------------------------------------------------------------------
    // Depth peeling
    // ---------------------------------------------------------------------------
    // The peel test, pasted into the in-scene fragment shaders by the PEEL token:
    //   - `z <= prev` drops what earlier passes peeled (`<=`: the winner wrote
    //     exactly this z). Exactly coplanar primitives share a layer and the
    //     farther is lost.
    //   - `z >= opaque` drops what the opaque phase covers (its depth is a copy).
    // `prev` is the z the winner wrote to an R32F target, not read back from the
    // depth buffer: Apple GPUs emulate DEPTH24_STENCIL8, the value read back can
    // fall just below the fragment's own z, and the same layer then passes the
    // test on every pass. Same float out, same float in.
    // texelFetch: the targets match the plot rect at framebuffer resolution.
    // gl_FragCoord.z includes polygon offset, so offset wireframes peel once.
    static constexpr char k_peel_test[] = R"(
layout(location = 1) out float PeelZ;
uniform sampler2D uPrevDepth;
uniform sampler2D uOpaqueDepth;
void peel_or_discard(float z) {
    ivec2 pc = ivec2(gl_FragCoord.xy);
    if (z <= texelFetch(uPrevDepth,   pc, 0).r) discard;
    if (z >= texelFetch(uOpaqueDepth, pc, 0).r) discard;
    PeelZ = z;
}
)";

    // The peel fragment shaders: the non-peeling twin plus the test and a
    // premultiply (the `under` blend needs src.rgb * src.a * dst.a).
    static constexpr char k_peel_bar3d_frag[] = R"(
#version 410 core
in float vShade;
uniform vec4 uColor;
layout(location = 0) out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    FragColor = vec4(uColor.rgb * vShade * uColor.a, uColor.a);
}
)";

    static constexpr char k_peel_surface_frag[] = R"(
#version 410 core
in vec4 vColor;
layout(location = 0) out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    FragColor = vec4(vColor.rgb * vColor.a, vColor.a);
}
)";

    // Peeled: layer test and premultiply; the colormap lookup stays per fragment.
    static constexpr char k_peel_surface_tri_frag[] = R"(
#version 410 core
in vec4  vColor;
in float vValue;
in float vShade;
uniform sampler2D uCmap;
uniform int  uColormapped;
layout(location = 0) out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    vec3 rgb = uColormapped != 0
             ? texture(uCmap, vec2(uColormapped == 2 ? fract(vValue) : clamp(vValue, 0.0, 1.0), 0.5)).rgb
             : vColor.rgb;
    rgb *= vShade;
    FragColor = vec4(rgb * vColor.a, vColor.a);
}
)";

    // The plane quad needs no premultiply: its raster already is.
    static constexpr char k_peel_plane3d_frag[] = R"(
#version 410 core
in vec2 vTC;
uniform sampler2D uTex;
uniform float uAlpha;
layout(location = 0) out vec4 FragColor;
PEEL
void main() {
    vec4 c = texture(uTex, vTC);
    if (c.a <= 0.0) discard;
    peel_or_discard(gl_FragCoord.z);
    FragColor = c * uAlpha;
}
)";

    // Bar and surface wireframes under peeling (k_flat_frag is shared with 2D).
    static constexpr char k_peel_flat_frag[] = R"(
#version 410 core
uniform vec4 uColor;
layout(location = 0) out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    FragColor = vec4(uColor.rgb * uColor.a, uColor.a);
}
)";

    // The peel composite quad, used twice per pass (layer under accumulation,
    // accumulation over the scene); the shader only decides what alpha means.
    static constexpr char k_peel_comp_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTC;
out vec2 vTC;
void main() { vTC = aTC; gl_Position = vec4(aPos, 0.0, 1.0); }
)";

    static constexpr char k_peel_comp_frag[] = R"(
#version 410 core
in vec2 vTC;
uniform sampler2D uTex;
uniform int uFlipAlpha;
out vec4 FragColor;
void main() {
    vec4 c = texture(uTex, vTC);
    // Accumulated alpha is transmittance; the final `over` wants coverage.
    FragColor = (uFlipAlpha != 0) ? vec4(c.rgb, 1.0 - c.a) : c;
}
)";

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------
    static unsigned int compile_shader(GLenum type, const char* src) {
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

    static unsigned int link_program(unsigned int vert, unsigned int frag) {
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

    static unsigned int build_program(const char* vert_src, const char* frag_src) {
        unsigned int v = compile_shader(GL_VERTEX_SHADER, vert_src);
        unsigned int f = compile_shader(GL_FRAGMENT_SHADER, frag_src);
        unsigned int p = link_program(v, f);
        glDeleteShader(v);
        glDeleteShader(f);
        return p;
    }

    // GLSL has no include: a line containing only RIBBON is replaced with
    // k_ribbon_expand.
    static std::string expand_shader(const char* src, const char* token,
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

    static MarkerEdge marker_edge(const std::optional<Color>& edgecolor, float alpha,
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

    static void set_marker_edge(int u_edge, int u_width, int u_face, const MarkerEdge& e) {
        if (u_edge >= 0) glUniform4fv(u_edge, 1, e.rgba);
        if (u_width >= 0) glUniform1f(u_width, e.width);
        if (u_face >= 0) glUniform1i(u_face, e.face ? 1 : 0);
    }

    // A 3D scatter fragment shader with its marker SDF and outline blend pasted in.
    static std::string scatter3d_marker_source(const char* src) {
        return expand_shader(expand_shader(src, "MARKER_SDF", k_marker_sdf).c_str(),
                             "MARKER_COLOR", k_scatter3d_shade);
    }

    // Scissor to this axes' plot rect, converting logical to framebuffer pixels
    // and rounding outward.
    void DataRenderer::begin_pass(const PlotRect& pr, float win_h) const {
        if (peel_.active) {
            // A peel target is the plot rect (the viewport is already shifted), so
            // clip to the whole target.
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, peel_.w, peel_.h);

            // No blending: a peel pass keeps one fragment per pixel; compositing
            // happens per layer afterwards.
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST); // each per-kind draw enables it itself
            glDisable(GL_CULL_FACE);
            return;
        }

        const float s = pixel_ratio_;
        const float x0 = pr.x * s;
        const float y0 = (win_h - pr.y - pr.h) * s; // GL origin is bottom-left
        const float x1 = (pr.x + pr.w) * s;
        const float y1 = (win_h - pr.y) * s;
        glEnable(GL_SCISSOR_TEST);
        glScissor(static_cast<int>(std::floor(x0)),
                  static_cast<int>(std::floor(y0)),
                  static_cast<int>(std::ceil(x1) - std::floor(x0)),
                  static_cast<int>(std::ceil(y1) - std::floor(y0)));

        // Set blending explicitly: NanoVG leaves premultiplied blending behind,
        // while the data shaders emit straight alpha. Colors use straight alpha;
        // the alpha channel uses GL_ONE, ONE_MINUS_SRC_ALPHA to accumulate coverage
        // (GL_SRC_ALPHA on alpha would leave partly covered texels too faint in a
        // plane raster, and a translucent PNG where the picture is opaque).
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        // The data pass is a fixed-order painter's draw with arbitrary winding.
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
    }

    void DataRenderer::end_pass() const {
        glDisable(GL_SCISSOR_TEST);
    }

    namespace {
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
    } // namespace

    // Exact equality: the transform is recomputed deterministically, so it is
    // bit-identical unless the view really moved.
    namespace {
        // The data->pixel map as the scatter shaders' uniforms, folded with an anchor:
        //
        //   to_px(x) = pr.x + (x - xmin) * sx,  with x = anchor + r
        //            = [pr.x + (anchor - xmin) * sx] + r * sx
        //
        // The bracketed term is computed in double, then narrowed.
        struct DataToPixel {
            float scale[2];
            float offset[2];
        };

        // In scale space (see CoordTransform): the buffers hold sx()/sy() of the
        // data, and the anchor is a scale-space value.
        DataToPixel data_to_pixel(const CoordTransform& tr, double ax, double ay) {
            const double sx = static_cast<double>(tr.pw) / tr.sxspan();
            const double sy = static_cast<double>(tr.ph) / tr.syspan();
            DataToPixel m;
            m.scale[0] = static_cast<float>(sx);
            m.scale[1] = static_cast<float>(-sy); // data y up, pixel y down
            m.offset[0] = static_cast<float>(tr.px + (ax - tr.sxmin()) * sx);
            m.offset[1] = static_cast<float>(tr.py + tr.ph - (ay - tr.symin()) * sy);
            return m;
        }

        // Can a float residual (at most half the data span) resolve this view to a
        // tenth of a pixel? Fails only at extreme zoom, where the caller falls back
        // to pixel-space centres.
        bool float_resolves_view(double span, double range, double pixels) {
            if (span <= 0.0 || range <= 0.0) return true; // degenerate: nothing to lose
            constexpr double kFloatEps = 5.96e-8; // 2^-24
            const double quantum_px = (span * 0.5) * kFloatEps * (pixels / range);
            return quantum_px < 0.1;
        }
    } // namespace

    // Data-space buffers hold scale-space values, so a scale change (a panel
    // edit keeps the data generation) rebuilds them.
    static bool same_scales(const CoordTransform& a, const CoordTransform& b) {
        return a.xs == b.xs && a.ys == b.ys;
    }

    static bool same_view(const CoordTransform& a, const CoordTransform& b) {
        return same_scales(a, b)
               && a.xmin == b.xmin && a.xmax == b.xmax
               && a.ymin == b.ymin && a.ymax == b.ymax
               && a.px == b.px && a.py == b.py && a.pw == b.pw && a.ph == b.ph
               && a.win_w == b.win_w && a.win_h == b.win_h;
    }

    namespace {
        // Axis-aligned rectangle outline as four quads straddling the edges by half
        // the stroke width (square corners for bar edges). Pixel-space.
        void build_rect_outline(float x0, float y0, float x1, float y1, float width,
                                std::vector<float>& out) {
            if (width <= 0.0f) return;
            if (x0 > x1) std::swap(x0, x1);
            if (y0 > y1) std::swap(y0, y1);
            // Keep the inner edge from crossing itself on a bar thinner than the stroke.
            const float hw = std::min(width * 0.5f,
                                      std::min(x1 - x0, y1 - y0) * 0.5f);
            if (hw <= 0.0f) return;

            const float ox0 = x0 - hw, oy0 = y0 - hw, ox1 = x1 + hw, oy1 = y1 + hw;
            const float ix0 = x0 + hw, iy0 = y0 + hw, ix1 = x1 - hw, iy1 = y1 - hw;

            // Matching corner order, so consecutive corners span one side.
            const float outer[8] = {ox0, oy0, ox1, oy0, ox1, oy1, ox0, oy1};
            const float inner[8] = {ix0, iy0, ix1, iy0, ix1, iy1, ix0, iy1};
            out.reserve(out.size() + 48);
            for (int i = 0; i < 4; ++i) {
                const int j = (i + 1) % 4;
                const float ax = outer[i * 2], ay = outer[i * 2 + 1];
                const float bx = outer[j * 2], by = outer[j * 2 + 1];
                const float cx = inner[i * 2], cy = inner[i * 2 + 1];
                const float dx = inner[j * 2], dy = inner[j * 2 + 1];
                out.insert(out.end(), {
                               ax, ay, bx, by, cx, cy,
                               cx, cy, bx, by, dx, dy
                           });
            }
        }

        // One axis-aligned pixel-space rectangle as two triangles.
        void push_px_rect(float x0, float y0, float x1, float y1,
                          std::vector<float>& out) {
            if (x0 > x1) std::swap(x0, x1);
            if (y0 > y1) std::swap(y0, y1);
            out.insert(out.end(), {
                           x0, y0, x1, y0, x1, y1,
                           x0, y0, x1, y1, x0, y1,
                       });
        }

        // A butt-ended segment `2 * hw` wide as two triangles.
        void push_px_segment(float x0, float y0, float x1, float y1, float hw,
                             std::vector<float>& out) {
            const float dx = x1 - x0, dy = y1 - y0;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len <= 0.0f || hw <= 0.0f) return;
            const float nx = -dy / len * hw, ny = dx / len * hw;
            out.insert(out.end(), {
                           x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny,
                           x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny,
                       });
        }

        // One whisker in pixels (whisker_segments()). `lo`/`hi` equal to the point's
        // coordinate mean nothing on that side.
        void build_whisker(float cx, float cy, float lo, float hi,
                           const ErrorBarOptions& style, bool vertical,
                           std::vector<float>& out) {
            const float hw = std::max(style.linewidth, 0.0f) * 0.5f;
            if (hw <= 0.0f) return;
            whisker_segments(cx, cy, lo, hi, vertical, 1.0, 1.0, style.capsize, style.capstyle,
                             [&](double x0, double y0, double x1, double y1) {
                                 push_px_segment(static_cast<float>(x0), static_cast<float>(y0),
                                                 static_cast<float>(x1), static_cast<float>(y1),
                                                 hw, out);
                             });
        }

        // The box outline: four inset bars (overlapping corners are invisible).
        void build_box_outline(float x0, float y0, float x1, float y1,
                               float linewidth, std::vector<float>& out) {
            const float hw = std::max(linewidth, 0.0f) * 0.5f;
            if (hw <= 0.0f) return;
            if (x0 > x1) std::swap(x0, x1);
            if (y0 > y1) std::swap(y0, y1);
            push_px_rect(x0 - hw, y0 - hw, x1 + hw, y0 + hw, out); // top
            push_px_rect(x0 - hw, y1 - hw, x1 + hw, y1 + hw, out); // bottom
            push_px_rect(x0 - hw, y0 - hw, x0 + hw, y1 + hw, out); // left
            push_px_rect(x1 - hw, y0 - hw, x1 + hw, y1 + hw, out); // right
        }

        // Error-bar geometry for any kind: anchor points plus ErrorBarData. `ys` is the
        // y the bar hangs off (a bar's height, not its baseline). Two buffers: box
        // interiors, and everything else.
        void build_error_bars(const CowVec<double>& xs, const CowVec<double>& ys,
                              const ErrorBarData& err, const ErrorBarOptions& style,
                              const CoordTransform& tr,
                              std::vector<float>& fill, std::vector<float>& stroke) {
            const std::size_t n = std::min(xs.size(), ys.size());
            const float halfbox = std::max(style.boxwidth, 0.0f) * 0.5f;
            const bool filled = style.box_alpha > 0.0f;

            // An absent side is the point's own coordinate, computed identically.
            auto end_px = [&](double p, double off) { return tr.to_px(p + off); };
            auto end_py = [&](double p, double off) { return tr.to_py(p + off); };

            for (std::size_t i = 0; i < n; ++i) {
                const float cx = tr.to_px(xs[i]);
                const float cy = tr.to_py(ys[i]);

                if (err.has_y_cap()) {
                    const ErrOffsets o = err.y_cap(i);
                    build_whisker(cx, cy, end_py(ys[i], -o.lo), end_py(ys[i], o.hi),
                                  style, true, stroke);
                }
                if (err.has_x_cap()) {
                    const ErrOffsets o = err.x_cap(i);
                    build_whisker(cx, cy, end_px(xs[i], -o.lo), end_px(xs[i], o.hi),
                                  style, false, stroke);
                }

                // Box spans directions with box data; others use `boxwidth` pixels.
                if (!err.has_y_box() && !err.has_x_box()) continue;
                float bx0, bx1, by0, by1;
                if (err.has_x_box()) {
                    const ErrOffsets o = err.x_box(i);
                    bx0 = end_px(xs[i], -o.lo);
                    bx1 = end_px(xs[i], o.hi);
                } else {
                    bx0 = cx - halfbox;
                    bx1 = cx + halfbox;
                }
                if (err.has_y_box()) {
                    const ErrOffsets o = err.y_box(i);
                    by0 = end_py(ys[i], -o.lo);
                    by1 = end_py(ys[i], o.hi);
                } else {
                    by0 = cy - halfbox;
                    by1 = cy + halfbox;
                }
                if (bx0 == bx1 || by0 == by1) continue; // zero extent: no box
                if (filled) push_px_rect(bx0, by0, bx1, by1, fill);
                build_box_outline(bx0, by0, bx1, by1, style.linewidth, stroke);
            }
        }
    } // namespace

    // -------------------------------------------------------------------------
    // Construction / destruction
    // -------------------------------------------------------------------------
    DataRenderer::DataRenderer() {
        // --- flat (line + bar) ---
        line_program_ = build_program(k_flat_vert, k_flat_frag);
        glGenVertexArrays(1, &line_vao_);
        glGenBuffers(1, &line_vbo_);
        glBindVertexArray(line_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glBindVertexArray(0);

        // --- instanced line segments ---
        lineseg_program_ = build_program(k_lineseg_vert, k_lineseg_frag);
        glGenVertexArrays(1, &lineseg_vao_);
        glGenBuffers(1, &lineseg_corner_vbo_);
        glBindVertexArray(lineseg_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, lineseg_corner_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(k_lineseg_corners), k_lineseg_corners,
                     GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glVertexAttribDivisor(0, 0);
        // Attributes 1..4 (prev/p0/p1/next) are pointed at each line's buffer at
        // draw time, offset by one point each.
        glBindVertexArray(0);

        // --- scatter ---
        scatter_program_ = build_program(
            k_scatter_vert, expand_shader(k_scatter_frag, "MARKER_SDF", k_marker_sdf).c_str());
        glGenVertexArrays(1, &scatter_vao_);
        glGenBuffers(1, &scatter_quad_vbo_);
        glGenBuffers(1, &scatter_inst_vbo_);

        // Unit quad: 6 vertices (2 triangles), positions in [-1,1]x[-1,1].
        static const float quad[12] = {
            -1, -1, 1, -1, 1, 1,
            -1, -1, 1, 1, -1, 1,
        };
        glBindVertexArray(scatter_vao_);

        glBindBuffer(GL_ARRAY_BUFFER, scatter_quad_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glVertexAttribDivisor(0, 0); // per-vertex

        // Instance buffer layout: (cx, cy, size) per instance = 3 floats
        glBindBuffer(GL_ARRAY_BUFFER, scatter_inst_vbo_);
        glEnableVertexAttribArray(1); // aCenter
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2); // aSize
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                              reinterpret_cast<void *>(2 * sizeof(float)));
        glVertexAttribDivisor(2, 1);

        glBindVertexArray(0);

        // --- scatter_z (continuous-color scatter) ---
        scatterz_program_ = build_program(
            k_scatterz_vert, expand_shader(k_scatterz_frag, "MARKER_SDF", k_marker_sdf).c_str());
        glGenVertexArrays(1, &scatterz_vao_);
        glGenBuffers(1, &scatterz_quad_vbo_);
        glGenBuffers(1, &scatterz_inst_vbo_);

        glBindVertexArray(scatterz_vao_);

        glBindBuffer(GL_ARRAY_BUFFER, scatterz_quad_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW); // same unit quad
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glVertexAttribDivisor(0, 0);

        // Instance buffer layout: (cx, cy, size, r, g, b, a) = 7 floats
        glBindBuffer(GL_ARRAY_BUFFER, scatterz_inst_vbo_);
        glEnableVertexAttribArray(1); // aCenter
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
        glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2); // aSize
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                              reinterpret_cast<void *>(2 * sizeof(float)));
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3); // aColor
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                              reinterpret_cast<void *>(3 * sizeof(float)));
        glVertexAttribDivisor(3, 1);

        glBindVertexArray(0);

        // --- quiver (instanced arrows) ---
        quiver_program_ = build_program(k_quiver_vert, k_quiver_frag);
        glGenVertexArrays(1, &quiver_vao_);
        glGenBuffers(1, &quiver_corner_vbo_);
        glBindVertexArray(quiver_vao_);
        static const float corners9[9] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
        glBindBuffer(GL_ARRAY_BUFFER, quiver_corner_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(corners9), corners9, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
        glVertexAttribDivisor(0, 0);
        // Attributes 1 (tail) and 2 (tip) are pointed at each quiver's buffer at
        // draw time.
        glBindVertexArray(0);

        // --- heatmap ---
        heatmap_program_ = build_program(k_heatmap_vert, k_heatmap_frag);
        glGenVertexArrays(1, &heatmap_vao_);
        glGenBuffers(1, &heatmap_vbo_);
        glGenBuffers(1, &heatmap_ebo_);
        // Layout: (px, py, tx, ty) per vertex — uploaded fresh each draw.
        glBindVertexArray(heatmap_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, heatmap_vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void *>(2 * sizeof(float)));
        glBindVertexArray(0);

        // --- heatmap_tri ---
        heattri_program_ = build_program(k_heattri_vert, k_heattri_frag);
        glGenVertexArrays(1, &heattri_vao_);
        glGenBuffers(1, &heattri_vbo_);
        // Layout: (px, py, t) per vertex, uploaded fresh each draw.
        glBindVertexArray(heattri_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, heattri_vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                              reinterpret_cast<void *>(2 * sizeof(float)));
        glBindVertexArray(0);

        // --- 3D planes ---
        plane3d_program_ = build_program(k_plane3d_vert, k_plane3d_frag);
        glGenVertexArrays(1, &plane3d_vao_);
        glGenBuffers(1, &plane3d_vbo_);
        // Layout: (x, y, z, tx, ty) per vertex — six of them, uploaded per draw.
        glBindVertexArray(plane3d_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, plane3d_vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                              reinterpret_cast<void *>(3 * sizeof(float)));
        glBindVertexArray(0);

        // --- The translucent-plane composite ---
        plane_comp_program_ = build_program(k_plane_comp_vert, k_plane_comp_frag);
        glGenVertexArrays(1, &plane_comp_vao_);
        glGenBuffers(1, &plane_comp_vbo_);
        // Layout: (ndc_x, ndc_y, ox, oy, oz, dx, dy, dz) per vertex -- the plot rect
        // with its corner rays, uploaded per group.
        glBindVertexArray(plane_comp_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, plane_comp_vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<void *>(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<void *>(5 * sizeof(float)));
        glBindVertexArray(0);

        // Sampler i reads texture unit i; set once.
        glUseProgram(plane_comp_program_);
        for (int i = 0; i < kMaxCompositePlanes; ++i) {
            char name[24];
            std::snprintf(name, sizeof(name), "uTex[%d]", i);
            glUniform1i(glGetUniformLocation(plane_comp_program_, name), i);
        }
        glUseProgram(0);

        // --- 3D scatter markers ---
        scatter3d_program_ = build_program(
            k_scatter3d_vert, scatter3d_marker_source(k_scatter3d_frag).c_str());
        glGenVertexArrays(1, &scatter3d_vao_);
        glGenBuffers(1, &scatter3d_corner_vbo_);
        glBindVertexArray(scatter3d_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, scatter3d_corner_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW); // the same unit quad
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glVertexAttribDivisor(0, 0); // per-vertex
        // Attributes 1..3 are pointed at each cloud's instance buffer at draw time.
        glBindVertexArray(0);

        // --- 3D bar faces ---
        bar3d_program_ = build_program(k_bar3d_vert, k_bar3d_frag);
        glGenVertexArrays(1, &bar3d_vao_);
        // Layout: (x, y, z, shade) per vertex; buffer bound per plot at draw time.
        glBindVertexArray(bar3d_vao_);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        // --- 3D surfaces ---
        surface_program_ = build_program(k_surface_vert, k_surface_frag);
        glGenVertexArrays(1, &surface_vao_);
        // Layout: (x, y, z, r, g, b, a) per vertex; buffer bound per plot.
        glBindVertexArray(surface_vao_);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        // --- 3D meshes ---
        surface_tri_program_ = build_program(k_surface_tri_vert, k_surface_tri_frag);
        glGenVertexArrays(1, &surface_tri_vao_);
        // Layout: (x, y, z, r, g, b, a, value, shade) per vertex; buffer bound per
        // plot.
        glBindVertexArray(surface_tri_vao_);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glEnableVertexAttribArray(3);
        glBindVertexArray(0);

        // --- 3D bar edges (world-space ribbons) ---
        // Unit quad as a triangle strip: x picks the segment end, y the side.
        static const float corners[8] = {0.f, -1.f, 0.f, 1.f, 1.f, -1.f, 1.f, 1.f};
        bar3d_edge_program_ = build_program(
            expand_shader(k_bar3d_edge_vert, "RIBBON", k_ribbon_expand).c_str(),
            k_flat_frag);
        glGenVertexArrays(1, &bar3d_edge_vao_);
        glGenBuffers(1, &bar3d_edge_corner_vbo_);
        glBindVertexArray(bar3d_edge_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, bar3d_edge_corner_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glVertexAttribDivisor(0, 0);
        // Attributes 1 and 2 (segment ends) are pointed at each plot's buffer at
        // draw time.
        glBindVertexArray(0);

        // --- 3D paths (mitered world-space ribbons) ---
        // Reuses the bar-edge fragment shader.
        line3d_program_ = build_program(
            expand_shader(k_line3d_vert, "RIBBON", k_ribbon_expand).c_str(),
            k_line3d_frag);
        glGenVertexArrays(1, &line3d_vao_);
        glGenBuffers(1, &line3d_corner_vbo_);
        glBindVertexArray(line3d_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, line3d_corner_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glVertexAttribDivisor(0, 0);
        // Attributes 1..6 are pointed at each path's buffer at draw time.
        glBindVertexArray(0);

        // --- 3D error bars ---
        // Box-space triangles, one buffer per series.
        errbar3d_program_ = build_program(k_errbar3d_vert, k_errbar3d_frag);
        glGenVertexArrays(1, &errbar3d_vao_);

        // --- Depth peeling ---
        // Same vertex shaders; only the fragment shaders differ.
        {
            const std::string peel_bar3d =
                    expand_shader(k_peel_bar3d_frag, "PEEL", k_peel_test);
            const std::string peel_surface =
                    expand_shader(k_peel_surface_frag, "PEEL", k_peel_test);
            const std::string peel_plane =
                    expand_shader(k_peel_plane3d_frag, "PEEL", k_peel_test);
            const std::string peel_flat =
                    expand_shader(k_peel_flat_frag, "PEEL", k_peel_test);
            const std::string peel_scatter3d = expand_shader(
                    scatter3d_marker_source(k_peel_scatter3d_frag).c_str(), "PEEL", k_peel_test);

            peel_bar3d_program_ = build_program(k_bar3d_vert, peel_bar3d.c_str());
            peel_surface_program_ = build_program(k_surface_vert, peel_surface.c_str());
            peel_surface_tri_program_ = build_program(
                k_surface_tri_vert,
                expand_shader(k_peel_surface_tri_frag, "PEEL", k_peel_test).c_str());
            peel_scatter3d_program_ = build_program(k_scatter3d_vert, peel_scatter3d.c_str());
            peel_plane3d_program_ = build_program(k_plane3d_vert, peel_plane.c_str());
            peel_edge_program_ = build_program(
                expand_shader(k_bar3d_edge_vert, "RIBBON", k_ribbon_expand).c_str(),
                peel_flat.c_str());
            peel_line3d_program_ = build_program(
                expand_shader(k_line3d_vert, "RIBBON", k_ribbon_expand).c_str(),
                expand_shader(k_peel_line3d_frag, "PEEL", k_peel_test).c_str());
            peel_errbar3d_program_ = build_program(
                k_errbar3d_vert,
                expand_shader(k_peel_errbar3d_frag, "PEEL", k_peel_test).c_str());

            peel_comp_program_ = build_program(k_peel_comp_vert, k_peel_comp_frag);
            glGenVertexArrays(1, &peel_comp_vao_);
            glGenBuffers(1, &peel_comp_vbo_);
            // Layout: (ndc_x, ndc_y, tx, ty) per vertex, rewritten per composite.
            glBindVertexArray(peel_comp_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, peel_comp_vbo_);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                  reinterpret_cast<void *>(2 * sizeof(float)));
            glBindVertexArray(0);

            // Fixed sampler units: 0 = plane raster, 1 and 2 = depth textures.
            auto peel_samplers = [](unsigned int p) {
                glUseProgram(p);
                const int prev = glGetUniformLocation(p, "uPrevDepth");
                const int opq = glGetUniformLocation(p, "uOpaqueDepth");
                if (prev >= 0) glUniform1i(prev, 1);
                if (opq >= 0) glUniform1i(opq, 2);
            };
            peel_samplers(peel_bar3d_program_);
            peel_samplers(peel_surface_program_);
            peel_samplers(peel_surface_tri_program_);
            peel_samplers(peel_scatter3d_program_);
            peel_samplers(peel_edge_program_);
            peel_samplers(peel_line3d_program_);
            peel_samplers(peel_errbar3d_program_);
            peel_samplers(peel_plane3d_program_);
            glUniform1i(glGetUniformLocation(peel_plane3d_program_, "uTex"), 0);
            glUseProgram(0);
        }

        // Resolve every uniform location once.
        auto loc = [](unsigned int p, const char* n) { return glGetUniformLocation(p, n); };
        line_u_ = {
            loc(line_program_, "uResolution"), loc(line_program_, "uColor"),
            loc(line_program_, "uScale"), loc(line_program_, "uOffset")
        };
        lineseg_u_ = {
            loc(lineseg_program_, "uResolution"), loc(lineseg_program_, "uColor"),
            loc(lineseg_program_, "uScale"), loc(lineseg_program_, "uOffset"),
            loc(lineseg_program_, "uHalfWidth"), loc(lineseg_program_, "uDash"),
            loc(lineseg_program_, "uDashPeriod"), loc(lineseg_program_, "uDistScale")
        };
        quiver_u_ = {
            loc(quiver_program_, "uResolution"), loc(quiver_program_, "uAlpha"),
            loc(quiver_program_, "uScale"), loc(quiver_program_, "uOffset"),
            loc(quiver_program_, "uWidth"), loc(quiver_program_, "uHeadWidth"),
            loc(quiver_program_, "uHeadLength")
        };
        scatter_u_ = {
            loc(scatter_program_, "uResolution"), loc(scatter_program_, "uColor"),
            loc(scatter_program_, "uMarker"),
            loc(scatter_program_, "uScale"), loc(scatter_program_, "uOffset"),
            loc(scatter_program_, "uEdge"), loc(scatter_program_, "uEdgeW"), -1
        };
        scatterz_u_ = {
            loc(scatterz_program_, "uResolution"), -1,
            loc(scatterz_program_, "uMarker"),
            loc(scatterz_program_, "uScale"), loc(scatterz_program_, "uOffset"),
            loc(scatterz_program_, "uEdge"), loc(scatterz_program_, "uEdgeW"),
            loc(scatterz_program_, "uEdgeFace")
        };
        heatmap_u_ = {loc(heatmap_program_, "uResolution"), loc(heatmap_program_, "uTex")};
        heattri_u_ = {loc(heattri_program_, "uResolution"), loc(heattri_program_, "uCmap"),
                      loc(heattri_program_, "uCyclic"), loc(heattri_program_, "uNorm"),
                      loc(heattri_program_, "uN"), loc(heattri_program_, "uBands"),
                      loc(heattri_program_, "uNB")};
        plane3d_u_ = {
            loc(plane3d_program_, "uClip"), loc(plane3d_program_, "uTex"),
            loc(plane3d_program_, "uAlpha"), loc(plane3d_program_, "uBoxScale"),
            loc(plane3d_program_, "uBoxOffset")
        };
        plane_comp_u_ = {
            loc(plane_comp_program_, "uClip"), loc(plane_comp_program_, "uCount"),
            loc(plane_comp_program_, "uSlot"), loc(plane_comp_program_, "uOrigin[0]"),
            loc(plane_comp_program_, "uDU[0]"), loc(plane_comp_program_, "uDV[0]"),
            loc(plane_comp_program_, "uAxis[0]"), loc(plane_comp_program_, "uAlpha[0]")
        };
        surface_u_ = {
            loc(surface_program_, "uClip"), loc(surface_program_, "uBoxScale"),
            loc(surface_program_, "uBoxOffset")
        };
        scatter3d_u_ = {
            loc(scatter3d_program_, "uClip"), loc(scatter3d_program_, "uBoxScale"),
            loc(scatter3d_program_, "uBoxOffset"), loc(scatter3d_program_, "uResolution"),
            loc(scatter3d_program_, "uMarker"), loc(scatter3d_program_, "uDepth"),
            loc(scatter3d_program_, "uShade"),
            loc(scatter3d_program_, "uEdge"), loc(scatter3d_program_, "uEdgeW"),
            loc(scatter3d_program_, "uEdgeFace")
        };
        bar3d_u_ = {
            loc(bar3d_program_, "uClip"), loc(bar3d_program_, "uColor"),
            loc(bar3d_program_, "uBoxScale"), loc(bar3d_program_, "uBoxOffset")
        };
        bar3d_edge_u_ = {
            loc(bar3d_edge_program_, "uClip"), loc(bar3d_edge_program_, "uColor"),
            loc(bar3d_edge_program_, "uBoxScale"), loc(bar3d_edge_program_, "uBoxOffset"),
            loc(bar3d_edge_program_, "uEye"), loc(bar3d_edge_program_, "uPersp"),
            loc(bar3d_edge_program_, "uHalfWidth")
        };

        // The peel programs' own uniform locations.
        peel_plane3d_u_ = {
            loc(peel_plane3d_program_, "uClip"),
            loc(peel_plane3d_program_, "uTex"),
            loc(peel_plane3d_program_, "uAlpha"),
            loc(peel_plane3d_program_, "uBoxScale"),
            loc(peel_plane3d_program_, "uBoxOffset")
        };
        peel_surface_u_ = {
            loc(peel_surface_program_, "uClip"),
            loc(peel_surface_program_, "uBoxScale"),
            loc(peel_surface_program_, "uBoxOffset")
        };
        surface_tri_u_ = {
            loc(surface_tri_program_, "uClip"),
            loc(surface_tri_program_, "uBoxScale"),
            loc(surface_tri_program_, "uBoxOffset"),
            loc(surface_tri_program_, "uCmap"),
            loc(surface_tri_program_, "uColormapped")
        };
        peel_surface_tri_u_ = {
            loc(peel_surface_tri_program_, "uClip"),
            loc(peel_surface_tri_program_, "uBoxScale"),
            loc(peel_surface_tri_program_, "uBoxOffset"),
            loc(peel_surface_tri_program_, "uCmap"),
            loc(peel_surface_tri_program_, "uColormapped")
        };
        peel_scatter3d_u_ = {
            loc(peel_scatter3d_program_, "uClip"),
            loc(peel_scatter3d_program_, "uBoxScale"),
            loc(peel_scatter3d_program_, "uBoxOffset"),
            loc(peel_scatter3d_program_, "uResolution"),
            loc(peel_scatter3d_program_, "uMarker"),
            loc(peel_scatter3d_program_, "uDepth"),
            loc(peel_scatter3d_program_, "uShade"),
            loc(peel_scatter3d_program_, "uEdge"), loc(peel_scatter3d_program_, "uEdgeW"),
            loc(peel_scatter3d_program_, "uEdgeFace")
        };
        peel_bar3d_u_ = {
            loc(peel_bar3d_program_, "uClip"),
            loc(peel_bar3d_program_, "uColor"),
            loc(peel_bar3d_program_, "uBoxScale"),
            loc(peel_bar3d_program_, "uBoxOffset")
        };
        peel_edge_u_ = {
            loc(peel_edge_program_, "uClip"), loc(peel_edge_program_, "uColor"),
            loc(peel_edge_program_, "uBoxScale"), loc(peel_edge_program_, "uBoxOffset"),
            loc(peel_edge_program_, "uEye"), loc(peel_edge_program_, "uPersp"),
            loc(peel_edge_program_, "uHalfWidth")
        };
        line3d_u_ = {
            loc(line3d_program_, "uClip"), loc(line3d_program_, "uBoxScale"),
            loc(line3d_program_, "uBoxOffset"), loc(line3d_program_, "uEye"),
            loc(line3d_program_, "uPersp"), loc(line3d_program_, "uHalfWidth"),
            loc(line3d_program_, "uDepth"), loc(line3d_program_, "uShade"),
            loc(line3d_program_, "uCmap"), loc(line3d_program_, "uColormapped")
        };
        peel_line3d_u_ = {
            loc(peel_line3d_program_, "uClip"),
            loc(peel_line3d_program_, "uBoxScale"),
            loc(peel_line3d_program_, "uBoxOffset"),
            loc(peel_line3d_program_, "uEye"),
            loc(peel_line3d_program_, "uPersp"),
            loc(peel_line3d_program_, "uHalfWidth"),
            loc(peel_line3d_program_, "uDepth"),
            loc(peel_line3d_program_, "uShade"),
            loc(peel_line3d_program_, "uCmap"),
            loc(peel_line3d_program_, "uColormapped")
        };
        errbar3d_u_ = {
            loc(errbar3d_program_, "uClip"), loc(errbar3d_program_, "uDepth"),
            loc(errbar3d_program_, "uShade")
        };
        peel_errbar3d_u_ = {
            loc(peel_errbar3d_program_, "uClip"),
            loc(peel_errbar3d_program_, "uDepth"),
            loc(peel_errbar3d_program_, "uShade")
        };
        peel_comp_u_ = {
            loc(peel_comp_program_, "uTex"),
            loc(peel_comp_program_, "uFlipAlpha")
        };
    }

    DataRenderer::~DataRenderer() {
        if (line_vbo_) glDeleteBuffers(1, &line_vbo_);
        if (line_vao_) glDeleteVertexArrays(1, &line_vao_);
        if (line_program_) glDeleteProgram(line_program_);

        if (lineseg_corner_vbo_) glDeleteBuffers(1, &lineseg_corner_vbo_);
        if (ref_vbo_) glDeleteBuffers(1, &ref_vbo_);
        if (fill_edge_vbo_) glDeleteBuffers(1, &fill_edge_vbo_);
        if (fill_dist_vbo_) glDeleteBuffers(1, &fill_dist_vbo_);
        if (lineseg_vao_) glDeleteVertexArrays(1, &lineseg_vao_);
        if (lineseg_program_) glDeleteProgram(lineseg_program_);

        if (quiver_corner_vbo_) glDeleteBuffers(1, &quiver_corner_vbo_);
        if (quiver_vao_) glDeleteVertexArrays(1, &quiver_vao_);
        if (quiver_program_) glDeleteProgram(quiver_program_);

        if (scatter_inst_vbo_) glDeleteBuffers(1, &scatter_inst_vbo_);
        if (scatter_quad_vbo_) glDeleteBuffers(1, &scatter_quad_vbo_);
        if (scatter_vao_) glDeleteVertexArrays(1, &scatter_vao_);
        if (scatter_program_) glDeleteProgram(scatter_program_);

        if (scatterz_inst_vbo_) glDeleteBuffers(1, &scatterz_inst_vbo_);
        if (scatterz_quad_vbo_) glDeleteBuffers(1, &scatterz_quad_vbo_);
        if (scatterz_vao_) glDeleteVertexArrays(1, &scatterz_vao_);
        if (scatterz_program_) glDeleteProgram(scatterz_program_);

        if (heatmap_vbo_) glDeleteBuffers(1, &heatmap_vbo_);
        if (heatmap_ebo_) glDeleteBuffers(1, &heatmap_ebo_);
        if (heatmap_vao_) glDeleteVertexArrays(1, &heatmap_vao_);
        if (heatmap_program_) glDeleteProgram(heatmap_program_);

        if (heattri_vbo_) glDeleteBuffers(1, &heattri_vbo_);
        if (heattri_vao_) glDeleteVertexArrays(1, &heattri_vao_);
        if (heattri_program_) glDeleteProgram(heattri_program_);
        if (heattri_cmap_tex_) glDeleteTextures(1, &heattri_cmap_tex_);
        if (heattri_bands_tex_) glDeleteTextures(1, &heattri_bands_tex_);

        if (plane_comp_vbo_) glDeleteBuffers(1, &plane_comp_vbo_);
        if (plane_comp_vao_) glDeleteVertexArrays(1, &plane_comp_vao_);
        if (plane_comp_program_) glDeleteProgram(plane_comp_program_);

        if (plane3d_vbo_) glDeleteBuffers(1, &plane3d_vbo_);
        if (plane3d_vao_) glDeleteVertexArrays(1, &plane3d_vao_);
        if (plane3d_program_) glDeleteProgram(plane3d_program_);

        for (auto& [k, e]: plane_raster_cache_) {
            if (e.fbo) glDeleteFramebuffers(1, &e.fbo);
            if (e.tex) glDeleteTextures(1, &e.tex);
        }

        if (surface_vao_) glDeleteVertexArrays(1, &surface_vao_);
        if (surface_program_) glDeleteProgram(surface_program_);
        if (surface_tri_vao_) glDeleteVertexArrays(1, &surface_tri_vao_);
        if (surface_tri_program_) glDeleteProgram(surface_tri_program_);
        if (peel_surface_tri_program_) glDeleteProgram(peel_surface_tri_program_);
        if (surface_tri_cmap_tex_) glDeleteTextures(1, &surface_tri_cmap_tex_);
        for (auto& [k, e]: surface_tri_cache_) {
            if (e.vbo) glDeleteBuffers(1, &e.vbo);
            if (e.edge_vbo) glDeleteBuffers(1, &e.edge_vbo);
            if (e.index_ebo) glDeleteBuffers(1, &e.index_ebo);
        }
        for (auto& [k, e]: surface_cache_) {
            if (e.vbo) glDeleteBuffers(1, &e.vbo);
            if (e.edge_vbo) glDeleteBuffers(1, &e.edge_vbo);
            if (e.index_ebo) glDeleteBuffers(1, &e.index_ebo);
        }

        for (auto& [k, e]: scatter3d_cache_) {
            if (e.vbo) glDeleteBuffers(1, &e.vbo);
            if (e.order_vbo) glDeleteBuffers(1, &e.order_vbo);
        }
        if (scatter3d_vao_) glDeleteVertexArrays(1, &scatter3d_vao_);
        if (scatter3d_corner_vbo_) glDeleteBuffers(1, &scatter3d_corner_vbo_);
        if (scatter3d_program_) glDeleteProgram(scatter3d_program_);

        for (auto& [k, e]: line3d_cache_) {
            if (e.vbo) glDeleteBuffers(1, &e.vbo);
            if (e.order_vbo) glDeleteBuffers(1, &e.order_vbo);
        }
        if (line3d_vao_) glDeleteVertexArrays(1, &line3d_vao_);
        if (line3d_corner_vbo_) glDeleteBuffers(1, &line3d_corner_vbo_);
        if (line3d_cmap_tex_) glDeleteTextures(1, &line3d_cmap_tex_);
        if (line3d_program_) glDeleteProgram(line3d_program_);
        if (peel_line3d_program_) glDeleteProgram(peel_line3d_program_);
        for (auto& [k, e]: errbar3d_cache_) {
            if (e.opaque_vbo) glDeleteBuffers(1, &e.opaque_vbo);
            if (e.trans_vbo) glDeleteBuffers(1, &e.trans_vbo);
        }
        if (errbar3d_vao_) glDeleteVertexArrays(1, &errbar3d_vao_);
        if (errbar3d_program_) glDeleteProgram(errbar3d_program_);
        if (peel_errbar3d_program_) glDeleteProgram(peel_errbar3d_program_);

        if (peel_scatter3d_program_) glDeleteProgram(peel_scatter3d_program_);
        if (peel_bar3d_program_) glDeleteProgram(peel_bar3d_program_);
        if (peel_surface_program_) glDeleteProgram(peel_surface_program_);
        if (peel_plane3d_program_) glDeleteProgram(peel_plane3d_program_);
        if (peel_edge_program_) glDeleteProgram(peel_edge_program_);
        if (peel_comp_vbo_) glDeleteBuffers(1, &peel_comp_vbo_);
        if (peel_comp_vao_) glDeleteVertexArrays(1, &peel_comp_vao_);
        if (peel_comp_program_) glDeleteProgram(peel_comp_program_);
        if (peel_.fbo) glDeleteFramebuffers(1, &peel_.fbo);
        if (peel_.accum_fbo) glDeleteFramebuffers(1, &peel_.accum_fbo);
        if (peel_.copy_fbo) glDeleteFramebuffers(1, &peel_.copy_fbo);
        if (peel_.layer_tex) glDeleteTextures(1, &peel_.layer_tex);
        if (peel_.accum_tex) glDeleteTextures(1, &peel_.accum_tex);
        if (peel_.opaque_tex) glDeleteTextures(1, &peel_.opaque_tex);
        if (peel_.depth_tex[0]) glDeleteTextures(2, peel_.depth_tex);
        if (peel_.z_tex[0]) glDeleteTextures(2, peel_.z_tex);
        if (peel_.query) glDeleteQueries(1, &peel_.query);

        if (bar3d_vao_) glDeleteVertexArrays(1, &bar3d_vao_);
        if (bar3d_program_) glDeleteProgram(bar3d_program_);
        if (bar3d_edge_corner_vbo_) glDeleteBuffers(1, &bar3d_edge_corner_vbo_);
        if (bar3d_edge_vao_) glDeleteVertexArrays(1, &bar3d_edge_vao_);
        if (bar3d_edge_program_) glDeleteProgram(bar3d_edge_program_);
        for (auto& [k, e]: bar3d_cache_) {
            if (e.vbo) glDeleteBuffers(1, &e.vbo);
            if (e.edge_vbo) glDeleteBuffers(1, &e.edge_vbo);
            if (e.index_ebo) glDeleteBuffers(1, &e.index_ebo);
        }

        for (auto& [k, e]: line_cache_) {
            if (e.vbo) glDeleteBuffers(1, &e.vbo);
            if (e.dist_vbo) glDeleteBuffers(1, &e.dist_vbo);
        }
        for (auto& [k, e]: heat_cache_) if (e.tex) glDeleteTextures(1, &e.tex);
        for (auto& [k, e]: scatter_cache_) if (e.vbo) glDeleteBuffers(1, &e.vbo);
        for (auto& [k, e]: scatterz_cache_) if (e.vbo) glDeleteBuffers(1, &e.vbo);
        for (auto& [k, e]: bar_cache_) {
            if (e.fill_vbo) glDeleteBuffers(1, &e.fill_vbo);
            if (e.edge_vbo) glDeleteBuffers(1, &e.edge_vbo);
        }
        for (auto& [k, e]: fill_cache_) if (e.vbo) glDeleteBuffers(1, &e.vbo);
        for (auto& [k, e]: quiver_cache_) if (e.vbo) glDeleteBuffers(1, &e.vbo);
        for (auto* m: {
                 &line_err_cache_, &bar_err_cache_,
                 &scatter_err_cache_, &scatterz_err_cache_
             })
            for (auto& [k, e]: *m) {
                if (e.fill_vbo) glDeleteBuffers(1, &e.fill_vbo);
                if (e.stroke_vbo) glDeleteBuffers(1, &e.stroke_vbo);
            }
    }

    // -------------------------------------------------------------------------
    // draw_lines
    // -------------------------------------------------------------------------
    void DataRenderer::draw_lines(const std::vector<LinePlot>& lines,
                                  std::span<const std::size_t> which,
                                  const CoordTransform& tr,
                                  const PlotRect& pr) {
        if (which.empty()) return;

        begin_pass(pr, tr.win_h);
        glUseProgram(lineseg_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(lineseg_u_.resolution, 1, res);
        glBindVertexArray(lineseg_vao_);

        std::vector<float> pts, dists;
        StrokePath path;
        PaintOnce once;
        for (const std::size_t li : which) {
            const auto& lp = lines[li];
            if (lp.x.size() < 2) continue;
            // LineStyle::None: no stroke.
            if (lp.opts.linestyle == LineStyle::None) continue;

            const CacheKey key{axes_index_, plane_index_, static_cast<int>(li)};
            LineCache& e = line_cache_[key];

            // The stroked vertices (the steps, if any); built on first use below.
            bool have_path = false;
            auto stroke_path = [&]() -> const StrokePath& {
                if (!have_path)
                    path.build(lp.x.data(), lp.y.data(), lp.x.size(), lp.opts.drawstyle,
                               lp.opts.loop);
                have_path = true;
                return path;
            };

            // The buffer holds only points; view and width are uniforms. Rebuilt on
            // data change or when leaving the float-precision regime.
            const bool precise = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                                 && float_resolves_view(e.span_y, tr.syspan(), tr.ph);
            const bool usable = e.vbo != 0
                                && e.segments > 0
                                && e.data_generation != 0
                                && e.data_generation == data_generation_
                                && same_scales(e.tr, tr)
                                && e.loop == lp.opts.loop
                                && e.drawstyle == lp.opts.drawstyle
                                && (e.data_space
                                        ? precise
                                        : (!precise && same_view(e.tr, tr)));
            if (!usable) {
                const StrokePath& sp = stroke_path();
                double xlo = tr.sx(sp.x[0]), xhi = xlo, ylo = tr.sy(sp.y[0]), yhi = ylo;
                for (std::size_t i = 1; i < sp.n; ++i) {
                    xlo = std::min(xlo, tr.sx(sp.x[i]));
                    xhi = std::max(xhi, tr.sx(sp.x[i]));
                    ylo = std::min(ylo, tr.sy(sp.y[i]));
                    yhi = std::max(yhi, tr.sy(sp.y[i]));
                }
                e.anchor_x = 0.5 * (xlo + xhi);
                e.span_x = xhi - xlo;
                e.anchor_y = 0.5 * (ylo + yhi);
                e.span_y = yhi - ylo;
                e.data_space = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                               && float_resolves_view(e.span_y, tr.syspan(), tr.ph);

                // Padded so instance i reads points [i, i+3]. An open path repeats
                // its ends (butt caps); a looped path pads with the real neighbours
                // across the seam (mitered).
                const std::size_t n = sp.n;
                pts.clear();
                pts.reserve((n + 3) * 2);
                auto emit = [&](std::size_t i) {
                    if (e.data_space) {
                        pts.push_back(static_cast<float>(tr.sx(sp.x[i]) - e.anchor_x));
                        pts.push_back(static_cast<float>(tr.sy(sp.y[i]) - e.anchor_y));
                    } else {
                        pts.push_back(tr.to_px(sp.x[i]));
                        pts.push_back(tr.to_py(sp.y[i]));
                    }
                };
                emit(lp.opts.loop ? n - 1 : 0);
                for (std::size_t i = 0; i < n; ++i) emit(i);
                if (lp.opts.loop) {
                    emit(0);
                    emit(1 % n);
                } else emit(n - 1);

                if (e.vbo == 0) glGenBuffers(1, &e.vbo);
                glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(pts.size() * sizeof(float)),
                             pts.data(), GL_DYNAMIC_DRAW);

                e.segments = static_cast<int>(sp.segment_count());
                e.loop = lp.opts.loop;
                e.drawstyle = lp.opts.drawstyle;
                e.data_generation = data_generation_;
                e.tr = tr;
                e.dist_valid = false; // arc lengths follow the points
            }
            if (e.segments <= 0) continue;

            // Four windows into the point array; the VAO records the buffer, so
            // redo this per entry.
            glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
            for (int a = 1; a <= 4; ++a) {
                glEnableVertexAttribArray(static_cast<unsigned>(a));
                glVertexAttribPointer(static_cast<unsigned>(a), 2, GL_FLOAT, GL_FALSE,
                                      2 * sizeof(float),
                                      reinterpret_cast<void *>((a - 1) * 2 * sizeof(float)));
                glVertexAttribDivisor(static_cast<unsigned>(a), 1);
            }

            const DataToPixel m = e.data_space
                                      ? data_to_pixel(tr, e.anchor_x, e.anchor_y)
                                      : DataToPixel{{1.0f, 1.0f}, {0.0f, 0.0f}};
            glUniform2fv(lineseg_u_.scale, 1, m.scale);
            glUniform2fv(lineseg_u_.offset, 1, m.offset);
            glUniform1f(lineseg_u_.half_width, lp.opts.linewidth * 0.5f);

            // --- dash phase -----------------------------------------------------
            const DashPattern dash = dash_pattern(lp.opts.linestyle);
            if (!dash.dashed()) {
                // Solid: disable attribute 5 (the shader reads the constant). Done
                // every time because enable state lives in the VAO.
                glDisableVertexAttribArray(5);
                glVertexAttrib1f(5, 0.0f);
                glUniform1f(lineseg_u_.dash_period, 0.0f);
            } else {
                // Reuse arc lengths if the scale changed uniformly (see
                // LineCache::dist_vbo).
                float dist_scale = 1.0f;
                bool reusable = e.dist_valid && e.dist_vbo != 0
                                && e.dist_ref_sx != 0.0f && e.dist_ref_sy != 0.0f;
                if (reusable) {
                    const float rx = m.scale[0] / e.dist_ref_sx;
                    const float ry = m.scale[1] / e.dist_ref_sy;
                    const float mag = std::max(std::fabs(rx), std::fabs(ry));
                    if (rx > 0.0f && ry > 0.0f
                        && std::fabs(rx - ry) <= 1e-4f * mag) {
                        dist_scale = 0.5f * (rx + ry);
                    } else {
                        reusable = false;
                    }
                }

                if (!reusable) {
                    // Accumulated in double from the same float positions the
                    // shader sees, so prefixes agree at joins.
                    const StrokePath& sp = stroke_path();
                    const std::size_t n = sp.n;
                    dists.clear();
                    dists.reserve(n);
                    double acc = 0.0;
                    float prev_x = 0.0f, prev_y = 0.0f;
                    for (std::size_t i = 0; i < n; ++i) {
                        float fx, fy;
                        if (e.data_space) {
                            fx = static_cast<float>(tr.sx(sp.x[i]) - e.anchor_x);
                            fy = static_cast<float>(tr.sy(sp.y[i]) - e.anchor_y);
                        } else {
                            fx = tr.to_px(sp.x[i]);
                            fy = tr.to_py(sp.y[i]);
                        }
                        const float px = m.scale[0] * fx + m.offset[0];
                        const float py = m.scale[1] * fy + m.offset[1];
                        if (i > 0) {
                            const double dx = px - prev_x, dy = py - prev_y;
                            acc += std::sqrt(dx * dx + dy * dy);
                        }
                        dists.push_back(static_cast<float>(acc));
                        prev_x = px;
                        prev_y = py;
                    }

                    if (e.dist_vbo == 0) glGenBuffers(1, &e.dist_vbo);
                    glBindBuffer(GL_ARRAY_BUFFER, e.dist_vbo);
                    glBufferData(GL_ARRAY_BUFFER,
                                 static_cast<GLsizeiptr>(dists.size() * sizeof(float)),
                                 dists.data(), GL_DYNAMIC_DRAW);
                    e.dist_ref_sx = m.scale[0];
                    e.dist_ref_sy = m.scale[1];
                    e.dist_valid = true;
                    dist_scale = 1.0f;
                }

                // Instance i reads dists[i] (no padding needed).
                glBindBuffer(GL_ARRAY_BUFFER, e.dist_vbo);
                glEnableVertexAttribArray(5);
                glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
                glVertexAttribDivisor(5, 1);

                glUniform4fv(lineseg_u_.dash, 1, dash.seg);
                glUniform1f(lineseg_u_.dash_period, dash.period);
                glUniform1f(lineseg_u_.dist_scale, dist_scale);
            }

            const auto& c = *lp.opts.color;
            glUniform4f(lineseg_u_.color,
                        c.r, c.g, c.b, c.a * lp.opts.alpha);
            once.stroke(c.a * lp.opts.alpha < 1.0f);
            glDrawArraysInstanced(GL_TRIANGLES, 0, k_lineseg_vertices, e.segments);
        }
        once.finish();

        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // draw_scatter
    // -------------------------------------------------------------------------
    void DataRenderer::draw_scatter(const std::vector<ScatterPlot>& scatters,
                                    std::span<const std::size_t> which,
                                    const CoordTransform& tr,
                                    const PlotRect& pr) {
        if (which.empty()) return;

        begin_pass(pr, tr.win_h);
        glUseProgram(scatter_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(scatter_u_.resolution, 1, res);
        glBindVertexArray(scatter_vao_);

        std::vector<float> inst;
        for (const std::size_t si : which) {
            const auto& sp = scatters[si];
            if (sp.x.empty()) continue;

            const CacheKey key{axes_index_, plane_index_, static_cast<int>(si)};
            InstanceCache& e = scatter_cache_[key];

            // Only the pixel-space fallback goes stale on a view change.
            const bool precise = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                                 && float_resolves_view(e.span_y, tr.syspan(), tr.ph);
            const bool usable = e.vbo != 0
                                && e.instances > 0
                                && e.data_generation != 0
                                && e.data_generation == data_generation_
                                && same_scales(e.tr, tr)
                                && e.size == sp.opts.size
                                && (e.data_space
                                        ? precise
                                        : (!precise && same_view(e.tr, tr)));

            if (!usable) {
                // Anchor at the data extent's midpoint (residuals <= half the span),
                // independent of the view.
                double xlo = tr.sx(sp.x[0]), xhi = xlo, ylo = tr.sy(sp.y[0]), yhi = ylo;
                for (std::size_t i = 1; i < sp.x.size(); ++i) {
                    xlo = std::min(xlo, tr.sx(sp.x[i]));
                    xhi = std::max(xhi, tr.sx(sp.x[i]));
                    ylo = std::min(ylo, tr.sy(sp.y[i]));
                    yhi = std::max(yhi, tr.sy(sp.y[i]));
                }
                e.anchor_x = 0.5 * (xlo + xhi);
                e.span_x = xhi - xlo;
                e.anchor_y = 0.5 * (ylo + yhi);
                e.span_y = yhi - ylo;
                e.data_space = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                               && float_resolves_view(e.span_y, tr.syspan(), tr.ph);

                inst.clear();
                inst.reserve(sp.x.size() * 3);
                for (std::size_t i = 0; i < sp.x.size(); ++i) {
                    if (e.data_space) {
                        inst.push_back(static_cast<float>(tr.sx(sp.x[i]) - e.anchor_x));
                        inst.push_back(static_cast<float>(tr.sy(sp.y[i]) - e.anchor_y));
                    } else {
                        inst.push_back(tr.to_px(sp.x[i]));
                        inst.push_back(tr.to_py(sp.y[i]));
                    }
                    inst.push_back(marker_size_at(sp, i));   // 0 hides a point
                }

                if (e.vbo == 0) glGenBuffers(1, &e.vbo);
                glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(inst.size() * sizeof(float)),
                             inst.data(), GL_DYNAMIC_DRAW);

                e.instances = static_cast<int>(sp.x.size());
                e.data_generation = data_generation_;
                e.size = sp.opts.size;
                e.tr = tr;
            }

            // Re-point the instance attributes at this entry's buffer.
            glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
            glVertexAttribDivisor(1, 1);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                                  reinterpret_cast<void *>(2 * sizeof(float)));
            glVertexAttribDivisor(2, 1);

            // Identity transform in the fallback (centres are already pixels).
            const DataToPixel m = e.data_space
                                      ? data_to_pixel(tr, e.anchor_x, e.anchor_y)
                                      : DataToPixel{{1.0f, 1.0f}, {0.0f, 0.0f}};
            glUniform2fv(scatter_u_.scale, 1, m.scale);
            glUniform2fv(scatter_u_.offset, 1, m.offset);

            const auto& c = *sp.opts.color;
            glUniform4f(scatter_u_.color,
                        c.r, c.g, c.b, c.a * marker_face_alpha(sp.opts));
            glUniform1i(scatter_u_.marker,
                        static_cast<int>(sp.opts.marker));
            set_marker_edge(scatter_u_.edge, scatter_u_.edge_w, scatter_u_.edge_face,
                            marker_edge(sp.opts.edgecolor, sp.opts.alpha,
                                        sp.opts.edge_linewidth, c));

            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, e.instances);
        }

        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // draw_scatter_z
    // -------------------------------------------------------------------------
    void DataRenderer::draw_scatter_z(const std::vector<ScatterZPlot>& points,
                                      std::span<const std::size_t> which,
                                      const CoordTransform& tr,
                                      const PlotRect& pr) {
        if (which.empty()) return;

        begin_pass(pr, tr.win_h);
        glUseProgram(scatterz_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(scatterz_u_.resolution, 1, res);
        glBindVertexArray(scatterz_vao_);

        std::vector<float> inst;
        for (const std::size_t si : which) {
            const auto& sp = points[si];
            if (sp.x.empty()) continue;

            const CacheKey key{axes_index_, plane_index_, static_cast<int>(si)};
            InstanceCache& e = scatterz_cache_[key];

            const bool precise = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                                 && float_resolves_view(e.span_y, tr.syspan(), tr.ph);
            // The colormap is baked into the buffer, so it's in the key.
            const bool usable = e.vbo != 0
                                && e.instances > 0
                                && e.data_generation != 0
                                && e.data_generation == data_generation_
                                && same_scales(e.tr, tr)
                                && e.size == sp.opts.size
                                && e.cmap == sp.opts.cmap
                                && e.vmin == sp.opts.vmin && e.vmax == sp.opts.vmax
                                && e.norm == sp.opts.norm
                                && e.alpha == marker_face_alpha(sp.opts)
                                && (e.data_space
                                        ? precise
                                        : (!precise && same_view(e.tr, tr)));

            if (!usable) {
                double xlo = tr.sx(sp.x[0]), xhi = xlo, ylo = tr.sy(sp.y[0]), yhi = ylo;
                for (std::size_t i = 1; i < sp.x.size(); ++i) {
                    xlo = std::min(xlo, tr.sx(sp.x[i]));
                    xhi = std::max(xhi, tr.sx(sp.x[i]));
                    ylo = std::min(ylo, tr.sy(sp.y[i]));
                    yhi = std::max(yhi, tr.sy(sp.y[i]));
                }
                e.anchor_x = 0.5 * (xlo + xhi);
                e.span_x = xhi - xlo;
                e.anchor_y = 0.5 * (ylo + yhi);
                e.span_y = yhi - ylo;
                e.data_space = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                               && float_resolves_view(e.span_y, tr.syspan(), tr.ph);

                // Per-point color via the colormap LUT.
                const uint8_t* lut = colormaps::get(sp.opts.cmap);
                const float vmin = sp.opts.vmin, vrange = sp.opts.vmax - sp.opts.vmin;
                const ColorNorm::Kind kind = norm_in_force(sp.opts.norm, sp.opts.vmin, sp.opts.vmax);

                inst.clear();
                inst.reserve(sp.x.size() * 7);
                for (std::size_t i = 0; i < sp.x.size(); ++i) {
                    const double t = kind == ColorNorm::Kind::Linear
                                         ? ((vrange != 0.0f) ? (sp.z[i] - vmin) / vrange : 0.0)
                                         : norm_t(kind, sp.opts.norm, sp.opts.vmin, sp.opts.vmax, sp.z[i]);
                    const uint8_t* c = &lut[colormaps::index(t, sp.opts.cmap.is_cyclic()) * 4];

                    if (e.data_space) {
                        inst.push_back(static_cast<float>(tr.sx(sp.x[i]) - e.anchor_x));
                        inst.push_back(static_cast<float>(tr.sy(sp.y[i]) - e.anchor_y));
                    } else {
                        inst.push_back(tr.to_px(sp.x[i]));
                        inst.push_back(tr.to_py(sp.y[i]));
                    }
                    inst.push_back(marker_size_at(sp, i));   // 0 hides a point
                    inst.push_back(static_cast<float>(c[0]) / 255.0f);
                    inst.push_back(static_cast<float>(c[1]) / 255.0f);
                    inst.push_back(static_cast<float>(c[2]) / 255.0f);
                    inst.push_back(static_cast<float>(c[3]) / 255.0f * marker_face_alpha(sp.opts));
                }

                if (e.vbo == 0) glGenBuffers(1, &e.vbo);
                glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(inst.size() * sizeof(float)),
                             inst.data(), GL_DYNAMIC_DRAW);

                e.instances = static_cast<int>(sp.x.size());
                e.data_generation = data_generation_;
                e.size = sp.opts.size;
                e.cmap = sp.opts.cmap;
                e.vmin = sp.opts.vmin;
                e.vmax = sp.opts.vmax;
                e.norm = sp.opts.norm;
                e.alpha = marker_face_alpha(sp.opts);
                e.tr = tr;
            }

            glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
            glVertexAttribDivisor(1, 1);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                                  reinterpret_cast<void *>(2 * sizeof(float)));
            glVertexAttribDivisor(2, 1);
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                                  reinterpret_cast<void *>(3 * sizeof(float)));
            glVertexAttribDivisor(3, 1);

            const DataToPixel m = e.data_space
                                      ? data_to_pixel(tr, e.anchor_x, e.anchor_y)
                                      : DataToPixel{{1.0f, 1.0f}, {0.0f, 0.0f}};
            glUniform2fv(scatterz_u_.scale, 1, m.scale);
            glUniform2fv(scatterz_u_.offset, 1, m.offset);

            glUniform1i(scatterz_u_.marker,
                        static_cast<int>(sp.opts.marker));
            set_marker_edge(scatterz_u_.edge, scatterz_u_.edge_w, scatterz_u_.edge_face,
                            marker_edge(sp.opts.edgecolor, sp.opts.alpha,
                                        sp.opts.edge_linewidth, Color{0.0f, 0.0f, 0.0f, 1.0f}));

            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, e.instances);
        }

        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // draw_bars
    // -------------------------------------------------------------------------
    void DataRenderer::draw_bars(const std::vector<BarPlot>& bars,
                                 std::span<const std::size_t> which,
                                 const CoordTransform& tr,
                                 const PlotRect& pr) {
        if (which.empty()) return;

        begin_pass(pr, tr.win_h);
        glUseProgram(line_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(line_u_.resolution, 1, res);
        glBindVertexArray(line_vao_);

        // Re-point the VAO attribute at the bound buffer (a VAO remembers the
        // buffer from specification time).
        auto point_attrib_at = [](unsigned int vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        };

        std::vector<float> scratch;
        for (const std::size_t bi : which) {
            const auto& bp = bars[bi];
            if (bp.centers.empty()) continue;

            const double half = bp.bar_width * 0.5;
            const std::size_t n = bp.centers.size();

            // Along the category axis (x standing, y lying) and the value axis:
            // scale space, and pixels.
            const bool lying = bp.horizontal();
            auto sc = [&](double v) { return lying ? tr.sy(v) : tr.sx(v); };
            auto sv = [&](double v) { return lying ? tr.sx(v) : tr.sy(v); };
            auto pc = [&](double v) { return lying ? tr.to_py(v) : tr.to_px(v); };
            auto pv = [&](double v) { return lying ? tr.to_px(v) : tr.to_py(v); };
            // A rect from category edges c0, c1 and value ends v0 (base), v1 (tip).
            struct Rect { float xl, xr, yb, yt; };
            auto rect = [&](float c0, float c1, float v0, float v1) {
                return lying ? Rect{v0, v1, c0, c1} : Rect{c0, c1, v0, v1};
            };

            const CacheKey key{axes_index_, plane_index_, static_cast<int>(bi)};
            BarCache& e = bar_cache_[key];

            // Read before the fill branch overwrites e.bar_width, so a width edit
            // (or an orientation one) rebuilds the outline.
            const bool width_same = e.bar_width == bp.bar_width && e.horizontal == lying;

            // --- filled rectangles (2 triangles = 6 verts each), in data space ---
            const bool precise = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                                 && float_resolves_view(e.span_y, tr.syspan(), tr.ph);
            const bool fill_ok = e.fill_vbo != 0
                                 && e.fill_verts > 0
                                 && e.fill_generation != 0
                                 && e.fill_generation == data_generation_
                                 && same_scales(e.fill_tr, tr)
                                 && width_same
                                 && (e.data_space
                                         ? precise
                                         : (!precise && same_view(e.fill_tr, tr)));
            if (!fill_ok) {
                // Extent includes the bases (0 without `bottom`). In scale space, as
                // uploaded.
                double clo = sc(bp.centers[0] - half), chi = sc(bp.centers[0] + half);
                double vlo = sv(bp.base_at(0)), vhi = vlo;
                for (std::size_t i = 0; i < n; ++i) {
                    clo = std::min(clo, sc(bp.centers[i] - half));
                    chi = std::max(chi, sc(bp.centers[i] + half));
                    if (!bp.bottom.empty()) {
                        vlo = std::min(vlo, sv(bp.base_at(i)));
                        vhi = std::max(vhi, sv(bp.base_at(i)));
                    }
                    vlo = std::min(vlo, sv(bp.tip_at(i)));
                    vhi = std::max(vhi, sv(bp.tip_at(i)));
                }
                const double anchor_c = 0.5 * (clo + chi), anchor_v = 0.5 * (vlo + vhi);
                e.anchor_x = lying ? anchor_v : anchor_c;
                e.span_x = lying ? vhi - vlo : chi - clo;
                e.anchor_y = lying ? anchor_c : anchor_v;
                e.span_y = lying ? chi - clo : vhi - vlo;
                e.data_space = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                               && float_resolves_view(e.span_y, tr.syspan(), tr.ph);

                scratch.clear();
                scratch.reserve(n * 12);
                for (std::size_t i = 0; i < n; ++i) {
                    Rect r;
                    if (e.data_space) {
                        const double ac = lying ? e.anchor_y : e.anchor_x;
                        const double av = lying ? e.anchor_x : e.anchor_y;
                        r = rect(static_cast<float>(sc(bp.centers[i] - half) - ac),
                                 static_cast<float>(sc(bp.centers[i] + half) - ac),
                                 static_cast<float>(sv(bp.base_at(i)) - av),
                                 static_cast<float>(sv(bp.tip_at(i)) - av));
                    } else {
                        r = rect(pc(bp.centers[i] - half), pc(bp.centers[i] + half),
                                 pv(bp.base_at(i)), pv(bp.tip_at(i)));
                    }
                    const auto [xl, xr, yb, yt] = r;
                    scratch.insert(scratch.end(), {
                                       xl, yb, xr, yb, xr, yt,
                                       xl, yb, xr, yt, xl, yt,
                                   });
                }
                if (e.fill_vbo == 0) glGenBuffers(1, &e.fill_vbo);
                glBindBuffer(GL_ARRAY_BUFFER, e.fill_vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(scratch.size() * sizeof(float)),
                             scratch.data(), GL_DYNAMIC_DRAW);
                e.fill_verts = static_cast<int>(n * 6);
                e.fill_generation = data_generation_;
                e.bar_width = bp.bar_width;
                e.horizontal = lying;
                e.fill_tr = tr;
            }

            point_attrib_at(e.fill_vbo);
            const DataToPixel m = e.data_space
                                      ? data_to_pixel(tr, e.anchor_x, e.anchor_y)
                                      : DataToPixel{{1.0f, 1.0f}, {0.0f, 0.0f}};
            glUniform2fv(line_u_.scale, 1, m.scale);
            glUniform2fv(line_u_.offset, 1, m.offset);

            const auto& c = *bp.opts.color;
            glUniform4f(line_u_.color,
                        c.r, c.g, c.b, c.a * bp.opts.alpha);
            glDrawArrays(GL_TRIANGLES, 0, e.fill_verts);

            // --- outlines (one batched draw); pixel space, view in the key ---
            if (bp.opts.linewidth > 0.0f) {
                const bool edge_ok = e.edge_vbo != 0
                                     && e.edge_verts > 0
                                     && e.edge_generation != 0
                                     && e.edge_generation == data_generation_
                                     && width_same
                                     && e.linewidth == bp.opts.linewidth
                                     && e.pixel_ratio == pixel_ratio_
                                     && same_view(e.edge_tr, tr);
                if (!edge_ok) {
                    scratch.clear();
                    scratch.reserve(n * 48);
                    for (std::size_t i = 0; i < n; ++i) {
                        const auto [xl, xr, yb, yt] = rect(pc(bp.centers[i] - half),
                                                           pc(bp.centers[i] + half),
                                                           pv(bp.base_at(i)), pv(bp.tip_at(i)));
                        build_rect_outline(xl, yb, xr, yt, bp.opts.linewidth, scratch);
                    }
                    if (!scratch.empty()) {
                        if (e.edge_vbo == 0) glGenBuffers(1, &e.edge_vbo);
                        glBindBuffer(GL_ARRAY_BUFFER, e.edge_vbo);
                        glBufferData(GL_ARRAY_BUFFER,
                                     static_cast<GLsizeiptr>(scratch.size() * sizeof(float)),
                                     scratch.data(), GL_DYNAMIC_DRAW);
                        e.edge_verts = static_cast<int>(scratch.size() / 2);
                        e.edge_generation = data_generation_;
                        e.edge_tr = tr;
                        e.linewidth = bp.opts.linewidth;
                        e.pixel_ratio = pixel_ratio_;
                    }
                }
                if (e.edge_verts > 0) {
                    point_attrib_at(e.edge_vbo);
                    glUniform2fv(line_u_.scale, 1, k_identity_scale);
                    glUniform2fv(line_u_.offset, 1, k_identity_offset);

                    const Color ec = bar_edge(bp);
                    glUniform4f(line_u_.color,
                                ec.r, ec.g, ec.b, ec.a);
                    glDrawArrays(GL_TRIANGLES, 0, e.edge_verts);
                }
            }
        }

        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // draw_plots
    // -------------------------------------------------------------------------
    void DataRenderer::draw_plots(const RenderSnapshot& snap, const CoordTransform& tr,
                                  const PlotRect& pr) {
        // One object per draw: consecutive objects of one kind can't share a
        // call, since each one's error bars go between them.
        for (const ObjectRef& r : draw_order(snap)) draw_object(snap, r, tr, pr);
    }

    void DataRenderer::draw_layers(std::span<const RenderSnapshot* const> snaps,
                                   std::span<const CoordTransform* const> trs, std::span<const int> keys,
                                   unsigned long long data_generation, const PlotRect& pr) {
        for (const LayerRef& r : layered_draw_order<RenderSnapshot>(snaps)) {
            set_frame_key(data_generation, keys[r.layer]);
            draw_object(*snaps[r.layer], r.ref, *trs[r.layer], pr);
        }
    }

    void DataRenderer::draw_object(const RenderSnapshot& snap, const ObjectRef& r,
                                   const CoordTransform& tr, const PlotRect& pr) {
        const std::size_t one[1] = {r.index};
        const auto draw_shape_of = [&](const auto& p, const CoordTransform& t, const PlotRect& rect) {
            draw_shape(shape_geometry(p, t), *p.opts.color, p.opts.alpha, p.opts.linewidth, p.opts.linestyle,
                       shape_edge(p), t, rect);
        };
        switch (r.kind) {
            case PlotKind::Heatmap:
                draw_heatmap(snap.heatmaps, one, tr, pr);
                break;
            case PlotKind::Bar:
                draw_bars(snap.bars, one, tr, pr);
                draw_error_bars(snap, r, tr, pr);
                break;
            case PlotKind::Line:
                draw_lines(snap.lines, one, tr, pr);
                draw_error_bars(snap, r, tr, pr);
                break;
            case PlotKind::Scatter:
                draw_error_bars(snap, r, tr, pr);
                draw_scatter(snap.scatters, one, tr, pr);
                break;
            case PlotKind::ScatterZ:
                draw_error_bars(snap, r, tr, pr);
                draw_scatter_z(snap.scatter_z, one, tr, pr);
                break;
            case PlotKind::AxLine:
                draw_axline(snap.axlines[r.index], tr, pr);
                break;
            case PlotKind::AxSpan:
                draw_axspan(snap.axspans[r.index], tr, pr);
                break;
            case PlotKind::Fill:
                draw_fill(snap.fills[r.index], static_cast<int>(r.index), tr, pr);
                break;
            case PlotKind::Quiver:
                draw_quiver(snap.quivers[r.index], static_cast<int>(r.index), tr, pr);
                break;
            case PlotKind::Poly: draw_shape_of(snap.polys[r.index], tr, pr); break;
            case PlotKind::Circle: draw_shape_of(snap.circles[r.index], tr, pr); break;
            case PlotKind::Wedge: draw_shape_of(snap.wedges[r.index], tr, pr); break;
            case PlotKind::HeatmapTri: draw_heatmap_tri(snap.heatmap_tris[r.index], tr, pr); break;
            case PlotKind::Contour:
                draw_contour(snap.contours[r.index], snap.axes_style.font_path, tr, pr);
                break;
            default: break;
        }
    }

    // -------------------------------------------------------------------------
    // draw_contour
    // -------------------------------------------------------------------------
    // A contourf: its triangles through draw_heatmap_tri() (the band found per
    // fragment). A contour: its lines as pixel paths, one stroke per level in
    // its colour, broken where a label goes (the labels themselves are text,
    // NvgRenderer::draw_contour_labels()). A plane has no labels: whole lines.
    void DataRenderer::draw_contour(const ContourPlot& p, const std::string& font_path, const CoordTransform& tr,
                                    const PlotRect& pr) {
        if (!p.geom) return;
        if (p.opts.filled) {
            if (p.geom->levels.size() >= 2 && !p.geom->fill.tri.empty()) draw_heatmap_tri(p.geom->fill, tr, pr);
            return;
        }
        if (!(p.opts.linewidth > 0.0f) || p.geom->lines.empty()) return;
        const ContourDraw d = plan_contours(p.geom->lines, tr, p.opts.labels && plane_index_ < 0, p.opts.fontsize,
                                            font_path);
        std::vector<std::vector<std::vector<float>>> by_level(p.geom->levels.size());
        for (const ContourRun& run : d.runs) {
            std::vector<float> v;
            v.reserve(run.px.size() * 2);
            for (std::size_t k = 0; k < run.px.size(); ++k) {
                v.push_back(run.px[k]);
                v.push_back(run.py[k]);
            }
            by_level[p.geom->line_level[run.line]].push_back(std::move(v));
        }
        for (std::size_t k = 0; k < by_level.size(); ++k) {
            if (by_level[k].empty()) continue;
            const std::optional<Color> c = contour_line_color(p, k);
            if (c) stroke_rings(by_level[k], tr, pr, p.opts.linewidth, LineStyle::Solid, *c, false);
        }
    }

    // -------------------------------------------------------------------------
    // draw_axline / draw_axspan
    // -------------------------------------------------------------------------
    void DataRenderer::draw_axline(const AxLinePlot& p, const CoordTransform& tr, const PlotRect& pr) {
        if (p.opts.linestyle == LineStyle::None || !(p.opts.linewidth > 0.0f)) return;
        if (p.sloped()) {
            std::vector<std::vector<float>> paths;
            for (const RefPath& run : axline_paths(p, tr)) {
                std::vector<float> v;
                for (const RefPt& q : run) {
                    v.push_back(q.x);
                    v.push_back(q.y);
                }
                paths.push_back(std::move(v));
            }
            Color c = *p.opts.color;
            c.a *= p.opts.alpha;
            stroke_rings(paths, tr, pr, p.opts.linewidth, p.opts.linestyle, c, false);
            return;
        }
        const std::optional<RefSegment> g = axline_segment(p, tr);
        if (!g) return;

        begin_pass(pr, tr.win_h);
        glUseProgram(lineseg_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(lineseg_u_.resolution, 1, res);
        glBindVertexArray(lineseg_vao_);

        // One segment, padded with its own ends (butt caps), in pixels.
        const float pts[8] = {g->x0, g->y0, g->x0, g->y0, g->x1, g->y1, g->x1, g->y1};
        if (ref_vbo_ == 0) glGenBuffers(1, &ref_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, ref_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(pts), pts, GL_STREAM_DRAW);
        for (int a = 1; a <= 4; ++a) {
            glEnableVertexAttribArray(static_cast<unsigned>(a));
            glVertexAttribPointer(static_cast<unsigned>(a), 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                                  reinterpret_cast<void *>((a - 1) * 2 * sizeof(float)));
            glVertexAttribDivisor(static_cast<unsigned>(a), 1);
        }
        glUniform2fv(lineseg_u_.scale, 1, k_identity_scale);
        glUniform2fv(lineseg_u_.offset, 1, k_identity_offset);
        glUniform1f(lineseg_u_.half_width, p.opts.linewidth * 0.5f);

        // The dash runs from the `from` end: arc length 0 there (a constant).
        glDisableVertexAttribArray(5);
        glVertexAttrib1f(5, 0.0f);
        const DashPattern dash = dash_pattern(p.opts.linestyle);
        if (dash.dashed()) {
            glUniform4fv(lineseg_u_.dash, 1, dash.seg);
            glUniform1f(lineseg_u_.dash_period, dash.period);
            glUniform1f(lineseg_u_.dist_scale, 1.0f);
        } else {
            glUniform1f(lineseg_u_.dash_period, 0.0f);
        }

        const Color& c = *p.opts.color;
        glUniform4f(lineseg_u_.color, c.r, c.g, c.b, c.a * p.opts.alpha);
        glDrawArraysInstanced(GL_TRIANGLES, 0, k_lineseg_vertices, 1);

        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    void DataRenderer::draw_axspan(const AxSpanPlot& p, const CoordTransform& tr, const PlotRect& pr) {
        const std::optional<RefRect> r = axspan_rect(p, tr, axspan_reach(p));
        if (!r) return;

        begin_pass(pr, tr.win_h);
        glUseProgram(line_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(line_u_.resolution, 1, res);
        glUniform2fv(line_u_.scale, 1, k_identity_scale);
        glUniform2fv(line_u_.offset, 1, k_identity_offset);
        glBindVertexArray(line_vao_);

        // Re-point the VAO attribute at line_vbo_ (see draw_bars).
        auto draw = [&](const std::vector<float>& v, const Color& c, float a) {
            if (v.empty() || a <= 0.0f) return;
            glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(),
                         GL_STREAM_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
            glUniform4f(line_u_.color, c.r, c.g, c.b, a);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size() / 2));
        };
        std::vector<float> v;
        push_px_rect(r->x0, r->y0, r->x1, r->y1, v);
        const Color& c = *p.opts.color;
        draw(v, c, c.a * p.opts.alpha);
        if (p.opts.linewidth > 0.0f) {
            v.clear();
            build_rect_outline(r->x0, r->y0, r->x1, r->y1, p.opts.linewidth, v);
            const Color ec = axspan_edge(p);
            draw(v, ec, ec.a);
        }

        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // draw_fill
    // -------------------------------------------------------------------------
    void DataRenderer::draw_fill(const FillPlot& p, int index, const CoordTransform& tr,
                                 const PlotRect& pr) {
        const bool vert = p.vertical();
        // The regions in scale space (built at most once per draw).
        std::vector<FillRun> runs;
        bool have_runs = false;
        auto regions = [&]() -> const std::vector<FillRun>& {
            if (!have_runs) runs = fill_runs(p, vert ? tr.ys : tr.xs, vert ? tr.xs : tr.ys);
            have_runs = true;
            return runs;
        };
        // A region point (run, curve) in scale space as (x, y).
        auto xy = [vert](double r, double v) { return vert ? std::array{v, r} : std::array{r, v}; };

        const CacheKey key{axes_index_, plane_index_, index};
        FillCache& e = fill_cache_[key];
        const bool precise = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                             && float_resolves_view(e.span_y, tr.syspan(), tr.ph);
        const bool ok = e.generation != 0
                        && e.generation == data_generation_
                        && same_scales(e.tr, tr)
                        && e.orientation == p.opts.orientation
                        && e.drawstyle == p.opts.drawstyle
                        && e.interpolate == p.opts.interpolate
                        && (e.data_space ? precise : (!precise && same_view(e.tr, tr)));
        if (!ok) {
            const std::vector<FillRun>& rs = regions();
            double xlo = std::numeric_limits<double>::max(), xhi = -xlo, ylo = xlo, yhi = -xlo;
            for (const FillRun& run : rs)
                for (std::size_t k = 0; k < run.size(); ++k)
                    for (double v : {run.a[k], run.b[k]}) {
                        const auto [x, y] = xy(run.r[k], v);
                        xlo = std::min(xlo, x);
                        xhi = std::max(xhi, x);
                        ylo = std::min(ylo, y);
                        yhi = std::max(yhi, y);
                    }
            if (xlo > xhi) xlo = xhi = ylo = yhi = 0.0;
            e.anchor_x = 0.5 * (xlo + xhi);
            e.span_x = xhi - xlo;
            e.anchor_y = 0.5 * (ylo + yhi);
            e.span_y = yhi - ylo;
            e.data_space = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                           && float_resolves_view(e.span_y, tr.syspan(), tr.ph);

            std::vector<float> verts;
            auto put = [&](double r, double v) {
                const auto [x, y] = xy(r, v);
                if (e.data_space) {
                    verts.push_back(static_cast<float>(x - e.anchor_x));
                    verts.push_back(static_cast<float>(y - e.anchor_y));
                } else {
                    verts.push_back(tr.scale_to_px(x));
                    verts.push_back(tr.scale_to_py(y));
                }
            };
            for (const FillRun& run : rs)
                fill_triangles(run, [&](double r0, double v0, double r1, double v1, double r2, double v2) {
                    put(r0, v0);
                    put(r1, v1);
                    put(r2, v2);
                });
            if (!verts.empty()) {
                if (e.vbo == 0) glGenBuffers(1, &e.vbo);
                glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                             verts.data(), GL_DYNAMIC_DRAW);
            }
            e.verts = static_cast<int>(verts.size() / 2);
            e.generation = data_generation_;
            e.orientation = p.opts.orientation;
            e.drawstyle = p.opts.drawstyle;
            e.interpolate = p.opts.interpolate;
            e.tr = tr;
        }

        const Color& c = *p.opts.color;
        if (e.verts > 0 && e.vbo != 0) {
            begin_pass(pr, tr.win_h);
            glUseProgram(line_program_);
            const float res[2] = {tr.win_w, tr.win_h};
            glUniform2fv(line_u_.resolution, 1, res);
            glBindVertexArray(line_vao_);
            // Re-point the VAO attribute at the bound buffer (see draw_bars).
            glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
            const DataToPixel m = e.data_space
                                      ? data_to_pixel(tr, e.anchor_x, e.anchor_y)
                                      : DataToPixel{{1.0f, 1.0f}, {0.0f, 0.0f}};
            glUniform2fv(line_u_.scale, 1, m.scale);
            glUniform2fv(line_u_.offset, 1, m.offset);
            glUniform4f(line_u_.color, c.r, c.g, c.b, c.a * p.opts.alpha);
            glDrawArrays(GL_TRIANGLES, 0, e.verts);
            glBindVertexArray(0);
            glUseProgram(0);
            end_pass();
        }

        // --- outline: each region's closed ring in pixels, rebuilt per frame ---
        if (!(p.opts.linewidth > 0.0f) || p.opts.linestyle == LineStyle::None) return;
        const std::vector<FillRun>& rs = regions();
        if (rs.empty()) return;

        std::vector<std::vector<float>> rings;
        for (const FillRun& run : rs) {
            // The ring in pixels, a point equal to the one before it dropped (a
            // zero-length segment has no direction to join).
            std::vector<float> ring;
            fill_outline(run, [&](double r, double v) {
                const auto [x, y] = xy(r, v);
                const float px = tr.scale_to_px(x), py = tr.scale_to_py(y);
                if (ring.size() >= 2 && ring[ring.size() - 2] == px && ring.back() == py) return;
                ring.push_back(px);
                ring.push_back(py);
            });
            while (ring.size() >= 4 && ring[0] == ring[ring.size() - 2] && ring[1] == ring.back()) {
                ring.pop_back();
                ring.pop_back();
            }
            rings.push_back(std::move(ring));
        }
        stroke_rings(rings, tr, pr, p.opts.linewidth, p.opts.linestyle, fill_edge(p));
    }

    // Rings of pixel points (x, y pairs) stroked through the segment program,
    // each as one stroke (as each SVG polygon is), dashed by `ls`; open paths
    // (closed = false) have no segment from the last point back to the first.
    void DataRenderer::stroke_rings(const std::vector<std::vector<float>>& rings, const CoordTransform& tr,
                                    const PlotRect& pr, float linewidth, LineStyle ls, const Color& ec,
                                    bool closed) {
        begin_pass(pr, tr.win_h);
        glUseProgram(lineseg_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(lineseg_u_.resolution, 1, res);
        glBindVertexArray(lineseg_vao_);
        glUniform2fv(lineseg_u_.scale, 1, k_identity_scale);
        glUniform2fv(lineseg_u_.offset, 1, k_identity_offset);
        glUniform1f(lineseg_u_.half_width, linewidth * 0.5f);
        const DashPattern dash = dash_pattern(ls);
        if (dash.dashed()) {
            glUniform4fv(lineseg_u_.dash, 1, dash.seg);
            glUniform1f(lineseg_u_.dash_period, dash.period);
            glUniform1f(lineseg_u_.dist_scale, 1.0f);
        } else {
            glUniform1f(lineseg_u_.dash_period, 0.0f);
        }
        glUniform4f(lineseg_u_.color, ec.r, ec.g, ec.b, ec.a);

        std::vector<float> pts, dists;
        PaintOnce once;
        for (const std::vector<float>& ring : rings) {
            const std::size_t m = ring.size() / 2;
            if (m < 2) continue;
            // Padded so instance i reads points [i, i+3], across the seam (a
            // looped line's layout): segment i runs from point i to i+1.
            pts.clear();
            auto emit = [&](std::size_t i) {
                pts.push_back(ring[2 * (i % m)]);
                pts.push_back(ring[2 * (i % m) + 1]);
            };
            const std::size_t segs = closed ? m : m - 1;
            if (closed) {
                emit(m - 1);
                for (std::size_t i = 0; i < m; ++i) emit(i);
                emit(0);
                emit(1);
            } else {
                // An open path repeats its end points instead.
                emit(0);
                for (std::size_t i = 0; i < m; ++i) emit(i);
                emit(m - 1);
            }
            if (fill_edge_vbo_ == 0) glGenBuffers(1, &fill_edge_vbo_);
            glBindBuffer(GL_ARRAY_BUFFER, fill_edge_vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(pts.size() * sizeof(float)), pts.data(),
                         GL_STREAM_DRAW);
            for (int a = 1; a <= 4; ++a) {
                glEnableVertexAttribArray(static_cast<unsigned>(a));
                glVertexAttribPointer(static_cast<unsigned>(a), 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                                      reinterpret_cast<void *>((a - 1) * 2 * sizeof(float)));
                glVertexAttribDivisor(static_cast<unsigned>(a), 1);
            }
            if (dash.dashed()) {
                // Arc length at each segment's start, from the ring's first point.
                dists.clear();
                double acc = 0.0;
                for (std::size_t i = 0; i < segs; ++i) {
                    dists.push_back(static_cast<float>(acc));
                    const std::size_t j = (i + 1) % m;
                    const double dx = ring[2 * j] - ring[2 * i], dy = ring[2 * j + 1] - ring[2 * i + 1];
                    acc += std::sqrt(dx * dx + dy * dy);
                }
                if (fill_dist_vbo_ == 0) glGenBuffers(1, &fill_dist_vbo_);
                glBindBuffer(GL_ARRAY_BUFFER, fill_dist_vbo_);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(dists.size() * sizeof(float)),
                             dists.data(), GL_STREAM_DRAW);
                glEnableVertexAttribArray(5);
                glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
                glVertexAttribDivisor(5, 1);
            } else {
                glDisableVertexAttribArray(5);
                glVertexAttrib1f(5, 0.0f);
            }
            once.stroke(ec.a < 1.0f);
            glDrawArraysInstanced(GL_TRIANGLES, 0, k_lineseg_vertices, static_cast<GLsizei>(segs));
        }
        once.finish();

        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // draw_shape
    // -------------------------------------------------------------------------
    void DataRenderer::draw_shape(const ShapeGeom& g, const Color& color, float alpha, float linewidth,
                                  LineStyle linestyle, const Color& edge, const CoordTransform& tr,
                                  const PlotRect& pr) {
        if (g.empty()) return;
        const float a = color.a * alpha;
        if (a > 0.0f) {
            std::vector<float> v;
            v.reserve(g.tris.size() * 2);
            for (const ShapePt& p : g.tris) {
                v.push_back(p.x);
                v.push_back(p.y);
            }
            begin_pass(pr, tr.win_h);
            glUseProgram(line_program_);
            const float res[2] = {tr.win_w, tr.win_h};
            glUniform2fv(line_u_.resolution, 1, res);
            glUniform2fv(line_u_.scale, 1, k_identity_scale);
            glUniform2fv(line_u_.offset, 1, k_identity_offset);
            glBindVertexArray(line_vao_);
            // Re-point the VAO attribute at line_vbo_ (see draw_axspan).
            glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(),
                         GL_STREAM_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
            glUniform4f(line_u_.color, color.r, color.g, color.b, a);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(v.size() / 2));
            glBindVertexArray(0);
            glUseProgram(0);
            end_pass();
        }
        if (!(linewidth > 0.0f) || linestyle == LineStyle::None) return;
        // A point equal to the one before it is dropped (a zero-length segment
        // has no direction to join), as a fill's ring.
        std::vector<std::vector<float>> rings;
        for (const std::vector<ShapePt>& src : g.rings) {
            std::vector<float> ring;
            for (const ShapePt& p : src) {
                if (ring.size() >= 2 && ring[ring.size() - 2] == p.x && ring.back() == p.y) continue;
                ring.push_back(p.x);
                ring.push_back(p.y);
            }
            while (ring.size() >= 4 && ring[0] == ring[ring.size() - 2] && ring[1] == ring.back()) {
                ring.pop_back();
                ring.pop_back();
            }
            rings.push_back(std::move(ring));
        }
        stroke_rings(rings, tr, pr, linewidth, linestyle, edge);
    }

    // -------------------------------------------------------------------------
    // draw_quiver
    // -------------------------------------------------------------------------
    void DataRenderer::draw_quiver(const QuiverPlot& p, int index, const CoordTransform& tr,
                                   const PlotRect& pr) {
        const CacheKey key{axes_index_, plane_index_, index};
        QuiverCache& e = quiver_cache_[key];
        const bool precise = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                             && float_resolves_view(e.span_y, tr.syspan(), tr.ph);
        const bool ok = e.generation != 0
                        && e.generation == data_generation_
                        && same_scales(e.tr, tr)
                        && e.amp == p.amp()
                        && e.pivot == p.opts.pivot
                        && e.mapped == p.colormapped()
                        && (!e.mapped || (e.cmap == p.opts.cmap && e.vmin == p.opts.vmin
                                          && e.vmax == p.opts.vmax && e.norm == p.opts.norm))
                        && (e.data_space ? precise : (!precise && same_view(e.tr, tr)));
        if (!ok) {
            // The arrows' ends in scale space, then anchored at their extent's
            // midpoint (residuals <= half the span), as scatter centres are. A
            // colormapped quiver's colors are baked in after each arrow's ends.
            const bool mapped = p.colormapped();
            std::vector<double> ends;
            std::vector<Color> colors;
            for_each_quiver_arrow(p, tr.xs, tr.ys,
                                  [&](std::size_t i, double tx, double ty, double hx, double hy) {
                ends.insert(ends.end(), {tx, ty, hx, hy});
                if (mapped) colors.push_back(quiver_arrow_color(p, i));
            });
            double xlo = std::numeric_limits<double>::max(), xhi = -xlo, ylo = xlo, yhi = -xlo;
            for (std::size_t k = 0; k + 1 < ends.size(); k += 2) {
                xlo = std::min(xlo, ends[k]);
                xhi = std::max(xhi, ends[k]);
                ylo = std::min(ylo, ends[k + 1]);
                yhi = std::max(yhi, ends[k + 1]);
            }
            if (xlo > xhi) xlo = xhi = ylo = yhi = 0.0;
            e.anchor_x = 0.5 * (xlo + xhi);
            e.span_x = xhi - xlo;
            e.anchor_y = 0.5 * (ylo + yhi);
            e.span_y = yhi - ylo;
            e.data_space = float_resolves_view(e.span_x, tr.sxspan(), tr.pw)
                           && float_resolves_view(e.span_y, tr.syspan(), tr.ph);

            std::vector<float> inst;
            inst.reserve(ends.size() * (mapped ? 2 : 1));
            for (std::size_t k = 0; k + 1 < ends.size(); k += 2) {
                if (e.data_space) {
                    inst.push_back(static_cast<float>(ends[k] - e.anchor_x));
                    inst.push_back(static_cast<float>(ends[k + 1] - e.anchor_y));
                } else {
                    inst.push_back(tr.scale_to_px(ends[k]));
                    inst.push_back(tr.scale_to_py(ends[k + 1]));
                }
                if (mapped && k % 4 == 2) {
                    const Color& c = colors[k / 4];
                    inst.insert(inst.end(), {c.r, c.g, c.b, c.a});
                }
            }
            if (!inst.empty()) {
                if (e.vbo == 0) glGenBuffers(1, &e.vbo);
                glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(inst.size() * sizeof(float)),
                             inst.data(), GL_DYNAMIC_DRAW);
            }
            e.instances = static_cast<int>(ends.size() / 4);
            e.generation = data_generation_;
            e.amp = p.amp();
            e.pivot = p.opts.pivot;
            e.mapped = mapped;
            e.cmap = p.opts.cmap;
            e.vmin = p.opts.vmin;
            e.vmax = p.opts.vmax;
            e.norm = p.opts.norm;
            e.tr = tr;
        }
        if (e.instances <= 0 || e.vbo == 0) return;

        begin_pass(pr, tr.win_h);
        glUseProgram(quiver_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(quiver_u_.resolution, 1, res);
        glBindVertexArray(quiver_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
        const GLsizei stride = (e.mapped ? 8 : 4) * sizeof(float);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
        glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void *>(2 * sizeof(float)));
        glVertexAttribDivisor(2, 1);
        if (e.mapped) {
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>(4 * sizeof(float)));
            glVertexAttribDivisor(3, 1);
        } else {
            const Color& c = *p.opts.color;
            glDisableVertexAttribArray(3);
            glVertexAttrib4f(3, c.r, c.g, c.b, c.a);
        }
        const DataToPixel m = e.data_space
                                  ? data_to_pixel(tr, e.anchor_x, e.anchor_y)
                                  : DataToPixel{{1.0f, 1.0f}, {0.0f, 0.0f}};
        glUniform2fv(quiver_u_.scale, 1, m.scale);
        glUniform2fv(quiver_u_.offset, 1, m.offset);
        glUniform1f(quiver_u_.width, p.opts.width);
        glUniform1f(quiver_u_.head_width, p.opts.head_width);
        glUniform1f(quiver_u_.head_length, p.opts.head_length);
        glUniform1f(quiver_u_.alpha, p.opts.alpha);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 9, e.instances);
        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // draw_error_bars
    // -------------------------------------------------------------------------
    void DataRenderer::draw_error_bars(const RenderSnapshot& snap, ObjectRef ref,
                                       const CoordTransform& tr,
                                       const PlotRect& pr) {
        // No error bars (the common case) must cost nothing.
        const bool has = with_kind(snap, ref.kind, [&ref](const auto& v) {
            if constexpr (requires { v[0].err; }) return !v[ref.index].err.empty();
            else return false;
        }, false);
        if (!has) return;

        begin_pass(pr, tr.win_h);
        glUseProgram(line_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(line_u_.resolution, 1, res);
        glUniform2fv(line_u_.scale, 1, k_identity_scale); // pixel space throughout
        glUniform2fv(line_u_.offset, 1, k_identity_offset);
        glBindVertexArray(line_vao_);

        std::vector<float> fill_scratch, stroke_scratch;

        // One plot object: rebuild pixel geometry when inputs change, then draw
        // both halves. `anchors()` gives the points the bars hang off, as x and y
        // columns; `horizontal` is a bar's orientation (false for the rest).
        auto emit = [&](auto& cache, const auto& plot, int index, auto anchors,
                        bool horizontal, const Color& fallback) {
            const ErrorBarOptions& style = plot.opts.errorbar;
            if (style.linewidth <= 0.0f) return;

            const CacheKey key{axes_index_, plane_index_, index};
            ErrCache& e = cache[key];
            const bool ok = e.generation != 0
                            && e.generation == data_generation_
                            && e.linewidth == style.linewidth
                            && e.capsize == style.capsize
                            && e.capstyle == static_cast<int>(style.capstyle)
                            && e.boxwidth == style.boxwidth
                            && e.box_alpha == style.box_alpha
                            && e.pixel_ratio == pixel_ratio_
                            && e.horizontal == horizontal
                            && same_view(e.tr, tr);
            if (!ok) {
                fill_scratch.clear();
                stroke_scratch.clear();
                const auto [xs, ys] = anchors();
                build_error_bars(xs, ys, plot.err, style, tr,
                                 fill_scratch, stroke_scratch);

                auto upload = [](unsigned int& vbo, int& verts,
                                 const std::vector<float>& src) {
                    verts = static_cast<int>(src.size() / 2);
                    if (src.empty()) return;
                    if (vbo == 0) glGenBuffers(1, &vbo);
                    glBindBuffer(GL_ARRAY_BUFFER, vbo);
                    glBufferData(GL_ARRAY_BUFFER,
                                 static_cast<GLsizeiptr>(src.size() * sizeof(float)),
                                 src.data(), GL_DYNAMIC_DRAW);
                };
                upload(e.fill_vbo, e.fill_verts, fill_scratch);
                upload(e.stroke_vbo, e.stroke_verts, stroke_scratch);

                e.generation = data_generation_;
                e.tr = tr;
                e.linewidth = style.linewidth;
                e.capsize = style.capsize;
                e.capstyle = static_cast<int>(style.capstyle);
                e.boxwidth = style.boxwidth;
                e.box_alpha = style.box_alpha;
                e.pixel_ratio = pixel_ratio_;
                e.horizontal = horizontal;
            }

            // Faded by the series' `alpha`.
            Color c = style.color.value_or(fallback);
            c.a *= std::clamp(plot.opts.alpha, 0.0f, 1.0f);

            // Re-point the VAO attribute (see draw_bars).
            auto draw = [&](unsigned int vbo, int verts, float alpha) {
                if (vbo == 0 || verts <= 0 || alpha <= 0.0f) return;
                glBindBuffer(GL_ARRAY_BUFFER, vbo);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
                glUniform4f(line_u_.color, c.r, c.g, c.b, alpha);
                glDrawArrays(GL_TRIANGLES, 0, verts);
            };
            // Interiors first, so outlines and whiskers draw over them.
            draw(e.fill_vbo, e.fill_verts, c.a * style.box_alpha);
            draw(e.stroke_vbo, e.stroke_verts, c.a);
        };

        const int i = static_cast<int>(ref.index);
        switch (ref.kind) {
            case PlotKind::Line: {
                const auto& p = snap.lines[ref.index];
                emit(line_err_cache_, p, i, [&] { return XYColumns{p.x, p.y}; }, false, *p.opts.color);
                break;
            }
            case PlotKind::Bar: {
                // Error bars hang off the tip, not the base.
                const auto& p = snap.bars[ref.index];
                emit(bar_err_cache_, p, i, [&] { return bar_anchors(p); }, p.horizontal(),
                     p.opts.edgecolor);
                break;
            }
            case PlotKind::Scatter: {
                const auto& p = snap.scatters[ref.index];
                emit(scatter_err_cache_, p, i, [&] { return XYColumns{p.x, p.y}; }, false,
                     *p.opts.color);
                break;
            }
            case PlotKind::ScatterZ: {
                // scatter_z has no single color, so black.
                const auto& p = snap.scatter_z[ref.index];
                emit(scatterz_err_cache_, p, i, [&] { return XYColumns{p.x, p.y}; }, false,
                     Color::Black);
                break;
            }
            default: break;
        }

        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // draw_heatmap
    // -------------------------------------------------------------------------
    // Colormapped heatmap texture from plane_heatmap_rgba() (also used by the SVG
    // plane path), built once per data generation and left bound on unit 0.
    void DataRenderer::ensure_heatmap_texture(HeatCache& hc, const HeatmapPlot& hp) {
        const bool flip = (hp.opts.origin == "lower");

        // Depends only on data and color mapping, so it survives pan, zoom, orbit.
        const bool usable = hc.tex != 0
                            && hc.data_generation != 0
                            && hc.data_generation == data_generation_
                            && hc.cmap == hp.opts.cmap
                            && hc.vmin == hp.opts.vmin && hc.vmax == hp.opts.vmax
                            && hc.norm == hp.opts.norm
                            && hc.flip == flip
                            && hc.rows == hp.rows && hc.cols == hp.cols;

        if (hc.tex == 0) glGenTextures(1, &hc.tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hc.tex);
        if (usable) return;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        const std::vector<uint8_t> rgba = plane_heatmap_rgba(hp);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, hp.cols, hp.rows, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

        hc.data_generation = data_generation_;
        hc.cmap = hp.opts.cmap;
        hc.vmin = hp.opts.vmin;
        hc.vmax = hp.opts.vmax;
        hc.norm = hp.opts.norm;
        hc.flip = flip;
        hc.rows = hp.rows;
        hc.cols = hp.cols;
    }

    void DataRenderer::draw_heatmap(const std::vector<HeatmapPlot>& heatmaps,
                                    std::span<const std::size_t> which,
                                    const CoordTransform& tr,
                                    const PlotRect& pr) {
        if (which.empty()) return;

        begin_pass(pr, tr.win_h);
        glUseProgram(heatmap_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(heatmap_u_.resolution, 1, res);
        glUniform1i(heatmap_u_.tex, 0);
        glBindVertexArray(heatmap_vao_);

        for (const std::size_t hi : which) {
            const auto& hp = heatmaps[hi];
            if (hp.rows <= 0 || hp.cols <= 0) continue;

            const CacheKey key{axes_index_, plane_index_, static_cast<int>(hi)};
            ensure_heatmap_texture(heat_cache_[key], hp);

            // Even cells on linear axes: one quad. Otherwise the grid path.
            if (hp.mesh() || !tr.xs.linear() || !tr.ys.linear()) {
                draw_heatmap_grid(hp, tr);
                continue;
            }

            // Quad over xrange x yrange in pixel space. Tex (0,0) = top-left; a
            // reversed range mirrors the image.
            const float x0 = tr.to_px(hp.xrange.lo), x1 = tr.to_px(hp.xrange.hi);
            const float y0 = tr.to_py(hp.yrange.lo), y1 = tr.to_py(hp.yrange.hi);
            const float verts[24] = {
                x0, y0, 0.0f, 1.0f,
                x1, y0, 1.0f, 1.0f,
                x1, y1, 1.0f, 0.0f,
                x0, y0, 0.0f, 1.0f,
                x1, y1, 1.0f, 0.0f,
                x0, y1, 0.0f, 0.0f,
            };
            glBindBuffer(GL_ARRAY_BUFFER, heatmap_vbo_);
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

            // Texture already bound on unit 0 and owned by heat_cache_.
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    // A per-axis scale maps a rectangle to a rectangle, so a grid of shared
    // vertices, each edge placed through the scale, with uv (c/cols, 1 - r/rows)
    // into the GL_NEAREST texture colours every cell exactly, unsubdivided.
    // Program, VAO and texture are draw_heatmap()'s, bound.
    void DataRenderer::draw_heatmap_grid(const HeatmapPlot& hp, const CoordTransform& tr) {
        const int rows = hp.rows, cols = hp.cols;
        const std::size_t w = static_cast<std::size_t>(cols) + 1;
        const std::size_t h = static_cast<std::size_t>(rows) + 1;

        // An edge far off the frame (Log puts v <= 0 hundreds of decades below)
        // is pulled in to just outside it: a cell shows one texel, so squeezing
        // its hidden part changes no pixel.
        const float xlo = tr.px - 1.0f, xhi = tr.px + tr.pw + 1.0f;
        const float ylo = tr.py - 1.0f, yhi = tr.py + tr.ph + 1.0f;
        std::vector<float> ex(w), ey(h);
        for (int c = 0; c <= cols; ++c)
            ex[static_cast<std::size_t>(c)] = std::clamp(tr.to_px(hp.x_edge(c)), xlo, xhi);
        for (int r = 0; r <= rows; ++r)
            ey[static_cast<std::size_t>(r)] = std::clamp(tr.to_py(hp.y_edge(r)), ylo, yhi);

        heatmap_grid_.resize(w * h * 4);
        float* v = heatmap_grid_.data();
        for (std::size_t r = 0; r < h; ++r) {
            const float tv = 1.0f - static_cast<float>(r) / static_cast<float>(rows);
            for (std::size_t c = 0; c < w; ++c) {
                *v++ = ex[c];
                *v++ = ey[r];
                *v++ = static_cast<float>(c) / static_cast<float>(cols);
                *v++ = tv;
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, heatmap_vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(heatmap_grid_.size() * sizeof(float)),
                     heatmap_grid_.data(), GL_DYNAMIC_DRAW);

        // The VAO keeps the element buffer binding.
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, heatmap_ebo_);
        if (heatmap_ebo_rows_ != rows || heatmap_ebo_cols_ != cols) {
            std::vector<std::uint32_t> idx;
            idx.reserve(static_cast<std::size_t>(rows) * cols * 6);
            for (std::size_t r = 0; r + 1 < h; ++r)
                for (std::size_t c = 0; c + 1 < w; ++c) {
                    const auto a = static_cast<std::uint32_t>(r * w + c);
                    const auto b = a + 1, d = static_cast<std::uint32_t>(a + w), e = d + 1;
                    idx.insert(idx.end(), { a, b, e, a, e, d });
                }
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(std::uint32_t)),
                         idx.data(), GL_STATIC_DRAW);
            heatmap_ebo_rows_ = rows;
            heatmap_ebo_cols_ = cols;
        }
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(static_cast<std::size_t>(rows) * cols * 6),
                       GL_UNSIGNED_INT, nullptr);
    }

    // -------------------------------------------------------------------------
    // draw_heatmap_tri
    // -------------------------------------------------------------------------
    // The drawn triangles of heatmap_tri_pixels(), each vertex with its colormap
    // position (under a non-linear norm, its value); GL interpolates it linearly
    // in screen space and the fragment normalises it and looks the colour up.
    void DataRenderer::draw_heatmap_tri(const HeatmapTriPlot& p, const CoordTransform& tr,
                                        const PlotRect& pr) {
        const std::vector<TriPx> tris = heatmap_tri_pixels(p, tr);
        if (tris.empty()) return;
        heattri_verts_.clear();
        heattri_verts_.reserve(tris.size() * 9);
        const ColorNorm::Kind kind = heatmap_tri_norm(p.opts);
        for (const TriPx& q : tris)
            for (int k = 0; k < 3; ++k) {
                heattri_verts_.push_back(static_cast<float>(q.x[k]));
                heattri_verts_.push_back(static_cast<float>(q.y[k]));
                heattri_verts_.push_back(static_cast<float>(heatmap_tri_attr(p.opts, kind, p.values[q.v[k]])));
            }

        begin_pass(pr, tr.win_h);
        glUseProgram(heattri_program_);
        const float res[2] = {tr.win_w, tr.win_h};
        glUniform2fv(heattri_u_.resolution, 1, res);
        glUniform1i(heattri_u_.cyclic, p.opts.cmap.is_cyclic() ? 1 : 0);
        glUniform1i(heattri_u_.norm, kind == ColorNorm::Kind::Log ? 1 : kind == ColorNorm::Kind::TwoSlope ? 2
                                     : kind == ColorNorm::Kind::Boundaries ? 3 : 0);
        if (kind == ColorNorm::Kind::Log)
            glUniform3f(heattri_u_.n, std::log(p.opts.vmin), std::log(p.opts.vmax), 0.0f);
        else
            glUniform3f(heattri_u_.n, p.opts.vmin, p.opts.norm.center, p.opts.vmax);

        // A boundary norm's bands, on unit 1 (re-uploaded per draw: a few texels).
        if (kind == ColorNorm::Kind::Boundaries) {
            const std::vector<float> texels = heatmap_band_texels(
                p.opts.norm.bounds, p.band_t.empty() ? norm_band_positions(p.opts.norm) : p.band_t,
                p.opts.cmap.is_cyclic());
            glActiveTexture(GL_TEXTURE1);
            if (!heattri_bands_tex_) {
                glGenTextures(1, &heattri_bands_tex_);
                glBindTexture(GL_TEXTURE_2D, heattri_bands_tex_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glBindTexture(GL_TEXTURE_2D, heattri_bands_tex_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, static_cast<GLsizei>(texels.size() / 2), 1, 0, GL_RG, GL_FLOAT,
                         texels.data());
            glUniform1i(heattri_u_.bands, 1);
            glUniform1i(heattri_u_.nb, static_cast<GLint>(texels.size() / 2));
        }

        glActiveTexture(GL_TEXTURE0);
        if (!heattri_cmap_tex_) {
            glGenTextures(1, &heattri_cmap_tex_);
            glBindTexture(GL_TEXTURE_2D, heattri_cmap_tex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            heattri_cmap_in_tex_.reset();
        }
        glBindTexture(GL_TEXTURE_2D, heattri_cmap_tex_);
        if (heattri_cmap_in_tex_ != p.opts.cmap) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         colormaps::get(p.opts.cmap));
            heattri_cmap_in_tex_ = p.opts.cmap;
        }
        glUniform1i(heattri_u_.cmap, 0);

        glBindVertexArray(heattri_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, heattri_vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(heattri_verts_.size() * sizeof(float)),
                     heattri_verts_.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(heattri_verts_.size() / 3));
        glBindVertexArray(0);
        glUseProgram(0);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // 3D planes
    // -------------------------------------------------------------------------
    // A plane's contents are rendered into its own framebuffer by the 2D draw
    // calls, in 2D order, and the plane enters the scene as one textured quad.
    // So a plane is one depth layer per pixel (enabling the per-pixel composite)
    // and is clipped to the box face.
    //
    // Called twice per frame (see ScenePass): opaque planes write depth; translucent
    // ones use composite_planes3d() (per-fragment sort), with whole-object order
    // only between groups. A translucent plane against a translucent bar has no
    // exact answer in the fallback path.
    void DataRenderer::draw_planes3d(const std::vector<PlaneSnapshot>& planes,
                                     const Projector3D& proj, const PlotRect& pr,
                                     float win_w, float win_h, ScenePass pass,
                                     const std::vector<std::size_t>* explicit_order) {
        const bool want_translucent = (pass == ScenePass::Translucent);

        // The planes for this pass (plane_drawn(), not "has a heatmap"), far to
        // near; the order only matters for grouping.
        std::vector<std::size_t> order;
        if (explicit_order) {
            order = *explicit_order; // already filtered and sorted
        } else {
            for (std::size_t i = 0; i < planes.size(); ++i)
                if (plane_translucent(planes[i]) == want_translucent
                    && plane_drawn(planes[i]))
                    order.push_back(i);
            // Nothing to sort under peeling.
            if (want_translucent && !peel_.active)
                std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                    return plane_distance(planes[a], proj) > plane_distance(planes[b], proj);
                });
        }
        if (order.empty()) return;

        // Render all rasters first (it rebinds framebuffer and viewport). Size cap:
        // the plot rect's longer side (camera-independent). Under peeling this was
        // done in prepare_plane_rasters() and must not repeat (occlusion query).
        std::vector<PlaneRaster> rasters(planes.size());
        for (const std::size_t pi: order) {
            const PlaneSnapshot& pl = planes[pi];
            rasters[pi] = plane_raster(proj, pl.orient, pl.offset);
            if (peel_.active) continue;
            const CacheKey key{axes_index_, static_cast<int>(pi), -1};
            render_plane_raster(pl, static_cast<int>(pi), rasters[pi],
                                plane_raster_cache_[key]);
        }

        // Drop planes without a raster, so every group member has a texture.
        std::vector<std::size_t> drawn;
        for (const std::size_t pi: order) {
            const CacheKey key{axes_index_, static_cast<int>(pi), -1};
            const PlaneRasterCache& c = plane_raster_cache_[key];
            if (c.valid && c.tex) drawn.push_back(pi);
        }
        if (drawn.empty()) return;

        begin_pass(pr, win_h);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(want_translucent && !peel_.active ? GL_FALSE : GL_TRUE);
        // Premultiplied blending (rasters are premultiplied).
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        // Under peeling translucent planes take the opaque quad path; the composite
        // is the fallback when peeling is unavailable.
        if (want_translucent && !peel_.active) {
            // Groups of kMaxCompositePlanes, far to near: exact within a group.
            for (std::size_t g = 0; g < drawn.size(); g += kMaxCompositePlanes) {
                const std::size_t hi =
                        std::min(drawn.size(), g + static_cast<std::size_t>(kMaxCompositePlanes));
                const std::vector<std::size_t> group(drawn.begin() + static_cast<std::ptrdiff_t>(g),
                                                     drawn.begin() + static_cast<std::ptrdiff_t>(hi));
                composite_planes3d(planes, rasters, group, proj, pr, win_w, win_h);
            }
            // Restore begin_pass()'s blend state (not plain glBlendFunc()).
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_TRUE);
            glDisable(GL_DEPTH_TEST);
            end_pass();
            return;
        }

        const std::array<float, 16> clip = proj.clip_matrix(win_w, win_h);

        const Plane3DUniforms& pu = peel_.active ? peel_plane3d_u_ : plane3d_u_;
        glUseProgram(peel_.active ? peel_plane3d_program_ : plane3d_program_);
        glUniformMatrix4fv(pu.clip, 1, GL_FALSE, clip.data());
        glUniform1i(pu.tex, 0);
        glBindVertexArray(plane3d_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, plane3d_vbo_);
        glActiveTexture(GL_TEXTURE0);

        for (const std::size_t pi: drawn) {
            const CacheKey key{axes_index_, static_cast<int>(pi), -1};
            const PlaneRasterCache& c = plane_raster_cache_[key];

            // Whole-plane alpha as a uniform.
            glUniform1f(pu.alpha, std::clamp(planes[pi].opts.alpha, 0.0f, 1.0f));
            glBindTexture(GL_TEXTURE_2D, c.tex);

            // Rebuilt per draw (four corners); anchored at the quad's centre for
            // precision.
            const PlaneRaster& q = rasters[pi];
            const Vec3 anchor{
                (q.p[0].x + q.p[2].x) * 0.5,
                (q.p[0].y + q.p[2].y) * 0.5,
                (q.p[0].z + q.p[2].z) * 0.5
            };
            float box_scale[3], box_offset[3];
            proj.box_affine(anchor, box_scale, box_offset);
            glUniform3fv(pu.box_scale, 1, box_scale);
            glUniform3fv(pu.box_offset, 1, box_offset);

            float verts[30];
            const int tri[6] = {0, 1, 2, 0, 2, 3};
            for (int t = 0; t < 6; ++t) {
                const int k = tri[t];
                float* v = &verts[t * 5];
                v[0] = static_cast<float>(q.p[k].x - anchor.x);
                v[1] = static_cast<float>(q.p[k].y - anchor.y);
                v[2] = static_cast<float>(q.p[k].z - anchor.z);
                v[3] = q.uv[k][0];
                v[4] = q.uv[k][1];
            }
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        // Restore begin_pass()'s blend state (not plain glBlendFunc()).
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glBindVertexArray(0);
        glUseProgram(0);
        glDisable(GL_DEPTH_TEST);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // The whole 3D scene, in two phases
    // -------------------------------------------------------------------------
    // All opaque objects first, then all translucent objects in one order across
    // kinds (by the comparable eye_coord() distances), so no kind is drawn after
    // another regardless of the camera.
    void DataRenderer::draw_scene3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                                    const PlotRect& pr, float win_w, float win_h) {
        // ---- Phase 1: everything opaque, any order (the depth buffer resolves it).
        draw_planes3d(snap.planes, proj, pr, win_w, win_h, ScenePass::Opaque);
        draw_bars3d(snap.bars3d, proj, pr, win_w, win_h, ScenePass::Opaque);
        draw_surfaces3d(snap.surfaces, proj, pr, win_w, win_h, ScenePass::Opaque);
        draw_surface_tri3d(snap.surface_tri, proj, pr, win_w, win_h, ScenePass::Opaque);
        draw_scatter3d(snap.scatter3d, proj, pr, win_w, win_h, ScenePass::Opaque);
        draw_lines3d(snap.lines3d, proj, pr, win_w, win_h, ScenePass::Opaque);
        draw_errorbars3d(snap, proj, pr, win_w, win_h, ScenePass::Opaque);

        // ---- Phase 2, first choice: depth peeling (exact per pixel). Declines
        // only if disabled or its targets can't be built.
        bool any_translucent = false;
        for (const PlaneSnapshot& p: snap.planes)
            any_translucent = any_translucent || (plane_translucent(p) && plane_drawn(p));
        for (const Bar3DPlot& b: snap.bars3d)
            any_translucent = any_translucent || (object_shown(b) && bar3d_translucent(b));
        for (const SurfacePlot& sp: snap.surfaces)
            any_translucent = any_translucent || (object_shown(sp) && surface_translucent(sp));
        for (const SurfaceTriPlot& sm: snap.surface_tri)
            any_translucent = any_translucent || (object_shown(sm) && surface_tri_translucent(sm));
        for (const Scatter3DPlot& sc: snap.scatter3d)
            any_translucent = any_translucent || (object_shown(sc) && scatter3d_translucent(sc));
        for (const Line3DPlot& ln: snap.lines3d)
            any_translucent = any_translucent || (object_shown(ln) && line3d_translucent(ln));
        for (const Scatter3DPlot& sc: snap.scatter3d)
            any_translucent = any_translucent || (object_shown(sc) && errorbar3d_translucent(sc));
        for (const Line3DPlot& ln: snap.lines3d)
            any_translucent = any_translucent || (object_shown(ln) && errorbar3d_translucent(ln));
        if (!any_translucent) return;
        if (peel_translucent3d(snap, proj, pr, win_w, win_h)) return;

        // ---- Phase 2 fallback: all translucent objects far to near (exact only
        // for objects separated in depth).
        enum class Kind { Plane, Bar, Surface, Mesh, Scatter, Line, ErrorBar };
        struct Item {
            Kind kind;
            std::size_t index;
            double distance;
        };
        std::vector<Item> list;

        for (std::size_t i = 0; i < snap.planes.size(); ++i)
            if (plane_translucent(snap.planes[i]) && plane_drawn(snap.planes[i]))
                list.push_back({Kind::Plane, i, plane_distance(snap.planes[i], proj)});
        for (std::size_t i = 0; i < snap.bars3d.size(); ++i)
            if (object_shown(snap.bars3d[i]) && bar3d_translucent(snap.bars3d[i]))
                list.push_back({Kind::Bar, i, bar3d_plot_distance(snap.bars3d[i], proj)});
        for (std::size_t i = 0; i < snap.surfaces.size(); ++i)
            if (object_shown(snap.surfaces[i]) && surface_translucent(snap.surfaces[i]))
                list.push_back({Kind::Surface, i, surface_plot_distance(snap.surfaces[i], proj)});
        for (std::size_t i = 0; i < snap.surface_tri.size(); ++i)
            if (object_shown(snap.surface_tri[i]) && surface_tri_translucent(snap.surface_tri[i]))
                list.push_back({
                    Kind::Mesh, i,
                    surface_tri_plot_distance(snap.surface_tri[i], proj)
                });
        for (std::size_t i = 0; i < snap.scatter3d.size(); ++i)
            if (object_shown(snap.scatter3d[i]) && scatter3d_translucent(snap.scatter3d[i]))
                list.push_back({
                    Kind::Scatter, i,
                    scatter3d_plot_distance(snap.scatter3d[i], proj)
                });
        for (std::size_t i = 0; i < snap.lines3d.size(); ++i)
            if (object_shown(snap.lines3d[i]) && line3d_translucent(snap.lines3d[i]))
                list.push_back({
                    Kind::Line, i,
                    line3d_plot_distance(snap.lines3d[i], proj)
                });
        // A series' error bars at the series' distance, after it (drawn over it on
        // ties).
        for (std::size_t i = 0; i < snap.scatter3d.size(); ++i)
            if (object_shown(snap.scatter3d[i]) && errorbar3d_translucent(snap.scatter3d[i]))
                list.push_back({
                    Kind::ErrorBar, i,
                    scatter3d_plot_distance(snap.scatter3d[i], proj)
                });
        for (std::size_t i = 0; i < snap.lines3d.size(); ++i)
            if (object_shown(snap.lines3d[i]) && errorbar3d_translucent(snap.lines3d[i]))
                list.push_back({
                    Kind::ErrorBar, snap.scatter3d.size() + i,
                    line3d_plot_distance(snap.lines3d[i], proj)
                });
        if (list.empty()) return;

        // Stable, for deterministic frames.
        std::stable_sort(list.begin(), list.end(),
                         [](const Item& a, const Item& b) { return a.distance > b.distance; });

        // Drawn as runs of one kind; consecutive planes form one composite group.
        std::size_t i = 0;
        std::vector<std::size_t> run;
        while (i < list.size()) {
            const Kind kind = list[i].kind;
            run.clear();
            while (i < list.size() && list[i].kind == kind) run.push_back(list[i++].index);
            switch (kind) {
                case Kind::Plane:
                    draw_planes3d(snap.planes, proj, pr, win_w, win_h,
                                  ScenePass::Translucent, &run);
                    break;
                case Kind::Bar:
                    draw_bars3d(snap.bars3d, proj, pr, win_w, win_h,
                                ScenePass::Translucent, &run);
                    break;
                case Kind::Surface:
                    draw_surfaces3d(snap.surfaces, proj, pr, win_w, win_h,
                                    ScenePass::Translucent, &run);
                    break;
                case Kind::Mesh:
                    draw_surface_tri3d(snap.surface_tri, proj, pr, win_w, win_h,
                                       ScenePass::Translucent, &run);
                    break;
                case Kind::Scatter:
                    draw_scatter3d(snap.scatter3d, proj, pr, win_w, win_h,
                                   ScenePass::Translucent, &run);
                    break;
                case Kind::Line:
                    draw_lines3d(snap.lines3d, proj, pr, win_w, win_h,
                                 ScenePass::Translucent, &run);
                    break;
                case Kind::ErrorBar:
                    draw_errorbars3d(snap, proj, pr, win_w, win_h,
                                     ScenePass::Translucent, &run);
                    break;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Depth peeling
    // -------------------------------------------------------------------------
    // Default 8 layers: covers every gallery scene but the densest (which needs
    // 12). Each layer is a full translucent redraw; the occlusion query stops
    // early, so shallow scenes don't pay. Raise via PngExportOptions::peel_layers
    // or SEXTANT_PEEL_LAYERS.
    int DataRenderer::peel_layer_default() {
        static const int n = [] {
            if (const char* s = std::getenv("SEXTANT_PEEL_LAYERS")) {
                const int v = std::atoi(s);
                if (v >= 0 && v <= 64) return v;
            }
            return 8;
        }();
        return n;
    }

    // Override > environment > default. SEXTANT_PEEL_LAYERS=0 must stay effective
    // even when an export requests layers (it is the test baseline).
    int DataRenderer::peel_layer_count() const {
        if (peel_layer_default() == 0) return 0;
        if (peel_layers_override_ > 0) return std::min(peel_layers_override_, 64);
        return peel_layer_default();
    }

    void DataRenderer::prepare_plane_rasters(const std::vector<PlaneSnapshot>& planes,
                                             const Projector3D& proj) {
        for (std::size_t pi = 0; pi < planes.size(); ++pi) {
            const PlaneSnapshot& pl = planes[pi];
            if (!plane_translucent(pl) || !plane_drawn(pl)) continue;
            const PlaneRaster r = plane_raster(proj, pl.orient, pl.offset);
            const CacheKey key{axes_index_, static_cast<int>(pi), -1};
            render_plane_raster(pl, static_cast<int>(pi), r, plane_raster_cache_[key]);
        }
    }

    bool DataRenderer::ensure_peel_targets(int w, int h) {
        if (w <= 0 || h <= 0) return false;
        // Negative size = the driver already refused; don't retry every frame.
        if (peel_.w < 0) return false;
        if (peel_.fbo && peel_.w == w && peel_.h == h) return true;

        // Everything is sized to the plot rect, so a resize rebuilds all but the
        // query.
        const unsigned int query = peel_.query;
        if (peel_.fbo) glDeleteFramebuffers(1, &peel_.fbo);
        if (peel_.accum_fbo) glDeleteFramebuffers(1, &peel_.accum_fbo);
        if (peel_.copy_fbo) glDeleteFramebuffers(1, &peel_.copy_fbo);
        if (peel_.layer_tex) glDeleteTextures(1, &peel_.layer_tex);
        if (peel_.accum_tex) glDeleteTextures(1, &peel_.accum_tex);
        if (peel_.opaque_tex) glDeleteTextures(1, &peel_.opaque_tex);
        if (peel_.depth_tex[0]) glDeleteTextures(2, peel_.depth_tex);
        if (peel_.z_tex[0]) glDeleteTextures(2, peel_.z_tex);
        peel_ = PeelTargets{};
        peel_.query = query;
        if (!peel_.query) glGenQueries(1, &peel_.query);

        auto make_color = [&](unsigned int& t) {
            glGenTextures(1, &t);
            glBindTexture(GL_TEXTURE_2D, t);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        };
        auto make_depth = [&](unsigned int& t) {
            glGenTextures(1, &t);
            glBindTexture(GL_TEXTURE_2D, t);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0,
                         GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            // Sampled for the depth value, not a shadow comparison.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        };

        auto make_z = [&](unsigned int& t) {
            glGenTextures(1, &t);
            glBindTexture(GL_TEXTURE_2D, t);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        };

        make_color(peel_.layer_tex);
        make_color(peel_.accum_tex);
        make_depth(peel_.opaque_tex);
        make_depth(peel_.depth_tex[0]);
        make_depth(peel_.depth_tex[1]);
        make_z(peel_.z_tex[0]);
        make_z(peel_.z_tex[1]);
        glBindTexture(GL_TEXTURE_2D, 0);

        int prev_fbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
        bool ok = true;
        auto attach = [&](unsigned int& fbo, unsigned int color, unsigned int depth) {
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            if (color)
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, color, 0);
            else glDrawBuffer(GL_NONE), glReadBuffer(GL_NONE);
            if (depth)
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                       GL_TEXTURE_2D, depth, 0);
            ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        };
        // The depth attachment and the z attachment are re-pointed every pass.
        attach(peel_.fbo, peel_.layer_tex, peel_.depth_tex[0]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                               peel_.z_tex[0], 0);
        {
            const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
            glDrawBuffers(2, bufs);
        }
        ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        attach(peel_.accum_fbo, peel_.accum_tex, 0);
        attach(peel_.copy_fbo, 0, peel_.opaque_tex);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(prev_fbo));

        if (!ok) {
            // Without targets, fall back to whole-object order (permanently).
            peel_.w = peel_.h = -1;
            return false;
        }
        peel_.w = w;
        peel_.h = h;
        return true;
    }

    void DataRenderer::draw_peel_quad(unsigned int tex, bool flip_alpha,
                                      float x0, float y0, float x1, float y1) const {
        const float v[24] = {
            x0, y0, 0.0f, 0.0f, x1, y0, 1.0f, 0.0f, x1, y1, 1.0f, 1.0f,
            x0, y0, 0.0f, 0.0f, x1, y1, 1.0f, 1.0f, x0, y1, 0.0f, 1.0f,
        };
        glUseProgram(peel_comp_program_);
        glUniform1i(peel_comp_u_.tex, 0);
        glUniform1i(peel_comp_u_.flip, flip_alpha ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glBindVertexArray(peel_comp_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, peel_comp_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Phase 2 via peeling. The per-kind draw sequence doesn't matter: blending is
    // off and the depth test keeps one fragment per pixel per pass.
    bool DataRenderer::peel_translucent3d(const RenderSnapshot3D& snap,
                                          const Projector3D& proj, const PlotRect& pr,
                                          float win_w, float win_h) {
        const int layers = peel_layer_count();
        if (layers <= 0) return false;

        // The plot rect in framebuffer pixels, rounded as begin_pass()'s scissor.
        const float s = pixel_ratio_;
        const int ox = static_cast<int>(std::floor(pr.x * s));
        const int oy = static_cast<int>(std::floor((win_h - pr.y - pr.h) * s));
        const int px1 = static_cast<int>(std::ceil((pr.x + pr.w) * s));
        const int py1 = static_cast<int>(std::ceil((win_h - pr.y) * s));
        const int w = px1 - ox, h = py1 - oy;
        if (!ensure_peel_targets(w, h)) return false;

        int prev_fbo = 0, prev_vp[4] = {0, 0, 0, 0};
        float prev_clear[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
        glGetIntegerv(GL_VIEWPORT, prev_vp);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear);
        const int fb_w = static_cast<int>(std::lround(win_w * s));
        const int fb_h = static_cast<int>(std::lround(win_h * s));

        // Plane rasters before the loop (see prepare_plane_rasters()).
        prepare_plane_rasters(snap.planes, proj);

        // Copy the opaque phase's depth; the peel test uses it for occlusion.
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned int>(prev_fbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, peel_.copy_fbo);
        glBlitFramebuffer(ox, oy, ox + w, oy + h, 0, 0, w, h,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);

        // Accumulation starts transparent: alpha is transmittance (all light through).
        glBindFramebuffer(GL_FRAMEBUFFER, peel_.accum_fbo);
        glViewport(0, 0, w, h);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Pass 0's "already peeled" z rejects nothing.
        const float z_none[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float z_far[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        glBindFramebuffer(GL_FRAMEBUFFER, peel_.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                               GL_TEXTURE_2D, peel_.z_tex[1], 0);
        glClearBufferfv(GL_COLOR, 1, z_none);

        int cur = 0;
        int peeled = 0;
        for (int n = 0; n < layers; ++n) {
            const int prev = 1 - cur;
            glBindFramebuffer(GL_FRAMEBUFFER, peel_.fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                   GL_TEXTURE_2D, peel_.depth_tex[cur], 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                                   GL_TEXTURE_2D, peel_.z_tex[cur], 0);
            // Shift the viewport by the rect origin so geometry lands in the peel
            // target, keeping clip_matrix() and the buffers unchanged.
            glViewport(-ox, -oy, fb_w, fb_h);
            glDisable(GL_SCISSOR_TEST);
            glDepthMask(GL_TRUE);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            // A pixel this pass leaves empty has nothing left behind it.
            glClearBufferfv(GL_COLOR, 1, z_far);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, peel_.z_tex[prev]);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, peel_.opaque_tex);
            glActiveTexture(GL_TEXTURE0);

            peel_.active = true;
            glBeginQuery(GL_ANY_SAMPLES_PASSED, peel_.query);
            draw_planes3d(snap.planes, proj, pr, win_w, win_h, ScenePass::Translucent);
            draw_bars3d(snap.bars3d, proj, pr, win_w, win_h, ScenePass::Translucent);
            draw_surfaces3d(snap.surfaces, proj, pr, win_w, win_h, ScenePass::Translucent);
            draw_surface_tri3d(snap.surface_tri, proj, pr, win_w, win_h,
                               ScenePass::Translucent);
            draw_scatter3d(snap.scatter3d, proj, pr, win_w, win_h, ScenePass::Translucent);
            draw_lines3d(snap.lines3d, proj, pr, win_w, win_h, ScenePass::Translucent);
            draw_errorbars3d(snap, proj, pr, win_w, win_h, ScenePass::Translucent);
            glEndQuery(GL_ANY_SAMPLES_PASSED);
            peel_.active = false;

            // Early-out when a pass peels nothing (a sync point per pass).
            unsigned int any = 0;
            glGetQueryObjectuiv(peel_.query, GL_QUERY_RESULT, &any);
            if (!any) break;
            ++peeled;

            // Composite this layer under the accumulation (front to back):
            //     rgb += layer.rgb * accum.a        (GL_DST_ALPHA, GL_ONE)
            //     a   *= 1 - layer.a                (GL_ZERO, GL_ONE_MINUS_SRC_ALPHA)
            glBindFramebuffer(GL_FRAMEBUFFER, peel_.accum_fbo);
            glViewport(0, 0, w, h);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
            draw_peel_quad(peel_.layer_tex, false, -1.0f, -1.0f, 1.0f, 1.0f);

            cur = prev;
        }

        // Composite the accumulation over the scene; `flip_alpha` turns
        // transmittance into coverage for a premultiplied `over`.
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(prev_fbo));
        glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
        if (peeled > 0) {
            begin_pass(pr, win_h);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            const float nx0 = 2.0f * static_cast<float>(ox) / static_cast<float>(fb_w) - 1.0f;
            const float nx1 = 2.0f * static_cast<float>(ox + w) / static_cast<float>(fb_w) - 1.0f;
            const float ny0 = 2.0f * static_cast<float>(oy) / static_cast<float>(fb_h) - 1.0f;
            const float ny1 = 2.0f * static_cast<float>(oy + h) / static_cast<float>(fb_h) - 1.0f;
            draw_peel_quad(peel_.accum_tex, true, nx0, ny0, nx1, ny1);
            end_pass();
        }

        glUseProgram(0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glClearColor(prev_clear[0], prev_clear[1], prev_clear[2], prev_clear[3]);
        // Restore begin_pass()'s blend state (not plain glBlendFunc()).
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
        return true;
    }

    // -------------------------------------------------------------------------
    // K translucent planes, composited exactly
    // -------------------------------------------------------------------------
    // Each plane goes to the shader as a box-space corner, two edges and an axis.
    // Expects (and preserves) the pass state: scissor to `pr`, depth test on with
    // writes off, premultiplied blending.
    void DataRenderer::composite_planes3d(const std::vector<PlaneSnapshot>& planes,
                                          const std::vector<PlaneRaster>& rasters,
                                          const std::vector<std::size_t>& group,
                                          const Projector3D& proj, const PlotRect& pr,
                                          float win_w, float win_h) {
        const int n = static_cast<int>(group.size());
        if (n <= 0) return;

        float origin[kMaxCompositePlanes * 3]{};
        float du[kMaxCompositePlanes * 3]{};
        float dv[kMaxCompositePlanes * 3]{};
        int axis[kMaxCompositePlanes]{};
        float alpha[kMaxCompositePlanes]{};

        for (int i = 0; i < n; ++i) {
            const PlaneRaster& q = rasters[group[static_cast<std::size_t>(i)]];

            // With the anchor at p0, `offset` is p0 in box space and the edges are
            // the scaled spans (precision relative to the data span).
            float sc[3], off[3];
            proj.box_affine(q.p[0], sc, off);
            const Vec3 e_u{q.p[1].x - q.p[0].x, q.p[1].y - q.p[0].y, q.p[1].z - q.p[0].z};
            const Vec3 e_v{q.p[3].x - q.p[0].x, q.p[3].y - q.p[0].y, q.p[3].z - q.p[0].z};
            for (int k = 0; k < 3; ++k) {
                origin[i * 3 + k] = off[k];
                du[i * 3 + k] = sc[k] * static_cast<float>((&e_u.x)[k]);
                dv[i * 3 + k] = sc[k] * static_cast<float>((&e_v.x)[k]);
            }
            axis[i] = q.normal_axis;
            alpha[i] = std::clamp(planes[group[static_cast<std::size_t>(i)]].opts.alpha, 0.0f, 1.0f);

            const CacheKey key{axes_index_, static_cast<int>(group[static_cast<std::size_t>(i)]), -1};
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
            glBindTexture(GL_TEXTURE_2D, plane_raster_cache_[key].tex);
        }

        const std::array<float, 16> clip = proj.clip_matrix(win_w, win_h);

        glUseProgram(plane_comp_program_);
        glUniformMatrix4fv(plane_comp_u_.clip, 1, GL_FALSE, clip.data());
        glUniform1i(plane_comp_u_.count, n);
        glUniform3fv(plane_comp_u_.origin, n, origin);
        glUniform3fv(plane_comp_u_.du, n, du);
        glUniform3fv(plane_comp_u_.dv, n, dv);
        glUniform1iv(plane_comp_u_.axis, n, axis);
        glUniform1fv(plane_comp_u_.alpha, n, alpha);

        // The plot rect with each corner's ray; the shader does the in-bounds test.
        const float cxp[4] = {pr.x, pr.x + pr.w, pr.x + pr.w, pr.x};
        const float cyp[4] = {pr.y, pr.y, pr.y + pr.h, pr.y + pr.h};
        const int tri[6] = {0, 1, 2, 0, 2, 3};
        float verts[6 * 8];
        for (int t = 0; t < 6; ++t) {
            const int k = tri[t];
            const Projector3D::Ray3 r = proj.ray_from_pixel(cxp[k], cyp[k]);
            float* v = &verts[t * 8];
            v[0] = 2.0f * cxp[k] / (win_w > 0.0f ? win_w : 1.0f) - 1.0f;
            v[1] = 1.0f - 2.0f * cyp[k] / (win_h > 0.0f ? win_h : 1.0f);
            v[2] = static_cast<float>(r.origin.x);
            v[3] = static_cast<float>(r.origin.y);
            v[4] = static_cast<float>(r.origin.z);
            v[5] = static_cast<float>(r.dir.x);
            v[6] = static_cast<float>(r.dir.y);
            v[7] = static_cast<float>(r.dir.z);
        }

        glBindVertexArray(plane_comp_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, plane_comp_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

        // Far end first, one draw per slot: the draw order is the sort.
        for (int s = 0; s < n; ++s) {
            glUniform1i(plane_comp_u_.slot, s);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        for (int i = 0; i < n; ++i) {
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    // -------------------------------------------------------------------------
    // One plane's contents, into that plane's own framebuffer
    // -------------------------------------------------------------------------
    // The 2D call sequence from render_frame.cpp, with the plane's transform and
    // the whole raster as plot rect.
    void DataRenderer::render_plane_raster(const PlaneSnapshot& pl, int plane_index,
                                           const PlaneRaster& raster,
                                           PlaneRasterCache& c) {
        // Rounded up: a fractional display scale (1.5) must not lose resolution.
        const int ss = static_cast<int>(std::ceil(pixel_ratio_ > 0.0f ? pixel_ratio_ - 1e-3f : 1.0f));
        const int rw = std::max(1, raster.w * std::max(1, ss));
        const int rh = std::max(1, raster.h * std::max(1, ss));

        // Exact transform equality, as in the 2D caches.
        const bool same_tr = c.tr.xmin == raster.tr.xmin && c.tr.xmax == raster.tr.xmax
                             && c.tr.ymin == raster.tr.ymin && c.tr.ymax == raster.tr.ymax
                             && c.tr.pw == raster.tr.pw && c.tr.ph == raster.tr.ph;
        const bool stale = !c.valid || c.w != rw || c.h != rh || !same_tr
                           || data_generation_ == 0 || c.data_generation != data_generation_
                           || c.style_generation != pl.style_generation;
        if (!stale) return;

        if (c.w != rw || c.h != rh || !c.tex) {
            if (!c.tex) glGenTextures(1, &c.tex);
            glBindTexture(GL_TEXTURE_2D, c.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            // GL_LINEAR for the composed raster; the heatmap inside keeps
            // GL_NEAREST.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            c.w = rw;
            c.h = rh;
        }
        // Read the framebuffer to restore before binding anything.
        int prev_fbo = 0, prev_vp[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
        glGetIntegerv(GL_VIEWPORT, prev_vp);
        const GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);

        if (!c.fbo) glGenFramebuffers(1, &c.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, c.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, c.tex, 0);
        // No depth attachment: the contents are a fixed-order painter's stack.
        glViewport(0, 0, rw, rh);
        glDisable(GL_SCISSOR_TEST);

        // Transparent black, so the quad composites as a decal.
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Premultiplied accumulation from straight-alpha sources: alpha must
        // accumulate coverage (see begin_pass()).
        plane_index_ = plane_index;
        draw_plots(pl.sheet, raster.tr, {0.0f, 0.0f, raster.tr.pw, raster.tr.ph});
        plane_index_ = -1;

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(prev_fbo));
        glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
        if (prev_scissor) glEnable(GL_SCISSOR_TEST);
        else glDisable(GL_SCISSOR_TEST);

        c.tr = raster.tr;
        c.data_generation = data_generation_;
        c.style_generation = pl.style_generation;
        c.valid = true;
    }

    // -------------------------------------------------------------------------
    // 3D bars
    // -------------------------------------------------------------------------
    // Depth-tested. Opaque plots: one unordered draw. Translucent plots: every
    // face back to front, depth writes off, with an index buffer rebuilt per frame.
    // Opaque first.
    void DataRenderer::draw_bars3d(const std::vector<Bar3DPlot>& bars,
                                   const Projector3D& proj, const PlotRect& pr,
                                   float win_w, float win_h, ScenePass pass,
                                   const std::vector<std::size_t>* explicit_order) {
        if (bars.empty()) return;
        const bool want_translucent = (pass == ScenePass::Translucent);

        begin_pass(pr, win_h);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        const std::array<float, 16> clip = proj.clip_matrix(win_w, win_h);

        // This pass's plots, far to near when translucent (or draw_scene3d()'s
        // explicit order).
        std::vector<std::size_t> plot_order;
        if (explicit_order) {
            plot_order = *explicit_order;
        } else {
            plot_order.reserve(bars.size());
            for (std::size_t i = 0; i < bars.size(); ++i)
                if (bar3d_translucent(bars[i]) == want_translucent) plot_order.push_back(i);
            // Nothing to sort under peeling: the depth test decides per pixel.
            if (want_translucent && !peel_.active)
                std::stable_sort(plot_order.begin(), plot_order.end(),
                                 [&](std::size_t a, std::size_t c) {
                                     return bar3d_plot_distance(bars[a], proj) >
                                            bar3d_plot_distance(bars[c], proj);
                                 });
        }
        // No early return: the cleanup below must still run.

        std::vector<std::size_t> order;
        std::vector<std::uint32_t> indices;
        for (const std::size_t bi: plot_order) {
            const Bar3DPlot& b = bars[bi];
            const std::size_t n = b.count();
            if (!object_shown(b) || n == 0 || b.heights.size() < n) continue;
            const bool translucent = bar3d_translucent(b);

            const CacheKey key{axes_index_, -1, static_cast<int>(bi)};
            Bar3DCache& c = bar3d_cache_[key];

            // The key holds what's baked; camera and limits are uniforms, so an
            // orbit costs one draw and no upload.
            const int signs = (proj.transform().xmax < proj.transform().xmin ? 1 : 0)
                              | (proj.transform().ymax < proj.transform().ymin ? 2 : 0)
                              | (proj.transform().zmax < proj.transform().zmin ? 4 : 0);
            const bool stale = c.vbo == 0 || data_generation_ == 0 ||
                               c.data_generation != data_generation_ ||
                               c.u_width != b.u_width || c.v_width != b.v_width ||
                               c.bottom != b.opts.bottom ||
                               c.shading != b.opts.shading ||
                               c.axis_signs != signs ||
                               c.edges != b.opts.edges;

            if (stale) {
                // Anchor at the data's middle, for float precision.
                Vec3 anchor{}; {
                    const Axis3Map m = axis_map(b.orient);
                    double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
                    lo[m.u] = hi[m.u] = b.u.size() ? b.u[0] : 0.0;
                    lo[m.v] = hi[m.v] = b.v.size() ? b.v[0] : 0.0;
                    lo[m.h] = hi[m.h] = b.h_lo(0);
                    for (double u: b.u) {
                        lo[m.u] = std::min(lo[m.u], u);
                        hi[m.u] = std::max(hi[m.u], u);
                    }
                    for (double v: b.v) {
                        lo[m.v] = std::min(lo[m.v], v);
                        hi[m.v] = std::max(hi[m.v], v);
                    }
                    for (std::size_t k = 0; k < n; ++k) {
                        lo[m.h] = std::min(lo[m.h], b.h_lo(k));
                        hi[m.h] = std::max(hi[m.h], b.h_hi(k));
                    }
                    anchor = {(lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5};
                }

                std::vector<float> verts;
                verts.reserve(n * 36 * 4);
                std::vector<float> edges;
                if (b.opts.edges) edges.reserve(n * 12 * 6);

                // Each face emits the two sides whose axis is the lower of its two
                // spanned axes, so each of the twelve edges appears once.
                Bar3DFace faces[6];
                for (std::size_t k = 0; k < n; ++k) {
                    bar3d_faces(b, k, proj.transform(), faces);
                    for (const Bar3DFace& f: faces) {
                        const int tri[6] = {0, 1, 2, 0, 2, 3};
                        for (int t: tri) {
                            const Vec3 p = f.p[t];
                            verts.push_back(static_cast<float>(p.x - anchor.x));
                            verts.push_back(static_cast<float>(p.y - anchor.y));
                            verts.push_back(static_cast<float>(p.z - anchor.z));
                            verts.push_back(f.shade);
                        }
                    }
                    if (b.opts.edges) {
                        // From bar3d.h, matching the SVG path's edges.
                        Vec3 seg[12][2];
                        bar3d_edges(faces, seg);
                        for (const auto& e: seg)
                            for (const Vec3& p: e) {
                                edges.push_back(static_cast<float>(p.x - anchor.x));
                                edges.push_back(static_cast<float>(p.y - anchor.y));
                                edges.push_back(static_cast<float>(p.z - anchor.z));
                            }
                    }
                }

                if (!c.vbo) glGenBuffers(1, &c.vbo);
                glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                             verts.data(), GL_STATIC_DRAW);
                c.verts = static_cast<int>(verts.size() / 4);

                c.edge_segs = 0;
                if (!edges.empty()) {
                    if (!c.edge_vbo) glGenBuffers(1, &c.edge_vbo);
                    glBindBuffer(GL_ARRAY_BUFFER, c.edge_vbo);
                    glBufferData(GL_ARRAY_BUFFER,
                                 static_cast<GLsizeiptr>(edges.size() * sizeof(float)),
                                 edges.data(), GL_STATIC_DRAW);
                    c.edge_segs = static_cast<int>(edges.size() / 6);
                }

                c.data_generation = data_generation_;
                c.anchor = anchor;
                c.u_width = b.u_width;
                c.v_width = b.v_width;
                c.bottom = b.opts.bottom;
                c.shading = b.opts.shading;
                c.edges = b.opts.edges;
                c.axis_signs = signs;
            }

            float box_scale[3], box_offset[3];
            proj.box_affine(c.anchor, box_scale, box_offset);

            const Bar3DUniforms& bu = peel_.active ? peel_bar3d_u_ : bar3d_u_;
            glUseProgram(peel_.active ? peel_bar3d_program_ : bar3d_program_);
            glUniformMatrix4fv(bu.clip, 1, GL_FALSE, clip.data());
            glUniform3fv(bu.box_scale, 1, box_scale);
            glUniform3fv(bu.box_offset, 1, box_offset);
            const float col[4] = {
                b.opts.color->r, b.opts.color->g, b.opts.color->b,
                bar3d_alpha(b)
            };
            glUniform4fv(bu.color, 1, col);

            glBindVertexArray(bar3d_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
            glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                  reinterpret_cast<void *>(3 * sizeof(float)));

            if (!translucent || peel_.active) {
                // Under peeling a translucent grid draws unordered and
                // depth-writing, like an opaque one; the peel orders the faces.
                glDrawArrays(GL_TRIANGLES, 0, c.verts);
            } else {
                // This camera's order from bar3d.h: bars back to front, then per
                // bar the away-facing faces before the facing ones (as the SVG).
                bar3d_draw_order(b, proj, order);
                indices.clear();
                indices.reserve(n * 36);
                Bar3DFace faces[6];
                for (const std::size_t k: order) {
                    bar3d_faces(b, k, proj.transform(), faces);
                    const Bar3DFaceOrder fo = bar3d_face_order(faces, proj.transform(), proj);
                    for (int fi = 0; fi < 6; ++fi) {
                        const std::uint32_t base =
                                static_cast<std::uint32_t>(k) * 36u +
                                static_cast<std::uint32_t>(fo.index[fi]) * 6u;
                        for (std::uint32_t t = 0; t < 6u; ++t) indices.push_back(base + t);
                    }
                }
                if (!c.index_ebo) glGenBuffers(1, &c.index_ebo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.index_ebo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                             indices.data(), GL_STREAM_DRAW);
                c.indices = static_cast<int>(indices.size());
                // No depth writes for translucent faces, so what's behind shows.
                glDepthMask(GL_FALSE);
                glDrawElements(GL_TRIANGLES, c.indices, GL_UNSIGNED_INT, nullptr);
            }

            if (b.opts.edges && c.edge_segs > 0) {
                // Width in pixels at the box centre, as a box length (thins with
                // distance). Polygon offset avoids z-fighting with the faces.
                const double half = 0.5 * b.opts.edge_linewidth *
                                    proj.box_units_per_pixel(Vec3{0.0, 0.0, 0.0});
                const Vec3 eye = proj.has_eye_point() ? proj.eye_point() : proj.eye_dir();

                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -2.0f);
                const Bar3DEdgeUniforms& eu = peel_.active ? peel_edge_u_ : bar3d_edge_u_;
                glUseProgram(peel_.active ? peel_edge_program_ : bar3d_edge_program_);
                glUniformMatrix4fv(eu.clip, 1, GL_FALSE, clip.data());
                glUniform3fv(eu.box_scale, 1, box_scale);
                glUniform3fv(eu.box_offset, 1, box_offset);
                // Edges have their own opacity; translucent bars show all twelve.
                const float ec[4] = {
                    b.opts.edgecolor.r, b.opts.edgecolor.g,
                    b.opts.edgecolor.b, bar3d_edge_alpha(b)
                };
                glUniform4fv(eu.color, 1, ec);
                const float eyef[3] = {
                    static_cast<float>(eye.x), static_cast<float>(eye.y),
                    static_cast<float>(eye.z)
                };
                glUniform3fv(eu.eye, 1, eyef);
                glUniform1i(eu.persp, proj.has_eye_point() ? 1 : 0);
                glUniform1f(eu.half_width, static_cast<float>(half));

                glBindVertexArray(bar3d_edge_vao_);
                glBindBuffer(GL_ARRAY_BUFFER, c.edge_vbo);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
                glVertexAttribDivisor(1, 1);
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                      reinterpret_cast<void *>(3 * sizeof(float)));
                glVertexAttribDivisor(2, 1);
                glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, c.edge_segs);
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            glDepthMask(GL_TRUE);
        }

        glBindVertexArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glUseProgram(0);
        glDisable(GL_DEPTH_TEST);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // 3D surfaces
    // -------------------------------------------------------------------------
    // As bars, but two-sided with per-cell color on the vertices. Opaque: one
    // unordered draw.
    void DataRenderer::draw_surfaces3d(const std::vector<SurfacePlot>& surfaces,
                                       const Projector3D& proj, const PlotRect& pr,
                                       float win_w, float win_h, ScenePass pass,
                                       const std::vector<std::size_t>* explicit_order) {
        if (surfaces.empty()) return;
        const bool want_translucent = (pass == ScenePass::Translucent);

        begin_pass(pr, win_h);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        const std::array<float, 16> clip = proj.clip_matrix(win_w, win_h);
        const Transform3D& tf = proj.transform();

        // This pass's plots, far to near when translucent (or draw_scene3d()'s
        // explicit order).
        std::vector<std::size_t> plot_order;
        if (explicit_order) {
            plot_order = *explicit_order;
        } else {
            plot_order.reserve(surfaces.size());
            for (std::size_t i = 0; i < surfaces.size(); ++i)
                if (surface_translucent(surfaces[i]) == want_translucent) plot_order.push_back(i);
            // Nothing to sort under peeling.
            if (want_translucent && !peel_.active)
                std::stable_sort(plot_order.begin(), plot_order.end(),
                                 [&](std::size_t a, std::size_t b) {
                                     return surface_plot_distance(surfaces[a], proj) >
                                            surface_plot_distance(surfaces[b], proj);
                                 });
        }

        std::vector<std::size_t> order;
        std::vector<std::uint32_t> indices;
        for (const std::size_t si: plot_order) {
            const SurfacePlot& s = surfaces[si];
            if (!object_shown(s) || s.cell_count() == 0 || s.heights.size() < s.count()) continue;
            const bool translucent = surface_translucent(s);
            const std::size_t nc = s.cell_cols();

            const CacheKey key{axes_index_, -1, static_cast<int>(si)};
            SurfaceCache& c = surface_cache_[key];

            double vmin = 0.0, vmax = 1.0;
            surface_value_range(s, vmin, vmax);

            // The key holds what's baked (including the color source); camera and
            // limits are uniforms.
            const int signs = (tf.xmax < tf.xmin ? 1 : 0)
                              | (tf.ymax < tf.ymin ? 2 : 0)
                              | (tf.zmax < tf.zmin ? 4 : 0);
            const bool stale = c.vbo == 0 || data_generation_ == 0 ||
                               c.data_generation != data_generation_ ||
                               c.shading != s.opts.shading ||
                               c.alpha != s.opts.alpha ||
                               c.color.r != s.opts.color->r ||
                               c.color.g != s.opts.color->g ||
                               c.color.b != s.opts.color->b ||
                               c.color.a != s.opts.color->a ||
                               c.colormap != s.opts.colormap ||
                               c.cmap != s.opts.cmap ||
                               c.vmin != vmin || c.vmax != vmax ||
                               c.axis_signs != signs ||
                               c.edges != s.opts.edges;

            if (stale) {
                // Anchor at the data's middle, for float precision.
                const Axis3Map m = axis_map(s.orient);
                double lo[3], hi[3];
                for (int a = 0; a < 3; ++a) {
                    lo[a] = 0.0;
                    hi[a] = 0.0;
                }
                lo[m.u] = hi[m.u] = s.u.size() ? s.u[0] : 0.0;
                lo[m.v] = hi[m.v] = s.v.size() ? s.v[0] : 0.0;
                lo[m.h] = hi[m.h] = s.height_at(0);
                for (double u: s.u) {
                    lo[m.u] = std::min(lo[m.u], u);
                    hi[m.u] = std::max(hi[m.u], u);
                }
                for (double v: s.v) {
                    lo[m.v] = std::min(lo[m.v], v);
                    hi[m.v] = std::max(hi[m.v], v);
                }
                for (std::size_t k = 0; k < s.count(); ++k) {
                    lo[m.h] = std::min(lo[m.h], s.height_at(k));
                    hi[m.h] = std::max(hi[m.h], s.height_at(k));
                }
                const Vec3 anchor{
                    (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5,
                    (lo[2] + hi[2]) * 0.5
                };

                std::vector<float> verts;
                std::vector<float> edges;
                verts.reserve(s.cell_count() * 6 * 7);

                SurfaceCell cell;
                for (std::size_t k = 0; k < s.cell_count(); ++k) {
                    surface_cell(s, k / nc, k % nc, tf, cell);
                    const Color col = surface_cell_color(s, cell, vmin, vmax);

                    // Two triangles on the 0-2 diagonal, fixed in data space so
                    // the buffer stays camera-independent.
                    const int tri[6] = {0, 1, 2, 0, 2, 3};
                    for (const int t: tri) {
                        verts.push_back(static_cast<float>(cell.p[t].x - anchor.x));
                        verts.push_back(static_cast<float>(cell.p[t].y - anchor.y));
                        verts.push_back(static_cast<float>(cell.p[t].z - anchor.z));
                        verts.push_back(col.r);
                        verts.push_back(col.g);
                        verts.push_back(col.b);
                        verts.push_back(col.a);
                    }

                    if (s.opts.edges) {
                        Vec3 seg[4][2];
                        surface_cell_edges(cell, seg);
                        for (const auto& e: seg)
                            for (const Vec3& p: e) {
                                edges.push_back(static_cast<float>(p.x - anchor.x));
                                edges.push_back(static_cast<float>(p.y - anchor.y));
                                edges.push_back(static_cast<float>(p.z - anchor.z));
                            }
                    }
                }

                if (!c.vbo) glGenBuffers(1, &c.vbo);
                glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                             verts.data(), GL_STATIC_DRAW);
                c.verts = static_cast<int>(verts.size() / 7);

                c.edge_segs = 0;
                if (!edges.empty()) {
                    if (!c.edge_vbo) glGenBuffers(1, &c.edge_vbo);
                    glBindBuffer(GL_ARRAY_BUFFER, c.edge_vbo);
                    glBufferData(GL_ARRAY_BUFFER,
                                 static_cast<GLsizeiptr>(edges.size() * sizeof(float)),
                                 edges.data(), GL_STATIC_DRAW);
                    c.edge_segs = static_cast<int>(edges.size() / 6);
                }

                c.data_generation = data_generation_;
                c.anchor = anchor;
                c.shading = s.opts.shading;
                c.alpha = s.opts.alpha;
                c.color = *s.opts.color;
                c.colormap = s.opts.colormap;
                c.cmap = s.opts.cmap;
                c.vmin = vmin;
                c.vmax = vmax;
                c.edges = s.opts.edges;
                c.axis_signs = signs;
            }
            if (c.verts == 0) continue;

            float box_scale[3], box_offset[3];
            proj.box_affine(c.anchor, box_scale, box_offset);

            const Surface3DUniforms& su = peel_.active ? peel_surface_u_ : surface_u_;
            glUseProgram(peel_.active ? peel_surface_program_ : surface_program_);
            glUniformMatrix4fv(su.clip, 1, GL_FALSE, clip.data());
            glUniform3fv(su.box_scale, 1, box_scale);
            glUniform3fv(su.box_offset, 1, box_offset);

            glBindVertexArray(surface_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                                  reinterpret_cast<void *>(3 * sizeof(float)));

            if (!translucent || peel_.active) {
                // Under peeling a translucent sheet draws like an opaque one.
                glDepthMask(GL_TRUE);
                glDrawArrays(GL_TRIANGLES, 0, c.verts);
            } else {
                // Back to front by the grid (exact per surface), rebuilt per frame.
                surface_draw_order(s, proj, order);
                indices.clear();
                indices.reserve(order.size() * 6);
                for (const std::size_t k: order)
                    for (int t = 0; t < 6; ++t)
                        indices.push_back(static_cast<std::uint32_t>(k * 6 + t));
                if (!c.index_ebo) glGenBuffers(1, &c.index_ebo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.index_ebo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                             indices.data(), GL_STREAM_DRAW);
                c.indices = static_cast<int>(indices.size());
                glDepthMask(GL_FALSE);
                glDrawElements(GL_TRIANGLES, c.indices, GL_UNSIGNED_INT, nullptr);
            }

            if (s.opts.edges && c.edge_segs > 0) {
                // Wireframe via the bar edge program (world-space ribbon).
                const double half = 0.5 * s.opts.edge_linewidth *
                                    proj.box_units_per_pixel(Vec3{0.0, 0.0, 0.0});
                const Vec3 eye = proj.has_eye_point() ? proj.eye_point() : proj.eye_dir();

                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -2.0f);
                const Bar3DEdgeUniforms& eu = peel_.active ? peel_edge_u_ : bar3d_edge_u_;
                glUseProgram(peel_.active ? peel_edge_program_ : bar3d_edge_program_);
                glUniformMatrix4fv(eu.clip, 1, GL_FALSE, clip.data());
                glUniform3fv(eu.box_scale, 1, box_scale);
                glUniform3fv(eu.box_offset, 1, box_offset);
                const float ec[4] = {
                    s.opts.edgecolor.r, s.opts.edgecolor.g,
                    s.opts.edgecolor.b, surface_edge_alpha(s)
                };
                glUniform4fv(eu.color, 1, ec);
                const float eyef[3] = {
                    static_cast<float>(eye.x), static_cast<float>(eye.y),
                    static_cast<float>(eye.z)
                };
                glUniform3fv(eu.eye, 1, eyef);
                glUniform1i(eu.persp, proj.has_eye_point() ? 1 : 0);
                glUniform1f(eu.half_width, static_cast<float>(half));

                glBindVertexArray(bar3d_edge_vao_);
                glBindBuffer(GL_ARRAY_BUFFER, c.edge_vbo);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
                glVertexAttribDivisor(1, 1);
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                      reinterpret_cast<void *>(3 * sizeof(float)));
                glVertexAttribDivisor(2, 1);
                glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, c.edge_segs);
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            glDepthMask(GL_TRUE);
        }

        glBindVertexArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glUseProgram(0);
        glDisable(GL_DEPTH_TEST);
        end_pass();
    }

    // -------------------------------------------------------------------------
    // 3D meshes
    // -------------------------------------------------------------------------
    // As surfaces, but the colormap is sampled per fragment (k_surface_tri_frag),
    // and the translucent unpeeled face order is a heuristic. Opaque: one
    // unordered draw.
    void DataRenderer::draw_surface_tri3d(const std::vector<SurfaceTriPlot>& meshes,
                                          const Projector3D& proj, const PlotRect& pr,
                                          float win_w, float win_h, ScenePass pass,
                                          const std::vector<std::size_t>* explicit_order) {
        if (meshes.empty()) return;
        const bool want_translucent = (pass == ScenePass::Translucent);

        begin_pass(pr, win_h);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        const std::array<float, 16> clip = proj.clip_matrix(win_w, win_h);
        const Transform3D& tf = proj.transform();

        // This pass's plots, far to near when translucent (or draw_scene3d()'s
        // explicit order).
        std::vector<std::size_t> plot_order;
        if (explicit_order) {
            plot_order = *explicit_order;
        } else {
            plot_order.reserve(meshes.size());
            for (std::size_t i = 0; i < meshes.size(); ++i)
                if (surface_tri_translucent(meshes[i]) == want_translucent) plot_order.push_back(i);
            // Nothing to sort under peeling.
            if (want_translucent && !peel_.active)
                std::stable_sort(plot_order.begin(), plot_order.end(),
                                 [&](std::size_t a, std::size_t b) {
                                     return surface_tri_plot_distance(meshes[a], proj) >
                                            surface_tri_plot_distance(meshes[b], proj);
                                 });
        }

        std::vector<std::size_t> order;
        std::vector<std::uint32_t> indices;
        for (const std::size_t mi: plot_order) {
            const SurfaceTriPlot& s = meshes[mi];
            if (!object_shown(s) || s.face_count() == 0 || s.count() == 0) continue;
            const bool translucent = surface_tri_translucent(s);

            const CacheKey key{axes_index_, -1, static_cast<int>(mi)};
            SurfaceTriCache& c = surface_tri_cache_[key];

            double vmin = 0.0, vmax = 1.0;
            surface_tri_value_range(s, vmin, vmax);

            // The key holds what's baked (shade and axis signs); camera, limits and
            // the color lookup are not.
            const int signs = (tf.xmax < tf.xmin ? 1 : 0)
                              | (tf.ymax < tf.ymin ? 2 : 0)
                              | (tf.zmax < tf.zmin ? 4 : 0);
            const bool stale = c.vbo == 0 || data_generation_ == 0 ||
                               c.data_generation != data_generation_ ||
                               c.shading != s.opts.shading ||
                               c.alpha != s.opts.alpha ||
                               c.color.r != s.opts.color->r ||
                               c.color.g != s.opts.color->g ||
                               c.color.b != s.opts.color->b ||
                               c.color.a != s.opts.color->a ||
                               c.colormapped != s.colormapped() ||
                               c.cmap != s.opts.cmap ||
                               c.vmin != vmin || c.vmax != vmax ||
                               c.axis_signs != signs ||
                               c.edges != s.opts.edges;

            if (stale) {
                // Anchor at the data's middle, for float precision.
                double lo[3], hi[3]; {
                    const Vec3 p0 = s.vertex(0);
                    const double c0[3] = {p0.x, p0.y, p0.z};
                    for (int a = 0; a < 3; ++a) {
                        lo[a] = c0[a];
                        hi[a] = c0[a];
                    }
                    for (std::size_t i = 1; i < s.count(); ++i) {
                        const Vec3 p = s.vertex(i);
                        const double cc[3] = {p.x, p.y, p.z};
                        for (int a = 0; a < 3; ++a) {
                            lo[a] = std::min(lo[a], cc[a]);
                            hi[a] = std::max(hi[a], cc[a]);
                        }
                    }
                }
                const Vec3 anchor{
                    (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5,
                    (lo[2] + hi[2]) * 0.5
                };

                std::vector<float> verts;
                std::vector<float> edges;
                verts.reserve(s.face_count() * 3 * 9);

                SurfaceTriFace face;
                for (std::size_t f = 0; f < s.face_count(); ++f) {
                    surface_tri_face(s, f, tf, face);
                    // Expanded to 3 vertices per face (per-face shade), so `tri`
                    // is never uploaded.
                    for (int k = 0; k < 3; ++k) {
                        const Color col = surface_tri_vertex_color(s, face.vert[k], vmin, vmax);
                        const double t = s.colormapped()
                                             ? surface_tri_vertex_t(s, face.vert[k], vmin, vmax)
                                             : 0.0;
                        verts.push_back(static_cast<float>(face.p[k].x - anchor.x));
                        verts.push_back(static_cast<float>(face.p[k].y - anchor.y));
                        verts.push_back(static_cast<float>(face.p[k].z - anchor.z));
                        verts.push_back(col.r);
                        verts.push_back(col.g);
                        verts.push_back(col.b);
                        verts.push_back(col.a);
                        verts.push_back(static_cast<float>(t));
                        verts.push_back(face.shade);
                    }

                    if (s.opts.edges) {
                        Vec3 seg[3][2];
                        surface_tri_face_edges(face, seg);
                        for (const auto& e: seg)
                            for (const Vec3& p: e) {
                                edges.push_back(static_cast<float>(p.x - anchor.x));
                                edges.push_back(static_cast<float>(p.y - anchor.y));
                                edges.push_back(static_cast<float>(p.z - anchor.z));
                            }
                    }
                }

                if (!c.vbo) glGenBuffers(1, &c.vbo);
                glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                             verts.data(), GL_STATIC_DRAW);
                c.verts = static_cast<int>(verts.size() / 9);

                c.edge_segs = 0;
                if (!edges.empty()) {
                    if (!c.edge_vbo) glGenBuffers(1, &c.edge_vbo);
                    glBindBuffer(GL_ARRAY_BUFFER, c.edge_vbo);
                    glBufferData(GL_ARRAY_BUFFER,
                                 static_cast<GLsizeiptr>(edges.size() * sizeof(float)),
                                 edges.data(), GL_STATIC_DRAW);
                    c.edge_segs = static_cast<int>(edges.size() / 6);
                }

                c.data_generation = data_generation_;
                c.anchor = anchor;
                c.shading = s.opts.shading;
                c.alpha = s.opts.alpha;
                c.color = *s.opts.color;
                c.colormapped = s.colormapped();
                c.cmap = s.opts.cmap;
                c.vmin = vmin;
                c.vmax = vmax;
                c.edges = s.opts.edges;
                c.axis_signs = signs;
            }
            if (c.verts == 0) continue;

            float box_scale[3], box_offset[3];
            proj.box_affine(c.anchor, box_scale, box_offset);

            const SurfaceTriUniforms& su = peel_.active ? peel_surface_tri_u_ : surface_tri_u_;
            glUseProgram(peel_.active ? peel_surface_tri_program_ : surface_tri_program_);
            glUniformMatrix4fv(su.clip, 1, GL_FALSE, clip.data());
            glUniform3fv(su.box_scale, 1, box_scale);
            glUniform3fv(su.box_offset, 1, box_offset);
            glUniform1i(su.colormapped, !s.colormapped() ? 0 : s.opts.cmap.is_cyclic() ? 2 : 1);

            if (s.colormapped()) {
                // 256x1 colormap texture, re-uploaded only when the map changes.
                glActiveTexture(GL_TEXTURE0);
                if (!surface_tri_cmap_tex_) {
                    glGenTextures(1, &surface_tri_cmap_tex_);
                    glBindTexture(GL_TEXTURE_2D, surface_tri_cmap_tex_);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    surface_tri_cmap_in_tex_.reset();
                }
                if (surface_tri_cmap_in_tex_ != s.opts.cmap) {
                    glBindTexture(GL_TEXTURE_2D, surface_tri_cmap_tex_);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA,
                                 GL_UNSIGNED_BYTE, colormaps::get(s.opts.cmap));
                    surface_tri_cmap_in_tex_ = s.opts.cmap;
                }
                glBindTexture(GL_TEXTURE_2D, surface_tri_cmap_tex_);
                glUniform1i(su.cmap, 0);
            }

            glBindVertexArray(surface_tri_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), nullptr);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                                  reinterpret_cast<void *>(3 * sizeof(float)));
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                                  reinterpret_cast<void *>(7 * sizeof(float)));
            glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                                  reinterpret_cast<void *>(8 * sizeof(float)));

            if (!translucent || peel_.active) {
                // Under peeling a translucent mesh draws like an opaque one; the
                // face order is only for the fallback.
                glDepthMask(GL_TRUE);
                glDrawArrays(GL_TRIANGLES, 0, c.verts);
            } else {
                surface_tri_draw_order(s, proj, order);
                indices.clear();
                indices.reserve(order.size() * 3);
                for (const std::size_t f: order)
                    for (int k = 0; k < 3; ++k)
                        indices.push_back(static_cast<std::uint32_t>(f * 3 + k));
                if (!c.index_ebo) glGenBuffers(1, &c.index_ebo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.index_ebo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                             indices.data(), GL_STREAM_DRAW);
                c.indices = static_cast<int>(indices.size());
                glDepthMask(GL_FALSE);
                glDrawElements(GL_TRIANGLES, c.indices, GL_UNSIGNED_INT, nullptr);
            }

            if (s.opts.edges && c.edge_segs > 0) {
                // Wireframe via the bar edge program (world-space ribbon).
                const double half = 0.5 * s.opts.edge_linewidth *
                                    proj.box_units_per_pixel(Vec3{0.0, 0.0, 0.0});
                const Vec3 eye = proj.has_eye_point() ? proj.eye_point() : proj.eye_dir();

                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -2.0f);
                const Bar3DEdgeUniforms& eu = peel_.active ? peel_edge_u_ : bar3d_edge_u_;
                glUseProgram(peel_.active ? peel_edge_program_ : bar3d_edge_program_);
                glUniformMatrix4fv(eu.clip, 1, GL_FALSE, clip.data());
                glUniform3fv(eu.box_scale, 1, box_scale);
                glUniform3fv(eu.box_offset, 1, box_offset);
                const float ec[4] = {
                    s.opts.edgecolor.r, s.opts.edgecolor.g,
                    s.opts.edgecolor.b, surface_tri_edge_alpha(s)
                };
                glUniform4fv(eu.color, 1, ec);
                const float eyef[3] = {
                    static_cast<float>(eye.x), static_cast<float>(eye.y),
                    static_cast<float>(eye.z)
                };
                glUniform3fv(eu.eye, 1, eyef);
                glUniform1i(eu.persp, proj.has_eye_point() ? 1 : 0);
                glUniform1f(eu.half_width, static_cast<float>(half));

                glBindVertexArray(bar3d_edge_vao_);
                glBindBuffer(GL_ARRAY_BUFFER, c.edge_vbo);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
                glVertexAttribDivisor(1, 1);
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                      reinterpret_cast<void *>(3 * sizeof(float)));
                glVertexAttribDivisor(2, 1);
                glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, c.edge_segs);
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            glDepthMask(GL_TRUE);
        }

        glBindVertexArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
        glDisable(GL_DEPTH_TEST);
        end_pass();
    }

    void DataRenderer::draw_scatter3d(const std::vector<Scatter3DPlot>& points,
                                      const Projector3D& proj, const PlotRect& pr,
                                      float win_w, float win_h, ScenePass pass,
                                      const std::vector<std::size_t>* explicit_order) {
        if (points.empty()) return;
        const bool want_translucent = (pass == ScenePass::Translucent);

        begin_pass(pr, win_h);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        const std::array<float, 16> clip = proj.clip_matrix(win_w, win_h);

        // The depth ramp, once per draw (see depth_affine()).
        float dmin = 0.0f, dmax = 1.0f;
        box_depth_range(proj, dmin, dmax);
        Vec3 ddir{};
        float dbase = 0.0f;
        depth_affine(proj, ddir, dbase);
        const float dspan = (dmax - dmin) != 0.0f ? 1.0f / (dmax - dmin) : 0.0f;

        std::vector<std::size_t> plot_order;
        if (explicit_order) {
            plot_order = *explicit_order;
        } else {
            plot_order.reserve(points.size());
            for (std::size_t i = 0; i < points.size(); ++i)
                if (scatter3d_translucent(points[i]) == want_translucent) plot_order.push_back(i);
            if (want_translucent && !peel_.active)
                std::stable_sort(plot_order.begin(), plot_order.end(),
                                 [&](std::size_t a, std::size_t b) {
                                     return scatter3d_plot_distance(points[a], proj) >
                                            scatter3d_plot_distance(points[b], proj);
                                 });
        }

        std::vector<std::size_t> order;
        std::vector<float> sorted;
        for (const std::size_t si: plot_order) {
            const Scatter3DPlot& s = points[si];
            if (!object_shown(s) || s.count() == 0 || s.opts.marker == MarkerStyle::None) continue;
            const bool translucent = scatter3d_translucent(s);

            const CacheKey key{axes_index_, -1, static_cast<int>(si)};
            Scatter3DCache& c = scatter3d_cache_[key];

            double vmin = 0.0, vmax = 1.0;
            scatter3d_value_range(s, vmin, vmax);

            const bool stale = c.vbo == 0 || data_generation_ == 0 ||
                               c.data_generation != data_generation_ ||
                               c.size != s.opts.size ||
                               c.alpha != marker_face_alpha(s.opts) ||
                               c.color.r != s.opts.color->r ||
                               c.color.g != s.opts.color->g ||
                               c.color.b != s.opts.color->b ||
                               c.color.a != s.opts.color->a ||
                               c.colormapped != s.colormapped() ||
                               c.cmap != s.opts.cmap ||
                               c.vmin != vmin || c.vmax != vmax;

            if (stale) {
                // Anchor at the bounding-box centre, for float precision.
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
                for (std::size_t i = 0; i < s.count(); ++i) {
                    const double p[3] = {s.x[i], s.y[i], s.z[i]};
                    for (int a = 0; a < 3; ++a) {
                        lo[a] = std::min(lo[a], p[a]);
                        hi[a] = std::max(hi[a], p[a]);
                    }
                }
                const Vec3 anchor{
                    (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5,
                    (lo[2] + hi[2]) * 0.5
                };

                std::vector<float> verts;
                verts.reserve(s.count() * 8);
                for (std::size_t i = 0; i < s.count(); ++i) {
                    const Color col = scatter3d_point_color(s, i, vmin, vmax);
                    verts.push_back(static_cast<float>(s.x[i] - anchor.x));
                    verts.push_back(static_cast<float>(s.y[i] - anchor.y));
                    verts.push_back(static_cast<float>(s.z[i] - anchor.z));
                    verts.push_back(marker_size_at(s, i));   // 0 hides a point
                    verts.push_back(col.r);
                    verts.push_back(col.g);
                    verts.push_back(col.b);
                    verts.push_back(col.a);
                }

                if (!c.vbo) glGenBuffers(1, &c.vbo);
                glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                             verts.data(), GL_STATIC_DRAW);
                c.points = static_cast<int>(s.count());
                // Kept CPU-side only when the sort needs it.
                c.host = scatter3d_translucent(s) ? std::move(verts) : std::vector<float>{};

                c.data_generation = data_generation_;
                c.anchor = anchor;
                c.size = s.opts.size;
                c.alpha = marker_face_alpha(s.opts);
                c.color = *s.opts.color;
                c.colormapped = s.colormapped();
                c.cmap = s.opts.cmap;
                c.vmin = vmin;
                c.vmax = vmax;
            }
            if (c.points == 0) continue;

            float box_scale[3], box_offset[3];
            proj.box_affine(c.anchor, box_scale, box_offset);

            const Scatter3DUniforms& su = peel_.active ? peel_scatter3d_u_ : scatter3d_u_;
            glUseProgram(peel_.active ? peel_scatter3d_program_ : scatter3d_program_);
            glUniformMatrix4fv(su.clip, 1, GL_FALSE, clip.data());
            glUniform3fv(su.box_scale, 1, box_scale);
            glUniform3fv(su.box_offset, 1, box_offset);
            glUniform2f(su.resolution, win_w, win_h);
            glUniform1i(su.marker, static_cast<int>(s.opts.marker));
            glUniform4f(su.depth, static_cast<float>(ddir.x), static_cast<float>(ddir.y),
                        static_cast<float>(ddir.z), dbase);
            glUniform3f(su.shade, std::clamp(s.opts.depthshade, 0.0f, 1.0f), dmin, dspan);
            set_marker_edge(su.edge, su.edge_w, su.edge_face,
                            marker_edge(s.opts.edgecolor, s.opts.alpha,
                                        s.opts.edge_linewidth,
                                        s.colormapped() ? Color{0.0f, 0.0f, 0.0f, 1.0f}
                                                        : *s.opts.color));

            glBindVertexArray(scatter3d_vao_);

            // Opaque or peeled: the buffer as baked.
            unsigned int inst = c.vbo;
            if (translucent && !peel_.active) {
                // Back to front (exact for billboards), as a reordered instance
                // copy (instanced draws have no index path).
                scatter3d_draw_order(s, proj, order);
                sorted.clear();
                sorted.reserve(order.size() * 8);
                for (const std::size_t k: order) {
                    const std::size_t b = k * 8;
                    if (b + 8 > c.host.size()) continue;
                    sorted.insert(sorted.end(), c.host.begin() + static_cast<std::ptrdiff_t>(b),
                                  c.host.begin() + static_cast<std::ptrdiff_t>(b + 8));
                }
                if (!c.order_vbo) glGenBuffers(1, &c.order_vbo);
                glBindBuffer(GL_ARRAY_BUFFER, c.order_vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(sorted.size() * sizeof(float)),
                             sorted.data(), GL_STREAM_DRAW);
                inst = c.order_vbo;
                glDepthMask(GL_FALSE);
            }

            glBindBuffer(GL_ARRAY_BUFFER, inst);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
            glVertexAttribDivisor(1, 1);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                                  reinterpret_cast<void *>(3 * sizeof(float)));
            glVertexAttribDivisor(2, 1);
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                                  reinterpret_cast<void *>(4 * sizeof(float)));
            glVertexAttribDivisor(3, 1);

            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, c.points);
            glDepthMask(GL_TRUE);
        }

        glBindVertexArray(0);
        glUseProgram(0);
        glDisable(GL_DEPTH_TEST);
        end_pass();
    }

    // Paths: one mitered world-space ribbon per segment. Same cache and fallback
    // scheme as draw_scatter3d(), with each instance carrying its neighbours.
    void DataRenderer::draw_lines3d(const std::vector<Line3DPlot>& lines, const Projector3D& proj,
                                    const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                                    const std::vector<std::size_t>* explicit_order) {
        if (lines.empty()) return;
        const bool want_translucent = (pass == ScenePass::Translucent);

        begin_pass(pr, win_h);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        const std::array<float, 16> clip = proj.clip_matrix(win_w, win_h);

        float dmin = 0.0f, dmax = 1.0f;
        box_depth_range(proj, dmin, dmax);
        Vec3 ddir{};
        float dbase = 0.0f;
        depth_affine(proj, ddir, dbase);
        const float dspan = (dmax - dmin) != 0.0f ? 1.0f / (dmax - dmin) : 0.0f;

        // Ribbon half-width in box units, measured at the box centre (see
        // line3d_half_width()); a uniform.
        const Vec3 eye = proj.has_eye_point() ? proj.eye_point() : proj.eye_dir();

        std::vector<std::size_t> plot_order;
        if (explicit_order) {
            plot_order = *explicit_order;
        } else {
            plot_order.reserve(lines.size());
            for (std::size_t i = 0; i < lines.size(); ++i)
                if (line3d_translucent(lines[i]) == want_translucent) plot_order.push_back(i);
            if (want_translucent && !peel_.active)
                std::stable_sort(plot_order.begin(), plot_order.end(),
                                 [&](std::size_t a, std::size_t b) {
                                     return line3d_plot_distance(lines[a], proj) >
                                            line3d_plot_distance(lines[b], proj);
                                 });
        }

        // 22 floats per instance: prev, a, b, next, a color at each end, and the
        // two ends' normalized colormap values.
        constexpr std::size_t kStride = 22;

        std::vector<std::size_t> order;
        std::vector<float> sorted;
        for (const std::size_t li: plot_order) {
            const Line3DPlot& l = lines[li];
            if (!object_shown(l) || l.segment_count() == 0) continue;
            const bool translucent = line3d_translucent(l);

            const CacheKey key{axes_index_, -1, static_cast<int>(li)};
            Line3DCache& c = line3d_cache_[key];

            double vmin = 0.0, vmax = 1.0;
            line3d_value_range(l, vmin, vmax);

            const bool stale = c.vbo == 0 || data_generation_ == 0 ||
                               c.data_generation != data_generation_ ||
                               c.alpha != l.opts.alpha ||
                               c.color.r != l.opts.color->r ||
                               c.color.g != l.opts.color->g ||
                               c.color.b != l.opts.color->b ||
                               c.color.a != l.opts.color->a ||
                               c.colormapped != l.colormapped() ||
                               c.loop != l.opts.loop ||
                               c.cmap != l.opts.cmap ||
                               c.vmin != vmin || c.vmax != vmax;

            if (stale) {
                // Anchor at the bounding-box centre, for float precision.
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
                for (std::size_t i = 0; i < l.count(); ++i) {
                    const double p[3] = {l.x[i], l.y[i], l.z[i]};
                    for (int a = 0; a < 3; ++a) {
                        lo[a] = std::min(lo[a], p[a]);
                        hi[a] = std::max(hi[a], p[a]);
                    }
                }
                const Vec3 anchor{
                    (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5,
                    (lo[2] + hi[2]) * 0.5
                };

                const std::size_t n = l.count();
                const std::size_t segs = l.segment_count();
                auto put = [&](std::vector<float>& v, std::size_t i) {
                    v.push_back(static_cast<float>(l.x[i] - anchor.x));
                    v.push_back(static_cast<float>(l.y[i] - anchor.y));
                    v.push_back(static_cast<float>(l.z[i] - anchor.z));
                };

                std::vector<float> verts;
                verts.reserve(segs * kStride);
                for (std::size_t s = 0; s < segs; ++s) {
                    std::size_t a = 0, b = 0;
                    l.segment_ends(s, a, b);
                    // A missing neighbour is written as the end point itself, which
                    // makes that end square in the shader.
                    const std::size_t prev = (a == 0) ? (l.opts.loop ? n - 1 : a) : a - 1;
                    const std::size_t next = (b + 1 >= n)
                                                 ? (l.opts.loop ? (b + 1) % n : b)
                                                 : b + 1;
                    put(verts, prev);
                    put(verts, a);
                    put(verts, b);
                    put(verts, next);
                    const Color ca = line3d_point_color(l, a, vmin, vmax);
                    const Color cb = line3d_point_color(l, b, vmin, vmax);
                    verts.push_back(ca.r);
                    verts.push_back(ca.g);
                    verts.push_back(ca.b);
                    verts.push_back(ca.a);
                    verts.push_back(cb.r);
                    verts.push_back(cb.g);
                    verts.push_back(cb.b);
                    verts.push_back(cb.a);
                    // Normalized as line3d_point_color() does; zero for flat series. A
                    // cyclic map's value goes unwrapped, so a segment crossing vmax
                    // runs on round the map; the shader wraps it per fragment.
                    const double span = vmax - vmin;
                    const bool cyclic = l.opts.cmap.is_cyclic();
                    auto norm = [&](std::size_t i) {
                        if (!l.colormapped() || span == 0.0) return 0.0f;
                        const double t = (l.color_at(i) - vmin) / span;
                        return static_cast<float>(cyclic ? t : std::clamp(t, 0.0, 1.0));
                    };
                    verts.push_back(norm(a));
                    verts.push_back(norm(b));
                }

                if (!c.vbo) glGenBuffers(1, &c.vbo);
                glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                             verts.data(), GL_STATIC_DRAW);
                c.segs = static_cast<int>(segs);
                c.host = translucent ? std::move(verts) : std::vector<float>{};

                c.data_generation = data_generation_;
                c.anchor = anchor;
                c.alpha = l.opts.alpha;
                c.color = *l.opts.color;
                c.colormapped = l.colormapped();
                c.loop = l.opts.loop;
                c.cmap = l.opts.cmap;
                c.vmin = vmin;
                c.vmax = vmax;
            }
            if (c.segs == 0) continue;

            float box_scale[3], box_offset[3];
            proj.box_affine(c.anchor, box_scale, box_offset);

            const Line3DUniforms& lu = peel_.active ? peel_line3d_u_ : line3d_u_;
            glUseProgram(peel_.active ? peel_line3d_program_ : line3d_program_);
            glUniformMatrix4fv(lu.clip, 1, GL_FALSE, clip.data());
            glUniform3fv(lu.box_scale, 1, box_scale);
            glUniform3fv(lu.box_offset, 1, box_offset);
            const float eyef[3] = {
                static_cast<float>(eye.x), static_cast<float>(eye.y),
                static_cast<float>(eye.z)
            };
            glUniform3fv(lu.eye, 1, eyef);
            glUniform1i(lu.persp, proj.has_eye_point() ? 1 : 0);
            glUniform1f(lu.half_width, static_cast<float>(line3d_half_width(l, proj)));
            glUniform4f(lu.depth, static_cast<float>(ddir.x), static_cast<float>(ddir.y),
                        static_cast<float>(ddir.z), dbase);
            glUniform3f(lu.shade, std::clamp(l.opts.depthshade, 0.0f, 1.0f), dmin, dspan);
            glUniform1i(lu.colormapped, !l.colormapped() ? 0 : l.opts.cmap.is_cyclic() ? 2 : 1);
            if (l.colormapped()) {
                // Unit 3 (0 = plane raster, 1/2 = peel depth textures).
                if (!line3d_cmap_tex_) {
                    glGenTextures(1, &line3d_cmap_tex_);
                    glBindTexture(GL_TEXTURE_2D, line3d_cmap_tex_);
                    // Linear filtering, so the ramp is continuous.
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                }
                if (line3d_cmap_in_tex_ != l.opts.cmap) {
                    glBindTexture(GL_TEXTURE_2D, line3d_cmap_tex_);
                    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0,
                                 GL_RGBA, GL_UNSIGNED_BYTE, colormaps::get(l.opts.cmap));
                    line3d_cmap_in_tex_ = l.opts.cmap;
                }
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, line3d_cmap_tex_);
                glActiveTexture(GL_TEXTURE0);
                glUniform1i(lu.cmap, 3);
            }

            glBindVertexArray(line3d_vao_);

            unsigned int inst = c.vbo;
            if (translucent && !peel_.active) {
                // Back to front by segment midpoint (a heuristic), as a reordered
                // instance copy.
                line3d_draw_order(l, proj, order);
                sorted.clear();
                sorted.reserve(order.size() * kStride);
                for (const std::size_t k: order) {
                    const std::size_t b = k * kStride;
                    if (b + kStride > c.host.size()) continue;
                    sorted.insert(sorted.end(), c.host.begin() + static_cast<std::ptrdiff_t>(b),
                                  c.host.begin() + static_cast<std::ptrdiff_t>(b + kStride));
                }
                if (!c.order_vbo) glGenBuffers(1, &c.order_vbo);
                glBindBuffer(GL_ARRAY_BUFFER, c.order_vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(sorted.size() * sizeof(float)),
                             sorted.data(), GL_STREAM_DRAW);
                inst = c.order_vbo;
                glDepthMask(GL_FALSE);
            }

            glBindBuffer(GL_ARRAY_BUFFER, inst);
            const GLsizei stride = static_cast<GLsizei>(kStride * sizeof(float));
            for (int a = 0; a < 4; ++a) {
                // prev, a, b, next
                glEnableVertexAttribArray(1 + a);
                glVertexAttribPointer(1 + a, 3, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<void *>(a * 3 * sizeof(float)));
                glVertexAttribDivisor(1 + a, 1);
            }
            for (int a = 0; a < 2; ++a) {
                // the two end colours
                glEnableVertexAttribArray(5 + a);
                glVertexAttribPointer(5 + a, 4, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<void *>((12 + a * 4) * sizeof(float)));
                glVertexAttribDivisor(5 + a, 1);
            }
            glEnableVertexAttribArray(7); // the two ends' values
            glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>(20 * sizeof(float)));
            glVertexAttribDivisor(7, 1);

            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, c.segs);
            glDepthMask(GL_TRUE);
        }

        glBindVertexArray(0);
        glUseProgram(0);
        glDisable(GL_DEPTH_TEST);
        end_pass();
    }

    // 3D error bars. The buffer is camera-dependent, so the cache is keyed on the
    // view too (see ErrorBar3DCache).
    void DataRenderer::draw_errorbars3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                                        const PlotRect& pr, float win_w, float win_h,
                                        ScenePass pass,
                                        const std::vector<std::size_t>* explicit_order) {
        const std::size_t ns = snap.scatter3d.size();
        const std::size_t total = ns + snap.lines3d.size();
        if (total == 0) return;
        const bool want_translucent = (pass == ScenePass::Translucent);

        // Series k is a cloud for k < ns, else a path.
        // A hidden series hides its error bars too.
        auto drawn = [&](std::size_t k) {
            return k < ns
                       ? object_shown(snap.scatter3d[k])
                             && errorbar3d_drawn(snap.scatter3d[k].err, snap.scatter3d[k].opts.errorbar)
                       : object_shown(snap.lines3d[k - ns])
                             && errorbar3d_drawn(snap.lines3d[k - ns].err,
                                                 snap.lines3d[k - ns].opts.errorbar);
        };
        auto translucent = [&](std::size_t k) {
            return k < ns
                       ? errorbar3d_translucent(snap.scatter3d[k])
                       : errorbar3d_translucent(snap.lines3d[k - ns]);
        };
        auto distance = [&](std::size_t k) {
            return k < ns
                       ? scatter3d_plot_distance(snap.scatter3d[k], proj)
                       : line3d_plot_distance(snap.lines3d[k - ns], proj);
        };

        std::vector<std::size_t> series;
        if (explicit_order) {
            series = *explicit_order;
        } else {
            for (std::size_t k = 0; k < total; ++k)
                if (drawn(k) && (!want_translucent || translucent(k))) series.push_back(k);
            if (want_translucent && !peel_.active)
                std::stable_sort(series.begin(), series.end(),
                                 [&](std::size_t a, std::size_t b) {
                                     return distance(a) > distance(b);
                                 });
        }
        if (series.empty()) return;

        const std::array<float, 16> clip = proj.clip_matrix(win_w, win_h);
        const Vec3 eye = proj.has_eye_point() ? proj.eye_point() : proj.eye_dir();

        float dmin = 0.0f, dmax = 1.0f;
        box_depth_range(proj, dmin, dmax);
        Vec3 ddir{};
        float dbase = 0.0f;
        depth_affine(proj, ddir, dbase);
        const float dspan = (dmax - dmin) != 0.0f ? 1.0f / (dmax - dmin) : 0.0f;

        begin_pass(pr, win_h);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        const ErrorBar3DUniforms& u = peel_.active ? peel_errbar3d_u_ : errbar3d_u_;
        glUseProgram(peel_.active ? peel_errbar3d_program_ : errbar3d_program_);
        glUniformMatrix4fv(u.clip, 1, GL_FALSE, clip.data());
        glUniform4f(u.depth, static_cast<float>(ddir.x), static_cast<float>(ddir.y),
                    static_cast<float>(ddir.z), dbase);
        glBindVertexArray(errbar3d_vao_);

        std::vector<ErrorBar3DPiece> pieces;
        std::vector<float> opaque, trans;
        for (const std::size_t k: series) {
            if (k >= total || !drawn(k)) continue;
            const bool is_scatter = k < ns;
            const ErrorBar3DOptions& st = is_scatter
                                              ? snap.scatter3d[k].opts.errorbar
                                              : snap.lines3d[k - ns].opts.errorbar;
            const Color col = is_scatter
                                  ? errorbar3d_color(snap.scatter3d[k])
                                  : errorbar3d_color(snap.lines3d[k - ns]);
            const float depthshade = is_scatter
                                         ? snap.scatter3d[k].opts.depthshade
                                         : snap.lines3d[k - ns].opts.depthshade;

            // Everything besides data the triangles depend on (view, style,
            // color), compared whole.
            std::vector<double> view;
            view.reserve(40);
            for (float v: clip) view.push_back(v);
            view.insert(view.end(), {
                            eye.x, eye.y, eye.z,
                            proj.has_eye_point() ? 1.0 : 0.0,
                            proj.box_units_per_pixel(Vec3{0.0, 0.0, 0.0}),
                            static_cast<double>(st.linewidth),
                            static_cast<double>(st.capsize),
                            static_cast<double>(st.capstyle),
                            static_cast<double>(st.boxwidth),
                            static_cast<double>(st.box_alpha),
                            static_cast<double>(col.r), static_cast<double>(col.g),
                            static_cast<double>(col.b), static_cast<double>(col.a)
                        });

            const CacheKey key{
                axes_index_, is_scatter ? 0 : 1,
                static_cast<int>(is_scatter ? k : k - ns)
            };
            ErrorBar3DCache& c = errbar3d_cache_[key];
            const bool stale = data_generation_ == 0 || c.data_generation != data_generation_ ||
                               c.view != view;
            if (stale) {
                pieces.clear();
                if (is_scatter) errorbar3d_pieces(proj, snap.scatter3d[k], k, pieces);
                else errorbar3d_pieces(proj, snap.lines3d[k - ns], k - ns, pieces);

                auto emit = [](std::vector<float>& out, const Vec3 q[4], Color cc) {
                    constexpr int tri[6] = {0, 1, 2, 0, 2, 3};
                    for (int t: tri)
                        out.insert(out.end(), {
                                       static_cast<float>(q[t].x),
                                       static_cast<float>(q[t].y),
                                       static_cast<float>(q[t].z),
                                       cc.r, cc.g, cc.b, cc.a
                                   });
                };
                auto corners = [](const ErrorBar3DPiece& s, Vec3 q[4]) {
                    if (s.face) {
                        for (int r = 0; r < 4; ++r) q[r] = s.p[r];
                        return true;
                    }
                    return errorbar3d_ribbon(s, q);
                };

                // Translucent pieces far to near by centroid, for the unpeeled
                // fallback.
                std::vector<std::pair<float, const ErrorBar3DPiece *>> back;
                opaque.clear();
                trans.clear();
                for (const ErrorBar3DPiece& s: pieces) {
                    if (!(s.color.a > 0.0f)) continue;
                    if (s.color.a < 1.0f) {
                        const Vec3 mid = s.face
                                             ? (s.p[0] + s.p[2]) * 0.5
                                             : (s.p[0] + s.p[1]) * 0.5;
                        back.push_back({proj.project_box(mid).depth, &s});
                        continue;
                    }
                    Vec3 q[4];
                    if (corners(s, q)) emit(opaque, q, s.color);
                }
                std::stable_sort(back.begin(), back.end(),
                                 [](const auto& a, const auto& b) { return a.first > b.first; });
                for (const auto& entry: back) {
                    Vec3 q[4];
                    if (corners(*entry.second, q)) emit(trans, q, entry.second->color);
                }

                auto upload = [](unsigned int& vbo, int& verts, const std::vector<float>& src) {
                    verts = static_cast<int>(src.size() / 7);
                    if (src.empty()) return;
                    if (!vbo) glGenBuffers(1, &vbo);
                    glBindBuffer(GL_ARRAY_BUFFER, vbo);
                    glBufferData(GL_ARRAY_BUFFER,
                                 static_cast<GLsizeiptr>(src.size() * sizeof(float)),
                                 src.data(), GL_DYNAMIC_DRAW);
                };
                upload(c.opaque_vbo, c.opaque_verts, opaque);
                upload(c.trans_vbo, c.trans_verts, trans);
                c.data_generation = data_generation_;
                c.view = std::move(view);
            }

            const unsigned int vbo = want_translucent ? c.trans_vbo : c.opaque_vbo;
            const int verts = want_translucent ? c.trans_verts : c.opaque_verts;
            if (!vbo || verts <= 0) continue;

            glUniform3f(u.shade, std::clamp(depthshade, 0.0f, 1.0f), dmin, dspan);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            const GLsizei stride = static_cast<GLsizei>(7 * sizeof(float));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>(3 * sizeof(float)));
            if (want_translucent && !peel_.active) glDepthMask(GL_FALSE);
            glDrawArrays(GL_TRIANGLES, 0, verts);
            glDepthMask(GL_TRUE);
        }

        glBindVertexArray(0);
        glUseProgram(0);
        glDisable(GL_DEPTH_TEST);
        end_pass();
    }
} // namespace sextant
