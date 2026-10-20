// Hiding plot objects (step 40): a hidden object is as if absent from the
// picture (raster and SVG, 2D, 3D and on planes), from the auto limits (shared
// axes too), the legend and the colorbars, and from hover and pick, while its
// data and index stay; the setters and getters; stack() layers; a Data-panel
// style edit against a later setter (visible_stamp); and a hidden plane, which
// keeps its limits as before. Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "figure_impl.h"
#include <stdexcept>

namespace lt {
    using namespace sextant;

    namespace {
        using V = std::vector<double>;

        constexpr Color kRed{1.0f, 0.0f, 0.0f, 1.0f};
        constexpr Color kGreen{0.0f, 0.6f, 0.0f, 1.0f};

        bool same(const RgbaImage& a, const RgbaImage& b) {
            return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
        }

        bool same(Range a, Range b) { return a.lo == b.lo && a.hi == b.hi; }

        template <class F>
        bool throws_range(F&& f) {
            try {
                f();
            } catch (const std::out_of_range&) {
                return true;
            }
            return false;
        }

        // A red line with a name and the legend on; with `hidden`, one hidden
        // object of every other 2D kind, all far outside the line's range, named,
        // keyed and (heatmap, scatter_z, quiver) asking for a colorbar.
        std::shared_ptr<Figure> figure_2d(bool hidden, std::shared_ptr<Axes>& ax) {
            auto fig = Figure::create({.width = 420, .height = 320});
            ax = fig->add_subplot(1, 1, 1);
            ax->line(V{0.0, 1.0, 2.0}, V{0.0, 2.0, 1.0}, {.color = kRed, .name = "line"});
            if (hidden) {
                const V e{0.5, 0.5};
                ax->scatter(V{50.0, 60.0}, V{50.0, 60.0}, ErrorBar{.y_cap_hi = e},
                            {.color = kGreen, .name = "points", .visible = false});
                ax->scatter_z(V{-40.0}, V{-40.0}, V{0.5},
                              {.colorbar = true, .name = "z", .visible = false});
                ax->bar(V{30.0, 31.0}, V{80.0, 90.0}, {.color = kGreen, .name = "bars", .visible = false});
                ax->heatmap(V{0.0, 1.0, 2.0, 3.0}, 2, 2, {-90.0, -70.0}, {-90.0, -70.0},
                            {.colorbar = true, .name = "heat", .visible = false,
                             .contours = V{1.5}, .contour_labels = true});
                ax->fill(V{-20.0, -10.0}, V{5.0, 6.0}, 0.0,
                         {.color = kGreen, .linewidth = 2.0f, .name = "fill", .visible = false});
                ax->quiver(V{70.0}, V{-70.0}, V{1.0}, V{1.0}, V{0.5},
                           {.colorbar = true, .name = "arrows", .visible = false});
                ax->axline(150.0, {.color = kGreen, .name = "axline", .visible = false});
                ax->axspan(-150.0, -120.0, {.orientation = Orientation::Vertical, .name = "span",
                                            .visible = false});
                ax->text("hidden", 1.0, 1.0, {.fontsize = 30.0f, .visible = false});
                ax->annotate(1.0, 1.0, "hidden too", Pos::fraction(0.2), Pos::fraction(0.8),
                             {.fontsize = 20.0f, .visible = false});
            }
            ax->legend();
            return fig;
        }

