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

    // Framebuffer pixels per logical pixel (the supersample factor), set per
    // frame. Only scissor rects and line widths need it.
    void set_pixel_ratio(float ratio) { pixel_ratio_ = ratio > 0.0f ? ratio : 1.0f; }

    // The snapshot/axes the following draws belong to: the cache key. Set per
    // axes per frame; generation 0 disables caching.
    void set_frame_key(unsigned long long data_generation, int axes_index) {
        data_generation_ = data_generation;
        axes_index_ = axes_index;
    }

    void draw_lines(const std::vector<LinePlot>& lines,
                    const CoordTransform& tr, const PlotRect& pr);

    void draw_scatter(const std::vector<ScatterPlot>& scatters,
                      const CoordTransform& tr, const PlotRect& pr);

    // Continuous-color scatter: per-point colors from the colormap, computed on
    // the CPU and uploaded per instance (separate shader from draw_scatter).
    void draw_scatter_z(const std::vector<ScatterZPlot>& points,
                        const CoordTransform& tr, const PlotRect& pr);

    void draw_bars(const std::vector<BarPlot>& bars,
                   const CoordTransform& tr, const PlotRect& pr);

    void draw_heatmap(const std::vector<HeatmapPlot>& heatmaps,
                      const CoordTransform& tr, const PlotRect& pr);

    // Error bars for all four 2D kinds in one call. Draw after fills, before
    // draw_scatter.
    void draw_error_bars(const AllPlotData& all,
                         const CoordTransform& tr, const PlotRect& pr);

    // The whole 3D scene in two phases: all opaque objects (depth-tested, any
    // order), then all translucent objects in one back-to-front order across
    // every kind (by eye_coord() distance; exact unless objects interpenetrate).
    // The only correct way to sequence the per-kind draws.
    void draw_scene3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                      const PlotRect& pr, float win_w, float win_h);

    // Per-renderer peel-layer override (0 = none); set for one PNG export and
    // restored (see PeelLayerScope in figure_export.cpp).
    int  peel_layers_override() const { return peel_layers_override_; }
    void set_peel_layers_override(int n) { peel_layers_override_ = n; }

