#pragma once
#include "../coord_transform.h"
#include "../coord_transform3d.h"
#include "../plot_objects.h"
#include <cstdint>
#include <string>
#include <vector>

namespace sextant {

// A 2D plane in the 3D scene, reduced to what each output path can draw.
//
// One definition, two consumers -- the GPU in DataRenderer and the SVG writer
// -- for the reason plan_box3d() and plan_bars3d() are one function each: the
// raster and vector outputs must not be able to disagree about where a plane's
// geometry is or which texel sits on which corner. Here that would show as a
// slice drawn at a different place in the two files, which no comparison of
// counts would catch.

// The four corners of a heatmap on a plane, in the parent's *data* space, with
// the texture coordinates that belong on them.
//
// Data space rather than box space for the same reason the bar3d buffer is:
// the GPU holds it there, with the limits and the camera in uniforms, so
// neither a limit change nor an orbit invalidates anything.
//
// Ring order and texture assignment are copied exactly from the 2D quad in
// DataRenderer::draw_heatmap(): corner 0 is (xrange.lo, yrange.lo) at uv
// (0,1), and uv v = 0 is the *first* uploaded row. That is what makes
// "the same heatmap on a plane and on a 2D axes" a statement a test can make.
struct PlaneQuad {
    Vec3  p[4];
    float uv[4][2];
    // The plane's own normal in data space, as an axis index (0/1/2) -- the
    // axis `orient` is normal to. Kept because both consumers need to know
    // which axis the offset was applied to and neither should re-derive it.
    int   normal_axis = 2;
};

PlaneQuad plane_heatmap_quad(const HeatmapPlot& hp, PlaneOrientation orient,
                             double offset);


// ---------------------------------------------------------------------------
// The plane's reference raster (step 7a)
// ---------------------------------------------------------------------------
// A plane is a little screen: its contents are rendered into a framebuffer of
// their own, in the ordinary 2D painter order, and the plane enters the scene
// as one textured quad -- which is what its heatmap already was, generalised
// to everything else on it. This struct is everything that decision needs and
// none of the GL, so all of it is assertable with no context.
//
// **The extent is the box face, not the data.** A plane's u and v *are* two of
// the parent's axes, so the raster spans the parent's limits on them and the
// quad spans the box's cross-section at `offset`. Content outside the limits
// is clipped by the raster's own edges, exactly as a 2D axes clips to its
// frame -- which it was not before, when each primitive was projected on its
// own and a heatmap wider than the box drew outside it.
struct PlaneRaster {
    // The raster's size in *logical* pixels. This is the unit spec_impl.md
    // step 7a calls an approximated pixel: `LineOptions::width` and
    // `ScatterOptions::size` still name pixels, and on a plane they name
    // pixels of this raster -- the size the primitive would have if the plane
    // were drawn to fill the subplot frame. Camera-independent by
    // construction, which is what keeps an orbit from invalidating anything.
    int w = 1, h = 1;

    // The plane's own 2D transform: the parent's limits on the two in-plane
    // axes, onto [0,w] x [0,h]. Handed straight to the 2D draw calls, which is
    // the whole point -- a plane's contents are drawn by the same code, in the
    // same order, as a 2D axes' are.
    CoordTransform tr;

    // The quad, in the parent's *data* space, with the texture coordinates
    // that belong on them. Corner 0 is (u_min, v_min) and the ring runs
    // u then v.
    Vec3  p[4];
    float uv[4][2];
    int   normal_axis = 2;

