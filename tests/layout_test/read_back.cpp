// Read-back and set_*_data() on Axes, Plane2D and Axes3D, and the journals
// and stamps that let panel edits (titles, limits, camera, data) reach them.
// Part of sextant_layout_test; see layout_test.h.
#include "layout_test.h"
#include "read_back.h"
#include <cmath>
#include <stdexcept>

namespace lt {
    using namespace sextant;

    namespace {
        bool near(double a, double b) { return std::fabs(a - b) < 1e-12; }

        bool range_is(Range r, double lo, double hi) { return near(r.lo, lo) && near(r.hi, hi); }

        template <typename F>
        bool throws_out_of_range(F&& f) {
            try { f(); } catch (const std::out_of_range&) { return true; } catch (...) {}
            return false;
        }
    } // namespace

    void test_read_back_2d() {
        std::printf("\n[read-back: Axes and Plane2D]\n");

        auto fig = Figure::create();
        auto ax = fig->axes();
        check(ax->title().empty() && ax->line_count() == 0 && ax->heatmap_count() == 0,
              "2d: a fresh axes has no titles and no objects");

        ax->set_title("T").set_xtitle("x").set_ytitle("y");
        check(ax->title() == "T" && ax->xtitle() == "x" && ax->ytitle() == "y",
              "2d: titles read back as set");

        // Auto limits are the padded data range, as drawn.
        const std::vector<double> x{0.0, 10.0}, y{-1.0, 3.0};
        ax->line(x, y);
        check(range_is(ax->xlim(), -0.5, 10.5) && range_is(ax->ylim(), -1.2, 3.2),
              "2d: auto limits are the data range padded by 5%");
        ax->set_axes_style({.origin_x = 20.0});
        check(range_is(ax->xlim(), -1.0, 21.0),
              "2d: an origin pin widens the auto limits exactly as layout does");
        ax->set_axes_style({});
        ax->set_ylim(5.0, -5.0);
        check(range_is(ax->ylim(), 5.0, -5.0) && range_is(ax->xlim(), -0.5, 10.5),
              "2d: explicit limits come back as set, reversed included; the other axis stays auto");

        const std::vector<double> z{0.25, 0.75};
        ax->line(y);
        ax->scatter(x, y);
        ax->scatter_z(x, y, z);
        ax->bar(x, y);
        const std::vector<double> samples{0.0, 0.1, 0.9, 1.0, 1.0};
        ax->hist(samples, 2);
        const std::vector<double> cells{0.1, 0.2, 0.3, 0.4, 0.5, 0.6};
        ax->heatmap(cells, 2, 3, {10.0, 40.0}, {0.0, 2.0});

        check(ax->line_count() == 2 && ax->scatter_count() == 1 && ax->scatter_z_count() == 1
              && ax->bar_count() == 2 && ax->heatmap_count() == 1,
              "2d: counts per kind, hist() counted as a bar plot");
        const LineData l0 = ax->line_data(0), l1 = ax->line_data(1);
        check(l0.x == x && l0.y == y, "2d: line data reads back as passed");
        check(l1.x == std::vector<double>{0.0, 1.0} && l1.y == y,
              "2d: line(y) reads back with x = 0, 1, ...");
        const ScatterZData sz = ax->scatter_z_data(0);
        check(ax->scatter_data(0).y == y && sz.x == x && sz.z == z,
              "2d: scatter and scatter_z data read back");
        const BarData b0 = ax->bar_data(0), hb = ax->bar_data(1);
        check(b0.x == x && b0.height == y, "2d: bar data is centers and heights");
        check(hb.x.size() == 2 && hb.height == std::vector<double>{2.0, 3.0},
              "2d: hist() reads back as bin centers and counts");
        const HeatmapData h = ax->heatmap_data(0);
        bool rounded = h.data.size() == cells.size();
        for (std::size_t i = 0; rounded && i < cells.size(); ++i)
            rounded = h.data[i] == static_cast<double>(static_cast<float>(cells[i]));
        check(rounded && h.rows == 2 && h.cols == 3 && range_is(h.xrange, 10.0, 40.0)
              && range_is(h.yrange, 0.0, 2.0),
              "2d: heatmap data reads back with its shape and extent, rounded to float");

        check(throws_out_of_range([&] { (void)ax->line_data(2); })
              && throws_out_of_range([&] { (void)ax->heatmap_data(1); }),
              "2d: an index past the count throws out_of_range");

        ax->cla();
        check(ax->title().empty() && ax->line_count() == 0 && ax->bar_count() == 0
              && range_is(ax->ylim(), -0.05, 1.05),
              "2d: cla() clears what reads back; an empty axes auto-scales to 0..1, padded");

        // A plane reads back its own objects; the parent's limits cover them.
        auto fig3 = Figure::create();
        auto ax3 = fig3->add_subplot3d(1, 1, 1);
        auto pl = ax3->plane(PlaneOrientation::XY, 0.5);
        pl->line(x, y);
        pl->heatmap(cells, 2, 3, {0.0, 3.0}, {0.0, 2.0});
        check(pl->line_count() == 1 && pl->heatmap_count() == 1 && pl->scatter_count() == 0
              && pl->line_data(0).x == x && pl->heatmap_data(0).cols == 3,
              "plane: its objects read back through the Plane2D");
        check(throws_out_of_range([&] { (void)pl->bar_data(0); }),
              "plane: an index past the count throws out_of_range");
        check(ax3->xlim().lo <= 0.0 && ax3->xlim().hi >= 10.0 && ax3->bar3d_count() == 0,
              "plane: the parent's auto limits cover the plane's data, and it has no 3D objects of its own");
    }

