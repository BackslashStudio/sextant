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
#include "error_bar_shape.h"
#include <glad/glad.h>
#include <vector>
#include <stdexcept>
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>


namespace sextant {

// -------------------------------------------------------------------------
// Shader sources
// -------------------------------------------------------------------------

// Shared by lines and bars, flat color.
//
// uScale/uOffset carry an optional data->logical-pixel transform, as in the
// scatter shaders. Bar *fills* use it, so their buffer is in data space and
// survives pan/zoom. Line strokes and bar *outlines* set it to identity:
// their geometry is already pixel-space, expanded by a width measured in
// pixels, which has no data-space expression.
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
static constexpr float k_identity_scale[2]  = { 1.0f, 1.0f };
static constexpr float k_identity_offset[2] = { 0.0f, 0.0f };

static constexpr char k_flat_frag[] = R"(
#version 410 core
uniform vec4 uColor;
out vec4 FragColor;
void main() { FragColor = uColor; }
)";

// Line strokes: one instance per segment, expanded to a quad in the vertex
// shader. The buffer holds nothing but the data points, so it is
// view-independent like scatter's; the transform, the stroke width and the
// join maths all live below.
//
// Each instance reads four consecutive points -- prev, p0, p1, next -- from
// the *same* buffer bound at four attribute offsets. The buffer is padded
// with a duplicate of the first and last point, so instance 0 sees
// prev == p0 and the last sees next == p1; both degenerate cases fall through
// to the plain segment normal, which is a butt cap.
//
// Joins are mitered. The `den > 1/kMiterLimit` guard matters: near a
// 180-degree reversal `na + nb` is catastrophic cancellation and its
// direction is noise, so `den` can come out negative and a naive
// `min(1/den, limit)` would fling the offset off-screen.
static constexpr char k_lineseg_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aCorner;   // x: 0 at p0, 1 at p1;  y: -1/+1 across
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

// Offset at a joint between two segments whose left normals are na and nb.
// Falls back to nb whenever the bisector is unusable.
vec2 joint_offset(vec2 na, vec2 nb) {
    vec2 m = na + nb;
    float ml = length(m);
    if (ml < 1e-3) return nb;              // ~180 degree reversal
    m /= ml;
    float den = dot(m, nb);
    if (den <= 1.0 / kMiterLimit) return nb;   // too sharp, or den gone negative
    return m / den;                            // 1/cos(theta/2)
}

void main() {
    vec2 pp = uScale * aPrev + uOffset;
    vec2 p0 = uScale * aP0   + uOffset;
    vec2 p1 = uScale * aP1   + uOffset;
    vec2 pn = uScale * aNext + uOffset;

    vec2  d  = p1 - p0;
    float dl = length(d);
    if (dl < 1e-6) {                        // zero-length segment: cull the quad
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    vec2 nb = vec2(-d.y, d.x) / dl;         // this segment's left normal

    vec2 o0 = nb;
    vec2 dp = p0 - pp;
    float dpl = length(dp);
    if (dpl > 1e-6) o0 = joint_offset(vec2(-dp.y, dp.x) / dpl, nb);

    vec2 o1 = nb;
    vec2 dn = pn - p1;
    float dnl = length(dn);
    if (dnl > 1e-6) o1 = joint_offset(nb, vec2(-dn.y, dn.x) / dnl);

    vec2 px = mix(p0, p1, aCorner.x)
            + mix(o0, o1, aCorner.x) * (aCorner.y * uHalfWidth);
    vec2 ndc = (px / uResolution) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);

    // Distance along the *centreline*, not along this corner's own edge — the
    // dash pattern must not shear across the stroke's width. `dl` is this
    // segment's length under the current transform, so only the accumulated
    // prefix needs the uDistScale correction.
    float d0 = aDist * uDistScale;
    vDist = d0 + dl * aCorner.x;
}
)";

// Line strokes, dashed.
//
// The pattern is a run-length list of alternating on/off lengths in *logical*
// pixels, from the shared table in src/line_dash.h -- the same one NanoVG's
// grid/legend dashing and the SVG writer's stroke-dasharray read. Padded to
// four entries with zeros, so a two-entry pattern leaves uDash.zw at 0 and
// the walk below collapses to the two-arm case. uDashPeriod <= 0 means solid,
// and then vDist is never read.
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

// Scatter: instanced quads clipped by a marker-shape SDF.
//
// aCenter is in *data* space offset by a per-plot anchor, and uScale/uOffset
// carry the data->logical-pixel transform, so the instance buffer is
// view-independent and survives pan/zoom. The marker half-extent is added
// *after* the transform because aSize is a pixel diameter -- markers must not
// grow when you zoom in. The CPU writes pixel-space centres instead (with
// uScale=(1,1), uOffset=(0,0)) when float cannot resolve the view; see
// pick_anchor().
static constexpr char k_scatter_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aQuadPos;   // unit quad [-1,1]x[-1,1]
layout(location = 1) in vec2 aCenter;    // instance: data-space center (anchored)
layout(location = 2) in float aSize;     // instance: pixel diameter
out vec2 vUV;
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
}
)";

static constexpr char k_scatter_frag[] = R"(
#version 410 core
in vec2 vUV;
uniform vec4 uColor;
uniform int  uMarker; // matches MarkerStyle enum: 0=None 1=Circle 2=Square 3=Triangle 4=Cross 5=Plus 6=Diamond
out vec4 FragColor;
void main() {
    bool inside = true;
    if      (uMarker == 0) inside = false;  // None — no marker drawn
    else if (uMarker == 1) inside = dot(vUV, vUV) <= 1.0;             // Circle
    else if (uMarker == 2) inside = true;                             // Square
    else if (uMarker == 3) inside = vUV.y >= abs(vUV.x) * 2.0 - 1.0;  // Triangle
    else if (uMarker == 4) {                                          // Cross
        float t = 0.3;
        inside = (abs(vUV.x - vUV.y) < t || abs(vUV.x + vUV.y) < t)
                 && dot(vUV, vUV) <= 1.0;
    }
    else if (uMarker == 5) {                                          // Plus
        float t = 0.3;
        inside = (abs(vUV.x) < t || abs(vUV.y) < t) && dot(vUV, vUV) <= 1.0;
    }
    else if (uMarker == 6) inside = abs(vUV.x) + abs(vUV.y) <= 1.0;   // Diamond
    if (!inside) discard;
    FragColor = uColor;
}
)";

// Continuous-color scatter: as k_scatter_vert/frag above, but the color is a
// per-instance attribute rather than a uniform. The marker SDF is duplicated
// rather than shared -- GLSL has no cross-shader include here.
static constexpr char k_scatterz_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aQuadPos;   // unit quad [-1,1]x[-1,1]
layout(location = 1) in vec2 aCenter;    // instance: data-space center (anchored)
layout(location = 2) in float aSize;     // instance: pixel diameter
layout(location = 3) in vec4 aColor;     // instance: per-point RGBA
out vec2 vUV;
out vec4 vColor;
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
}
)";

static constexpr char k_scatterz_frag[] = R"(
#version 410 core
in vec2 vUV;
in vec4 vColor;
uniform int  uMarker; // matches MarkerStyle enum: 0=None 1=Circle 2=Square 3=Triangle 4=Cross 5=Plus 6=Diamond
out vec4 FragColor;
void main() {
    bool inside = true;
    if      (uMarker == 0) inside = false;  // None — no marker drawn
    else if (uMarker == 1) inside = dot(vUV, vUV) <= 1.0;             // Circle
    else if (uMarker == 2) inside = true;                             // Square
    else if (uMarker == 3) inside = vUV.y >= abs(vUV.x) * 2.0 - 1.0;  // Triangle
    else if (uMarker == 4) {                                          // Cross
        float t = 0.3;
        inside = (abs(vUV.x - vUV.y) < t || abs(vUV.x + vUV.y) < t)
                 && dot(vUV, vUV) <= 1.0;
    }
    else if (uMarker == 5) {                                          // Plus
        float t = 0.3;
        inside = (abs(vUV.x) < t || abs(vUV.y) < t) && dot(vUV, vUV) <= 1.0;
    }
    else if (uMarker == 6) inside = abs(vUV.x) + abs(vUV.y) <= 1.0;   // Diamond
    if (!inside) discard;
    FragColor = vColor;
}
)";

// 3D scatter markers (v1.0 step 12): instanced quads again, and the third
// member of spec_3d.md §4's stroke fork -- a *symbol in the scene*.
//
// The billboard is the whole of it, and it is four lines: project the point,
// then add the quad's pixel offset in clip space **premultiplied by w**, which
// the perspective divide then cancels. So the marker comes out exactly aSize
// pixels across wherever it is, while keeping the point's own depth for the
// depth test -- which is what a marker needs and what no other geometry here
// wants (a bar edge is geometry and thins with distance; see k_bar3d_edge).
//
// Step 6 built this and step 7a removed it, when a plane's markers became
// texels of the plane's own raster rather than billboards in the scene. A
// scatter3d has no plane to be rasterized into, so the mechanism is back for
// the case it was always right for.
//
// The colour arrives per instance -- baked flat or through the colormap, since
// neither depends on the camera -- and `depthshade` is applied *here* rather
// than baked, because it does depend on the camera. That is what makes the cue
// free: a uniform changes, and an orbit re-uploads nothing.
static constexpr char k_scatter3d_vert[] = R"(
#version 410 core
layout(location = 0) in vec2 aQuadPos;   // unit quad [-1,1]x[-1,1]
layout(location = 1) in vec3 aCenter;    // instance: data space, less the plot's anchor
layout(location = 2) in float aSize;     // instance: pixel diameter
layout(location = 3) in vec4 aColor;     // instance: RGBA, already alpha'd
out vec2 vUV;
out vec4 vColor;
uniform vec3 uBoxScale;
uniform vec3 uBoxOffset;
uniform mat4 uClip;
uniform vec2 uResolution;   // logical pixels, the units uClip's frame is in
// xyz = the view direction, w = the depth of the box origin: together they are
// Px3::depth as an affine function (see depth_affine()).
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
    vColor = vec4(aColor.rgb * (1.0 - uShade.x * t), aColor.a);
}
)";

// The marker SDF once more. Three copies of it now (2D scatter, 2D scatter_z,
// this), which is what GLSL's lack of an include costs; marker_shape.h's
// comment on the CPU side names the same tests as the contract they all meet.
static constexpr char k_scatter3d_frag[] = R"(
#version 410 core
in vec2 vUV;
in vec4 vColor;
uniform int  uMarker; // matches MarkerStyle enum: 0=None 1=Circle 2=Square 3=Triangle 4=Cross 5=Plus 6=Diamond
out vec4 FragColor;
void main() {
    bool inside = true;
    if      (uMarker == 0) inside = false;  // None — no marker drawn
    else if (uMarker == 1) inside = dot(vUV, vUV) <= 1.0;             // Circle
    else if (uMarker == 2) inside = true;                             // Square
    else if (uMarker == 3) inside = vUV.y >= abs(vUV.x) * 2.0 - 1.0;  // Triangle
    else if (uMarker == 4) {                                          // Cross
        float t = 0.3;
        inside = (abs(vUV.x - vUV.y) < t || abs(vUV.x + vUV.y) < t)
                 && dot(vUV, vUV) <= 1.0;
    }
    else if (uMarker == 5) {                                          // Plus
        float t = 0.3;
        inside = (abs(vUV.x) < t || abs(vUV.y) < t) && dot(vUV, vUV) <= 1.0;
    }
    else if (uMarker == 6) inside = abs(vUV.x) + abs(vUV.y) <= 1.0;   // Diamond
    if (!inside) discard;
    FragColor = vColor;
}
)";

// The peeled marker. The shape test comes *before* peel_or_discard() so a
// fragment outside the marker never takes part in the peel at all -- a
// discarded fragment writes no depth, but one that peels first would have
// consumed a layer for a corner of the quad nobody can see.
static constexpr char k_peel_scatter3d_frag[] = R"(
#version 410 core
in vec2 vUV;
in vec4 vColor;
uniform int  uMarker;
out vec4 FragColor;
PEEL
void main() {
    bool inside = true;
    if      (uMarker == 0) inside = false;
    else if (uMarker == 1) inside = dot(vUV, vUV) <= 1.0;
    else if (uMarker == 2) inside = true;
    else if (uMarker == 3) inside = vUV.y >= abs(vUV.x) * 2.0 - 1.0;
    else if (uMarker == 4) {
        float t = 0.3;
        inside = (abs(vUV.x - vUV.y) < t || abs(vUV.x + vUV.y) < t)
                 && dot(vUV, vUV) <= 1.0;
    }
    else if (uMarker == 5) {
        float t = 0.3;
        inside = (abs(vUV.x) < t || abs(vUV.y) < t) && dot(vUV, vUV) <= 1.0;
    }
    else if (uMarker == 6) inside = abs(vUV.x) + abs(vUV.y) <= 1.0;
    if (!inside) discard;
    peel_or_discard(gl_FragCoord.z);
    FragColor = vec4(vColor.rgb * vColor.a, vColor.a);
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

// A heatmap on a plane in the 3D scene: the same textured quad as above, with
// a 3D MVP instead of a 2D one -- which is the whole of what "the 2D kinds on
// a plane" costs for this kind. The texture and its cache are untouched,
// because the cache key deliberately excludes the transform: the extent moves
// the quad, not the texture.
//
// The perspective divide is GL's own, so the texture is interpolated
// perspective-correctly with nothing to write here. That is exactly what the
// SVG path cannot do (spec_3d.md §10) and why it needs its own two forms.
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

// The plane's raster, composited into the scene. The texture is
// **premultiplied** -- it was accumulated over transparent black, see
// begin_pass() -- so the whole-plane alpha scales the colour and the coverage
// together, and the draw blends with GL_ONE rather than GL_SRC_ALPHA. Scaling
// only `a` here, as a straight-alpha texture would want, would leave the
// colour at full strength and make a translucent plane too bright.
static constexpr char k_plane3d_frag[] = R"(
#version 410 core
in vec2 vTC;
uniform sampler2D uTex;
uniform float uAlpha;
out vec4 FragColor;
void main() {
    vec4 c = texture(uTex, vTC);
    // **A texel with no ink is not part of the plane.** The raster spans the
    // whole box face, so most planes carry a transparent margin -- and an
    // opaque plane draws in the depth-writing pass, where a margin that did
    // not discard would write depth and occlude everything behind it while
    // painting nothing. Before step 7a the quad was the heatmap's own extent
    // and there was no margin to get this wrong.
    if (c.a <= 0.0) discard;
    FragColor = c * uAlpha;
}
)";

// ---------------------------------------------------------------------------
// The translucent-plane composite (step 7b)
// ---------------------------------------------------------------------------
// K planes, sorted at the fragment instead of as whole objects. The geometry
// is the plot rect, drawn once per depth slot; the shader casts the pixel's
// ray at every plane and emits the slot-th hit counting from the far end.
//
// The ray arrives as vertex attributes rather than being reconstructed from an
// inverse matrix, and it is `Projector3D::ray_from_pixel()` that produces it --
// the same inverse the hover hint casts with, so "what the composite thinks is
// under this pixel" and "what the tooltip names" are one function rather than
// two agreeing implementations.
//
// Interpolating a ray across a screen-space quad is exact in both projections,
// and that is not a coincidence to be checked per camera. Under orthographic
// the origin is affine in the pixel and the direction is constant; under
// perspective the origin is the eye and the *direction* is affine in the pixel
// (see ray_from_pixel: both branches are `basis + right*dx + up*dy`). The quad
// is drawn at w = 1, so GL's perspective-correct interpolation is plain linear
// interpolation here, which is precisely what an affine function needs.
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

// Why one draw per slot rather than one draw that blends all K internally: the
// samples still have to be depth-tested individually against the opaque scene,
// and a fragment has exactly one `gl_FragDepth`. Emitting one sample per draw
// gives each of them its own depth, so the fixed-function test does the
// occlusion -- and the order the draws arrive in *is* the sorted order at every
// fragment, so the fixed-function blend does the compositing. Nothing has to
// read the depth buffer back.
//
// The rejection is sound and not an approximation: if the slot-th sample
// passes the depth test then every sample in front of it does too, because a
// nearer sample cannot be behind an opaque surface a farther one is in front
// of. So no sample is ever dropped whose contribution a later slot needed.
//
// Each plane's raster is premultiplied (see begin_pass), so `c * uAlpha` scales
// colour and coverage together and the draw blends with GL_ONE.
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

        // The depth the buffer holds, from the same matrix the quad path
        // projects with -- so a sample's depth means what a rasterized
        // vertex's would, and the test against the bars is the same test.
        vec4 cl = uClip * vec4(h, 1.0);
        if (cl.w <= 0.0) continue;               // behind a perspective eye
        float z = (cl.z / cl.w) * 0.5 + 0.5;
        if (z < 0.0 || z > 1.0) continue;        // what GL's clip would drop

        // A texel with no ink is not part of the plane -- the raster spans the
        // whole box face, so most planes carry a transparent margin. Dropping
        // it here also keeps it out of the sort, which is what stops an empty
        // margin from occupying a slot a farther plane's ink needed.
        vec4 c = texture(uTex[i], vec2(su, sv));
        if (c.a <= 0.0) continue;

        col[n] = c * uAlpha[i];
        dep[n] = z;
        ++n;
    }

    if (uSlot >= n) discard;

    // Farthest first. Insertion sort: n is at most KMAX and usually two.
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

// 3D surface cells (step 7c). Same buffer discipline as the bar faces below --
// data-space corners offset by a per-plot anchor, with the whole camera in
// uniforms -- and one difference: the colour rides on the vertex rather than
// arriving as a uniform, because a colormapped surface has a colour per cell.
//
// It is *constant across a cell's six vertices*, so this is flat shading done
// with attributes GL already has rather than with a `flat` qualifier. That is
// deliberate and not an economy: the SVG path emits one fill per cell, and an
// interpolated colour here would have no counterpart there.
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

// 3D mesh faces (v1.0 step 14). The surface program above with the colour
// resolved *per fragment* instead of per vertex, which is the whole difference
// between a grid cell and a mesh face:
//
//   - a cell has one value and one colour, constant across its six vertices,
//     so k_surface_vert can carry the finished RGBA and the SVG can emit one
//     fill per cell;
//   - a face has three independent vertex values, because a mesh's fourth
//     dimension is measured at the vertices the caller gave. So what the
//     vertex carries is the *value*, and the map is sampled here.
//
// **Looking the colour up at the three corners and letting the rasterizer
// blend the results is a different picture**, and the same one k_line3d_vert
// rejects for a segment: a barycentric RGB blend of two distant colormap
// entries leaves the colormap -- viridis 0 to 1 blends through a brown that is
// nowhere on the colorbar beside it -- and where a segment's chord is one
// straight line off the map, a triangle's blend is a whole area of them.
// Interpolating the value and looking it up keeps every drawn pixel on the
// scale the reader is given.
//
// The *shade* stays flat per face: all three vertices carry the same one, so
// the interpolation is a no-op and this is flat shading done with the
// attributes GL already has, exactly as k_bar3d_vert does it.
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
    // vColor.a carries the mesh's resolved alpha either way, so only rgb comes
    // from the map -- k_line3d_frag's arrangement.
    vec3 rgb = uColormapped == 1
             ? texture(uCmap, vec2(clamp(vValue, 0.0, 1.0), 0.5)).rgb
             : vColor.rgb;
    FragColor = vec4(rgb * vShade, vColor.a);
}
)";

// 3D bar faces. The buffer is data-space corners offset by a per-plot anchor,
// so nothing about the camera or the limits is baked into it: uBoxScale /
// uBoxOffset are the data->box step and uClip is the whole camera. The shade
// is per vertex because it is per *face*, and a face's three vertices carry
// the same value -- flat shading without a flat-qualified varying.
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

// The world-space ribbon expansion, shared verbatim by the two programs that
// stroke geometry *in* the scene: bar outlines and the strokes on a plane.
// One copy of the billboard math with two programs around it, because the two
// differ only in where the width and the colour come from -- a uniform for a
// bar plot, per instance for a plane, whose lines, bar edges and whiskers all
// have their own. GLSL has no include, so the sharing is textual and the
// programs are built by concatenation.
static constexpr char k_ribbon_expand[] = R"(
// `a`/`b` are the segment in box space; the offset is perpendicular to it and
// to `ref`, so `ref` is the whole of what the two callers differ by:
//
//   bar3d edges pass the direction of the *eye*, which billboards the ribbon
//   to face the camera -- right for a box edge floating in the scene, which
//   has no surface to lie on.
//
//   a plane's strokes pass the plane's own *normal*, which keeps the ribbon
//   in the plane -- ink on paper. That is not a refinement: a billboarded
//   ribbon on a plane puts half its width in front of the surface and half
//   behind, which is exactly the z-fighting and the inside-vs-outside
//   thickness asymmetry a bar outline showed against its own bar.
vec3 ribbon_offset(vec3 a, vec3 b, vec3 ref, float half_w) {
    vec3 side = cross(b - a, ref);
    float sl = length(side);
    side = sl > 1e-9 ? side / sl : vec3(0.0);
    return side * half_w;
}
)";

// Bar outlines: the world-space half of the stroke fork (spec_3d.md §4).
//
// The 2D stroke shader transforms first and expands in pixels, which is what
// keeps the axis frame one width on screen however the camera moves. This one
// does the opposite -- it expands in *box* space, around a direction
// billboarded toward the eye, and only then projects -- so its width is a
// length in the scene and a distant bar's edges come out thinner. That is not
// a uniform's worth of difference from the other shader; it is the other
// order of operations, which is why it is a separate program.
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

