#pragma once
#include "plot_rect.h"
#include "../plot_objects.h"
#include "../coord_transform.h"
#include "../coord_transform3d.h"
#include "plane2d.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sextant {

class DataRenderer {
public:
    DataRenderer();
    ~DataRenderer();

    DataRenderer(const DataRenderer&) = delete;
    DataRenderer& operator=(const DataRenderer&) = delete;

    // Ratio of framebuffer pixels to the logical pixels every draw_*() works
    // in -- the supersample factor, set once per frame by render_frame().
    // Vertex positions need no adjustment (they reach NDC via a logical
    // uResolution), but the two pieces of GL state measured in real
    // framebuffer pixels do: the scissor rectangle and the line width.
    void set_pixel_ratio(float ratio) { pixel_ratio_ = ratio > 0.0f ? ratio : 1.0f; }

    // Identifies which snapshot, and which axes within it, the following
    // draw_*() calls belong to — the invalidation key for the geometry and
    // texture caches below. Set once per axes per frame by render_frame().
    // A generation of 0 disables caching (see FigureSnapshot::generation).
    void set_frame_key(unsigned long long data_generation, int axes_index) {
        data_generation_ = data_generation;
        axes_index_ = axes_index;
    }

    void draw_lines(const std::vector<LinePlot>& lines,
                    const CoordTransform& tr, const PlotRect& pr);

    void draw_scatter(const std::vector<ScatterPlot>& scatters,
                      const CoordTransform& tr, const PlotRect& pr);

    // Continuous-color scatter — per-point color from opts.cmap/vmin/vmax,
    // computed on CPU (like draw_heatmap's colormap LUT lookup) and uploaded
    // as a per-instance color attribute, since this needs a separate
    // shader/VAO from draw_scatter's single-uniform-color one.
    void draw_scatter_z(const std::vector<ScatterZPlot>& points,
                        const CoordTransform& tr, const PlotRect& pr);

    void draw_bars(const std::vector<BarPlot>& bars,
                   const CoordTransform& tr, const PlotRect& pr);

    void draw_heatmap(const std::vector<HeatmapPlot>& heatmaps,
                      const CoordTransform& tr, const PlotRect& pr);

    // Error bars for all four kinds that carry them, in one call and one
    // program bind: the geometry is identical whether it hangs off x/y or off
    // centers/heights, so splitting it per kind would duplicate the builder
    // and the cache four ways. Called after the fills so a bar's error bar
    // sits over its own bar, and before draw_scatter so markers stay on top.
    void draw_error_bars(const AllPlotData& all,
                         const CoordTransform& tr, const PlotRect& pr);

    // The whole 3D scene: planes, bar grids and surfaces, in the two phases
    // the depth buffer divides everything into.
    //
    // **One entry point rather than three calls in a sequence**, and that is
    // the whole reason it exists. Until this existed, render_frame() called
    // the three per-kind draws in a fixed order, each sorting only its *own*
    // objects -- so a translucent surface painted after a translucent bar grid
    // whatever the camera said, and an opaque surface painted *over* a
    // translucent bar in front of it, because the bars had written no depth.
    // A fixed call sequence is not an order; it only looks like one while the
    // scene holds a single kind.
    //
    // Phase 1 is everything opaque, in any order, resolved per fragment by the
    // depth buffer. Phase 2 is everything translucent in one back-to-front
    // list *across* the kinds, by the object distance all three already answer
    // in (see eye_coord()). Whole-object granularity, which is exact for
    // objects that do not interpenetrate and is memory/spec_3d.md §10's named
    // limitation for objects that do -- but it is now applied to every pair
    // rather than to pairs of the same kind.
    void draw_scene3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                      const PlotRect& pr, float win_w, float win_h);

    // Depth-peeling layers for this renderer, overriding the environment and
    // the default. 0 means "no override". Set for the duration of one PNG
    // export (PngExportOptions::peel_layers) and restored afterwards, since
    // this same renderer is usually the live window's -- see PeelLayerScope in
    // figure_export.cpp.
    int  peel_layers_override() const { return peel_layers_override_; }
    void set_peel_layers_override(int n) { peel_layers_override_ = n; }

