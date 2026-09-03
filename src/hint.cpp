#include "hint.h"
#include <algorithm>
#include <cstdio>

namespace sextant {

namespace {

std::string append_label(std::string base, const std::vector<std::string>& labels, std::size_t idx) {
    if (idx < labels.size() && !labels[idx].empty()) {
        base += '\n';
        base += labels[idx];
    }
    return base;
}

std::string fmt_point(double x, double y) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g", x, y);
    return buf;
}

// "y=2.5" becomes "y=2.5 +/-0.3 [2.0, 3.1]": the variance box's half-extent,
// then the whisker's range. Either half is dropped when the series does not
// carry it. Written per value rather than appended to the end of the line, so
// an x uncertainty sits next to x and a y one next to y.
std::string fmt_value(const char* name, double v,
                      bool has_var, double var,
                      bool has_span, double lo, double hi) {
    char buf[160];
    int n = std::snprintf(buf, sizeof(buf), "%s=%.4g", name, v);
    if (has_var && var != 0.0)
        n += std::snprintf(buf + n, sizeof(buf) - static_cast<std::size_t>(n),
                           " \xC2\xB1%.4g", var);
    if (has_span && lo != hi)
        std::snprintf(buf + n, sizeof(buf) - static_cast<std::size_t>(n),
                      " [%.4g, %.4g]", lo, hi);
    return buf;
}

std::string fmt_err_pair(const char* xname, double x, const char* yname, double y,
                         const ErrorBarData& err, std::size_t i) {
    return fmt_value(xname, x, err.has_x_box(), err.x_var(i),
                     err.has_x_span(), err.x_lo(i, x), err.x_hi(i, x))
         + ", "
         + fmt_value(yname, y, err.has_y_box(), err.y_var(i),
                     err.has_y_span(), err.y_lo(i, y), err.y_hi(i, y));
}

std::string fmt_point_err(double x, double y, const ErrorBarData& err, std::size_t i) {
    if (err.empty()) return fmt_point(x, y);
    return fmt_err_pair("x", x, "y", y, err, i);
}

// No no-error fast path here, unlike fmt_point_err: with nothing to report
// fmt_value emits exactly "x=%.4g" / "height=%.4g", so this reproduces the
// old fmt_bar() text character for character and that function is gone.
std::string fmt_bar_err(double x, double h, const ErrorBarData& err, std::size_t i) {
    return fmt_err_pair("x", x, "height", h, err, i);
}

std::string fmt_z(double x, double y, double z) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "x=%.4g, y=%.4g, z=%.4g", x, y, z);
    return buf;
}

// Z is the colormapped value, which carries no error bar of its own — only
// the position does, so it is appended plain.
std::string fmt_z_err(double x, double y, double z,
                      const ErrorBarData& err, std::size_t i) {
    if (err.empty()) return fmt_z(x, y, z);
    char buf[48];
    std::snprintf(buf, sizeof(buf), ", z=%.4g", z);
    return fmt_err_pair("x", x, "y", y, err, i) + buf;
}

std::string fmt_heatmap(int row, int col, float value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "row=%d, col=%d, value=%.4g", row, col, value);
    return buf;
}

} // namespace

const AxesLayout* find_hint_cell(const std::vector<AxesLayout>& layout,
                                  float cursor_x, float cursor_y) {
    for (const auto& al : layout) {
        const auto& tr = al.tr;
        if (cursor_x >= tr.px && cursor_x < tr.px + tr.pw &&
            cursor_y >= tr.py && cursor_y < tr.py + tr.ph)
            return &al;
    }
    return nullptr;
}

