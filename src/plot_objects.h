#pragma once
#include "sextant/style.h"
#include "cow_vec.h"
#include "tick.h"
#include <atomic>
#include <cmath>
#include <optional>
#include <vector>

namespace sextant {

// Identifies which of the five per-kind plot vectors an object lives in. Both
// the data-panel view layer and the cross-thread edit payload need to name a
// plot object as a (kind, index) pair -- plot objects carry no identity of
// their own.
enum class PlotKind { Line, Scatter, Bar, Heatmap, ScatterZ };

// The bulk data of every plot object below is a CowVec, not a plain vector:
// Axes::Impl and RenderSnapshot declare these same structs, and a snapshot is
// copied on hot paths (refresh(), and every frame of a pan). See cow_vec.h —
// in particular its threading note, which is what keeps a shared buffer safe.

// The data half of ErrorBarOptions, which is where a caller sets it; the Axes
// ingest methods move the six vectors here. They live in the plot object
// rather than the options struct for the reason CowVec exists: `opts` is
// deep-copied into every snapshot, so per-point vectors parked there would put
// a memcpy of the whole series back on the pan path.
//
// The whisker span is in absolute data coordinates and the box half-extent is
// a spread in data units. The accessors take the point's own coordinate so an
// absent end reads as the point itself, which is what draws a one-sided
// whisker and lets every caller skip the empty-vector case.
struct ErrorBarData {
    CowVec<double> ymin, ymax, yvar;
    CowVec<double> xmin, xmax, xvar;

    bool has_y_span() const { return !ymin.empty() || !ymax.empty(); }
    bool has_x_span() const { return !xmin.empty() || !xmax.empty(); }
    bool has_y_box()  const { return !yvar.empty(); }
    bool has_x_box()  const { return !xvar.empty(); }
    bool empty() const {
        return !has_y_span() && !has_x_span() && !has_y_box() && !has_x_box();
    }

    double y_lo(std::size_t i, double y) const { return at(ymin, i, y); }
    double y_hi(std::size_t i, double y) const { return at(ymax, i, y); }
    double x_lo(std::size_t i, double x) const { return at(xmin, i, x); }
    double x_hi(std::size_t i, double x) const { return at(xmax, i, x); }

    // Absolute-valued: a negative spread is a magnitude, not an inverted box.
    double y_var(std::size_t i) const { return mag(yvar, i); }
    double x_var(std::size_t i) const { return mag(xvar, i); }

private:
    static double at(const CowVec<double>& v, std::size_t i, double fallback) {
        return i < v.size() ? v[i] : fallback;
    }
    static double mag(const CowVec<double>& v, std::size_t i) {
        return i < v.size() ? std::fabs(v[i]) : 0.0;
    }
};

struct LinePlot {
    CowVec<double> x, y;
    ErrorBarData   err;
    LineOptions    opts;
};

struct ScatterPlot {
    CowVec<double> x, y;
    ErrorBarData   err;
    ScatterOptions opts;
};

// bar_width is always in data-space units, already resolved: inter-bar
// spacing x BarOptions::width for bar(), bin width x the same fraction for
// hist(). This struct does not know which method produced it.
struct BarPlot {
    CowVec<double> centers;
    CowVec<double> heights;
    double         bar_width = 1.0;
    ErrorBarData   err;
    BarOptions     opts;
};

// Data is row-major, rows×cols float values in [vmin,vmax] (before colormap).
struct HeatmapPlot {
    CowVec<float>  data;
    int            rows = 0, cols = 0;
    HeatmapOptions opts;
};

// Continuous-color scatter — see ScatterZOptions. z is per-point data mapped
// through opts.cmap/vmin/vmax, independent of the x/y position.
struct ScatterZPlot {
    CowVec<double>  x, y, z;
    ErrorBarData    err;
    ScatterZOptions opts;
};

struct AllPlotData {
    const std::vector<LinePlot>&     lines;
    const std::vector<ScatterPlot>&  scatters;
    const std::vector<BarPlot>&      bars;
    const std::vector<HeatmapPlot>&  heatmaps;
    const std::vector<ScatterZPlot>& scatter_z;
};

// Owns everything the render path needs from Axes::Impl, so a background
// render thread never dereferences live Axes state. Decoration and limits are
// copies; the bulk plot data is an immutable *share* of the live buffers
// (CowVec), which gives the same isolation. Built by
// Axes::Impl::build_snapshot() and handed off through SnapshotBox.
struct RenderSnapshot {
    std::vector<LinePlot>     lines;
    std::vector<ScatterPlot>  scatters;
    std::vector<BarPlot>      bars;
    std::vector<HeatmapPlot>  heatmaps;
    std::vector<ScatterZPlot> scatter_z;

