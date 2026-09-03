#include "data_renderer.h"
#include "coord_transform.h"
#include "../colormaps.h"
#include "../line_dash.h"
#include <glad/glad.h>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>

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

// Clips drawing to this axes' plot rect. pr/win_h are in logical pixels;
// glScissor wants real framebuffer pixels, hence the pixel_ratio_ scale.
// Rounded outward (floor the origin, ceil the far edge) so a fractional rect
// never clips a pixel the plot legitimately covers.
void DataRenderer::begin_pass(const PlotRect& pr, float win_h) const {
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

// One whisker in pixels: a capped segment from `lo` to `hi` along one axis.
// `vertical` selects the axis; (cx, cy) is the point it hangs off. A whisker
// of zero length draws nothing -- the stem would vanish but its two caps
// would not, and stacked caps read as a tiny range rather than none.
void build_whisker(float cx, float cy, float lo, float hi,
                   float linewidth, float capsize, bool vertical,
                   std::vector<float>& out)
{
    const float hw = std::max(linewidth, 0.0f) * 0.5f;
    if (hw <= 0.0f || lo == hi) return;
    const float cap = std::max(capsize, 0.0f) * 0.5f;

    if (vertical) {
        push_px_rect(cx - hw, lo, cx + hw, hi, out);
        if (cap > 0.0f) {
            push_px_rect(cx - cap, lo - hw, cx + cap, lo + hw, out);
            push_px_rect(cx - cap, hi - hw, cx + cap, hi + hw, out);
        }
    } else {
        push_px_rect(lo, cy - hw, hi, cy + hw, out);
        if (cap > 0.0f) {
            push_px_rect(lo - hw, cy - cap, lo + hw, cy + cap, out);
            push_px_rect(hi - hw, cy - cap, hi + hw, cy + cap, out);
        }
    }
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

    for (std::size_t i = 0; i < n; ++i) {
        const float cx = tr.to_px(xs[i]);
        const float cy = tr.to_py(ys[i]);

        if (err.has_y_span())
            build_whisker(cx, cy,
                          tr.to_py(err.y_lo(i, ys[i])),
                          tr.to_py(err.y_hi(i, ys[i])),
                          style.linewidth, style.capsize, true, stroke);
        if (err.has_x_span())
            build_whisker(cx, cy,
                          tr.to_px(err.x_lo(i, xs[i])),
                          tr.to_px(err.x_hi(i, xs[i])),
                          style.linewidth, style.capsize, false, stroke);

        // The box spans whichever directions have a variance; a direction
        // without one falls back to `boxwidth` pixels, which is what makes a
        // line's y-only box a fixed-width rectangle and a scatter's with both
        // a true 2D one.
        if (!err.has_y_box() && !err.has_x_box()) continue;
        float bx0, bx1, by0, by1;
        if (err.has_x_box()) {
            bx0 = tr.to_px(xs[i] - err.x_var(i));
            bx1 = tr.to_px(xs[i] + err.x_var(i));
        } else {
            bx0 = cx - halfbox; bx1 = cx + halfbox;
        }
        if (err.has_y_box()) {
            by0 = tr.to_py(ys[i] - err.y_var(i));
            by1 = tr.to_py(ys[i] + err.y_var(i));
        } else {
            by0 = cy - halfbox; by1 = cy + halfbox;
        }
        if (bx0 == bx1 || by0 == by1) continue;   // zero variance: no box
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

        const CacheKey key{ axes_index_, static_cast<int>(li) };
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

            // Padded with a duplicate of the first and last point, so instance
            // i can read prev/p0/p1/next as points [i, i+1, i+2, i+3] and the
            // two ends degenerate into butt caps.
            const std::size_t n = lp.x.size();
            pts.clear();
            pts.reserve((n + 2) * 2);
            auto emit = [&](std::size_t i) {
                if (e.data_space) {
                    pts.push_back(static_cast<float>(lp.x[i] - e.anchor_x));
                    pts.push_back(static_cast<float>(lp.y[i] - e.anchor_y));
                } else {
                    pts.push_back(tr.to_px(lp.x[i]));
                    pts.push_back(tr.to_py(lp.y[i]));
                }
            };
            emit(0);
            for (std::size_t i = 0; i < n; ++i) emit(i);
            emit(n - 1);

            if (e.vbo == 0) glGenBuffers(1, &e.vbo);
            glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(pts.size() * sizeof(float)),
                         pts.data(), GL_DYNAMIC_DRAW);

            e.segments        = static_cast<int>(n - 1);
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

        const CacheKey key{ axes_index_, static_cast<int>(si) };
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

        const CacheKey key{ axes_index_, static_cast<int>(si) };
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

        const CacheKey key{ axes_index_, static_cast<int>(bi) };
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

        const CacheKey key{ axes_index_, index };
        ErrCache& e = cache[key];
        const bool ok = e.generation != 0
                     && e.generation == data_generation_
                     && e.linewidth == style.linewidth
                     && e.capsize == style.capsize
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

        // Origin="lower": row 0 is at bottom → flip vertically when uploading
        const bool flip = (hp.opts.origin == "lower");

        const CacheKey key{ axes_index_, static_cast<int>(hi) };
        HeatCache& hc = heat_cache_[key];

        // Depends only on the data and the colour mapping, never on the
        // view, so unlike the line cache this survives pan and zoom.
        const bool usable = hc.tex != 0
                         && hc.data_generation != 0
                         && hc.data_generation == data_generation_
                         && hc.cmap == hp.opts.cmap
                         && hc.vmin == hp.opts.vmin && hc.vmax == hp.opts.vmax
                         && hc.flip == flip
                         && hc.rows == hp.rows && hc.cols == hp.cols;

        if (usable) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, hc.tex);
        } else {

        // Apply colormap on CPU → RGBA texture
        const uint8_t* lut = colormaps::get(hp.opts.cmap);
        const int n = hp.rows * hp.cols;
        std::vector<uint8_t> rgba(static_cast<std::size_t>(n) * 4);
        const float vmin = hp.opts.vmin, vrange = hp.opts.vmax - hp.opts.vmin;
        for (int i = 0; i < n; ++i) {
            float t = (vrange != 0.0f) ? (hp.data[i] - vmin) / vrange : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            const int idx = static_cast<int>(t * 255.0f);
            std::memcpy(&rgba[static_cast<std::size_t>(i) * 4], &lut[idx * 4], 4);
        }

        // Upload texture
        if (hc.tex == 0) glGenTextures(1, &hc.tex);
        const unsigned int tex = hc.tex;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        if (flip) {
            std::vector<uint8_t> flipped(rgba.size());
            const std::size_t row_bytes = static_cast<std::size_t>(hp.cols) * 4;
            for (int r = 0; r < hp.rows; ++r) {
                std::memcpy(&flipped[static_cast<std::size_t>(r) * row_bytes],
                            &rgba[static_cast<std::size_t>(hp.rows - 1 - r) * row_bytes],
                            row_bytes);
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, hp.cols, hp.rows, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, hp.cols, hp.rows, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        }

        hc.data_generation = data_generation_;
        hc.cmap = hp.opts.cmap;
        hc.vmin = hp.opts.vmin; hc.vmax = hp.opts.vmax;
        hc.flip = flip;
        hc.rows = hp.rows; hc.cols = hp.cols;
        }   // end cache miss

        // Quad covering [0,cols] × [0,rows] in data space, mapped to pixel space.
        // Tex coords: (0,0)=top-left, (1,1)=bottom-right in GL convention.
        const float x0 = tr.to_px(0.0),           x1 = tr.to_px(static_cast<double>(hp.cols));
        const float y0 = tr.to_py(0.0),            y1 = tr.to_py(static_cast<double>(hp.rows));
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

} // namespace sextant
