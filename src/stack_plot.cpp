// Axes::stack()'s drawing data: one fill per layer between the stacked curves;
// Axes::bar_stack()'s: one bar per layer on the running sums.
#include "plot_objects.h"
#include "stack_curves.h"

#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

namespace sextant {

void stack_rebuild(StackPlot& p) {
    std::vector<std::vector<double>> ys;
    ys.reserve(p.ys.size());
    for (const CowVec<double>& y : p.ys) ys.push_back(y.get());
    const std::vector<std::vector<double>> curves = stack_curves(ys, p.x.size(), p.opts.baseline);

    const StackOptions& o = p.opts;
    auto layers = std::make_shared<std::vector<FillPlot>>();
    layers->reserve(ys.size());
    for (std::size_t k = 0; k < ys.size(); ++k) {
        FillPlot f;
        f.x = p.x;
        f.y1 = curves[k];
        f.y2 = curves[k + 1];
        f.opts.orientation = o.orientation;
        f.opts.color = k < o.colors.size() ? o.colors[k] : Color::Blue;
        f.opts.alpha = o.alpha;
        f.opts.edgecolor = o.edgecolor;
        f.opts.linewidth = o.linewidth;
        f.opts.linestyle = o.linestyle;
        f.opts.drawstyle = o.drawstyle;
        if (k < o.names.size()) f.opts.name = o.names[k];
        f.opts.show_legend = o.show_legend;
        f.data_stamp = p.data_stamp;
        f.id = p.id;
        layers->push_back(std::move(f));
    }
    p.layers = std::move(layers);
}

// Each position keeps two running sums from its bottom: a positive height
// stacks up from the one, a negative height down from the other; a NaN draws
// no bar and moves neither.
void bar_stack_rebuild(BarStackPlot& p) {
    const BarStackOptions& o = p.opts;
    const std::size_t n = p.x.size();
    const double spacing = n > 1 ? std::abs(p.x[1] - p.x[0]) : 1.0;
    std::vector<double> up(n), down(n);
    for (std::size_t i = 0; i < n; ++i) up[i] = down[i] = i < p.bottom.size() ? p.bottom[i] : 0.0;

    auto layers = std::make_shared<std::vector<BarPlot>>();
    layers->reserve(p.heights.size());
    for (std::size_t k = 0; k < p.heights.size(); ++k) {
        const CowVec<double>& h = p.heights[k];
        std::vector<double> base(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double v = h[i];
            if (std::isnan(v)) {
                base[i] = up[i];
            } else if (v < 0.0) {
                base[i] = down[i];
                down[i] += v;
            } else {
                base[i] = up[i];
                up[i] += v;
            }
        }
        BarPlot b;
        b.centers = p.x;
        b.heights = h;
        b.bottom = std::move(base);
        b.bar_width = spacing * static_cast<double>(o.width);
        b.opts.orientation = o.orientation;
        b.opts.color = k < o.colors.size() ? o.colors[k] : Color::Blue;
        b.opts.width = o.width;
        b.opts.alpha = o.alpha;
        if (k < o.names.size()) b.opts.name = o.names[k];
        b.opts.show_legend = o.show_legend;
        b.opts.edgecolor = o.edgecolor;
        b.opts.linewidth = o.linewidth;
        b.data_stamp = p.data_stamp;
        b.id = p.id;
        layers->push_back(std::move(b));
    }
    p.layers = std::move(layers);
}

} // namespace sextant