    // Their font sizes live in axes_style (see Axes::Impl).
    std::string title, xtitle, ytitle;

    bool          grid_enabled   = false;
    GridOptions   grid_opts;
    bool          legend_enabled = false;
    LegendOptions   legend_opts;
    ColorbarOptions colorbar_opts;
    AxesStyle     axes_style;

    double xmin = 0, xmax = 1, ymin = 0, ymax = 1;
    bool   xlim_auto = true, ylim_auto = true;

    // Explicit tick override set via Axes::set_xticks/set_yticks (or the
    // widget panel's tick table). Absent = fall back to generate_ticks().
    std::optional<std::vector<Tick>> xticks_override, yticks_override;

    AllPlotData all() const { return { lines, scatters, bars, heatmaps, scatter_z }; }
};

// Grid position of one Axes within a Figure (1-indexed, row-major —
// matplotlib add_subplot semantics). rows=cols=index=1 for a Figure that
// never called add_subplot().
struct AxesSlot {
    int rows = 1, cols = 1, index = 1;
};

struct FigureAxesSnapshot {
    AxesSlot       slot;
    RenderSnapshot snap;
};

// What SnapshotBox holds: every Axes in a Figure with its grid slot, captured
// at once so the render thread draws a consistent grid without touching live
// Axes::Impl.

// Monotonic id stamped on every FigureSnapshot as it is built, and the
// invalidation key for the render thread's caches: a snapshot is immutable
// once published, so equal generations guarantee identical plot data. An
// explicit counter rather than the snapshot's address, because a freed
// snapshot's address can be recycled by the allocator.
inline unsigned long long next_snapshot_generation() {
    static std::atomic<unsigned long long> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

struct FigureSnapshot {
    std::vector<FigureAxesSnapshot> axes;

    // 0 means "never stamped" and never matches a cache entry, so a path that
    // forgets to stamp degrades to rebuilding every frame rather than showing
    // stale geometry.
    //
    // `generation` changes whenever *anything* in the snapshot changes;
    // `data_generation` only when the plot *data* does. The split exists
    // because pan and zoom push a limits edit on nearly every frame of a
    // drag: keying the render caches on `generation` would make them miss on
    // exactly the frames they are meant to help.
    unsigned long long generation      = 0;
    unsigned long long data_generation = 0;

    // Copied from FigureOptions::subplot_col_gap/row_gap so the render
    // thread never has to read Figure::Impl::opts directly. These separate whole subplots,
    // decorations included, not bare plot frames.
    float col_gap = 20.0f;
    float row_gap = 20.0f;

    // Border between the figure's edge and the subplot grid; see
    // FigureMargins.
    FigureMargins margins;

    // Whole-figure decoration (Figure::suptitle), as opposed to the per-axes
    // title/xtitle/ytitle in RenderSnapshot.
    std::string suptitle;
    SuptitleOptions suptitle_opts;
};

struct ColorbarRequest { Colormap cmap; float vmin, vmax; };

// A colorbar is a decoration any plot kind can opt into, so the request is
// looked up across every kind that carries the flag rather than hardcoded to
// heatmaps. Only the first request in an axes reserves space. Shared by the
// raster and SVG paths so the two cannot disagree on which axes gets one.
inline std::optional<ColorbarRequest> find_colorbar_request(const RenderSnapshot& snap) {
    for (const auto& hp : snap.heatmaps)
        if (hp.opts.colorbar) return ColorbarRequest{ hp.opts.cmap, hp.opts.vmin, hp.opts.vmax };
    for (const auto& sp : snap.scatter_z)
        if (sp.opts.colorbar) return ColorbarRequest{ sp.opts.cmap, sp.opts.vmin, sp.opts.vmax };
    return std::nullopt;
}

} // namespace sextant