    void test_read_back_3d() {
        std::printf("\n[read-back: Axes3D]\n");

        auto fig = Figure::create();
        auto ax = fig->add_subplot3d(1, 1, 1);
        ax->set_title("T").set_xtitle("x").set_ytitle("y").set_ztitle("z");
        check(ax->title() == "T" && ax->xtitle() == "x" && ax->ytitle() == "y" && ax->ztitle() == "z",
              "3d: titles read back as set");
        check(range_is(ax->xlim(), 0.0, 1.0) && range_is(ax->zlim(), 0.0, 1.0),
              "3d: an empty axes reads back its declared 0..1");

        const std::vector<double> u{0.0, 1.0}, v{0.0, 1.0, 2.0};
        const std::vector<double> hs{0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
        ax->surface(PlaneOrientation::XY, u, v, hs);
        check(range_is(ax->zlim(), -0.25, 5.25) && range_is(ax->xlim(), -0.05, 1.05),
              "3d: auto limits are the padded data range per axis");
        ax->set_zlim(-1.0, 1.0);
        check(range_is(ax->zlim(), -1.0, 1.0), "3d: explicit limits come back as set");

        const std::vector<double> bottoms(6, -1.0);
        ax->bar3d(PlaneOrientation::YZ, u, v, hs);
        ax->bar3d(PlaneOrientation::XY, u, v, hs, bottoms);
        const std::vector<double> px{0.0, 1.0, 0.0}, py{0.0, 0.0, 1.0}, pz{1.0, 2.0, 3.0};
        const std::vector<double> colors{0.1, 0.5, 0.9};
        const std::vector<std::uint32_t> tri{0, 1, 2};
        ax->scatter3d(px, py, pz);
        ax->scatter3d(px, py, pz, colors);
        ax->line3d(px, py, pz);
        ax->surface_tri(px, py, pz, tri);

        check(ax->surface_count() == 1 && ax->bar3d_count() == 2 && ax->scatter3d_count() == 2
              && ax->line3d_count() == 1 && ax->surface_tri_count() == 1,
              "3d: counts per kind");
        const SurfaceData s = ax->surface_data(0);
        check(s.orient == PlaneOrientation::XY && s.u == u && s.v == v && s.heights == hs,
              "3d: surface data reads back with its orientation");
        const Bar3DData b0 = ax->bar3d_data(0), b1 = ax->bar3d_data(1);
        check(b0.orient == PlaneOrientation::YZ && b0.heights == hs && b0.bottoms.empty()
              && b1.bottoms == bottoms,
              "3d: bar3d data reads back, bottoms only when given");
        check(ax->scatter3d_data(0).colors.empty() && ax->scatter3d_data(1).colors == colors
              && ax->scatter3d_data(1).z == pz,
              "3d: scatter3d data reads back, colors only when colormapped");
        const SurfaceTriData m = ax->surface_tri_data(0);
        check(ax->line3d_data(0).x == px && m.tri == tri && m.z == pz && m.colors.empty(),
              "3d: line3d and surface_tri data read back");
        check(throws_out_of_range([&] { (void)ax->line3d_data(1); })
              && throws_out_of_range([&] { (void)ax->surface_data(1); }),
              "3d: an index past the count throws out_of_range");
    }

    void test_title_journal() {
        std::printf("\n[read-back: the title journal]\n");

        FigureEditBox box;
        box.update(1, [](AxesEdit& e) { e.title = "a"; e.title_seen.title = 5; });
        box.load_and_clear_journaled();
        box.update(1, [](AxesEdit& e) {
            e.title = "ab"; e.title_seen.title = 5;
            e.xtitle = "x"; e.title_seen.xtitle = 6;
        });
        box.load_and_clear_journaled();
        box.update3d(2, [](AxesEdit3D& e) { e.ztitle = "z"; e.title_seen.ztitle = 7; });
        box.load_and_clear_journaled();
        auto j = box.take_journal();
        bool ok = j && j->per_axes.empty() && j->titles.size() == 2;
        if (ok) {
            const TitleEdits& t1 = j->titles[0].second;
            const TitleEdits& t2 = j->titles[1].second;
            ok = j->titles[0].first == 1 && t1.title == "ab" && t1.xtitle == "x" && !t1.ytitle
                 && t1.title_seen.title == 5 && t1.title_seen.xtitle == 6
                 && j->titles[1].first == 2 && t2.ztitle == "z" && t2.title_seen.ztitle == 7;
        }
        check(ok, "journal: titles from both lanes are journaled per slot, latest text with its own stamp");
        check(!box.take_journal().has_value(), "journal: taking it is destructive");

        box.update(1, [](AxesEdit& e) { e.grid_enabled = true; });
        box.load_and_clear_journaled();
        {
            auto sj = box.take_journal();
            check(sj && sj->titles.empty() && sj->styles.size() == 1 && sj->styles[0].second.grid_enabled,
                  "journal: a style edit goes to the style lane, not the title lane");
        }

        // The stamp rule, on the snapshot types (Axes::Impl shares the members).
        RenderSnapshot s;
        s.title = "caller";
        s.title_stamps.title = 10;
        TitleEdits typed;
        typed.title = "typed";
        typed.title_seen.title = 10;
        apply_title_edits(s, typed);
        check(s.title == "typed", "stamps: an edit typed over the current title applies");
        s.title = "newer";
        s.title_stamps.title = 11;
        apply_title_edits(s, typed);
        check(s.title == "newer", "stamps: a set_title() made since the edit was typed wins");
        s.title_stamps = {};
        apply_title_edits(s, typed);
        check(s.title == "typed", "stamps: after cla() the typed title applies again");

        s.xtitle = "keep";
        s.title_stamps.xtitle = 12;
        typed.xtitle = "typed x";
        typed.title_seen.xtitle = 10;
        s.title_stamps.title = 3;
        apply_title_edits(s, typed);
        check(s.xtitle == "keep" && s.title == "typed", "stamps: each title is judged by its own stamp");

        RenderSnapshot3D s3;
        s3.ztitle = "caller";
        s3.title_stamps.ztitle = 20;
        AxesEdit3D e3;
        e3.ztitle = "z typed";
        e3.title_seen.ztitle = 19;
        apply_axes3d_edit(s3, e3);
        check(s3.ztitle == "caller", "stamps: the 3D ztitle follows the same rule");
        e3.title_seen = TitleStamps::any();
        apply_axes3d_edit(s3, e3);
        check(s3.ztitle == "z typed", "stamps: an edit built without a snapshot applies over anything");
    }

    // A navigated 3D camera survives refresh(): journaled (latest only), stamped
    // against the camera setters, and the panel follows a camera the program set.
    void test_camera_journal() {
        std::printf("\n[read-back: the camera journal]\n");

        FigureEditBox box;
        Camera3D a, b;
        a.azimuth = 10.0;
        b.azimuth = 20.0;
        box.update3d(3, [&](AxesEdit3D& e) { e.camera = a; e.camera_seen = 4; });
        box.load_and_clear_journaled();
        box.update3d(3, [&](AxesEdit3D& e) { e.camera = b; e.camera_seen = 5; });
        box.load_and_clear_journaled();
        auto j = box.take_journal();
        check(j && j->cameras.size() == 1 && j->cameras[0].first == 3
              && j->cameras[0].second.camera.azimuth == 20.0 && j->cameras[0].second.seen == 5
              && j->per_axes.empty() && j->titles.empty(),
              "journal: a navigated camera is journaled, latest value only, with its stamp");
        check(!box.take_journal().has_value(), "journal: taking it is destructive");

        // The stamp rule, on the snapshot (Axes3D::Impl shares the members).
        RenderSnapshot3D s;
        s.camera.azimuth = -60.0;
        s.camera_stamp = 7;
        AxesEdit3D e;
        e.camera = b;
        e.camera_seen = 7;
        apply_axes3d_edit(s, e);
        check(s.camera.azimuth == 20.0, "stamps: a drag from the current camera applies");
        s.camera.azimuth = 45.0;
        s.camera_stamp = 8;
        apply_axes3d_edit(s, e);
        check(s.camera.azimuth == 45.0, "stamps: a camera set since the drag began wins");
        s.camera_stamp = 0;
        apply_axes3d_edit(s, e);
        check(s.camera.azimuth == 20.0, "stamps: after cla() the navigated camera applies again");

        // The panel's navigation copy: kept across a refresh that left the camera
        // alone, re-seeded when the program set it.
        FigureSnapshot fs;
        RenderSnapshot3D r;
        r.camera.azimuth = 30.0;
        r.camera_stamp = 9;
        fs.axes.push_back({{1, 1, 1}, r});
        PanelState st;
        sync_selected_slot(st, fs);
        check(st.camera_local.azimuth == 30.0, "panel: navigation starts from the snapshot's camera");
        st.camera_local.azimuth = 55.0;   // dragged, not yet folded in
        sync_selected_slot(st, fs);
        check(st.camera_local.azimuth == 55.0,
              "panel: a snapshot with the same camera stamp keeps the dragged camera");
        fs.axes[0].snap3d()->camera.azimuth = 90.0;
        fs.axes[0].snap3d()->camera_stamp = 10;
        sync_selected_slot(st, fs);
        check(st.camera_local.azimuth == 90.0,
              "panel: a camera the program set is followed, so the next drag starts from it");
    }

    namespace {
        template <typename F>
        bool throws_invalid(F&& f) {
            try { f(); } catch (const std::invalid_argument&) { return true; } catch (...) {}
            return false;
        }
    } // namespace

    // set_*_data() on Axes and Plane2D: a round trip with the read-back structs,
    // validated as plotting, nothing changed when it throws.
    void test_set_data_2d() {
        std::printf("\n[set_data: Axes and Plane2D]\n");

        auto fig = Figure::create();
        auto ax = fig->axes();
        const std::vector<double> x{0.0, 1.0, 2.0}, y{1.0, 2.0, 3.0};
        ax->set_title("kept").line(x, y).set_xlim(-1.0, 5.0);

        LineData d = ax->line_data(0);
        d.y[1] = 20.0;
        ax->set_line_data(0, d);
        check(ax->line_data(0).y == std::vector<double>{1.0, 20.0, 3.0} && ax->title() == "kept"
              && range_is(ax->xlim(), -1.0, 5.0),
              "2d: set_line_data replaces the data and leaves titles and limits alone");
        ax->set_line_data(0, {{0.0, 1.0}, {5.0, 6.0}});
        check(ax->line_data(0).x.size() == 2 && ax->line_count() == 1,
              "2d: the point count may change; the object is replaced in place");
        check(throws_invalid([&] { ax->set_line_data(0, {{0.0, 1.0}, {5.0}}); })
              && ax->line_data(0).y == std::vector<double>{5.0, 6.0},
              "2d: mismatched lengths throw invalid_argument and change nothing");
        check(throws_out_of_range([&] { ax->set_line_data(1, d); })
              && throws_out_of_range([&] { ax->set_heatmap_data(0, {}); }),
              "2d: an index past the count throws out_of_range");

        // The span overload: a slice of a caller's buffer, as a matrix column.
        const double buf[] = {0.0, 1.0, 2.0, 30.0, 40.0, 50.0};
        const std::span<const double> all(buf);
        ax->set_line_data(0, all.first(3), all.last(3));
        check(ax->line_data(0).x == x && ax->line_data(0).y == std::vector<double>{30.0, 40.0, 50.0},
              "2d: set_line_data from spans copies the slices in");
        check(throws_invalid([&] { ax->set_line_data(0, all.first(2), all.last(3)); })
              && ax->line_data(0).y == std::vector<double>{30.0, 40.0, 50.0}
              && throws_out_of_range([&] { ax->set_line_data(1, all.first(3), all.last(3)); }),
              "2d: the span overload validates as the LineData one");

        ax->scatter(x, y).scatter_z(x, y, y).bar(x, y);
        ax->set_scatter_data(0, {{4.0}, {5.0}});
        ax->set_scatter_z_data(0, {{4.0}, {5.0}, {6.0}});
        ax->set_bar_data(0, {{10.0, 20.0}, {1.0, 2.0}});
        check(ax->scatter_data(0).x == std::vector<double>{4.0}
              && ax->scatter_z_data(0).z == std::vector<double>{6.0}
              && ax->bar_data(0).x == std::vector<double>{10.0, 20.0},
              "2d: scatter, scatter_z and bar data are replaced");
        check(throws_invalid([&] { ax->set_scatter_z_data(0, {{1.0}, {1.0}, {}}); })
              && throws_invalid([&] { ax->set_bar_data(0, {{1.0}, {}}); }),
              "2d: each kind validates its lengths");

        // Span overloads of the other kinds, from slices of one buffer.
        ax->set_scatter_data(0, all.first(3), all.last(3));
        ax->set_scatter_z_data(0, all.first(3), all.first(3), all.last(3));
        ax->set_bar_data(0, all.first(2), all.last(2));
        check(ax->scatter_data(0).y == std::vector<double>{30.0, 40.0, 50.0}
              && ax->scatter_z_data(0).z == std::vector<double>{30.0, 40.0, 50.0}
              && ax->bar_data(0).x == std::vector<double>{0.0, 1.0}
              && ax->bar_data(0).height == std::vector<double>{40.0, 50.0},
              "2d: scatter, scatter_z and bar data from spans");
        check(throws_invalid([&] { ax->set_scatter_data(0, all.first(2), all.last(3)); })
              && throws_invalid([&] { ax->set_scatter_z_data(0, all.first(3), all.first(3), all.last(2)); })
              && throws_invalid([&] { ax->set_bar_data(0, all.first(1), all.last(2)); })
              && ax->bar_data(0).x == std::vector<double>{0.0, 1.0},
              "2d: the span overloads validate their lengths and change nothing");

        const std::vector<double> cells{1.0, 2.0, 3.0, 4.0};
        ax->imshow(cells, 2, 2);
        HeatmapData h = ax->heatmap_data(0);
        h.data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
        h.cols = 3;
        h.xrange = {0.0, 3.0};
        ax->set_heatmap_data(0, h);
        check(ax->heatmap_data(0).cols == 3 && ax->heatmap_data(0).data[5] == 6.0,
              "2d: a heatmap may change shape");
        h.rows = 3;
        check(throws_invalid([&] { ax->set_heatmap_data(0, h); })
              && ax->heatmap_data(0).rows == 2,
              "2d: heatmap data too small for rows x cols throws and changes nothing");
        h.rows = 2;
        h.yrange = {1.0, 1.0};
        check(throws_invalid([&] { ax->set_heatmap_data(0, h); }),
              "2d: a degenerate heatmap range throws, as heatmap() does");
        ax->set_heatmap_data(0, all, 3, 2, {0.0, 2.0}, {0.0, 3.0});
        check(ax->heatmap_data(0).rows == 3 && ax->heatmap_data(0).data[3] == 30.0
              && ax->heatmap_data(0).yrange.hi == 3.0,
              "2d: heatmap data from a span, in heatmap()'s argument order");
        check(throws_invalid([&] { ax->set_heatmap_data(0, all, 4, 2, {0.0, 2.0}, {0.0, 3.0}); })
              && ax->heatmap_data(0).rows == 3,
              "2d: a span too small for rows x cols throws and changes nothing");

        auto fig3 = Figure::create();
        auto plane = fig3->add_subplot3d(1, 1, 1)->plane(PlaneOrientation::XY, 0.0);
        plane->line(x, y);
        plane->set_line_data(0, {{7.0, 8.0}, {9.0, 10.0}});
        check(plane->line_data(0).x == std::vector<double>{7.0, 8.0},
              "plane: set_line_data replaces a plane's line");
        plane->set_line_data(0, all.first(3), all.last(3));
        check(plane->line_data(0).y == std::vector<double>{30.0, 40.0, 50.0},
              "plane: set_line_data from spans");
        check(throws_out_of_range([&] { plane->set_bar_data(0, {}); }),
              "plane: an index past the count throws out_of_range");
        plane->scatter(x, y).scatter_z(x, y, y).bar(x, y).imshow(cells, 2, 2);
        plane->set_scatter_data(0, all.first(3), all.last(3))
                .set_scatter_z_data(0, all.first(3), all.first(3), all.last(3))
                .set_bar_data(0, all.first(3), all.last(3))
                .set_heatmap_data(0, all, 2, 3, {0.0, 3.0}, {0.0, 2.0});
        check(plane->scatter_data(0).y[0] == 30.0 && plane->scatter_z_data(0).z[2] == 50.0
              && plane->bar_data(0).height[1] == 40.0 && plane->heatmap_data(0).cols == 3,
              "plane: every set_*_data span overload");
    }

    void test_set_data_3d() {
        std::printf("\n[set_data: Axes3D]\n");

        auto fig = Figure::create();
        auto ax = fig->add_subplot3d(1, 1, 1);
        const std::vector<double> u{0.0, 1.0}, v{0.0, 1.0, 2.0};
        const std::vector<double> hs{0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
        const std::vector<double> px{0.0, 1.0, 0.0}, py{0.0, 0.0, 1.0}, pz{1.0, 2.0, 3.0};
        const std::vector<std::uint32_t> tri{0, 1, 2};
        ax->bar3d(PlaneOrientation::XY, u, v, hs).surface(PlaneOrientation::XY, u, v, hs);
        ax->surface_tri(px, py, pz, tri).scatter3d(px, py, pz).line3d(px, py, pz);

        Bar3DData b = ax->bar3d_data(0);
        b.heights[0] = 9.0;
        b.orient = PlaneOrientation::YZ;
        ax->set_bar3d_data(0, b);
        check(ax->bar3d_data(0).heights[0] == 9.0 && ax->bar3d_data(0).orient == PlaneOrientation::YZ,
              "3d: bar3d data and orientation are replaced");
        b.bottoms = {1.0};
        check(throws_invalid([&] { ax->set_bar3d_data(0, b); })
              && ax->bar3d_data(0).bottoms.empty(),
              "3d: bar3d bottoms of the wrong length throw and change nothing");

        SurfaceData s = ax->surface_data(0);
        s.u = {0.0};
        s.heights = {0.0, 1.0, 2.0};
        check(throws_invalid([&] { ax->set_surface_data(0, s); }),
              "3d: a surface below 2 x 2 samples throws, as surface() does");
        s = ax->surface_data(0);
        s.heights.back() = -1.0;
        ax->set_surface_data(0, s);
        check(ax->surface_data(0).heights.back() == -1.0, "3d: surface heights are replaced");

        SurfaceTriData m = ax->surface_tri_data(0);
        m.z = {5.0, 5.0, 5.0};
        m.colors = {0.0, 0.5, 1.0};
        ax->set_surface_tri_data(0, m);
        check(ax->surface_tri_data(0).colors.size() == 3 && ax->surface_tri_data(0).tri == tri,
              "3d: surface_tri data is replaced, a flat mesh may become colormapped");
        m.tri = {0, 1, 3};
        check(throws_invalid([&] { ax->set_surface_tri_data(0, m); }),
              "3d: a tri index past the vertex count throws");

        ax->set_scatter3d_data(0, {{1.0}, {2.0}, {3.0}, {}});
        check(ax->scatter3d_data(0).x.size() == 1, "3d: scatter3d data is replaced");
        check(throws_invalid([&] { ax->set_scatter3d_data(0, {{}, {}, {}, {}}); }),
              "3d: an empty scatter3d throws, as scatter3d() does");
        check(throws_invalid([&] { ax->set_line3d_data(0, {{1.0}, {2.0}, {3.0}, {}}); })
              && ax->line3d_data(0).x == px,
              "3d: a one-point path throws, as line3d() does, and changes nothing");
        const double nan = std::nan("");
        check(throws_invalid([&] { ax->set_line3d_data(0, {{0.0, nan}, {0.0, 1.0}, {0.0, 1.0}, {}}); }),
              "3d: non-finite values throw");
        check(throws_out_of_range([&] { ax->set_surface_data(1, s); }),
              "3d: an index past the count throws out_of_range");

        // Span overloads, from slices of one buffer.
        const double buf[] = {0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 7.0, 8.0, 9.0};
        const std::span<const double> all(buf);
        const auto sx = all.first(3), sy = all.subspan(3, 3), sz = all.last(3);
        ax->set_bar3d_data(0, PlaneOrientation::ZX, u, v, all.last(6));
        check(ax->bar3d_data(0).heights[0] == 0.0 && ax->bar3d_data(0).heights[5] == 9.0
              && ax->bar3d_data(0).orient == PlaneOrientation::ZX && ax->bar3d_data(0).bottoms.empty(),
              "3d: bar3d data from spans; no bottoms means none");
        ax->set_bar3d_data(0, PlaneOrientation::XY, u, v, hs, all.first(6));
        check(ax->bar3d_data(0).bottoms.size() == 6, "3d: bar3d bottoms from a span");
        ax->set_surface_data(0, PlaneOrientation::YZ, u, v, all.last(6));
        check(ax->surface_data(0).heights[5] == 9.0 && ax->surface_data(0).orient == PlaneOrientation::YZ,
              "3d: surface data from spans");
        check(throws_invalid([&] { ax->set_surface_data(0, PlaneOrientation::XY, u, v, all.last(5)); })
              && ax->surface_data(0).heights[5] == 9.0,
              "3d: a surface span of the wrong size throws and changes nothing");

        ax->set_surface_tri_data(0, sx, sy, sz, tri, all.last(3));
        check(ax->surface_tri_data(0).z == std::vector<double>{7.0, 8.0, 9.0},
              "3d: surface_tri data from spans");
        check(ax->surface_tri_data(0).colors.size() == 3, "3d: surface_tri colors from a span");
        ax->set_surface_tri_data(0, sx, sy, sz, tri);
        check(ax->surface_tri_data(0).colors.empty(),
              "3d: surface_tri from spans without colors is flat, as surface_tri() plots it");
        const std::uint32_t bad_tri[] = {0, 1, 3};
        check(throws_invalid([&] { ax->set_surface_tri_data(0, sx, sy, sz, bad_tri); }),
              "3d: a span tri index past the vertex count throws");

        ax->set_scatter3d_data(0, sx, sy, sz, all.first(3));
        check(ax->scatter3d_data(0).z == std::vector<double>{7.0, 8.0, 9.0}
              && ax->scatter3d_data(0).colors.size() == 3,
              "3d: scatter3d data and colors from spans");
        ax->set_scatter3d_data(0, sx, sy, sz);
        check(ax->scatter3d_data(0).colors.empty(), "3d: scatter3d from spans without colors is flat");
        ax->set_line3d_data(0, sx, sy, sz, all.last(3));
        check(ax->line3d_data(0).x == std::vector<double>{0.0, 1.0, 0.0}
              && ax->line3d_data(0).colors.size() == 3,
              "3d: line3d data and colors from spans");
        check(throws_invalid([&] { ax->set_line3d_data(0, sx, sy, all.last(2)); })
              && ax->line3d_data(0).colors.size() == 3,
              "3d: line3d spans of mismatched length throw and change nothing");
        ax->set_line3d_data(0, sx, sy, sz);
        check(ax->line3d_data(0).colors.empty(), "3d: line3d from spans without colors is flat");
    }

    // What set_*_data() keeps: error bars and hint_labels while the count is
    // unchanged; a fresh data_stamp always.
    void test_set_data_keep_aligned() {
        std::printf("\n[set_data: aligned arrays and stamps]\n");

        LinePlot p;
        p.x = std::vector<double>{0.0, 1.0};
        p.err.y_cap_lo = std::vector<double>{0.1, 0.2};
        p.opts.hint_labels = {"a", "b"};
        p.data_stamp = 3;
        read_back::keep_aligned(p, true);
        check(p.err.y_cap_lo.size() == 2 && p.opts.hint_labels.size() == 2 && p.data_stamp > 3,
              "keep: the same count keeps error bars and labels, and takes a new stamp");
        const unsigned long long first = p.data_stamp;
        read_back::keep_aligned(p, false);
        check(p.err.y_cap_lo.empty() && p.opts.hint_labels.empty() && p.data_stamp > first,
              "keep: a new count drops them");

        Bar3DPlot b;
        b.opts.hint_labels = {"a"};
        read_back::keep_aligned(b, false);
        check(b.opts.hint_labels.empty(), "keep: kinds without error bars drop just the labels");
    }

    // A Data-panel op carries the data_stamp it was made over; a plot re-plotted
    // or given new data since drops it.
    void test_data_op_stamps() {
        std::printf("\n[set_data: Data-panel op stamps]\n");

        RenderSnapshot s;
        LinePlot lp;
        lp.x = std::vector<double>{0.0, 1.0};
        lp.y = std::vector<double>{0.0, 1.0};
        lp.data_stamp = 5;
        s.lines.push_back(lp);
        PlotCellEdit cell{PlotKind::Line, 0, 1, 0, 42.0};
        cell.seen = 5;
        apply_plot_data_ops(s, {cell});
        check(s.lines[0].y[0] == 42.0, "stamps: an op made over the current data applies");
        s.lines[0].data_stamp = 6;
        cell.value = 7.0;
        PlotRowEdit row{PlotRowEdit::Op::Insert, PlotKind::Line, 0, 2};
        row.seen = 5;
        apply_plot_data_ops(s, {cell, row});
        check(s.lines[0].y[0] == 42.0 && s.lines[0].x.size() == 2,
              "stamps: cell and row ops made before the data was replaced are dropped");
        cell.seen = ~0ull;
        apply_plot_data_ops(s, {cell});
        check(s.lines[0].y[0] == 7.0, "stamps: an op built without a snapshot applies over anything");

        RenderSnapshot3D s3;
        Bar3DPlot b;
        b.u = std::vector<double>{0.0, 1.0};
        b.v = std::vector<double>{0.0};
        b.heights = std::vector<double>{1.0, 2.0};
        b.data_stamp = 9;
        s3.bars3d.push_back(b);
        BarWidthEdit w{0, 0.25, -1, PlotKind::Bar3D, 0};
        w.seen = 8;
        MatrixLineEdit grow{MatrixLineEdit::Op::Insert, MatrixLineEdit::Axis::Row, 0, 2, -1,
                            PlotKind::Bar3D};
        grow.seen = 8;
        apply_plot_data_ops(s3, {PlotDataOp{w}, PlotDataOp{grow}});
        check(s3.bars3d[0].u_width == 1.0 && s3.bars3d[0].u.size() == 2,
              "stamps: the 3D kinds follow the same rule");

        // The Data panel stamps its ops from the table it drew.
        const std::vector<PlotDataTable> tables = collect_plot_data_tables(s);
        check(tables.size() == 1 && tables[0].data_stamp == 6,
              "panel: a table carries its plot's data_stamp");
    }

    // Pan/zoom and the Limits fields survive refresh(): journaled (latest
    // only), stamped per axis against set_xlim()/set_ylim()/set_zlim().
    void test_limit_journal() {
        std::printf("\n[limits: the limit journal]\n");

        FigureEditBox box;
        box.update(1, [](AxesEdit& e) {
            e.xmin = 0.0; e.xmax = 1.0; e.xlim_auto = false;
            e.ymin = 2.0; e.ymax = 3.0; e.ylim_auto = false;
            e.lim_seen = {4, 5, 0};
        });
        box.load_and_clear_journaled();
        box.update(1, [](AxesEdit& e) {
            e.xmin = 0.5; e.xmax = 1.5; e.xlim_auto = false;
            e.lim_seen = {6, 7, 0};
        });
        box.load_and_clear_journaled();
        box.update3d(2, [](AxesEdit3D& e) {
            e.zmin = -1.0; e.zmax = 1.0; e.zlim_auto = false;
            e.lim_seen.z = 8;
        });
        box.load_and_clear_journaled();
        auto j = box.take_journal();
        bool ok = j && j->limits.size() == 2 && j->per_axes.empty() && j->titles.empty();
        if (ok) {
            const LimitEdits& l1 = j->limits[0].second;
            const LimitEdits& l2 = j->limits[1].second;
            ok = j->limits[0].first == 1 && l1.xmin == 0.5 && l1.xmax == 1.5 && l1.ymin == 2.0
                 && l1.lim_seen.x == 6 && l1.lim_seen.y == 5
                 && j->limits[1].first == 2 && l2.zmin == -1.0 && l2.lim_seen.z == 8 && !l2.xmin;
        }
        check(ok, "journal: limits from both lanes are journaled per slot, each axis with its own stamp");
        check(!box.take_journal().has_value(), "journal: taking it is destructive");

        // The stamp rule, on the snapshot (Axes::Impl shares the members).
        RenderSnapshot s;
        s.limit_stamps = {10, 10, 0};
        LimitEdits panned;
        panned.xmin = -2.0; panned.xmax = 2.0; panned.xlim_auto = false;
        panned.ymin = -3.0; panned.ymax = 3.0; panned.ylim_auto = false;
        panned.lim_seen = {10, 10, 0};
        apply_limit_edits(s, panned);
        check(s.xmin == -2.0 && s.ymax == 3.0 && !s.xlim_auto,
              "stamps: a pan over the current limits applies");
        s.xmin = 7.0; s.xmax = 8.0;
        s.limit_stamps.x = 11;
        s.ymin = 0.0;
        apply_limit_edits(s, panned);
        check(s.xmin == 7.0 && s.ymin == -3.0,
              "stamps: an axis the program set since wins; the other axis still takes the pan");
        s.limit_stamps = {};
        s.xlim_auto = true;
        apply_limit_edits(s, panned);
        check(s.xmin == -2.0 && !s.xlim_auto, "stamps: after cla() the pan applies again");

        RenderSnapshot3D s3;
        s3.limit_stamps.z = 20;
        AxesEdit3D e3;
        e3.zmin = -5.0; e3.zmax = 5.0; e3.zlim_auto = false;
        e3.lim_seen.z = 19;
        apply_axes3d_edit(s3, e3);
        check(s3.zmin == 0.0 && s3.zlim_auto, "stamps: 3D limits follow the same rule");
        e3.lim_seen = LimitStamps::any();
        apply_axes3d_edit(s3, e3);
        check(s3.zmin == -5.0, "stamps: an edit built without a snapshot applies over anything");

        // The panel's Limits fields: kept while the stamps hold, re-seeded per
        // axis when the program sets one.
        FigureSnapshot fs;
        RenderSnapshot r;
        r.xmin = 1.0; r.xmax = 2.0; r.xlim_auto = false;
        r.ymin = 3.0; r.ymax = 4.0; r.ylim_auto = false;
        r.limit_stamps = {1, 1, 0};
        fs.axes.push_back({{1, 1, 1}, r});
        PanelState st;
        sync_selected_slot(st, fs);
        check(st.xmin_local == 1.0 && st.ymin_local == 3.0, "panel: the fields seed from the snapshot");
        st.xmin_local = 1.5;
        st.ymin_local = 3.5;
        fs.axes[0].snap2d()->xmin = 9.0;
        fs.axes[0].snap2d()->limit_stamps.x = 2;
        sync_selected_slot(st, fs);
        check(st.xmin_local == 9.0 && st.ymin_local == 3.5,
              "panel: an axis the program set is re-seeded; the other keeps its edit");
    }

    // Every other panel edit survives refresh() too: journaled as its latest
    // value, stamped per setter group from the snapshot the panel drew, and
    // guarded against cla() reusing object and plane indices.
    void test_style_journal() {
        std::printf("\n[styles: the style journal]\n");

        // The latest value per field, per slot and lane.
        FigureEditBox box;
        AxesStyle a, b;
        a.tick_length = 3.0f;
        b.tick_length = 9.0f;
        LineOptions red, blue;
        red.color = Color::Red;
        blue.color = Color::Blue;
        box.update(1, [&](AxesEdit& e) {
            e.axes_style = a;
            e.grid_enabled = true;
            e.plot_styles.push_back({0, -1, red});
        });
        box.load_and_clear_journaled();
        box.update(1, [&](AxesEdit& e) {
            e.axes_style = b;
            e.legend_enabled = true;
            e.plot_styles.push_back({0, -1, blue});
            e.plot_styles.push_back({1, -1, red});
        });
        box.update3d(2, [](AxesEdit3D& e) {
            e.box_style = Box3DStyle{};
            e.planes.push_back({0, PlaneOrientation::YZ, std::nullopt, std::nullopt});
        });
        box.load_and_clear_journaled();
        box.update3d(2, [](AxesEdit3D& e) {
            e.planes.push_back({0, std::nullopt, 0.5, std::nullopt});
            e.bars3d.push_back({0, Bar3DOptions{}});
        });
        box.update_figure([](FigureEdits& f) { f.suptitle = "sup"; f.margins = FigureMargins{}; });
        box.load_and_clear_journaled();
        auto j = box.take_journal();
        bool ok = j && j->styles.size() == 1 && j->styles3d.size() == 1 && j->titles.empty()
                  && j->limits.empty() && j->per_axes.empty();
        if (ok) {
            const AxesEdit& s = j->styles[0].second;
            ok = s.axes_style && s.axes_style->tick_length == 9.0f && s.grid_enabled
                 && s.legend_enabled && s.plot_styles.size() == 2
                 && std::get<LineOptions>(s.plot_styles[0].opts).color.b == Color::Blue.b;
        }
        check(ok, "journal: 2D styles merge to the latest value per field, plot styles per object");
        ok = j && !j->styles3d.empty();
        if (ok) {
            const AxesEdit3D& s = j->styles3d[0].second;
            ok = s.box_style && s.planes.size() == 1 && s.planes[0].orient == PlaneOrientation::YZ
                 && s.planes[0].offset == 0.5 && s.bars3d.size() == 1;
        }
        check(ok, "journal: 3D box, planes (field by field) and object options are journaled");
        check(j && j->figure.suptitle == "sup" && j->figure.margins && j->figure.per_axes.empty(),
              "journal: the suptitle and margins are journaled");

        // The drawn snapshot's stamps land on every pushed edit.
        auto fs = std::make_shared<FigureSnapshot>();
        RenderSnapshot r2;
        r2.style_stamps.grid = 5;
        r2.style_stamps.cleared = 3;
        RenderSnapshot3D r3;
        r3.style_stamps.box = 7;
        r3.style_stamps.cleared = 4;
        PlaneSnapshot plane;
        plane.placement_stamp = 8;
        plane.sheet.style_stamps.cleared = 9;
        r3.planes.push_back(plane);
        fs->axes.push_back({{1, 2, 1}, r2});
        fs->axes.push_back({{1, 2, 2}, r3});
        fs->stamps.margins = 6;
        box.set_drawn(fs);
        box.update(1, [&](AxesEdit& e) { e.grid_enabled = false; e.plot_styles.push_back({0, -1, red}); });
        box.update3d(2, [&](AxesEdit3D& e) {
            e.planes.push_back({0, std::nullopt, 1.0, std::nullopt});
            e.plot_styles.push_back({0, 0, red});
            e.surfaces.push_back({0, SurfaceOptions{}});
        });
        box.update_figure([](FigureEdits& f) { f.margins = FigureMargins{}; });
        auto p = box.load_and_clear_journaled();
        ok = p && p->per_axes.size() == 1 && p->per_axes3d.size() == 1;
        if (ok) {
            const AxesEdit& e2 = p->per_axes[0].second;
            const AxesEdit3D& e3 = p->per_axes3d[0].second;
            ok = e2.style_seen.grid == 5 && e2.plot_styles[0].seen == 3
                 && e3.style_seen.box == 7 && e3.planes[0].seen == 8
                 && e3.plot_styles[0].seen == 9 && e3.surfaces[0].seen == 4
                 && p->fig_seen.margins == 6;
        }
        check(ok, "stamps: edits record the drawn snapshot's group, cleared, plane and figure stamps");

        // The stamp rule per group, on the snapshot types.
        RenderSnapshot s;
        s.style_stamps.grid = 10;
        AxesEdit e;
        e.grid_enabled = true;
        e.axes_style = b;
        e.style_seen.grid = 10;
        e.style_seen.style = 0;
        apply_axes_edit(s, e);
        check(s.grid_enabled && s.axes_style.tick_length == 9.0f,
              "stamps: a style edit over the current setters applies");
        s.grid_enabled = false;
        s.style_stamps.grid = 11;
        s.style_stamps.style = 11;
        s.axes_style = a;
        apply_axes_edit(s, e);
        check(!s.grid_enabled && s.axes_style.tick_length == 3.0f,
              "stamps: grid() or set_axes_style() made since wins, group by group");
        s.style_stamps = {};
        apply_axes_edit(s, e);
        check(s.grid_enabled, "stamps: after cla() the group stamps are 0 and the edit applies again");

        // Object- and plane-addressed edits die with a cla().
        LinePlot lp;
        s.lines.push_back(lp);
        s.style_stamps.cleared = 20;
        s.lines[0].opts.linewidth = 1.0f;
        LineOptions thick;
        thick.linewidth = 4.0f;
        PlotStyleEdit ps{0, -1, thick};
        ps.seen = 19;
        apply_plot_style_edits(s, {ps});
        check(s.lines[0].opts.linewidth == 1.0f,
              "cleared: a plot style edit made before a cla() is dropped");
        ps.seen = 20;
        apply_plot_style_edits(s, {ps});
        check(s.lines[0].opts.linewidth == 4.0f, "cleared: one made after it applies");

        RenderSnapshot3D s3;
        s3.style_stamps.cleared = 30;
        s3.bars3d.push_back(Bar3DPlot{});
        s3.planes.push_back(PlaneSnapshot{});
        s3.planes[0].placement_stamp = 31;
        AxesEdit3D e3;
        Bar3DOptions wide;
        wide.width = 0.25f;
        e3.bars3d.push_back({0, wide, 29});
        e3.planes.push_back({0, std::nullopt, 2.0, std::nullopt, 30});
        apply_axes3d_edit(s3, e3);
        check(s3.bars3d[0].opts.width != 0.25f && s3.planes[0].offset == 0.0,
              "cleared: a 3D object edit before a cla(), and a plane edit before set_offset(), drop");
        e3.bars3d[0].seen = 30;
        e3.planes[0].seen = 31;
        apply_axes3d_edit(s3, e3);
        check(s3.bars3d[0].opts.width == 0.25f && s3.planes[0].offset == 2.0,
              "cleared: made over the current ones, both apply");

        // Figure level.
        FigureEdits f;
        f.suptitle = "typed";
        f.fig_seen.suptitle = 5;
        FigureStamps have;
        have.suptitle = 6;
        std::string sup = "program";
        SuptitleOptions so;
        FigureMargins m;
        float cg = 1.0f, rg = 1.0f;
        f.col_gap = 7.0f;
        apply_figure_edits(f, have, sup, so, m, cg, rg);
        check(sup == "program" && cg == 7.0f,
              "figure: a suptitle() made since wins; the gaps (no setter) always apply");
        have.suptitle = 5;
        apply_figure_edits(f, have, sup, so, m, cg, rg);
        check(sup == "typed", "figure: otherwise the typed suptitle applies");
    }
} // namespace lt
