#pragma once
#include "export.h"
#include "style.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sextant {

// A point or direction in the box's own coordinates. Public because Camera3D
// carries one; the arithmetic on it lives in src/coord_transform3d.h, which is
// the only place that needs it.
struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

// The side lengths of the box every 3D axes normalizes its data onto.
//
// 3D data has incommensurable units on its three axes -- nanometres, seconds,
// counts -- so the camera cannot be pointed at raw data coordinates without
// the picture's shape depending on the unit a caller happened to choose, and
// without "distance from the camera" being meaningless. Each axis's resolved
// limits are therefore mapped onto a box centred on the origin with these
// side lengths, and every camera number below is in *box* units, which makes
// them scale-free: one default serves every dataset.
//
// This is also the only place a 3D axes' proportions are chosen. {1,1,1} is a
// cube; {2,1,1} makes x twice as long on screen as y, whatever the data says.
struct BoxAspect {
    double x = 1.0, y = 1.0, z = 1.0;
};

// How the eye maps the scene onto the cell.
//
// Orthographic is the default because a plot is read as much as it is looked
// at: parallel edges stay parallel, so a length on the box means the same
// thing wherever it sits, which is what makes the picture measurable. A
// perspective camera trades that for depth cues, and is worth having for a
// scene with real extent in it.
enum class Projection {
    Orthographic,
    Perspective,
};

// Where the camera is and what it looks at, in box coordinates.
//
// An orbit camera rather than a free one: azimuth/elevation/target is what a
// plot is actually navigated with, and it cannot be rolled into a disorienting
// state the way a free rotation can. `zoom` scales a fit that is otherwise
// computed per frame -- the box is sized to fill its cell whatever the camera
// angle, so there is no distance to tune to get a picture at all.
struct Camera3D {
    // Degrees. Azimuth rotates about the box's z axis (0 looks along +x);
    // elevation is the angle above the xy plane and is clamped to +-89 so the
    // view-up vector cannot become parallel to the view direction.
    double azimuth   = -60.0;
    double elevation =  30.0;

    // The box-space point the camera orbits and centres on. Defaults to the
    // box's own centre, i.e. the middle of the data.
    Vec3 target{ 0.0, 0.0, 0.0 };

    // Multiplies the automatic fit: > 1 magnifies, < 1 pulls back. A pure
    // magnification of the finished picture in both projection modes, so it
    // never changes how much perspective there is -- that is what `fov` and
    // the dolly do.
    double zoom = 1.0;

    Projection projection = Projection::Orthographic;

    // Vertical field of view in degrees, perspective only; ignored under an
    // orthographic camera. Clamped to [5, 120].
    //
    // This is the only perspective knob, and it is not a focal length in
    // disguise: the eye distance is *derived* from it, since the box is
    // fitted to the cell every frame (see BoxAspect). A narrow fov is a
    // distant camera and a nearly parallel picture; a wide one is a close
    // camera and strong foreshortening. The box fills the cell either way.
    double fov = 45.0;
};

// Which pair of box axes something lies in -- named by the two it spans, with
// the third being the one perpendicular to it. `bar3d` uses it to say which
// way its bars stand; `Plane2D` uses the same enum to say which way a plane
// faces, and in the same order -- the two it spans first, the one it is offset
// along last.
//
//   XY -> bars stand along z, on an x-y grid
//   YZ -> along x, on a y-z grid
//   ZX -> along y, on a z-x grid
enum class PlaneOrientation { XY, YZ, ZX };

struct Plane2DOptions {
    // Whole-plane opacity, for stacked slices: it multiplies whatever the
    // plot objects on the plane already carry. Exactly 1 keeps the cheap
    // path -- an opaque plane is resolved by the depth buffer, a translucent
    // one has to be ordered against its siblings for the camera it is seen
    // from, which is the same bargain Bar3DOptions::alpha strikes.
    float alpha = 1.0f;

    // Off hides the plane's *drawing* and nothing else: the box's limits, the
    // colorbar a heatmap on it asked for and its legend keys all stay exactly
    // where they were. That is deliberate, and is what makes the Cosmetic
    // panel's per-plane checkbox usable for the comparison it exists for --
    // switching a slice off to look behind it must not resize the box and
    // move every other slice in the same instant.
    bool visible = true;
};

struct Bar3DOptions {
    Color color = Color::Blue;

    // Below 1 the bars are translucent and every face of every bar is drawn,
    // back to front, so the ones behind show through.
    //
    // It costs more than it looks: an opaque scene is resolved by the depth
    // buffer in one draw whatever the camera does, while a translucent one has
    // to be *ordered* for the camera every frame, and blending has to be
    // ordered per face rather than per bar. Exactly 1 keeps the cheap path,
    // which is why it is the default and why the comparison is exact rather
    // than a tolerance.
    float alpha = 1.0f;

    // Footprint as a fraction of the grid spacing, in each of the two
    // directions the grid spans -- BarOptions::width's precedent, and resolved
    // to data-space extents at ingest for the same reason. 1.0 makes
    // neighbouring bars touch.
    float width = 0.8f;   // along u
    float depth = 0.8f;   // along v

    // Where every bar starts. The overload taking a `bottoms` span gives one
    // per bar instead; this is what the rest read.
    double bottom = 0.0;

    // Per-face flat shading, from a light fixed in *box* space -- so a face
    // keeps its brightness as the camera orbits, and the three visible faces
    // of a bar read as distinct without a lighting model. 0 draws every face
    // in the flat `color`; 1 takes the dimmest face to black.
    float shading = 0.45f;

    // Bar outlines. Their width is in pixels at the bar's own depth, so under
    // a perspective camera a distant bar's edges are thinner -- they are
    // geometry in the scene, unlike the axis frame, whose width is fixed on
    // screen whatever the camera does (see memory/spec_3d.md §4).
    //
    // The outline has its own opacity rather than sharing the face's. It was
    // shared once, on the argument that an outline belongs to its bar -- but a
    // wireframe faded to match the glass it is drawn on is exactly what makes
    // translucent bars unreadable, since the edges are the only thing left
    // saying where one box stops and the next starts. So an outline stays as
    // solid as it was asked to be, and `edge_alpha` is what dims it.
    //
    // What `alpha` still decides for the outline is *which* edges are drawn: a
    // translucent bar shows all twelve of every box, including the six a solid
    // one hides behind itself, because there is nothing left to hide them.
    bool  edges         = false;
    Color edgecolor     = Color::Black;
    float edge_alpha    = 1.0f;
    float edge_linewidth = 1.0f;

