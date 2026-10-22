// Heatmaps on every axis scale, and heatmap_rec() (cells of any size): GL
// pixels where the layout's transform puts the cell edges, clipping at <= 0 on
// a log axis, the SVG's per-cell rects and raster, limits, picks, read-back
// and the refusals. Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "heatmap_pixels.h"

namespace lt {
    using namespace sextant;

    namespace {
        constexpr int W = 400, H = 300;

        AxisScale log10s() { return make_axis_scale(Scale::Log, {}); }
        AxisScale symlog() { return make_axis_scale(Scale::Symlog, {}); }

        FigureSnapshot one_axes(RenderSnapshot s) {
            FigureSnapshot fs;
            FigureAxesSnapshot fa;
            fa.slot = AxesSlot{1, 1, 1};
            fa.snap = std::move(s);
            fs.axes.push_back(std::move(fa));
            return fs;
        }

        // Fixed limits and scales, a grey plot area (so a transparent cell shows).
        RenderSnapshot limits(double x0, double x1, double y0, double y1, AxisScale xs, AxisScale ys) {
            RenderSnapshot s;
            s.xmin = x0;
            s.xmax = x1;
            s.ymin = y0;
            s.ymax = y1;
            s.xlim_auto = s.ylim_auto = false;
            s.xscale = xs;
            s.yscale = ys;
            s.axes_style.background = {0.5f, 0.5f, 0.5f, 1.0f};
            return s;
        }

        // One row of values 0, 0.5, 1, ... through viridis, so each cell has its own colour.
        HeatmapPlot row_of(std::vector<float> v, Range xr, Range yr) {
            const int n = static_cast<int>(v.size());
            return make_heatmap(1, n, [v](int, int c) { return v[static_cast<std::size_t>(c)]; }, {}, xr, yr);
        }

        HeatmapPlot with_edges(HeatmapPlot hp, std::vector<double> xe, std::vector<double> ye) {
            hp.xrange = {xe.front(), xe.back()};
            hp.yrange = {ye.front(), ye.back()};
            hp.x_edges = std::move(xe);
            hp.y_edges = std::move(ye);
            return hp;
        }

        struct Probe {
            RgbaImage im;
            CoordTransform tr;
            const std::uint8_t* at(float x, float y) const {
                static const std::uint8_t none[4] = {0, 0, 0, 0};
                const int ix = static_cast<int>(x), iy = static_cast<int>(y);
                if (ix < 0 || iy < 0 || ix >= im.width || iy >= im.height) return none;
                return &im.pixels[(static_cast<std::size_t>(iy) * im.width + ix) * 4];
            }
        };

        bool is(const std::uint8_t* p, const std::uint8_t* c) {
            return std::abs(p[0] - c[0]) <= 2 && std::abs(p[1] - c[1]) <= 2 && std::abs(p[2] - c[2]) <= 2;
        }

        bool throws(const auto& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }

        std::size_t count(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
            return n;
        }
    } // namespace