// A path's ribbon (v1.0 step 13). The bar-edge program above with two things
// added, and both of them are what separates a *path* from a set of disjoint
// segments:
//
//   - **a miter join.** Bar edges are twelve unconnected segments per bar, so
//     nobody noticed that expanding each one about its own perpendicular
//     leaves a wedge-shaped gap on the outside of any corner. A path has
//     |x| - 2 interior corners and would show one at every bend. So each end
//     of a segment is offset along the *bisector* of its own perpendicular and
//     its neighbour's, lengthened by 1/cos(half the turn) so the ribbon's edge
//     stays straight through the corner -- the 2D stroke shader's miter, done
//     in box space instead of pixel space. The first and last points of an
//     open path have no neighbour, which is passed in as prev == a (or
//     next == b): ribbon_offset() of a zero-length segment is the zero vector,
//     so the bisector degenerates to the segment's own perpendicular and the
//     end is square, with no branch anywhere.
//   - **a ramp between its two ends.** A `colors` value is measured at a
//     point, so a segment ramps between the two it joins.
//
//     **What ramps is the value, not the colour**, and for a colormapped path
//     the two are different pictures. A straight line in RGB between two
//     distant colormap entries leaves the colormap: viridis 0 to viridis 1
//     blends dark purple to yellow through *brown*, and brown appears nowhere
//     on the colorbar drawn beside it -- so half of every long segment would
//     be coloured with something the scale does not define. Interpolating the
//     value and looking the colour up keeps every drawn pixel on the scale the
//     reader is given. At a point the two agree exactly, which is why
//     line3d_point_color() is still the one definition of a point's colour;
//     they differ only along a segment, and only for a sparse path.
//
//     A flat series has no value to ramp, so it keeps the colour pair, whose
//     two entries are then equal and whose mix is a no-op.
//
// The miter limit matters at a hairpin, where the bisector goes to infinity.
// Clamped to 4x the half width, which is SVG's own default `stroke-miterlimit`
// -- so the raster path and a mitered polyline in the vector path round the
// same corner the same way.
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
// The colour a flat series draws, the *value* a colormapped one looks up, and
// the depth ramp -- all three as varyings, because all three must be resolved
// per fragment. Interpolating a value and looking it up in the fragment is not
// the same picture as looking up at the two ends and interpolating the
// results: the second is the RGB chord again, just reached by a longer route,
// since a ribbon quad has vertices only at its two ends.
out vec4  vColor;
out float vValue;
out float vShadeT;
uniform vec3  uBoxScale;
uniform vec3  uBoxOffset;
uniform mat4  uClip;
uniform vec3  uEye;         // box space: a position, or a direction under ortho
uniform int   uPersp;
uniform float uHalfWidth;   // box units -- a length in the scene, not in pixels
// xyz = the view direction, w = the depth of the box origin: together they are
// Px3::depth as an affine function (see depth_affine()).
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

    // This segment's own perpendicular, and the neighbour's at the end being
    // expanded. Unit, or zero where the neighbouring segment has no length --
    // which is exactly the open-path end, and is why that needs no branch.
    vec3 here  = ribbon_offset(a, b, to_eye, 1.0);
    vec3 other = aCorner.x < 0.5 ? ribbon_offset(prv, a, to_eye, 1.0)
                                 : ribbon_offset(b, nxt, to_eye, 1.0);
    vec3 miter = here + other;
    float ml = length(miter);
    miter = ml > 1e-9 ? miter / ml : here;
    // 1/cos(half the turn), which is the length that keeps the outer edge
    // straight through the corner. At a hairpin the dot goes to zero, so the
    // clamp is the miter limit rather than a guard against dividing by zero.
    float scale = min(1.0 / max(dot(miter, here), 1e-4), 4.0);

    vec3 pos = p + miter * (aCorner.y * uHalfWidth * scale);
    gl_Position = uClip * vec4(pos, 1.0);

    vShadeT = clamp((dot(pos, uDepth.xyz) + uDepth.w - uShade.y) * uShade.z, 0.0, 1.0);
    vColor  = mix(aColorA, aColorB, aCorner.x);
    vValue  = mix(aValue.x, aValue.y, aCorner.x);
}
)";

// A path's fragment. Its own rather than the surface's, because the colormap
// lookup and the depth shade both have to happen *here*: see the varyings in
// k_line3d_vert for why looking up at the two ends and interpolating is a
// different picture.
static constexpr char k_line3d_frag[] = R"(
#version 410 core
in vec4  vColor;
in float vValue;
in float vShadeT;
// The colormap as a 256x1 lookup, and whether this path has values to look up.
uniform sampler2D uCmap;
uniform int  uColormapped;
// x = depthshade; y and z are the vertex shader's, unused here.
uniform vec3 uShade;
out vec4 FragColor;
void main() {
    vec4 c = vColor;
    // `vColor.a` carries the series' resolved alpha either way, so only rgb
    // comes from the map.
    if (uColormapped == 1)
        c = vec4(texture(uCmap, vec2(clamp(vValue, 0.0, 1.0), 0.5)).rgb, c.a);
    FragColor = vec4(c.rgb * (1.0 - uShade.x * vShadeT), c.a);
}
)";

// The same, peeled: the layer test, and the premultiply every peeled fragment
// does (see k_peel_surface_frag).
static constexpr char k_peel_line3d_frag[] = R"(
#version 410 core
in vec4  vColor;
in float vValue;
in float vShadeT;
uniform sampler2D uCmap;
uniform int  uColormapped;
uniform vec3 uShade;
out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    vec4 c = vColor;
    if (uColormapped == 1)
        c = vec4(texture(uCmap, vec2(clamp(vValue, 0.0, 1.0), 0.5)).rgb, c.a);
    vec3 rgb = c.rgb * (1.0 - uShade.x * vShadeT);
    FragColor = vec4(rgb * c.a, c.a);
}
)";

// Error bars in a scene (v1.0 step 17). The geometry arrives finished, in box
// space -- ribbons already expanded toward the eye and pixel lengths already
// converted, by errorbar3d_pieces() on the CPU (see error_bar3d.h for why) --
// so all this does is project it and apply the depth shade, per vertex from
// the same affine depth the path shader uses.
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
out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    vec3 rgb = vColor.rgb * (1.0 - uShade.x * vShadeT);
    FragColor = vec4(rgb * vColor.a, vColor.a);
}
)";

// ---------------------------------------------------------------------------
// Depth peeling (step 8)
// ---------------------------------------------------------------------------
// The test every peeled fragment runs, pasted into each of the four in-scene
// fragment shaders by the PEEL token. Two rejections, and they answer
// different questions:
//
//   - `z <= prev` drops everything the earlier passes already peeled, which is
//     what makes pass n deliver the n-th nearest sample rather than the
//     nearest one again. `<=` and not `<`: at pass n-1 the winner was written
//     at exactly this depth, so anything equal to it has already been emitted.
//     Two *different* primitives that are exactly coplanar therefore share one
//     layer and the farther is lost -- depth peeling's one structural
//     limitation, and the reason a plane's contents still belong in the raster
//     of step 7a rather than being left for this to resolve.
//   - `z >= opaque` drops everything the opaque phase already covered. Opaque
//     geometry is not re-drawn here and its depth is not in this framebuffer,
//     so the ordinary depth test cannot see it; the copy taken before the loop
//     is what stands in for it.
//
// texelFetch and not texture(): the peel targets are the plot rect at exactly
// framebuffer resolution and the viewport is shifted so the rect lands at
// their origin, so the fragment's own integer coordinate *is* the texel, and a
// filtered lookup could only blur a depth comparison.
//
// gl_FragCoord.z is the window depth after polygon offset -- offset is a
// rasterization stage and rasterization precedes fragment shading -- so a
// wireframe drawn with glPolygonOffset is tested at the same depth it writes,
// and peels once rather than in every pass.
static constexpr char k_peel_test[] = R"(
uniform sampler2D uPrevDepth;
uniform sampler2D uOpaqueDepth;
void peel_or_discard(float z) {
    ivec2 pc = ivec2(gl_FragCoord.xy);
    if (z <= texelFetch(uPrevDepth,   pc, 0).r) discard;
    if (z >= texelFetch(uOpaqueDepth, pc, 0).r) discard;
}
)";

// The four peel fragment shaders. Each is its non-peeling twin plus the test
// and a premultiply: the layer is composited into the accumulation with the
// `under` operator, which needs `src.rgb * src.a * dst.a`, and fixed-function
// blending has no factor that multiplies the source by its own alpha *and* by
// the destination's. So the shader does one of the two multiplications.
static constexpr char k_peel_bar3d_frag[] = R"(
#version 410 core
in float vShade;
uniform vec4 uColor;
out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    FragColor = vec4(uColor.rgb * vShade * uColor.a, uColor.a);
}
)";

static constexpr char k_peel_surface_frag[] = R"(
#version 410 core
in vec4 vColor;
out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    FragColor = vec4(vColor.rgb * vColor.a, vColor.a);
}
)";

// The same, peeled: the layer test, and the premultiply every peeled fragment
// does. The colormap lookup stays in the fragment shader, which is the point.
static constexpr char k_peel_surface_tri_frag[] = R"(
#version 410 core
in vec4  vColor;
in float vValue;
in float vShade;
uniform sampler2D uCmap;
uniform int  uColormapped;
out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    vec3 rgb = uColormapped == 1
             ? texture(uCmap, vec2(clamp(vValue, 0.0, 1.0), 0.5)).rgb
             : vColor.rgb;
    rgb *= vShade;
    FragColor = vec4(rgb * vColor.a, vColor.a);
}
)";

// The plane quad is the one that needs no premultiply: its raster was
// accumulated over transparent black and so already holds one (see
// begin_pass), which is the same property k_plane3d_frag relies on to blend
// with GL_ONE.
static constexpr char k_peel_plane3d_frag[] = R"(
#version 410 core
in vec2 vTC;
uniform sampler2D uTex;
uniform float uAlpha;
out vec4 FragColor;
PEEL
void main() {
    vec4 c = texture(uTex, vTC);
    if (c.a <= 0.0) discard;
    peel_or_discard(gl_FragCoord.z);
    FragColor = c * uAlpha;
}
)";

// Bar and surface wireframes, i.e. k_flat_frag under peeling. A separate
// shader rather than a variant of that one because k_flat_frag is shared with
// every 2D fill and stroke in the library.
static constexpr char k_peel_flat_frag[] = R"(
#version 410 core
uniform vec4 uColor;
out vec4 FragColor;
PEEL
void main() {
    peel_or_discard(gl_FragCoord.z);
    FragColor = vec4(uColor.rgb * uColor.a, uColor.a);
}
)";

// The peel's composite quad, in NDC with its own texture coordinates. Used
// twice per pass -- layer under accumulation, then accumulation over the
// scene -- and both times the blend function does the arithmetic; the only
// thing the shader decides is what the alpha channel it is handing over means.
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
    // The accumulation's alpha channel is the light still getting through, so
    // that the `under` blend can multiply by it directly; the composite that
    // puts the result over the scene wants the light stopped instead, and one
    // subtraction here is what lets that be an ordinary premultiplied `over`.
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
    unsigned int v = compile_shader(GL_VERTEX_SHADER,   vert_src);
    unsigned int f = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    unsigned int p = link_program(v, f);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// GLSL has no include, so a shared helper is pasted in by name: the token
// RIBBON on a line of its own becomes k_ribbon_expand. Textual, and
// deliberately so -- the alternative is a second copy of the billboard maths,
// which is exactly the kind of duplication that drifts.
static std::string expand_shader(const char* src, const char* token,
                                 const char* body) {
    std::string s(src);
    const std::size_t at = s.find(token);
    if (at != std::string::npos) s.replace(at, std::strlen(token), body);
    return s;
}

// Clips drawing to this axes' plot rect. pr/win_h are in logical pixels;
// glScissor wants real framebuffer pixels, hence the pixel_ratio_ scale.
// Rounded outward (floor the origin, ceil the far edge) so a fractional rect
// never clips a pixel the plot legitimately covers.
void DataRenderer::begin_pass(const PlotRect& pr, float win_h) const {
    if (peel_.active) {
        // A peel target *is* the plot rect, and the viewport peel_translucent3d()
        // set has already been shifted so the rect lands at the target's
        // origin -- so the clip is the whole target and pr does not come into
        // it. Anything the shift pushes outside is geometry this axes was
        // never going to show.
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, peel_.w, peel_.h);

        // No blending at all. A peel pass keeps exactly one fragment per pixel
        // -- the nearest that survives the two rejections -- and blending it
        // with whatever an earlier primitive happened to leave there would be
        // compositing in draw order, which is precisely the thing this step
        // exists to stop doing. The compositing happens once per layer,
        // afterwards, in an order the depth test rather than the caller chose.
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);   // each per-kind draw enables it itself
        glDisable(GL_CULL_FACE);
        return;
    }

    const float s  = pixel_ratio_;
    const float x0 = pr.x * s;
    const float y0 = (win_h - pr.y - pr.h) * s;   // GL origin is bottom-left
    const float x1 = (pr.x + pr.w) * s;
    const float y1 = (win_h - pr.y) * s;
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<int>(std::floor(x0)),
              static_cast<int>(std::floor(y0)),
              static_cast<int>(std::ceil(x1) - std::floor(x0)),
              static_cast<int>(std::ceil(y1) - std::floor(y0)));

    // Blending must be set here, not inherited. NanoVG (passes 1 and 3)
    // leaves premultiplied-alpha blending behind, while the data shaders emit
    // straight alpha -- under its leftover state every translucent primitive
    // composites too bright.
    //
    // The colour channels take the ordinary straight-alpha blend; the alpha
    // channel must not. GL_SRC_ALPHA applies to alpha as well, which lands a
    // primitive of opacity a at a^2 + da(1-a) rather than accumulating
    // coverage -- so the destination comes out translucent exactly where the
    // picture is solid. Both destinations this pass draws into need GL_ONE
    // there, for what look like two reasons and are one:
    //
    //  - a plane's own raster (step 7a) starts as transparent black, so the
    //    first primitive lands at a^2 and every partly covered texel
    //    composites far too faintly into the scene;
    //  - a 2D cell's framebuffer starts opaque, so a marker at the default
    //    ScatterOptions::alpha of 0.8 left dst.a at 0.84 with its colour
    //    channels already correct. On screen that is invisible -- ImGui
    //    composites the plot over the Light theme's 0.94 grey, within a third
    //    of a byte of the plot's own 0.93 clear -- and the SVG has no alpha
    //    channel to get wrong, so it showed only in savefig()'s PNG, whose
    //    apparent colour then depended on the viewer's own backdrop.
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE,       GL_ONE_MINUS_SRC_ALPHA);

    // The data pass is a painter's-algorithm draw in a fixed back-to-front
    // order (see render_frame.cpp) with no depth buffer semantics of its own,
    // and its quads are emitted in whatever winding the transform produces.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

void DataRenderer::end_pass() const {
    glDisable(GL_SCISSOR_TEST);
}

// Exact equality is the right test here, not a tolerance: the transform is
// recomputed by the same deterministic code from the same inputs every
// frame, so it is bit-identical whenever the view genuinely has not moved,
// and any real pan/zoom/resize changes it by far more than an ULP.
namespace {

// The data->logical-pixel map as the scatter shaders' two uniforms, folded
// with a per-plot anchor:
//
//   to_px(x) = pr.x + (x - xmin) * sx,  with x = anchor + r
//            = [pr.x + (anchor - xmin) * sx] + r * sx
//
// The bracketed term is computed here in double and only then narrowed, so
// the large cancellation happens at full precision rather than in the shader.
struct DataToPixel { float scale[2]; float offset[2]; };

DataToPixel data_to_pixel(const CoordTransform& tr, double ax, double ay) {
    const double sx = static_cast<double>(tr.pw) / (tr.xmax - tr.xmin);
    const double sy = static_cast<double>(tr.ph) / (tr.ymax - tr.ymin);
    DataToPixel m;
    m.scale[0]  = static_cast<float>(sx);
    m.scale[1]  = static_cast<float>(-sy);          // data y up, pixel y down
    m.offset[0] = static_cast<float>(tr.px + (ax - tr.xmin) * sx);
    m.offset[1] = static_cast<float>(tr.py + tr.ph - (ay - tr.ymin) * sy);
    return m;
}

// Can a float residual resolve this view to better than a tenth of a pixel?
// Residuals are at most half the data's own span, so their quantum is
// span/2 * 2^-24; times the pixels-per-unit scale gives the positional error.
// Fails only at extreme zoom -- a span of 200 in an 800px plot, at roughly
// 1e4x -- where the caller falls back to pixel-space centres in double.
bool float_resolves_view(double span, double range, double pixels) {
    if (span <= 0.0 || range <= 0.0) return true;   // degenerate: nothing to lose
    constexpr double kFloatEps = 5.96e-8;           // 2^-24
    const double quantum_px = (span * 0.5) * kFloatEps * (pixels / range);
    return quantum_px < 0.1;
}

} // namespace

static bool same_view(const CoordTransform& a, const CoordTransform& b) {
    return a.xmin == b.xmin && a.xmax == b.xmax
        && a.ymin == b.ymin && a.ymax == b.ymax
        && a.px == b.px && a.py == b.py && a.pw == b.pw && a.ph == b.ph
        && a.win_w == b.win_w && a.win_h == b.win_h;
}


namespace {

// Axis-aligned rectangle outline as a closed ring of 4 quads, straddling the
// rect's edges by half the stroke width. Used for bar edges, where exact
// square corners matter more than the generic miter path. x0/y0/x1/y1 need
// not be sorted. The inset is measured in pixels, so this is the one CPU-side
// geometry expansion left in the data pass (see k_flat_vert).
void build_rect_outline(float x0, float y0, float x1, float y1, float width,
                        std::vector<float>& out)
{
    if (width <= 0.0f) return;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    // Keep the inner edge from crossing itself on a bar thinner than the stroke.
    const float hw = std::min(width * 0.5f,
                              std::min(x1 - x0, y1 - y0) * 0.5f);
    if (hw <= 0.0f) return;

    const float ox0 = x0 - hw, oy0 = y0 - hw, ox1 = x1 + hw, oy1 = y1 + hw;
    const float ix0 = x0 + hw, iy0 = y0 + hw, ix1 = x1 - hw, iy1 = y1 - hw;

    // Corner order must match between the outer and inner rings so each pair
    // of consecutive corners spans one side of the frame.
    const float outer[8] = { ox0, oy0,  ox1, oy0,  ox1, oy1,  ox0, oy1 };
    const float inner[8] = { ix0, iy0,  ix1, iy0,  ix1, iy1,  ix0, iy1 };
    out.reserve(out.size() + 48);
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        const float ax = outer[i * 2], ay = outer[i * 2 + 1];
        const float bx = outer[j * 2], by = outer[j * 2 + 1];
        const float cx = inner[i * 2], cy = inner[i * 2 + 1];
        const float dx = inner[j * 2], dy = inner[j * 2 + 1];
        out.insert(out.end(), { ax, ay,  bx, by,  cx, cy,
                                cx, cy,  bx, by,  dx, dy });
    }
}

// One axis-aligned pixel-space rectangle as two triangles. Error
// bars are nothing but these — a stem and its two caps — so they need no
// join handling and none of build_polyline()'s machinery.
void push_px_rect(float x0, float y0, float x1, float y1,
                  std::vector<float>& out)
{
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    out.insert(out.end(), {
        x0, y0,  x1, y0,  x1, y1,
        x0, y0,  x1, y1,  x0, y1,
    });
}

// A butt-ended segment `2 * hw` wide as two triangles. Axis-aligned, it is
// exactly push_px_rect() of the same extent, so a flat whisker draws what it
// did before the chevron needed an oblique segment.
void push_px_segment(float x0, float y0, float x1, float y1, float hw,
                     std::vector<float>& out)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f || hw <= 0.0f) return;
    const float nx = -dy / len * hw, ny = dx / len * hw;
    out.insert(out.end(), {
        x0 + nx, y0 + ny,  x1 + nx, y1 + ny,  x1 - nx, y1 - ny,
        x0 + nx, y0 + ny,  x1 - nx, y1 - ny,  x0 - nx, y0 - ny,
    });
}

// One whisker in pixels, as whisker_segments() defines it. `lo`/`hi` are the
// ends along the whisker, each equal to the point's own coordinate when that
// side is absent -- which draws nothing there, not a stack of caps reading as
// a tiny range.
void build_whisker(float cx, float cy, float lo, float hi,
                   const ErrorBarOptions& style, bool vertical,
                   std::vector<float>& out)
{
    const float hw = std::max(style.linewidth, 0.0f) * 0.5f;
    if (hw <= 0.0f) return;
    whisker_segments(cx, cy, lo, hi, vertical, 1.0, 1.0, style.capsize, style.capstyle,
                     [&](double x0, double y0, double x1, double y1) {
                         push_px_segment(static_cast<float>(x0), static_cast<float>(y0),
                                         static_cast<float>(x1), static_cast<float>(y1),
                                         hw, out);
                     });
}

// The variance box's outline: four bars, one per side, inset the way
// build_rect_outline() does but without its corner-ring bookkeeping — the box
// is small and axis-aligned, so overlapping corners are invisible.
void build_box_outline(float x0, float y0, float x1, float y1,
                       float linewidth, std::vector<float>& out)
{
    const float hw = std::max(linewidth, 0.0f) * 0.5f;
    if (hw <= 0.0f) return;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    push_px_rect(x0 - hw, y0 - hw, x1 + hw, y0 + hw, out);   // top
    push_px_rect(x0 - hw, y1 - hw, x1 + hw, y1 + hw, out);   // bottom
    push_px_rect(x0 - hw, y0 - hw, x0 + hw, y1 + hw, out);   // left
    push_px_rect(x1 - hw, y0 - hw, x1 + hw, y1 + hw, out);   // right
}