    // Keys this grid in the axes' legend, by a swatch of `color` (v1.0 step
    // 11.5). A grid is one series in the sense a legend means: every bar in it
    // is drawn in the one colour, so the swatch is honest about all of them --
    // which is what separates this from a colormapped kind, where it would
    // not be. Empty draws no key, and `show_legend` is the switch that does
    // not cost the caller the text (see LineOptions::show_legend).
    std::string name;
    bool        show_legend = true;

    // Optional hover text per bar, appended below the default
    // "x=.., y=.., height=.." line. Row-major over the u x v grid with u as
    // the major index -- the same layout `heights` has, so a label and its
    // bar share one index. Empty or out of range = no custom line.
    std::vector<std::string> hint_labels;
};

// A surface over the same `u x v` grid `bar3d` stands its bars on -- one
// mental model for both, so `heights` is row-major with u as the major index
// in each and a caller who has laid out one has laid out the other.
struct SurfaceOptions {
    // The flat colour every cell takes, unless `colormap` is on.
    Color color = Color::Blue;

    // Colour each cell by its own height instead -- which is what a surface is
    // usually for, since the shape already shows the height and the colour can
    // then carry it a second time for the reader who is looking down at it.
    bool     colormap = false;
    Colormap cmap     = Colormap::Viridis;

    // The range the colormap is normalized over, in the dependent axis's own
    // data units. **Equal (the default) means the surface's own range**, which
    // is a deliberate departure from HeatmapOptions and ScatterZOptions, where
    // vmin/vmax default to 0..1. Their data is often already normalized;
    // a surface's heights are in whatever units the caller measured, so a
    // 0..1 default would clip almost every real surface to one end of the
    // colormap. An empty interval cannot be a colour scale, so it is free to
    // mean something else.
    float vmin = 0.0f, vmax = 0.0f;

    // Draw the scale beside the frame, exactly as a heatmap's `colorbar` does
    // (v1.0 step 11.3). Gated on `colormap`: with it off the sheet is one flat
    // colour, and a bar would be a key to a mapping the picture does not use.
    //
    // What the bar spans is the *resolved* range, so an untouched vmin/vmax
    // labels the bar with the surface's own heights rather than with 0 and 0.
    // An axes draws every bar that was asked for, so two surfaces on different
    // scales get one each.
    bool colorbar = false;

    // Names this surface. Which way it is shown depends on how the surface is
    // coloured, and the two are mutually exclusive: with `colormap` on it
    // names the colour scale on the bar (v1.0 step 11.4), and with it off it
    // keys the legend by a swatch of `color` (step 11.5, finished after it).
    //
    // A colormapped sheet is deliberately *not* keyed -- it has no one colour
    // a swatch could honestly show, which is the same reason scatter_z is
    // keyed white with a black edge rather than in a colour it does not have.
    // A flat one has exactly one colour, so a swatch is the whole truth.
    std::string name;
    bool        show_legend = true;   // see LineOptions::show_legend

    // Below 1 the surface is translucent. It costs the same as it costs a
    // bar3d grid, and buys less: a translucent surface against another
    // translucent object has no exact order (see memory/spec_3d.md §10), so
    // this is the one option here whose result is a heuristic.
    float alpha = 1.0f;

    // Per-cell flat shading from the same box-space light `bar3d` uses, so a
    // surface and a bar chart in one figure are lit alike and a cell keeps its
    // brightness as the camera orbits. 0 draws every cell flat; 1 takes the
    // unlit ones to black. A surface needs this more than bars do -- without
    // it a single-coloured surface is a silhouette with no shape in it.
    float shading = 0.45f;

    // The wireframe. Drawn as a stroke on each cell, so it is the grid the
    // surface was sampled on rather than a decoration with its own spacing.
    // Width is in pixels at the box centre and thins with distance, like a
    // bar's outline and unlike the axis frame (memory/spec_3d.md §4).
    bool  edges          = false;
    Color edgecolor      = Color::Black;
    float edge_alpha     = 1.0f;
    float edge_linewidth = 1.0f;

    // Optional hover text per *sample*, appended below the default
    // "x=.., y=.., z=.." line. Row-major over the u x v grid with u as the
    // major index -- the same layout `heights` has, so a label and its sample
    // share one index, exactly as Bar3DOptions::hint_labels does.
    //
    // Indexed by sample and not by cell, even though what the pointer lands on
    // is a cell: a cell has four samples and no identity of its own, and the
    // tooltip reports the nearest of the four, so the label a caller writes
    // belongs to the point they wrote it about.
    std::vector<std::string> hint_labels;
};

// A sheet on a triangulated mesh -- `surface`'s sibling for data that has no
// `u x v` grid (v1.0 step 14). A mesh out of a solver, a scan or a mesh file
// is vertices plus topology, and forcing it onto a rectangular grid would be a
// resampling the caller never asked for.
//
// **The colour is per vertex and interpolates across a triangle; the shade is
// flat per face**, and the two are telling the truth about different things.
// The shade encodes the geometry, which really is faceted -- a mesh is flat
// within each triangle. The colour encodes the fourth measured quantity, which
// really is continuous, and flattening it per face would throw away resolution
// the caller supplied.
//
// What interpolates is the **value**, not the colour: a barycentric blend of
// two distant colormap entries is a whole triangle of colours the colorbar
// beside it does not contain, so each fragment looks its own interpolated
// value up in the map. That is Line3DOptions' rule, binding harder here -- a
// segment's RGB chord is one line off the map, a triangle's blend is an area.
struct SurfaceTriOptions {
    // The flat colour the whole mesh takes, unless the mesh was given a
    // `colors` vector -- which overrides it entirely and ramps each triangle
    // through cmap/vmin/vmax below.
    //
    // There is no `colormap` flag of the kind SurfaceOptions carries, and that
    // is structural rather than an omission: a grid surface rises along the
    // axis its `orient` names, so "colour by height" names a real quantity,
    // while a mesh has no privileged axis and no height to colour by. The
    // fourth dimension has to be given, exactly as scatter3d and line3d
    // require it, and *having* it is what makes the mesh colormapped.
    Color color = Color::Blue;

