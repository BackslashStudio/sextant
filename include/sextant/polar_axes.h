#pragma once
#include "axes.h"
#include "events.h"
#include "export.h"
#include "object_id.h"
#include "style.h"
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sextant {
    // Where θ = 0 points on screen (matplotlib's set_theta_zero_location()).
    enum class ThetaZero { E, N, W, S, NE, NW, SW, SE };

    // A polar subplot, from Figure::add_subplot_polar(). θ is in radians in
    // data (plot(), line_data(), events), in degrees in every setter that
    // places the frame or its grid, as matplotlib's. r runs outward from rmin
    // at the centre to rmax at the frame. Part of its Figure's object graph, as
    // Axes: any thread, one call at a time per graph.
    class SEXTANT_API PolarAxes {
    public:
        ~PolarAxes();

        // ----------------------------------------------------------------
        // Plot methods
        // ----------------------------------------------------------------

        // A line through (theta[i], r[i]), straight in (θ, r): drawn as an
        // arc where θ changes. NaN breaks it. LineOptions as Axes::line()
        // (drawstyle steps in θ, loop closes back to the first point).
        PolarAxes& plot(std::span<const double> theta, std::span<const double> r,
                        LineOptions opts = {});

        // theta = 0, 1, 2, ... radians.
        PolarAxes& plot(std::span<const double> r, LineOptions opts = {});

        // Markers at (theta[i], r[i]), ScatterOptions as Axes::scatter(). A
        // point outside the r limits or the θ span isn't drawn.
        PolarAxes& scatter(std::span<const double> theta, std::span<const double> r,
                           ScatterOptions opts = {});

        // Markers colored by z through a colormap, ScatterZOptions as
        // Axes::scatter_z() (its colorbar beside the frame).
        PolarAxes& scatter_z(std::span<const double> theta, std::span<const double> r,
                             std::span<const double> z, ScatterZOptions opts = {});

        // Ring sectors: bar i spans θ = theta[i] ± width/2 (radians; width as
        // Axes::bar(), a fraction of the θ spacing) and r from bottom[i] to
        // bottom[i] + height[i], cut to the r limits and the θ span. Horizontal
        // bars throw std::invalid_argument.
        PolarAxes& bar(std::span<const double> theta, std::span<const double> height,
                       BarOptions opts = {});

        // ----------------------------------------------------------------
        // Objects, as Axes's
        // ----------------------------------------------------------------

        std::size_t line_count() const;
        // x is θ (radians), y is r.
        LineData line_data(std::size_t i) const;
        PolarAxes& set_line_data(std::size_t i, const LineData& data);
        PolarAxes& set_line_data(std::size_t i, std::span<const double> theta,
                                 std::span<const double> r);
        PolarAxes& set_line_visible(std::size_t i, bool visible);
        bool line_visible(std::size_t i) const;

        // x is θ (radians), y is r.
        std::size_t scatter_count() const;
        ScatterData scatter_data(std::size_t i) const;
        PolarAxes& set_scatter_data(std::size_t i, const ScatterData& data);
        PolarAxes& set_scatter_data(std::size_t i, std::span<const double> theta,
                                    std::span<const double> r);
        PolarAxes& set_scatter_visible(std::size_t i, bool visible);
        bool scatter_visible(std::size_t i) const;

        std::size_t scatter_z_count() const;
        ScatterZData scatter_z_data(std::size_t i) const;
        PolarAxes& set_scatter_z_data(std::size_t i, const ScatterZData& data);
        PolarAxes& set_scatter_z_data(std::size_t i, std::span<const double> theta,
                                      std::span<const double> r, std::span<const double> z);
        PolarAxes& set_scatter_z_visible(std::size_t i, bool visible);
        bool scatter_z_visible(std::size_t i) const;

        // Centres are θ (radians), heights and bottoms r; as Axes::set_bar_data()
        // a change of centres re-derives the width.
        std::size_t bar_count() const;
        BarData bar_data(std::size_t i) const;
        PolarAxes& set_bar_data(std::size_t i, const BarData& data);
        PolarAxes& set_bar_data(std::size_t i, std::span<const double> theta,
                                std::span<const double> height);
        PolarAxes& set_bar_visible(std::size_t i, bool visible);
        bool bar_visible(std::size_t i) const;

        ObjectId last_object() const;
        bool remove(ObjectId id);
        std::optional<ObjectLocation> find_object(ObjectId id) const;
        bool set_zorder(ObjectId id, double zorder);
        std::optional<double> zorder(ObjectId id) const;

        // ----------------------------------------------------------------
        // Decoration, as Axes's
        // ----------------------------------------------------------------

        PolarAxes& set_title(std::string_view text, float fontsize = 18.0f);
        std::string title() const;
        PolarAxes& legend(LegendOptions opts = {});
        PolarAxes& set_color_cycle(std::vector<Color> colors);
        // The grid is on by default here: rings at the r ticks, spokes at the
        // θ grid lines.
        PolarAxes& grid(bool enable, GridOptions opts = {});
        // background fills the frame, spine_color/spine_linewidth stroke its
        // outline, tick_color/label sizes style the labels; show_xticks hides
        // the θ labels and show_yticks the r labels. Axis positions, origins,
        // tick lengths and minor ticks don't apply.
        PolarAxes& set_axes_style(AxesStyle opts);
        // As Axes::set_colorbar_style(): a colorbar comes from
        // ScatterZOptions::colorbar.
        PolarAxes& set_colorbar_style(ColorbarOptions opts = {});
        // Everything back to a new polar axes' defaults (the plot objects too).
        PolarAxes& cla();

        // ----------------------------------------------------------------
        // The r axis
        // ----------------------------------------------------------------

        // Throw unless both are finite and lo != hi (lo > hi turns r inward).
        PolarAxes& set_rlim(double lo, double hi);
        // One end, the other kept at its current value (rlim()).
        PolarAxes& set_rmin(double lo);
        PolarAxes& set_rmax(double hi);
        // As drawn: set ones, or automatic: rmin 0 when no shown r is negative
        // (else the smallest, rounded down to a tick), rmax the largest
        // rounded up to a tick, [0, 1] with no data.
        Range rlim() const;

        // Rings at `positions` labelled `labels` (empty = formatted); empty
        // positions goes back to automatic ones.
        PolarAxes& set_rticks(std::span<const double> positions, std::vector<std::string> labels = {});
        // As Axes::set_ytick_format().
        PolarAxes& set_rtick_format(std::string_view fmt);
        // The angle (degrees, in θ) the r labels sit along.
        PolarAxes& set_rlabel_position(double degrees);
        double rlabel_position() const;

        // ----------------------------------------------------------------
        // The θ axis (degrees)
        // ----------------------------------------------------------------

        // Spokes at `degrees` labelled `labels` (empty = "45°" ...); empty
        // degrees goes back to every 45°.
        PolarAxes& set_thetagrids(std::span<const double> degrees, std::vector<std::string> labels = {});

        // θ = 0 at `loc`, turned a further `offset` degrees counterclockwise.
        PolarAxes& set_theta_zero_location(ThetaZero loc, double offset = 0.0);
        // The screen angle of θ = 0, degrees counterclockwise from east.
        PolarAxes& set_theta_offset(double degrees);
        double theta_offset() const;
        // 1 counterclockwise (the default), -1 clockwise; anything else throws.
        PolarAxes& set_theta_direction(int direction);
        int theta_direction() const;

        // The θ span drawn: a sector between thetamin and thetamax (degrees,
        // finite, thetamin < thetamax, at most 360 apart; else throws).
        PolarAxes& set_thetamin(double degrees);
        PolarAxes& set_thetamax(double degrees);
        PolarAxes& set_thetalim(double min_degrees, double max_degrees);
        Range thetalim() const;   // degrees

    private:
        struct Impl;
        std::unique_ptr<Impl> d;
        friend class Figure;

        explicit PolarAxes(std::shared_ptr<Axes> sheet);
    };
} // namespace sextant