// Every error-bar-carrying kind reduces to the same thing: a sequence of
// anchor points with an ErrorBarData beside them. `ys` is whatever the y bar
// hangs off -- a line's or scatter's y, or a bar's *height* (its tip), never
// the baseline. Two buffers out, because the box interior draws at a
// different opacity from the whiskers, caps and outlines.
void build_error_bars(const CowVec<double>& xs, const CowVec<double>& ys,
                      const ErrorBarData& err, const ErrorBarOptions& style,
                      const CoordTransform& tr,
                      std::vector<float>& fill, std::vector<float>& stroke)
{
    const std::size_t n = std::min(xs.size(), ys.size());
    const float halfbox = std::max(style.boxwidth, 0.0f) * 0.5f;
    const bool  filled  = style.box_alpha > 0.0f;

    // An absent side is the point's own coordinate, computed by the same
    // to_px/to_py call as the point, so the two compare equal exactly.
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

        // The box spans whichever directions have box data; a direction
        // without falls back to `boxwidth` pixels, which is what makes a
        // y-only box a fixed-width rectangle and one with both a true 2D one.
        if (!err.has_y_box() && !err.has_x_box()) continue;
        float bx0, bx1, by0, by1;
        if (err.has_x_box()) {
            const ErrOffsets o = err.x_box(i);
            bx0 = end_px(xs[i], -o.lo);
            bx1 = end_px(xs[i],  o.hi);
        } else {
            bx0 = cx - halfbox; bx1 = cx + halfbox;
        }
        if (err.has_y_box()) {
            const ErrOffsets o = err.y_box(i);
            by0 = end_py(ys[i], -o.lo);
            by1 = end_py(ys[i],  o.hi);
        } else {
            by0 = cy - halfbox; by1 = cy + halfbox;
        }
        if (bx0 == bx1 || by0 == by1) continue;   // zero extent: no box
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
    // Unit quad as a triangle strip: x picks the segment end, y the side.
    static const float corners[8] = { 0.f,-1.f,  0.f, 1.f,  1.f,-1.f,  1.f, 1.f };
    glBindBuffer(GL_ARRAY_BUFFER, lineseg_corner_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);
    // Attributes 1..4 (prev/p0/p1/next) are pointed at each cached line's own
    // buffer at draw time — four windows into one array of points, offset by
    // one point each.
    glBindVertexArray(0);

    // --- scatter ---
    scatter_program_ = build_program(k_scatter_vert, k_scatter_frag);
    glGenVertexArrays(1, &scatter_vao_);
    glGenBuffers(1, &scatter_quad_vbo_);
    glGenBuffers(1, &scatter_inst_vbo_);

    // Unit quad: 6 vertices (2 triangles), positions in [-1,1]x[-1,1].
    static const float quad[12] = {
        -1,-1,  1,-1,  1, 1,
        -1,-1,  1, 1, -1, 1,
    };
    glBindVertexArray(scatter_vao_);

    glBindBuffer(GL_ARRAY_BUFFER, scatter_quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);  // per-vertex

    // Instance buffer layout: (cx, cy, size) per instance = 3 floats
    glBindBuffer(GL_ARRAY_BUFFER, scatter_inst_vbo_);
    glEnableVertexAttribArray(1);  // aCenter
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);  // aSize
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(0);

    // --- scatter_z (continuous-color scatter) ---
    scatterz_program_ = build_program(k_scatterz_vert, k_scatterz_frag);
    glGenVertexArrays(1, &scatterz_vao_);
    glGenBuffers(1, &scatterz_quad_vbo_);
    glGenBuffers(1, &scatterz_inst_vbo_);

    glBindVertexArray(scatterz_vao_);

    glBindBuffer(GL_ARRAY_BUFFER, scatterz_quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);  // same unit quad
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);

    // Instance buffer layout: (cx, cy, size, r, g, b, a) = 7 floats
    glBindBuffer(GL_ARRAY_BUFFER, scatterz_inst_vbo_);
    glEnableVertexAttribArray(1);  // aCenter
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);  // aSize
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);  // aColor
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);

    // --- heatmap ---
    heatmap_program_ = build_program(k_heatmap_vert, k_heatmap_frag);
    glGenVertexArrays(1, &heatmap_vao_);
    glGenBuffers(1, &heatmap_vbo_);
    // Layout: (px, py, tx, ty) per vertex — uploaded fresh each draw.
    glBindVertexArray(heatmap_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, heatmap_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
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
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    // --- The translucent-plane composite (step 7b) ---
    plane_comp_program_ = build_program(k_plane_comp_vert, k_plane_comp_frag);
    glGenVertexArrays(1, &plane_comp_vao_);
    glGenBuffers(1, &plane_comp_vbo_);
    // Layout: (ndc_x, ndc_y, ox, oy, oz, dx, dy, dz) per vertex — six of
    // them, the plot rect with its corner rays, uploaded per group.
    glBindVertexArray(plane_comp_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, plane_comp_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          reinterpret_cast<void*>(5 * sizeof(float)));
    glBindVertexArray(0);

    // Sampler i reads texture unit i, once and for all: the bindings change
    // per group, the mapping never does.
    glUseProgram(plane_comp_program_);
    for (int i = 0; i < kMaxCompositePlanes; ++i) {
        char name[24];
        std::snprintf(name, sizeof(name), "uTex[%d]", i);
        glUniform1i(glGetUniformLocation(plane_comp_program_, name), i);
    }
    glUseProgram(0);

    // --- 3D scatter markers ---
    scatter3d_program_ = build_program(k_scatter3d_vert, k_scatter3d_frag);
    glGenVertexArrays(1, &scatter3d_vao_);
    glGenBuffers(1, &scatter3d_corner_vbo_);
    glBindVertexArray(scatter3d_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, scatter3d_corner_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);  // the same unit quad
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);   // per-vertex
    // Attributes 1..3 (centre, size, colour) are windows into each cached
    // cloud's own instance buffer, pointed at it at draw time -- the bar-edge
    // VAO's arrangement, since the buffer is per plot object.
    glBindVertexArray(0);

    // --- 3D bar faces ---
    bar3d_program_ = build_program(k_bar3d_vert, k_bar3d_frag);
    glGenVertexArrays(1, &bar3d_vao_);
    // Layout: (x, y, z, shade) per vertex; the buffer itself is per plot
    // object and bound at draw time, like the line cache's.
    glBindVertexArray(bar3d_vao_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // --- 3D surfaces ---
    surface_program_ = build_program(k_surface_vert, k_surface_frag);
    glGenVertexArrays(1, &surface_vao_);
    // Layout: (x, y, z, r, g, b, a) per vertex; the buffer is per plot object
    // and bound at draw time, like the bar cache's.
    glBindVertexArray(surface_vao_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // --- 3D meshes (step 14) ---
    surface_tri_program_ = build_program(k_surface_tri_vert, k_surface_tri_frag);
    glGenVertexArrays(1, &surface_tri_vao_);
    // Layout: (x, y, z, r, g, b, a, value, shade) per vertex; the buffer is per
    // plot object and bound at draw time, like the surface cache's.
    glBindVertexArray(surface_tri_vao_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);

    // --- 3D bar edges (world-space ribbons) ---
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
    // Attributes 1 and 2 (the segment's two ends) are two windows into each
    // cached plot's own buffer, pointed at it at draw time.
    glBindVertexArray(0);

    // --- 3D paths (mitered world-space ribbons, step 13) ---
    // The bar-edge program's structure with a varying colour, so it reuses
    // that kind's fragment shader rather than adding a fourth copy of
    // "output what came in".
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
    // Attributes 1..6 (prev/a/b/next and the two end colours) are windows into
    // each cached path's own buffer, pointed at it at draw time.
    glBindVertexArray(0);

    // --- 3D error bars (step 17) ---
    // Finished box-space triangles, one interleaved buffer per series: the
    // VAO's two attributes are pointed at whichever buffer is drawn.
    errbar3d_program_ = build_program(k_errbar3d_vert, k_errbar3d_frag);
    glGenVertexArrays(1, &errbar3d_vao_);

    // --- Depth peeling (step 8) ---
    //
    // The same four vertex shaders, so nothing about where geometry lands can
    // differ between a peeled frame and an unpeeled one; only the fragment
    // shaders are new. They share the peel test textually, the way the two
    // ribbon programs share the billboard maths.
    {
        const std::string peel_bar3d =
            expand_shader(k_peel_bar3d_frag, "PEEL", k_peel_test);
        const std::string peel_surface =
            expand_shader(k_peel_surface_frag, "PEEL", k_peel_test);
        const std::string peel_plane =
            expand_shader(k_peel_plane3d_frag, "PEEL", k_peel_test);
        const std::string peel_flat =
            expand_shader(k_peel_flat_frag, "PEEL", k_peel_test);
        const std::string peel_scatter3d =
            expand_shader(k_peel_scatter3d_frag, "PEEL", k_peel_test);

        peel_bar3d_program_   = build_program(k_bar3d_vert,   peel_bar3d.c_str());
        peel_surface_program_ = build_program(k_surface_vert, peel_surface.c_str());
        peel_surface_tri_program_ = build_program(
            k_surface_tri_vert,
            expand_shader(k_peel_surface_tri_frag, "PEEL", k_peel_test).c_str());
        peel_scatter3d_program_ = build_program(k_scatter3d_vert, peel_scatter3d.c_str());
        peel_plane3d_program_ = build_program(k_plane3d_vert, peel_plane.c_str());
        peel_edge_program_    = build_program(
            expand_shader(k_bar3d_edge_vert, "RIBBON", k_ribbon_expand).c_str(),
            peel_flat.c_str());
        peel_line3d_program_  = build_program(
            expand_shader(k_line3d_vert, "RIBBON", k_ribbon_expand).c_str(),
            expand_shader(k_peel_line3d_frag, "PEEL", k_peel_test).c_str());
        peel_errbar3d_program_ = build_program(
            k_errbar3d_vert,
            expand_shader(k_peel_errbar3d_frag, "PEEL", k_peel_test).c_str());

        peel_comp_program_ = build_program(k_peel_comp_vert, k_peel_comp_frag);
        glGenVertexArrays(1, &peel_comp_vao_);
        glGenBuffers(1, &peel_comp_vbo_);
        // Layout: (ndc_x, ndc_y, tx, ty) per vertex — six of them, rewritten
        // per composite because the two composites cover different rectangles.
        glBindVertexArray(peel_comp_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, peel_comp_vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void*>(2 * sizeof(float)));
        glBindVertexArray(0);

        // The sampler bindings never change: unit 0 is the plane's own raster
        // where there is one, units 1 and 2 are the two depth textures. Set
        // once here rather than per pass, exactly as the composite of step 7b
        // sets its own array of them.
        auto peel_samplers = [](unsigned int p) {
            glUseProgram(p);
            const int prev = glGetUniformLocation(p, "uPrevDepth");
            const int opq  = glGetUniformLocation(p, "uOpaqueDepth");
            if (prev >= 0) glUniform1i(prev, 1);
            if (opq  >= 0) glUniform1i(opq,  2);
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

    // Resolve every uniform location once, now, rather than per draw call.
    auto loc = [](unsigned int p, const char* n) { return glGetUniformLocation(p, n); };
    line_u_     = { loc(line_program_, "uResolution"),     loc(line_program_, "uColor"),
                    loc(line_program_, "uScale"),          loc(line_program_, "uOffset") };
    lineseg_u_  = { loc(lineseg_program_, "uResolution"),  loc(lineseg_program_, "uColor"),
                    loc(lineseg_program_, "uScale"),       loc(lineseg_program_, "uOffset"),
                    loc(lineseg_program_, "uHalfWidth"),   loc(lineseg_program_, "uDash"),
                    loc(lineseg_program_, "uDashPeriod"),  loc(lineseg_program_, "uDistScale") };
    scatter_u_  = { loc(scatter_program_, "uResolution"),  loc(scatter_program_, "uColor"),
                    loc(scatter_program_, "uMarker"),
                    loc(scatter_program_, "uScale"),       loc(scatter_program_, "uOffset") };
    scatterz_u_ = { loc(scatterz_program_, "uResolution"), -1,
                    loc(scatterz_program_, "uMarker"),
                    loc(scatterz_program_, "uScale"),      loc(scatterz_program_, "uOffset") };
    heatmap_u_  = { loc(heatmap_program_, "uResolution"),  loc(heatmap_program_, "uTex") };
    plane3d_u_  = { loc(plane3d_program_, "uClip"),       loc(plane3d_program_, "uTex"),
                    loc(plane3d_program_, "uAlpha"),      loc(plane3d_program_, "uBoxScale"),
                    loc(plane3d_program_, "uBoxOffset") };
    plane_comp_u_ = { loc(plane_comp_program_, "uClip"),    loc(plane_comp_program_, "uCount"),
                      loc(plane_comp_program_, "uSlot"),    loc(plane_comp_program_, "uOrigin[0]"),
                      loc(plane_comp_program_, "uDU[0]"),   loc(plane_comp_program_, "uDV[0]"),
                      loc(plane_comp_program_, "uAxis[0]"), loc(plane_comp_program_, "uAlpha[0]") };
    surface_u_  = { loc(surface_program_, "uClip"), loc(surface_program_, "uBoxScale"),
                    loc(surface_program_, "uBoxOffset") };
    scatter3d_u_ = { loc(scatter3d_program_, "uClip"),       loc(scatter3d_program_, "uBoxScale"),
                     loc(scatter3d_program_, "uBoxOffset"), loc(scatter3d_program_, "uResolution"),
                     loc(scatter3d_program_, "uMarker"),    loc(scatter3d_program_, "uDepth"),
                     loc(scatter3d_program_, "uShade") };
    bar3d_u_    = { loc(bar3d_program_, "uClip"),      loc(bar3d_program_, "uColor"),
                    loc(bar3d_program_, "uBoxScale"), loc(bar3d_program_, "uBoxOffset") };
    bar3d_edge_u_ = { loc(bar3d_edge_program_, "uClip"),      loc(bar3d_edge_program_, "uColor"),
                      loc(bar3d_edge_program_, "uBoxScale"), loc(bar3d_edge_program_, "uBoxOffset"),
                      loc(bar3d_edge_program_, "uEye"),      loc(bar3d_edge_program_, "uPersp"),
                      loc(bar3d_edge_program_, "uHalfWidth") };

    // The peel programs' own copies of the same names. A separate program has
    // separate locations even for an identical uniform, so this is four more
    // lookups and not four aliases.
    peel_plane3d_u_ = { loc(peel_plane3d_program_, "uClip"),
                        loc(peel_plane3d_program_, "uTex"),
                        loc(peel_plane3d_program_, "uAlpha"),
                        loc(peel_plane3d_program_, "uBoxScale"),
                        loc(peel_plane3d_program_, "uBoxOffset") };
    peel_surface_u_ = { loc(peel_surface_program_, "uClip"),
                        loc(peel_surface_program_, "uBoxScale"),
                        loc(peel_surface_program_, "uBoxOffset") };
    surface_tri_u_ = { loc(surface_tri_program_, "uClip"),
                       loc(surface_tri_program_, "uBoxScale"),
                       loc(surface_tri_program_, "uBoxOffset"),
                       loc(surface_tri_program_, "uCmap"),
                       loc(surface_tri_program_, "uColormapped") };
    peel_surface_tri_u_ = { loc(peel_surface_tri_program_, "uClip"),
                            loc(peel_surface_tri_program_, "uBoxScale"),
                            loc(peel_surface_tri_program_, "uBoxOffset"),
                            loc(peel_surface_tri_program_, "uCmap"),
                            loc(peel_surface_tri_program_, "uColormapped") };
    peel_scatter3d_u_ = { loc(peel_scatter3d_program_, "uClip"),
                          loc(peel_scatter3d_program_, "uBoxScale"),
                          loc(peel_scatter3d_program_, "uBoxOffset"),
                          loc(peel_scatter3d_program_, "uResolution"),
                          loc(peel_scatter3d_program_, "uMarker"),
                          loc(peel_scatter3d_program_, "uDepth"),
                          loc(peel_scatter3d_program_, "uShade") };
    peel_bar3d_u_   = { loc(peel_bar3d_program_, "uClip"),
                        loc(peel_bar3d_program_, "uColor"),
                        loc(peel_bar3d_program_, "uBoxScale"),
                        loc(peel_bar3d_program_, "uBoxOffset") };
    peel_edge_u_    = { loc(peel_edge_program_, "uClip"),      loc(peel_edge_program_, "uColor"),
                        loc(peel_edge_program_, "uBoxScale"), loc(peel_edge_program_, "uBoxOffset"),
                        loc(peel_edge_program_, "uEye"),      loc(peel_edge_program_, "uPersp"),
                        loc(peel_edge_program_, "uHalfWidth") };
    line3d_u_ = { loc(line3d_program_, "uClip"),      loc(line3d_program_, "uBoxScale"),
                  loc(line3d_program_, "uBoxOffset"), loc(line3d_program_, "uEye"),
                  loc(line3d_program_, "uPersp"),     loc(line3d_program_, "uHalfWidth"),
                  loc(line3d_program_, "uDepth"),     loc(line3d_program_, "uShade"),
                  loc(line3d_program_, "uCmap"),      loc(line3d_program_, "uColormapped") };
    peel_line3d_u_ = { loc(peel_line3d_program_, "uClip"),
                       loc(peel_line3d_program_, "uBoxScale"),
                       loc(peel_line3d_program_, "uBoxOffset"),
                       loc(peel_line3d_program_, "uEye"),
                       loc(peel_line3d_program_, "uPersp"),
                       loc(peel_line3d_program_, "uHalfWidth"),
                       loc(peel_line3d_program_, "uDepth"),
                       loc(peel_line3d_program_, "uShade"),
                       loc(peel_line3d_program_, "uCmap"),
                       loc(peel_line3d_program_, "uColormapped") };
    errbar3d_u_ = { loc(errbar3d_program_, "uClip"), loc(errbar3d_program_, "uDepth"),
                    loc(errbar3d_program_, "uShade") };
    peel_errbar3d_u_ = { loc(peel_errbar3d_program_, "uClip"),
                         loc(peel_errbar3d_program_, "uDepth"),
                         loc(peel_errbar3d_program_, "uShade") };
    peel_comp_u_    = { loc(peel_comp_program_, "uTex"),
                        loc(peel_comp_program_, "uFlipAlpha") };
}

DataRenderer::~DataRenderer() {
    if (line_vbo_)         glDeleteBuffers(1, &line_vbo_);
    if (line_vao_)         glDeleteVertexArrays(1, &line_vao_);
    if (line_program_)     glDeleteProgram(line_program_);

    if (lineseg_corner_vbo_) glDeleteBuffers(1, &lineseg_corner_vbo_);
    if (lineseg_vao_)        glDeleteVertexArrays(1, &lineseg_vao_);
    if (lineseg_program_)    glDeleteProgram(lineseg_program_);

    if (scatter_inst_vbo_) glDeleteBuffers(1, &scatter_inst_vbo_);
    if (scatter_quad_vbo_) glDeleteBuffers(1, &scatter_quad_vbo_);
    if (scatter_vao_)      glDeleteVertexArrays(1, &scatter_vao_);
    if (scatter_program_)  glDeleteProgram(scatter_program_);

    if (scatterz_inst_vbo_) glDeleteBuffers(1, &scatterz_inst_vbo_);
    if (scatterz_quad_vbo_) glDeleteBuffers(1, &scatterz_quad_vbo_);
    if (scatterz_vao_)      glDeleteVertexArrays(1, &scatterz_vao_);
    if (scatterz_program_)  glDeleteProgram(scatterz_program_);

    if (heatmap_vbo_)      glDeleteBuffers(1, &heatmap_vbo_);
    if (heatmap_vao_)      glDeleteVertexArrays(1, &heatmap_vao_);
    if (heatmap_program_)  glDeleteProgram(heatmap_program_);

    if (plane_comp_vbo_)     glDeleteBuffers(1, &plane_comp_vbo_);
    if (plane_comp_vao_)     glDeleteVertexArrays(1, &plane_comp_vao_);
    if (plane_comp_program_) glDeleteProgram(plane_comp_program_);

    if (plane3d_vbo_)      glDeleteBuffers(1, &plane3d_vbo_);
    if (plane3d_vao_)      glDeleteVertexArrays(1, &plane3d_vao_);
    if (plane3d_program_)  glDeleteProgram(plane3d_program_);

    for (auto& [k, e] : plane_raster_cache_) {
        if (e.fbo) glDeleteFramebuffers(1, &e.fbo);
        if (e.tex) glDeleteTextures(1, &e.tex);
    }

    if (surface_vao_)     glDeleteVertexArrays(1, &surface_vao_);
    if (surface_program_) glDeleteProgram(surface_program_);
    if (surface_tri_vao_)      glDeleteVertexArrays(1, &surface_tri_vao_);
    if (surface_tri_program_)  glDeleteProgram(surface_tri_program_);
    if (peel_surface_tri_program_) glDeleteProgram(peel_surface_tri_program_);
    if (surface_tri_cmap_tex_) glDeleteTextures(1, &surface_tri_cmap_tex_);
    for (auto& [k, e] : surface_tri_cache_) {
        if (e.vbo)       glDeleteBuffers(1, &e.vbo);
        if (e.edge_vbo)  glDeleteBuffers(1, &e.edge_vbo);
        if (e.index_ebo) glDeleteBuffers(1, &e.index_ebo);
    }
    for (auto& [k, e] : surface_cache_) {
        if (e.vbo)       glDeleteBuffers(1, &e.vbo);
        if (e.edge_vbo)  glDeleteBuffers(1, &e.edge_vbo);
        if (e.index_ebo) glDeleteBuffers(1, &e.index_ebo);
    }

    for (auto& [k, e] : scatter3d_cache_) {
        if (e.vbo)       glDeleteBuffers(1, &e.vbo);
        if (e.order_vbo) glDeleteBuffers(1, &e.order_vbo);
    }
    if (scatter3d_vao_)        glDeleteVertexArrays(1, &scatter3d_vao_);
    if (scatter3d_corner_vbo_) glDeleteBuffers(1, &scatter3d_corner_vbo_);
    if (scatter3d_program_)    glDeleteProgram(scatter3d_program_);

    for (auto& [k, e] : line3d_cache_) {
        if (e.vbo)       glDeleteBuffers(1, &e.vbo);
        if (e.order_vbo) glDeleteBuffers(1, &e.order_vbo);
    }
    if (line3d_vao_)        glDeleteVertexArrays(1, &line3d_vao_);
    if (line3d_corner_vbo_) glDeleteBuffers(1, &line3d_corner_vbo_);
    if (line3d_cmap_tex_)   glDeleteTextures(1, &line3d_cmap_tex_);
    if (line3d_program_)    glDeleteProgram(line3d_program_);
    if (peel_line3d_program_) glDeleteProgram(peel_line3d_program_);
    for (auto& [k, e] : errbar3d_cache_) {
        if (e.opaque_vbo) glDeleteBuffers(1, &e.opaque_vbo);
        if (e.trans_vbo)  glDeleteBuffers(1, &e.trans_vbo);
    }
    if (errbar3d_vao_)          glDeleteVertexArrays(1, &errbar3d_vao_);
    if (errbar3d_program_)      glDeleteProgram(errbar3d_program_);
    if (peel_errbar3d_program_) glDeleteProgram(peel_errbar3d_program_);

    if (peel_scatter3d_program_) glDeleteProgram(peel_scatter3d_program_);
    if (peel_bar3d_program_)   glDeleteProgram(peel_bar3d_program_);
    if (peel_surface_program_) glDeleteProgram(peel_surface_program_);
    if (peel_plane3d_program_) glDeleteProgram(peel_plane3d_program_);
    if (peel_edge_program_)    glDeleteProgram(peel_edge_program_);
    if (peel_comp_vbo_)        glDeleteBuffers(1, &peel_comp_vbo_);
    if (peel_comp_vao_)        glDeleteVertexArrays(1, &peel_comp_vao_);
    if (peel_comp_program_)    glDeleteProgram(peel_comp_program_);
    if (peel_.fbo)       glDeleteFramebuffers(1, &peel_.fbo);
    if (peel_.accum_fbo) glDeleteFramebuffers(1, &peel_.accum_fbo);
    if (peel_.copy_fbo)  glDeleteFramebuffers(1, &peel_.copy_fbo);
    if (peel_.layer_tex) glDeleteTextures(1, &peel_.layer_tex);
    if (peel_.accum_tex) glDeleteTextures(1, &peel_.accum_tex);
    if (peel_.opaque_tex) glDeleteTextures(1, &peel_.opaque_tex);
    if (peel_.depth_tex[0]) glDeleteTextures(2, peel_.depth_tex);
    if (peel_.query)     glDeleteQueries(1, &peel_.query);

    if (bar3d_vao_)     glDeleteVertexArrays(1, &bar3d_vao_);
    if (bar3d_program_) glDeleteProgram(bar3d_program_);
    if (bar3d_edge_corner_vbo_) glDeleteBuffers(1, &bar3d_edge_corner_vbo_);
    if (bar3d_edge_vao_)        glDeleteVertexArrays(1, &bar3d_edge_vao_);
    if (bar3d_edge_program_)    glDeleteProgram(bar3d_edge_program_);
    for (auto& [k, e] : bar3d_cache_) {
        if (e.vbo)      glDeleteBuffers(1, &e.vbo);
        if (e.edge_vbo) glDeleteBuffers(1, &e.edge_vbo);
        if (e.index_ebo) glDeleteBuffers(1, &e.index_ebo);
    }

    for (auto& [k, e] : line_cache_) {
        if (e.vbo)      glDeleteBuffers(1, &e.vbo);
        if (e.dist_vbo) glDeleteBuffers(1, &e.dist_vbo);
    }
    for (auto& [k, e] : heat_cache_)     if (e.tex) glDeleteTextures(1, &e.tex);
    for (auto& [k, e] : scatter_cache_)  if (e.vbo) glDeleteBuffers(1, &e.vbo);
    for (auto& [k, e] : scatterz_cache_) if (e.vbo) glDeleteBuffers(1, &e.vbo);
    for (auto& [k, e] : bar_cache_) {
        if (e.fill_vbo) glDeleteBuffers(1, &e.fill_vbo);
        if (e.edge_vbo) glDeleteBuffers(1, &e.edge_vbo);
    }
    for (auto* m : { &line_err_cache_, &bar_err_cache_,
                     &scatter_err_cache_, &scatterz_err_cache_ })
        for (auto& [k, e] : *m) {
            if (e.fill_vbo)   glDeleteBuffers(1, &e.fill_vbo);
            if (e.stroke_vbo) glDeleteBuffers(1, &e.stroke_vbo);
        }
}

// -------------------------------------------------------------------------
// draw_lines
// -------------------------------------------------------------------------
void DataRenderer::draw_lines(const std::vector<LinePlot>& lines,
                              const CoordTransform& tr,
                              const PlotRect& pr)
{
    if (lines.empty()) return;

    begin_pass(pr, tr.win_h);
    glUseProgram(lineseg_program_);
    const float res[2] = { tr.win_w, tr.win_h };
    glUniform2fv(lineseg_u_.resolution, 1, res);
    glBindVertexArray(lineseg_vao_);

    std::vector<float> pts, dists;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        const auto& lp = lines[li];
        if (lp.x.size() < 2) continue;
        // LineStyle::None means "markers only, no stroke" — the SVG writer has
        // always skipped these; the data pass used to draw them anyway.
        if (lp.opts.linestyle == LineStyle::None) continue;

        const CacheKey key{ axes_index_, plane_index_, static_cast<int>(li) };
        LineCache& e = line_cache_[key];

        // The buffer is just the points now, so neither the view nor the
        // stroke width can invalidate it — both live in uniforms. Only a data
        // change does, or dropping out of the float-precision regime.
        const bool precise = float_resolves_view(e.span_x, tr.xmax - tr.xmin, tr.pw)
                          && float_resolves_view(e.span_y, tr.ymax - tr.ymin, tr.ph);
        const bool usable = e.vbo != 0
                         && e.segments > 0
                         && e.data_generation != 0
                         && e.data_generation == data_generation_
                         && e.loop == lp.opts.loop
                         && (e.data_space ? precise
                                          : (!precise && same_view(e.tr, tr)));
        if (!usable) {
            double xlo = lp.x[0], xhi = lp.x[0], ylo = lp.y[0], yhi = lp.y[0];
            for (std::size_t i = 1; i < lp.x.size(); ++i) {
                xlo = std::min(xlo, lp.x[i]); xhi = std::max(xhi, lp.x[i]);
                ylo = std::min(ylo, lp.y[i]); yhi = std::max(yhi, lp.y[i]);
            }
            e.anchor_x = 0.5 * (xlo + xhi); e.span_x = xhi - xlo;
            e.anchor_y = 0.5 * (ylo + yhi); e.span_y = yhi - ylo;
            e.data_space = float_resolves_view(e.span_x, tr.xmax - tr.xmin, tr.pw)
                        && float_resolves_view(e.span_y, tr.ymax - tr.ymin, tr.ph);

            // Padded so instance i can read prev/p0/p1/next as points
            // [i, i+1, i+2, i+3]. An open path repeats its own first and last
            // point, so the two ends see prev == p0 and next == p1 and
            // degenerate into butt caps. A looped one pads with the real
            // neighbours across the seam instead -- p[n-1] in front, then
            // p[0] and p[1] behind -- so the closing segment is an instance
            // like any other and the seam is mitered rather than capped. The
            // shader is untouched either way: the wrap is a buffer layout.
            const std::size_t n = lp.x.size();
            pts.clear();
            pts.reserve((n + 3) * 2);
            auto emit = [&](std::size_t i) {
                if (e.data_space) {
                    pts.push_back(static_cast<float>(lp.x[i] - e.anchor_x));
                    pts.push_back(static_cast<float>(lp.y[i] - e.anchor_y));
                } else {
                    pts.push_back(tr.to_px(lp.x[i]));
                    pts.push_back(tr.to_py(lp.y[i]));
                }
            };
            emit(lp.opts.loop ? n - 1 : 0);
            for (std::size_t i = 0; i < n; ++i) emit(i);
            if (lp.opts.loop) { emit(0); emit(1 % n); }
            else                emit(n - 1);

            if (e.vbo == 0) glGenBuffers(1, &e.vbo);
            glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(pts.size() * sizeof(float)),
                         pts.data(), GL_DYNAMIC_DRAW);

            e.segments        = static_cast<int>(lp.segment_count());
            e.loop            = lp.opts.loop;
            e.data_generation = data_generation_;
            e.tr              = tr;
            e.dist_valid      = false;   // arc lengths follow the points
        }
        if (e.segments <= 0) continue;

        // Four windows into the same point array, each offset by one point.
        // A VAO records the buffer bound when the attribute was specified, so
        // this has to be redone per entry.
        glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
        for (int a = 1; a <= 4; ++a) {
            glEnableVertexAttribArray(static_cast<unsigned>(a));
            glVertexAttribPointer(static_cast<unsigned>(a), 2, GL_FLOAT, GL_FALSE,
                                  2 * sizeof(float),
                                  reinterpret_cast<void*>((a - 1) * 2 * sizeof(float)));
            glVertexAttribDivisor(static_cast<unsigned>(a), 1);
        }

        const DataToPixel m = e.data_space
            ? data_to_pixel(tr, e.anchor_x, e.anchor_y)
            : DataToPixel{ { 1.0f, 1.0f }, { 0.0f, 0.0f } };
        glUniform2fv(lineseg_u_.scale,  1, m.scale);
        glUniform2fv(lineseg_u_.offset, 1, m.offset);
        glUniform1f(lineseg_u_.half_width, lp.opts.linewidth * 0.5f);

        // --- dash phase -----------------------------------------------------
        const DashPattern dash = dash_pattern(lp.opts.linestyle);
        if (!dash.dashed()) {
            // Solid: attribute 5 is left disabled, and the constant below is
            // what the shader reads for it. Doing this every iteration matters
            // because attribute enable state lives in the VAO, so a dashed
            // plot earlier in the list would otherwise leak its binding here.
            glDisableVertexAttribArray(5);
            glVertexAttrib1f(5, 0.0f);
            glUniform1f(lineseg_u_.dash_period, 0.0f);
        } else {
            // Reuse the cached arc lengths if the scale has only changed by a
            // uniform factor — see LineCache::dist_vbo for why that is the
            // interesting case.
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
                // Accumulated in double, from the same float positions the
                // shader will see, so each segment's CPU prefix and the
                // shader's own `dl` agree at the joins.
                const std::size_t n = lp.x.size();
                dists.clear();
                dists.reserve(n);
                double acc = 0.0;
                float prev_x = 0.0f, prev_y = 0.0f;
                for (std::size_t i = 0; i < n; ++i) {
                    float fx, fy;
                    if (e.data_space) {
                        fx = static_cast<float>(lp.x[i] - e.anchor_x);
                        fy = static_cast<float>(lp.y[i] - e.anchor_y);
                    } else {
                        fx = tr.to_px(lp.x[i]);
                        fy = tr.to_py(lp.y[i]);
                    }
                    const float px = m.scale[0] * fx + m.offset[0];
                    const float py = m.scale[1] * fy + m.offset[1];
                    if (i > 0) {
                        const double dx = px - prev_x, dy = py - prev_y;
                        acc += std::sqrt(dx * dx + dy * dy);
                    }
                    dists.push_back(static_cast<float>(acc));
                    prev_x = px; prev_y = py;
                }

                if (e.dist_vbo == 0) glGenBuffers(1, &e.dist_vbo);
                glBindBuffer(GL_ARRAY_BUFFER, e.dist_vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(dists.size() * sizeof(float)),
                             dists.data(), GL_DYNAMIC_DRAW);
                e.dist_ref_sx = m.scale[0];
                e.dist_ref_sy = m.scale[1];
                e.dist_valid  = true;
                dist_scale    = 1.0f;
            }

            // Instance i reads dists[i], the arc length at its own p0 — no
            // padding, unlike the point buffer, since nothing here needs a
            // neighbour.
            glBindBuffer(GL_ARRAY_BUFFER, e.dist_vbo);
            glEnableVertexAttribArray(5);
            glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
            glVertexAttribDivisor(5, 1);

            glUniform4fv(lineseg_u_.dash, 1, dash.seg);
            glUniform1f(lineseg_u_.dash_period, dash.period);
            glUniform1f(lineseg_u_.dist_scale, dist_scale);
        }

        const auto& c = lp.opts.color;
        glUniform4f(lineseg_u_.color,
                    c.r, c.g, c.b, c.a * lp.opts.alpha);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, e.segments);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    end_pass();
}

