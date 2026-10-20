// Per-point marker sizes: ScatterOptions / ScatterZOptions /
// Scatter3DOptions::sizes. The call's length check, read-back and set_*_data,
// GPU pixels and SVG radii against a uniform size, hidden points (NaN, 0,
// negative), hover, the Data panel's size column and its row and cell edits,
// planes and 3D. Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "hint.h"
#include "plot_data_view.h"
#include "renderer/scatter3d.h"
#include <cmath>
#include <limits>

namespace lt {
    using namespace sextant;

    namespace {
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

        std::size_t count_near(const RgbaImage& img, int r, int g, int b, int tol) {
            std::size_t n = 0;
            for (std::size_t i = 0; i + 3 < img.pixels.size(); i += 4)
                if (std::abs(img.pixels[i] - r) <= tol && std::abs(img.pixels[i + 1] - g) <= tol &&
                    std::abs(img.pixels[i + 2] - b) <= tol)
                    ++n;
            return n;
        }

        std::size_t count_of(const std::string& hay, const std::string& needle) {
            std::size_t n = 0;
            for (std::size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1))
                ++n;
            return n;
        }

        // Points along y = 5, one per size, in a 600x300 figure.
        std::shared_ptr<Figure> row_of(const std::vector<double>& x, ScatterOptions o) {
            FigureOptions fo;
            fo.width = 600;
            fo.height = 300;
            fo.supersample = 1;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 10).set_ylim(0, 10);
            ax->scatter(x, std::vector<double>(x.size(), 5.0), std::move(o));
            return fig;
        }

        ScatterOptions blue() {
            ScatterOptions o;
            o.color = Color{0.0f, 0.0f, 1.0f, 1.0f};
            o.alpha = 1.0f;
            return o;
        }

        template <class F>
        bool throws_invalid(F&& f) {
            try {
                f();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        }
    } // namespace

    static void marker_sizes_checks();

    void test_marker_sizes() {
        std::printf("\n[markers: per-point sizes]\n");
        try {
            marker_sizes_checks();
        } catch (const std::exception& e) {
            check(false, std::string("per-point sizes: unexpected exception: ") + e.what());
        }
    }

    static void marker_sizes_checks() {
        const std::vector<double> x3{1.0, 2.0, 3.0}, y3{1.0, 2.0, 3.0}, z3{0.1, 0.5, 0.9};

        // --- The call -------------------------------------------------------
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ScatterOptions bad;
            bad.sizes = {10.0, 20.0};
            ScatterZOptions badz;
            badz.sizes = {10.0};
            check(throws_invalid([&] { ax->scatter(x3, y3, bad); }) &&
                      throws_invalid([&] { ax->scatter_z(x3, y3, z3, badz); }) && ax->scatter_count() == 0 &&
                      ax->scatter_z_count() == 0,
                  "scatter/scatter_z: sizes of another length throw, nothing is added");
            ScatterOptions ok;
            ok.sizes = {10.0, 20.0, 30.0};
            ax->scatter(x3, y3, ok);
            check(ax->scatter_data(0).sizes == ok.sizes, "scatter_data() reads the sizes back");
            ax->scatter(x3, y3);
            check(ax->scatter_data(1).sizes.empty(), "...and none for a uniform series");

            // set_*_data: the span overload keeps them while the count holds.
            ax->set_scatter_data(0, std::vector<double>{4.0, 5.0, 6.0}, y3);
            check(ax->scatter_data(0).sizes == ok.sizes, "set_scatter_data (spans), same count: sizes kept");
            ax->set_scatter_data(0, std::vector<double>{4.0, 5.0}, std::vector<double>{1.0, 2.0});
            check(ax->scatter_data(0).sizes.empty(), "set_scatter_data (spans), new count: sizes dropped");
            ScatterData d{x3, y3, {7.0, 8.0, 9.0}};
            ax->set_scatter_data(0, d);
            check(ax->scatter_data(0).sizes == d.sizes, "set_scatter_data (ScatterData): sizes as given");
            const ScatterData before = ax->scatter_data(0);
            d.sizes = {1.0};
            check(throws_invalid([&] { ax->set_scatter_data(0, d); }) &&
                      ax->scatter_data(0).sizes == before.sizes && ax->scatter_data(0).x == before.x,
                  "set_scatter_data: sizes of another length throw and change nothing");
            d.sizes.clear();
            ax->set_scatter_data(0, d);
            check(ax->scatter_data(0).sizes.empty(), "set_scatter_data: empty sizes clear them");

            ScatterZOptions zo;
            zo.sizes = {5.0, 6.0, 7.0};
            ax->scatter_z(x3, y3, z3, zo);
            ax->set_scatter_z_data(0, x3, y3, z3);
            check(ax->scatter_z_data(0).sizes == zo.sizes, "scatter_z: read back, kept by the span overload");
            ax->set_scatter_z_data(0, ScatterZData{x3, y3, z3, {}});
            check(ax->scatter_z_data(0).sizes.empty(), "scatter_z: cleared through ScatterZData");

            // On a plane and in 3D.
            auto fig3 = Figure::create();
            auto ax3 = fig3->add_subplot3d(1, 1, 1);
            auto pl = ax3->plane(PlaneOrientation::XY, 0.0);
            check(throws_invalid([&] { pl->scatter(x3, y3, bad); }), "Plane2D::scatter checks them too");
            pl->scatter(x3, y3, ok);
            check(pl->scatter_data(0).sizes == ok.sizes, "Plane2D: read back");
            Scatter3DOptions o3;
            o3.sizes = {1.0, 2.0};
            check(throws_invalid([&] { ax3->scatter3d(x3, y3, z3, o3); }) && ax3->scatter3d_count() == 0,
                  "scatter3d: sizes of another length throw");
            o3.sizes = {11.0, 12.0, 13.0};
            ax3->scatter3d(x3, y3, z3, o3);
            ax3->set_scatter3d_data(0, x3, y3, z3);
            check(ax3->scatter3d_data(0).sizes == o3.sizes, "scatter3d: read back, kept by the span overload");
            Scatter3DData d3 = ax3->scatter3d_data(0);
            d3.sizes = {1.0};
            check(throws_invalid([&] { ax3->set_scatter3d_data(0, d3); }) &&
                      ax3->scatter3d_data(0).sizes == o3.sizes,
                  "set_scatter3d_data: a bad length throws before anything changes");
        }

        // --- GPU and SVG ----------------------------------------------------
        {
            // A per-point size equal to the uniform one draws the same picture.
            ScatterOptions u = blue();
            u.size = 30.0f;
            ScatterOptions p = blue();
            p.sizes = {30.0, 30.0};
            const std::vector<double> x2{3.0, 7.0};
            const RgbaImage a = row_of(x2, u)->render_rgba(), b = row_of(x2, p)->render_rgba();
            check(a.pixels == b.pixels, "sizes all equal to `size` render byte-identically to it");
            check(row_of(x2, u)->render_svg().svg == row_of(x2, p)->render_svg().svg,
                  "...and write the same SVG");

            // Two sizes: the ink is both discs.
            p.sizes = {10.0, 40.0};
            const std::size_t ink = count_near(row_of(x2, p)->render_rgba(), 0, 0, 255, 8);
            const double want = 3.14159265 * (5.0 * 5.0 + 20.0 * 20.0);
            check(ink > want * 0.9 && ink < want * 1.1,
                  "sizes 10 and 40: ink of both discs (" + std::to_string(ink) + " ~ " +
                      std::to_string(static_cast<int>(want)) + ")");
            const std::string svg = row_of(x2, p)->render_svg().svg;
            check(svg.find("r=\"5\"") != std::string::npos && svg.find("r=\"20\"") != std::string::npos,
                  "SVG: each marker at its own radius");

            // NaN, 0 and a negative size hide their points.
            p.sizes = {kNaN, 0.0, -4.0, 30.0};
            const std::vector<double> x4{2.0, 4.0, 6.0, 8.0};
            const std::size_t one = count_near(row_of(x4, p)->render_rgba(), 0, 0, 255, 8);
            const double disc = 3.14159265 * 15.0 * 15.0;
            check(one > disc * 0.9 && one < disc * 1.1, "NaN, 0 and negative sizes draw nothing");
            check(count_of(row_of(x4, p)->render_svg().svg, "fill=\"rgb(0,0,255)\"") == 1,
                  "SVG: only the sized point is written");

            // scatter_z: per-point sizes with per-point colors.
            FigureOptions fo;
            fo.width = 600;
            fo.height = 300;
            fo.supersample = 1;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            ax->set_xlim(0, 10).set_ylim(0, 10);
            ScatterZOptions z;
            z.alpha = 1.0f;
            z.sizes = {10.0, 40.0};
            ax->scatter_z(x2, std::vector<double>{5.0, 5.0}, std::vector<double>{1.0, 1.0}, z);
            const std::size_t zi = count_near(fig->render_rgba(), 253, 231, 37, 10);  // viridis top
            check(zi > want * 0.9 && zi < want * 1.1, "scatter_z: each marker its own size");
        }

        // --- Hover ----------------------------------------------------------
        {
            RenderSnapshot s;
            ScatterPlot sp;
            sp.x = std::vector<double>{3.0, 7.0};
            sp.y = std::vector<double>{5.0, 5.0};
            sp.sizes = std::vector<double>{kNaN, 20.0};
            sp.id = 1;
            s.scatters.push_back(sp);
            const CoordTransform tr{0.0, 10.0, 0.0, 10.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
            const auto hidden = find_pick(s, tr, tr.to_px(3.0), tr.to_py(5.0));
            const auto shown = find_pick(s, tr, tr.to_px(7.0), tr.to_py(5.0));
            check(!hidden && shown && shown->element == 1, "hover: a hidden point is not hit, a sized one is");
        }

        // --- Data panel: the size column, row and cell edits ----------------
        {
            RenderSnapshot s;
            ScatterPlot sp;
            sp.x = std::vector<double>{1.0, 2.0, 3.0};
            sp.y = std::vector<double>{1.0, 2.0, 3.0};
            sp.sizes = std::vector<double>{10.0, 20.0, 30.0};
            s.scatters.push_back(sp);
            ScatterPlot flat = sp;
            flat.sizes = std::vector<double>{};
            s.scatters.push_back(flat);
            const auto tables = collect_plot_data_tables(s);
            check(tables.size() == 2 && tables[0].columns.size() == 3 &&
                      std::string(tables[0].columns[2].name) == "size" && tables[1].columns.size() == 2,
                  "panel: a size column only for a series with sizes");

            // Insert at the top: the new point takes the next one's size (not 0).
            RenderSnapshot t = s;
            std::vector<PlotDataOp> inv;
            apply_plot_data_ops(t, {PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Scatter, 0, 0, -1}}, &inv);
            check(t.scatters[0].sizes.size() == 4 && t.scatters[0].sizes[0] == 10.0,
                  "rows: a point inserted at row 0 copies row 0's size, never 0");
            apply_plot_data_ops(t, {PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Scatter, 0, 4, -1}});
            check(t.scatters[0].sizes.size() == 5 && t.scatters[0].sizes[4] == 30.0,
                  "rows: an appended point copies the last size");
            apply_plot_data_ops(t, {PlotRowEdit{PlotRowEdit::Op::Insert, PlotKind::Scatter, 1, 0, -1}});
            check(t.scatters[1].sizes.empty(), "rows: a series without sizes stays without");

            // Remove and undo: the size comes back with its point.
            RenderSnapshot r = s;
            std::vector<PlotDataOp> undo;
            apply_plot_data_ops(r, {PlotRowEdit{PlotRowEdit::Op::Remove, PlotKind::Scatter, 0, 1, -1}}, &undo);
            check(r.scatters[0].sizes.size() == 2 && r.scatters[0].sizes[1] == 30.0,
                  "rows: a remove takes the size out with its point");
            apply_plot_data_ops(r, undo);
            check(r.scatters[0].sizes.size() == 3 && r.scatters[0].sizes[1] == 20.0,
                  "rows: undoing it puts the size back");

            // A cell edit in the size column, and its undo.
            RenderSnapshot c = s;
            std::vector<PlotDataOp> cu;
            apply_plot_data_ops(c, {PlotCellEdit{PlotKind::Scatter, 0, 2, 1, 55.0}}, &cu);
            check(c.scatters[0].sizes[1] == 55.0 && c.scatters[0].x[1] == 2.0 && c.scatters[0].y[1] == 2.0,
                  "cells: column 2 is the size");
            apply_plot_data_ops(c, cu);
            check(c.scatters[0].sizes[1] == 20.0, "cells: undone");

            // scatter_z: z is column 2, the size column 3.
            RenderSnapshot zs;
            ScatterZPlot zp;
            zp.x = sp.x;
            zp.y = sp.y;
            zp.z = std::vector<double>{0.1, 0.2, 0.3};
            zp.sizes = std::vector<double>{10.0, 20.0, 30.0};
            zs.scatter_z.push_back(zp);
            apply_plot_data_ops(zs, {PlotCellEdit{PlotKind::ScatterZ, 0, 3, 0, 44.0}});
            check(zs.scatter_z[0].sizes[0] == 44.0 && zs.scatter_z[0].z[0] == 0.1, "cells: scatter_z's size column");
        }

        // --- Plane and 3D -----------------------------------------------------
        {
            PlaneSnapshot pl;
            ScatterPlot sp;
            sp.x = std::vector<double>{1.0, 2.0, 3.0};
            sp.y = std::vector<double>{1.0, 2.0, 3.0};
            sp.sizes = std::vector<double>{10.0, kNaN, 30.0};
            sp.id = 1;
            pl.sheet.scatters.push_back(sp);
            const PlaneGeometry g = plane_geometry(pl);
            check(g.markers.size() == 2 && g.markers[0].size_px == 10.0f && g.markers[1].size_px == 30.0f,
                  "plane: each marker its own size, the hidden one left out");

            Transform3D tf;
            tf.xmin = tf.ymin = tf.zmin = 0.0;
            tf.xmax = tf.ymax = tf.zmax = 10.0;
            Camera3D cam;
            const Projector3D proj(tf, cam, PlotRect{20.0f, 15.0f, 400.0f, 320.0f}, 0.1f);
            RenderSnapshot3D s3;
            Scatter3DPlot cloud;
            cloud.x = std::vector<double>{2.0, 5.0, 8.0};
            cloud.y = std::vector<double>{2.0, 6.0, 3.0};
            cloud.z = std::vector<double>{3.0, 5.0, 7.0};
            cloud.sizes = std::vector<double>{60.0, kNaN, 8.0};
            s3.scatter3d.push_back(cloud);
            const auto plan = plan_scatter3d(proj, s3.scatter3d);
            check(plan.size() == 2, "3D SVG plan: the hidden point is left out");
            bool radii = false;
            for (const auto& m: plan)
                if (m.index == 0) radii = m.radius == 30.0f;
            check(radii, "3D SVG plan: a marker's radius is its own size");
            const Px3 q0 = proj.project(2.0, 2.0, 3.0), q1 = proj.project(5.0, 6.0, 3.0 + 2.0);
            const auto far = find_hint3d(s3, proj, q0.x + 25.0f, q0.y);
            check(far.has_value(), "3D hover: a 60 px marker is hit 25 px from its centre (its own radius)");
            check(!find_hint3d(s3, proj, q1.x, q1.y).has_value() ||
                      find_hint3d(s3, proj, q1.x, q1.y)->text.find("y=6") == std::string::npos,
                  "3D hover: the hidden point is never named");

            // On the GPU a 3D cloud with sizes draws, the hidden point does not.
            auto fig = Figure::create();
            auto ax = fig->add_subplot3d(1, 1, 1);
            Scatter3DOptions o;
            o.color = Color{0.0f, 0.0f, 1.0f, 1.0f};
            o.sizes = {30.0, kNaN, 30.0};
            ax->scatter3d(cloud.x.get(), cloud.y.get(), cloud.z.get(), o);
            const std::size_t two = count_near(fig->render_rgba(), 0, 0, 255, 40);
            o.sizes.clear();
            o.size = 30.0f;
            auto fig2 = Figure::create();
            fig2->add_subplot3d(1, 1, 1)->scatter3d(cloud.x.get(), cloud.y.get(), cloud.z.get(), o);
            const std::size_t three = count_near(fig2->render_rgba(), 0, 0, 255, 40);
            check(two > 0 && two < three && two > three / 2,
                  "3D GPU: two of three markers (" + std::to_string(two) + " vs " + std::to_string(three) + ")");
        }

        // The look of it, for the eye: a bubble chart, PNG and SVG.
        {
            FigureOptions fo;
            fo.width = 640;
            fo.height = 400;
            auto fig = Figure::create(fo);
            auto ax = fig->add_subplot(1, 1, 1);
            std::vector<double> x, y, z;
            ScatterZOptions o;
            for (int i = 0; i < 30; ++i) {
                x.push_back(i % 6 + 0.5 * (i / 6 % 2));
                y.push_back(i / 6);
                z.push_back(i / 29.0);
                o.sizes.push_back(6.0 + 2.0 * i);
            }
            o.alpha = 0.7f;
            o.edge_linewidth = 1.0f;
            o.edgecolor = Color::Black;
            ax->scatter_z(x, y, z, o).set_xlim(-1, 7).set_ylim(-1, 5);
            fig->savefig("marker_sizes.png");
            fig->savefig("marker_sizes.svg");
        }
    }
} // namespace lt