    // The colormap a `colors` vector is mapped through, and the range it is
    // normalized over. Read only by the overloads that take one.
    Colormap cmap = Colormap::Viridis;

    // **Equal (the default) means the mesh's own range**, on exactly the terms
    // Scatter3DOptions::vmin/vmax states: a `colors` vector is a measured
    // quantity in the caller's own units, so a 0..1 default would clip almost
    // every real mesh to one end of the colormap.
    float vmin = 0.0f, vmax = 0.0f;

    // Draw the scale beside the frame, gated on the mesh actually having a
    // `colors` vector -- without one every triangle is the flat `color`, and a
    // bar would key a mapping the picture does not use.
    bool colorbar = false;

    // Names this mesh. Shown on the colorbar when the mesh is colormapped, and
    // as a legend swatch of `color` when it is flat -- and, as with
    // SurfaceOptions, the two are mutually exclusive: a colormapped sheet is
    // **not** keyed in the legend, because it has no one colour a swatch could
    // honestly show and it does have a shape in the picture to be recognized
    // by. That is also why a colormapped *path* is keyed where this is not:
    // every line's swatch is the same shape (see Line3DOptions::name).
    std::string name;
    bool        show_legend = true;   // see LineOptions::show_legend

    // Below 1 the mesh is translucent, at SurfaceOptions::alpha's cost -- and
    // a little more of it. A grid's cells are separable by an axis-aligned
    // plane and a mesh's triangles are not, so a mesh can occlude *itself*
    // with no exact whole-object order to fall back on. Depth peeling answers
    // that exactly; the sorted fallback is a heuristic (memory/spec_3d.md
    // §10).
    float alpha = 1.0f;

    // Per-face flat shading from the same box-space light `bar3d` and
    // `surface` use, on SurfaceOptions::shading's exact terms: 0 draws every
    // triangle flat, 1 takes the unlit ones to black.
    //
    // Two-sided, like a grid surface's, for a reason that binds harder here:
    // the shade is |n.l| rather than max(0, n.l), because a sheet has two
    // sides and no outside. A grid at least has consistent winding by
    // construction; a mesh read out of a file often does not, so a one-sided
    // rule would black out whichever triangles happened to be wound the other
    // way. Nothing is ever backface-culled, for the same reason.
    float shading = 0.45f;

    // The wireframe -- **all three edges of every triangle**, so an interior
    // edge shared by two faces is drawn twice. The same bargain
    // SurfaceOptions::edges strikes: a duplicate line, and no boundary special
    // case anywhere. Width is in pixels at the box centre and thins with
    // distance (memory/spec_3d.md §4).
    bool  edges          = false;
    Color edgecolor      = Color::Black;
    float edge_alpha     = 1.0f;
    float edge_linewidth = 1.0f;

    // Optional hover text per **vertex**, index-aligned with x/y/z, appended
    // below the default "x=.., y=.., z=.." line. A mesh is hovered at its
    // vertices -- §7b's "a cell has four samples and no identity of its own"
    // with three for four, agreeing with line3d's vertex rule -- so a label
    // belongs to the point the caller wrote it about.
    std::vector<std::string> hint_labels;
};

// Error-bar **data** in a scene (v1.0 step 17): twelve per-point spans, four
// per direction, passed to scatter3d()/line3d() as a parameter of their own.
//
// **Spans, and a parameter rather than a field of the options struct.** This
// is the same decision `colors` records, for the same reason: `opts` is
// deep-copied into every snapshot the render thread takes, so twelve per-point
// vectors parked there would put a memcpy of them on every frame of a pan.
// The line it draws is the one that runs through the whole library -- **`opts`
// holds what a panel can edit, parameters hold what the caller measured** --
// and the style half is ErrorBar3DOptions, which a panel *can* edit. Written
// braced with designated initializers, which is what makes twelve of them
// cost nothing to a caller who uses one:
//
//     ax3->scatter3d(x, y, z, {.z_cap_lo = err}, opts);
//
// Every rule is 2D's `ErrorBar` (step 16), read in three directions:
//
//   - **Offsets from the point, not absolute coordinates**, and
//     magnitude-valued: a negative is a spread, not an inverted bar.
//   - **One end given means symmetric** -- `z_cap_lo` alone puts the same
//     distance either side.
//   - **Cap data draws the capped whisker, box data draws the box**, and each
//     is omittable.
//   - **A zero offset draws nothing on that side**, and a non-finite entry is
//     read as zero, so one point's bar can be masked out without a throw.
//
// A non-empty span must hold exactly one entry per point, or the method
// throws. The spans are read during the call and copied.
//
// What a scene changes about the drawing:
//
//   - **a whisker and its caps are a flat, rigid billboard.** A cap has no
//     plane to lie in -- the perpendicular to a whisker in a scene is a whole
//     circle of directions -- so each whisker is laid in the plane containing
//     its own axis and facing the eye *at the point*: memory/spec_3d.md §4's
//     billboarded-ribbon row, the one `bar3d` edges and `line3d` use. A
//     whisker pointing straight at the camera draws nothing, which is what an
//     end-on stick looks like.
//   - **the box is a block**, one per point: `[p - box_lo, p + box_hi]` on
//     every axis with box data, and ErrorBar3DOptions::boxwidth wide, centred
//     on the point, on every axis without. With box data on all three axes
//     `boxwidth` plays no part. It is translucent by default, so the point
//     inside it stays visible.
struct ErrorBar3D {
    std::span<const double> x_cap_lo, x_cap_hi, x_box_lo, x_box_hi;
    std::span<const double> y_cap_lo, y_cap_hi, y_box_lo, y_box_hi;
    std::span<const double> z_cap_lo, z_cap_hi, z_box_lo, z_box_hi;

    // True when anything at all has been set.
    bool any() const {
        for (std::span<const double> s : { x_cap_lo, x_cap_hi, x_box_lo, x_box_hi,
                                           y_cap_lo, y_cap_hi, y_box_lo, y_box_hi,
                                           z_cap_lo, z_cap_hi, z_box_lo, z_box_hi })
            if (!s.empty()) return true;
        return false;
    }
};

