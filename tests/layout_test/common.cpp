// Builders shared by more than one subject file (see layout_test.h).
#include "layout_test.h"

namespace lt {
    int g_checks = 0;
    int g_failures = 0;

    void check(bool ok, const std::string& what) {
        ++g_checks;
        if (!ok) {
            ++g_failures;
            std::printf("  FAIL: %s\n", what.c_str());
        }
    }

    // A one-line figure built directly (not via the public API).
    sextant::FigureSnapshot make_snapshot(int rows, int cols, int n_cells,
                                          double x_scale, double y_scale) {
        sextant::FigureSnapshot fs;
        for (int i = 1; i <= n_cells; ++i) {
            sextant::RenderSnapshot rs;
            sextant::LinePlot lp;
            std::vector<double> xs, ys;
            for (int k = 0; k < 50; ++k) {
                xs.push_back(static_cast<double>(k) * x_scale);
                ys.push_back(std::sin(k * 0.1) * y_scale);
            }
            lp.x = sextant::CowVec<double>(std::move(xs));
            lp.y = sextant::CowVec<double>(std::move(ys));
            rs.lines.push_back(std::move(lp));
            fs.axes.push_back({{rows, cols, i}, std::move(rs)});
        }
        return fs;
    }

    // Reads the first axes-background rect (the frame) out of an SVG.
    bool read_first_svg_frame(const std::string& path, sextant::PlotRect& out) {
        std::ifstream f(path);
        if (!f) return false;
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const std::size_t at = text.find("\" fill=\"white\"/>");
        if (at == std::string::npos) return false;
        const std::size_t start = text.rfind("<rect x=\"", at);
        if (start == std::string::npos) return false;
        return std::sscanf(text.c_str() + start,
                           "<rect x=\"%f\" y=\"%f\" width=\"%f\" height=\"%f\"",
                           &out.x, &out.y, &out.w, &out.h) == 4;
    }

    // Index extent by default ([0,cols] x [0,rows]); `xr`/`yr` override it.
    sextant::HeatmapPlot make_heatmap(int rows, int cols,
                                      const std::function<float(int, int)>& f,
                                      sextant::HeatmapOptions opts,
                                      std::optional<sextant::Range> xr,
                                      std::optional<sextant::Range> yr) {
        std::vector<float> v(static_cast<std::size_t>(rows) * cols);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                v[static_cast<std::size_t>(r) * cols + c] = f(r, c);
        return sextant::HeatmapPlot{
            std::move(v), rows, cols,
            xr.value_or(sextant::Range{0.0, static_cast<double>(cols)}),
            yr.value_or(sextant::Range{0.0, static_cast<double>(rows)}),
            std::move(opts)
        };
    }

    sextant::FigureSnapshot one_line_snapshot(const std::vector<double>& x,
                                              const std::vector<double>& y) {
        sextant::LinePlot lp;
        lp.x = x;
        lp.y = y;

        sextant::FigureSnapshot fs;
        sextant::FigureAxesSnapshot fa;
        fa.slot = sextant::AxesSlot{1, 1, 1};
        fa.snap2d()->lines.push_back(std::move(lp));
        fs.axes.push_back(std::move(fa));
        fs.generation = fs.data_generation = 1;
        return fs;
    }

    sextant::FigureSnapshot make_snapshot3d(int rows, int cols, int index,
                                            sextant::Camera3D cam) {
        sextant::FigureSnapshot fs;
        sextant::RenderSnapshot3D rs;
        rs.camera = cam;
        fs.axes.push_back({{rows, cols, index}, std::move(rs)});
        fs.generation = fs.data_generation = 1;
        return fs;
    }

    bool near_px(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

    // A plane carrying one heatmap, built directly.
    sextant::PlaneSnapshot make_plane(sextant::PlaneOrientation o, double offset,
                                      std::vector<float> data, int rows, int cols,
                                      sextant::Range xr, sextant::Range yr,
                                      sextant::HeatmapOptions ho) {
        sextant::PlaneSnapshot p;
        p.orient = o;
        p.offset = offset;
        sextant::HeatmapPlot hp;
        hp.data = std::move(data);
        hp.rows = rows;
        hp.cols = cols;
        hp.xrange = xr;
        hp.yrange = yr;
        hp.opts = std::move(ho);
        p.sheet.heatmaps.push_back(std::move(hp));
        return p;
    }

    // Two planes, each with one line and one heatmap.
    sextant::RenderSnapshot3D two_plane_snapshot() {
        using namespace sextant;
        RenderSnapshot3D s;
        for (int p = 0; p < 2; ++p) {
            PlaneSnapshot pl;
            pl.orient = (p == 0) ? PlaneOrientation::XY : PlaneOrientation::YZ;
            pl.offset = 0.25 * (p + 1);

            LinePlot lp;
            lp.x = std::vector<double>{0.0, 1.0, 2.0};
            lp.y = std::vector<double>{10.0 * p, 10.0 * p + 1, 10.0 * p + 2};
            pl.sheet.lines.push_back(std::move(lp));

            BarPlot bp;
            bp.centers = std::vector<double>{0.0, 1.0};
            bp.heights = std::vector<double>{1.0, 2.0};
            bp.bar_width = 0.5;
            pl.sheet.bars.push_back(std::move(bp));

            HeatmapPlot hp;
            hp.rows = 2;
            hp.cols = 2;
            hp.xrange = {0.0, 1.0};
            hp.yrange = {0.0, 1.0};
            hp.data = std::vector<float>{0.0f, 1.0f, 2.0f, 3.0f};
            pl.sheet.heatmaps.push_back(std::move(hp));

            s.planes.push_back(std::move(pl));
        }
        return s;
    }

    // A grid of bars with distinct heights, u and v.
    sextant::Bar3DPlot bar3d_grid() {
        using namespace sextant;
        Bar3DPlot b;
        b.orient = PlaneOrientation::XY;
        b.u = std::vector<double>{2.0, 5.0, 8.0};
        b.v = std::vector<double>{2.0, 5.0, 8.0};
        b.u_width = 2.0;
        b.v_width = 2.0;
        // Row-major, u major; all values distinct so a wrong bar is detectable.
        b.heights = std::vector<double>{
            1.0, 2.0, 3.0,
            4.0, 5.0, 6.0,
            7.0, 8.0, 9.0
        };
        return b;
    }

    sextant::SurfacePlot ripple_surface() {
        using namespace sextant;
        SurfacePlot s;
        s.u = std::vector<double>{0.0, 2.0, 4.0};
        s.v = std::vector<double>{1.0, 3.0, 5.0};
        s.heights = std::vector<double>{1, 2, 3, 4, 5, 6, 7, 8, 9};
        for (std::size_t k = 0; k < 9; ++k) s.opts.hint_labels.push_back("S" + std::to_string(k));
        return s;
    }
} // namespace lt