    // How many *box* units one raster pixel covers. Square by construction --
    // the raster's two sides are proportional to the plane's two box sides, so
    // this one number serves both directions, which is what keeps a circular
    // marker circular in the raster.
    //
    // **This is the number that makes an approximated pixel a length**, and it
    // is here rather than derived at each call site because the SVG writer
    // needs exactly the same one: a stroke on a plane must come out the same
    // width in both outputs, and two derivations of "how big is a raster
    // pixel" would be two chances to disagree.
    double box_per_px = 1.0;
};

// The raster's longer side, in logical pixels. One definition, because the
// raster path and the vector path must agree on it to the pixel, and the
// frame is the only input: a plane can carry no useful detail beyond the
// viewport that displays it.
int plane_raster_cap(const PlotRect& frame);

PlaneRaster plane_raster(const Transform3D& tf, PlaneOrientation orient,
                         double offset, int max_dim);

// The overload both callers should use: the cap comes from the projector's own
// frame, so neither path can pick a different one.
PlaneRaster plane_raster(const Projector3D& proj, PlaneOrientation orient,
                         double offset);

// The heatmap's colormapped image, RGBA, rows top-down and the `origin` flip
// already applied -- i.e. row 0 is the row at the yrange.hi edge, which is the
// row a GL texture samples at t = 0 and the row a PNG puts at the top. One
// buffer therefore serves the texture upload and the <image> encode without
// either having to re-derive which way up it goes.
std::vector<std::uint8_t> plane_heatmap_rgba(const HeatmapPlot& hp);


// ---------------------------------------------------------------------------
// The rest of the 2D kinds, as primitives in the scene (step 6)
// ---------------------------------------------------------------------------
// Everything on a plane except its heatmaps, reduced to three kinds of
// primitive in the parent's *data* space -- which is where both output paths
// want them, the GPU because a data-space buffer survives an orbit and the SVG
// writer because it projects them itself through the shared projector.
//
// The split into three is §4's stroke fork made concrete:
//
//   Tri     a filled area in the plane      -- bar bodies, error-bar boxes
//   Seg     a stroke *in* the scene         -- lines, bar edges, whiskers
//   Marker  a symbol *on* the picture       -- scatter and scatter_z points
//
// A Seg's width is a pixel width **at the box centre**, converted once to a
// length in the box, so the number a caller writes is a pixel width somewhere
// (which is what makes `linewidth` readable) and a world length everywhere
// (which is what makes it geometry). A Marker's size is a pixel size
// everywhere, and that is a decision rather than an omission -- see below.
struct PlaneGeometry {
    struct Tri    { Vec3 p[3]; Color fill; };
    struct Seg    { Vec3 a, b; Color color; float width_px = 1.0f; };
    struct Marker { Vec3 p; Color color; float size_px = 1.0f;
                    MarkerStyle marker = MarkerStyle::Circle; };

    std::vector<Tri>    tris;
    std::vector<Seg>    segs;
    std::vector<Marker> markers;

    // The 2D painter order, as consecutive ranges into the three lists above.
    //
    // Three passes over three lists would be simpler and would get the layers
    // wrong: on a 2D axes the order is bar fill, bar edge, line, error box,
    // error whisker, then markers -- which alternates between fills and
    // strokes twice. Since every primitive on one plane is exactly coplanar,
    // no depth test can recover that order, so it has to be *drawn* in it.
    // These batches are what let a plane composite exactly as the same data
    // does on a 2D axes, which is the claim this step exists to make.
    struct Batch {
        enum class Kind { Tri, Seg, Marker } kind = Kind::Tri;
        std::size_t begin = 0, end = 0;
    };
    std::vector<Batch> batches;

    bool empty() const { return batches.empty(); }
};

// Everything on `p` except its heatmaps, in the 2D painter order.
//
// **The SVG writer's own view of a plane, and only that, since step 7a.** The
// raster path used to consume this too, through three dedicated programs; it
// now renders a plane's contents into the plane's own framebuffer through the
// ordinary 2D draw calls (see PlaneRaster). The two paths still share this one
// description of *where* everything is, which is what the file's opening
// paragraph asks for -- what they no longer share is how it gets rasterized,
// exactly as the 2D kinds have always had a GPU path and an SVG path.
//
// > **Correction (step 7a).** This said "**Marker sizes stay in pixels**", on
// > the grounds that a marker denotes a point, carries no data in its size,
// > and becomes unreadable when shrunk with distance -- matplotlib's mplot3d
// > does the same, and it superseded §4's table, which had listed markers
// > among the world-space cases. That argument was sound for a *billboard*,
// > and a billboard is what a plane's markers no longer are. Under the screen
// > model everything drawn on a plane is drawn *into* it, so a marker
// > foreshortens and recedes with the plane, and even at a 1:1 texel ratio a
// > circle on a tilted plane is an ellipse -- there is no version of the old
// > rule still available. `size` still names pixels, but pixels of the
// > plane's own raster: see PlaneRaster and spec_impl.md step 7a, decisions 2
// > and 4. The one marker that is still a screen symbol is the one in the
// > *hoisted* legend, which was never in the scene to begin with.
PlaneGeometry plane_geometry(const PlaneSnapshot& p);

// How far a plane's centre is from the eye, for ordering translucent planes
// against each other and against the bar3d objects beside them. The same
// admitted heuristic bar3d_plot_distance() is, and here for the same reason:
// two objects with no separating plane between them have no exact order. See
// memory/spec_3d.md §10 for the known flaw that granularity leaves.
double plane_distance(const PlaneSnapshot& p, const Projector3D& proj);

// True when a plane has to be drawn back to front rather than left to the
// depth buffer. One predicate, asked by the raster path, the vector path and
// the tests.
bool plane_translucent(const PlaneSnapshot& p);

// ---------------------------------------------------------------------------
// The SVG side: the warp problem
// ---------------------------------------------------------------------------
// SVG's transform is affine and a projective warp is not, so <image> -- the
// representation the 2D path uses -- cannot place a perspective-projected
// plane. The split is by projection mode (spec_3d.md §10):
//
//   Orthographic: the plane maps affinely, so <image> with a matrix(...) is
//                 exact. This is the default camera, so the common case keeps
//                 the compact output.
//   Perspective:  one <polygon> per cell, projected individually. Also exact,
//                 and bounded by kPlane3DCellCap -- a 512x512 slice is 262,144
//                 polygons, which is not an SVG anybody wants. Above the cap
//                 the plane falls back to an affine <image> of three of its
//                 four projected corners, with `warning` saying so.
//
// Everything else on the plane needs no such split: a triangle, a stroke and a
// marker all project to ordinary pixel-space elements in either mode.
struct PlanePlanItem {
    // Which form this item took; exactly one of the payloads below is
    // populated. One flat ordered list rather than a struct per form, because
    // the writer's whole job here is to emit them in order -- and the order
    // runs *across* the forms, since a plane's heatmap, its fills, its strokes
    // and its markers are four layers of one picture.
    enum class Form { Image, Polys, Strokes, Markers };
    Form form = Form::Image;

