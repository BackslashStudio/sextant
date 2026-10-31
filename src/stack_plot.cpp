// Axes::stack()'s drawing data: one fill per layer between the stacked curves.
#include "plot_objects.h"
#include "stack_curves.h"

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

} // namespace sextant