private:
    // Which half of the scene a per-kind draw is for: opaque (depth-resolved)
    // or translucent (depth writes off). Translucent planes are composited per
    // fragment instead (composite_planes3d()).
    enum class ScenePass { Opaque, Translucent };

    // Per-kind 3D draws, sequenced only by draw_scene3d(). `order` lists the
    // object indices to draw (filtered to `pass`, sorted); null = all objects of
    // this pass in the kind's own order.

    // 3D bars, depth-tested. Buffers hold data-space corners minus an anchor;
    // the whole projection is one clip_matrix() uniform, so orbits and limit
    // changes invalidate nothing.
    void draw_bars3d(const std::vector<Bar3DPlot>& bars, const Projector3D& proj,
                     const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                     const std::vector<std::size_t>* order = nullptr);

    // 3D surfaces, two-sided sheets. Opaque: one unordered draw. Translucent:
    // sorted by the grid, and ordered as a whole object by draw_scene3d().
    void draw_surfaces3d(const std::vector<SurfacePlot>& surfaces, const Projector3D& proj,
                         const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                         const std::vector<std::size_t>* order = nullptr);

    // 2D kinds on a plane. Buffers are in data space and the projection is one
    // matrix; the heatmap quad is rebuilt per frame but its texture is cached.
    // Within a plane, batches are drawn in 2D painter order (coplanar
    // primitives can't be depth-sorted).

    void draw_planes3d(const std::vector<PlaneSnapshot>& planes, const Projector3D& proj,
                       const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                       const std::vector<std::size_t>* order = nullptr);

    // 3D scatter: pixel-sized billboards at the point's depth, so the depth
    // buffer resolves them against geometry. Translucent unpeeled sorting is
    // exact (see scatter3d_draw_order()).
    void draw_scatter3d(const std::vector<Scatter3DPlot>& points, const Projector3D& proj,
                        const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                        const std::vector<std::size_t>* order = nullptr);

    // Triangulated meshes: color value per vertex, looked up per fragment.
    // Translucent unpeeled face order is a heuristic.
    void draw_surface_tri3d(const std::vector<SurfaceTriPlot>& meshes,
                            const Projector3D& proj, const PlotRect& pr,
                            float win_w, float win_h, ScenePass pass,
                            const std::vector<std::size_t>* order = nullptr);

    // Paths as world-space ribbons with mitered joins. Segment order within a
    // translucent path is a heuristic; peeling ignores it.
    void draw_lines3d(const std::vector<Line3DPlot>& lines, const Projector3D& proj,
                      const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                      const std::vector<std::size_t>* order = nullptr);

    // scatter3d/line3d error bars as errorbar3d_pieces() triangles. `order`
    // indexes clouds first, then paths (`scatter3d.size() + i`). Opaque pass:
    // opaque pieces; translucent pass: the rest, sorted by piece when unpeeled.
    void draw_errorbars3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                          const PlotRect& pr, float win_w, float win_h, ScenePass pass,
                          const std::vector<std::size_t>* order = nullptr);

    // Common per-draw GL state for the data pass (see begin_pass() in the .cpp).
    void begin_pass(const PlotRect& pr, float win_h) const;
    void end_pass() const;

    // Render one plane's contents into its own framebuffer via the 2D draw
    // calls, in 2D order. Restores framebuffer, viewport and scissor.
    struct PlaneRasterCache;   // defined with the other caches below
    void render_plane_raster(const PlaneSnapshot& pl, int plane_index,
                             const PlaneRaster& raster, PlaneRasterCache& c);

    // Composite one group of translucent planes exactly, per pixel: for each
    // slot, far to near, the shader ray-casts all planes, sorts the hits and
    // emits the slot-th with gl_FragDepth, so opaque occlusion and blend order
    // are both right. Every plane in `group` needs a valid raster.
    void composite_planes3d(const std::vector<PlaneSnapshot>& planes,
                            const std::vector<PlaneRaster>& rasters,
                            const std::vector<std::size_t>& group,
                            const Projector3D& proj, const PlotRect& pr,
                            float win_w, float win_h);

    // Render all translucent plane rasters before peeling, so the occlusion
    // query in each pass counts only that pass's peel.
    void prepare_plane_rasters(const std::vector<PlaneSnapshot>& planes,
                               const Projector3D& proj);

    // ---- Depth peeling ----------------------------------------------------
    // Pass 0 keeps the nearest translucent depth; pass n discards fragments at
    // or nearer than pass n-1's, so layers arrive front to back with no sorting
    // (exact even for cyclic overlap). 8 layers leave < ~1% transmittance at any
    // alpha; an occlusion query stops early when a pass peels nothing.
    // Returns false when targets can't be built (fall back to object order).
    bool peel_translucent3d(const RenderSnapshot3D& snap, const Projector3D& proj,
                            const PlotRect& pr, float win_w, float win_h);

    // A textured NDC quad for the peel composites; `flip_alpha` converts the
    // accumulated transmittance to coverage.
    void draw_peel_quad(unsigned int tex, bool flip_alpha,
                        float x0, float y0, float x1, float y1) const;

    float pixel_ratio_ = 1.0f;

    // ---- Per-frame caches (per window thread) ----------------------------
    // Keyed on (data generation, axes, plot index) plus whatever else is baked
    // into the result.
    unsigned long long data_generation_ = 0;
    int                axes_index_ = -1;
    // The plane the 2D draws are currently targeting (-1 = the axes); part of
    // every 2D cache key.
    int                plane_index_ = -1;

    // One plot object across frames (indices are positional). `plane_index` is
    // -1 for the axes' own objects, else the plane's index.
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

    // Stroke points (the shader expands segments), padded with duplicates of
    // the first and last point so each instance reads prev/p0/p1/next.
    struct LineCache {
        unsigned int       vbo        = 0;
        int                segments   = 0;   // instance count = segment_count()
        unsigned long long data_generation = 0;
        // `loop` changes the pad points, so it is part of the key.
        bool               loop       = false;
        double             anchor_x = 0.0, anchor_y = 0.0;
        double             span_x = 0.0, span_y = 0.0;
        bool               data_space = true;
        CoordTransform     tr{};             // only consulted when !data_space

        // Dash phase: cumulative arc length in logical pixels per point, for
        // non-solid lines only. View-dependent; reused while the scale changes
        // uniformly (pan, symmetric zoom), rebuilt on aspect change.
        unsigned int       dist_vbo    = 0;
        bool               dist_valid  = false;
        float              dist_ref_sx = 0.0f, dist_ref_sy = 0.0f;
    };
    std::unordered_map<CacheKey, LineCache, CacheKeyHash> line_cache_;

    // Colormapped heatmap texture (the quad is rebuilt per frame).
    struct HeatCache {
        unsigned int       tex        = 0;
        unsigned long long data_generation = 0;
        Colormap           cmap       = Colormap::Viridis;
        float              vmin = 0.0f, vmax = 0.0f;
        bool               flip = false;
        int                rows = 0, cols = 0;
    };
    std::unordered_map<CacheKey, HeatCache, CacheKeyHash> heat_cache_;

    // Upload `hp`'s colormapped pixels unless cached; leaves the texture bound
    // on unit 0. Shared by 2D and plane quads.
    void ensure_heatmap_texture(HeatCache& hc, const HeatmapPlot& hp);

    // Scatter/scatter_z instances in data space minus `anchor` (transform is a
    // uniform, so pan/zoom reuses them). `data_space == false` is the deep-zoom
    // precision fallback with pixel-space centres, hence `tr` in the key.
    struct InstanceCache {
        unsigned int       vbo        = 0;
        int                instances  = 0;
        unsigned long long data_generation = 0;
        double             anchor_x = 0.0, anchor_y = 0.0;
        double             span_x = 0.0, span_y = 0.0;   // for the precision test
        float              size = 0.0f;                  // marker size is baked in
        bool               data_space = true;
        // scatter_z only: the colormap is baked in.
        Colormap           cmap = Colormap::Viridis;
        float              vmin = 0.0f, vmax = 0.0f, alpha = -1.0f;
        CoordTransform     tr{};                         // only when !data_space
    };
    std::unordered_map<CacheKey, InstanceCache, CacheKeyHash> scatter_cache_;
    std::unordered_map<CacheKey, InstanceCache, CacheKeyHash> scatterz_cache_;

    // Bars: data-space fills survive pan/zoom; outlines are inset in pixel
    // space, so they carry the transform in their key.
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

    // 2D error bars: pixel-space geometry (transform in key). Two buffers (box
    // interior at box_alpha, the rest opaque); one map per kind, since CacheKey
    // doesn't include the kind.
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

    // 3D bars: 36 vertices per bar (corner - anchor, shade). No camera in the
    // key; it holds what's baked: generation, footprints, base, shading, axis
    // signs.
    struct Bar3DCache {
        unsigned int       vbo   = 0;
        int                verts = 0;
        unsigned long long data_generation = 0;
        Vec3               anchor{};
        double             u_width = -1.0, v_width = -1.0, bottom = 0.0;
        float              shading = -1.0f;
        int                axis_signs = 0;
        // Edge ribbons (box-space endpoints expanded in the shader). `edges`
        // records what the buffer was built for, so edges can be turned back
        // on.
        bool               edges      = false;
        unsigned int       edge_vbo   = 0;
        int                edge_segs  = 0;
        // Translucent only: camera-dependent draw order, rebuilt per frame.
        unsigned int       index_ebo  = 0;
        int                indices    = 0;
    };
    std::unordered_map<CacheKey, Bar3DCache, CacheKeyHash> bar3d_cache_;

    // 3D surfaces: 6 vertices per cell (corner - anchor, RGBA), color constant
    // per cell (matching the SVG's per-cell fill). No camera in the key.
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
        // What the buffer was built for (see Bar3DCache::edges).
        bool               edges     = false;
        unsigned int       edge_vbo  = 0;
        int                edge_segs = 0;
        unsigned int       index_ebo = 0;
        int                indices   = 0;
    };
    std::unordered_map<CacheKey, SurfaceCache, CacheKeyHash> surface_cache_;

    // 3D meshes: 3 vertices per face (corner - anchor, RGBA, color value,
    // shade), expanded since per-face normals can't share vertices. The
    // colormap lookup is per fragment (see k_surface_tri_frag). No camera in
    // the key.
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
        // What the buffer was built for (see Bar3DCache::edges).
        bool               edges     = false;
        unsigned int       edge_vbo  = 0;
        int                edge_segs = 0;
        unsigned int       index_ebo = 0;
        int                indices   = 0;
    };
    std::unordered_map<CacheKey, SurfaceTriCache, CacheKeyHash> surface_tri_cache_;

    // 3D scatter: one instance per point (centre - anchor, size, RGBA). No
    // camera in the key; `depthshade` and `marker` are uniforms.
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
        // Translucent only, rebuilt per frame: a reordered copy of the
        // instances (instanced draws have no index path). `host` keeps the data
        // CPU-side to avoid a readback.
        unsigned int       order_vbo = 0;
        std::vector<float> host;
    };
    std::unordered_map<CacheKey, Scatter3DCache, CacheKeyHash> scatter3d_cache_;

    // 3D paths: one instance per segment (prev, a, b, next, color at a and b);
    // neighbours are stored for the miter join. No camera in the key; width
    // and depth shade are uniforms.
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
        // Translucent only, rebuilt per frame (see Scatter3DCache::order_vbo).
        unsigned int       order_vbo = 0;
        std::vector<float> host;
    };
    std::unordered_map<CacheKey, Line3DCache, CacheKeyHash> line3d_cache_;

    // One series' 3D error bars as box-space triangles; `plane_index` names the
    // owner kind (0 scatter3d, 1 line3d). Keyed on the view (clip matrix and
    // eye), since whiskers face the eye: rebuilt once per changed frame.
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
    // 256x1 colormap texture for paths, re-uploaded only when the map changes.
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

    // Mesh program uniforms, including the colormap lookup and its flag.
    struct SurfaceTriUniforms { int clip = -1, box_scale = -1, box_offset = -1,
                                   cmap = -1, colormapped = -1; };
    SurfaceTriUniforms surface_tri_u_{};
    SurfaceTriUniforms peel_surface_tri_u_{};
    unsigned int surface_tri_program_ = 0;
    unsigned int peel_surface_tri_program_ = 0;
    unsigned int surface_tri_vao_     = 0;
    // Colormap texture for meshes, re-uploaded only when the map changes.
    unsigned int surface_tri_cmap_tex_ = 0;
    int          surface_tri_cmap_in_tex_ = -1;

    struct Plane3DUniforms { int clip = -1, tex = -1, alpha = -1,
                                 box_scale = -1, box_offset = -1; };
    Plane3DUniforms plane3d_u_{};
    unsigned int plane3d_program_ = 0;
    unsigned int plane3d_vao_     = 0;
    unsigned int plane3d_vbo_     = 0;   // 6 vertices, rewritten per plane

    // ---- The translucent-plane composite ---------------------------------
    // Planes per exact group, bounded by the shader's unrolled loop (cost is
    // quadratic). Larger sets are split into groups composited in object order.
    static constexpr int kMaxCompositePlanes = 8;

    struct PlaneCompositeUniforms {
        int clip = -1, count = -1, slot = -1;
        int origin = -1, du = -1, dv = -1, axis = -1, alpha = -1;
    };
    PlaneCompositeUniforms plane_comp_u_{};
    unsigned int plane_comp_program_ = 0;
    unsigned int plane_comp_vao_     = 0;
    unsigned int plane_comp_vbo_     = 0;   // 6 vertices, the plot rect

    // One plane's rendered contents. Keyed on the plane's data and the parent
    // limits, not the camera, so orbits re-render nothing. Plane alpha is a
    // uniform, so it isn't in the key.
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

    // ---- Depth peeling: the programs --------------------------------------
    // The in-scene 3D programs with the peel test and premultiply added.
    // Separate programs keep the original shaders unchanged.
    unsigned int peel_bar3d_program_   = 0;   Bar3DUniforms     peel_bar3d_u_{};
    unsigned int peel_surface_program_ = 0;   Surface3DUniforms peel_surface_u_{};
    unsigned int peel_plane3d_program_ = 0;   Plane3DUniforms   peel_plane3d_u_{};
    unsigned int peel_edge_program_    = 0;   Bar3DEdgeUniforms peel_edge_u_{};

    // The peel composite quad (layer under accumulation, then over the scene).
    struct PeelCompUniforms { int tex = -1, flip = -1; };
    PeelCompUniforms peel_comp_u_{};
    unsigned int peel_comp_program_ = 0;
    unsigned int peel_comp_vao_     = 0;
    unsigned int peel_comp_vbo_     = 0;   // 6 vertices, rewritten per composite

    // Peel render targets, sized to the plot rect in framebuffer pixels.
    // `opaque_tex` is a blit of the scene depth after the opaque phase (same
    // GL_DEPTH24_STENCIL8 format, required for the blit). Colour targets are
    // RGBA16F: premultiplied low-alpha layers and multiplied transmittance
    // need the precision.
    struct PeelTargets {
        unsigned int fbo = 0;         // colour = layer, depth = depth_tex[cur]
        unsigned int accum_fbo = 0;   // colour = accum
        unsigned int copy_fbo = 0;    // depth = opaque_tex; the blit's target
        unsigned int layer_tex = 0, accum_tex = 0, opaque_tex = 0;
        unsigned int depth_tex[2]{ 0, 0 };
        unsigned int query = 0;       // GL_ANY_SAMPLES_PASSED, the early-out
        int  w = 0, h = 0;
        // True while drawing a peel pass: scissor to the peel target, no
        // blending, depth writes on, no back-to-front index buffers.
        bool active = false;
    };
    PeelTargets peel_{};
    bool ensure_peel_targets(int w, int h);

    // Peel layers: this renderer's override, else SEXTANT_PEEL_LAYERS (read
    // once), else 8. 0 from the environment disables peeling.
    int        peel_layer_count() const;
    static int peel_layer_default();
    int peel_layers_override_ = 0;

    // Uniform locations, resolved once at construction.
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

    // Shared line/bar program: aPos(vec2) -> uColor(vec4), for bar fills and
    // outlines.
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

    // scatter_z instanced program (per-point color in the instance layout).
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