// -------------------------------------------------------------------------
// draw_scatter
// -------------------------------------------------------------------------
void DataRenderer::draw_scatter(const std::vector<ScatterPlot>& scatters,
                                const CoordTransform& tr,
                                const PlotRect& pr)
{
    if (scatters.empty()) return;

    begin_pass(pr, tr.win_h);
    glUseProgram(scatter_program_);
    const float res[2] = { tr.win_w, tr.win_h };
    glUniform2fv(scatter_u_.resolution, 1, res);
    glBindVertexArray(scatter_vao_);

    std::vector<float> inst;
    for (std::size_t si = 0; si < scatters.size(); ++si) {
        const auto& sp = scatters[si];
        if (sp.x.empty()) continue;

        const CacheKey key{ axes_index_, plane_index_, static_cast<int>(si) };
        InstanceCache& e = scatter_cache_[key];

        // Does the existing buffer still resolve the current view? Only the
        // fallback (pixel-space) form can go stale on a view change.
        const bool precise = float_resolves_view(e.span_x, tr.xmax - tr.xmin, tr.pw)
                          && float_resolves_view(e.span_y, tr.ymax - tr.ymin, tr.ph);
        const bool usable = e.vbo != 0
                         && e.instances > 0
                         && e.data_generation != 0
                         && e.data_generation == data_generation_
                         && e.size == sp.opts.size
                         && (e.data_space ? precise
                                          : (!precise && same_view(e.tr, tr)));

        if (!usable) {
            // Anchor at the midpoint of the data's own extent, so residuals
            // are at most half its span — that is what the precision test
            // above assumes, and it keeps the anchor independent of the view
            // (an anchor that tracked the view would defeat the whole point).
            double xlo = sp.x[0], xhi = sp.x[0], ylo = sp.y[0], yhi = sp.y[0];
            for (std::size_t i = 1; i < sp.x.size(); ++i) {
                xlo = std::min(xlo, sp.x[i]); xhi = std::max(xhi, sp.x[i]);
                ylo = std::min(ylo, sp.y[i]); yhi = std::max(yhi, sp.y[i]);
            }
            e.anchor_x = 0.5 * (xlo + xhi); e.span_x = xhi - xlo;
            e.anchor_y = 0.5 * (ylo + yhi); e.span_y = yhi - ylo;
            e.data_space = float_resolves_view(e.span_x, tr.xmax - tr.xmin, tr.pw)
                        && float_resolves_view(e.span_y, tr.ymax - tr.ymin, tr.ph);

            inst.clear();
            inst.reserve(sp.x.size() * 3);
            for (std::size_t i = 0; i < sp.x.size(); ++i) {
                if (e.data_space) {
                    inst.push_back(static_cast<float>(sp.x[i] - e.anchor_x));
                    inst.push_back(static_cast<float>(sp.y[i] - e.anchor_y));
                } else {
                    inst.push_back(tr.to_px(sp.x[i]));
                    inst.push_back(tr.to_py(sp.y[i]));
                }
                inst.push_back(sp.opts.size);
            }

            if (e.vbo == 0) glGenBuffers(1, &e.vbo);
            glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(inst.size() * sizeof(float)),
                         inst.data(), GL_DYNAMIC_DRAW);

            e.instances       = static_cast<int>(sp.x.size());
            e.data_generation = data_generation_;
            e.size            = sp.opts.size;
            e.tr              = tr;
        }

        // Re-point the instance attributes at this entry's buffer — see the
        // same note in draw_lines().
        glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                              reinterpret_cast<void*>(2 * sizeof(float)));
        glVertexAttribDivisor(2, 1);

        // Identity transform in the fallback, where centres are already pixels.
        const DataToPixel m = e.data_space
            ? data_to_pixel(tr, e.anchor_x, e.anchor_y)
            : DataToPixel{ { 1.0f, 1.0f }, { 0.0f, 0.0f } };
        glUniform2fv(scatter_u_.scale,  1, m.scale);
        glUniform2fv(scatter_u_.offset, 1, m.offset);

        const auto& c = sp.opts.color;
        glUniform4f(scatter_u_.color,
                    c.r, c.g, c.b, c.a * sp.opts.alpha);
        glUniform1i(scatter_u_.marker,
                    static_cast<int>(sp.opts.marker));

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
                                  const CoordTransform& tr,
                                  const PlotRect& pr)
{
    if (points.empty()) return;

    begin_pass(pr, tr.win_h);
    glUseProgram(scatterz_program_);
    const float res[2] = { tr.win_w, tr.win_h };
    glUniform2fv(scatterz_u_.resolution, 1, res);
    glBindVertexArray(scatterz_vao_);

    std::vector<float> inst;
    for (std::size_t si = 0; si < points.size(); ++si) {
        const auto& sp = points[si];
        if (sp.x.empty()) continue;

        const CacheKey key{ axes_index_, plane_index_, static_cast<int>(si) };
        InstanceCache& e = scatterz_cache_[key];

        const bool precise = float_resolves_view(e.span_x, tr.xmax - tr.xmin, tr.pw)
                          && float_resolves_view(e.span_y, tr.ymax - tr.ymin, tr.ph);
        // The colour mapping is baked into this buffer too, so it joins the
        // key. Caching it also retires the per-point LUT lookup that used to
        // run on every frame.
        const bool usable = e.vbo != 0
                         && e.instances > 0
                         && e.data_generation != 0
                         && e.data_generation == data_generation_
                         && e.size == sp.opts.size
                         && e.cmap == sp.opts.cmap
                         && e.vmin == sp.opts.vmin && e.vmax == sp.opts.vmax
                         && e.alpha == sp.opts.alpha
                         && (e.data_space ? precise
                                          : (!precise && same_view(e.tr, tr)));

        if (!usable) {
            double xlo = sp.x[0], xhi = sp.x[0], ylo = sp.y[0], yhi = sp.y[0];
            for (std::size_t i = 1; i < sp.x.size(); ++i) {
                xlo = std::min(xlo, sp.x[i]); xhi = std::max(xhi, sp.x[i]);
                ylo = std::min(ylo, sp.y[i]); yhi = std::max(yhi, sp.y[i]);
            }
            e.anchor_x = 0.5 * (xlo + xhi); e.span_x = xhi - xlo;
            e.anchor_y = 0.5 * (ylo + yhi); e.span_y = yhi - ylo;
            e.data_space = float_resolves_view(e.span_x, tr.xmax - tr.xmin, tr.pw)
                        && float_resolves_view(e.span_y, tr.ymax - tr.ymin, tr.ph);

            // Per-point color: same CPU colormap-LUT lookup as draw_heatmap.
            const uint8_t* lut   = colormaps::get(sp.opts.cmap);
            const float    vmin  = sp.opts.vmin, vrange = sp.opts.vmax - sp.opts.vmin;

            inst.clear();
            inst.reserve(sp.x.size() * 7);
            for (std::size_t i = 0; i < sp.x.size(); ++i) {
                float t = (vrange != 0.0f)
                        ? static_cast<float>((sp.z[i] - vmin) / vrange) : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                const uint8_t* c = &lut[static_cast<int>(t * 255.0f) * 4];

                if (e.data_space) {
                    inst.push_back(static_cast<float>(sp.x[i] - e.anchor_x));
                    inst.push_back(static_cast<float>(sp.y[i] - e.anchor_y));
                } else {
                    inst.push_back(tr.to_px(sp.x[i]));
                    inst.push_back(tr.to_py(sp.y[i]));
                }
                inst.push_back(sp.opts.size);
                inst.push_back(static_cast<float>(c[0]) / 255.0f);
                inst.push_back(static_cast<float>(c[1]) / 255.0f);
                inst.push_back(static_cast<float>(c[2]) / 255.0f);
                inst.push_back(static_cast<float>(c[3]) / 255.0f * sp.opts.alpha);
            }

            if (e.vbo == 0) glGenBuffers(1, &e.vbo);
            glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(inst.size() * sizeof(float)),
                         inst.data(), GL_DYNAMIC_DRAW);

            e.instances       = static_cast<int>(sp.x.size());
            e.data_generation = data_generation_;
            e.size            = sp.opts.size;
            e.cmap            = sp.opts.cmap;
            e.vmin            = sp.opts.vmin;
            e.vmax            = sp.opts.vmax;
            e.alpha           = sp.opts.alpha;
            e.tr              = tr;
        }

        glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
        glVertexAttribDivisor(1, 1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                              reinterpret_cast<void*>(2 * sizeof(float)));
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glVertexAttribDivisor(3, 1);

        const DataToPixel m = e.data_space
            ? data_to_pixel(tr, e.anchor_x, e.anchor_y)
            : DataToPixel{ { 1.0f, 1.0f }, { 0.0f, 0.0f } };
        glUniform2fv(scatterz_u_.scale,  1, m.scale);
        glUniform2fv(scatterz_u_.offset, 1, m.offset);

        glUniform1i(scatterz_u_.marker,
                    static_cast<int>(sp.opts.marker));

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
                             const CoordTransform& tr,
                             const PlotRect& pr)
{
    if (bars.empty()) return;

    begin_pass(pr, tr.win_h);
    glUseProgram(line_program_);
    const float res[2] = { tr.win_w, tr.win_h };
    glUniform2fv(line_u_.resolution, 1, res);
    glBindVertexArray(line_vao_);

    const float py0 = tr.to_py(0.0);  // pixel y of the zero baseline

    // Re-points the shared VAO's attribute at whichever buffer is bound. A
    // VAO remembers the buffer bound when the attribute was *specified*, not
    // the one bound at draw time, so without this bars would read whichever
    // line cache was specified last.
    auto point_attrib_at = [](unsigned int vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    };

    std::vector<float> scratch;
    for (std::size_t bi = 0; bi < bars.size(); ++bi) {
        const auto& bp = bars[bi];
        if (bp.centers.empty()) continue;

        const double half = bp.bar_width * 0.5;
        const std::size_t n = bp.centers.size();

        const CacheKey key{ axes_index_, plane_index_, static_cast<int>(bi) };
        BarCache& e = bar_cache_[key];

        // Sampled before the fill branch below, which overwrites e.bar_width —
        // otherwise the outline check further down would always see it as
        // unchanged and a bar-width edit would leave a stale outline.
        const bool width_same = (e.bar_width == bp.bar_width);

        // --- filled rectangles (2 triangles = 6 verts each), in data space ---
        const bool precise = float_resolves_view(e.span_x, tr.xmax - tr.xmin, tr.pw)
                          && float_resolves_view(e.span_y, tr.ymax - tr.ymin, tr.ph);
        const bool fill_ok = e.fill_vbo != 0
                          && e.fill_verts > 0
                          && e.fill_generation != 0
                          && e.fill_generation == data_generation_
                          && width_same
                          && (e.data_space ? precise
                                           : (!precise && same_view(e.fill_tr, tr)));
        if (!fill_ok) {
            // Extent includes the zero baseline, since every bar spans to it.
            double xlo = bp.centers[0] - half, xhi = bp.centers[0] + half;
            double ylo = 0.0, yhi = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                xlo = std::min(xlo, bp.centers[i] - half);
                xhi = std::max(xhi, bp.centers[i] + half);
                ylo = std::min(ylo, bp.heights[i]);
                yhi = std::max(yhi, bp.heights[i]);
            }
            e.anchor_x = 0.5 * (xlo + xhi); e.span_x = xhi - xlo;
            e.anchor_y = 0.5 * (ylo + yhi); e.span_y = yhi - ylo;
            e.data_space = float_resolves_view(e.span_x, tr.xmax - tr.xmin, tr.pw)
                        && float_resolves_view(e.span_y, tr.ymax - tr.ymin, tr.ph);

            scratch.clear();
            scratch.reserve(n * 12);
            for (std::size_t i = 0; i < n; ++i) {
                float xl, xr, yt, yb;
                if (e.data_space) {
                    xl = static_cast<float>(bp.centers[i] - half - e.anchor_x);
                    xr = static_cast<float>(bp.centers[i] + half - e.anchor_x);
                    yt = static_cast<float>(bp.heights[i] - e.anchor_y);
                    yb = static_cast<float>(0.0 - e.anchor_y);
                } else {
                    xl = tr.to_px(bp.centers[i] - half);
                    xr = tr.to_px(bp.centers[i] + half);
                    yt = tr.to_py(bp.heights[i]);
                    yb = py0;
                }
                scratch.insert(scratch.end(), {
                    xl, yb,  xr, yb,  xr, yt,
                    xl, yb,  xr, yt,  xl, yt,
                });
            }
            if (e.fill_vbo == 0) glGenBuffers(1, &e.fill_vbo);
            glBindBuffer(GL_ARRAY_BUFFER, e.fill_vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(scratch.size() * sizeof(float)),
                         scratch.data(), GL_DYNAMIC_DRAW);
            e.fill_verts      = static_cast<int>(n * 6);
            e.fill_generation = data_generation_;
            e.bar_width       = bp.bar_width;
            e.fill_tr         = tr;
        }

        point_attrib_at(e.fill_vbo);
        const DataToPixel m = e.data_space
            ? data_to_pixel(tr, e.anchor_x, e.anchor_y)
            : DataToPixel{ { 1.0f, 1.0f }, { 0.0f, 0.0f } };
        glUniform2fv(line_u_.scale,  1, m.scale);
        glUniform2fv(line_u_.offset, 1, m.offset);

        const auto& c = bp.opts.color;
        glUniform4f(line_u_.color,
                    c.r, c.g, c.b, c.a * bp.opts.alpha);
        glDrawArrays(GL_TRIANGLES, 0, e.fill_verts);

        // --- outlines (a triangulated frame per bar, batched into one draw) ---
        // Pixel space, so identity transform and the view joins the key.
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
                    const float xl = tr.to_px(bp.centers[i] - half);
                    const float xr = tr.to_px(bp.centers[i] + half);
                    const float yt = tr.to_py(bp.heights[i]);
                    const float yb = py0;
                    build_rect_outline(xl, yb, xr, yt, bp.opts.linewidth, scratch);
                }
                if (!scratch.empty()) {
                    if (e.edge_vbo == 0) glGenBuffers(1, &e.edge_vbo);
                    glBindBuffer(GL_ARRAY_BUFFER, e.edge_vbo);
                    glBufferData(GL_ARRAY_BUFFER,
                                 static_cast<GLsizeiptr>(scratch.size() * sizeof(float)),
                                 scratch.data(), GL_DYNAMIC_DRAW);
                    e.edge_verts      = static_cast<int>(scratch.size() / 2);
                    e.edge_generation = data_generation_;
                    e.edge_tr         = tr;
                    e.linewidth       = bp.opts.linewidth;
                    e.pixel_ratio     = pixel_ratio_;
                }
            }
            if (e.edge_verts > 0) {
                point_attrib_at(e.edge_vbo);
                glUniform2fv(line_u_.scale,  1, k_identity_scale);
                glUniform2fv(line_u_.offset, 1, k_identity_offset);

                const auto& ec = bp.opts.edgecolor;
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
// draw_error_bars
// -------------------------------------------------------------------------
void DataRenderer::draw_error_bars(const AllPlotData& all,
                                   const CoordTransform& tr,
                                   const PlotRect& pr)
{
    // The overwhelmingly common case is no error bars anywhere, and it must
    // cost nothing: no pass, no program bind, no state change. Each scan is
    // over the *plot object* count, not the point count.
    auto none = [](const auto& v) {
        return std::none_of(v.begin(), v.end(),
                            [](const auto& p) { return !p.err.empty(); });
    };
    if (none(all.lines) && none(all.bars) &&
        none(all.scatters) && none(all.scatter_z)) return;

    begin_pass(pr, tr.win_h);
    glUseProgram(line_program_);
    const float res[2] = { tr.win_w, tr.win_h };
    glUniform2fv(line_u_.resolution, 1, res);
    glUniform2fv(line_u_.scale,  1, k_identity_scale);   // pixel space throughout
    glUniform2fv(line_u_.offset, 1, k_identity_offset);
    glBindVertexArray(line_vao_);

    std::vector<float> fill_scratch, stroke_scratch;

    // One plot object's worth: rebuild the pixel-space geometry when anything
    // it depends on moved, then draw its two halves. Generic over the plot
    // type because the four kinds differ only in which vectors the bars hang
    // off and which color they fall back to.
    auto emit = [&](auto& cache, const auto& plot, int index,
                    const CowVec<double>& xs, const CowVec<double>& ys,
                    const Color& fallback)
    {
        const ErrorBarOptions& style = plot.opts.errorbar;
        if (style.linewidth <= 0.0f) return;

        const CacheKey key{ axes_index_, plane_index_, index };
        ErrCache& e = cache[key];
        const bool ok = e.generation != 0
                     && e.generation == data_generation_
                     && e.linewidth == style.linewidth
                     && e.capsize == style.capsize
                     && e.capstyle == static_cast<int>(style.capstyle)
                     && e.boxwidth == style.boxwidth
                     && e.box_alpha == style.box_alpha
                     && e.pixel_ratio == pixel_ratio_
                     && same_view(e.tr, tr);
        if (!ok) {
            fill_scratch.clear();
            stroke_scratch.clear();
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
            upload(e.fill_vbo,   e.fill_verts,   fill_scratch);
            upload(e.stroke_vbo, e.stroke_verts, stroke_scratch);

            e.generation  = data_generation_;
            e.tr          = tr;
            e.linewidth   = style.linewidth;
            e.capsize     = style.capsize;
            e.capstyle    = static_cast<int>(style.capstyle);
            e.boxwidth    = style.boxwidth;
            e.box_alpha   = style.box_alpha;
            e.pixel_ratio = pixel_ratio_;
        }

        const Color c = style.color.value_or(fallback);

        // Same re-pointing dance draw_bars documents: the VAO remembers the
        // buffer bound when the attribute was specified, not at draw time.
        auto draw = [&](unsigned int vbo, int verts, float alpha) {
            if (vbo == 0 || verts <= 0 || alpha <= 0.0f) return;
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
            glUniform4f(line_u_.color, c.r, c.g, c.b, alpha);
            glDrawArrays(GL_TRIANGLES, 0, verts);
        };
        // Interiors first, so the outline and the whisker read over them.
        draw(e.fill_vbo,   e.fill_verts,   c.a * style.box_alpha);
        draw(e.stroke_vbo, e.stroke_verts, c.a);
    };

    for (std::size_t i = 0; i < all.lines.size(); ++i) {
        const auto& p = all.lines[i];
        if (!p.err.empty())
            emit(line_err_cache_, p, static_cast<int>(i), p.x, p.y, p.opts.color);
    }
    for (std::size_t i = 0; i < all.bars.size(); ++i) {
        const auto& p = all.bars[i];
        // Heights, not the zero baseline: an error bar measures the bar's tip.
        if (!p.err.empty())
            emit(bar_err_cache_, p, static_cast<int>(i),
                 p.centers, p.heights, p.opts.edgecolor);
    }
    for (std::size_t i = 0; i < all.scatters.size(); ++i) {
        const auto& p = all.scatters[i];
        if (!p.err.empty())
            emit(scatter_err_cache_, p, static_cast<int>(i), p.x, p.y, p.opts.color);
    }
    for (std::size_t i = 0; i < all.scatter_z.size(); ++i) {
        const auto& p = all.scatter_z[i];
        // Scatter_z has no single color of its own — each point's comes from
        // the colormap — so an unset errorbar color falls back to black
        // rather than to a per-point value the bar cannot have.
        if (!p.err.empty())
            emit(scatterz_err_cache_, p, static_cast<int>(i), p.x, p.y, Color::Black);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    end_pass();
}

// -------------------------------------------------------------------------
// draw_heatmap
// -------------------------------------------------------------------------
// The colormapped texture, built once per data generation and left bound on
// unit 0. Split out of draw_heatmap() when the plane quad arrived, because the
// two would otherwise carry a colormap loop and an origin flip each and could
// disagree about a heatmap drawn both ways. The pixels come from
// plane_heatmap_rgba(), which the SVG plane path also uses -- so the raster
// plane, the raster axes and the vector plane are one set of texels.
void DataRenderer::ensure_heatmap_texture(HeatCache& hc, const HeatmapPlot& hp) {
    const bool flip = (hp.opts.origin == "lower");

    // Depends only on the data and the colour mapping, never on the view, so
    // unlike the line cache this survives pan, zoom and an orbit.
    const bool usable = hc.tex != 0
                     && hc.data_generation != 0
                     && hc.data_generation == data_generation_
                     && hc.cmap == hp.opts.cmap
                     && hc.vmin == hp.opts.vmin && hc.vmax == hp.opts.vmax
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
    hc.vmin = hp.opts.vmin; hc.vmax = hp.opts.vmax;
    hc.flip = flip;
    hc.rows = hp.rows; hc.cols = hp.cols;
}

void DataRenderer::draw_heatmap(const std::vector<HeatmapPlot>& heatmaps,
                                const CoordTransform& tr,
                                const PlotRect& pr)
{
    if (heatmaps.empty()) return;

    begin_pass(pr, tr.win_h);
    glUseProgram(heatmap_program_);
    const float res[2] = { tr.win_w, tr.win_h };
    glUniform2fv(heatmap_u_.resolution, 1, res);
    glUniform1i(heatmap_u_.tex, 0);
    glBindVertexArray(heatmap_vao_);

    for (std::size_t hi = 0; hi < heatmaps.size(); ++hi) {
        const auto& hp = heatmaps[hi];
        if (hp.rows <= 0 || hp.cols <= 0) continue;

        const CacheKey key{ axes_index_, plane_index_, static_cast<int>(hi) };
        ensure_heatmap_texture(heat_cache_[key], hp);

        // Quad covering the heatmap's xrange × yrange in data space, mapped
        // to pixel space (for imshow() that is the old [0,cols] × [0,rows]).
        // Tex coords: (0,0)=top-left, (1,1)=bottom-right in GL convention;
        // `lo` carries the same texture edge the index 0 end always did, so a
        // reversed range mirrors the image rather than folding the quad.
        const float x0 = tr.to_px(hp.xrange.lo), x1 = tr.to_px(hp.xrange.hi);
        const float y0 = tr.to_py(hp.yrange.lo), y1 = tr.to_py(hp.yrange.hi);
        const float verts[24] = {
            x0, y0,  0.0f, 1.0f,
            x1, y0,  1.0f, 1.0f,
            x1, y1,  1.0f, 0.0f,
            x0, y0,  0.0f, 1.0f,
            x1, y1,  1.0f, 0.0f,
            x0, y1,  0.0f, 0.0f,
        };
        glBindBuffer(GL_ARRAY_BUFFER, heatmap_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

        // The texture is already bound on unit 0 by either cache branch, and
        // is owned by heat_cache_ now — no per-frame delete.
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    end_pass();
}

// -------------------------------------------------------------------------
// 3D planes -- a plane is a little screen (step 7a)
// -------------------------------------------------------------------------
// A plane's contents are rendered into a framebuffer of the plane's own, by
// the *2D* draw calls in this same class and in the order a 2D axes draws
// them, and the plane then enters the scene as one textured transparent quad.
// That is what its heatmap always was; step 7a generalises it to everything
// else on the plane.
//
// **What this replaces is worth stating, because it was a lot.** Every
// primitive on a plane is exactly coplanar with every other, so no depth test
// can recover the 2D painter order and the old path had to fake it: three
// dedicated programs (a fill, a ribbon expanded in the plane, and a billboard
// marker), GL_LEQUAL, and a per-batch NDC nudge toward the viewer to break the
// ties LEQUAL would otherwise lose as speckle. None of that exists any more.
// The order is the 2D one because it *is* the 2D one -- the same code, the
// same sequence, the same painter's stack -- and the plane arrives in the
// scene as a single flat surface with a single depth per pixel.
//
// Two further things fall out, and both matter downstream:
//
//   - one fragment per pixel means a plane is exactly one depth layer, which
//     is what lets step 7b composite crossing translucent planes per pixel
//     without a BSP;
//   - the raster is clipped to the box face, so a heatmap wider than the
//     limits no longer draws outside the box -- a 2D axes clips to its frame
//     and now so does a plane.
//
// Called twice per frame with the bars in between (see ScenePass): opaque
// planes write depth ahead of the bars and are resolved against them by the
// buffer, translucent ones come after with depth writes off, so a slice in
// front of a bar grid blends over it and one behind is rejected by the depth
// the bars wrote.
//
// Since step 7b the translucent half is not a back-to-front draw of quads but
// composite_planes3d(), which sorts the planes at each fragment instead of as
// whole objects -- so two translucent planes that cross are right on *both*
// sides of their intersection line. The whole-object order below survives as
// the grouping when there are more planes than one group holds.
//
// The one pairing with no exact answer is a translucent plane against a
// translucent bar, which is the same no-separating-plane case spec_3d.md §10
// already admits for two bar3d objects: a bar grid is not one depth layer per
// pixel, so it cannot be a sample in the composite.
void DataRenderer::draw_planes3d(const std::vector<PlaneSnapshot>& planes,
                                 const Projector3D& proj, const PlotRect& pr,
                                 float win_w, float win_h, ScenePass pass,
                                 const std::vector<std::size_t>* explicit_order) {
    const bool want_translucent = (pass == ScenePass::Translucent);

    // Which planes this call is for, and -- for the translucent half -- in
    // what order. Far to near by the same heuristic plan_planes3d() sorts by.
    // Since 7b the composite does not need this order at all; it survives as
    // the *grouping* order for a scene with more translucent planes than one
    // group holds, and as the last place the raster and vector paths still
    // agree on which heuristic they are using.
    // plane_drawn(), not "has a heatmap": a plane carrying only a line or a
    // scatter has just as much to draw, and testing for the backdrop instead
    // of for the contents is what made those planes invisible the first time.
    std::vector<std::size_t> order;
    if (explicit_order) {
        order = *explicit_order;                 // already filtered and sorted
    } else {
        for (std::size_t i = 0; i < planes.size(); ++i)
            if (plane_translucent(planes[i]) == want_translucent
                && plane_drawn(planes[i]))
                order.push_back(i);
        // Nothing to sort under peeling -- see draw_bars3d() for why.
        if (want_translucent && !peel_.active)
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return plane_distance(planes[a], proj) > plane_distance(planes[b], proj);
            });
    }
    if (order.empty()) return;

    // The rasters first, all of them, before any of the scene state below is
    // touched: rendering one rebinds the framebuffer and the viewport, and
    // doing that from inside the quad loop would mean restoring the scene's
    // pass state after every plane instead of once.
    //
    // The cap is the plot rect's longer side. A plane can carry no useful
    // detail beyond the viewport that displays it, so this is an upper bound
    // that is always sufficient and -- being independent of the camera -- one
    // that never needs a re-render when the view turns.
    //
    // Under peeling this has already happened, in prepare_plane_rasters(),
    // and it must not happen again here: the peel's early-out is an occlusion
    // query, and a query counts every draw made while it is running.
    std::vector<PlaneRaster> rasters(planes.size());
    for (const std::size_t pi : order) {
        const PlaneSnapshot& pl = planes[pi];
        rasters[pi] = plane_raster(proj, pl.orient, pl.offset);
        if (peel_.active) continue;
        const CacheKey key{ axes_index_, static_cast<int>(pi), -1 };
        render_plane_raster(pl, static_cast<int>(pi), rasters[pi],
                            plane_raster_cache_[key]);
    }

    // A plane whose raster did not come out is not a plane either path can
    // draw, and dropping it here rather than inside each loop is what lets the
    // composite promise every member of a group has a texture to bind.
    std::vector<std::size_t> drawn;
    for (const std::size_t pi : order) {
        const CacheKey key{ axes_index_, static_cast<int>(pi), -1 };
        const PlaneRasterCache& c = plane_raster_cache_[key];
        if (c.valid && c.tex) drawn.push_back(pi);
    }
    if (drawn.empty()) return;

    begin_pass(pr, win_h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(want_translucent && !peel_.active ? GL_FALSE : GL_TRUE);
    // **Premultiplied**, because that is what a raster accumulated over
    // transparent black holds (see begin_pass). Blending it with the ordinary
    // straight-alpha func would multiply by alpha twice and every plane would
    // come out too faint -- the failure that only shows where a texel is
    // neither empty nor fully covered.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // Under peeling a translucent plane takes the same quad path an opaque one
    // takes, and step 7b's composite is not called at all: the peel resolves
    // plane against plane at the fragment for the same reason it resolves
    // plane against bar there, and does it without a per-plane texture unit or
    // a group cap. The composite stays for the SVG writer's cousin -- and for
    // the fallback below, which is what a machine with no working peel targets
    // gets.
    if (want_translucent && !peel_.active) {
        // Groups of kMaxCompositePlanes, far to near. One group is the whole
        // list in every scene anyone has drawn so far, and then the composite
        // is exact outright; a longer list degrades to the old whole-object
        // order *between* groups and stays exact within each.
        for (std::size_t g = 0; g < drawn.size(); g += kMaxCompositePlanes) {
            const std::size_t hi =
                std::min(drawn.size(), g + static_cast<std::size_t>(kMaxCompositePlanes));
            const std::vector<std::size_t> group(drawn.begin() + static_cast<std::ptrdiff_t>(g),
                                                 drawn.begin() + static_cast<std::ptrdiff_t>(hi));
            composite_planes3d(planes, rasters, group, proj, pr, win_w, win_h);
        }
        // Back to what begin_pass() establishes -- straight alpha on the colour
        // channels, accumulated coverage on alpha. Restoring the plain
        // glBlendFunc() here would leave the pass in a state begin_pass() never
        // sets, which is how a later draw in the same pass would silently get the
        // squared alpha back.
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE,       GL_ONE_MINUS_SRC_ALPHA);
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

    for (const std::size_t pi : drawn) {
        const CacheKey key{ axes_index_, static_cast<int>(pi), -1 };
        const PlaneRasterCache& c = plane_raster_cache_[key];

        // The whole-plane alpha multiplies the finished raster here rather
        // than being baked into every vertex colour, which is what the old
        // geometry path had to do. Changing it now costs a uniform.
        glUniform1f(pu.alpha, std::clamp(planes[pi].opts.alpha, 0.0f, 1.0f));
        glBindTexture(GL_TEXTURE_2D, c.tex);

        // Four corners is not enough vertex data to be worth caching, and the
        // anchor is the quad's own centre for the same reason the bar buffer
        // carries one: the floats measure offsets within the data's span
        // rather than its distance from the origin.
        const PlaneRaster& q = rasters[pi];
        const Vec3 anchor{ (q.p[0].x + q.p[2].x) * 0.5,
                           (q.p[0].y + q.p[2].y) * 0.5,
                           (q.p[0].z + q.p[2].z) * 0.5 };
        float box_scale[3], box_offset[3];
        proj.box_affine(anchor, box_scale, box_offset);
        glUniform3fv(pu.box_scale, 1, box_scale);
        glUniform3fv(pu.box_offset, 1, box_offset);

        float verts[30];
        const int tri[6] = { 0, 1, 2, 0, 2, 3 };
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
    // Back to what begin_pass() establishes -- straight alpha on the colour
    // channels, accumulated coverage on alpha. Restoring the plain
    // glBlendFunc() here would leave the pass in a state begin_pass() never
    // sets, which is how a later draw in the same pass would silently get the
    // squared alpha back.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE,       GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    end_pass();
}

// -------------------------------------------------------------------------
// The whole 3D scene, in two phases (post-step-7d)
// -------------------------------------------------------------------------
// What this replaces is worth stating, because it looked like an order and
// was not. render_frame() used to call the three per-kind draws in a fixed
// sequence -- planes(opaque), bars, surfaces, planes(translucent) -- and each
// sorted only its *own* objects. Two things were wrong with that and neither
// needed any new machinery to fix:
//
//   - A translucent surface always painted after a translucent bar grid,
//     whatever the camera said. The two need not interpenetrate for that to
//     be visibly wrong; they only need to be in front of one another.
//   - Worse, opaque surfaces were drawn after *translucent* bars. Translucent
//     bars write no depth, so an opaque surface behind them passed the depth
//     test and painted straight over: a translucent bar in front of an opaque
//     surface vanished.
//
// The three distances (plane_distance, bar3d_plot_distance,
// surface_plot_distance) were built to be comparable -- all three are a
// box-space distance from eye_coord() -- and nothing had ever compared them.
// That is the whole of the fix.
void DataRenderer::draw_scene3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                                const PlotRect& pr, float win_w, float win_h) {
    // ---- Phase 1: everything opaque, in any order at all.
    // The depth buffer resolves it per fragment, which is why nothing here
    // needs sorting and why an opaque scene has never had an ordering bug.
    draw_planes3d  (snap.planes,   proj, pr, win_w, win_h, ScenePass::Opaque);
    draw_bars3d    (snap.bars3d,   proj, pr, win_w, win_h, ScenePass::Opaque);
    draw_surfaces3d(snap.surfaces, proj, pr, win_w, win_h, ScenePass::Opaque);
    draw_surface_tri3d(snap.surface_tri, proj, pr, win_w, win_h, ScenePass::Opaque);
    draw_scatter3d (snap.scatter3d, proj, pr, win_w, win_h, ScenePass::Opaque);
    draw_lines3d   (snap.lines3d,  proj, pr, win_w, win_h, ScenePass::Opaque);
    draw_errorbars3d(snap, proj, pr, win_w, win_h, ScenePass::Opaque);

    // ---- Phase 2, first choice: depth peeling (step 8).
    // Exact per pixel, so it does not need -- and does not use -- any of the
    // three object distances below. It declines only when the environment
    // turns it off or the targets could not be built.
    bool any_translucent = false;
    for (const PlaneSnapshot& p : snap.planes)
        any_translucent = any_translucent || (plane_translucent(p) && plane_drawn(p));
    for (const Bar3DPlot& b : snap.bars3d)
        any_translucent = any_translucent || bar3d_translucent(b);
    for (const SurfacePlot& sp : snap.surfaces)
        any_translucent = any_translucent || surface_translucent(sp);
    for (const SurfaceTriPlot& sm : snap.surface_tri)
        any_translucent = any_translucent || surface_tri_translucent(sm);
    for (const Scatter3DPlot& sc : snap.scatter3d)
        any_translucent = any_translucent || scatter3d_translucent(sc);
    for (const Line3DPlot& ln : snap.lines3d)
        any_translucent = any_translucent || line3d_translucent(ln);
    for (const Scatter3DPlot& sc : snap.scatter3d)
        any_translucent = any_translucent || errorbar3d_translucent(sc);
    for (const Line3DPlot& ln : snap.lines3d)
        any_translucent = any_translucent || errorbar3d_translucent(ln);
    if (!any_translucent) return;
    if (peel_translucent3d(snap, proj, pr, win_w, win_h)) return;

    // ---- Phase 2, the fallback: everything translucent, one list, far to
    // near. Exact only for objects separated in depth (see the correction in
    // memory/spec_impl.md), which is why it is the fallback and not the path.
    enum class Kind { Plane, Bar, Surface, Mesh, Scatter, Line, ErrorBar };
    struct Item { Kind kind; std::size_t index; double distance; };
    std::vector<Item> list;

    for (std::size_t i = 0; i < snap.planes.size(); ++i)
        if (plane_translucent(snap.planes[i]) && plane_drawn(snap.planes[i]))
            list.push_back({ Kind::Plane, i, plane_distance(snap.planes[i], proj) });
    for (std::size_t i = 0; i < snap.bars3d.size(); ++i)
        if (bar3d_translucent(snap.bars3d[i]))
            list.push_back({ Kind::Bar, i, bar3d_plot_distance(snap.bars3d[i], proj) });
    for (std::size_t i = 0; i < snap.surfaces.size(); ++i)
        if (surface_translucent(snap.surfaces[i]))
            list.push_back({ Kind::Surface, i, surface_plot_distance(snap.surfaces[i], proj) });
    for (std::size_t i = 0; i < snap.surface_tri.size(); ++i)
        if (surface_tri_translucent(snap.surface_tri[i]))
            list.push_back({ Kind::Mesh, i,
                             surface_tri_plot_distance(snap.surface_tri[i], proj) });
    for (std::size_t i = 0; i < snap.scatter3d.size(); ++i)
        if (scatter3d_translucent(snap.scatter3d[i]))
            list.push_back({ Kind::Scatter, i,
                             scatter3d_plot_distance(snap.scatter3d[i], proj) });
    for (std::size_t i = 0; i < snap.lines3d.size(); ++i)
        if (line3d_translucent(snap.lines3d[i]))
            list.push_back({ Kind::Line, i,
                             line3d_plot_distance(snap.lines3d[i], proj) });
    // A series' error bars at its own series' distance, listed after the
    // series itself so that at equal distance the bars draw over it.
    for (std::size_t i = 0; i < snap.scatter3d.size(); ++i)
        if (errorbar3d_translucent(snap.scatter3d[i]))
            list.push_back({ Kind::ErrorBar, i,
                             scatter3d_plot_distance(snap.scatter3d[i], proj) });
    for (std::size_t i = 0; i < snap.lines3d.size(); ++i)
        if (errorbar3d_translucent(snap.lines3d[i]))
            list.push_back({ Kind::ErrorBar, snap.scatter3d.size() + i,
                             line3d_plot_distance(snap.lines3d[i], proj) });
    if (list.empty()) return;

    // Stable, so objects at equal distance keep the kind order above and one
    // frame is the same frame as the last. Same reason plan_bars3d() sorts
    // stably: an order that is only usually the same order is not one.
    std::stable_sort(list.begin(), list.end(),
                     [](const Item& a, const Item& b) { return a.distance > b.distance; });

    // Drawn as runs of one kind rather than one object at a time, which costs
    // nothing in correctness and matters for the planes: a run of consecutive
    // planes is exactly a composite group, so the per-fragment sort of step 7b
    // still applies to every plane nothing is interleaved between. A bar grid
    // or a surface landing between two planes splits them into two groups,
    // which is the same fallback the group cap already uses.
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
// Depth peeling (step 8)
// -------------------------------------------------------------------------
// **Eight, and it is a compromise rather than a sufficient number.**
//
// What a scene needs is measurable and was measured: `sextant_perf_test peel`
// (4c.5) renders one at a rising count until the picture stops changing, and
// the gallery's sixth cell -- a sheet threaded through a 13x13 grid of
// translucent bars, where a ray crosses two faces per bar -- converges at
// **twelve**. Eight does not reach the back of it, and what the last layer
// does not reach is simply absent from the picture.
//
// That is not a defect being left in; it is a cost being declined. A pass is a
// full redraw of every translucent primitive, so the deepest scenes -- the
// ones that need the most layers -- are also the ones each extra layer costs
// most on: 4c.4's everything-at-once figure moves 72 ms -> 89 ms between eight
// and sixteen. Eight covers every scene in the gallery but that one, and the
// scenes it does not cover have two ways out that a wrong default would not
// have: `PngExportOptions::peel_layers` per export, and SEXTANT_PEEL_LAYERS
// for a live window.
//
// The asymmetry is worth stating even so, because it is what makes raising it
// cheap *for the scenes that do not need it*: the occlusion query below breaks
// out the moment a pass peels nothing, so a scene needing three layers runs
// four passes at any cap. Only scenes that genuinely go deeper pay. If the
// per-pass cost ever falls -- or if the peel learns to report that it ran out,
// which unlike the SVG's split bound it currently cannot -- this number should
// be revisited against 4c.5 rather than re-guessed.
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

// An explicit override wins over the environment, and the environment over the
// default. The order matters in exactly one direction: SEXTANT_PEEL_LAYERS=0
// is the negative control every step-8 assertion is read against, and it must
// stay reachable *through* an export that asks for more layers -- otherwise
// the control silently stops controlling for the one path it is checked on.
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
        const CacheKey key{ axes_index_, static_cast<int>(pi), -1 };
        render_plane_raster(pl, static_cast<int>(pi), r, plane_raster_cache_[key]);
    }
}

bool DataRenderer::ensure_peel_targets(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    // A negative size is the "already asked, the driver said no" mark set
    // below. Retrying would build and tear down five textures every frame for
    // an answer that cannot change.
    if (peel_.w < 0) return false;
    if (peel_.fbo && peel_.w == w && peel_.h == h) return true;

    // Everything here is sized to the plot rect, so a resize keeps nothing.
    // The query is the one exception -- it is a name, not storage.
    const unsigned int query = peel_.query;
    if (peel_.fbo)          glDeleteFramebuffers(1, &peel_.fbo);
    if (peel_.accum_fbo)    glDeleteFramebuffers(1, &peel_.accum_fbo);
    if (peel_.copy_fbo)     glDeleteFramebuffers(1, &peel_.copy_fbo);
    if (peel_.layer_tex)    glDeleteTextures(1, &peel_.layer_tex);
    if (peel_.accum_tex)    glDeleteTextures(1, &peel_.accum_tex);
    if (peel_.opaque_tex)   glDeleteTextures(1, &peel_.opaque_tex);
    if (peel_.depth_tex[0]) glDeleteTextures(2, peel_.depth_tex);
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
        // Sampled for a value, never for a shadow comparison: the peel test
        // wants the depth itself, and a comparison sampler would hand back a
        // 0/1 verdict against a reference this code never sets.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    };

    make_color(peel_.layer_tex);
    make_color(peel_.accum_tex);
    make_depth(peel_.opaque_tex);
    make_depth(peel_.depth_tex[0]);
    make_depth(peel_.depth_tex[1]);
    glBindTexture(GL_TEXTURE_2D, 0);

    int prev_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    bool ok = true;
    auto attach = [&](unsigned int& fbo, unsigned int color, unsigned int depth) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        if (color) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_TEXTURE_2D, color, 0);
        else       glDrawBuffer(GL_NONE), glReadBuffer(GL_NONE);
        if (depth) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                          GL_TEXTURE_2D, depth, 0);
        ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    };
    // The peel target's depth attachment is re-pointed at the other texture
    // every pass, so which one it starts with does not matter.
    attach(peel_.fbo,       peel_.layer_tex, peel_.depth_tex[0]);
    attach(peel_.accum_fbo, peel_.accum_tex, 0);
    attach(peel_.copy_fbo,  0,               peel_.opaque_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(prev_fbo));

    if (!ok) {
        // A machine that cannot give us these targets gets the whole-object
        // order instead of a black plot rect -- this frame and every later one.
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
        x0, y0, 0.0f, 0.0f,   x1, y0, 1.0f, 0.0f,   x1, y1, 1.0f, 1.0f,
        x0, y0, 0.0f, 0.0f,   x1, y1, 1.0f, 1.0f,   x0, y1, 0.0f, 1.0f,
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

// The whole of phase 2, when the targets allow it.
//
// The three per-kind draws are called in a fixed sequence here as well, and
// this time that is not a bug: within one pass the sequence decides nothing,
// because blending is off and the depth test keeps exactly one fragment per
// pixel. The order the layers are *composited* in is the order the passes
// produce them, which is the order the pixel's ray meets the geometry.
bool DataRenderer::peel_translucent3d(const RenderSnapshot3D& snap,
                                      const Projector3D& proj, const PlotRect& pr,
                                      float win_w, float win_h) {
    const int layers = peel_layer_count();
    if (layers <= 0) return false;

    // The plot rect in real framebuffer pixels, rounded outward exactly as
    // begin_pass() rounds its scissor -- the two have to name the same pixels
    // or the composite would land a pixel off the geometry it composites.
    const float s  = pixel_ratio_;
    const int ox = static_cast<int>(std::floor(pr.x * s));
    const int oy = static_cast<int>(std::floor((win_h - pr.y - pr.h) * s));
    const int px1 = static_cast<int>(std::ceil((pr.x + pr.w) * s));
    const int py1 = static_cast<int>(std::ceil((win_h - pr.y) * s));
    const int w = px1 - ox, h = py1 - oy;
    if (!ensure_peel_targets(w, h)) return false;

    int prev_fbo = 0, prev_vp[4] = { 0, 0, 0, 0 };
    float prev_clear[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear);
    const int fb_w = static_cast<int>(std::lround(win_w * s));
    const int fb_h = static_cast<int>(std::lround(win_h * s));

    // Every plane's little screen, rendered before the loop rather than
    // inside it -- see prepare_plane_rasters().
    prepare_plane_rasters(snap.planes, proj);

    // The opaque phase's depth, copied out of the main framebuffer. This is
    // the whole of how opaque geometry keeps occluding translucent geometry
    // while the opaque path stays exactly as post-step-7d left it.
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned int>(prev_fbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, peel_.copy_fbo);
    glBlitFramebuffer(ox, oy, ox + w, oy + h, 0, 0, w, h,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);

    // The accumulation starts as "nothing yet, and all the light still gets
    // through": the alpha channel is transmittance, not coverage.
    glBindFramebuffer(GL_FRAMEBUFFER, peel_.accum_fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Pass 0's "already peeled" depth: nothing has been, so a test of
    // `z <= 0` rejects nothing at all.
    glBindFramebuffer(GL_FRAMEBUFFER, peel_.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_TEXTURE_2D, peel_.depth_tex[1], 0);
    glClearDepth(0.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glClearDepth(1.0);

    int cur = 0;
    int peeled = 0;
    for (int n = 0; n < layers; ++n) {
        const int prev = 1 - cur;
        glBindFramebuffer(GL_FRAMEBUFFER, peel_.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D, peel_.depth_tex[cur], 0);
        // The one line that makes the geometry land in the peel target: the
        // clip matrix puts the plot rect somewhere inside the *window*, so
        // shifting the viewport by the rect's origin puts it at the target's.
        // Negative viewport origins are ordinary -- the viewport is an affine
        // map, not a region -- and this keeps clip_matrix() and the vertex
        // buffers identical to what an unpeeled frame uses.
        glViewport(-ox, -oy, fb_w, fb_h);
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, peel_.depth_tex[prev]);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, peel_.opaque_tex);
        glActiveTexture(GL_TEXTURE0);

        peel_.active = true;
        glBeginQuery(GL_ANY_SAMPLES_PASSED, peel_.query);
        draw_planes3d  (snap.planes,   proj, pr, win_w, win_h, ScenePass::Translucent);
        draw_bars3d    (snap.bars3d,   proj, pr, win_w, win_h, ScenePass::Translucent);
        draw_surfaces3d(snap.surfaces, proj, pr, win_w, win_h, ScenePass::Translucent);
        draw_surface_tri3d(snap.surface_tri, proj, pr, win_w, win_h,
                           ScenePass::Translucent);
        draw_scatter3d (snap.scatter3d, proj, pr, win_w, win_h, ScenePass::Translucent);
        draw_lines3d   (snap.lines3d,  proj, pr, win_w, win_h, ScenePass::Translucent);
        draw_errorbars3d(snap, proj, pr, win_w, win_h, ScenePass::Translucent);
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        peel_.active = false;

        // The early-out, and the reason a two-layer scene does not cost eight
        // passes. It is a synchronisation point -- the answer is not available
        // until the pass has run -- which is the trade Everitt's original
        // makes too: a stall per pass against passes that draw nothing.
        unsigned int any = 0;
        glGetQueryObjectuiv(peel_.query, GL_QUERY_RESULT, &any);
        if (!any) break;
        ++peeled;

        // This layer, composited *under* everything already accumulated. The
        // layer holds a premultiplied colour and the accumulation an alpha
        // that means transmittance, so:
        //     rgb += layer.rgb * accum.a        (GL_DST_ALPHA, GL_ONE)
        //     a   *= 1 - layer.a                (GL_ZERO, GL_ONE_MINUS_SRC_ALPHA)
        // which is the front-to-back operator, exact for any number of layers.
        glBindFramebuffer(GL_FRAMEBUFFER, peel_.accum_fbo);
        glViewport(0, 0, w, h);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        draw_peel_quad(peel_.layer_tex, false, -1.0f, -1.0f, 1.0f, 1.0f);

        cur = prev;
    }

    // Back to the scene, and one quad puts the whole accumulation over it.
    // `flip_alpha` turns the transmittance the accumulation carries into the
    // coverage a premultiplied `over` wants, so this is the ordinary
    // GL_ONE / GL_ONE_MINUS_SRC_ALPHA and not a third blend equation.
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
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glClearColor(prev_clear[0], prev_clear[1], prev_clear[2], prev_clear[3]);
    // Back to what begin_pass() establishes -- straight alpha on the colour
    // channels, accumulated coverage on alpha. Restoring the plain
    // glBlendFunc() here would leave the pass in a state begin_pass() never
    // sets, which is how a later draw in the same pass would silently get the
    // squared alpha back.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE,       GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    return true;
}

// -------------------------------------------------------------------------
// K translucent planes, composited exactly (step 7b)
// -------------------------------------------------------------------------
// Everything the shader needs about a plane is three box-space vectors and an
// axis. `plane_raster()` already fixes the quad's four corners and the texture
// coordinates on them; this is the same quad expressed as a corner and two
// edges, which is what a ray wants and a rasterizer does not.
//
// Assumes the caller has set the pass state: scissor to `pr`, depth test on
// with writes off, and premultiplied blending. It leaves them as it found
// them, so a group is composable with the group before it.
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
    int   axis[kMaxCompositePlanes]{};
    float alpha[kMaxCompositePlanes]{};

    for (int i = 0; i < n; ++i) {
        const PlaneRaster& q = rasters[group[static_cast<std::size_t>(i)]];

        // box = scale * (data - anchor) + offset, with the anchor at the p0
        // corner -- so `offset` *is* p0 in box space, exactly, and the two
        // edges are the scale applied to the quad's own spans. Taking the
        // anchor here rather than at the origin is the same bargain the vertex
        // buffers strike: float precision measured against the data's span
        // instead of against its distance from zero.
        float sc[3], off[3];
        proj.box_affine(q.p[0], sc, off);
        const Vec3 e_u{ q.p[1].x - q.p[0].x, q.p[1].y - q.p[0].y, q.p[1].z - q.p[0].z };
        const Vec3 e_v{ q.p[3].x - q.p[0].x, q.p[3].y - q.p[0].y, q.p[3].z - q.p[0].z };
        for (int k = 0; k < 3; ++k) {
            origin[i * 3 + k] = off[k];
            du[i * 3 + k] = sc[k] * static_cast<float>((&e_u.x)[k]);
            dv[i * 3 + k] = sc[k] * static_cast<float>((&e_v.x)[k]);
        }
        axis[i]  = q.normal_axis;
        alpha[i] = std::clamp(planes[group[static_cast<std::size_t>(i)]].opts.alpha, 0.0f, 1.0f);

        const CacheKey key{ axes_index_, static_cast<int>(group[static_cast<std::size_t>(i)]), -1 };
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
        glBindTexture(GL_TEXTURE_2D, plane_raster_cache_[key].tex);
    }

    const std::array<float, 16> clip = proj.clip_matrix(win_w, win_h);

    glUseProgram(plane_comp_program_);
    glUniformMatrix4fv(plane_comp_u_.clip, 1, GL_FALSE, clip.data());
    glUniform1i (plane_comp_u_.count, n);
    glUniform3fv(plane_comp_u_.origin, n, origin);
    glUniform3fv(plane_comp_u_.du,     n, du);
    glUniform3fv(plane_comp_u_.dv,     n, dv);
    glUniform1iv(plane_comp_u_.axis,   n, axis);
    glUniform1fv(plane_comp_u_.alpha,  n, alpha);

    // The plot rect, with each corner's ray. Six vertices is the whole of the
    // geometry: the planes' own outlines are the shader's in-bounds test, not
    // anything the rasterizer is asked to find.
    const float cxp[4] = { pr.x, pr.x + pr.w, pr.x + pr.w, pr.x };
    const float cyp[4] = { pr.y, pr.y,        pr.y + pr.h, pr.y + pr.h };
    const int   tri[6] = { 0, 1, 2, 0, 2, 3 };
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

    // Far end first, one draw per slot. The draws are the sort: a fragment
    // sees its own samples in depth order because slot s is drawn before slot
    // s+1 everywhere, and blending is order-sensitive in exactly that way.
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
// The 2D call sequence, verbatim from render_frame.cpp's 2D branch, with the
// plane's own transform and a plot rect that is the whole raster. That the two
// lists are the same list is the point of the step: a plane's layering cannot
// drift from a 2D axes' layering, because there is only one description of it.
void DataRenderer::render_plane_raster(const PlaneSnapshot& pl, int plane_index,
                                       const PlaneRaster& raster,
                                       PlaneRasterCache& c) {
    const int ss = static_cast<int>(std::lround(pixel_ratio_ > 0.0f ? pixel_ratio_ : 1.0f));
    const int rw = std::max(1, raster.w * std::max(1, ss));
    const int rh = std::max(1, raster.h * std::max(1, ss));

    // Exact equality on the transform, not a tolerance, for the reason the 2D
    // caches use it: the transform is recomputed by the same deterministic
    // code from the same inputs every frame, so it is bit-identical whenever
    // the view has genuinely not moved.
    const bool same_tr = c.tr.xmin == raster.tr.xmin && c.tr.xmax == raster.tr.xmax
                      && c.tr.ymin == raster.tr.ymin && c.tr.ymax == raster.tr.ymax
                      && c.tr.pw   == raster.tr.pw   && c.tr.ph   == raster.tr.ph;
    const bool stale = !c.valid || c.w != rw || c.h != rh || !same_tr
                    || data_generation_ == 0 || c.data_generation != data_generation_;
    if (!stale) return;

    if (c.w != rw || c.h != rh || !c.tex) {
        if (!c.tex) glGenTextures(1, &c.tex);
        glBindTexture(GL_TEXTURE_2D, c.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        // GL_LINEAR on the *composed* raster, where the heatmap inside it
        // keeps GL_NEAREST (see draw_heatmap). The two filtering decisions
        // fought over one texture before this step: nearest kept a heatmap's
        // cell edges crisp and left every line and marker aliased. They are
        // now separate textures and each takes the filter it wants.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        c.w = rw; c.h = rh;
    }
    // **Read what to restore before binding anything**, or the "previous"
    // framebuffer is this plane's own and every draw for the rest of the frame
    // lands in the last plane's raster instead of on the screen.
    int prev_fbo = 0, prev_vp[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    const GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);

    if (!c.fbo) glGenFramebuffers(1, &c.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, c.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, c.tex, 0);
    // **No depth attachment, and that is the whole idea.** A plane's contents
    // are a painter's stack in a fixed order, exactly as a 2D axes' are, so
    // there is nothing here for a depth buffer to decide.
    glViewport(0, 0, rw, rh);
    glDisable(GL_SCISSOR_TEST);

    // Transparent black, so the plane's background carries zero alpha whatever
    // is drawn on it -- the property that lets the quad composite as a decal
    // rather than as an opaque card with a picture on it.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // **Premultiplied accumulation, straight-alpha sources.** The 2D shaders
    // emit straight alpha, so the colour channels take the ordinary src-alpha
    // blend -- but the *alpha* channel must accumulate coverage rather than be
    // overwritten, or a partly covered texel comes out with the last
    // primitive's alpha and composites far too faintly onto the scene. This is
    // the trap begin_pass()'s own comment records from the other direction,
    // and it only shows on a pixel that is neither empty nor fully covered.
    const AllPlotData all = pl.sheet.all();
    plane_index_ = plane_index;
    draw_heatmap   (all.heatmaps,  raster.tr, { 0.0f, 0.0f, raster.tr.pw, raster.tr.ph });
    draw_bars      (all.bars,      raster.tr, { 0.0f, 0.0f, raster.tr.pw, raster.tr.ph });
    draw_lines     (all.lines,     raster.tr, { 0.0f, 0.0f, raster.tr.pw, raster.tr.ph });
    draw_error_bars(all,           raster.tr, { 0.0f, 0.0f, raster.tr.pw, raster.tr.ph });
    draw_scatter   (all.scatters,  raster.tr, { 0.0f, 0.0f, raster.tr.pw, raster.tr.ph });
    draw_scatter_z (all.scatter_z, raster.tr, { 0.0f, 0.0f, raster.tr.pw, raster.tr.ph });
    plane_index_ = -1;

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned>(prev_fbo));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    if (prev_scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

    c.tr = raster.tr;
    c.data_generation = data_generation_;
    c.valid = true;
}
// -------------------------------------------------------------------------
// 3D bars
// -------------------------------------------------------------------------
// The only draw here that is a *scene* rather than a stack of layers, which is
// the whole of what makes it different: the fixed back-to-front order every
// 2D kind relies on has no meaning for boxes seen from an arbitrary angle, so
// this one turns the depth test on and lets the buffer decide. Everything else
// -- the scissor, the blend state, the cache shape -- is the same bargain the
// other kinds strike.
//
// Two passes, and which one a plot lands in is the whole of what `alpha` costs.
// An opaque plot is resolved by the depth buffer: one unordered glDrawArrays,
// whatever the camera does. A translucent one has to be *ordered* for the
// camera it is being seen from -- every face of every bar, back to front, with
// depth writes off so the ones behind show through -- which means an index
// buffer rebuilt every frame. Opaque first, so translucent geometry tests
// against solid geometry and not the other way round.
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

    // This pass's plots, far to near when they are translucent -- by the
    // heuristic plan_bars3d() uses for the same question. An explicit order
    // from draw_scene3d() has already done both, against the whole scene
    // rather than against this kind alone.
    std::vector<std::size_t> plot_order;
    if (explicit_order) {
        plot_order = *explicit_order;
    } else {
        plot_order.reserve(bars.size());
        for (std::size_t i = 0; i < bars.size(); ++i)
            if (bar3d_translucent(bars[i]) == want_translucent) plot_order.push_back(i);
        // Under peeling there is nothing to sort: which plot is in front is
        // decided per pixel, by the depth test, and a plot order could only
        // agree with that answer or contradict it.
        if (want_translucent && !peel_.active)
            std::stable_sort(plot_order.begin(), plot_order.end(),
                             [&](std::size_t a, std::size_t c) {
                                 return bar3d_plot_distance(bars[a], proj) >
                                        bar3d_plot_distance(bars[c], proj);
                             });
    }
    // No early return on an empty list: begin_pass() has already run, so the
    // loop simply does nothing and the cleanup below still puts the state back.

    std::vector<std::size_t> order;
    std::vector<std::uint32_t> indices;
    for (const std::size_t bi : plot_order) {
        const Bar3DPlot& b = bars[bi];
        const std::size_t n = b.count();
        if (n == 0 || b.heights.size() < n) continue;
        const bool translucent = bar3d_translucent(b);

        const CacheKey key{ axes_index_, -1, static_cast<int>(bi) };
        Bar3DCache& c = bar3d_cache_[key];

        // Everything baked into the buffer is in the key; the camera and the
        // limits are not, because both are uniforms. So an orbit -- which
        // republishes the snapshot every frame -- costs one draw call and no
        // upload, which is the point of holding the vertices in data space.
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
            // The anchor is the middle of this plot's own data, so the floats
            // in the buffer are offsets within the data's span rather than
            // absolute coordinates -- the same reason the 2D scatter cache
            // carries one.
            Vec3 anchor{};
            {
                const Axis3Map m = axis_map(b.orient);
                double lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
                lo[m.u] = hi[m.u] = b.u.size() ? b.u[0] : 0.0;
                lo[m.v] = hi[m.v] = b.v.size() ? b.v[0] : 0.0;
                lo[m.h] = hi[m.h] = b.h_lo(0);
                for (double u : b.u) { lo[m.u] = std::min(lo[m.u], u); hi[m.u] = std::max(hi[m.u], u); }
                for (double v : b.v) { lo[m.v] = std::min(lo[m.v], v); hi[m.v] = std::max(hi[m.v], v); }
                for (std::size_t k = 0; k < n; ++k) {
                    lo[m.h] = std::min(lo[m.h], b.h_lo(k));
                    hi[m.h] = std::max(hi[m.h], b.h_hi(k));
                }
                anchor = { (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5 };
            }

            std::vector<float> verts;
            verts.reserve(n * 36 * 4);
            std::vector<float> edges;
            if (b.opts.edges) edges.reserve(n * 12 * 6);

            // Which corner pairs of a face are box edges: a face contributes
            // its own four sides, and each box edge is shared by two faces, so
            // taking only the two sides whose axis index is the lower of the
            // face's two spanned axes emits each of the twelve exactly once.
            Bar3DFace faces[6];
            for (std::size_t k = 0; k < n; ++k) {
                bar3d_faces(b, k, proj.transform(), faces);
                for (const Bar3DFace& f : faces) {
                    const int tri[6] = { 0, 1, 2, 0, 2, 3 };
                    for (int t : tri) {
                        const Vec3 p = f.p[t];
                        verts.push_back(static_cast<float>(p.x - anchor.x));
                        verts.push_back(static_cast<float>(p.y - anchor.y));
                        verts.push_back(static_cast<float>(p.z - anchor.z));
                        verts.push_back(f.shade);
                    }
                }
                if (b.opts.edges) {
                    // From bar3d.h, so the SVG path's translucent outlines are
                    // the same twelve segments in the same order rather than a
                    // second enumeration that could differ from this one by an
                    // edge nobody would notice was missing.
                    Vec3 seg[12][2];
                    bar3d_edges(faces, seg);
                    for (const auto& e : seg)
                        for (const Vec3& p : e) {
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
            c.anchor  = anchor;
            c.u_width = b.u_width;
            c.v_width = b.v_width;
            c.bottom  = b.opts.bottom;
            c.shading = b.opts.shading;
            c.edges   = b.opts.edges;
            c.axis_signs = signs;
        }

        float box_scale[3], box_offset[3];
        proj.box_affine(c.anchor, box_scale, box_offset);

        const Bar3DUniforms& bu = peel_.active ? peel_bar3d_u_ : bar3d_u_;
        glUseProgram(peel_.active ? peel_bar3d_program_ : bar3d_program_);
        glUniformMatrix4fv(bu.clip, 1, GL_FALSE, clip.data());
        glUniform3fv(bu.box_scale, 1, box_scale);
        glUniform3fv(bu.box_offset, 1, box_offset);
        const float col[4] = { b.opts.color.r, b.opts.color.g, b.opts.color.b,
                               bar3d_alpha(b) };
        glUniform4fv(bu.color, 1, col);

        glBindVertexArray(bar3d_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));

        if (!translucent || peel_.active) {
            // Under peeling a translucent grid draws exactly as an opaque one
            // does: unordered, depth-writing, all thirty-six vertices of every
            // bar. Nothing is dropped and nothing is sequenced -- the six
            // faces of a bar are six samples along the pixel's ray and the
            // peel delivers them in the order the ray meets them, which is
            // what bar3d_draw_order() and bar3d_face_order() were both
            // approximating. They stay for the SVG writer, which has no
            // depth test to do it with.
            glDrawArrays(GL_TRIANGLES, 0, c.verts);
        } else {
            // The order, rebuilt for this camera: bars back to front by the
            // grid, and within each bar the three faces turned away before the
            // three turned toward -- which is exact for a convex solid, since
            // neither group overlaps itself. Both orders come from bar3d.h, so
            // this is the same sequence the SVG painter emits rather than a
            // second opinion about it.
            bar3d_draw_order(b, proj, order);
            indices.clear();
            indices.reserve(n * 36);
            Bar3DFace faces[6];
            for (const std::size_t k : order) {
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
            // Blended, not resolved: a translucent face must not write depth,
            // or the bars behind it -- and their outlines -- would be rejected
            // by the very geometry they are meant to show through.
            glDepthMask(GL_FALSE);
            glDrawElements(GL_TRIANGLES, c.indices, GL_UNSIGNED_INT, nullptr);
        }

        if (b.opts.edges && c.edge_segs > 0) {
            // A width given in pixels, converted once to a length in the box
            // at the box's own centre -- so it is that pixel width there and
            // thins with distance elsewhere, which is what "geometry in the
            // scene" means (spec_3d.md §4). The polygon offset is the ordinary
            // fix for a ribbon coplanar with the face it outlines.
            const double half = 0.5 * b.opts.edge_linewidth *
                                proj.box_units_per_pixel(Vec3{ 0.0, 0.0, 0.0 });
            const Vec3 eye = proj.has_eye_point() ? proj.eye_point() : proj.eye_dir();

            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -2.0f);
            const Bar3DEdgeUniforms& eu = peel_.active ? peel_edge_u_ : bar3d_edge_u_;
            glUseProgram(peel_.active ? peel_edge_program_ : bar3d_edge_program_);
            glUniformMatrix4fv(eu.clip, 1, GL_FALSE, clip.data());
            glUniform3fv(eu.box_scale, 1, box_scale);
            glUniform3fv(eu.box_offset, 1, box_offset);
            // The outline has its own opacity (Bar3DOptions::edge_alpha), so a
            // translucent bar keeps a readable wireframe. What the face's alpha
            // does decide is how many edges show: with no depth written by the
            // faces, nothing occludes the six a solid bar hides behind itself,
            // so all twelve draw -- which is what the SVG path emits as well.
            const float ec[4] = { b.opts.edgecolor.r, b.opts.edgecolor.g,
                                  b.opts.edgecolor.b, bar3d_edge_alpha(b) };
            glUniform4fv(eu.color, 1, ec);
            const float eyef[3] = { static_cast<float>(eye.x), static_cast<float>(eye.y),
                                    static_cast<float>(eye.z) };
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
                                  reinterpret_cast<void*>(3 * sizeof(float)));
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
// 3D surfaces (step 7c)
// -------------------------------------------------------------------------
// The bar path's shape with two differences, both of which come from a sheet
// not being a solid. There are no hidden faces to drop -- a cell is drawn from
// whichever side is facing -- and the colour is per cell rather than per plot,
// so it rides on the vertex instead of arriving as a uniform.
//
// An *opaque* surface is one unordered glDrawArrays resolved by the depth
// buffer, whatever the camera does. That is why 7c needs nothing at all from
// 7a or 7b: the depth buffer was always exact for opaque geometry, and it is
// only translucency that has ever needed an order.
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

    // This pass's plots, far to near when translucent -- the same rule
    // draw_bars3d() follows, and by the same whole-object heuristic since two
    // surfaces have no separating plane. An explicit order from draw_scene3d()
    // has already applied it against the whole scene.
    std::vector<std::size_t> plot_order;
    if (explicit_order) {
        plot_order = *explicit_order;
    } else {
        plot_order.reserve(surfaces.size());
        for (std::size_t i = 0; i < surfaces.size(); ++i)
            if (surface_translucent(surfaces[i]) == want_translucent) plot_order.push_back(i);
        // Nothing to sort under peeling -- see draw_bars3d() for why.
        if (want_translucent && !peel_.active)
            std::stable_sort(plot_order.begin(), plot_order.end(),
                             [&](std::size_t a, std::size_t b) {
                                 return surface_plot_distance(surfaces[a], proj) >
                                        surface_plot_distance(surfaces[b], proj);
                             });
    }

    std::vector<std::size_t> order;
    std::vector<std::uint32_t> indices;
    for (const std::size_t si : plot_order) {
        const SurfacePlot& s = surfaces[si];
        if (s.cell_count() == 0 || s.heights.size() < s.count()) continue;
        const bool translucent = surface_translucent(s);
        const std::size_t nc = s.cell_cols();

        const CacheKey key{ axes_index_, -1, static_cast<int>(si) };
        SurfaceCache& c = surface_cache_[key];

        double vmin = 0.0, vmax = 1.0;
        surface_value_range(s, vmin, vmax);

        // Everything baked into the buffer is in the key; the camera and the
        // limits are not, because both are uniforms. The colour source is in
        // it because the colour is baked per vertex here, which is the one
        // thing a surface bakes that a bar grid does not.
        const int signs = (tf.xmax < tf.xmin ? 1 : 0)
                        | (tf.ymax < tf.ymin ? 2 : 0)
                        | (tf.zmax < tf.zmin ? 4 : 0);
        const bool stale = c.vbo == 0 || data_generation_ == 0 ||
                           c.data_generation != data_generation_ ||
                           c.shading != s.opts.shading ||
                           c.alpha != s.opts.alpha ||
                           c.color.r != s.opts.color.r ||
                           c.color.g != s.opts.color.g ||
                           c.color.b != s.opts.color.b ||
                           c.color.a != s.opts.color.a ||
                           c.colormap != s.opts.colormap ||
                           c.cmap != s.opts.cmap ||
                           c.vmin != vmin || c.vmax != vmax ||
                           c.axis_signs != signs ||
                           c.edges != s.opts.edges;

        if (stale) {
            // The anchor is the middle of this plot's own data, so the floats
            // in the buffer measure offsets within the data's span rather than
            // its distance from the origin -- the bargain every buffer here
            // strikes.
            const Axis3Map m = axis_map(s.orient);
            double lo[3], hi[3];
            for (int a = 0; a < 3; ++a) { lo[a] = 0.0; hi[a] = 0.0; }
            lo[m.u] = hi[m.u] = s.u.size() ? s.u[0] : 0.0;
            lo[m.v] = hi[m.v] = s.v.size() ? s.v[0] : 0.0;
            lo[m.h] = hi[m.h] = s.height_at(0);
            for (double u : s.u) { lo[m.u] = std::min(lo[m.u], u); hi[m.u] = std::max(hi[m.u], u); }
            for (double v : s.v) { lo[m.v] = std::min(lo[m.v], v); hi[m.v] = std::max(hi[m.v], v); }
            for (std::size_t k = 0; k < s.count(); ++k) {
                lo[m.h] = std::min(lo[m.h], s.height_at(k));
                hi[m.h] = std::max(hi[m.h], s.height_at(k));
            }
            const Vec3 anchor{ (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5,
                               (lo[2] + hi[2]) * 0.5 };

            std::vector<float> verts;
            std::vector<float> edges;
            verts.reserve(s.cell_count() * 6 * 7);

            SurfaceCell cell;
            for (std::size_t k = 0; k < s.cell_count(); ++k) {
                surface_cell(s, k / nc, k % nc, tf, cell);
                const Color col = surface_cell_color(s, cell, vmin, vmax);

                // Two triangles, split on the 0-2 diagonal. A cell's four
                // corners are generally *not* coplanar -- four independent
                // samples -- so a diagonal has to be chosen, and it is chosen
                // here in data space rather than per frame from the camera,
                // or the buffer would stop being camera-independent and an
                // orbit would re-upload the whole surface.
                const int tri[6] = { 0, 1, 2, 0, 2, 3 };
                for (const int t : tri) {
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
                    for (const auto& e : seg)
                        for (const Vec3& p : e) {
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
            c.anchor     = anchor;
            c.shading    = s.opts.shading;
            c.alpha      = s.opts.alpha;
            c.color      = s.opts.color;
            c.colormap   = s.opts.colormap;
            c.cmap       = s.opts.cmap;
            c.vmin       = vmin;
            c.vmax       = vmax;
            c.edges      = s.opts.edges;
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
                              reinterpret_cast<void*>(3 * sizeof(float)));

        if (!translucent || peel_.active) {
            // Under peeling a translucent sheet draws as an opaque one does,
            // for the reason draw_bars3d() records: surface_draw_order() is an
            // answer to a question the depth test is about to answer per
            // pixel, and it stays for the path that has no depth test.
            glDepthMask(GL_TRUE);
            glDrawArrays(GL_TRIANGLES, 0, c.verts);
        } else {
            // Back to front by the grid, which is exact within one surface for
            // the reason surface_draw_order() records. Camera-dependent, so it
            // is the one thing rebuilt every frame -- the cost of alpha, same
            // as a translucent bar grid pays.
            surface_draw_order(s, proj, order);
            indices.clear();
            indices.reserve(order.size() * 6);
            for (const std::size_t k : order)
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
            // The wireframe, through the *bar* edge program: a width in pixels
            // at the box centre, expanded to a world-space ribbon, thinning
            // with distance. One program for both kinds because it is one
            // question -- "a stroke that is geometry in the scene" -- and
            // spec_3d.md §4 has already answered it once.
            const double half = 0.5 * s.opts.edge_linewidth *
                                proj.box_units_per_pixel(Vec3{ 0.0, 0.0, 0.0 });
            const Vec3 eye = proj.has_eye_point() ? proj.eye_point() : proj.eye_dir();

            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -2.0f);
            const Bar3DEdgeUniforms& eu = peel_.active ? peel_edge_u_ : bar3d_edge_u_;
            glUseProgram(peel_.active ? peel_edge_program_ : bar3d_edge_program_);
            glUniformMatrix4fv(eu.clip, 1, GL_FALSE, clip.data());
            glUniform3fv(eu.box_scale, 1, box_scale);
            glUniform3fv(eu.box_offset, 1, box_offset);
            const float ec[4] = { s.opts.edgecolor.r, s.opts.edgecolor.g,
                                  s.opts.edgecolor.b, surface_edge_alpha(s) };
            glUniform4fv(eu.color, 1, ec);
            const float eyef[3] = { static_cast<float>(eye.x), static_cast<float>(eye.y),
                                    static_cast<float>(eye.z) };
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
                                  reinterpret_cast<void*>(3 * sizeof(float)));
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
// 3D meshes (step 14)
// -------------------------------------------------------------------------
// draw_surfaces3d()'s shape with two differences, and both come from a mesh
// having vertices where a grid has samples on a lattice:
//
//   - **the colour is resolved per fragment.** A cell has one value, so a grid
//     surface can bake the finished RGBA onto all six of its vertices; a face
//     has three, so what is baked is the *value* and the colormap is sampled
//     in k_surface_tri_frag. Looking up at the corners and interpolating would
//     be a barycentric RGB blend, which leaves the colormap -- the picture
//     k_line3d_vert rejects for a segment, and worse here because a triangle's
//     blend covers an area rather than a line.
//   - **the translucent unpeeled order is a heuristic.** A grid's cells are
//     separated by axis-aligned planes and a mesh's faces are not, so
//     surface_tri_draw_order() is an approximation where surface_draw_order()
//     is exact. Peeling, which is the default path, consults neither.
//
// An *opaque* mesh is one unordered glDrawArrays resolved by the depth buffer,
// whatever the camera does -- which is the same thing that makes an opaque
// grid surface free, and the reason self-occlusion only ever costs anything
// once alpha is involved.
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

    // This pass's plots, far to near when translucent -- the whole-object
    // heuristic every kind here falls back to. An explicit order from
    // draw_scene3d() has already applied it against the whole scene.
    std::vector<std::size_t> plot_order;
    if (explicit_order) {
        plot_order = *explicit_order;
    } else {
        plot_order.reserve(meshes.size());
        for (std::size_t i = 0; i < meshes.size(); ++i)
            if (surface_tri_translucent(meshes[i]) == want_translucent) plot_order.push_back(i);
        // Nothing to sort under peeling -- see draw_bars3d() for why.
        if (want_translucent && !peel_.active)
            std::stable_sort(plot_order.begin(), plot_order.end(),
                             [&](std::size_t a, std::size_t b) {
                                 return surface_tri_plot_distance(meshes[a], proj) >
                                        surface_tri_plot_distance(meshes[b], proj);
                             });
    }

    std::vector<std::size_t> order;
    std::vector<std::uint32_t> indices;
    for (const std::size_t mi : plot_order) {
        const SurfaceTriPlot& s = meshes[mi];
        if (s.face_count() == 0 || s.count() == 0) continue;
        const bool translucent = surface_tri_translucent(s);

        const CacheKey key{ axes_index_, -1, static_cast<int>(mi) };
        SurfaceTriCache& c = surface_tri_cache_[key];

        double vmin = 0.0, vmax = 1.0;
        surface_tri_value_range(s, vmin, vmax);

        // Everything baked into the buffer is in the key; the camera and the
        // limits are not, because both are uniforms. The axis signs are in it
        // because a reversed limit mirrors an axis and so changes how a face
        // is tilted and lit -- the shade is baked, where the colour lookup is
        // not.
        const int signs = (tf.xmax < tf.xmin ? 1 : 0)
                        | (tf.ymax < tf.ymin ? 2 : 0)
                        | (tf.zmax < tf.zmin ? 4 : 0);
        const bool stale = c.vbo == 0 || data_generation_ == 0 ||
                           c.data_generation != data_generation_ ||
                           c.shading != s.opts.shading ||
                           c.alpha != s.opts.alpha ||
                           c.color.r != s.opts.color.r ||
                           c.color.g != s.opts.color.g ||
                           c.color.b != s.opts.color.b ||
                           c.color.a != s.opts.color.a ||
                           c.colormapped != s.colormapped() ||
                           c.cmap != s.opts.cmap ||
                           c.vmin != vmin || c.vmax != vmax ||
                           c.axis_signs != signs ||
                           c.edges != s.opts.edges;

        if (stale) {
            // The anchor is the middle of this plot's own data, so the floats
            // in the buffer measure offsets within the data's span rather than
            // its distance from the origin -- the bargain every buffer here
            // strikes.
            double lo[3], hi[3];
            {
                const Vec3 p0 = s.vertex(0);
                const double c0[3] = { p0.x, p0.y, p0.z };
                for (int a = 0; a < 3; ++a) { lo[a] = c0[a]; hi[a] = c0[a]; }
                for (std::size_t i = 1; i < s.count(); ++i) {
                    const Vec3 p = s.vertex(i);
                    const double cc[3] = { p.x, p.y, p.z };
                    for (int a = 0; a < 3; ++a) {
                        lo[a] = std::min(lo[a], cc[a]);
                        hi[a] = std::max(hi[a], cc[a]);
                    }
                }
            }
            const Vec3 anchor{ (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5,
                               (lo[2] + hi[2]) * 0.5 };

            std::vector<float> verts;
            std::vector<float> edges;
            verts.reserve(s.face_count() * 3 * 9);

            SurfaceTriFace face;
            for (std::size_t f = 0; f < s.face_count(); ++f) {
                surface_tri_face(s, f, tf, face);
                // The mesh expands to 3M independent vertices, which is why
                // SurfaceTriPlot::tri is never uploaded: the shade is per face
                // and the vertices meeting at a corner belong to faces that
                // are lit differently, so there is nothing to share.
                for (int k = 0; k < 3; ++k) {
                    const Color col = surface_tri_vertex_color(s, face.vert[k], vmin, vmax);
                    const double t = s.colormapped()
                        ? surface_tri_vertex_t(s, face.vert[k], vmin, vmax) : 0.0;
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
                    for (const auto& e : seg)
                        for (const Vec3& p : e) {
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
            c.anchor      = anchor;
            c.shading     = s.opts.shading;
            c.alpha       = s.opts.alpha;
            c.color       = s.opts.color;
            c.colormapped = s.colormapped();
            c.cmap        = s.opts.cmap;
            c.vmin        = vmin;
            c.vmax        = vmax;
            c.edges       = s.opts.edges;
            c.axis_signs  = signs;
        }
        if (c.verts == 0) continue;

        float box_scale[3], box_offset[3];
        proj.box_affine(c.anchor, box_scale, box_offset);

        const SurfaceTriUniforms& su = peel_.active ? peel_surface_tri_u_ : surface_tri_u_;
        glUseProgram(peel_.active ? peel_surface_tri_program_ : surface_tri_program_);
        glUniformMatrix4fv(su.clip, 1, GL_FALSE, clip.data());
        glUniform3fv(su.box_scale, 1, box_scale);
        glUniform3fv(su.box_offset, 1, box_offset);
        glUniform1i(su.colormapped, s.colormapped() ? 1 : 0);

        if (s.colormapped()) {
            // The map as a 256x1 texture, uploaded only when a mesh asks for a
            // different one -- line3d's arrangement, and shared with nothing
            // so that two kinds cannot invalidate each other's cache.
            glActiveTexture(GL_TEXTURE0);
            if (!surface_tri_cmap_tex_) {
                glGenTextures(1, &surface_tri_cmap_tex_);
                glBindTexture(GL_TEXTURE_2D, surface_tri_cmap_tex_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                surface_tri_cmap_in_tex_ = -1;
            }
            if (surface_tri_cmap_in_tex_ != static_cast<int>(s.opts.cmap)) {
                glBindTexture(GL_TEXTURE_2D, surface_tri_cmap_tex_);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, colormaps::get(s.opts.cmap));
                surface_tri_cmap_in_tex_ = static_cast<int>(s.opts.cmap);
            }
            glBindTexture(GL_TEXTURE_2D, surface_tri_cmap_tex_);
            glUniform1i(su.cmap, 0);
        }

        glBindVertexArray(surface_tri_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), nullptr);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                              reinterpret_cast<void*>(7 * sizeof(float)));
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                              reinterpret_cast<void*>(8 * sizeof(float)));

        if (!translucent || peel_.active) {
            // Under peeling a translucent mesh draws as an opaque one does,
            // for the reason draw_bars3d() records: the face order is an
            // answer to a question the depth test is about to answer per
            // pixel. It stays for the path that has no depth test -- and here
            // that path is the weakest of the five, since a mesh can occlude
            // itself and no order of whole faces resolves that.
            glDepthMask(GL_TRUE);
            glDrawArrays(GL_TRIANGLES, 0, c.verts);
        } else {
            surface_tri_draw_order(s, proj, order);
            indices.clear();
            indices.reserve(order.size() * 3);
            for (const std::size_t f : order)
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
            // The wireframe, through the *bar* edge program -- a width in
            // pixels at the box centre expanded to a world-space ribbon,
            // thinning with distance. One program for three kinds, because it
            // is one question that spec_3d.md §4 has already answered once.
            const double half = 0.5 * s.opts.edge_linewidth *
                                proj.box_units_per_pixel(Vec3{ 0.0, 0.0, 0.0 });
            const Vec3 eye = proj.has_eye_point() ? proj.eye_point() : proj.eye_dir();

            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -2.0f);
            const Bar3DEdgeUniforms& eu = peel_.active ? peel_edge_u_ : bar3d_edge_u_;
            glUseProgram(peel_.active ? peel_edge_program_ : bar3d_edge_program_);
            glUniformMatrix4fv(eu.clip, 1, GL_FALSE, clip.data());
            glUniform3fv(eu.box_scale, 1, box_scale);
            glUniform3fv(eu.box_offset, 1, box_offset);
            const float ec[4] = { s.opts.edgecolor.r, s.opts.edgecolor.g,
                                  s.opts.edgecolor.b, surface_tri_edge_alpha(s) };
            glUniform4fv(eu.color, 1, ec);
            const float eyef[3] = { static_cast<float>(eye.x), static_cast<float>(eye.y),
                                    static_cast<float>(eye.z) };
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
                                  reinterpret_cast<void*>(3 * sizeof(float)));
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

    // The depth ramp, once per draw: two numbers off the box and the affine
    // form of Px3::depth, so the shader reconstructs exactly what the CPU
    // would have computed for the same point (see depth_affine()).
    float dmin = 0.0f, dmax = 1.0f;
    box_depth_range(proj, dmin, dmax);
    Vec3 ddir{}; float dbase = 0.0f;
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
    for (const std::size_t si : plot_order) {
        const Scatter3DPlot& s = points[si];
        if (s.count() == 0 || s.opts.marker == MarkerStyle::None) continue;
        const bool translucent = scatter3d_translucent(s);

        const CacheKey key{ axes_index_, -1, static_cast<int>(si) };
        Scatter3DCache& c = scatter3d_cache_[key];

        double vmin = 0.0, vmax = 1.0;
        scatter3d_value_range(s, vmin, vmax);

        const bool stale = c.vbo == 0 || data_generation_ == 0 ||
                           c.data_generation != data_generation_ ||
                           c.size != s.opts.size ||
                           c.alpha != s.opts.alpha ||
                           c.color.r != s.opts.color.r ||
                           c.color.g != s.opts.color.g ||
                           c.color.b != s.opts.color.b ||
                           c.color.a != s.opts.color.a ||
                           c.colormapped != s.colormapped() ||
                           c.cmap != s.opts.cmap ||
                           c.vmin != vmin || c.vmax != vmax;

        if (stale) {
            // The anchor is the cloud's own bounding-box centre, so the floats
            // measure offsets within the data's span rather than its distance
            // from the origin -- the bargain every buffer here strikes.
            double lo[3] = {  std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::max() };
            double hi[3] = { -std::numeric_limits<double>::max(),
                             -std::numeric_limits<double>::max(),
                             -std::numeric_limits<double>::max() };
            for (std::size_t i = 0; i < s.count(); ++i) {
                const double p[3] = { s.x[i], s.y[i], s.z[i] };
                for (int a = 0; a < 3; ++a) {
                    lo[a] = std::min(lo[a], p[a]);
                    hi[a] = std::max(hi[a], p[a]);
                }
            }
            const Vec3 anchor{ (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5,
                               (lo[2] + hi[2]) * 0.5 };

            std::vector<float> verts;
            verts.reserve(s.count() * 8);
            for (std::size_t i = 0; i < s.count(); ++i) {
                const Color col = scatter3d_point_color(s, i, vmin, vmax);
                verts.push_back(static_cast<float>(s.x[i] - anchor.x));
                verts.push_back(static_cast<float>(s.y[i] - anchor.y));
                verts.push_back(static_cast<float>(s.z[i] - anchor.z));
                verts.push_back(s.opts.size);
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
            // Kept CPU-side only where the sort below will need it.
            c.host = scatter3d_translucent(s) ? std::move(verts) : std::vector<float>{};

            c.data_generation = data_generation_;
            c.anchor      = anchor;
            c.size        = s.opts.size;
            c.alpha       = s.opts.alpha;
            c.color       = s.opts.color;
            c.colormapped = s.colormapped();
            c.cmap        = s.opts.cmap;
            c.vmin        = vmin;
            c.vmax        = vmax;
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

        glBindVertexArray(scatter3d_vao_);

        // Opaque, or peeled: the instance buffer as it is baked. A peel
        // resolves the order per fragment, so a sorted copy could only agree
        // with it or contradict it -- draw_bars3d()'s rule, and the same one.
        unsigned int inst = c.vbo;
        if (translucent && !peel_.active) {
            // Back to front, and *exactly* so: a marker is a flat billboard,
            // so no two of them interpenetrate and "which is in front" really
            // is one number per point. Unlike a bar grid or a surface, this
            // fallback is not an approximation.
            //
            // A reordered copy of the instance data rather than an index
            // buffer, because the sort permutes instances and an instanced
            // draw has no index path into its attributes.
            scatter3d_draw_order(s, proj, order);
            sorted.clear();
            sorted.reserve(order.size() * 8);
            for (const std::size_t k : order) {
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
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<void*>(4 * sizeof(float)));
        glVertexAttribDivisor(3, 1);

        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, c.points);
        glDepthMask(GL_TRUE);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    end_pass();
}

// A path, as one mitered world-space ribbon per segment (v1.0 step 13).
// draw_scatter3d()'s shape throughout -- the same cache/stale/anchor bargain,
// the same reordered-copy fallback -- with the instance being a segment rather
// than a point, and carrying its two neighbours so the join can be mitered.
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
    Vec3 ddir{}; float dbase = 0.0f;
    depth_affine(proj, ddir, dbase);
    const float dspan = (dmax - dmin) != 0.0f ? 1.0f / (dmax - dmin) : 0.0f;

    // The ribbon's half-width, in box units and measured once at the box
    // centre -- see line3d_half_width(). A uniform, so an orbit re-uploads
    // nothing, and the same number the SVG path will measure.
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

    // Twenty-two floats per instance: prev, a, b, next, a colour at each end,
    // and the two ends' normalized colormap values -- which are what actually
    // ramps for a colormapped path (see k_line3d_vert).
    constexpr std::size_t kStride = 22;

    std::vector<std::size_t> order;
    std::vector<float> sorted;
    for (const std::size_t li : plot_order) {
        const Line3DPlot& l = lines[li];
        if (l.segment_count() == 0) continue;
        const bool translucent = line3d_translucent(l);

        const CacheKey key{ axes_index_, -1, static_cast<int>(li) };
        Line3DCache& c = line3d_cache_[key];

        double vmin = 0.0, vmax = 1.0;
        line3d_value_range(l, vmin, vmax);

        const bool stale = c.vbo == 0 || data_generation_ == 0 ||
                           c.data_generation != data_generation_ ||
                           c.alpha != l.opts.alpha ||
                           c.color.r != l.opts.color.r ||
                           c.color.g != l.opts.color.g ||
                           c.color.b != l.opts.color.b ||
                           c.color.a != l.opts.color.a ||
                           c.colormapped != l.colormapped() ||
                           c.loop != l.opts.loop ||
                           c.cmap != l.opts.cmap ||
                           c.vmin != vmin || c.vmax != vmax;

        if (stale) {
            // The anchor is the path's own bounding-box centre, so the floats
            // measure offsets within the data's span rather than its distance
            // from the origin -- the bargain every buffer here strikes.
            double lo[3] = {  std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::max() };
            double hi[3] = { -std::numeric_limits<double>::max(),
                             -std::numeric_limits<double>::max(),
                             -std::numeric_limits<double>::max() };
            for (std::size_t i = 0; i < l.count(); ++i) {
                const double p[3] = { l.x[i], l.y[i], l.z[i] };
                for (int a = 0; a < 3; ++a) {
                    lo[a] = std::min(lo[a], p[a]);
                    hi[a] = std::max(hi[a], p[a]);
                }
            }
            const Vec3 anchor{ (lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5,
                               (lo[2] + hi[2]) * 0.5 };

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
                // The neighbours the miter needs. **An absent neighbour is
                // written as the end point itself**, which makes the joint
                // degenerate in the shader and leaves that end square -- so a
                // closed path and an open one differ only in this lookup, and
                // the shader has no case to get wrong. A looped path always
                // has both, which is what makes its closing corner mitered
                // like every other.
                const std::size_t prev = (a == 0) ? (l.opts.loop ? n - 1 : a) : a - 1;
                const std::size_t next = (b + 1 >= n) ? (l.opts.loop ? (b + 1) % n : b)
                                                      : b + 1;
                put(verts, prev);
                put(verts, a);
                put(verts, b);
                put(verts, next);
                const Color ca = line3d_point_color(l, a, vmin, vmax);
                const Color cb = line3d_point_color(l, b, vmin, vmax);
                verts.push_back(ca.r); verts.push_back(ca.g);
                verts.push_back(ca.b); verts.push_back(ca.a);
                verts.push_back(cb.r); verts.push_back(cb.g);
                verts.push_back(cb.b); verts.push_back(cb.a);
                // The same normalization line3d_point_color() applies before
                // its own lookup, so the shader's texture fetch at either end
                // lands on the entry the CPU would have picked. Zero for a
                // flat series, which the shader never reads.
                const double span = vmax - vmin;
                auto norm = [&](std::size_t i) {
                    if (!l.colormapped() || span == 0.0) return 0.0f;
                    return static_cast<float>(
                        std::clamp((l.color_at(i) - vmin) / span, 0.0, 1.0));
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
            c.anchor      = anchor;
            c.alpha       = l.opts.alpha;
            c.color       = l.opts.color;
            c.colormapped = l.colormapped();
            c.loop        = l.opts.loop;
            c.cmap        = l.opts.cmap;
            c.vmin        = vmin;
            c.vmax        = vmax;
        }
        if (c.segs == 0) continue;

        float box_scale[3], box_offset[3];
        proj.box_affine(c.anchor, box_scale, box_offset);

        const Line3DUniforms& lu = peel_.active ? peel_line3d_u_ : line3d_u_;
        glUseProgram(peel_.active ? peel_line3d_program_ : line3d_program_);
        glUniformMatrix4fv(lu.clip, 1, GL_FALSE, clip.data());
        glUniform3fv(lu.box_scale, 1, box_scale);
        glUniform3fv(lu.box_offset, 1, box_offset);
        const float eyef[3] = { static_cast<float>(eye.x), static_cast<float>(eye.y),
                                static_cast<float>(eye.z) };
        glUniform3fv(lu.eye, 1, eyef);
        glUniform1i(lu.persp, proj.has_eye_point() ? 1 : 0);
        glUniform1f(lu.half_width, static_cast<float>(line3d_half_width(l, proj)));
        glUniform4f(lu.depth, static_cast<float>(ddir.x), static_cast<float>(ddir.y),
                    static_cast<float>(ddir.z), dbase);
        glUniform3f(lu.shade, std::clamp(l.opts.depthshade, 0.0f, 1.0f), dmin, dspan);
        glUniform1i(lu.colormapped, l.colormapped() ? 1 : 0);
        if (l.colormapped()) {
            // Unit 3: 0 is a plane's own raster and 1/2 are the peel's depth
            // textures, so this is the first unit nothing else claims.
            if (!line3d_cmap_tex_) {
                glGenTextures(1, &line3d_cmap_tex_);
                glBindTexture(GL_TEXTURE_2D, line3d_cmap_tex_);
                // Linear, which is the whole point: sampling between two
                // entries is what makes the ramp continuous rather than
                // 256 steps.
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            if (line3d_cmap_in_tex_ != static_cast<int>(l.opts.cmap)) {
                glBindTexture(GL_TEXTURE_2D, line3d_cmap_tex_);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, colormaps::get(l.opts.cmap));
                line3d_cmap_in_tex_ = static_cast<int>(l.opts.cmap);
            }
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, line3d_cmap_tex_);
            glActiveTexture(GL_TEXTURE0);
            glUniform1i(lu.cmap, 3);
        }

        glBindVertexArray(line3d_vao_);

        unsigned int inst = c.vbo;
        if (translucent && !peel_.active) {
            // Back to front by segment midpoint, which is a heuristic and says
            // so -- two ribbons of one path can cross, unlike two billboards
            // of one cloud. A reordered copy rather than an index buffer, for
            // the reason the cloud's fallback gives.
            line3d_draw_order(l, proj, order);
            sorted.clear();
            sorted.reserve(order.size() * kStride);
            for (const std::size_t k : order) {
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
        for (int a = 0; a < 4; ++a) {      // prev, a, b, next
            glEnableVertexAttribArray(1 + a);
            glVertexAttribPointer(1 + a, 3, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void*>(a * 3 * sizeof(float)));
            glVertexAttribDivisor(1 + a, 1);
        }
        for (int a = 0; a < 2; ++a) {      // the two end colours
            glEnableVertexAttribArray(5 + a);
            glVertexAttribPointer(5 + a, 4, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void*>((12 + a * 4) * sizeof(float)));
            glVertexAttribDivisor(5 + a, 1);
        }
        glEnableVertexAttribArray(7);      // the two ends' values
        glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(20 * sizeof(float)));
        glVertexAttribDivisor(7, 1);

        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, c.segs);
        glDepthMask(GL_TRUE);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    end_pass();
}

// Error bars in a scene (v1.0 step 17). Unlike every other 3D draw here the
// buffer is camera-dependent -- a whisker faces the eye and the pixel lengths
// are converted at the box centre -- so the cache is keyed on the view as well
// as the data and the style. See ErrorBar3DCache.
void DataRenderer::draw_errorbars3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                                    const PlotRect& pr, float win_w, float win_h,
                                    ScenePass pass,
                                    const std::vector<std::size_t>* explicit_order) {
    const std::size_t ns = snap.scatter3d.size();
    const std::size_t total = ns + snap.lines3d.size();
    if (total == 0) return;
    const bool want_translucent = (pass == ScenePass::Translucent);

    // Series k is a cloud for k < ns and a path after -- the one indexing the
    // scene's fallback list and the peel loop share.
    auto drawn = [&](std::size_t k) {
        return k < ns ? errorbar3d_drawn(snap.scatter3d[k].err, snap.scatter3d[k].opts.errorbar)
                      : errorbar3d_drawn(snap.lines3d[k - ns].err,
                                         snap.lines3d[k - ns].opts.errorbar);
    };
    auto translucent = [&](std::size_t k) {
        return k < ns ? errorbar3d_translucent(snap.scatter3d[k])
                      : errorbar3d_translucent(snap.lines3d[k - ns]);
    };
    auto distance = [&](std::size_t k) {
        return k < ns ? scatter3d_plot_distance(snap.scatter3d[k], proj)
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
    Vec3 ddir{}; float dbase = 0.0f;
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
    for (const std::size_t k : series) {
        if (k >= total || !drawn(k)) continue;
        const bool is_scatter = k < ns;
        const ErrorBar3DOptions& st = is_scatter ? snap.scatter3d[k].opts.errorbar
                                                 : snap.lines3d[k - ns].opts.errorbar;
        const Color col = is_scatter ? errorbar3d_color(snap.scatter3d[k])
                                     : errorbar3d_color(snap.lines3d[k - ns]);
        const float depthshade = is_scatter ? snap.scatter3d[k].opts.depthshade
                                            : snap.lines3d[k - ns].opts.depthshade;

        // Everything the triangles depend on other than the data: the view,
        // the style and the resolved colour. Compared whole, which is simpler
        // to keep right than a field-by-field test that has to grow with the
        // struct.
        std::vector<double> view;
        view.reserve(40);
        for (float v : clip) view.push_back(v);
        view.insert(view.end(), { eye.x, eye.y, eye.z,
                                  proj.has_eye_point() ? 1.0 : 0.0,
                                  proj.box_units_per_pixel(Vec3{ 0.0, 0.0, 0.0 }),
                                  static_cast<double>(st.linewidth),
                                  static_cast<double>(st.capsize),
                                  static_cast<double>(st.capstyle),
                                  static_cast<double>(st.boxwidth),
                                  static_cast<double>(st.box_alpha),
                                  static_cast<double>(st.edge_alpha),
                                  static_cast<double>(col.r), static_cast<double>(col.g),
                                  static_cast<double>(col.b), static_cast<double>(col.a) });

        const CacheKey key{ axes_index_, is_scatter ? 0 : 1,
                            static_cast<int>(is_scatter ? k : k - ns) };
        ErrorBar3DCache& c = errbar3d_cache_[key];
        const bool stale = data_generation_ == 0 || c.data_generation != data_generation_ ||
                           c.view != view;
        if (stale) {
            pieces.clear();
            if (is_scatter) errorbar3d_pieces(proj, snap.scatter3d[k], k, pieces);
            else            errorbar3d_pieces(proj, snap.lines3d[k - ns], k - ns, pieces);

            auto emit = [](std::vector<float>& out, const Vec3 q[4], Color cc) {
                constexpr int tri[6] = { 0, 1, 2, 0, 2, 3 };
                for (int t : tri)
                    out.insert(out.end(), { static_cast<float>(q[t].x),
                                            static_cast<float>(q[t].y),
                                            static_cast<float>(q[t].z),
                                            cc.r, cc.g, cc.b, cc.a });
            };
            auto corners = [](const ErrorBar3DPiece& s, Vec3 q[4]) {
                if (s.face) {
                    for (int r = 0; r < 4; ++r) q[r] = s.p[r];
                    return true;
                }
                return errorbar3d_ribbon(s, q);
            };

            // Translucent pieces far to near by centroid depth, for the
            // unpeeled fallback: exact for pieces that do not cross, and
            // irrelevant where depth peeling runs.
            std::vector<std::pair<float, const ErrorBar3DPiece*>> back;
            opaque.clear();
            trans.clear();
            for (const ErrorBar3DPiece& s : pieces) {
                if (!(s.color.a > 0.0f)) continue;
                if (s.color.a < 1.0f) {
                    const Vec3 mid = s.face ? (s.p[0] + s.p[2]) * 0.5
                                            : (s.p[0] + s.p[1]) * 0.5;
                    back.push_back({ proj.project_box(mid).depth, &s });
                    continue;
                }
                Vec3 q[4];
                if (corners(s, q)) emit(opaque, q, s.color);
            }
            std::stable_sort(back.begin(), back.end(),
                             [](const auto& a, const auto& b) { return a.first > b.first; });
            for (const auto& entry : back) {
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
            upload(c.trans_vbo,  c.trans_verts,  trans);
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
                              reinterpret_cast<void*>(3 * sizeof(float)));
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
