#pragma once
#include "plot_rect.h"
#include "../plot_objects.h"
#include "../coord_transform.h"
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

private:
    // Common per-draw GL state for the data pass — see begin_pass()'s comment
    // in the .cpp for why blending in particular has to be set explicitly.
    void begin_pass(const PlotRect& pr, float win_h) const;
    void end_pass() const;

    float pixel_ratio_ = 1.0f;

    // ---- Per-frame caches ------------------------------------------------
    //
    // Both key on (data generation, axes, plot index) plus whatever else the
    // result depends on, and are held per DataRenderer, i.e. per window
    // thread. Without them the pass re-derives and re-uploads every vertex and
    // every heatmap texture each frame whether or not anything changed.
    unsigned long long data_generation_ = 0;
    int                axes_index_ = -1;

    // Identifies one plot object across frames. Plot indices are positional
    // within an axes, so this is stable exactly as long as the plot list is.
    struct CacheKey {
        int axes_index = -1;
        int plot_index = -1;
        bool operator==(const CacheKey& o) const {
            return axes_index == o.axes_index && plot_index == o.plot_index;
        }
    };
    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& k) const {
            return (static_cast<std::size_t>(static_cast<unsigned>(k.axes_index)) << 32)
                 ^ static_cast<unsigned>(k.plot_index);
        }
    };

    // Stroke *points*, not expanded geometry: the shader turns each segment
    // into a quad, so this buffer is view-independent like scatter's and the
    // stroke width is a uniform. Padded with a duplicate of the first and last
    // point so every instance can read prev/p0/p1/next.
    struct LineCache {
        unsigned int       vbo        = 0;
        int                segments   = 0;   // instance count = points - 1
        unsigned long long data_generation = 0;
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
        float              boxwidth  = -1.0f;
        float              box_alpha = -1.0f;
        float              pixel_ratio = 0.0f;
    };
    std::unordered_map<CacheKey, ErrCache, CacheKeyHash> line_err_cache_;
    std::unordered_map<CacheKey, ErrCache, CacheKeyHash> bar_err_cache_;
    std::unordered_map<CacheKey, ErrCache, CacheKeyHash> scatter_err_cache_;
    std::unordered_map<CacheKey, ErrCache, CacheKeyHash> scatterz_err_cache_;

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
