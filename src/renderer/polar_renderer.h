#pragma once
// Internal header: the GL data pass of a polar axes. Geometry is built once per
// data change in (θ, r) (polar_geometry.h's data-space functions); the shaders
// map it to pixels and clip it to the frame per pixel, so a turn, an r zoom or a
// resize changes uniforms only.
#include "plot_rect.h"
#include "../coord_transform.h"
#include "../plot_objects.h"
#include "../polar_geometry.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sextant {

class PolarRenderer {
public:
    PolarRenderer();
    ~PolarRenderer();

    PolarRenderer(const PolarRenderer&) = delete;
    PolarRenderer& operator=(const PolarRenderer&) = delete;

    // As DataRenderer::set_pixel_ratio() (the scissor needs it).
    void set_pixel_ratio(float ratio) { pixel_ratio_ = ratio > 0.0f ? ratio : 1.0f; }

    // While true (a window's drag or wheel on a polar cell), a dashed stroke
    // keeps the dash lengths measured at the last r limits, scaled with the
    // radius, instead of measuring them again: the dashes drift during the
    // gesture and are exact again on the first frame after it. Off by default
    // (exports and headless draws are always exact).
    void set_nav_active(bool active) { nav_active_ = active; }

    // A polar cell's objects in draw_order(), mapped by `m` into the frame
    // whose unit disc `tr` maps (PolarPlan's), scissored to `pr`.
    void draw(const RenderSnapshotPolar& snap, const PolarMap& m, const CoordTransform& tr, const PlotRect& pr);

    // Drops the cache entries no draw() used since the last call (objects
    // removed, cells gone). Once per frame, after the data pass.
    void end_frame();

private:
    struct Run {
        int first = 0;   // the run's first padded point
        int segs = 0;
    };
    // A stroke set in (θ, r - r_ref): padded points (see draw_strokes()) and
    // per-segment arc length (dashed only), in unit space at dist_rmin/rmax.
    struct Strokes {
        unsigned int vbo = 0, dist_vbo = 0;
        std::vector<Run> runs;
        std::vector<PolarStroke> src;   // kept for re-measuring the dashes
        double r_ref = 0.0;
        bool dist_valid = false;
        double dist_rmin = 0.0, dist_rmax = 0.0;
    };
    struct Area {
        unsigned int vbo = 0;
        int verts = 0;
        double r_ref = 0.0;
    };
    struct Markers {
        unsigned int vbo = 0;
        int instances = 0;
        double r_ref = 0.0;
    };
    // One object's GPU data, keyed by its id; `key` holds what it was built
    // from (data stamp and the options that move geometry or colours).
    struct Entry {
        std::vector<double> key;
        Strokes strokes;
        Area area;
        Markers markers;
        std::uint64_t frame = 0;
    };

    struct View;   // the uniforms of one draw()

    Entry& entry(ObjectId id, std::vector<double> key, bool& fresh);
    static void release(Entry& e);

    void draw_line(const LinePlot& p, const View& v);
    void draw_scatter(const ScatterPlot& p, const View& v);
    void draw_scatter_z(const ScatterZPlot& p, const View& v);
    void draw_bar(const BarPlot& p, const View& v);
    void draw_fill(const FillPlot& p, const View& v);
    void draw_poly(const PolyPlot& p, const View& v);

    void build_strokes(Strokes& s, std::vector<PolarStroke> src);
    void build_area(Area& a, const std::vector<double>& tris);
    void draw_strokes(Strokes& s, const View& v, float linewidth, LineStyle ls, const Color& c);
    void draw_area(const Area& a, const View& v, const Color& c);

    void begin_pass(const PlotRect& pr, float win_h) const;

    // One program's uniform locations (-1: not in it).
    struct Uniforms {
        int resolution = -1, offset = -1, dir = -1, rshift = -1, rspan = -1, centre = -1, scale = -1,
            start = -1, sweep = -1, color = -1, half_width = -1, dash = -1, dash_period = -1,
            dist_scale = -1, edge = -1, edge_w = -1, edge_face = -1, marker = -1;
    };
    static Uniforms locate(unsigned int program);
    void set_view(const Uniforms& u, const View& v, double r_ref) const;

    Uniforms line_u_, area_u_, marker_u_, markerz_u_;
    float pixel_ratio_ = 1.0f;
    bool nav_active_ = false;
    std::uint64_t frame_ = 1;
    std::unordered_map<ObjectId, Entry> cache_;

    unsigned int line_program_ = 0, line_vao_ = 0, corner_vbo_ = 0;
    unsigned int area_program_ = 0, area_vao_ = 0;
    unsigned int marker_program_ = 0, marker_vao_ = 0;
    unsigned int markerz_program_ = 0, markerz_vao_ = 0;
    unsigned int quad_vbo_ = 0;
};

} // namespace sextant