        // The same in 3D: a cloud, and with `hidden` a hidden grid of bars, a
        // surface, a mesh, a path and a text, and a plane whose only heatmap is
        // hidden (so the plane counts as empty: its offset is no limit either).
        std::shared_ptr<Figure> figure_3d(bool hidden, std::shared_ptr<Axes3D>& ax) {
            auto fig = Figure::create({.width = 420, .height = 360});
            ax = fig->add_subplot3d(1, 1, 1);
            ax->scatter3d(V{0.0, 1.0, 2.0}, V{0.0, 2.0, 1.0}, V{1.0, 0.0, 2.0},
                          {.color = kRed, .name = "cloud"});
            if (hidden) {
                ax->bar3d(PlaneOrientation::XY, V{20.0, 21.0}, V{20.0}, V{30.0, 40.0},
                          {.color = kGreen, .name = "bars", .visible = false});
                ax->surface(PlaneOrientation::XY, V{-30.0, -29.0}, V{-30.0, -29.0},
                            V{1.0, 2.0, 3.0, 4.0},
                            {.colormap = true, .colorbar = true, .name = "surf", .visible = false});
                ax->surface_tri(V{0.0, 9.0, 0.0}, V{0.0, 0.0, 9.0}, V{-9.0, -9.0, -9.0}, PlaneOrientation::XY,
                                {.name = "mesh", .visible = false});
                ax->line3d(V{0.0, 50.0}, V{0.0, 50.0}, V{0.0, 50.0},
                           {.color = kGreen, .linewidth = 6.0f, .name = "path", .visible = false});
                ax->text("hidden", 1.0, 1.0, 1.0, {.fontsize = 30.0f, .visible = false});
                ax->plane(PlaneOrientation::XY, 80.0)
                    ->heatmap(V{0.0, 1.0, 2.0, 3.0}, 2, 2, {-60.0, -40.0}, {-60.0, -40.0},
                              {.colorbar = true, .name = "slice", .visible = false});
            }
            ax->legend();
            return fig;
        }
    } // namespace

    void test_visible() {
        std::printf("\n[visible: a hidden object is as if absent]\n");

        // ---- 2D: the picture, the limits, the legend and colorbars.
        {
            std::shared_ptr<Axes> a, b;
            auto with = figure_2d(true, a), without = figure_2d(false, b);
            check(same(with->render_rgba(), without->render_rgba()),
                  "2D: every kind hidden, the raster is the figure without them (limits, legend, colorbars)");
            check(with->render_svg().svg == without->render_svg().svg,
                  "2D: and so is the SVG, byte for byte");
            check(same(a->xlim(), b->xlim()) && same(a->ylim(), b->ylim()),
                  "2D: hidden objects leave the auto limits");
        }

        // ---- The setters, the getters, and the limits following a toggle.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(V{0.0, 1.0}, V{0.0, 1.0}).scatter(V{100.0}, V{100.0});
            const Range shown = ax->xlim();
            check(ax->line_visible(0) && ax->scatter_visible(0), "getters: objects are visible by default");
            ax->set_scatter_visible(0, false);
            check(!ax->scatter_visible(0) && ax->xlim().hi < 2.0,
                  "set_scatter_visible(false): the auto x limit shrinks to the line");
            check(ax->scatter_count() == 1 && ax->scatter_data(0).x[0] == 100.0,
                  "hidden: the object and its data stay");
            ax->set_scatter_visible(0, true);
            check(same(ax->xlim(), shown), "set_scatter_visible(true): the limit is back");
            ax->set_xlim(-5.0, 5.0);
            ax->set_scatter_visible(0, false);
            check(same(ax->xlim(), Range{-5.0, 5.0}), "explicit limits: untouched by hiding");
            check(throws_range([&] { ax->set_line_visible(1, false); })
                      && throws_range([&] { (void)ax->bar_visible(0); })
                      && throws_range([&] { ax->set_text_visible(0, true); }),
                  "setters and getters: std::out_of_range past the count");

            // Every kind's pair reaches its own vector.
            ax->bar(V{1.0}, V{1.0}).heatmap(V{1.0}, 1, 1, {0.0, 1.0}, {0.0, 1.0})
                .scatter_z(V{0.0}, V{0.0}, V{0.0}).fill(V{0.0, 1.0}, V{1.0, 1.0})
                .quiver(V{0.0}, V{0.0}, V{1.0}, V{1.0}).axline(0.5).axspan(0.1, 0.2)
                .text("t", 0.5, 0.5);
            ax->set_bar_visible(0, false).set_heatmap_visible(0, false).set_scatter_z_visible(0, false)
                .set_fill_visible(0, false).set_quiver_visible(0, false).set_axline_visible(0, false)
                .set_axspan_visible(0, false).set_text_visible(0, false).set_line_visible(0, false);
            check(!ax->bar_visible(0) && !ax->heatmap_visible(0) && !ax->scatter_z_visible(0)
                      && !ax->fill_visible(0) && !ax->quiver_visible(0) && !ax->axline_visible(0)
                      && !ax->axspan_visible(0) && !ax->text_visible(0) && !ax->line_visible(0),
                  "setters: every 2D kind's flag reads back");
        }