    void test_heatmap_scale() {
        std::printf("\n[heatmaps on every scale; heatmap_rec]\n");

        // ---- The mesh accessors.
        {
            const HeatmapPlot up = with_edges(row_of({0, 0, 0}, {0, 1}, {0, 1}), {1, 10, 100, 1000}, {0, 1});
            check(up.mesh() && up.x_edge(2) == 100.0 && up.x_at(1.5) == 55.0 && up.x_at(3.0) == 1000.0,
                  "mesh: x_at() is linear within a cell, x_edge() exact");
            check(up.col_at(55.0) == 1.5 && up.col_at(1000.0) == 3.0 && up.col_at(0.0) < 0.0,
                  "mesh: col_at() is its inverse, past either end too");
            const HeatmapPlot down = with_edges(row_of({0, 0, 0}, {0, 1}, {0, 1}), {1000, 100, 10, 1}, {0, 1});
            check(down.col_at(55.0) == 1.5 && down.col_at(1000.0) == 0.0 && down.col_at(0.5) > 3.0,
                  "mesh: and for decreasing edges");
            const HeatmapPlot even = row_of({0, 0, 0}, {0, 3}, {0, 1});
            check(!even.mesh() && even.x_edge(3) == 3.0 && even.x_edge(1) == 1.0, "mesh: an even grid's edges");
        }

        // ---- GL: cell edges where the transform puts them.
        const std::vector<float> three{0.0f, 0.5f, 1.0f};
        RenderSnapshot log_even = limits(1.0, 1000.0, 0.0, 1.0, log10s(), AxisScale{});
        log_even.heatmaps.push_back(row_of(three, {1.0, 1000.0}, {0.0, 1.0}));
        RenderSnapshot log_rec = limits(1.0, 1000.0, 0.0, 1.0, log10s(), AxisScale{});
        log_rec.heatmaps.push_back(with_edges(row_of(three, {0, 1}, {0, 1}), {1, 10, 100, 1000}, {0, 1}));
        RenderSnapshot lin_rec = limits(0.0, 1.0, 0.0, 1.0, AxisScale{}, AxisScale{});
        lin_rec.heatmaps.push_back(with_edges(make_heatmap(2, 1, [](int r, int) { return r == 0 ? 0.0f : 1.0f; }, {}),
                                              {0, 1}, {0, 0.2, 1}));
        RenderSnapshot clip = limits(0.5, 10.0, 0.0, 1.0, log10s(), AxisScale{});
        {
            std::vector<float> v(11, 1.0f);
            v[0] = 0.0f;
            v[1] = 0.5f;
            clip.heatmaps.push_back(row_of(v, {-1.0, 10.0}, {0.0, 1.0}));
        }
        RenderSnapshot sym = limits(-100.0, 100.0, -100.0, 100.0, symlog(), symlog());
        sym.heatmaps.push_back(make_heatmap(2, 2, [](int r, int c) { return 0.25f + 0.5f * static_cast<float>(r * 2 + c) / 3.0f; },
                                            {}, Range{-100.0, 100.0}, Range{-100.0, 100.0}));

        std::vector<Probe> probes;
        {
            GLContext ctx({.width = W, .height = H, .title = "layout_test", .visible = false});
            RenderDevice dev;
            DataRenderer data;
            unsigned long long gen = 0;
            for (const RenderSnapshot* s : {&log_even, &log_rec, &lin_rec, &clip, &sym}) {
                FigureSnapshot fs = one_axes(*s);
                fs.generation = fs.data_generation = ++gen;
                probes.push_back({render_figure_rgba(dev, data, fs, W, H),
                                  compute_figure_layout(fs, W, H).cells[0].tr});
            }
            FigureSnapshot fs = one_axes(log_rec);
            fs.generation = fs.data_generation = ++gen;
            export_figure_png(dev, data, fs, "heatmap_scale_rec.png", W, H, 1);
            export_figure_svg(fs, "heatmap_scale_rec.svg", W, H);
        }

        {
            const Probe& p = probes[0];
            const HeatmapPixels px(log_even.heatmaps[0]);
            std::uint8_t c[3][4];
            for (int k = 0; k < 3; ++k) px.rgba(static_cast<std::size_t>(k), c[k]);
            const float y = p.tr.to_py(0.5);
            const float e1 = p.tr.to_px(334.0), e2 = p.tr.to_px(667.0);
            check(is(p.at(e1 - 2.0f, y), c[0]) && is(p.at(e1 + 2.0f, y), c[1])
                      && is(p.at(e2 - 2.0f, y), c[1]) && is(p.at(e2 + 2.0f, y), c[2]),
                  "GL: an even heatmap on a log axis has its edges where the scale puts them");
            check(is(p.at(p.tr.to_px(10.0), y), c[0]) && e2 - e1 < 0.15f * p.tr.pw,
                  "GL: so its first cell fills most of the frame");
        }
        {
            const Probe& p = probes[1];
            const HeatmapPixels px(log_rec.heatmaps[0]);
            std::uint8_t c[3][4];
            for (int k = 0; k < 3; ++k) px.rgba(static_cast<std::size_t>(k), c[k]);
            const float y = p.tr.to_py(0.5);
            bool ok = true;
            for (int k = 0; k < 3; ++k) {
                const double lo = std::pow(10.0, k), hi = std::pow(10.0, k + 1);
                ok = ok && is(p.at(p.tr.to_px(lo) + 2.0f, y), c[k]) && is(p.at(p.tr.to_px(hi) - 2.0f, y), c[k]);
            }
            check(ok, "GL: heatmap_rec with log-spaced edges: each cell between its edges");
            check(std::abs((p.tr.to_px(10.0) - p.tr.to_px(1.0)) - (p.tr.to_px(1000.0) - p.tr.to_px(100.0))) < 0.5f,
                  "GL: and the cells are even on the log axis");
        }
        {
            const Probe& p = probes[2];
            const HeatmapPixels px(lin_rec.heatmaps[0]);
            std::uint8_t lo[4], hi[4];
            px.rgba(0, lo);
            px.rgba(1, hi);
            const float x = p.tr.to_px(0.5), e = p.tr.to_py(0.2);
            check(is(p.at(x, e + 2.0f), lo) && is(p.at(x, e - 2.0f), hi),
                  "GL: heatmap_rec on linear axes: row 0 from the first y edge to the second");
        }
        {
            const Probe& p = probes[3];
            const HeatmapPixels px(clip.heatmaps[0]);
            std::uint8_t c0[4], c1[4];
            px.rgba(0, c0);
            px.rgba(1, c1);
            const float y = p.tr.to_py(0.5);
            bool none = true;
            for (int x = static_cast<int>(p.tr.px); x < static_cast<int>(p.tr.px + p.tr.pw); ++x)
                none = none && !is(p.at(static_cast<float>(x), y), c0);
            check(none, "GL: on a log axis a cell at or below 0 is not drawn");
            check(is(p.at(p.tr.px + 2.0f, y), c1) && is(p.at(p.tr.to_px(1.0) - 2.0f, y), c1),
                  "GL: and a cell from 0 draws its part above 0, from the frame's edge");
        }
        {
            const Probe& p = probes[4];
            const HeatmapPlot& hp = sym.heatmaps[0];
            const HeatmapPixels px(hp);
            bool ok = true;
            for (int r = 0; r < 2; ++r)
                for (int col = 0; col < 2; ++col) {
                    std::uint8_t c[4];
                    px.rgba(static_cast<std::size_t>(r * 2 + col), c);
                    const double x = col == 0 ? -0.5 : 0.5, y = r == 0 ? -0.5 : 0.5;
                    ok = ok && is(p.at(p.tr.to_px(x), p.tr.to_py(y)), c)
                         && is(p.at(p.tr.to_px(x * 150.0), p.tr.to_py(y * 150.0)), c);
                }
            check(ok, "GL: symlog on both axes: each cell meets 0 where the scale puts it");
        }

        // ---- SVG.
        {
            const CoordTransform tr = probes[0].tr;
            const std::string svg = render_figure_svg(one_axes(log_even), W, H);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "<rect x=\"%g\"", std::min(tr.to_px(334.0), tr.to_px(667.0)));
            check(count(svg, "shape-rendering=\"crispEdges\"") == 1 && svg.find(buf) != std::string::npos
                      && count(svg, "<image") == 0,
                  "SVG: a small heatmap on a log axis is one rect per cell, at the transform's edges");

            RenderSnapshot big = limits(1.0, 1000.0, 0.0, 1.0, log10s(), AxisScale{});
            big.heatmaps.push_back(make_heatmap(50, 100, [](int r, int c) { return static_cast<float>((r + c) % 7) / 6.0f; },
                                                {}, Range{1.0, 1000.0}, Range{0.0, 1.0}));
            const std::string bs = render_figure_svg(one_axes(big), W, H);
            std::snprintf(buf, sizeof(buf), "width=\"%d\"", static_cast<int>(std::ceil(tr.px + tr.pw)) - static_cast<int>(std::floor(tr.px)));
            check(count(bs, "<image") == 1 && count(bs, "crispEdges") == 0 && bs.find(buf) != std::string::npos,
                  "SVG: over the cell cap, one <image> at the plot area's pixel size");

            RenderSnapshot lin = limits(0.0, 3.0, 0.0, 1.0, AxisScale{}, AxisScale{});
            lin.heatmaps.push_back(row_of(three, {0.0, 3.0}, {0.0, 1.0}));
            const std::string ls = render_figure_svg(one_axes(lin), W, H);
            check(count(ls, "<image") == 1 && count(ls, "crispEdges") == 0,
                  "SVG: an even heatmap on linear axes is still one affine <image>");

            RenderSnapshot nan = log_rec;
            nan.heatmaps[0].data.mut()[1] = std::numeric_limits<float>::quiet_NaN();
            const std::string ns = render_figure_svg(one_axes(nan), W, H);
            const std::size_t g = ns.find("crispEdges");
            check(g != std::string::npos && count(ns.substr(g, ns.find("</g>", g) - g), "<rect") == 2,
                  "SVG: a NaN cell has no rect");
        }