// Error-bar **style** in a scene: what a panel can edit, as against the
// measured spans in ErrorBar3D. Scalars only, so unlike the data it costs
// nothing to deep-copy into a snapshot.
//
// **Its own struct again since step 17**, after step 16 made it an alias of
// the 2D ErrorBarOptions: a 2D box is a flat rectangle, a 3D one is a block
// whose outline is twelve edges seen through its own faces, and that outline
// needs an opacity of its own -- which 2D has no use for.
//
// **Every pixel length here is a length in the scene** (`linewidth`,
// `capsize`, `boxwidth`): a pixel count *at the box centre*, converted once to
// box units, which is Line3DOptions::linewidth's and Bar3DOptions::
// edge_linewidth's bargain. Under a perspective camera a distant error bar is
// therefore smaller as a whole -- its caps and its box shrink with its
// whisker, whose length is data -- so it keeps its proportions at every depth.
struct ErrorBar3DOptions {
    // Unset = the series' own flat `color`, or black for a series with a
    // `colors` vector: a colormapped series has no one colour of its own, and
    // a bar per point in the point's colormap colour would key the error to
    // the value rather than to the series (2D scatter_z's rule).
    std::optional<Color> color;

    // The whisker, the caps and the box's edges. 0 draws no error bar at all,
    // box included -- 2D's rule.
    float linewidth = 1.0f;

    // Total cap length crossing each whisker end (capsize/2 either side). 0
    // draws no caps.
    float capsize = 6.0f;

    // Flat crossbar, or an open chevron whose tip is the whisker's end:
    // `capsize` wide, 0.87 x `capsize` long, scaled down whole when the
    // whisker is shorter than that. The same shape 2D draws.
    CapStyle capstyle = CapStyle::Flat;

    // The block's extent on each axis that has no box data of its own.
    float boxwidth = 10.0f;

    // Opacity of the block's faces and of its edges, each as a fraction of
    // `color`'s own alpha. Separate because they do different jobs: the faces
    // say how much of the scene the uncertainty covers, the edges say where
    // it stops, and a block faint enough to see a point through needs edges
    // that are not as faint. 0 drops that half.
    float box_alpha  = 0.25f;
    float edge_alpha = 0.5f;
};

// Markers at |x| points in the scene -- the 3D counterpart of Axes::scatter()
// and Axes::scatter_z() at once, since which of the two a series behaves like
// is decided by whether it was given a `colors` vector rather than by which
// method was called.
//
// **A note on the name `z`, because the 2D vocabulary collides here.** In 2D,
// ScatterZOptions' "z" *is* the colour dimension: a scatter_z has x and y
// positions and a z that is mapped through a colormap. In a 3D scene z is a
// coordinate like the other two, and the colour dimension is a fourth vector,
// `colors`, passed to the second scatter3d() overload. Nothing about a 2D
// scatter_z is renamed by this; the two simply mean different things by the
// same letter, and this is the one place that is visible.
struct Scatter3DOptions {
    // The flat colour every marker takes, unless the series was given a
    // `colors` vector -- which overrides it entirely and colours each marker
    // through cmap/vmin/vmax below.
    Color       color  = Color::Blue;

    // Marker diameter in pixels, and pixels wherever the marker is: a marker
    // denotes a point and its size carries no data, so it is a symbol in the
    // same category as a tick label and must not shrink with distance. See
    // memory/spec_3d.md §4 -- this is the one thing in the scene whose size
    // the camera does not decide.
    float       size   = 20.0f;
    MarkerStyle marker = MarkerStyle::Circle;

    // Below 1 the markers are translucent, with the cost every other 3D kind's
    // alpha has: an opaque series is resolved by the depth buffer in one draw,
    // a translucent one has to be composited against everything else in the
    // scene for the camera it is seen from.
    //
    // **Defaults to 1, where ScatterOptions::alpha defaults to 0.8.** In 2D
    // that default is what keeps a dense cloud readable where markers overlap;
    // here it would put every default scatter3d on the expensive path for a
    // legibility gain `depthshade` gives for nothing. A caller who wants to
    // see through a cloud still sets it.
    float       alpha  = 1.0f;

    // Darken each marker toward black with distance from the camera:
    // `depthshade * t`, where t runs 0 at the near face of the box to 1 at the
    // far one. Exactly the meaning SurfaceOptions::shading has -- 0 draws
    // every marker in its own colour, 1 takes the farthest to black -- so the
    // two 3D depth cues are one idea, one keyed on a face's normal and one on
    // distance. It costs nothing: the marker stays opaque and depth-tested.
    //
    // **t is measured over the box's own depth extent, not the series'**, so
    // two clouds in one scene are shaded on one scale and a series of one
    // point has an answer.
    //
    // Off by default, unlike `shading`: darkening is a change to the colour
    // the caller asked for, and a cloud is read for where its points are
    // before it is read for how far away they are. Note that it dims exactly
    // the colours that are already dark, which is where a depth cue is needed
    // most -- and that the legend key and the colorbar never darken, since
    // neither is in the scene.
    float       depthshade = 0.0f;

    // The colormap the `colors` vector is mapped through, and the range it is
    // normalized over. Read only by the overload that takes one.
    Colormap    cmap = Colormap::Viridis;

    // **Equal (the default) means the series' own range**, as SurfaceOptions
    // does and unlike HeatmapOptions/ScatterZOptions, whose 0..1 suits data
    // that is already normalized. A `colors` vector is a fourth measured
    // quantity in whatever units the caller measured it in, so a 0..1 default
    // would clip almost every real series to one end of the colormap. An empty
    // interval cannot be a colour scale, so it is free to mean something else.
    float       vmin = 0.0f, vmax = 0.0f;

    // Draw the scale beside the frame. Gated on the series actually having a
    // `colors` vector: without one every marker is the flat `color`, and a bar
    // would key a mapping the picture does not use -- the same gate
    // SurfaceOptions::colorbar has on `colormap`. The bar spans the *resolved*
    // range, so an untouched vmin/vmax labels it with the series' own values.
    bool        colorbar = false;