std::optional<HintResult> find_hint(const RenderSnapshot& snap, const CoordTransform& tr,
                                    float cursor_x, float cursor_y,
                                    HintIndexCache* index) {
    // Runs on every frame the cursor is over the plot, so it is written in two
    // phases. Phase 1 finds the nearest point and does *no* string work --
    // formatting a candidate that is then discarded costs one allocation per
    // point examined, which at 200k points dominated everything else. Phase 2
    // formats the winner alone.
    //
    // Candidates come from a data-space bucket grid (hint_index.h) rather than
    // a scan of every point: the box below is the hit radius mapped back into
    // data space, and only points in the overlapping cells are transformed.
    // Without an index PointGrid falls back to a linear scan applying the same
    // box test inline.
    double x_lo = tr.to_data_x(cursor_x - kHintHitRadiusPx);
    double x_hi = tr.to_data_x(cursor_x + kHintHitRadiusPx);
    if (x_lo > x_hi) { const double t = x_lo; x_lo = x_hi; x_hi = t; }  // inverted axis
    // Screen y grows downward and data y upward, so this pair always needs
    // the swap the x pair only needs on an inverted axis.
    double y_lo = tr.to_data_y(cursor_y + kHintHitRadiusPx);
    double y_hi = tr.to_data_y(cursor_y - kHintHitRadiusPx);
    if (y_lo > y_hi) { const double t = y_lo; y_lo = y_hi; y_hi = t; }

    float best_d2 = kHintHitRadiusPx * kHintHitRadiusPx;
    int         best_kind = -1;          // 0 line, 1 scatter, 2 scatter_z, 3 bar
    std::size_t best_obj  = 0, best_i = 0;
    float       best_px   = 0.0f, best_py = 0.0f;

    // The candidate set is only conservative — the true (circular, pixel-space)
    // test is here, and is what makes the indexed and unindexed paths agree.
    auto consider = [&](float px, float py,
                        int kind, std::size_t obj, std::size_t i) {
        const float dx = px - cursor_x, dy = py - cursor_y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= best_d2) {
            best_d2 = d2;
            best_kind = kind; best_obj = obj; best_i = i;
            best_px = px; best_py = py;
        }
    };

    // The no-index fallback is a shared *const* pass-through grid rather than
    // a dummy cache object: nothing mutates it, so no thread-visible state is
    // introduced by taking this branch.
    static const PointGrid kScan{};

    auto sweep = [&](PlotKind kind, std::size_t o, int kind_id,
                     const CowVec<double>& xs, const CowVec<double>& ys) {
        const PointGrid& g = index ? index->grid(kind, o, xs, ys) : kScan;
        g.for_each_in(xs, ys, x_lo, x_hi, y_lo, y_hi,
                      [&](std::size_t i) {
                          consider(tr.to_px(xs[i]), tr.to_py(ys[i]), kind_id, o, i);
                      });
    };

    for (std::size_t o = 0; o < snap.lines.size(); ++o)
        sweep(PlotKind::Line, o, 0, snap.lines[o].x, snap.lines[o].y);
    for (std::size_t o = 0; o < snap.scatters.size(); ++o)
        sweep(PlotKind::Scatter, o, 1, snap.scatters[o].x, snap.scatters[o].y);
    for (std::size_t o = 0; o < snap.scatter_z.size(); ++o)
        sweep(PlotKind::ScatterZ, o, 2, snap.scatter_z[o].x, snap.scatter_z[o].y);
    for (std::size_t o = 0; o < snap.bars.size(); ++o)
        sweep(PlotKind::Bar, o, 3, snap.bars[o].centers, snap.bars[o].heights);

    // Phase 2: build the text for the winning point only.
    if (best_kind >= 0) {
        std::string text;
        switch (best_kind) {
            case 0: {
                const auto& lp = snap.lines[best_obj];
                text = append_label(fmt_point_err(lp.x[best_i], lp.y[best_i], lp.err, best_i),
                                    lp.opts.hint_labels, best_i);
                break;
            }
            case 1: {
                const auto& sp = snap.scatters[best_obj];
                text = append_label(fmt_point_err(sp.x[best_i], sp.y[best_i], sp.err, best_i),
                                    sp.opts.hint_labels, best_i);
                break;
            }
            case 2: {
                const auto& sp = snap.scatter_z[best_obj];
                text = append_label(fmt_z_err(sp.x[best_i], sp.y[best_i], sp.z[best_i], sp.err, best_i),
                                    sp.opts.hint_labels, best_i);
                break;
            }
            default: {
                const auto& bp = snap.bars[best_obj];
                text = append_label(fmt_bar_err(bp.centers[best_i], bp.heights[best_i], bp.err, best_i),
                                    bp.opts.hint_labels, best_i);
                break;
            }
        }
        return HintResult{ std::move(text), best_px, best_py };
    }

    // Heatmap fallback: cursor inside a heatmap's [0,cols)x[0,rows) data-space
    // cell. Storage (RenderSnapshot::heatmaps[i].data) is always row-major
    // with row 0 first, regardless of origin — only the GPU texture upload is
    // vertically flipped for origin=="lower" (see DataRenderer::draw_heatmap
    // in data_renderer.cpp). To find the storage row actually displayed at a
    // given data-y, reverse that same flip here.
    const double dx = tr.to_data_x(cursor_x), dy = tr.to_data_y(cursor_y);
    for (const auto& hp : snap.heatmaps) {
        if (hp.rows <= 0 || hp.cols <= 0) continue;
        if (dx < 0.0 || dx >= hp.cols || dy < 0.0 || dy >= hp.rows) continue;

        const int col = std::clamp(static_cast<int>(dx), 0, hp.cols - 1);
        const int row_from_bottom = std::clamp(static_cast<int>(dy), 0, hp.rows - 1);
        const int row = (hp.opts.origin == "lower") ? row_from_bottom : (hp.rows - 1 - row_from_bottom);
        const std::size_t idx = static_cast<std::size_t>(row) * static_cast<std::size_t>(hp.cols)
                               + static_cast<std::size_t>(col);
        const float value = idx < hp.data.size() ? hp.data[idx] : 0.0f;
        return HintResult{ append_label(fmt_heatmap(row, col, value), hp.opts.hint_labels, idx),
                            cursor_x, cursor_y };
    }

    return std::nullopt;
}

} // namespace sextant
