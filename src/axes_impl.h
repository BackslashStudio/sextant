#pragma once
// Internal header — defines Axes::Impl.
#include "sextant/axes.h"
#include "sextant/style.h"
#include "color_cycle.h"
#include "plot_objects.h"
#include "tick.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sextant {

struct ResolvedLimits;
struct AxesEdit;

struct Axes::Impl {
    std::vector<LinePlot>     lines;
    std::vector<ScatterPlot>  scatters;
    std::vector<BarPlot>      bars;
    std::vector<HeatmapPlot>  heatmaps;
    std::vector<ScatterZPlot> scatter_z;
    std::vector<TextPlot>     texts;
    std::vector<AxLinePlot>   axlines;
    std::vector<AxSpanPlot>   axspans;
    std::vector<FillPlot>     fills;
    std::vector<QuiverPlot>   quivers;

    // The figure's FigureOptions::mathtext, set when the figure makes this
    // axes; only warn_math() reads it (drawing reads the snapshot's).
    bool mathtext = true;

    // What an unset `color` takes, at ingest. A plane's sheet shares its
    // Axes3D's (see Axes3D::plane()); cla() of the sheet keeps it.
    std::shared_ptr<ColorCycle> cycle = std::make_shared<ColorCycle>();

    // Font sizes for these live in axes_style, so the panel edits them via AxesEdit.
    std::string   title, xtitle, ytitle;
    TitleStamps   title_stamps;
    bool          grid_enabled    = false;
    GridOptions   grid_opts;
    bool          legend_enabled  = false;
    LegendOptions   legend_opts;
    ColorbarOptions colorbar_opts;
    AxesStyle     axes_style;

    // Axis limits (auto = not yet set by user)
    double xmin = 0, xmax = 1;
    double ymin = 0, ymax = 1;
    bool   xlim_auto = true;
    bool   ylim_auto = true;
    LimitStamps limit_stamps;
    StyleStamps style_stamps;

    // set_xscale()/set_yscale(). A plane's sheet keeps Linear (Plane2D has no
    // scale), so ingest_heatmap() only refuses on an Axes.
    AxisScale xscale, yscale;

    // The figure that made this axes (its process-unique figure id, never
    // reused, unlike an address); sharex() refuses another figure's axes. 0 for
    // a plane's sheet, which never shares.
    std::uint64_t owner = 0;

    // sharex()/sharey(). A group owns one axis' value domain:
    // its limits (lo, hi, auto), their LimitStamps part and its scale. Every
    // member holds the same copy of those fields, so readers stay per-axes;
    // whatever writes them on one member copies them to the rest
    // (sync_shared_x/y()). Members leave in ~Axes(); cla() keeps membership.
    struct ShareGroup {
        unsigned long long id = 0;   // process-unique; RenderSnapshot::xshare/yshare
        std::vector<Impl*> members;
    };
    std::shared_ptr<ShareGroup> xshare, yshare;

    // Copy this axes' shared x (y) fields to the other members of its group.
    void sync_shared_x() const;
    void sync_shared_y() const;
    // Take the shared fields from another member, if there is one (cla()).
    void adopt_shared_x();
    void adopt_shared_y();
    // Whether any member of the x (y) group, or this axes alone, holds a heatmap.
    bool group_has_heatmap_x() const;
    bool group_has_heatmap_y() const;
    void leave_groups();
    // sharex() (x) / sharey() on `other`; see Axes::sharex().
    void share(Impl& other, bool x, const char* who);
    // The limits as layout resolves them: a shared axis over its whole group.
    ResolvedLimits resolve_live() const;
    // A panel edit replayed on the caller thread: applied (minus a scale the
    // group can't take), then the group follows. Defined in figure.cpp.
    void apply_shared_edit(const AxesEdit& e);