    // Names the series. Both colourings are keyed, and they are keyed
    // differently:
    //
    //   - a flat series gets its marker shape filled in `color`;
    //   - a `colors` series gets the same shape **filled white with a black
    //     edge**, exactly as a 2D scatter_z does, because a continuous-colour
    //     series has no one colour a swatch could honestly show. The key says
    //     which shape is which series; the bar beside it says what the colours
    //     mean.
    //
    // With a `colors` vector the same string also names the scale on the
    // colorbar, if one was asked for -- one string for one series, since the
    // two explain the same data.
    std::string name;
    bool        show_legend = true;   // see LineOptions::show_legend

    // How an error bar given to scatter3d() is drawn. Style only -- the spans
    // are an ErrorBar3D parameter, which is what keeps them off the snapshot
    // copy.
    ErrorBar3DOptions errorbar;

    // Optional hover text per point, index-aligned with x/y/z, appended below
    // the default "x=.., y=.., z=.." line (which grows a "c=.." when the
    // series has a `colors` vector). Empty or out of range = no custom line.
    std::vector<std::string> hint_labels;
};

// A path through the scene: the |x| points joined in the order given, and the
// 3D counterpart of Axes::plot(). The first stroked *path* in a 3D scene --
// `bar3d`'s edges are the only other geometry here that is stroked at all, and
// they are twelve disjoint segments per bar rather than a polyline.
struct Line3DOptions {
    // The flat colour the whole path takes, unless the series was given a
    // `colors` vector -- which overrides it entirely and ramps each segment
    // through cmap/vmin/vmax below.
    Color color     = Color::Blue;

    // **A length in the scene, not a count of pixels**, which is the one place
    // this parts company with LineOptions::linewidth. A line3d is geometry in
    // the scene with no surface to lie on, which is memory/spec_3d.md §4's
    // world-space billboarded ribbon -- the row written for `bar3d` edges,
    // whose `edge_linewidth` this matches exactly: a pixel width *at the box
    // centre*, converted once to a length in the box, so the number written
    // here is a pixel width somewhere and a world length everywhere. Under a
    // perspective camera a distant stretch of path is therefore thinner, while
    // the axis frame beside it is not.
    float linewidth = 1.5f;

    // **There is no `linestyle`, and that is structural rather than pending.**
    // A dash pattern is an arc length in pixels; a world-space ribbon expands
    // before it projects, so it has no pixel arc length and no per-segment
    // quantity reconstructs one (memory/spec_3d.md §4). A dashed 3D line would
    // mean giving up the width above, not adding a field.

    // Below 1 the path is translucent, with the cost every other 3D kind's
    // alpha has: an opaque series is resolved by the depth buffer in one draw,
    // a translucent one has to be composited against everything else in the
    // scene for the camera it is seen from.
    float alpha     = 1.0f;

    // Close the path with one more segment from the last point back to the
    // first. It draws a segment and nothing else: no point is added, so the
    // limits an auto-scaled axes resolves to are exactly what they were, and
    // with a `colors` vector the closing segment ramps from the last point's
    // colour back to the first's like any other. Step 18 gives Axes::plot()
    // the same option with the same meaning.
    bool  loop      = false;

    // Darken the path toward black with distance from the camera, with exactly
    // the meaning Scatter3DOptions::depthshade has -- 0 draws the path in its
    // own colour, 1 takes the farthest stretch to black -- and `t` normalized
    // over the *box's* depth extent, so two series in one scene shade on one
    // scale. Per-vertex, so it interpolates along a segment for nothing.
    //
    // Off by default: darkening is a change to the colour the caller asked
    // for, and a path is read for where it goes before it is read for how far
    // away it is.
    float depthshade = 0.0f;

    // The colormap a `colors` vector is mapped through, and the range it is
    // normalized over. Read only by the overloads that take one.
    Colormap cmap = Colormap::Viridis;

    // **Equal (the default) means the series' own range**, on exactly the
    // terms Scatter3DOptions::vmin/vmax states: a `colors` vector is a fourth
    // measured quantity in the caller's own units, so a 0..1 default would
    // clip almost every real series to one end of the colormap.
    float vmin = 0.0f, vmax = 0.0f;

    // Draw the scale beside the frame, gated on the series actually having a
    // `colors` vector -- without one every segment is the flat `color`, and a
    // bar would key a mapping the picture does not use.
    bool  colorbar = false;

    // Names the series. Both colourings are keyed, and they are keyed
    // differently:
    //
    //   - a flat series gets a short segment of its own colour;
    //   - a `colors` series gets **the same segment with the colormap swept
    //     along it**, which is the line's form of the white-with-black-edge
    //     marker a colormapped scatter gets (v1.0 step 11.5). A continuous-
    //     colour series has no one colour a swatch could honestly show, so the
    //     swatch shows the scale instead; the key says which series is which,
    //     and the bar beside it says what the values are.
    //
    // A colormapped line is keyed at all -- rather than dropping out of the
    // legend as a colormapped *surface* does -- because a sheet has a shape in
    // the picture to be recognized by and two colormapped paths in one axes
    // have nothing else to tell them apart.
    std::string name;
    bool        show_legend = true;   // see LineOptions::show_legend

    // How an error bar given to line3d() is drawn. Style only -- the spans are
    // an ErrorBar3D parameter. See Scatter3DOptions::errorbar.
    ErrorBar3DOptions errorbar;

    // Optional hover text per point, index-aligned with x/y/z. See
    // Scatter3DOptions::hint_labels; a path is hovered at its *vertices*,
    // since those are the points the caller gave.
    std::vector<std::string> hint_labels;
};

// The box's own furniture, as opposed to the axis annotation drawn on it
// (which is AxesStyle, shared with 2D).
struct Box3DStyle {
    bool  panes           = true;
    Color pane_color      = { 0.94f, 0.94f, 0.96f, 1.0f };
    Color pane_edge_color = { 0.75f, 0.75f, 0.78f, 1.0f };

    // Fraction of the cell's frame left empty around the projected box, for
    // the tick labels and axis titles to sit in.
    //
    // Chosen, not measured -- the one place 3D departs from the rule that
    // decoration space is measured from its text (see spec_architecture.md's
    // "Layout"). It has to be: where a 3D label sits depends on the camera,
    // the camera's fit depends on the frame, and the frame would depend on
    // the label. That is a real circular dependency, unlike the apparent one
    // 2D layout dissolves.
    float margin = 0.12f;
};