        // ---- Limits, through the public API.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xscale(Scale::Log);
            ax->heatmap(std::vector<double>(10, 0.5), 1, 10, {0.0, 10.0}, {0.0, 1.0});
            const Range x = ax->xlim();
            check(x.lo == 1.0 && x.hi == 10.0, "limits: on a log axis, the cells it can place, unpadded");

            auto ax2 = Figure::create()->add_subplot(1, 1, 1);
            const std::vector<double> xe{1, 10, 100}, ye{-1, 0, 5};
            ax2->heatmap_rec(std::vector<double>{0, 1, 2, 3}, 2, 2, xe, ye);
            check(ax2->xlim().lo == 1.0 && ax2->xlim().hi == 100.0 && ax2->ylim().lo == -1.0 && ax2->ylim().hi == 5.0,
                  "limits: a heatmap_rec's extent, first edge to last");
        }

        // ---- Picks.
        {
            RenderSnapshot s = log_rec;
            const CoordTransform tr{1.0, 1000.0, 0.0, 1.0, 0.0f, 0.0f, 300.0f, 100.0f, 300.0f, 100.0f, log10s(), AxisScale{}};
            const auto mid = find_pick(s, tr, tr.to_px(50.0), tr.to_py(0.5));
            const auto end = find_pick(s, tr, tr.to_px(900.0), tr.to_py(0.5));
            check(mid && mid->kind == PickKind::Heatmap && mid->col == 1 && mid->row == 0
                      && end && end->col == 2,
                  "pick: a heatmap_rec's cell by its edges, on a log axis");
            check(format_pick(s, *mid).rfind("row=0, col=1", 0) == 0, "pick: hover names row and col");
        }

        // ---- API: read-back, set data, refusals.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const std::vector<double> v{0, 1, 2, 3, 4, 5};
            const std::vector<double> xe{0, 1, 3, 7}, ye{10, 5, 0};
            ax->heatmap_rec(v, 2, 3, xe, ye, {.cmap = Colormap::Magma, .vmax = 5.0f});
            ax->heatmap(v, 2, 3, {0, 3}, {0, 2});
            check(ax->heatmap_count() == 2, "api: a heatmap_rec counts as a heatmap");
            const HeatmapData d = ax->heatmap_data(0);
            check(d.x_edges == xe && d.y_edges == ye && d.xrange.lo == 0.0 && d.xrange.hi == 7.0
                      && d.yrange.lo == 10.0 && d.yrange.hi == 0.0 && d.rows == 2 && d.cols == 3 && d.data == v,
                  "api: read-back has the edges, and the ranges are the first and last");
            check(ax->heatmap_data(1).x_edges.empty() && ax->heatmap_data(1).y_edges.empty(),
                  "api: an even grid reads back no edges");

            check(!throws([&] { ax->set_heatmap_data(0, d); }), "set data: a read-back round-trips");
            HeatmapData wider = d;
            wider.cols = 4;
            wider.data.assign(8, 1.0);
            wider.x_edges = {0, 1, 2, 3, 4};
            wider.xrange = {99, 100};
            ax->set_heatmap_data(0, wider);
            const HeatmapData w2 = ax->heatmap_data(0);
            check(w2.cols == 4 && w2.x_edges.size() == 5 && w2.xrange.hi == 4.0,
                  "set data: new edges reshape it; xrange is not read");
            check(throws([&] { ax->set_heatmap_data(0, v, 2, 3, {0, 3}, {0, 2}); }),
                  "set data: the span form is refused on a heatmap_rec");
            HeatmapData no_edges = w2;
            no_edges.x_edges.clear();
            no_edges.y_edges.clear();
            check(throws([&] { ax->set_heatmap_data(0, no_edges); }), "set data: and edges are required");
            HeatmapData even = ax->heatmap_data(1);
            even.x_edges = {0, 1, 2, 3};
            even.y_edges = {0, 1, 2};
            check(throws([&] { ax->set_heatmap_data(1, even); }), "set data: an even grid takes no edges");
            HeatmapData bad = w2;
            bad.x_edges = {0, 1, 1, 3, 4};
            check(throws([&] { ax->set_heatmap_data(0, bad); }) && ax->heatmap_data(0).x_edges == w2.x_edges,
                  "set data: bad edges throw and change nothing");

            const std::vector<double> nan_edges{0, std::nan(""), 2, 3};
            check(throws([&] { ax->heatmap_rec(v, 2, 3, std::vector<double>{0, 1, 2}, ye); }),
                  "heatmap_rec: a wrong x edge count throws");
            check(throws([&] { ax->heatmap_rec(v, 2, 3, xe, std::vector<double>{0, 1}); }),
                  "heatmap_rec: a wrong y edge count throws");
            check(throws([&] { ax->heatmap_rec(v, 2, 3, std::vector<double>{0, 2, 1, 3}, ye); }),
                  "heatmap_rec: edges out of order throw");
            check(throws([&] { ax->heatmap_rec(v, 2, 3, nan_edges, ye); }), "heatmap_rec: a NaN edge throws");
            check(throws([&] { ax->heatmap_rec(std::vector<double>(5, 0.0), 2, 3, xe, ye); }),
                  "heatmap_rec: too little data throws");
            check(throws([&] { ax->heatmap_rec(v, 0, 3, xe, ye); }), "heatmap_rec: rows must be positive");
            check(throws([&] { ax->heatmap_rec(v, 2, 3, xe, ye, {.zorder = std::nan("")}); }),
                  "heatmap_rec: a non-finite zorder throws");
            check(ax->heatmap_count() == 2, "heatmap_rec: and a refused call adds nothing");

            ax->heatmap_rec(v, 2, 3, xe, ye, {.colorbar = true});
            const ObjectId id = ax->last_object();
            check(ax->remove(id) && ax->heatmap_count() == 2, "heatmap_rec: removed by its id");
        }

        // ---- What a mesh does not take: contours, origin, line edits.
        {
            HeatmapPlot hp = with_edges(make_heatmap(2, 2, [](int r, int c) { return static_cast<float>(r + c); }, {}),
                                        {0, 1, 3}, {0, 1, 2});
            hp.opts.contours = {1.0};
            check(trace_contours(hp).empty(), "mesh: traces no contours");
            RenderSnapshot t;
            t.heatmaps.push_back(hp);
            MatrixLineEdit e{MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Col, 0, 1};
            check(!apply_plot_data_op(t, e) && t.heatmaps[0].cols == 2, "mesh: takes no row/column insert");
            PlotCellEdit ce;
            ce.kind = PlotKind::Heatmap;
            ce.plot_index = 0;
            ce.column = 0;
            ce.element = 3;
            ce.value = 9.0;
            check(apply_plot_data_op(t, ce) && t.heatmaps[0].data[3] == 9.0f, "mesh: but its cells edit");

            RenderSnapshot s;
            s.heatmaps.push_back(hp);
            s.heatmaps[0].opts.colorbar = true;
            check(find_colorbar_requests(s).size() == 1, "mesh: has a colorbar when asked");
        }

        // ---- Plane2D.
        {
            auto fig = Figure::create({.width = 400, .height = 400});
            auto ax3 = fig->add_subplot3d(1, 1, 1);
            auto pl = ax3->plane(PlaneOrientation::XY, 0.5);
            pl->heatmap_rec(std::vector<double>{0, 1, 2, 3}, 2, 2, std::vector<double>{0, 0.2, 1},
                            std::vector<double>{0, 0.7, 1});
            check(pl->heatmap_count() == 1 && pl->heatmap_data(0).x_edges.size() == 3,
                  "plane: heatmap_rec is a heatmap there too");

            FigureSnapshot fs = make_snapshot3d(1, 1, 1);
            RenderSnapshot3D* s = fs.axes[0].snap3d();
            PlaneSnapshot p = make_plane(PlaneOrientation::XY, 0.5, {0.0f, 0.5f, 0.5f, 1.0f}, 2, 2, {0, 1}, {0, 1});
            p.sheet.heatmaps[0] = with_edges(p.sheet.heatmaps[0], {0, 0.2, 1}, {0, 0.7, 1});
            s->planes.push_back(p);
            const FigureLayout lay = compute_figure_layout(fs, 400, 400);
            const std::vector<PlanePlanItem> plan = plan_planes3d(lay.cells[0].box3d->proj, s->planes);
            check(plan.size() == 1 && plan[0].form == PlanePlanItem::Form::Polys && plan[0].polys.size() == 4,
                  "plane SVG: a mesh is cell polygons, even under orthographic");
        }
    }
} // namespace lt