    // twinx()/twiny(). The host owns its twins; a twin points
    // back at it (null once the host is gone, see ~Axes()) and is its layer
    // (kLayerTwinX/Y) of the cell. A twin shares the host's color cycle, and
    // its title, legend and colorbar setters write the host's (cell()).
    Impl* host = nullptr;
    int layer = 0;
    std::shared_ptr<Axes> twin_x, twin_y;
    Impl& cell() { return host ? *host : *this; }
    const Impl& cell() const { return host ? *host : *this; }
    // twinx() (x = true) / twiny().
    std::shared_ptr<Axes> make_twin(bool x, const char* who);
    // What a twin starts with: its shared axis hidden.
    void twin_defaults();
    // Whose x (y) axis this axes' x (y) calls address: the
    // host's for a twin's shared axis (a twinx's x), else its own.
    bool shares_host_axis(bool x) const { return host && layer == (x ? kLayerTwinX : kLayerTwinY); }
    Impl& axis_owner(bool x) { return shares_host_axis(x) ? *host : *this; }
    const Impl& axis_owner(bool x) const { return shares_host_axis(x) ? *host : *this; }
    // set_axes_style() on a twin: see Axes::twinx().
    void set_twin_style(const AxesStyle& s);
    // set_xtick_format()/set_ytick_format(); empty = the scale's default.
    std::string xtick_format, ytick_format;
    // set_aspect().
    AxesAspect aspect;

    // Explicit tick override set via Axes::set_xticks/set_yticks (or the
    // widget panel's tick table). Absent = auto-generated ticks.
    std::optional<std::vector<Tick>> xticks_override, yticks_override;

    // Every plotting call takes its object's id here, so last_object() can
    // answer; a call that makes several objects leaves the last one's.
    ObjectId last_id = 0;
    ObjectId new_id() { return last_id = next_object_id(); }

    // Plot objects by identity (see ObjectId). Axes::remove() is the public
    // removal; reorder and restore stay internal. Each is a caller-thread change
    // like a plotting call, reaching a window at the next publish; panel edits
    // made over the old order follow their objects (or drop with them).
    // Reorder is within a kind; draw and legend order follow the ids
    // (call_order()), so a move changes neither.
    // Each returns what undoes it: the removed object for
    // restore_object(), the old index for a move back.
    std::optional<ObjectRef> find_object(ObjectId id) const {
        return find_object_in(*this, kPlotKinds2D, id);
    }
    std::optional<RemovedObject<PlotObject2D>> remove_object(ObjectId id) {
        return remove_object_in<PlotObject2D>(*this, kPlotKinds2D, id);
    }
    bool restore_object(RemovedObject<PlotObject2D> r) {
        return restore_object_in(*this, kPlotKinds2D, std::move(r));
    }
    std::optional<std::size_t> move_object(ObjectId id, std::size_t to) {
        return move_object_in(*this, kPlotKinds2D, id, to);
    }

    // Copy for the render thread. Plot vectors are CowVec, so this shares
    // buffers instead of copying the data.
    RenderSnapshot build_snapshot() const {
        RenderSnapshot s;
        s.lines = lines; s.scatters = scatters; s.bars = bars; s.heatmaps = heatmaps;
        s.scatter_z = scatter_z;
        s.texts = texts;
        s.axlines = axlines; s.axspans = axspans;
        s.fills = fills;
        s.quivers = quivers;
        s.title = title; s.xtitle = xtitle; s.ytitle = ytitle;
        s.title_stamps = title_stamps;
        s.grid_enabled   = grid_enabled;   s.grid_opts   = grid_opts;
        s.legend_enabled = legend_enabled; s.legend_opts = legend_opts;
        s.colorbar_opts = colorbar_opts;
        s.axes_style = axes_style;
        s.xmin = xmin; s.xmax = xmax; s.ymin = ymin; s.ymax = ymax;
        s.xlim_auto = xlim_auto; s.ylim_auto = ylim_auto;
        s.limit_stamps = limit_stamps;
        s.style_stamps = style_stamps;
        s.xscale = xscale; s.yscale = yscale;
        s.xshare = xshare ? xshare->id : 0;
        s.yshare = yshare ? yshare->id : 0;
        s.xtick_format = xtick_format; s.ytick_format = ytick_format;
        s.aspect = aspect;
        s.xticks_override = xticks_override;
        s.yticks_override = yticks_override;
        return s;
    }