// A 2D plane embedded in the 3D scene: one of the box's three axis-aligned
// orientations, at a fixed offset along the third axis, carrying the 2D plot
// kinds.
//
// **A plane's in-plane coordinates are the parent's data coordinates.** A
// plane at XY, offset 0.5 spans the parent's x and y axes at z = 0.5, and a
// heatmap placed on it over x 400..700 occupies 400..700 of the *parent* box.
// The rejected alternative -- independent limits stretched to fill the box
// face -- demos well and is wrong for what planes are for: a field with slices
// and cuts is only meaningful if slice coordinates are scene coordinates. It
// also makes auto-scaling incoherent, whereas this way a plane's data feeds
// the parent's automatic limits directly, along the two axes it spans.
//
// This is deliberately not an `Axes`: internally it holds one (which is what
// makes it affordable at all -- ingest, validation, buffer sharing and the
// snapshot are the 2D code unchanged), but set_xlim, set_xticks and
// set_title mean nothing on an object whose coordinates and annotation belong
// to its parent, so they are not re-exposed. What is here is the ingest and
// the placement.
class SEXTANT_API Plane2D {
public:
    ~Plane2D();

    // The 2D plot kinds, on the plane. Every coordinate is in the *parent's*
    // data coordinates along the two axes this plane spans, in the order the
    // orientation names them: XY -> (x, y), YZ -> (y, z), ZX -> (z, x). Every
    // option means exactly what it means in 2D, and every one of these
    // validates through the same ingest `Axes` uses.
    //
    // What differs is only what a *width* means once the drawing is in a
    // scene (see memory/spec_3d.md §4): a line's `linewidth`, a bar's edge and
    // an error bar's stroke are pixel widths **at the box centre**, converted
    // once to a length in the box, so under perspective a near one comes out
    // thicker and a far one thinner. Marker sizes and contour lines are the
    // other half of that fork and keep a fixed size on screen, because a
    // marker denotes a point and a contour annotates a field -- neither is
    // geometry whose size carries data.
    Plane2D& line(std::span<const double> x, std::span<const double> y,
                  LineOptions opts = {});
    Plane2D& line(std::span<const double> y, LineOptions opts = {});

    Plane2D& scatter(std::span<const double> x, std::span<const double> y,
                     ScatterOptions opts = {});

    Plane2D& scatter_z(std::span<const double> x, std::span<const double> y,
                       std::span<const double> z, ScatterZOptions opts = {});

    Plane2D& bar(std::span<const double> x, std::span<const double> height,
                 BarOptions opts = {});

    // With error bars, exactly as on Axes. See ErrorBar.
    Plane2D& line(std::span<const double> x, std::span<const double> y,
                  const ErrorBar& err, LineOptions opts = {});
    Plane2D& line(std::span<const double> y, const ErrorBar& err, LineOptions opts = {});
    Plane2D& scatter(std::span<const double> x, std::span<const double> y,
                     const ErrorBar& err, ScatterOptions opts = {});
    Plane2D& scatter_z(std::span<const double> x, std::span<const double> y,
                       std::span<const double> z, const ErrorBar& err,
                       ScatterZOptions opts = {});
    Plane2D& bar(std::span<const double> x, std::span<const double> height,
                 const ErrorBar& err, BarOptions opts = {});

    // `xrange`/`yrange` are the mesh's outer edges, as in 2D; a reversed range
    // mirrors that axis and a degenerate one throws. Contours are drawn, and
    // like the box's own annotation they keep a fixed width on screen.
    Plane2D& heatmap(std::span<const float> data, int rows, int cols,
                     Range xrange, Range yrange, HeatmapOptions opts = {});

    // heatmap() over the extent the indices themselves give, exactly as
    // Axes::imshow() is to Axes::heatmap().
    Plane2D& imshow(std::span<const float> data, int rows, int cols,
                    HeatmapOptions opts = {});

    // Where the plane sits along the axis it is normal to, in that axis's own
    // data units. Slides the whole plane; nothing on it moves relative to it.
    Plane2D& set_offset(double offset);

    // Whole-plane opacity, for stacked slices. Multiplies whatever the plot
    // objects on it already carry.
    Plane2D& set_alpha(float alpha);

    Plane2D& cla();

    PlaneOrientation orientation() const;
    double           offset() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    friend class Axes3D;
    friend class Figure;
    Plane2D(PlaneOrientation orient, double offset, Plane2DOptions opts);
};

class SEXTANT_API Axes3D {
public:
    ~Axes3D();

    // ----------------------------------------------------------------
    // Plot types
    // ----------------------------------------------------------------
    // Bars on the grid `u` x `v`, standing along the axis `orient` is normal
    // to. `heights` is |u| x |v| row-major with u as the major index -- the
    // shape a 2D histogram comes out in, which is what a 3D bar chart is for.
    // Throws if a vector is empty or non-finite, or if `heights` is not
    // exactly |u| * |v| long.
    //
    // The second overload gives each bar its own base; `bottoms` is indexed
    // like `heights`. Without it every bar starts at Bar3DOptions::bottom.
    Axes3D& bar3d(PlaneOrientation orient,
                  std::span<const double> u, std::span<const double> v,
                  std::span<const double> heights, Bar3DOptions opts = {});
    Axes3D& bar3d(PlaneOrientation orient,
                  std::span<const double> u, std::span<const double> v,
                  std::span<const double> heights,
                  std::span<const double> bottoms, Bar3DOptions opts = {});

    // A surface over the grid `u` x `v`, rising along the axis `orient` is
    // normal to. `heights` is |u| x |v| row-major with u as the major index --
    // deliberately the identical shape and the identical parameter list to
    // `bar3d` above, because the two answer the same question about the same
    // data and a caller who has laid out one should not have to re-learn the
    // layout for the other. Throws on the same conditions.
    //
    // The grid must be at least 2 x 2: a surface is made of *cells* between
    // neighbouring samples, and a single row of them has none.
    Axes3D& surface(PlaneOrientation orient,
                    std::span<const double> u, std::span<const double> v,
                    std::span<const double> heights, SurfaceOptions opts = {});