        // ---- stack(): every layer takes StackOptions::visible.
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            const std::vector<std::vector<double>> ys{{1.0, 2.0}, {2.0, 1.0}};
            ax->stack(V{0.0, 1.0}, ys, {.visible = false});
            check(ax->fill_count() == 2 && !ax->fill_visible(0) && !ax->fill_visible(1),
                  "stack: a hidden stack's layers are hidden fills");
            ax->set_fill_visible(1, true);
            check(ax->fill_visible(1) && !ax->fill_visible(0), "stack: and each shows on its own");
        }

        // ---- A share group counts its visible members' data only.
        {
            auto fig = Figure::create();
            auto a = fig->add_subplot(1, 2, 1);
            auto b = fig->add_subplot(1, 2, 2);
            b->sharex(*a);
            a->line(V{0.0, 1.0}, V{0.0, 1.0});
            b->line(V{0.0, 100.0}, V{0.0, 1.0}, {.visible = false});
            check(a->xlim().hi < 2.0 && b->xlim().hi < 2.0,
                  "shared x: a hidden member object widens neither axes");
            b->set_line_visible(0, true);
            check(a->xlim().hi > 90.0, "shared x: shown, it widens the group");
        }

        // ---- Hover and pick skip a hidden object: what is under it answers.
        {
            const CoordTransform tr{0.0, 10.0, 0.0, 10.0, 0.0f, 0.0f, 400.0f, 400.0f, 400.0f, 400.0f};
            RenderSnapshot s;
            s.xmin = s.ymin = 0.0;
            s.xmax = s.ymax = 10.0;
            s.xlim_auto = s.ylim_auto = false;
            BarPlot bp;
            bp.centers = V{5.0};
            bp.heights = V{8.0};
            bp.bar_width = 2.0;
            bp.id = 1;
            s.bars.push_back(bp);
            ScatterPlot sp;
            sp.x = V{5.0};
            sp.y = V{4.0};
            sp.id = 2;
            s.scatters.push_back(sp);
            const float px = tr.to_px(5.0), py = tr.to_py(4.0);
            auto h = find_pick(s, tr, px, py);
            check(h && h->kind == PickKind::Scatter, "pick: a shown point beats the bar under it");
            s.scatters[0].opts.visible = false;
            h = find_pick(s, tr, px, py);
            check(h && h->kind == PickKind::Bar, "pick: hidden, the bar under it answers");
            s.bars[0].opts.visible = false;
            check(!find_pick(s, tr, px, py) && !find_hint(s, tr, px, py),
                  "pick and hint: both hidden, nothing");
        }
        {
            Transform3D tf;
            tf.xmin = tf.ymin = tf.zmin = 0.0;
            tf.xmax = tf.ymax = tf.zmax = 10.0;
            Camera3D cam;
            cam.projection = Projection::Orthographic;
            const Projector3D proj(tf, cam, PlotRect{20.0f, 15.0f, 400.0f, 320.0f}, 0.1f);
            RenderSnapshot3D rs;
            Scatter3DPlot c;
            c.x = V{5.0};
            c.y = V{5.0};
            c.z = V{5.0};
            rs.scatter3d.push_back(c);
            const Px3 q = proj.project(5.0, 5.0, 5.0);
            check(find_pick3d(rs, proj, q.x, q.y).has_value(), "pick 3D: a shown marker is picked");
            rs.scatter3d[0].opts.visible = false;
            check(!find_pick3d(rs, proj, q.x, q.y) && !find_hint3d(rs, proj, q.x, q.y),
                  "pick 3D: hidden, it is not, nor hinted");
        }

        // ---- 3D: the picture and the limits, planes included.
        {
            std::shared_ptr<Axes3D> a, b;
            auto with = figure_3d(true, a), without = figure_3d(false, b);
            check(same(with->render_rgba(), without->render_rgba()),
                  "3D: every kind hidden, the raster is the figure without them");
            check(with->render_svg().svg == without->render_svg().svg, "3D: and so is the SVG");
            check(same(a->xlim(), b->xlim()) && same(a->ylim(), b->ylim()) && same(a->zlim(), b->zlim()),
                  "3D: hidden objects (and a plane holding only hidden ones) leave the auto limits");
            a->set_bar3d_visible(0, true);
            check(a->xlim().hi > 15.0 && a->bar3d_visible(0), "3D: a bar grid shown widens them");
            a->set_bar3d_visible(0, false).set_surface_visible(0, true);
            check(a->xlim().lo < -20.0 && a->surface_visible(0) && !a->surface_tri_visible(0)
                      && !a->line3d_visible(0) && !a->text_visible(0) && a->scatter3d_visible(0),
                  "3D: and so does a surface; every 3D kind's flag reads back");
            check(throws_range([&] { a->set_scatter3d_visible(1, false); }),
                  "3D setters: std::out_of_range past the count");
        }

        // ---- A plane's objects against the plane's own flag.
        {
            auto box_of = [](bool plane_shown, bool heat_shown) {
                auto fig = Figure::create();
                auto ax = fig->add_subplot3d(1, 1, 1);
                ax->scatter3d(V{0.0, 1.0}, V{0.0, 1.0}, V{0.0, 1.0});
                auto pl = ax->plane(PlaneOrientation::XY, 30.0, {.visible = plane_shown});
                pl->heatmap(V{0.0, 1.0, 2.0, 3.0}, 2, 2, {0.0, 1.0}, {0.0, 1.0});
                pl->set_heatmap_visible(0, heat_shown);
                check(pl->heatmap_visible(0) == heat_shown, "Plane2D: the getter reads the setter");
                return ax->zlim();
            };
            const Range both = box_of(true, true), plane_hidden = box_of(false, true),
                        heat_hidden = box_of(true, false);
            check(both.hi > 25.0 && same(plane_hidden, both),
                  "plane: a hidden plane keeps its limits (Plane2DOptions::visible is ink only)");
            check(heat_hidden.hi < 5.0, "plane: a plane whose objects are hidden is empty, offset and all");
        }

        // ---- The Data panel against the setter (visible_stamp).
        {
            auto fig = Figure::create();
            auto ax = fig->add_subplot(1, 1, 1);
            ax->line(V{0.0, 1.0}, V{0.0, 1.0});
            auto& fi = detail::FigureAccess::impl(*fig);
            auto hide_in_panel = [&] {
                fi.host_frame();
                fi.edit_box.update(1, [](AxesEdit& e) {
                    e.plot_styles.push_back({0, -1, LineOptions{.linewidth = 7.0f, .visible = false}});
                });
                fi.host_frame();
            };

            hide_in_panel();
            fi.apply_edits_and_publish();
            check(!ax->line_visible(0), "panel: Visible unticked replays onto the live axes");

            ax->set_line_visible(0, true);
            hide_in_panel();
            ax->set_line_visible(0, true);   // after the panel drew
            fi.apply_edits_and_publish();
            check(ax->line_visible(0), "panel: a setter after the panel drew keeps its visible");
            const auto snap = fi.host_frame();
            check(snap->axes[0].snap2d()->lines[0].opts.linewidth == 7.0f
                      && snap->axes[0].snap2d()->lines[0].opts.visible,
                  "panel: and the rest of the panel's edit still lands");
        }
    }
} // namespace lt