private:
    // Which half of the scene a per-kind draw is for. Opaque objects are drawn
    // first and resolved by the depth buffer; translucent ones after, with
    // depth writes off, so a plane in front of a bar blends over it and one
    // behind is correctly rejected by the depth the bar wrote.
    //
    // A plane's translucent half is no longer a back-to-front draw of K quads:
    // since 7a a plane is one flat quad with a single depth per pixel, which
    // is exactly the condition under which K of them can be sorted *at the
    // fragment* rather than as whole objects -- see composite_planes3d().
    enum class ScenePass { Opaque, Translucent };

    // The three per-kind draws, private since post-step-7d because the only correct
    // way to sequence them is draw_scene3d(). Each takes an optional explicit
    // `order`: the object indices to draw, already filtered to `pass` and
    // already sorted. Null means "every object of this pass, in this kind's
    // own order", which is what the opaque phase wants and what each of them
    // did for itself before there was a scene-wide order to obey.

    // 3D bars. The one call here that takes a Projector3D instead of a
    // CoordTransform, and the one that depth-tests: everything else in this
    // class is a painter's-algorithm draw in a fixed order, which is exactly
    // what a scene cannot be.
    //
    // The vertex buffer holds *data*-space corners offset by a per-plot
    // anchor, with the whole projection -- limits, aspect, camera, fit,
    // viewport -- arriving as one matrix from Projector3D::clip_matrix(). So
    // orbiting invalidates nothing, and neither does a limit change; only the
    // data does. That is the payoff of the coordinate chain (spec_3d.md §2).
    void draw_bars3d(const std::vector<Bar3DPlot>& bars, const Projector3D& proj,
                     const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                     const std::vector<std::size_t>* order = nullptr);

    // 3D surfaces (step 7c). Beside draw_bars3d() rather than folded into it:
    // the two share the grid, the light and the buffer discipline, and nothing
    // else -- a bar is a solid whose hidden faces can be dropped, a surface is
    // a sheet that has to be drawn from both sides.
    //
    // An *opaque* surface is one unordered draw resolved by the depth buffer,
    // which is why 7c needs nothing from 7a or 7b. A translucent one is sorted
    // back to front by its own grid, exactly as a translucent bar grid is, and
    // is ordered against everything else as a whole object by draw_scene3d().
    void draw_surfaces3d(const std::vector<SurfacePlot>& surfaces, const Projector3D& proj,
                         const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                         const std::vector<std::size_t>* order = nullptr);

    // The 2D kinds on a plane: the heatmap quad (step 5), and the filled
    // areas, world-space strokes and pixel-sized markers everything else
    // reduces to (step 6, via plane_geometry()).
    //
    // Like draw_bars3d(), every buffer is in data space and the whole
    // projection arrives as one matrix, so an orbit re-uploads nothing. The
    // heatmap quad is the exception and is rebuilt per frame, because four
    // vertices are not worth a cache; its *texture* is cached, keyed as the 2D
    // heatmap's is and living in the same map under a plane index.
    //
    // Within one plane the batches are drawn in the 2D painter order rather
    // than one pass per primitive kind: every primitive on a plane is exactly
    // coplanar, so no depth test can recover the layering and it has to be
    // drawn in it (see PlaneGeometry::Batch).

    void draw_planes3d(const std::vector<PlaneSnapshot>& planes, const Projector3D& proj,
                       const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                       const std::vector<std::size_t>* order = nullptr);

    // 3D scatter clouds (v1.0 step 12). The one draw here whose primitives are
    // *symbols* rather than geometry: pixel-sized billboards carrying the
    // point's own depth, so the depth buffer resolves them against the bars,
    // sheets and planes exactly as if they were solid, while the camera never
    // changes how big they are.
    //
    // Translucent and unpeeled is the one case in this scene where a sort is
    // exact rather than a heuristic -- flat billboards cannot interpenetrate,
    // so back-to-front by the point's depth is the answer and not an
    // approximation of one. See scatter3d_draw_order().
    void draw_scatter3d(const std::vector<Scatter3DPlot>& points, const Projector3D& proj,
                        const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                        const std::vector<std::size_t>* order = nullptr);

    // Triangulated meshes (v1.0 step 14). draw_surfaces3d()'s shape with the
    // two differences a mesh has: the colour is per *vertex* and looked up per
    // fragment (a face's three corners generally carry three different
    // values), and the face order for a translucent unpeeled draw is a
    // heuristic rather than exact -- mesh faces have no separating plane and a
    // mesh can fold over itself. See surface_tri_draw_order().
    void draw_surface_tri3d(const std::vector<SurfaceTriPlot>& meshes,
                            const Projector3D& proj, const PlotRect& pr,
                            float win_w, float win_h, ScenePass pass,
                            const std::vector<std::size_t>* order = nullptr);

    // Paths, as world-space ribbons with mitered joins (v1.0 step 13). The
    // `order` argument is the path list's order, as it is for the three kinds
    // above; the order of the *segments within* a translucent path is
    // line3d_draw_order()'s, and unlike a cloud's it is a heuristic -- two
    // ribbons of one path can cross. Peeling answers per fragment and consults
    // neither.
    void draw_lines3d(const std::vector<Line3DPlot>& lines, const Projector3D& proj,
                      const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                      const std::vector<std::size_t>* order = nullptr);

    // The scatter3d and line3d error bars (v1.0 step 17), as the triangles of
    // errorbar3d_pieces(): ribbons for the whiskers, caps and block edges, two
    // triangles per block face. `order` indexes the scene's error-bar series,
    // scatter clouds first and then paths (`scatter3d.size() + i` for path i).
    // The opaque pass draws each series' opaque pieces, the translucent pass
    // the rest -- sorted back to front by piece when unpeeled, which is a
    // heuristic for crossing block faces and exact for everything else.
    void draw_errorbars3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                          const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                          const std::vector<std::size_t>* order = nullptr);

    // Common per-draw GL state for the data pass — see begin_pass()'s comment
    // in the .cpp for why blending in particular has to be set explicitly.
    void begin_pass(const PlotRect& pr, float win_h) const;
    void end_pass() const;

    // One plane's contents, rendered into that plane's own framebuffer (step
    // 7a). Everything on the plane goes through the *2D* draw calls above, in
    // the order a 2D axes draws them, which is the whole of what makes a
    // plane's layering right: it is the same order, run by the same code.
    // Leaves the previously bound framebuffer, viewport and scissor restored.
    struct PlaneRasterCache;   // defined with the other caches below
    void render_plane_raster(const PlaneSnapshot& pl, int plane_index,
                             const PlaneRaster& raster, PlaneRasterCache& c);

    // One group of translucent planes, composited *exactly*, per pixel (step
    // 7b). Draws the plot rect once per slot, from the far end forwards: at
    // each fragment the shader casts the pixel's ray at all K planes, sorts
    // the hits by depth and emits the slot-th of them, with `gl_FragDepth` set
    // to that hit's own depth. So the ordinary depth test rejects exactly the
    // samples an opaque object hides -- no depth texture, no accumulation
    // buffer -- and the fixed-function blend sees each fragment's samples in
    // sorted order, which is what makes the composite right on both sides of
    // an intersection line instead of on whichever side the whole-object
    // heuristic picked. `group` indexes `planes` and `rasters`; every plane in
    // it must have a valid raster.
    void composite_planes3d(const std::vector<PlaneSnapshot>& planes,
                            const std::vector<PlaneRaster>& rasters,
                            const std::vector<std::size_t>& group,
                            const Projector3D& proj, const PlotRect& pr,
                            float win_w, float win_h);

    // Every translucent plane's raster, rendered before a peel begins.
    //
    // Not an optimisation. render_plane_raster() *draws*, and a draw made
    // while an occlusion query is running is counted by it -- so a raster
    // rendered during pass 0 and served from the cache during pass 1 would
    // give peel_translucent3d()'s early-out two different answers to the same
    // question. Hoisting it out is what makes that answer mean only what it
    // is read as meaning: whether this pass peeled a layer.
    void prepare_plane_rasters(const std::vector<PlaneSnapshot>& planes,
                               const Projector3D& proj);

    // ---- Depth peeling (step 8) ------------------------------------------
    //
    // The raster path's answer to translucent ordering, and it sorts nothing.
    // Pass 0 draws every translucent primitive with an ordinary depth test and
    // keeps the winning depth; pass n re-draws them all and discards any
    // fragment at or nearer than pass n-1's depth, so the nearest *remaining*
    // fragment wins and the passes deliver the layers strictly front to back.
    // Order stops being something anyone answers -- it is what the depth test
    // produces, per pixel -- which is why this is exact in the cases where
    // memory/spec_impl.md's step-8 note proves no sort can be: two objects
    // whose depth ranges coincide, and the pinwheel, which has no total order
    // at any granularity to sort into.
    //
    // The layer count is bounded by *alpha* rather than by geometry: after n
    // layers at alpha a the transmittance left is (1-a)^n, and eight layers
    // are under ~1% for every alpha -- low alpha decays slowly but contributes
    // little per layer, high alpha converges in two or three. An occlusion
    // query ends the loop as soon as a pass peels nothing, so a two-layer
    // scene costs three passes and not eight.
    //
    // False when the targets could not be built, which is the caller's signal
    // to fall back to the whole-object order post-step-7d left behind.
    bool peel_translucent3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                            const PlotRect& pr, float win_w, float win_h);

    // One textured quad, in NDC, for the peel's two composites. `flip_alpha`
    // is the whole difference between them: the accumulation's alpha channel
    // carries *transmittance*, and the composite that puts it over the scene
    // wants coverage.
    void draw_peel_quad(unsigned int tex, bool flip_alpha,
                        float x0, float y0, float x1, float y1) const;

    float pixel_ratio_ = 1.0f;

    // ---- Per-frame caches ------------------------------------------------
    //
    // Both key on (data generation, axes, plot index) plus whatever else the
    // result depends on, and are held per DataRenderer, i.e. per window
    // thread. Without them the pass re-derives and re-uploads every vertex and
    // every heatmap texture each frame whether or not anything changed.
    unsigned long long data_generation_ = 0;
    int                axes_index_ = -1;
    // Which plane the 2D draw calls are currently drawing *into* -- -1 at the
    // axes, the plane's position while render_plane_raster() is running. It is
    // in every 2D cache key, which is what lets three planes each carrying a
    // line at index 0 keep three cache entries instead of invalidating one
    // entry three times a frame.
    int                plane_index_ = -1;

    // Identifies one plot object across frames. Plot indices are positional
    // within an axes, so this is stable exactly as long as the plot list is.
    //
    // `plane_index` is -1 for a plot object owned by the axes itself -- the
    // only form a 2D slot ever produces -- and the plane's position otherwise.
    // It is the same extra field the cross-thread edit scheme takes on to
    // address a plane (spec_3d.md §6), and it is here so that a plane's
    // heatmap and the axes' own heatmap at the same index do not share one
    // cache entry and invalidate each other every frame.
    struct CacheKey {
        int axes_index  = -1;
        int plane_index = -1;
        int plot_index  = -1;
        bool operator==(const CacheKey& o) const {
            return axes_index == o.axes_index && plane_index == o.plane_index
                && plot_index == o.plot_index;
        }
    };
    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& k) const {
            return (static_cast<std::size_t>(static_cast<unsigned>(k.axes_index)) << 40)
                 ^ (static_cast<std::size_t>(static_cast<unsigned>(k.plane_index)) << 20)
                 ^ static_cast<unsigned>(k.plot_index);
        }
    };

    // Stroke *points*, not expanded geometry: the shader turns each segment
    // into a quad, so this buffer is view-independent like scatter's and the
    // stroke width is a uniform. Padded with a duplicate of the first and last
    // point so every instance can read prev/p0/p1/next.
    struct LineCache {
        unsigned int       vbo        = 0;
        int                segments   = 0;   // instance count = segment_count()
        unsigned long long data_generation = 0;
        // `loop` is in the key because it is baked into the buffer: the two
        // pad points a closed path carries are its real neighbours across the
        // seam, where an open one repeats its own ends. Toggling it therefore
        // has to rebuild, which no data change would report.
        bool               loop       = false;
        double             anchor_x = 0.0, anchor_y = 0.0;
        double             span_x = 0.0, span_y = 0.0;
        bool               data_space = true;
        CoordTransform     tr{};             // only consulted when !data_space

        // Dash phase: cumulative arc length in logical pixels, one float per
        // point, feeding the stroke shader's aDist. Built only for a non-solid
        // linestyle, so a solid line allocates nothing extra.
        //
        // Arc length is the one quantity here that genuinely depends on the
        // view -- a sum of sqrt((sx*dx)^2 + (sy*dy)^2), which no prefix sum
        // over data-space points can reconstruct once sx/sy change. So it is
        // stored at the scale it was built at and reused whenever the current
        // scale is a *uniform* multiple of that one, covering panning and
        // symmetric zoom. Only an aspect-ratio change rebuilds it.
        unsigned int       dist_vbo    = 0;
        bool               dist_valid  = false;
        float              dist_ref_sx = 0.0f, dist_ref_sy = 0.0f;
    };
    std::unordered_map<CacheKey, LineCache, CacheKeyHash> line_cache_;

    // Colormapped heatmap texture. Independent of the transform — the quad
    // is re-derived each frame (24 floats) but the texture is not.
    struct HeatCache {
        unsigned int       tex        = 0;
        unsigned long long data_generation = 0;
        Colormap           cmap       = Colormap::Viridis;
        float              vmin = 0.0f, vmax = 0.0f;
        bool               flip = false;
        int                rows = 0, cols = 0;
    };
    std::unordered_map<CacheKey, HeatCache, CacheKeyHash> heat_cache_;

    // Uploads `hp`'s colormapped pixels into `hc` unless the entry already
    // holds them, and leaves the texture bound on unit 0 either way. Shared by
    // the 2D quad and the plane quad, so the same heatmap drawn on an axes and
    // on a plane is provably the same texels -- which is the claim step 5
    // exists to make and would be two colormap loops away from being untrue.
    void ensure_heatmap_texture(HeatCache& hc, const HeatmapPlot& hp);

    // Scatter/scatter_z instance buffers, held in *data* space offset by
    // `anchor`, so the transform lives in a uniform and the buffer survives
    // pan and zoom -- unlike LineCache, the key carries no transform.
    //
    // `data_space == false` is the precision fallback: at deep zoom a float
    // cannot resolve the view finely enough against the data's own span, so
    // the CPU writes pixel-space centres and the shader's transform is
    // identity. That path does depend on the view, hence `tr` in the key.
    struct InstanceCache {
        unsigned int       vbo        = 0;
        int                instances  = 0;
        unsigned long long data_generation = 0;
        double             anchor_x = 0.0, anchor_y = 0.0;
        double             span_x = 0.0, span_y = 0.0;   // for the precision test
        float              size = 0.0f;                  // marker size is baked in
        bool               data_space = true;
        // Scatter_z only: the colour mapping is baked into the buffer too.
        Colormap           cmap = Colormap::Viridis;
        float              vmin = 0.0f, vmax = 0.0f, alpha = -1.0f;
        CoordTransform     tr{};                         // only when !data_space
    };
    std::unordered_map<CacheKey, InstanceCache, CacheKeyHash> scatter_cache_;
    std::unordered_map<CacheKey, InstanceCache, CacheKeyHash> scatterz_cache_;

    // Bars, in two halves with different invalidation rules. Fills are quads
    // in data space and survive pan/zoom; outlines inset by linewidth/2 in
    // *pixel* space, which has no data-space form, so they carry the
    // transform in their key.
    struct BarCache {
        unsigned int       fill_vbo = 0;   int fill_verts = 0;
        unsigned int       edge_vbo = 0;   int edge_verts = 0;
        unsigned long long fill_generation = 0;
        unsigned long long edge_generation = 0;
        double             bar_width = -1.0;
        double             anchor_x = 0.0, anchor_y = 0.0;
        double             span_x = 0.0, span_y = 0.0;
        bool               data_space = true;
        CoordTransform     fill_tr{};      // only consulted when !data_space
        CoordTransform     edge_tr{};
        float              linewidth = -1.0f;
        float              pixel_ratio = 0.0f;
    };
    std::unordered_map<CacheKey, BarCache, CacheKeyHash> bar_cache_;

    // Error bars. Caps and the box's fallback width are measured in pixels, so
    // the geometry is pixel-space like a bar outline and carries the transform
    // in its key. Two buffers per plot object, because the box interior draws
    // at `box_alpha` while everything else is opaque and the flat program
    // takes one color per draw. One map per *kind*, because CacheKey is only
    // (axes, plot index): a line and a bar both at index 0 would otherwise
    // share an entry and invalidate each other every frame.
    struct ErrCache {
        unsigned int       fill_vbo   = 0;   int fill_verts   = 0;
        unsigned int       stroke_vbo = 0;   int stroke_verts = 0;
        unsigned long long generation = 0;
        CoordTransform     tr{};
        float              linewidth = -1.0f;
        float              capsize   = -1.0f;
        int                capstyle  = -1;
        float              boxwidth  = -1.0f;
        float              box_alpha = -1.0f;
        float              pixel_ratio = 0.0f;
    };
    std::unordered_map<CacheKey, ErrCache, CacheKeyHash> line_err_cache_;
    std::unordered_map<CacheKey, ErrCache, CacheKeyHash> bar_err_cache_;
    std::unordered_map<CacheKey, ErrCache, CacheKeyHash> scatter_err_cache_;
    std::unordered_map<CacheKey, ErrCache, CacheKeyHash> scatterz_err_cache_;

    // 3D bar geometry: 36 vertices per bar, each (data-space corner - anchor,
    // face shade). No camera and no transform in the key -- both are uniforms
    // -- so a whole orbit costs one glDrawArrays per plot object and no
    // uploads. What *is* in the key is everything baked into the buffer: the
    // data generation, the resolved footprints, the base, the shading, and
    // the three axis signs, since a reversed limit mirrors an axis and so
    // changes which faces are lit.
    struct Bar3DCache {
        unsigned int       vbo   = 0;
        int                verts = 0;
        unsigned long long data_generation = 0;
        Vec3               anchor{};
        double             u_width = -1.0, v_width = -1.0, bottom = 0.0;
        float              shading = -1.0f;
        int                axis_signs = 0;
        // Edge ribbons: box-space segment endpoints, expanded to a quad in the
        // vertex shader. Same key, one more buffer, built only when the plot
        // asks for edges.
        //
        // `edges` is what the buffer was *built* for, and it is in the key for
        // the reason everything else baked into a buffer is: an edge VBO that
        // exists is not the same thing as an edge VBO that holds this plot's
        // edges. Turning edges off rebuilds without them, leaving `edge_segs`
        // at 0 and the VBO allocated -- so a test that asks only whether the
        // VBO exists never fires again, and the wireframe cannot be turned
        // back on. See the stale test in draw_bars3d().
        bool               edges      = false;
        unsigned int       edge_vbo   = 0;
        int                edge_segs  = 0;
        // Translucent plots only: the draw order, which unlike everything else
        // here *is* camera-dependent and so is rebuilt every frame. That is the
        // cost of alpha -- an opaque scene is resolved by the depth buffer in
        // one unordered draw, and a translucent one has to be sorted for the
        // camera it is being seen from.
        unsigned int       index_ebo  = 0;
        int                indices    = 0;
    };
    std::unordered_map<CacheKey, Bar3DCache, CacheKeyHash> bar3d_cache_;

    // 3D surface geometry: 6 vertices per cell, each (data-space corner -
    // anchor, RGBA). The colour is per *vertex* in the buffer and constant
    // within a cell, which is flat shading done with the attributes GL has
    // rather than with a `flat` qualifier -- and it is what keeps the raster
    // and the vector paths agreeing, since the SVG emits one fill per cell and
    // an interpolated one would have no counterpart there.
    //
    // Keyed like Bar3DCache and for the same reason: nothing about the camera
    // is in the buffer, so an orbit costs one glDrawArrays and no uploads.
    // What is in the key is everything baked in -- the data generation, the
    // shading, the colour source, and the axis signs, since a reversed limit
    // mirrors an axis and so changes how a cell is tilted and lit.
    struct SurfaceCache {
        unsigned int       vbo   = 0;
        int                verts = 0;
        unsigned long long data_generation = 0;
        Vec3               anchor{};
        float              shading = -1.0f;
        float              alpha   = -1.0f;
        Color              color{ 0, 0, 0, 0 };
        bool               colormap = false;
        Colormap           cmap = Colormap::Viridis;
        double             vmin = 0.0, vmax = 0.0;
        int                axis_signs = 0;
        // The wireframe, keyed for Bar3DCache::edges' reason and against the
        // same defect: this one was reachable from the Cosmetic panel by
        // turning "Colour by height" on while "Wireframe" was off, which
        // rebuilt the buffer without edges and left nothing that could ever
        // ask for them again.
        bool               edges     = false;
        unsigned int       edge_vbo  = 0;
        int                edge_segs = 0;
        unsigned int       index_ebo = 0;
        int                indices   = 0;
    };
    std::unordered_map<CacheKey, SurfaceCache, CacheKeyHash> surface_cache_;

    // 3D mesh geometry: 3 vertices per face, each (data-space corner - anchor,
    // RGBA, normalized colour value, face shade). Expanded rather than indexed,
    // even though the caller gave an index array -- a per-face normal and shade
    // cannot be shared between the faces meeting at a vertex, so the mesh has
    // to expand exactly as SurfaceCache's cells already do. That is also why
    // SurfaceTriPlot::tri never reaches the GPU at all.
    //
    // The value rides on the vertex and the *lookup happens in the fragment
    // shader*, which is the one thing this buffer does that SurfaceCache's does
    // not: looking up at the three corners and letting the rasterizer blend the
    // results is the RGB chord again -- a whole triangle of colours the
    // colorbar does not contain. See k_surface_tri_frag.
    //
    // Keyed like the others: nothing about the camera is baked, so an orbit
    // costs one glDrawArrays and no uploads.
    struct SurfaceTriCache {
        unsigned int       vbo   = 0;
        int                verts = 0;
        unsigned long long data_generation = 0;
        Vec3               anchor{};
        float              shading = -1.0f;
        float              alpha   = -1.0f;
        Color              color{ 0, 0, 0, 0 };
        bool               colormapped = false;
        Colormap           cmap = Colormap::Viridis;
        double             vmin = 0.0, vmax = 0.0;
        int                axis_signs = 0;
        // The wireframe, in the key for Bar3DCache::edges' reason and against
        // the same defect: a buffer baked while edges were off used to leave a
        // latch set that nothing could ever ask past.
        bool               edges     = false;
        unsigned int       edge_vbo  = 0;
        int                edge_segs = 0;
        unsigned int       index_ebo = 0;
        int                indices   = 0;
    };
    std::unordered_map<CacheKey, SurfaceTriCache, CacheKeyHash> surface_tri_cache_;


    // 3D scatter clouds: one instance per point, (data-space centre - anchor,
    // pixel size, RGBA). Keyed like the two above -- nothing about the camera
    // is baked, so an orbit costs one instanced draw and no uploads.
    //
    // `depthshade` is deliberately **not** in the key: it is a uniform the
    // vertex shader applies, which is the whole reason the cue costs nothing.
    // `marker` is not either, for the same reason. What is in the key is what
    // the buffer holds: the size, and everything the colour was baked from.
    struct Scatter3DCache {
        unsigned int       vbo    = 0;
        int                points = 0;
        unsigned long long data_generation = 0;
        Vec3               anchor{};
        float              size  = -1.0f;
        float              alpha = -1.0f;
        Color              color{ 0, 0, 0, 0 };
        bool               colormapped = false;
        Colormap           cmap = Colormap::Viridis;
        double             vmin = 0.0, vmax = 0.0;
        // Translucent clouds only, and rebuilt every frame because it is the
        // one camera-dependent thing here. A second buffer rather than an
        // index list: the sort reorders *instances*, and an instanced draw
        // has no index path into its attributes.
        //
        // `host` is the baked instance data kept CPU-side so the reordered
        // copy can be assembled without reading the VBO back -- a readback
        // would stall the pipeline once per frame for data this side wrote.
        // Held only for a translucent cloud, which is decidable from the key
        // fields above and so cannot go stale on its own.
        unsigned int       order_vbo = 0;
        std::vector<float> host;
    };
    std::unordered_map<CacheKey, Scatter3DCache, CacheKeyHash> scatter3d_cache_;

    // 3D path geometry: one instance per segment, each (prev, a, b, next,
    // colour at a, colour at b). The two neighbours are in the buffer rather
    // than derived in the shader because an instanced draw has no way to read
    // another instance; they are what the miter join needs (see k_line3d_vert).
    //
    // Keyed like the three above: nothing about the camera is baked in -- the
    // width is a uniform and so is the depth shade -- so an orbit costs one
    // glDrawArraysInstanced per path and no uploads. `loop` is in the key
    // because it adds a segment, and the colour source is because it decides
    // every instance's two colours.
    struct Line3DCache {
        unsigned int       vbo  = 0;
        int                segs = 0;
        unsigned long long data_generation = 0;
        Vec3               anchor{};
        float              alpha = -1.0f;
        Color              color{ 0, 0, 0, 0 };
        bool               colormapped = false;
        bool               loop = false;
        Colormap           cmap = Colormap::Viridis;
        double             vmin = 0.0, vmax = 0.0;
        // Translucent paths only, and rebuilt every frame: the one
        // camera-dependent thing here. A second buffer rather than an index
        // list, for Scatter3DCache::order_vbo's reason -- the sort reorders
        // *instances*, and an instanced draw has no index path into its
        // attributes -- and `host` is the baked data kept CPU-side so the
        // reordered copy needs no readback.
        unsigned int       order_vbo = 0;
        std::vector<float> host;
    };
    std::unordered_map<CacheKey, Line3DCache, CacheKeyHash> line3d_cache_;

    // One series' error bars, as finished box-space triangles (v1.0 step 17),
    // keyed with `plane_index` naming the owner kind (0 scatter3d, 1 line3d).
    //
    // **Keyed on the view, unlike every cache above**, because the geometry is
    // not view-independent: a whisker faces the eye and every pixel length is
    // converted at the box centre. `view` is the clip matrix and the eye, so an
    // orbit rebuilds once per frame -- not once per peel pass -- and a still
    // frame rebuilds nothing.
    struct ErrorBar3DCache {
        unsigned int       opaque_vbo = 0, trans_vbo = 0;
        int                opaque_verts = 0, trans_verts = 0;
        unsigned long long data_generation = 0;
        std::vector<double> view;
    };
    std::unordered_map<CacheKey, ErrorBar3DCache, CacheKeyHash> errbar3d_cache_;

    struct ErrorBar3DUniforms { int clip = -1, depth = -1, shade = -1; };
    ErrorBar3DUniforms errbar3d_u_{};
    ErrorBar3DUniforms peel_errbar3d_u_{};
    unsigned int errbar3d_program_ = 0;
    unsigned int peel_errbar3d_program_ = 0;
    unsigned int errbar3d_vao_ = 0;

    struct Line3DUniforms { int clip = -1, box_scale = -1, box_offset = -1,
                                eye = -1, persp = -1, half_width = -1,
                                depth = -1, shade = -1, cmap = -1, colormapped = -1; };
    Line3DUniforms line3d_u_{};
    Line3DUniforms peel_line3d_u_{};
    unsigned int line3d_program_ = 0;
    unsigned int peel_line3d_program_ = 0;
    unsigned int line3d_vao_ = 0;
    unsigned int line3d_corner_vbo_ = 0;   // static 4-vertex unit quad
    // The colormap as a 256x1 texture, shared by every path and re-uploaded
    // only when a path asks for a different map -- one KB, so the check is
    // worth more than the upload it saves would be.
    unsigned int line3d_cmap_tex_ = 0;
    int          line3d_cmap_in_tex_ = -1;

    struct Scatter3DUniforms { int clip = -1, box_scale = -1, box_offset = -1,
                                   resolution = -1, marker = -1, depth = -1, shade = -1; };
    Scatter3DUniforms scatter3d_u_{};
    Scatter3DUniforms peel_scatter3d_u_{};
    unsigned int scatter3d_program_ = 0;
    unsigned int peel_scatter3d_program_ = 0;
    unsigned int scatter3d_vao_     = 0;
    unsigned int scatter3d_corner_vbo_ = 0;
    struct Surface3DUniforms { int clip = -1, box_scale = -1, box_offset = -1; };
    Surface3DUniforms surface_u_{};
    unsigned int surface_program_ = 0;
    unsigned int surface_vao_     = 0;

    // The mesh program. Two uniforms a grid surface's has no use for: the
    // colormap as a 256x1 lookup and the flag saying whether to use it, since
    // a mesh resolves its colour per fragment.
    struct SurfaceTriUniforms { int clip = -1, box_scale = -1, box_offset = -1,
                                   cmap = -1, colormapped = -1; };
    SurfaceTriUniforms surface_tri_u_{};
    SurfaceTriUniforms peel_surface_tri_u_{};
    unsigned int surface_tri_program_ = 0;
    unsigned int peel_surface_tri_program_ = 0;
    unsigned int surface_tri_vao_     = 0;
    // The colormap texture, shared by every mesh and re-uploaded only when one
    // asks for a different map -- line3d_cmap_tex_'s bargain exactly.
    unsigned int surface_tri_cmap_tex_ = 0;
    int          surface_tri_cmap_in_tex_ = -1;

    struct Plane3DUniforms { int clip = -1, tex = -1, alpha = -1,
                                 box_scale = -1, box_offset = -1; };
    Plane3DUniforms plane3d_u_{};
    unsigned int plane3d_program_ = 0;
    unsigned int plane3d_vao_     = 0;
    unsigned int plane3d_vbo_     = 0;   // 6 vertices, rewritten per plane

    // ---- The translucent-plane composite (step 7b) -----------------------
    //
    // How many planes one exact group holds. The ceiling is the fragment
    // shader's own unrolled loop, not the hardware -- 4.1 Core guarantees 16
    // texture units, and the per-fragment cost is quadratic in this number
    // (every slot re-gathers every plane), so a generous small number is the
    // right one. Past the cap the far-to-near list is cut into groups of this
    // size and the *groups* are composited in that whole-object order: exact
    // within a group, today's heuristic between them.
    static constexpr int kMaxCompositePlanes = 8;

    struct PlaneCompositeUniforms {
        int clip = -1, count = -1, slot = -1;
        int origin = -1, du = -1, dv = -1, axis = -1, alpha = -1;
    };
    PlaneCompositeUniforms plane_comp_u_{};
    unsigned int plane_comp_program_ = 0;
    unsigned int plane_comp_vao_     = 0;
    unsigned int plane_comp_vbo_     = 0;   // 6 vertices, the plot rect

    // One plane's rendered contents (step 7a): the little screen itself.
    //
    // **Not keyed on the camera**, which is the property the whole design
    // rests on. What the raster holds depends on the plane's data and on the
    // parent's limits -- the two things `PlaneRaster::tr` is built from -- and
    // on nothing about where the eye is, so an orbit re-renders nothing here
    // any more than it re-uploads a vertex buffer. A pan or a zoom does change
    // the limits, and does re-render, which is correct: the content moves.
    //
    // `Plane2DOptions::alpha` is deliberately *not* in the key. It multiplies
    // the finished quad through the plane3d shader's uniform, so changing it
    // costs nothing -- where the old geometry path baked it into every vertex
    // colour and had to rebuild the buffers for it.
    struct PlaneRasterCache {
        unsigned int       fbo = 0, tex = 0;
        int                w = 0, h = 0;       // real framebuffer pixels
        unsigned long long data_generation = 0;
        CoordTransform     tr{};
        bool               valid = false;
    };
    std::unordered_map<CacheKey, PlaneRasterCache, CacheKeyHash> plane_raster_cache_;

    struct Bar3DUniforms { int clip = -1, color = -1, box_scale = -1, box_offset = -1; };
    struct Bar3DEdgeUniforms { int clip = -1, color = -1, box_scale = -1, box_offset = -1,
                                   eye = -1, persp = -1, half_width = -1; };
    Bar3DUniforms     bar3d_u_{};
    Bar3DEdgeUniforms bar3d_edge_u_{};

    unsigned int bar3d_program_ = 0;
    unsigned int bar3d_vao_     = 0;

    unsigned int bar3d_edge_program_ = 0;
    unsigned int bar3d_edge_vao_     = 0;
    unsigned int bar3d_edge_corner_vbo_ = 0;   // static 4-vertex unit quad

    // ---- Depth peeling: the programs (step 8) ----------------------------
    //
    // The four in-scene 3D programs again, with the peel test and the
    // premultiply in the fragment shader. Separate programs rather than a
    // `uPeel` uniform on the originals, so that the shaders every existing
    // output was rendered by stay textually unchanged -- the standing
    // byte-identity bar is cheaper to keep than to argue about, and the flat
    // fragment shader the bar and surface wireframes use is shared with the
    // 2D path, which must not learn about peeling at all.
    unsigned int peel_bar3d_program_   = 0;   Bar3DUniforms     peel_bar3d_u_{};
    unsigned int peel_surface_program_ = 0;   Surface3DUniforms peel_surface_u_{};
    unsigned int peel_plane3d_program_ = 0;   Plane3DUniforms   peel_plane3d_u_{};
    unsigned int peel_edge_program_    = 0;   Bar3DEdgeUniforms peel_edge_u_{};

    // The one textured quad the peel composites with -- layer under
    // accumulation, then accumulation over the scene.
    struct PeelCompUniforms { int tex = -1, flip = -1; };
    PeelCompUniforms peel_comp_u_{};
    unsigned int peel_comp_program_ = 0;
    unsigned int peel_comp_vao_     = 0;
    unsigned int peel_comp_vbo_     = 0;   // 6 vertices, rewritten per composite

    // The peel's own render targets, sized to the plot rect in *real*
    // framebuffer pixels and rebuilt only when that size changes.
    //
    // `opaque_tex` is a copy of the main framebuffer's depth, taken after the
    // opaque phase, and it is what keeps opaque geometry occluding translucent
    // geometry while the opaque path itself changes not at all: a peel pass
    // discards any fragment at or behind it. Copied by a blit rather than by
    // re-drawing the opaque scene, which is why it carries the same
    // GL_DEPTH24_STENCIL8 format PlotFbo and FboReadback both use --
    // glBlitFramebuffer refuses a depth blit between unlike formats.
    //
    // The colour targets are RGBA16F and not RGBA8. `layer` holds a
    // *premultiplied* colour whose magnitude is alpha times the material
    // colour, so at alpha 0.05 an 8-bit target would quantise a whole layer to
    // a dozen distinct values; `accum` holds a transmittance that up to eight
    // layers multiply together.
    struct PeelTargets {
        unsigned int fbo = 0;         // colour = layer, depth = depth_tex[cur]
        unsigned int accum_fbo = 0;   // colour = accum
        unsigned int copy_fbo = 0;    // depth = opaque_tex; the blit's target
        unsigned int layer_tex = 0, accum_tex = 0, opaque_tex = 0;
        unsigned int depth_tex[2]{ 0, 0 };
        unsigned int query = 0;       // GL_ANY_SAMPLES_PASSED, the early-out
        int  w = 0, h = 0;
        // Set only while a peel pass's geometry is being drawn. Everything the
        // three per-kind draws do differently under peeling reads this: the
        // scissor is the peel target rather than the plot rect, blending is
        // off, depth writes stay on, and no back-to-front index buffer is
        // built -- the peel *is* the order, so building one would be paying a
        // second time for the thing this step exists to stop paying for once.
        bool active = false;
    };
    PeelTargets peel_{};
    bool ensure_peel_targets(int w, int h);

    // How many layers peel_translucent3d() may take. Three sources, in order:
    // this renderer's own override when one is set (PngExportOptions::
    // peel_layers, for one export), then SEXTANT_PEEL_LAYERS in the
    // environment, read once, then 8. 0 from the environment turns peeling off
    // and restores post-step-7d's whole-object order, which is what the two
    // paths have to be timed against each other for. See peel_translucent3d()
    // for the alpha bound.
    int        peel_layer_count() const;
    static int peel_layer_default();
    int peel_layers_override_ = 0;

    // Uniform locations, resolved once at construction. glGetUniformLocation
    // is a driver-side string lookup, and these were previously called from
    // inside the per-plot-object draw loops — once per object per frame, for
    // names that are compile-time constants.
    struct LineUniforms   { int resolution = -1, color = -1, scale = -1, offset = -1; };
    struct SegUniforms    { int resolution = -1, color = -1, scale = -1, offset = -1,
                                half_width = -1, dash = -1, dash_period = -1,
                                dist_scale = -1; };
    SegUniforms    lineseg_u_{};
    struct MarkerUniforms { int resolution = -1, color = -1, marker = -1,
                                scale = -1, offset = -1; };
    struct HeatUniforms   { int resolution = -1, tex = -1; };
    LineUniforms   line_u_{};       // shared by draw_lines and draw_bars
    MarkerUniforms scatter_u_{};
    MarkerUniforms scatterz_u_{};   // .color unused: colour is per-instance
    HeatUniforms   heatmap_u_{};

    // Line / bar shared program: aPos(vec2) → uColor(vec4). Used by bar fills
    // and bar outlines; line strokes have their own instanced program below.
    unsigned int line_program_ = 0;
    unsigned int line_vao_     = 0;
    unsigned int line_vbo_     = 0;

    // Instanced line-segment program
    unsigned int lineseg_program_   = 0;
    unsigned int lineseg_vao_       = 0;
    unsigned int lineseg_corner_vbo_ = 0;   // static 4-vertex unit quad

    // Scatter instanced program
    unsigned int scatter_program_ = 0;
    unsigned int scatter_vao_     = 0;
    unsigned int scatter_quad_vbo_ = 0;   // unit quad (shared geometry)
    unsigned int scatter_inst_vbo_ = 0;   // per-instance data

    // Continuous-color scatter (scatter_z) instanced program — separate from
    // the above since its instance layout carries a per-point color.
    unsigned int scatterz_program_  = 0;
    unsigned int scatterz_vao_      = 0;
    unsigned int scatterz_quad_vbo_ = 0;
    unsigned int scatterz_inst_vbo_ = 0;

    // Heatmap textured-quad program
    unsigned int heatmap_program_ = 0;
    unsigned int heatmap_vao_     = 0;
    unsigned int heatmap_vbo_     = 0;
};

} // namespace sextant