    // Ingest shared by Axes and Plane2D, so both validate identically. `who`
    // names the caller in error messages. No hist(): planes don't expose it.
    void ingest_line(std::span<const double> x, std::span<const double> y,
                     const ErrorBar& err, LineOptions opts, const char* who);
    void ingest_scatter(std::span<const double> x, std::span<const double> y,
                        const ErrorBar& err, ScatterOptions opts, const char* who);
    void ingest_scatter_z(std::span<const double> x, std::span<const double> y,
                          std::span<const double> z, const ErrorBar& err,
                          ScatterZOptions opts, const char* who);
    void ingest_bar(std::span<const double> x, std::span<const double> height,
                    const ErrorBar& err, BarOptions opts, const char* who);
    void push_bars(std::vector<double> centers, std::vector<double> heights, double width,
                   ErrorBarData err, BarOptions opts, const char* who);
    void ingest_heatmap(std::span<const double> data, int rows, int cols,
                        Range xrange, Range yrange, HeatmapOptions opts,
                        const char* who);

    // set_*_data() for Axes and Plane2D: replace object i's data, validated as
    // when it was plotted. `who` names the caller.
    void set_line_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                       const char* who);
    // `sizes`: the new per-point sizes (a *_Data overload), or null to keep the
    // current ones while the point count is unchanged (a span overload).
    void set_scatter_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                          const std::vector<double>* sizes, const char* who);
    void set_scatter_z_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                            std::span<const double> z, const std::vector<double>* sizes,
                            const char* who);
    // `bottom`: the new bases (BarData), or null to keep them as `sizes` above.
    void set_bar_data(std::size_t i, std::span<const double> x, std::span<const double> height,
                      const std::vector<double>* bottom, const char* who);
    void set_heatmap_data(std::size_t i, std::span<const double> data, int rows, int cols,
                          Range xrange, Range yrange, const char* who);

    // fill() for Axes and Plane2D: x, y1 and y2 one value each per
    // point. set_fill_data(): `exclude` the new mask (FillData), or null to
    // keep it while the point count is unchanged.
    void ingest_fill(std::span<const double> x, std::span<const double> y1,
                     std::span<const double> y2, FillOptions opts, const char* who);
    // stack() for Axes and Plane2D: every check first, then one fill per layer.
    void ingest_stack(std::span<const double> x, std::span<const std::vector<double>> ys,
                      const StackOptions& opts, const char* who);
    void set_fill_data(std::size_t i, std::span<const double> x, std::span<const double> y1,
                       std::span<const double> y2, const std::vector<bool>* exclude,
                       const char* who);

    // quiver() for Axes and Plane2D: x, y, u and v one value each per arrow,
    // `c` (null = flat) one per arrow too; an unset amp is resolved here
    // (quiver_auto_amp()). set_quiver_data() keeps the amp; `c` the new values
    // (QuiverData, empty = flat), or null to keep them while the count holds.
    void ingest_quiver(std::span<const double> x, std::span<const double> y,
                       std::span<const double> u, std::span<const double> v,
                       const std::span<const double>* c, QuiverOptions opts, const char* who);
    void set_quiver_data(std::size_t i, std::span<const double> x, std::span<const double> y,
                         std::span<const double> u, std::span<const double> v,
                         const std::vector<double>* c, const char* who);

    // Text (Axes only; a plane has none). Throw on a non-finite number.
    void ingest_text(TextContent c, TextStyle style, const char* who);
    void set_text_data(std::size_t i, TextContent c, const char* who);

    // Reference lines and spans (Axes only). Throw on a non-finite number.
    void ingest_axline(double value, AxLineOptions opts, const char* who);
    void ingest_axspan(double lo, double hi, AxSpanOptions opts, const char* who);
};

} // namespace sextant
