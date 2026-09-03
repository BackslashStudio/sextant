#include "plot_data_view.h"
#include <algorithm>
#include <cstdio>

namespace sextant {

namespace {

// "line 0" / "heatmap 2". Used whenever the plot object has no user label.
// Note hist() produces a BarPlot (there is no separate histogram struct), so
// histograms surface here as "bar N" — same as Axes::bar().
std::string synth_label(const char* kind_name, int index) {
    return std::string(kind_name) + " " + std::to_string(index);
}

// ScatterZOptions and HeatmapOptions deliberately carry no `label` (a colorbar,
// not a legend swatch, conveys their mapping — see style.h), so those kinds
// always fall through to the synthesized name.
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
        t.label = pick_label(lp.opts.label, "line", static_cast<int>(i));
        t.columns.push_back({"x", lp.x.data(), lp.x.size()});
        t.columns.push_back({"y", lp.y.data(), lp.y.size()});
        out.push_back(std::move(t));
    }

    for (std::size_t i = 0; i < snap.scatters.size(); ++i) {
        const auto& sp = snap.scatters[i];
        PlotDataTable t;
        t.kind = PlotKind::Scatter;
        t.plot_index = static_cast<int>(i);
        t.label = pick_label(sp.opts.label, "scatter", static_cast<int>(i));
        t.columns.push_back({"x", sp.x.data(), sp.x.size()});
        t.columns.push_back({"y", sp.y.data(), sp.y.size()});
        out.push_back(std::move(t));
    }

    for (std::size_t i = 0; i < snap.bars.size(); ++i) {
        const auto& bp = snap.bars[i];
        PlotDataTable t;
        t.kind = PlotKind::Bar;
        t.plot_index = static_cast<int>(i);
        t.label = pick_label(bp.opts.label, "bar", static_cast<int>(i));
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