    // Form::Image -- the colormapped pixels, and the matrix mapping the unit
    // image square (0,0 top-left .. 1,1 bottom-right) onto the figure's
    // pixels, as SVG's matrix(a b c d e f).
    std::vector<std::uint8_t> rgba;
    int   rows = 0, cols = 0;
    float matrix[6] = { 1, 0, 0, 1, 0, 0 };

    // Form::Polys -- filled rings of x,y pixel pairs, already near-plane
    // clipped. Both the per-cell perspective heatmap and the plane's own
    // filled areas (bar bodies, error boxes) take this form: they are the same
    // element, and giving them one representation is what keeps the writer
    // free of a second polygon emitter.
    struct Poly { std::vector<float> xy; Color fill; };
    std::vector<Poly> polys;

    // Form::Strokes -- open pixel polylines with a width. A width is one
    // number per stroke, which is §4's SVG concession spent per segment here
    // rather than per line, so it is finer than the concession allows.
    struct Stroke { std::vector<float> xy; Color color; float width = 1.0f; };
    std::vector<Stroke> strokes;

    // Form::Markers -- a symbol at a pixel, at its own pixel size, which is
    // the same thing the raster path draws because a marker does not scale.
    struct Mark { float x = 0, y = 0, size = 0; Color color;
                  MarkerStyle marker = MarkerStyle::Circle; };
    std::vector<Mark> marks;

    // Whole-plane opacity (Plane2DOptions::alpha); the per-primitive colours
    // carry only what their own plot object gave them.
    float alpha = 1.0f;

    // Distance of the owning plane's centre from the eye -- the key the writer
    // merges these against the bar polygons with. Larger is further. Every
    // item of one plane carries the same value, so a stable sort keeps a
    // plane's own layers together and in build order.
    float depth = 0.0f;

    // Which plane and which plot object this came from. The plan is the only
    // account of the scene the writer gets, so without this the order it is put
    // in cannot be checked from outside -- the lesson of the bar3d sort.
    std::size_t plane = 0, plot = 0;

    // The owning plane's own quad, in box space, repeated on every item of
    // that plane. It is what the step-9 painter orders a plane *by*: since 7a
    // a plane is one flat quad, so all four of its forms -- image, cells,
    // strokes, markers -- are layers of one coplanar picture and share one
    // position in the scene. Splitting the quad therefore splits the plane,
    // and the writer draws its items through the piece's outline.
    Vec3 quad[4]{};

    // Non-empty only when the cell cap forced the affine fallback above. The
    // writer emits it as an XML comment: the file itself is where a warning
    // about the file belongs, and this library has no logging channel.
    std::string warning;
};

// Above this many cells a perspective plane stops being emitted per cell.
// Chosen so a 128x128 slice is still exact and a 512x512 one is not.
inline constexpr std::size_t kPlane3DCellCap = 20000;

// Every plane's contents, reduced as above and ordered back to front by plane
// distance -- the painter's algorithm SVG needs because it has no depth
// buffer. Within one plane the items are in the 2D painter order. Planes with
// nothing on them produce nothing.
std::vector<PlanePlanItem> plan_planes3d(const Projector3D& proj,
                                         const std::vector<PlaneSnapshot>& planes);

} // namespace sextant
