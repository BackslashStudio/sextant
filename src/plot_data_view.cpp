#include "plot_data_view.h"
#include <algorithm>
#include <cstdio>

namespace sextant {

namespace {

// "line 0" / "heatmap 2". Used whenever the plot object has no user name.
// Note hist() produces a BarPlot (there is no separate histogram struct), so
// histograms surface here as "bar N" — same as Axes::bar().
std::string synth_label(const char* kind_name, int index) {
    return std::string(kind_name) + " " + std::to_string(index);
}

// Only some kinds route through here; heatmap, scatter_z, bar3d and surface
// tabs always take the synthesized name, even when the object has a `name`.
std::string pick_label(const std::string& user_label, const char* kind_name, int index) {
    return user_label.empty() ? synth_label(kind_name, index) : user_label;
}

} // namespace

std::vector<PlotDataTable> collect_plot_data_tables(const RenderSnapshot& snap) {
    std::vector<PlotDataTable> out;
    out.reserve(snap.lines.size() + snap.scatters.size() + snap.bars.size()
                + snap.heatmaps.size() + snap.scatter_z.size());

    for (std::size_t i = 0; i < snap.lines.size(); ++i) {
        const auto& lp = snap.lines[i];
        PlotDataTable t;
        t.kind = PlotKind::Line;
        t.plot_index = static_cast<int>(i);
        t.label = pick_label(lp.opts.name, "line", static_cast<int>(i));
        t.columns.push_back({"x", lp.x.data(), lp.x.size()});
        t.columns.push_back({"y", lp.y.data(), lp.y.size()});
        out.push_back(std::move(t));
    }

    for (std::size_t i = 0; i < snap.scatters.size(); ++i) {
        const auto& sp = snap.scatters[i];
        PlotDataTable t;
        t.kind = PlotKind::Scatter;
        t.plot_index = static_cast<int>(i);
        t.label = pick_label(sp.opts.name, "scatter", static_cast<int>(i));
        t.columns.push_back({"x", sp.x.data(), sp.x.size()});
        t.columns.push_back({"y", sp.y.data(), sp.y.size()});
        out.push_back(std::move(t));
    }

    for (std::size_t i = 0; i < snap.bars.size(); ++i) {
        const auto& bp = snap.bars[i];
        PlotDataTable t;
        t.kind = PlotKind::Bar;
        t.plot_index = static_cast<int>(i);
        t.label = pick_label(bp.opts.name, "bar", static_cast<int>(i));
        t.columns.push_back({"center", bp.centers.data(), bp.centers.size()});
        t.columns.push_back({"height", bp.heights.data(), bp.heights.size()});
        t.bar_width = &bp.bar_width;
        out.push_back(std::move(t));
    }

    for (std::size_t i = 0; i < snap.heatmaps.size(); ++i) {
        PlotDataTable t;
        t.kind = PlotKind::Heatmap;
        t.plot_index = static_cast<int>(i);
        t.label = synth_label("heatmap", static_cast<int>(i));
        t.heatmap = &snap.heatmaps[i];
        out.push_back(std::move(t));
    }

    for (std::size_t i = 0; i < snap.scatter_z.size(); ++i) {
        const auto& sp = snap.scatter_z[i];
        PlotDataTable t;
        t.kind = PlotKind::ScatterZ;
        t.plot_index = static_cast<int>(i);
        t.label = synth_label("scatter_z", static_cast<int>(i));
        t.columns.push_back({"x", sp.x.data(), sp.x.size()});
        t.columns.push_back({"y", sp.y.data(), sp.y.size()});
        t.columns.push_back({"z", sp.z.data(), sp.z.size()});
        out.push_back(std::move(t));
    }

    return out;
}

std::string plane_group_label(const PlaneSnapshot& p, int index) {
    const char* o = "XY";
    switch (p.orient) {
        case PlaneOrientation::YZ: o = "YZ"; break;
        case PlaneOrientation::ZX: o = "ZX"; break;
        case PlaneOrientation::XY: break;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "plane %d (%s @ %.4g)", index, o, p.offset);
    return buf;
}

std::vector<PlotDataTable> collect_plot_data_tables(const RenderSnapshot3D& snap) {
    std::vector<PlotDataTable> out;

    // The axes' own objects first. `plane_index` stays -1: a bar3d grid is on
    // the axes, not on a plane, and that is exactly what routes its edits.
    // The tab name is always the synthesized one (see pick_label()).
    for (std::size_t i = 0; i < snap.bars3d.size(); ++i) {
        PlotDataTable t;
        t.kind = PlotKind::Bar3D;
        t.plot_index = static_cast<int>(i);
        t.label = synth_label("bar3d", static_cast<int>(i));
        t.bars3d = &snap.bars3d[i];
        out.push_back(std::move(t));
    }

    // Then the surfaces, in RenderSnapshot3D's own member order. Also on the
    // axes, also plane -1, and also the grid table shape -- which is the whole
    // of what 7d had to add here, because 6c built that shape generally.
    for (std::size_t i = 0; i < snap.surfaces.size(); ++i) {
        PlotDataTable t;
        t.kind = PlotKind::Surface;
        t.plot_index = static_cast<int>(i);
        t.label = synth_label("surface", static_cast<int>(i));
        t.surface = &snap.surfaces[i];
        out.push_back(std::move(t));
    }

    // Then the clouds -- the plain vector shape, and the only 3D kind that
    // takes it: three coordinate columns indexed alike, plus the fourth
    // dimension when the series has one. A flat cloud shows three columns
    // rather than an empty fourth, because a column of nothing is not the
    // same statement as no column.
    for (std::size_t i = 0; i < snap.scatter3d.size(); ++i) {
        const auto& sp = snap.scatter3d[i];
        PlotDataTable t;
        t.kind = PlotKind::Scatter3D;
        t.plot_index = static_cast<int>(i);
        t.label = pick_label(sp.opts.name, "scatter3d", static_cast<int>(i));
        t.columns.push_back({"x", sp.x.data(), sp.x.size()});
        t.columns.push_back({"y", sp.y.data(), sp.y.size()});
        t.columns.push_back({"z", sp.z.data(), sp.z.size()});
        if (sp.colormapped())
            t.columns.push_back({"c", sp.colors.data(), sp.colors.size()});
        out.push_back(std::move(t));
    }

    // Then the paths, which take the cloud's shape exactly and for the same
    // reason: a path's data *is* a cloud's, three independent coordinates and
    // an optional fourth, and what differs is only that the points are joined.
    // So the table is the points, one row each -- not the segments, which have
    // no values of their own and would report a pair of row numbers.
    for (std::size_t i = 0; i < snap.lines3d.size(); ++i) {
        const auto& lp = snap.lines3d[i];
        PlotDataTable t;
        t.kind = PlotKind::Line3D;
        t.plot_index = static_cast<int>(i);
        t.label = pick_label(lp.opts.name, "line3d", static_cast<int>(i));
        t.columns.push_back({"x", lp.x.data(), lp.x.size()});
        t.columns.push_back({"y", lp.y.data(), lp.y.size()});
        t.columns.push_back({"z", lp.z.data(), lp.z.size()});
        if (lp.colormapped())
            t.columns.push_back({"c", lp.colors.data(), lp.colors.size()});
        out.push_back(std::move(t));
    }

    // Then the meshes, which take the cloud's and the path's table shape for
    // the third time: a mesh's vertices are three independent coordinates and
    // an optional fourth, and what differs is only which of them are joined.
    // So the rows are vertices -- not faces, which have no values of their own
    // and would report three row numbers. The topology rides along in `mesh`,
    // read-only.
    for (std::size_t i = 0; i < snap.surface_tri.size(); ++i) {
        const auto& sm = snap.surface_tri[i];
        PlotDataTable t;
        t.kind = PlotKind::SurfaceTri;
        t.plot_index = static_cast<int>(i);
        t.label = pick_label(sm.opts.name, "surface_tri", static_cast<int>(i));
        t.columns.push_back({"x", sm.x.data(), sm.x.size()});
        t.columns.push_back({"y", sm.y.data(), sm.y.size()});
        t.columns.push_back({"z", sm.z.data(), sm.z.size()});
        if (sm.colormapped())
            t.columns.push_back({"c", sm.colors.data(), sm.colors.size()});
        t.mesh = &snap.surface_tri[i];
        t.rows_fixed = true;
        out.push_back(std::move(t));
    }

    for (std::size_t i = 0; i < snap.planes.size(); ++i) {
        const PlaneSnapshot& p = snap.planes[i];
        // The 2D overload verbatim -- a plane's sheet *is* a RenderSnapshot,
        // which is the whole point of §6's arrangement. Only the address is
        // added on top.
        std::vector<PlotDataTable> sheet = collect_plot_data_tables(p.sheet);
        const std::string group = plane_group_label(p, static_cast<int>(i));
        for (PlotDataTable& t : sheet) {
            t.plane_index = static_cast<int>(i);
            t.group       = group;
            out.push_back(std::move(t));
        }
    }
    return out;
}

const char* format_spec(const ValueFormat& vf, char* buf, std::size_t n) {
    const int prec = std::clamp(vf.precision, 0, 17);
    char conv = 'g';
    switch (vf.notation) {
        case ValueFormat::Notation::Fixed:      conv = 'f'; break;
        case ValueFormat::Notation::Scientific: conv = 'e'; break;
        case ValueFormat::Notation::General:    conv = 'g'; break;
    }
    std::snprintf(buf, n, "%%.%d%c", prec, conv);
    return buf;
}

} // namespace sextant