    // A sheet on a triangulated mesh: the |x| vertices (x[i], y[i], z[i]) and
    // the topology joining them. `surface` above needs a rectangular `u x v`
    // grid; this takes what a mesh out of a solver, a scan or a mesh file
    // actually is.
    //
    // **`tri` is 3M indices, row-major -- three per triangle.** That is what
    // every library agrees on once the artefacts are stripped off:
    // matplotlib's `triangles` (M,3), MATLAB's `T`, libigl/Open3D's `F` and
    // three.js's index array are the same bytes; Plotly's parallel i/j/k is a
    // JSON schema artefact and VTK's leading per-face count exists only
    // because VTK cells are not all triangles. `std::uint32_t` because it is
    // the one unambiguous "this is an index" type -- the indices are consumed
    // at plan time and never uploaded (a per-face normal and shade cannot be
    // shared between faces, so the mesh expands exactly as `surface`'s cells
    // already do), which leaves the type free to be chosen for clarity.
    //
    // Throws if a vector is empty or non-finite, if the three coordinate
    // lengths differ, if there are fewer than three vertices, if `tri` is
    // empty or not a multiple of three, or if any index is >= |x|. Degenerate
    // (zero-area) triangles are **kept**, with the normal guarded, rather than
    // dropped: silently dropping rows would make the caller's face count and
    // ours disagree, and a face count is how a caller checks their own mesh.
    Axes3D& surface_tri(std::span<const double> x, std::span<const double> y,
                        std::span<const double> z,
                        std::span<const std::uint32_t> tri,
                        SurfaceTriOptions opts = {});

    // The same, with a fourth dimension colouring the mesh through
    // opts.cmap/vmin/vmax -- which overrides opts.color entirely and lets the
    // mesh ask for a colorbar. `colors` is indexed like x/y/z and must be the
    // same length; **a triangle interpolates between its three vertices'
    // values**, so the value a caller gives belongs to the vertex they gave it
    // for rather than to one of the faces meeting there. Empty is exactly the
    // overload above.
    //
    // A span rather than a field of SurfaceTriOptions, for the reason
    // Scatter3DOptions' own `colors` documents: `opts` is deep-copied into
    // every snapshot, so a per-vertex vector there is a memcpy of the whole
    // mesh on every frame of a pan.
    Axes3D& surface_tri(std::span<const double> x, std::span<const double> y,
                        std::span<const double> z,
                        std::span<const std::uint32_t> tri,
                        std::span<const double> colors,
                        SurfaceTriOptions opts = {});

    // The same two, with the topology **derived** instead of given: a Delaunay
    // triangulation of the vertices projected onto the plane `orient` names.
    //
    // `orient` is a parameter rather than matplotlib's hardcoded x-y because
    // Plotly had to bolt on `delaunayaxis` later to fix exactly that. It says
    // how the mesh is *made* and nothing about the drawing consults it
    // afterwards -- unlike `bar3d`/`surface`, where the orientation names an
    // axis the data stands on. An index span and an enum are distinct types,
    // so overload resolution never has to guess which of the four this is.
    //
    // **The triangulation runs once, at ingest, and what is stored is
    // indices**: downstream there is then exactly one kind of mesh, the cost
    // is paid once rather than per frame, and the Data panel has real topology
    // to show. One consequence taken deliberately -- **editing a vertex does
    // not re-triangulate**. An edit moves a point; it does not re-mesh, and
    // topology jumping under a drag would be worse than a mesh that deforms.
    //
    // Three boundaries worth knowing before the call:
    //   - **duplicate points** are deduplicated for the triangulation only and
    //     keep their slots in the vertex array, since renumbering would break
    //     `colors` and `hint_labels`;
    //   - **collinear input** has no triangulation and throws here, rather
    //     than rendering an empty box;
    //   - **a concave domain gets spanned**, because Delaunay fills the convex
    //     hull. matplotlib answers that with `Triangulation.mask`; masking is
    //     deferred, so this is a boundary rather than a bug -- give `tri`
    //     explicitly when the domain is not convex.
    Axes3D& surface_tri(std::span<const double> x, std::span<const double> y,
                        std::span<const double> z, PlaneOrientation orient,
                        SurfaceTriOptions opts = {});

    Axes3D& surface_tri(std::span<const double> x, std::span<const double> y,
                        std::span<const double> z, PlaneOrientation orient,
                        std::span<const double> colors,
                        SurfaceTriOptions opts = {});

    // Markers at the |x| points (x[i], y[i], z[i]). Throws if a vector is
    // empty or non-finite, if the three lengths differ, or if a non-empty
    // ErrorBar3D span does not hold one entry per point.
    //
    // Unlike `bar3d` and `surface` there is no orientation: a scatter is not
    // laid out on a grid standing on one pair of axes, so all three
    // coordinates are given directly and none of them is privileged.
    //
    // **Four overloads, because `colors` and `err` are independently
    // optional**, and `opts` is last in every one of them -- "data first,
    // `opts` last" holds in every method this library has, and breaking it in
    // the two that take error bars would cost a reader more than six
    // declarations do. The one ambiguity is a bare `scatter3d(x, y, z, {},
    // opts)`, which cannot tell the two middle overloads apart; that is a
    // compile error rather than a silent wrong pick, and any named brace
    // (`{.z_cap_lo = e}`) or real vector resolves it.
    Axes3D& scatter3d(std::span<const double> x, std::span<const double> y,
                      std::span<const double> z, Scatter3DOptions opts = {});

    // The same, with error bars. See ErrorBar3D on why the spans are a
    // parameter rather than a field of `opts`.
    Axes3D& scatter3d(std::span<const double> x, std::span<const double> y,
                      std::span<const double> z, const ErrorBar3D& err,
                      Scatter3DOptions opts = {});

    // The same, with a fourth dimension colouring each marker through
    // opts.cmap/vmin/vmax -- which overrides opts.color entirely, and is what
    // switches this series' legend key to the white-with-black-edge form and
    // lets it ask for a colorbar. `colors` is indexed like x/y/z and must be
    // the same length; empty is exactly the overload above.
    //
    // A span rather than a field of Scatter3DOptions, and that is not a
    // stylistic choice: `opts` is deep-copied into every snapshot the render
    // thread takes, so a per-point vector parked there would put a memcpy of
    // the whole series on every frame of a pan. Same reason `bar3d` takes its
    // `bottoms` this way. (`hint_labels` is the standing exception: it is
    // per-point data in an options struct, and is moved out at ingest.)
    Axes3D& scatter3d(std::span<const double> x, std::span<const double> y,
                      std::span<const double> z, std::span<const double> colors,
                      Scatter3DOptions opts = {});

    // Both at once.
    Axes3D& scatter3d(std::span<const double> x, std::span<const double> y,
                      std::span<const double> z, std::span<const double> colors,
                      const ErrorBar3D& err, Scatter3DOptions opts = {});

    // A path through the |x| points (x[i], y[i], z[i]), joined in the order
    // given -- Axes::plot()'s 3D counterpart. Throws on the same conditions
    // scatter3d does, with one addition: a path needs at least two points,
    // where a cloud of one is a picture of one point.
    //
    // Like scatter3d and unlike bar3d/surface there is no orientation: a path
    // is not laid out on a grid, so all three coordinates are given directly.
    // The four overloads are scatter3d's four, for the same reasons.
    Axes3D& line3d(std::span<const double> x, std::span<const double> y,
                   std::span<const double> z, Line3DOptions opts = {});

    // With error bars. See ErrorBar3D.
    Axes3D& line3d(std::span<const double> x, std::span<const double> y,
                   std::span<const double> z, const ErrorBar3D& err,
                   Line3DOptions opts = {});

    // With a fourth dimension colouring the path through opts.cmap/vmin/vmax,
    // which overrides opts.color entirely, sweeps the legend key and lets the
    // series ask for a colorbar. `colors` is indexed like x/y/z and must be
    // the same length; **a segment ramps between its two endpoints' colours**,
    // so the value a caller gives belongs to the point they gave it for rather
    // than to one of the two segments meeting there. Empty is exactly the
    // overload above.
    //
    // A span rather than a field of Line3DOptions, for the reason
    // Scatter3DOptions' own `colors` documents: `opts` is deep-copied into
    // every snapshot, so a per-point vector there is a memcpy of the whole
    // series on every frame of a pan.
    Axes3D& line3d(std::span<const double> x, std::span<const double> y,
                   std::span<const double> z, std::span<const double> colors,
                   Line3DOptions opts = {});

    // Both at once.
    Axes3D& line3d(std::span<const double> x, std::span<const double> y,
                   std::span<const double> z, std::span<const double> colors,
                   const ErrorBar3D& err, Line3DOptions opts = {});

    // A 2D plane in the scene, spanning the two box axes `orient` names at
    // `offset` along the third, in that axis's own data units. Returns the
    // plane rather than *this, because what happens next is drawing on it:
    //
    //     ax3->plane(PlaneOrientation::XY, 0.5)->heatmap(field, r, c, {400,700}, {-1,1});
    //
    // Unlike the plot methods above, an axes may hold any number of planes and
    // they are drawn in the order added (their depth order is resolved per
    // frame, so that only decides ties). Throws on a non-finite offset.
    std::shared_ptr<Plane2D> plane(PlaneOrientation orient, double offset,
                                   Plane2DOptions opts = {});

    // ----------------------------------------------------------------
    // Decoration -- all return *this for chaining, like Axes
    // ----------------------------------------------------------------
    Axes3D& set_title(std::string_view text, float fontsize = 18.0f);
    Axes3D& set_xtitle(std::string_view text, float fontsize = 16.5f);
    Axes3D& set_ytitle(std::string_view text, float fontsize = 16.5f);
    Axes3D& set_ztitle(std::string_view text, float fontsize = 16.5f);

    Axes3D& set_xlim(double lo, double hi);
    Axes3D& set_ylim(double lo, double hi);
    Axes3D& set_zlim(double lo, double hi);

    Axes3D& set_xticks(std::span<const double> positions,
                       std::vector<std::string> labels = {});
    Axes3D& set_yticks(std::span<const double> positions,
                       std::vector<std::string> labels = {});
    Axes3D& set_zticks(std::span<const double> positions,
                       std::vector<std::string> labels = {});

    // Grid lines are drawn on the three back panes, at the tick positions of
    // the two axes each pane spans. Unlike 2D, they default to on: without
    // them a pane is a blank wall and there is nothing to read a depth off.
    Axes3D& grid(bool enable = true, GridOptions opts = {});

    Axes3D& set_axes_style(AxesStyle opts = {});
    Axes3D& set_box_style(Box3DStyle opts = {});
    Axes3D& set_box_aspect(BoxAspect aspect);

    // The legend keys every named series of this axes' own kinds, then of each
    // of its planes in plane order. It belongs to the axes rather than to a
    // plane for the same reason it is drawn beside the frame rather than in
    // the scene: one cell has one legend, and two planes asking for two boxes
    // is not something a figure can lay out.
    Axes3D& legend(LegendOptions opts = {});

    // Styling only, exactly as Axes::set_colorbar_style() is -- a colorbar is
    // drawn when a plot object asks for one (HeatmapOptions::colorbar on a
    // plane, and every kind that grows the flag), not by this call.
    //
    // Here rather than on Plane2D, and for the legend's reason: a bar is drawn
    // beside the *cell's* frame, so the several bars a cell can carry are one
    // piece of furniture and have to look alike. Before v1.0 step 11.2 the
    // styling was hoisted off whichever plane had asked -- which meant it was
    // always the default, since a Plane2D has no setter to reach the
    // ColorbarOptions it holds, and it had no answer at all for a bar
    // requested by something that is not on a plane.
    Axes3D& set_colorbar_style(ColorbarOptions opts = {});

    // ----------------------------------------------------------------
    // Camera
    // ----------------------------------------------------------------
    // Elevation is clamped to +-89 degrees (see Camera3D).
    Axes3D& set_view(double azimuth_deg, double elevation_deg);
    Axes3D& set_camera(Camera3D cam);
    Camera3D camera() const;

    // The projection mode and, under perspective, how much of it there is.
    // Neither changes how large the box is drawn -- it is fitted to the cell
    // either way -- so switching between them is a change of depth cue, not
    // of scale. Field of view is clamped to [5, 120] degrees.
    Axes3D& set_projection(Projection mode);
    Axes3D& set_fov(double degrees);

    // The camera a double-click returns to. Separate from set_camera() on
    // purpose: a scripted fly-through, or a drag in the window, must not
    // redefine what "reset" means.
    Axes3D& set_default_camera(Camera3D cam);

    Axes3D& cla();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    friend class Figure;
    explicit Axes3D();
};

} // namespace sextant
